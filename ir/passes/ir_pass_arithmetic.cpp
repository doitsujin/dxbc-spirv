#include <algorithm>

#include "ir_pass_arithmetic.h"

#include "../ir_utils.h"

#include "../../util/util_log.h"

namespace dxbc_spv::ir {

ArithmeticPass::ArithmeticPass(Builder& builder, const Options& options)
: m_builder(builder), m_options(options) {
  /* Scan float modes */
  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eSetFpMode) {
      if (iter->getType() == ScalarType::eF16) m_fp16Flags = iter->getFlags();
      if (iter->getType() == ScalarType::eF32) m_fp32Flags = iter->getFlags();
      if (iter->getType() == ScalarType::eF64) m_fp64Flags = iter->getFlags();
    }
  }
}


ArithmeticPass::~ArithmeticPass() {

}


void ArithmeticPass::runLowering() {
  lowerInstructions();
}


void ArithmeticPass::runLoweringPasses(Builder& builder, const Options& options) {
  ArithmeticPass(builder, options).runLowering();
}


void ArithmeticPass::lowerInstructions() {
  auto iter = m_builder.getCode().first;

  while (iter != m_builder.end()) {
    switch (iter->getOpCode()) {
      case OpCode::eFDot:
      case OpCode::eFDotLegacy: {
        if (m_options.lowerDot) {
          iter = lowerDot(iter);
          continue;
        }
      } break;

      default:
        break;
    }

    ++iter;
  }
}


void ArithmeticPass::performInitialTransforms() {
  auto iter = m_builder.getCode().first;

  while (iter != m_builder.end()) {
    switch (iter->getOpCode()) {
      case OpCode::eFAdd:
      case OpCode::eFSub: {
        iter = fuseMad(iter);
      } continue;

      default:
        break;
    }

    ++iter;
  }
}


Builder::iterator ArithmeticPass::lowerDot(Builder::iterator op) {
  const auto& srcA = m_builder.getOpForOperand(*op, 0u);
  const auto& srcB = m_builder.getOpForOperand(*op, 1u);

  dxbc_spv_assert(srcA.getType() == srcB.getType());

  /* Determine which opcodes to use */
  bool isLegacy = op->getOpCode() == OpCode::eFDotLegacy;

  auto mulOp = isLegacy ? OpCode::eFMulLegacy : OpCode::eFMul;
  auto madOp = isLegacy ? OpCode::eFMadLegacy : OpCode::eFMad;

  /* Mark the multiply-add chain as precise so that compilers don't screw around with
   * it, otherwise we run into rendering issues in e.g. Trails through Daybreak. */
  auto opFlags = op->getFlags() | OpFlag::ePrecise;

  Op result = Op(mulOp, op->getType()).setFlags(opFlags)
    .addOperand(extractFromVector(srcA.getDef(), 0u))
    .addOperand(extractFromVector(srcB.getDef(), 0u));

  for (uint32_t i = 1u; i < srcA.getType().getBaseType(0u).getVectorSize(); i++) {
    result = Op(madOp, op->getType()).setFlags(opFlags)
      .addOperand(extractFromVector(srcA.getDef(), i))
      .addOperand(extractFromVector(srcB.getDef(), i))
      .addOperand(m_builder.addBefore(op->getDef(), std::move(result)));
  }

  m_builder.rewriteOp(op->getDef(), std::move(result));
  return ++op;
}


Builder::iterator ArithmeticPass::fuseMad(Builder::iterator op) {
  if (getFpFlags(*op) & OpFlag::ePrecise)
    return ++op;

  /* Don't fuse if there is ambiguity since that would break
   * invariance if the order of the FAdd operands changed */
  auto canFuseA = canFuseMadOperand(op, 0u);
  auto canFuseB = canFuseMadOperand(op, 1u);

  if (canFuseA && canFuseB)
    return ++op;

  if (canFuseA)
    return fuseMadOperand(op, 0u);

  if (canFuseB)
    return fuseMadOperand(op, 1u);

  return ++op;
}


Builder::iterator ArithmeticPass::fuseMadOperand(Builder::iterator op, uint32_t operand) {
  const auto& mulOp = m_builder.getOpForOperand(*op, operand);

  bool isLegacy = mulOp.getOpCode() == OpCode::eFMulLegacy ||
                  mulOp.getOpCode() == OpCode::eFDotLegacy;

  bool negate = op->getOpCode() == OpCode::eFSub;

  auto factorA = m_builder.getOpForOperand(mulOp, 0u).getDef();
  auto factorB = m_builder.getOpForOperand(mulOp, 1u).getDef();

  if (negate) {
    factorA = m_builder.addBefore(op->getDef(),
      Op::FNeg(m_builder.getOp(factorA).getType(), factorA));
  }

  auto madOp = isLegacy ? OpCode::eFMadLegacy : OpCode::eFMad;

  auto fusedOp = Op(madOp, op->getType())
    .setFlags(op->getFlags())
    .addOperand(factorA)
    .addOperand(factorB)
    .addOperand(SsaDef(op->getOperand(operand ^ 1u)));

  m_builder.addAfter(op->getDef(), fusedOp);

  m_builder.rewriteOp(op->getDef(), std::move(fusedOp));

  removeIfUnused(m_builder, mulOp.getDef());
  return ++op;
}


bool ArithmeticPass::canFuseMadOperand(Builder::iterator op, uint32_t operand) const {
  const auto& mulOp = m_builder.getOpForOperand(*op, operand);

  if (!(getFpFlags(mulOp) & OpFlag::ePrecise))
    return mulOp.getOpCode() == OpCode::eFMul || mulOp.getOpCode() == OpCode::eFMulLegacy;

  return false;
}


SsaDef ArithmeticPass::extractFromVector(SsaDef vector, uint32_t component) {
  const auto& vectorOp = m_builder.getOp(vector);

  if (vectorOp.getOpCode() == OpCode::eCompositeConstruct)
    return SsaDef(vectorOp.getOperand(component));

  return m_builder.addAfter(vector, Op::CompositeExtract(
    vectorOp.getType().getSubType(0u), vector,
    m_builder.makeConstant(component)));
}


OpFlags ArithmeticPass::getFpFlags(const Op& op) const {
  dxbc_spv_assert(op.getType().isBasicType());

  auto flags = op.getFlags();
  auto type = op.getType().getBaseType(0u).getBaseType();

  if (type == ScalarType::eF16) flags |= m_fp16Flags;
  if (type == ScalarType::eF32) flags |= m_fp32Flags;
  if (type == ScalarType::eF64) flags |= m_fp64Flags;

  return flags;
}

}
