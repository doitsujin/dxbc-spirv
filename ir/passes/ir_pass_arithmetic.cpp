#include <algorithm>

#include "ir_pass_arithmetic.h"
#include "ir_pass_scalarize.h"

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


bool ArithmeticPass::runPass() {
  return false;
}


void ArithmeticPass::runLowering() {
  lowerInstructions();

  /* Some instructions operate on composites but
   * then get scalarized, fix that up immediately. */
  ir::ScalarizePass::runResolveRedundantCompositesPass(m_builder);
}


bool ArithmeticPass::runPass(Builder& builder, const Options& options) {
  return ArithmeticPass(builder, options).runPass();
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


SsaDef ArithmeticPass::extractFromVector(SsaDef vector, uint32_t component) {
  const auto& vectorOp = m_builder.getOp(vector);

  if (vectorOp.isUndef())
    return m_builder.makeUndef(vectorOp.getType().getSubType(component));

  if (vectorOp.isConstant()) {
    auto constant = Op(OpCode::eConstant, vectorOp.getType().getSubType(component))
      .addOperand(vectorOp.getOperand(component));
    return m_builder.add(std::move(constant));
  }

  if (vectorOp.getOpCode() == OpCode::eCompositeConstruct)
    return SsaDef(vectorOp.getOperand(component));

  return m_builder.addAfter(vector, Op::CompositeExtract(
    vectorOp.getType().getSubType(0u), vector,
    m_builder.makeConstant(component)));
}

}
