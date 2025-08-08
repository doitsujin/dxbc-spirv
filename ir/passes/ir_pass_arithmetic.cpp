#include <algorithm>
#include <cmath>

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
  bool progress = false;

  auto iter = m_builder.getCode().first;

  while (iter != m_builder.getCode().second) {
    bool status = false;
    auto next = iter;

    if (!status)
      std::tie(status, next) = constantFoldOp(iter);

    if (!status)
      std::tie(status, next) = reorderConstantOperandsOp(iter);

    if (status) {
      progress = true;
      iter = next;
    } else {
      iter++;
    }
  }

  return progress;
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


std::pair<bool, Builder::iterator> ArithmeticPass::reorderConstantOperandsCompareOp(Builder::iterator op) {
  /* If the op has exactly one constant operand and it is on the left,
   * flip the operation so that it is on the right. Subsequent passes
   * will assume that constant operands are always right where applicable. */
  const auto& a = m_builder.getOpForOperand(*op, 0u);
  const auto& b = m_builder.getOpForOperand(*op, 1u);

  if (!a.isConstant() || b.isConstant())
    return std::make_pair(false, ++op);

  static const std::array<std::pair<OpCode, OpCode>, 10u> s_opcodePairs = {{
    { OpCode::eFEq, OpCode::eFEq },
    { OpCode::eFNe, OpCode::eFNe },
    { OpCode::eFLt, OpCode::eFGt },
    { OpCode::eFLe, OpCode::eFGe },
    { OpCode::eIEq, OpCode::eIEq },
    { OpCode::eINe, OpCode::eINe },
    { OpCode::eSLt, OpCode::eSGt },
    { OpCode::eSLe, OpCode::eSGe },
    { OpCode::eULt, OpCode::eUGt },
    { OpCode::eULe, OpCode::eUGe },
  }};

  auto opCode = [op] {
    for (const auto& e : s_opcodePairs) {
      if (op->getOpCode() == e.first)
        return e.second;
      if (op->getOpCode() == e.second)
        return e.first;
    }

    dxbc_spv_unreachable();
    return op->getOpCode();
  } ();

  auto newOp = Op(opCode, op->getType())
    .setFlags(op->getFlags())
    .addOperand(b.getDef())
    .addOperand(a.getDef());

  m_builder.rewriteOp(op->getDef(), std::move(newOp));
  return std::make_pair(true, op);
}


std::pair<bool, Builder::iterator> ArithmeticPass::reorderConstantOperandsCommutativeOp(Builder::iterator op) {
  /* Only flip the first two operands around, this way we can
   * handle multiply-add instructions here as well. */
  const auto& a = m_builder.getOpForOperand(*op, 0u);
  const auto& b = m_builder.getOpForOperand(*op, 1u);

  if (!a.isConstant() || b.isConstant())
    return std::make_pair(false, ++op);

  auto newOp = *op;
  newOp.setOperand(0u, b.getDef());
  newOp.setOperand(1u, a.getDef());

  m_builder.rewriteOp(op->getDef(), std::move(newOp));
  return std::make_pair(true, op);
}


std::pair<bool, Builder::iterator> ArithmeticPass::reorderConstantOperandsOp(Builder::iterator op) {
  switch (op->getOpCode()) {
    case OpCode::eFEq:
    case OpCode::eFNe:
    case OpCode::eFLt:
    case OpCode::eFLe:
    case OpCode::eFGt:
    case OpCode::eFGe:
    case OpCode::eIEq:
    case OpCode::eINe:
    case OpCode::eSLt:
    case OpCode::eSLe:
    case OpCode::eSGt:
    case OpCode::eSGe:
    case OpCode::eULt:
    case OpCode::eULe:
    case OpCode::eUGt:
    case OpCode::eUGe:
      return reorderConstantOperandsCompareOp(op);

    case OpCode::eBAnd:
    case OpCode::eBOr:
    case OpCode::eBEq:
    case OpCode::eBNe:
    case OpCode::eFAdd:
    case OpCode::eFMul:
    case OpCode::eFMulLegacy:
    case OpCode::eFMad:
    case OpCode::eFMadLegacy:
    case OpCode::eFMin:
    case OpCode::eFMax:
    case OpCode::eIAnd:
    case OpCode::eIOr:
    case OpCode::eIXor:
    case OpCode::eIAdd:
    case OpCode::eIAddCarry:
    case OpCode::eIMul:
    case OpCode::eSMulExtended:
    case OpCode::eUMulExtended:
    case OpCode::eSMin:
    case OpCode::eSMax:
    case OpCode::eUMin:
    case OpCode::eUMax:
      return reorderConstantOperandsCommutativeOp(op);

    default:
      return std::make_pair(false, ++op);
  }
}


std::pair<bool, Builder::iterator> ArithmeticPass::constantFoldArithmeticOp(Builder::iterator op) {
  if (!allOperandsConstant(*op))
    return std::make_pair(false, ++op);

  Op constant(OpCode::eConstant, op->getType());

  for (uint32_t i = 0u; i < op->getType().getBaseType(0u).getVectorSize(); i++) {
    Operand operand = [this, op, i] {
      switch (op->getOpCode()) {
        case OpCode::eIAnd: {
          const auto& a = getConstantAsUint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsUint(m_builder.getOpForOperand(*op, 1u), i);

          return makeScalarOperand(op->getType(), a & b);
        }

        case OpCode::eIOr: {
          const auto& a = getConstantAsUint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsUint(m_builder.getOpForOperand(*op, 1u), i);

          return makeScalarOperand(op->getType(), a | b);
        }

        case OpCode::eIXor: {
          const auto& a = getConstantAsUint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsUint(m_builder.getOpForOperand(*op, 1u), i);

          return makeScalarOperand(op->getType(), a ^ b);
        }

        case OpCode::eINot: {
          const auto& a = getConstantAsUint(m_builder.getOpForOperand(*op, 0u), i);

          return makeScalarOperand(op->getType(), ~a);
        }

        case OpCode::eIBitInsert: {
          const auto& base = getConstantAsUint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& insert = getConstantAsUint(m_builder.getOpForOperand(*op, 1u), i);
          const auto& ofs = getConstantAsUint(m_builder.getOpForOperand(*op, 2u), i) & 31u;
          const auto& cnt = getConstantAsUint(m_builder.getOpForOperand(*op, 3u), i) & 31u;

          return makeScalarOperand(op->getType(), util::binsert(base, insert, ofs, cnt));
        }

        case OpCode::eUBitExtract: {
          const auto& base = getConstantAsUint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& ofs = getConstantAsUint(m_builder.getOpForOperand(*op, 1u), i) & 31u;
          const auto& cnt = getConstantAsUint(m_builder.getOpForOperand(*op, 2u), i) & 31u;

          return makeScalarOperand(op->getType(), util::bextract(base, ofs, cnt));
        }

        case OpCode::eSBitExtract: {
          const auto& base = getConstantAsUint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& ofs = getConstantAsUint(m_builder.getOpForOperand(*op, 1u), i) & 31u;
          const auto& cnt = getConstantAsUint(m_builder.getOpForOperand(*op, 2u), i) & 31u;

          auto value = util::bextract(base, ofs, cnt);

          if (cnt) {
            auto sign = value & (uint64_t(1u) << (cnt - 1u));
            value |= -sign;
          }

          return makeScalarOperand(op->getType(), value);
        }

        case OpCode::eIShl: {
          const auto& a = getConstantAsUint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsUint(m_builder.getOpForOperand(*op, 1u), i) & 31u;

          return makeScalarOperand(op->getType(), a << b);
        }

        case OpCode::eSShr: {
          const auto& a = getConstantAsSint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsUint(m_builder.getOpForOperand(*op, 1u), i) & 31u;

          /* Manually sign-extend as necessary */
          auto value = a >> b;
          auto sign = value & ((uint64_t(1u) << 31u) >> b);

          return makeScalarOperand(op->getType(), value | (-sign));
        }

        case OpCode::eUShr: {
          const auto& a = getConstantAsUint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsUint(m_builder.getOpForOperand(*op, 1u), i) & 31u;

          return makeScalarOperand(op->getType(), a >> b);
        }

        case OpCode::eIAdd: {
          const auto& a = getConstantAsSint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsSint(m_builder.getOpForOperand(*op, 1u), i);

          return makeScalarOperand(op->getType(), a + b);
        }

        case OpCode::eISub: {
          const auto& a = getConstantAsSint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsSint(m_builder.getOpForOperand(*op, 1u), i);

          return makeScalarOperand(op->getType(), a - b);
        }

        case OpCode::eIAbs: {
          const auto& a = getConstantAsSint(m_builder.getOpForOperand(*op, 0u), i);

          return makeScalarOperand(op->getType(), std::abs(a));
        }

        case OpCode::eINeg: {
          const auto& a = getConstantAsSint(m_builder.getOpForOperand(*op, 0u), i);

          return makeScalarOperand(op->getType(), -a);
        }

        case OpCode::eIMul: {
          const auto& a = getConstantAsSint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsSint(m_builder.getOpForOperand(*op, 1u), i);

          return makeScalarOperand(op->getType(), a * b);
        }

        case OpCode::eUDiv: {
          const auto& a = getConstantAsUint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsUint(m_builder.getOpForOperand(*op, 1u), i);

          return makeScalarOperand(op->getType(), a / b);
        }

        case OpCode::eUMod: {
          const auto& a = getConstantAsUint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsUint(m_builder.getOpForOperand(*op, 1u), i);

          return makeScalarOperand(op->getType(), a % b);
        }

        case OpCode::eSMin: {
          const auto& a = getConstantAsSint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsSint(m_builder.getOpForOperand(*op, 1u), i);

          return makeScalarOperand(op->getType(), std::min(a, b));
        }

        case OpCode::eSMax: {
          const auto& a = getConstantAsSint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsSint(m_builder.getOpForOperand(*op, 1u), i);

          return makeScalarOperand(op->getType(), std::max(a, b));
        }

        case OpCode::eSClamp: {
          const auto& v = getConstantAsSint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& lo = getConstantAsSint(m_builder.getOpForOperand(*op, 1u), i);
          const auto& hi = getConstantAsSint(m_builder.getOpForOperand(*op, 2u), i);

          return makeScalarOperand(op->getType(), std::clamp(v, lo, hi));
        }

        case OpCode::eUMin: {
          const auto& a = getConstantAsUint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsUint(m_builder.getOpForOperand(*op, 1u), i);

          return makeScalarOperand(op->getType(), std::min(a, b));
        }

        case OpCode::eUMax: {
          const auto& a = getConstantAsUint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsUint(m_builder.getOpForOperand(*op, 1u), i);

          return makeScalarOperand(op->getType(), std::max(a, b));
        }

        case OpCode::eUClamp: {
          const auto& v = getConstantAsUint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& lo = getConstantAsUint(m_builder.getOpForOperand(*op, 1u), i);
          const auto& hi = getConstantAsUint(m_builder.getOpForOperand(*op, 2u), i);

          return makeScalarOperand(op->getType(), std::clamp(v, lo, hi));
        }

        default: {
          dxbc_spv_unreachable();
          return Operand();
        }
      }
    } ();

    constant.addOperand(operand);
  }

  auto def = m_builder.rewriteDef(op->getDef(), m_builder.add(std::move(constant)));
  return std::make_pair(true, m_builder.iter(def));
}


std::pair<bool, Builder::iterator> ArithmeticPass::constantFoldBoolOp(Builder::iterator op) {
  if (!allOperandsConstant(*op))
    return std::make_pair(false, ++op);

  Op constant(OpCode::eConstant, op->getType());

  for (uint32_t i = 0u; i < op->getType().getBaseType(0u).getVectorSize(); i++) {
    bool value = [this, op, i] {
      switch (op->getOpCode()) {
        case OpCode::eBAnd: {
          const auto& a = m_builder.getOpForOperand(*op, 0u);
          const auto& b = m_builder.getOpForOperand(*op, 1u);

          return bool(a.getOperand(i)) && bool(b.getOperand(i));
        }

        case OpCode::eBOr: {
          const auto& a = m_builder.getOpForOperand(*op, 0u);
          const auto& b = m_builder.getOpForOperand(*op, 1u);

          return bool(a.getOperand(i)) || bool(b.getOperand(i));
        }

        case OpCode::eBEq: {
          const auto& a = m_builder.getOpForOperand(*op, 0u);
          const auto& b = m_builder.getOpForOperand(*op, 1u);

          return bool(a.getOperand(i)) == bool(b.getOperand(i));
        }

        case OpCode::eBNe: {
          const auto& a = m_builder.getOpForOperand(*op, 0u);
          const auto& b = m_builder.getOpForOperand(*op, 1u);

          return bool(a.getOperand(i)) != bool(b.getOperand(i));
        }

        case OpCode::eBNot: {
          const auto& a = m_builder.getOpForOperand(*op, 0u);

          return !bool(a.getOperand(i));
        }

        default: {
          dxbc_spv_unreachable();
          return false;
        }
      }
    } ();

    constant.addOperand(value);
  }

  auto def = m_builder.rewriteDef(op->getDef(), m_builder.add(std::move(constant)));
  return std::make_pair(true, m_builder.iter(def));
}


std::pair<bool, Builder::iterator> ArithmeticPass::constantFoldCompare(Builder::iterator op) {
  if (!allOperandsConstant(*op))
    return std::make_pair(false, ++op);

  Op constant(OpCode::eConstant, op->getType());

  for (uint32_t i = 0u; i < op->getType().getBaseType(0u).getVectorSize(); i++) {
    auto value = [this, op, i] {
      switch (op->getOpCode()) {
        case OpCode::eFEq: {
          const auto& a = getConstantAsFloat(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsFloat(m_builder.getOpForOperand(*op, 1u), i);

          return a == b && !std::isunordered(a, b);
        }

        case OpCode::eFNe: {
          const auto& a = getConstantAsFloat(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsFloat(m_builder.getOpForOperand(*op, 1u), i);

          /* Exact opposite of not-equal */
          return a != b || std::isunordered(a, b);
        }

        case OpCode::eFLt: {
          const auto& a = getConstantAsFloat(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsFloat(m_builder.getOpForOperand(*op, 1u), i);

          return a < b && !std::isunordered(a, b);
        }

        case OpCode::eFLe: {
          const auto& a = getConstantAsFloat(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsFloat(m_builder.getOpForOperand(*op, 1u), i);

          return a <= b && !std::isunordered(a, b);
        }

        case OpCode::eFGt: {
          const auto& a = getConstantAsFloat(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsFloat(m_builder.getOpForOperand(*op, 1u), i);

          return a > b && !std::isunordered(a, b);
        }

        case OpCode::eFGe: {
          const auto& a = getConstantAsFloat(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsFloat(m_builder.getOpForOperand(*op, 1u), i);

          return a >= b && !std::isunordered(a, b);
        }

        case OpCode::eFIsNan: {
          const auto& a = getConstantAsFloat(m_builder.getOpForOperand(*op, 0u), i);

          return std::isnan(a);
        }

        case OpCode::eIEq: {
          const auto& a = getConstantAsUint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsUint(m_builder.getOpForOperand(*op, 1u), i);

          return a == b;
        }

        case OpCode::eINe: {
          const auto& a = getConstantAsUint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsUint(m_builder.getOpForOperand(*op, 1u), i);

          return a != b;
        }

        case OpCode::eSLt: {
          const auto& a = getConstantAsSint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsSint(m_builder.getOpForOperand(*op, 1u), i);

          return a < b;
        }

        case OpCode::eSLe: {
          const auto& a = getConstantAsSint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsSint(m_builder.getOpForOperand(*op, 1u), i);

          return a <= b;
        }

        case OpCode::eSGt: {
          const auto& a = getConstantAsSint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsSint(m_builder.getOpForOperand(*op, 1u), i);

          return a > b;
        }

        case OpCode::eSGe: {
          const auto& a = getConstantAsSint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsSint(m_builder.getOpForOperand(*op, 1u), i);

          return a >= b;
        }

        case OpCode::eULt: {
          const auto& a = getConstantAsUint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsUint(m_builder.getOpForOperand(*op, 1u), i);

          return a < b;
        }

        case OpCode::eULe: {
          const auto& a = getConstantAsUint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsUint(m_builder.getOpForOperand(*op, 1u), i);

          return a <= b;
        }

        case OpCode::eUGt: {
          const auto& a = getConstantAsUint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsUint(m_builder.getOpForOperand(*op, 1u), i);

          return a > b;
        }

        case OpCode::eUGe: {
          const auto& a = getConstantAsUint(m_builder.getOpForOperand(*op, 0u), i);
          const auto& b = getConstantAsUint(m_builder.getOpForOperand(*op, 1u), i);

          return a >= b;
        }

        default: {
          dxbc_spv_unreachable();
          return false;
        }
      }
    } ();

    constant.addOperand(value);
  }

  auto def = m_builder.rewriteDef(op->getDef(), m_builder.add(std::move(constant)));
  return std::make_pair(true, m_builder.iter(def));
}


std::pair<bool, Builder::iterator> ArithmeticPass::constantFoldSelect(Builder::iterator op) {
  const auto& condOp = m_builder.getOpForOperand(*op, 0u);

  if (!condOp.isConstant())
    return std::make_pair(false, ++op);

  /* Check condition and replace select op with appropriate operand */
  auto cond = bool(condOp.getOperand(0u));
  auto operand = ir::SsaDef(op->getOperand(cond ? 1u : 2u));

  return std::make_pair(true, m_builder.iter(m_builder.rewriteDef(op->getDef(), operand)));
}


std::pair<bool, Builder::iterator> ArithmeticPass::constantFoldOp(Builder::iterator op) {
  switch (op->getOpCode()) {
    case OpCode::eIAnd:
    case OpCode::eIOr:
    case OpCode::eIXor:
    case OpCode::eINot:
    case OpCode::eIBitInsert:
    case OpCode::eUBitExtract:
    case OpCode::eSBitExtract:
    case OpCode::eIShl:
    case OpCode::eSShr:
  case OpCode::eUShr:
    case OpCode::eIAdd:
    case OpCode::eISub:
    case OpCode::eIAbs:
    case OpCode::eINeg:
    case OpCode::eIMul:
    case OpCode::eUDiv:
    case OpCode::eUMod:
    case OpCode::eSMin:
    case OpCode::eSMax:
    case OpCode::eSClamp:
    case OpCode::eUMin:
    case OpCode::eUMax:
    case OpCode::eUClamp:
      return constantFoldArithmeticOp(op);

    case OpCode::eBAnd:
    case OpCode::eBOr:
    case OpCode::eBEq:
    case OpCode::eBNe:
    case OpCode::eBNot:
      return constantFoldBoolOp(op);

    case OpCode::eFEq:
    case OpCode::eFNe:
    case OpCode::eFLt:
    case OpCode::eFLe:
    case OpCode::eFGt:
    case OpCode::eFGe:
    case OpCode::eFIsNan:
    case OpCode::eIEq:
    case OpCode::eINe:
    case OpCode::eSLt:
    case OpCode::eSLe:
    case OpCode::eSGt:
    case OpCode::eSGe:
    case OpCode::eULt:
    case OpCode::eULe:
    case OpCode::eUGt:
    case OpCode::eUGe:
      return constantFoldCompare(op);

    case OpCode::eSelect:
      return constantFoldSelect(op);

    default:
      return std::make_pair(false, ++op);
  }
}


bool ArithmeticPass::allOperandsConstant(const Op& op) const {
  for (uint32_t i = 0u; i < op.getFirstLiteralOperandIndex(); i++) {
    if (!m_builder.getOpForOperand(op, i).isConstant())
      return false;
  }

  return true;
}


uint64_t ArithmeticPass::getConstantAsUint(const Op& op, uint32_t index) const {
  dxbc_spv_assert(op.isConstant());

  switch (op.getType().getBaseType(0u).getBaseType()) {
    case ScalarType::eU8:
    case ScalarType::eI8:
      return uint8_t(op.getOperand(index));

    case ScalarType::eU16:
    case ScalarType::eI16:
      return uint16_t(op.getOperand(index));

    case ScalarType::eU32:
    case ScalarType::eI32:
      return uint32_t(op.getOperand(index));

    case ScalarType::eU64:
    case ScalarType::eI64:
      return uint64_t(op.getOperand(index));

    default:
      dxbc_spv_unreachable();
      return 0u;
  }
}


int64_t ArithmeticPass::getConstantAsSint(const Op& op, uint32_t index) const {
  dxbc_spv_assert(op.isConstant());

  switch (op.getType().getBaseType(0u).getBaseType()) {
    case ScalarType::eU8:
    case ScalarType::eI8:
      return int8_t(op.getOperand(index));

    case ScalarType::eU16:
    case ScalarType::eI16:
      return int16_t(op.getOperand(index));

    case ScalarType::eU32:
    case ScalarType::eI32:
      return int32_t(op.getOperand(index));

    case ScalarType::eU64:
    case ScalarType::eI64:
      return int64_t(op.getOperand(index));

    default:
      dxbc_spv_unreachable();
      return 0u;
  }
}


double ArithmeticPass::getConstantAsFloat(const Op& op, uint32_t index) const {
  dxbc_spv_assert(op.isConstant());

  switch (op.getType().getBaseType(0u).getBaseType()) {
    case ScalarType::eU8:
      return double(uint8_t(op.getOperand(index)));

    case ScalarType::eI8:
      return double(int8_t(op.getOperand(index)));

    case ScalarType::eU16:
      return double(uint16_t(op.getOperand(index)));

    case ScalarType::eI16:
      return double(int16_t(op.getOperand(index)));

    case ScalarType::eU32:
      return double(uint32_t(op.getOperand(index)));

    case ScalarType::eI32:
      return double(int32_t(op.getOperand(index)));

    case ScalarType::eU64:
      return double(uint64_t(op.getOperand(index)));

    case ScalarType::eI64:
      return double(int64_t(op.getOperand(index)));

    case ScalarType::eF16:
      return double(float16_t(op.getOperand(index)));

    case ScalarType::eF32:
      return float(op.getOperand(index));

    case ScalarType::eF64:
      return double(op.getOperand(index));

    default:
      dxbc_spv_unreachable();
      return 0.0;
  }
}


template<typename T>
Operand ArithmeticPass::makeScalarOperand(const Type& type, T value) {
  dxbc_spv_assert(type.isBasicType());

  switch (type.getBaseType(0u).getBaseType()) {
    case ScalarType::eI8:   return Operand(int8_t(value));
    case ScalarType::eU8:   return Operand(uint8_t(value));
    case ScalarType::eI16:  return Operand(int16_t(value));
    case ScalarType::eU16:  return Operand(uint16_t(value));
    case ScalarType::eI32:  return Operand(int32_t(value));
    case ScalarType::eU32:  return Operand(uint32_t(value));
    case ScalarType::eI64:  return Operand(int64_t(value));
    case ScalarType::eU64:  return Operand(uint64_t(value));
    case ScalarType::eF16:  return Operand(float16_t(double(value)));
    case ScalarType::eF32:  return Operand(float(value));
    case ScalarType::eF64:  return Operand(double(value));
    default:                break;
  }

  dxbc_spv_unreachable();
  return Operand();
}

}
