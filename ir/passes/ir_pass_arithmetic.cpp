#include <algorithm>
#include <cmath>

#include "ir_pass_arithmetic.h"
#include "ir_pass_remove_unused.h"
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

    if (!status)
      std::tie(status, next) = resolveIdentityOp(iter);

    if (!status)
      std::tie(status, next) = selectOp(iter);

    if (status) {
      progress = true;
      iter = next;
    } else {
      iter++;
    }
  }

  return progress;
}


void ArithmeticPass::runEarlyLowering() {
  lowerInstructionsPreTransform();

  /* Some instructions operate on composites but
   * then get scalarized, fix that up immediately. */
  ScalarizePass::runResolveRedundantCompositesPass(m_builder);
}


void ArithmeticPass::runLateLowering() {
  lowerInstructionsPostTransform();

  RemoveUnusedPass::runPass(m_builder);
}


bool ArithmeticPass::runPass(Builder& builder, const Options& options) {
  return ArithmeticPass(builder, options).runPass();
}


void ArithmeticPass::runEarlyLoweringPasses(Builder& builder, const Options& options) {
  ArithmeticPass(builder, options).runEarlyLowering();
}


void ArithmeticPass::runLateLoweringPasses(Builder& builder, const Options& options) {
  ArithmeticPass(builder, options).runLateLowering();
}


void ArithmeticPass::lowerInstructionsPreTransform() {
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

      case OpCode::eFClamp:
      case OpCode::eSClamp:
      case OpCode::eUClamp: {
        iter = lowerClamp(iter);
      } continue;

      default:
        break;
    }

    ++iter;
  }
}


void ArithmeticPass::lowerInstructionsPostTransform() {
  auto iter = m_builder.getCode().first;

  while (iter != m_builder.end()) {
    switch (iter->getOpCode()) {
      case OpCode::eFAdd:
      case OpCode::eFSub: {
        if (m_options.fuseMad) {
          iter = tryFuseMad(iter);
          continue;
        }
      } break;

      case OpCode::eFMin:
      case OpCode::eSMin:
      case OpCode::eUMin: {
        iter = tryFuseClamp(iter);
      } continue;

      case OpCode::eConvertFtoI: {
        if (m_options.lowerConvertFtoI) {
          iter = lowerConvertFtoI(iter);
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


Builder::iterator ArithmeticPass::lowerClamp(Builder::iterator op) {
  /* Lower clamp to min(max(v, lo), hi) so that transforms can ensure
   * consistent behaviour. */
  const auto& v = m_builder.getOpForOperand(*op, 0u);
  const auto& lo = m_builder.getOpForOperand(*op, 1u);
  const auto& hi = m_builder.getOpForOperand(*op, 2u);

  auto [minOpCode, maxOpCode] = [op] {
    switch (op->getOpCode()) {
      case OpCode::eFClamp: return std::make_pair(OpCode::eFMin, OpCode::eFMax);
      case OpCode::eSClamp: return std::make_pair(OpCode::eSMin, OpCode::eSMax);
      case OpCode::eUClamp: return std::make_pair(OpCode::eUMin, OpCode::eUMax);
      default: break;
    }

    dxbc_spv_unreachable();
    return std::make_pair(OpCode::eUnknown, OpCode::eUnknown);
  } ();

  auto maxOp = Op(maxOpCode, op->getType()).setFlags(op->getFlags()).addOperands(v.getDef(), lo.getDef());
  auto maxDef = m_builder.addBefore(op->getDef(), std::move(maxOp));

  auto minOp = Op(minOpCode, op->getType()).setFlags(op->getFlags()).addOperands(maxDef, hi.getDef());
  m_builder.rewriteOp(op->getDef(), std::move(minOp));

  return ++op;
}


Builder::iterator ArithmeticPass::lowerConvertFtoI(Builder::iterator op) {
  const auto& src = m_builder.getOpForOperand(*op, 0u);

  /* Look up existing function for float-to-int conversion */
  auto srcType = src.getType().getBaseType(0u).getBaseType();
  auto dstType = op->getType().getBaseType(0u).getBaseType();

  auto e = std::find_if(m_convertFunctions.begin(), m_convertFunctions.end(),
    [dstType, srcType] (const ConvertFunc& fn) {
      return fn.dstType == dstType &&
             fn.srcType == srcType;
    });

  if (e == m_convertFunctions.end()) {
    ConvertFunc fn = m_convertFunctions.emplace_back();
    fn.dstType = dstType;
    fn.srcType = srcType;

    /* Declare actual conversion function */
    auto param = m_builder.add(Op::DclParam(srcType));
    m_builder.add(Op::DebugName(param, "v"));

    fn.function = m_builder.addBefore(m_builder.getCode().first->getDef(),
      Op::Function(dstType).addParam(param));

    std::stringstream debugName;
    debugName << "cvt_" << srcType << "_" << dstType;

    m_builder.add(Op::DebugName(fn.function, debugName.str().c_str()));

    m_builder.setCursor(fn.function);
    m_builder.add(Op::Label());

    bool isUnsigned = BasicType(dstType).isUnsignedIntType();
    auto bits = BasicType(dstType).byteSize() * 8u;

    /* Determine integer range of values that the destination type supports */
    Operand minValueDst = Operand(isUnsigned ? uint64_t(0u) : uint64_t(-1) << (bits - 1u));
    Operand maxValueDst = Operand((uint64_t(isUnsigned ? 2u : 1u) << (bits - 1u)) - 1u);

    /* Determine corresponding bounds as floating point values */
    Operand minValueSrc = { };
    Operand maxValueSrc = { };

    switch (srcType) {
      case ScalarType::eF16: {
        /* F16 is special because it is the only floating point
         * type with a lower dynamic range than integer types */
        if (isUnsigned) {
          minValueSrc = Operand(float16_t(0.0f));
          maxValueSrc = Operand(float16_t::maxValue());
        } else if (dstType == ScalarType::eI16) {
          minValueSrc = Operand(float16_t::fromRaw(util::convertSintToFloatRtz<uint16_t, 5u, 10u>(int64_t(minValueDst))));
          maxValueSrc = Operand(float16_t::fromRaw(util::convertSintToFloatRtz<uint16_t, 5u, 10u>(int64_t(maxValueDst))));
        } else {
          minValueSrc = Operand(float16_t::minValue());
          maxValueSrc = Operand(float16_t::maxValue());
        }
      } break;

      case ScalarType::eF32: {
        minValueSrc = Operand(isUnsigned
          ? util::convertUintToFloatRtz<uint32_t, 8u, 23u>(uint64_t(minValueDst))
          : util::convertSintToFloatRtz<uint32_t, 8u, 23u>(int64_t(minValueDst)));
        maxValueSrc = Operand(isUnsigned
          ? util::convertUintToFloatRtz<uint32_t, 8u, 23u>(uint64_t(maxValueDst))
          : util::convertSintToFloatRtz<uint32_t, 8u, 23u>(int64_t(maxValueDst)));
      } break;

      case ScalarType::eF64: {
        minValueSrc = Operand(isUnsigned
          ? util::convertUintToFloatRtz<uint64_t, 11u, 52u>(uint64_t(minValueDst))
          : util::convertSintToFloatRtz<uint64_t, 11u, 52u>(int64_t(minValueDst)));
        maxValueSrc = Operand(isUnsigned
          ? util::convertUintToFloatRtz<uint64_t, 11u, 52u>(uint64_t(maxValueDst))
          : util::convertSintToFloatRtz<uint64_t, 11u, 52u>(int64_t(maxValueDst)));
      } break;

      default:
        dxbc_spv_unreachable();
        break;
    }

    /* Load parameter and perform basic range checking Ü*/
    auto v = m_builder.add(Op::ParamLoad(srcType, fn.function, param));

    auto hiCond = m_builder.add(Op::FGt(ScalarType::eBool, v,
      m_builder.add(Op(OpCode::eConstant, srcType).addOperand(maxValueSrc))));

    if (BasicType(dstType).isUnsignedIntType()) {
      /* Max will implicitly flush nan to 0 */
      v = m_builder.add(Op::FMax(srcType, v,
        m_builder.add(Op(OpCode::eConstant, srcType).addOperand(0u))));
      v = m_builder.add(Op::ConvertFtoI(dstType, v));
      v = m_builder.add(Op::Select(dstType, hiCond,
        m_builder.add(Op(OpCode::eConstant, dstType).addOperand(maxValueDst)), v));
    } else {
      /* Need to handle every possible case separately */
      auto loCond = m_builder.add(Op::FLt(ScalarType::eBool, v,
        m_builder.add(Op(OpCode::eConstant, srcType).addOperand(minValueSrc))));
      auto nanCond = m_builder.add(Op::FIsNan(ScalarType::eBool, v));

      v = m_builder.add(Op::ConvertFtoI(dstType, v));
      v = m_builder.add(Op::Select(dstType, hiCond,
        m_builder.add(Op(OpCode::eConstant, dstType).addOperand(maxValueDst)), v));
      v = m_builder.add(Op::Select(dstType, loCond,
        m_builder.add(Op(OpCode::eConstant, dstType).addOperand(minValueDst)), v));
      v = m_builder.add(Op::Select(dstType, nanCond,
        m_builder.add(Op(OpCode::eConstant, dstType).addOperand(0u)), v));
    }

    m_builder.add(Op::Return(dstType, v));
    m_builder.add(Op::FunctionEnd());

    e = m_convertFunctions.insert(m_convertFunctions.end(), fn);
  }

  m_builder.rewriteOp(op->getDef(),
    Op::FunctionCall(dstType, e->function).addParam(src.getDef()));
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


Builder::iterator ArithmeticPass::tryFuseClamp(Builder::iterator op) {
  const auto& a = m_builder.getOpForOperand(*op, 0u);
  const auto& b = m_builder.getOpForOperand(*op, 1u);

  auto [maxOpCode, clampOpCode] = [op] {
    switch (op->getOpCode()) {
      case OpCode::eFMin: return std::make_pair(OpCode::eFMax, OpCode::eFClamp);
      case OpCode::eSMin: return std::make_pair(OpCode::eSMax, OpCode::eSClamp);
      case OpCode::eUMin: return std::make_pair(OpCode::eUMax, OpCode::eUClamp);
      default: break;
    }

    dxbc_spv_unreachable();
    return std::make_pair(OpCode::eUnknown, OpCode::eUnknown);
  } ();

  bool aIsMax = a.getOpCode() == maxOpCode;
  bool bIsMax = b.getOpCode() == maxOpCode;

  if (aIsMax == bIsMax)
    return ++op;

  const auto& v = m_builder.getOpForOperand(aIsMax ? a : b, 0u);
  const auto& lo = m_builder.getOpForOperand(aIsMax ? a : b, 1u);
  const auto& hi = aIsMax ? b : a;

  auto clampOp = Op(clampOpCode, op->getType())
    .setFlags(op->getFlags() | (aIsMax ? a : b).getFlags())
    .addOperands(v.getDef(), lo.getDef(), hi.getDef());

  m_builder.rewriteOp(op->getDef(), std::move(clampOp));
  return ++op;
}


Builder::iterator ArithmeticPass::tryFuseMad(Builder::iterator op) {
  /* Only fuse if neither the op nor any of the operands involved are
   * precise, Also, to maintain invariance, only fuse if exactly one
   * operand is FMul. */
  if (getFpFlags(*op) & OpFlag::ePrecise)
    return ++op;

  uint32_t fmulOperand = -1u;

  for (uint32_t i = 0u; i < 2u; i++) {
    const auto& operand = m_builder.getOpForOperand(*op, i);

    if (operand.getOpCode() == OpCode::eFMul) {
      if (std::exchange(fmulOperand, i) != -1u)
        return ++op;

      if (getFpFlags(operand) & OpFlag::ePrecise)
        return ++op;
    }
  }

  if (fmulOperand == -1u)
    return ++op;

  /* c + a * b -> FMad(a, b, c)
     a * b + c -> FMad(a, b, c) */
  auto a = SsaDef(m_builder.getOpForOperand(*op, fmulOperand).getOperand(0u));
  auto b = SsaDef(m_builder.getOpForOperand(*op, fmulOperand).getOperand(1u));
  auto c = SsaDef(op->getOperand(1u - fmulOperand));

  if (op->getOpCode() == OpCode::eFSub) {
    if (!fmulOperand) {
      /* a * b - c -> a * b + (-c) */
      c = m_builder.addBefore(op->getDef(), Op::FNeg(m_builder.getOp(c).getType(), c));
    } else {
      /* c - a * b -> -a * b + c. Try to eliminate any existing negations. */
      if (m_builder.getOp(a).getOpCode() == OpCode::eFNeg)
        a = SsaDef(m_builder.getOp(a).getOperand(0u));
      else if (m_builder.getOp(b).getOpCode() == OpCode::eFNeg)
        b = SsaDef(m_builder.getOp(b).getOperand(0u));
      else
        a = m_builder.addBefore(op->getDef(), Op::FNeg(m_builder.getOp(a).getType(), a));
    }
  }

  m_builder.rewriteOp(op->getDef(), Op::FMad(op->getType(), a, b, c).setFlags(op->getFlags()));
  return ++op;
}


std::pair<bool, Builder::iterator> ArithmeticPass::selectCompare(Builder::iterator op) {
  const auto& a = m_builder.getOpForOperand(*op, 0u);
  const auto& b = m_builder.getOpForOperand(*op, 1u);

  if (!op->getType().isScalarType())
    return std::make_pair(false, ++op);

  /* Clean up common boolean patterns. This assumes that comparisons have already
   * been reordered in such a way that constant operands are on the right, and that
   * the select operands differ. */
  if (!isConstantSelect(a) || !b.isConstant())
    return std::make_pair(false, ++op);

  /* select(cond, a, b) == a -> cond
   * select(cond, a, b) != a -> !cond
   * select(cond, a, b) == b -> !cond
   * select(cond, a, b) != b -> cond
   * select(cond, a, b) == c -> false
   * select(cond, a, b) != c -> true */
  bool negate = op->getOpCode() == OpCode::eINe;

  auto condDef = SsaDef(a.getOperand(0u));
  auto trueDef = SsaDef(a.getOperand(1u));
  auto falseDef = SsaDef(a.getOperand(2u));

  if (b.getDef() != trueDef && b.getDef() != falseDef) {
    auto next = m_builder.rewriteDef(op->getDef(), m_builder.makeConstant(negate));
    return std::make_pair(true, m_builder.iter(next));
  }

  if (b.getDef() == falseDef)
    negate = !negate;

  if (!negate) {
    auto next = m_builder.rewriteDef(op->getDef(), condDef);
    return std::make_pair(true, m_builder.iter(next));
  } else {
    m_builder.rewriteOp(op->getDef(), Op::BNot(op->getType(), condDef));
    return std::make_pair(true, op);
  }
}


std::pair<bool, Builder::iterator> ArithmeticPass::selectBitOp(Builder::iterator op) {
  const auto& a = m_builder.getOpForOperand(*op, 0u);

  if (!op->getType().isScalarType() || !isConstantSelect(a))
    return std::make_pair(false, ++op);

  switch (op->getOpCode()) {
    case OpCode::eCast:
    case OpCode::eIAbs:
    case OpCode::eINot:
    case OpCode::eINeg: {
      if (!isOnlyUse(m_builder, a.getDef(), op->getDef()))
        break;

      /* Fold op into select operands */
      auto trueDef = m_builder.addBefore(op->getDef(),
        Op(op->getOpCode(), op->getType()).addOperand(SsaDef(a.getOperand(1u))));
      auto falseDef = m_builder.addBefore(op->getDef(),
        Op(op->getOpCode(), op->getType()).addOperand(SsaDef(a.getOperand(2u))));

      m_builder.rewriteOp(op->getDef(), Op::Select(op->getType(),
        SsaDef(a.getOperand(0u)), trueDef, falseDef).setFlags(a.getFlags()));

      return std::make_pair(true, m_builder.iter(trueDef));
    }

    case OpCode::eIAnd:
    case OpCode::eIOr:
    case OpCode::eIXor: {
      const auto& b = m_builder.getOpForOperand(*op, 1u);

      if (isConstantSelect(b)) {
        /* Handle patterns such as:
         * select(c0, a, 0) & select(c1, a, 0) -> select(c0 && c1, a, 0)
         * select(c0, a, 0) | select(c1, a, 0) -> select(c0 || c1, a, 0)
         * select(c0, a, 0) ^ select(c1, a, 0) -> select(c0 != c1, a, 0) */
        auto at = SsaDef(a.getOperand(1u));
        auto af = SsaDef(a.getOperand(2u));
        auto bt = SsaDef(b.getOperand(1u));
        auto bf = SsaDef(b.getOperand(2u));

        if (((at == bt && af == bf) || (at == bf && af == bt)) &&
            (isZeroConstant(m_builder.getOp(at)) || isZeroConstant(m_builder.getOp(af)))) {
          auto ac = SsaDef(a.getOperand(0u));
          auto bc = SsaDef(b.getOperand(0u));

          auto ref = op->getDef();

          /* Arrange and invert conditions as necessary so hat the zero constant
           * is last. This is needed for the and pattern to be correct. */
          if (isConstantValue(m_builder.getOp(at), 0)) {
            ac = ref = m_builder.addBefore(ref, Op::BNot(ScalarType::eBool, ac));
            std::swap(at, af);
          }

          /* Arrange B condition so that the select operands match */
          if (isConstantValue(m_builder.getOp(bt), 0)) {
            bc = ref = m_builder.addBefore(ref, Op::BNot(ScalarType::eBool, bc));
            std::swap(bt, bf);
          }

          dxbc_spv_assert(at == bt && af == bf);

          /* Make boolean op for the respective conditions */
          auto condOpCode = [op] {
            switch (op->getOpCode()) {
              case OpCode::eIAnd: return OpCode::eBAnd;
              case OpCode::eIOr:  return OpCode::eBOr;
              case OpCode::eIXor: return OpCode::eBNe;
              default: break;
            }

            dxbc_spv_unreachable();
            return OpCode::eUnknown;
          } ();

          auto cond = m_builder.addBefore(op->getDef(),
            Op(condOpCode, ScalarType::eBool).addOperands(ac, bc));

          m_builder.rewriteOp(op->getDef(), Op::Select(op->getType(), cond, at, af));
          return std::make_pair(true, m_builder.iter(ref));
        }
      }
    } [[fallthrough]];

    case OpCode::eIAdd:
    case OpCode::eISub:
    case OpCode::eIMul:
    case OpCode::eUDiv:
    case OpCode::eUMod:
    case OpCode::eSMax:
    case OpCode::eSMin:
    case OpCode::eUMax:
    case OpCode::eUMin:
    case OpCode::eIShl:
    case OpCode::eUShr:
    case OpCode::eSShr: {
      const auto& b = m_builder.getOpForOperand(*op, 1u);

      if (b.isConstant()) {
        /* Fold op into select operands and then constant-fold */
        if (!isOnlyUse(m_builder, a.getDef(), op->getDef()))
          break;

        auto trueDef = m_builder.addBefore(op->getDef(), Op(op->getOpCode(), op->getType())
          .addOperands(SsaDef(a.getOperand(1u)), b.getDef()));
        auto falseDef = m_builder.addBefore(op->getDef(), Op(op->getOpCode(), op->getType())
          .addOperands(SsaDef(a.getOperand(2u)), b.getDef()));

        m_builder.rewriteOp(op->getDef(), Op::Select(op->getType(),
          SsaDef(a.getOperand(0u)), trueDef, falseDef).setFlags(a.getFlags()));

        return std::make_pair(true, m_builder.iter(trueDef));
      }
    } break;

    default:
      dxbc_spv_unreachable();
      break;
  }

  return std::make_pair(false, ++op);
}


std::pair<bool, Builder::iterator> ArithmeticPass::selectOp(Builder::iterator op) {
  switch (op->getOpCode()) {
    case OpCode::eCast:
    case OpCode::eIAbs:
    case OpCode::eINeg:
    case OpCode::eINot:
    case OpCode::eIAnd:
    case OpCode::eIOr:
    case OpCode::eIXor:
    case OpCode::eIAdd:
    case OpCode::eISub:
    case OpCode::eIMul:
    case OpCode::eUDiv:
    case OpCode::eUMod:
    case OpCode::eSMax:
    case OpCode::eSMin:
    case OpCode::eUMax:
    case OpCode::eUMin:
    case OpCode::eIShl:
    case OpCode::eUShr:
    case OpCode::eSShr:
      return selectBitOp(op);

    case OpCode::eIEq:
    case OpCode::eINe:
      return selectCompare(op);

    default:
      return std::make_pair(false, ++op);
  }
}


std::pair<bool, Builder::iterator> ArithmeticPass::propagateAbsUnary(Builder::iterator op) {
  dxbc_spv_assert(op->getOperandCount() == 1u);

  /* op(|a|) -> |op(a)| */
  const auto& a = m_builder.getOpForOperand(*op, 0u);

  if (a.getOpCode() == OpCode::eFAbs) {
    auto newOp = m_builder.addBefore(op->getDef(), Op(op->getOpCode(), op->getType())
      .setFlags(op->getFlags())
      .addOperand(SsaDef(a.getOperand(0u))));

    m_builder.rewriteOp(op->getDef(), Op::FAbs(op->getType(), newOp).setFlags(op->getFlags()));
    return std::make_pair(true, m_builder.iter(newOp));
  }

  return std::make_pair(false, ++op);
}


std::pair<bool, Builder::iterator> ArithmeticPass::propagateAbsBinary(Builder::iterator op) {
  dxbc_spv_assert(op->getOperandCount() == 2u);

  /* op(|a|, |b|) -> |op(a, b)| */
  const auto& a = m_builder.getOpForOperand(*op, 0u);
  const auto& b = m_builder.getOpForOperand(*op, 1u);

  if (a.getOpCode() == OpCode::eFAbs && b.getOpCode() == OpCode::eFAbs) {
    auto newOp = m_builder.addBefore(op->getDef(), Op(op->getOpCode(), op->getType())
      .setFlags(op->getFlags())
      .addOperand(SsaDef(a.getOperand(0u)))
      .addOperand(SsaDef(b.getOperand(0u))));

    m_builder.rewriteOp(op->getDef(), Op::FAbs(op->getType(), newOp).setFlags(op->getFlags()));
    return std::make_pair(true, m_builder.iter(newOp));
  }

  return std::make_pair(false, ++op);
}


std::pair<bool, Builder::iterator> ArithmeticPass::propagateSignUnary(Builder::iterator op) {
  dxbc_spv_assert(op->getOperandCount() == 1u);

  /* op(-a) -> -op(a) */
  const auto& a = m_builder.getOpForOperand(*op, 0u);

  if (a.getOpCode() == OpCode::eFNeg) {
    auto newOp = m_builder.addBefore(op->getDef(), Op(op->getOpCode(), op->getType())
      .setFlags(op->getFlags())
      .addOperand(SsaDef(a.getOperand(0u))));

    m_builder.rewriteOp(op->getDef(), Op(a.getOpCode(), op->getType())
      .setFlags(op->getFlags())
      .addOperand(newOp));

    return std::make_pair(true, m_builder.iter(newOp));
  }

  return std::make_pair(false, ++op);
}


std::pair<bool, Builder::iterator> ArithmeticPass::propagateSignBinary(Builder::iterator op) {
  dxbc_spv_assert(op->getOperandCount() == 2u);

  /* Handles instructions that follow multiplication semantics */
  const auto& a = m_builder.getOpForOperand(*op, 0u);
  const auto& b = m_builder.getOpForOperand(*op, 1u);

  bool aIsNeg = a.getOpCode() == OpCode::eFNeg;
  bool bIsNeg = b.getOpCode() == OpCode::eFNeg;

  /* op(-a, constant) -> op(a, -constant) */
  if (aIsNeg && b.isConstant()) {
    auto constantOp = b;
    auto signBit = uint64_t(1u) << (8u * byteSize(b.getType().getBaseType(0u).getBaseType()) - 1u);

    for (uint32_t i = 0u; i < constantOp.getOperandCount(); i++) {
      auto literal = uint64_t(constantOp.getOperand(i));
      constantOp.setOperand(i, Operand(literal ^ signBit));
    }

    m_builder.rewriteOp(op->getDef(), Op(op->getOpCode(), op->getType())
      .setFlags(op->getFlags())
      .addOperand(SsaDef(a.getOperand(0u)))
      .addOperand(m_builder.add(std::move(constantOp))));

    return std::make_pair(true, op);
  }

  /* op(-a, -b) -> op(a, b) */
  if (aIsNeg && bIsNeg) {
    m_builder.rewriteOp(op->getDef(), Op(op->getOpCode(), op->getType())
      .setFlags(op->getFlags())
      .addOperand(SsaDef(a.getOperand(0u)))
      .addOperand(SsaDef(b.getOperand(0u))));
    return std::make_pair(true, op);
  }

  /* op(-a, b) -> -op(a, b)
   * op(a, -b) -> -op(a, b) */
  if (aIsNeg || bIsNeg) {
    auto newOp = m_builder.addBefore(op->getDef(), Op(op->getOpCode(), op->getType())
      .setFlags(op->getFlags())
      .addOperand(aIsNeg ? SsaDef(a.getOperand(0u)) : a.getDef())
      .addOperand(bIsNeg ? SsaDef(b.getOperand(0u)) : b.getDef()));

    m_builder.rewriteOp(op->getDef(), Op::FNeg(op->getType(), newOp).setFlags(op->getFlags()));
    return std::make_pair(true, m_builder.iter(newOp));
  }

  return std::make_pair(false, ++op);
}


std::pair<bool, Builder::iterator> ArithmeticPass::propagateAbsSignSelect(Builder::iterator op) {
  const auto& a = m_builder.getOpForOperand(*op, 1u);
  const auto& b = m_builder.getOpForOperand(*op, 2u);

  if ((a.getOpCode() != OpCode::eFAbs && a.getOpCode() != OpCode::eFNeg) || b.getOpCode() != a.getOpCode())
    return std::make_pair(false, ++op);

  auto selectOp = m_builder.addBefore(op->getDef(), Op::Select(op->getType(),
    SsaDef(op->getOperand(0u)), SsaDef(a.getOperand(0u)), SsaDef(b.getOperand(0u))).setFlags(op->getFlags()));

  m_builder.rewriteOp(op->getDef(), Op(a.getOpCode(), op->getType())
    .setFlags(op->getFlags()).addOperand(selectOp));

  return std::make_pair(true, m_builder.iter(selectOp));
}


std::pair<bool, Builder::iterator> ArithmeticPass::propagateAbsSignPhi(Builder::iterator op) {
  const auto& first = m_builder.getOpForOperand(*op, 1u);

  if (first.getOpCode() != OpCode::eFAbs && first.getOpCode() != OpCode::eFNeg)
    return std::make_pair(false, ++op);

  /* Verify that all operands have the same opcodes */
  auto phi = Op(OpCode::ePhi, op->getType()).setFlags(op->getFlags());
  bool canPropagate = true;

  forEachPhiOperand(*op, [&] (SsaDef block, SsaDef value) {
    const auto& valueOp = m_builder.getOp(value);

    if (valueOp.getOpCode() != first.getOpCode())
      canPropagate = false;

    phi.addOperand(block);
    phi.addOperand(SsaDef(valueOp.getOperand(0u)));
  });

  if (!canPropagate)
    return std::make_pair(false, ++op);

  /* Insert new phi op and put negation or absolute later in the block */
  auto phiDef = m_builder.addBefore(op->getDef(), std::move(phi));
  auto ref = phiDef;

  while (m_builder.getOp(ref).getOpCode() == OpCode::ePhi)
    ref = m_builder.getNext(ref);

  m_builder.rewriteDef(op->getDef(), m_builder.addBefore(ref,
    Op(first.getOpCode(), first.getType()).setFlags(op->getFlags()).addOperand(phiDef)));

  return std::make_pair(true, m_builder.iter(phiDef));
}


std::pair<bool, Builder::iterator> ArithmeticPass::resolveIdentityArithmeticOp(Builder::iterator op) {
  switch (op->getOpCode()) {
    case OpCode::eFAbs:
    case OpCode::eIAbs: {
      /* |(|a|)| -> |a|
       * |-a| -> |a| */
      auto negOp = op->getOpCode() == OpCode::eFAbs ? OpCode::eFNeg : OpCode::eINeg;

      const auto& a = m_builder.getOpForOperand(*op, 0u);

      if (a.getOpCode() == op->getOpCode()) {
        auto next = m_builder.rewriteDef(op->getDef(), a.getDef());
        return std::make_pair(true, m_builder.iter(next));
      }

      if (a.getOpCode() == negOp) {
        m_builder.rewriteOp(op->getDef(),
          Op(op->getOpCode(), op->getType()).setFlags(op->getFlags()).addOperand(SsaDef(a.getOperand(0u))));
        return std::make_pair(true, op);
      }
    } break;

    case OpCode::eFNeg:
    case OpCode::eINeg: {
      /* -(-a) -> a */
      const auto& a = m_builder.getOpForOperand(*op, 0u);

      if (a.getOpCode() == op->getOpCode()) {
        auto next = m_builder.rewriteDef(op->getDef(), SsaDef(a.getOperand(0u)));
        return std::make_pair(true, m_builder.iter(next));
      }

      /* -(a - b) = b - a */
      if (a.getOpCode() == OpCode::eFSub || a.getOpCode() == OpCode::eISub) {
        m_builder.rewriteOp(op->getDef(), Op(a.getOpCode(), op->getType())
          .setFlags(op->getFlags())
          .addOperand(SsaDef(a.getOperand(1u)))
          .addOperand(SsaDef(a.getOperand(0u))));

        return std::make_pair(true, op);
      }
    } break;

    case OpCode::eFRcp: {
      const auto& a = m_builder.getOpForOperand(*op, 0u);

      if (!(getFpFlags(*op) & OpFlag::ePrecise) &&
          !(getFpFlags(a) & OpFlag::ePrecise)) {
        /* rcp(rcp(a)) -> a. This pattern commonly occurs with
         * SV_Position.w reads in fragment shaders. */
        if (a.getOpCode() == OpCode::eFRcp) {
          auto next = m_builder.rewriteDef(op->getDef(), SsaDef(a.getOperand(0u)));
          return std::make_pair(true, m_builder.iter(next));
        }
      }

      auto result = propagateSignUnary(op);

      if (!result.first)
        result = propagateAbsUnary(op);

      return result;
    }

    case OpCode::eFDiv: {
      const auto& a = m_builder.getOpForOperand(*op, 0u);
      const auto& b = m_builder.getOpForOperand(*op, 1u);

      /* 1.0 / a -> rcp(a) */
      if (!(getFpFlags(*op) & OpFlag::ePrecise)) {
        if (a.isConstant()) {
          bool isPosRcp = true;
          bool isNegRcp = true;

          for (uint32_t i = 0u; i < a.getOperandCount(); i++) {
            isPosRcp = isPosRcp && getConstantAsFloat(a, i) ==  1.0;
            isNegRcp = isNegRcp && getConstantAsFloat(a, i) == -1.0;
          }

          if (isPosRcp) {
            m_builder.rewriteOp(op->getDef(),
              Op::FRcp(op->getType(), b.getDef()).setFlags(op->getFlags()));
            return std::make_pair(true, op);
          } else if (isNegRcp) {
            auto rcpDef = m_builder.addBefore(op->getDef(),
              Op::FRcp(op->getType(), b.getDef()).setFlags(op->getFlags()));
            m_builder.rewriteOp(op->getDef(), Op::FNeg(op->getType(), rcpDef));
            return std::make_pair(true, op);
          }
        }
      }
    } return propagateSignBinary(op);

    case OpCode::eFMin:
    case OpCode::eSMin:
    case OpCode::eUMin:
    case OpCode::eFMax:
    case OpCode::eSMax:
    case OpCode::eUMax: {
      const auto& a = m_builder.getOpForOperand(*op, 0u);
      const auto& b = m_builder.getOpForOperand(*op, 1u);

      bool isUInt = op->getOpCode() == OpCode::eUMin || op->getOpCode() == OpCode::eUMax;
      bool isFloat = op->getOpCode() == OpCode::eFMin || op->getOpCode() == OpCode::eFMax;

      auto negOp = isFloat ? OpCode::eFNeg : OpCode::eINeg;
      auto absOp = isFloat ? OpCode::eFAbs : OpCode::eIAbs;

      /* min(a, a) -> a
       * max(a, a) -> a */
      if (a.getDef() == b.getDef()) {
        auto next = m_builder.rewriteDef(op->getDef(), a.getDef());
        return std::make_pair(true, m_builder.iter(next));
      }

      /* min(-a, -b) -> -max(a, b)
       * max(-a, -b) -> -min(a, b) */
      if (a.getOpCode() == negOp && b.getOpCode() == negOp && !isUInt) {
        auto inverseOpCode = [op] {
          switch (op->getOpCode()) {
            case OpCode::eFMin: return OpCode::eFMax;
            case OpCode::eFMax: return OpCode::eFMin;
            case OpCode::eSMin: return OpCode::eSMax;
            case OpCode::eSMax: return OpCode::eSMin;
            default: break;
          }

          dxbc_spv_unreachable();
          return OpCode::eUnknown;
        } ();

        auto inverseOp = m_builder.addBefore(op->getDef(), Op(inverseOpCode, op->getType())
          .setFlags(op->getFlags())
          .addOperand(ir::SsaDef(a.getOperand(0u)))
          .addOperand(ir::SsaDef(b.getOperand(0u))));

        m_builder.rewriteOp(op->getDef(),
          Op(negOp, op->getType()).setFlags(op->getFlags()).addOperand(inverseOp));

        return std::make_pair(true, m_builder.iter(inverseOp));
      }

      /* max(-a, a) -> |a|
       * min(-a, a) -> -|a| */
      if ((a.getOpCode() == negOp || b.getOpCode() == negOp) && !isUInt) {
        const auto& aOp = a.getOpCode() == negOp ? m_builder.getOpForOperand(a, 0u) : a;
        const auto& bOp = b.getOpCode() == negOp ? m_builder.getOpForOperand(b, 0u) : b;

        if (aOp.getDef() == bOp.getDef()) {
          auto newOp = Op(absOp, op->getType()).setFlags(op->getFlags()).addOperand(aOp.getDef());

          if (op->getOpCode() == OpCode::eFMax || op->getOpCode() == OpCode::eSMax) {
            m_builder.rewriteOp(op->getDef(), std::move(newOp));
            return std::make_pair(true, op);
          } else {
            auto newDef = m_builder.addBefore(op->getDef(), std::move(newOp));

            m_builder.rewriteOp(op->getDef(),
              Op(negOp, op->getType()).setFlags(op->getFlags()).addOperand(newDef));

            return std::make_pair(true, m_builder.iter(newDef));
          }
        }
      }
    } break;

    case OpCode::eFAdd:
    case OpCode::eIAdd:
    case OpCode::eFSub:
    case OpCode::eISub: {
      bool isInt = op->getOpCode() == OpCode::eIAdd || op->getOpCode() == OpCode::eISub;
      bool isSub = op->getOpCode() == OpCode::eISub || op->getOpCode() == OpCode::eFSub;

      auto negOpCode = isInt ? OpCode::eINeg : OpCode::eFNeg;

      auto inverseOpCode = isInt
        ? (op->getOpCode() == OpCode::eIAdd ? OpCode::eISub : OpCode::eIAdd)
        : (op->getOpCode() == OpCode::eFAdd ? OpCode::eFSub : OpCode::eFAdd);

      const auto& a = m_builder.getOpForOperand(*op, 0u);
      const auto& b = m_builder.getOpForOperand(*op, 1u);

      /* a + (-b) -> a - b
       * a - (-b) -> a + b */
      if (b.getOpCode() == negOpCode) {
        m_builder.rewriteOp(op->getDef(), Op(inverseOpCode, op->getType())
          .setFlags(op->getFlags())
          .addOperand(a.getDef())
          .addOperand(SsaDef(b.getOperand(0u))));
        return std::make_pair(true, op);
      }

      /* -a + b -> b - a
       * -a - b -> -(b + a) */
      if (a.getOpCode() == negOpCode) {
        auto inverseOp = Op(inverseOpCode, op->getType())
          .setFlags(op->getFlags())
          .addOperand(b.getDef())
          .addOperand(SsaDef(a.getOperand(0u)));

        if (isSub) {
          auto inverseDef = m_builder.addBefore(op->getDef(), std::move(inverseOp));

          m_builder.rewriteOp(op->getDef(), Op(negOpCode, op->getType())
            .setFlags(op->getFlags())
            .addOperand(inverseDef));

          return std::make_pair(true, m_builder.iter(inverseDef));
        } else {
          m_builder.rewriteOp(op->getDef(), std::move(inverseOp));
          return std::make_pair(true, op);
        }
      }

      /* a + -constant = a - constant
       * a - -constant = a + constant */
      if (b.isConstant() && b.getType().getBaseType(0u).isIntType()) {
        Op constant(OpCode::eConstant, b.getType());

        bool isConstantNegative = false;
        bool isConstantPositive = false;

        for (uint32_t i = 0u; i < b.getOperandCount(); i++) {
          auto value = getConstantAsSint(b, i);

          isConstantNegative = isConstantNegative || value < 0;
          isConstantPositive = isConstantPositive || value > 0;

          constant.addOperand(makeScalarOperand(b.getType(), -value));
        }

        if (isConstantNegative && !isConstantPositive) {
          auto inverseOp = Op(inverseOpCode, op->getType())
            .setFlags(op->getFlags())
            .addOperand(a.getDef())
            .addOperand(m_builder.add(std::move(constant)));

          m_builder.rewriteOp(op->getDef(), std::move(inverseOp));
          return std::make_pair(true, op);
        }

        if (!isConstantNegative && !isConstantPositive) {
          /* Constant is 0 */
          auto next = m_builder.rewriteDef(op->getDef(), a.getDef());
          return std::make_pair(true, m_builder.iter(next));
        }
      }
    } break;

    case OpCode::eFMul:
    case OpCode::eFMulLegacy: {
      auto result = propagateSignBinary(op);

      if (!result.first)
        result = propagateAbsBinary(op);

      return result;
    }

    case OpCode::eFRound: {
      auto roundMode = RoundMode(op->getOperand(op->getFirstLiteralOperandIndex()));

      /* rtz(-a) = -rtz(a)
       * rte(-a) = -rte(a)
       * ceil(-a) = floor(a)
       * floor(-a) = ceil(a) */
      const auto& a = m_builder.getOpForOperand(*op, 0u);

      if (a.getOpCode() == OpCode::eFNeg) {
        if (roundMode == RoundMode::ePositiveInf)
          roundMode = RoundMode::eNegativeInf;
        else if (roundMode == RoundMode::eNegativeInf)
          roundMode = RoundMode::ePositiveInf;

        auto newOp = m_builder.addBefore(op->getDef(),
          Op::FRound(op->getType(), SsaDef(a.getOperand(0u)), roundMode).setFlags(op->getFlags()));

        m_builder.rewriteOp(op->getDef(), Op::FNeg(op->getType(), newOp).setFlags(op->getFlags()));
        return std::make_pair(true, m_builder.iter(newOp));
      }

      /* rtz(|a|) -> |rtz(a)|
       * rte(|a|) -> |rte(a)| */
      if (a.getOpCode() == OpCode::eFAbs && (roundMode == RoundMode::eZero || roundMode == RoundMode::eNearestEven)) {
        auto newOp = m_builder.addBefore(op->getDef(),
          Op::FRound(op->getType(), SsaDef(a.getOperand(0u)), roundMode).setFlags(op->getFlags()));

        m_builder.rewriteOp(op->getDef(), Op::FAbs(op->getType(), newOp).setFlags(op->getFlags()));
        return std::make_pair(true, m_builder.iter(newOp));
      }
    } break;

    case OpCode::eFSin:
      return propagateSignUnary(op);

    case OpCode::eFCos: {
      /* cos(-a) -> cos(a)
       * cos(|a|) -> cos(a) */
      const auto& a = m_builder.getOpForOperand(*op, 0u);

      if (a.getOpCode() == OpCode::eFNeg || a.getOpCode() == OpCode::eFAbs) {
        m_builder.rewriteOp(op->getDef(), Op::FCos(op->getType(), SsaDef(a.getOperand(0u))).setFlags(op->getFlags()));
        return std::make_pair(true, op);
      }
    } break;

    case OpCode::eINot: {
      /* ~(~a) -> a */
      const auto& a = m_builder.getOpForOperand(*op, 0u);

      if (a.getOpCode() == OpCode::eINot) {
        auto next = m_builder.rewriteDef(op->getDef(), SsaDef(a.getOperand(0u)));
        return std::make_pair(true, m_builder.iter(next));
      }

    } break;

    default:
      break;
  }

  return std::make_pair(false, ++op);
}


std::pair<bool, Builder::iterator> ArithmeticPass::resolveIdentityBoolOp(Builder::iterator op) {
  if (!op->getType().isScalarType())
    return std::make_pair(false, ++op);

  switch (op->getOpCode()) {
    case OpCode::eBAnd: {
      const auto& a = m_builder.getOpForOperand(*op, 0u);
      const auto& b = m_builder.getOpForOperand(*op, 1u);

      /* a && a -> a */
      if (a.getDef() == b.getDef()) {
        auto next = m_builder.rewriteDef(op->getDef(), a.getDef());
        return std::make_pair(true, m_builder.iter(next));
      }

      /* a && true -> a; a && false -> false */
      if (b.isConstant()) {
        auto value = bool(b.getOperand(0u));

        auto next = m_builder.rewriteDef(op->getDef(),
          value ? a.getDef() : m_builder.makeConstant(false));
        return std::make_pair(true, m_builder.iter(next));
      }

      /* !a && !b -> !(a || b) */
      if (a.getOpCode() == OpCode::eBNot && b.getOpCode() == OpCode::eBNot) {
        m_builder.rewriteOp(op->getDef(), Op::BNot(op->getType(),
          m_builder.addBefore(op->getDef(), Op::BOr(op->getType(),
            SsaDef(a.getOperand(0u)), SsaDef(b.getOperand(0u))))));
        return std::make_pair(true, op);
      }
    } break;

    case OpCode::eBOr: {
      const auto& a = m_builder.getOpForOperand(*op, 0u);
      const auto& b = m_builder.getOpForOperand(*op, 1u);

      /* a || a -> a */
      if (a.getDef() == b.getDef()) {
        auto next = m_builder.rewriteDef(op->getDef(), a.getDef());
        return std::make_pair(true, m_builder.iter(next));
      }

      /* a || true -> true; a || false -> a */
      if (b.isConstant()) {
        auto value = bool(b.getOperand(0u));

        auto next = m_builder.rewriteDef(op->getDef(),
          value ? m_builder.makeConstant(true) : a.getDef());
        return std::make_pair(true, m_builder.iter(next));
      }

      /* !a || !b -> !(a && b) */
      if (a.getOpCode() == OpCode::eBNot && b.getOpCode() == OpCode::eBNot) {
        m_builder.rewriteOp(op->getDef(), Op::BNot(op->getType(),
          m_builder.addBefore(op->getDef(), Op::BAnd(op->getType(),
            SsaDef(a.getOperand(0u)), SsaDef(b.getOperand(0u))))));
        return std::make_pair(true, op);
      }

      return resolveIsNanCheck(op);
    }

    case OpCode::eBEq: {
      const auto& a = m_builder.getOpForOperand(*op, 0u);
      const auto& b = m_builder.getOpForOperand(*op, 1u);

      /* a == a -> true */
      if (a.getDef() == b.getDef()) {
        auto next = m_builder.rewriteDef(op->getDef(), m_builder.makeConstant(true));
        return std::make_pair(true, m_builder.iter(next));
      }

      /* a == true -> a; a == false => !a */
      if (b.isConstant()) {
        auto value = bool(b.getOperand(0u));

        if (value) {
          auto next = m_builder.rewriteDef(op->getDef(), a.getDef());
          return std::make_pair(true, m_builder.iter(next));
        } else {
          m_builder.rewriteOp(op->getDef(), Op::BNot(op->getType(), a.getDef()));
          return std::make_pair(true, op);
        }
      }

      /* !a == b -> a != b */
      if (a.getOpCode() == OpCode::eBNot) {
        m_builder.rewriteOp(op->getDef(), Op::BNe(ScalarType::eBool,
          SsaDef(a.getOperand(0u)), b.getDef()));
        return std::make_pair(true, op);
      }

      /* a == !b -> a != b */
      if (b.getOpCode() == OpCode::eBNot) {
        m_builder.rewriteOp(op->getDef(), Op::BNe(ScalarType::eBool,
          a.getDef(), SsaDef(b.getOperand(0u))));
        return std::make_pair(true, op);
      }
    } break;

    case OpCode::eBNe: {
      const auto& a = m_builder.getOpForOperand(*op, 0u);
      const auto& b = m_builder.getOpForOperand(*op, 1u);

      /* a != a -> false */
      if (a.getDef() == b.getDef()) {
        auto next = m_builder.rewriteDef(op->getDef(), m_builder.makeConstant(false));
        return std::make_pair(true, m_builder.iter(next));
      }

      /* a != true -> !a; a != false => a */
      if (b.isConstant()) {
        auto value = bool(b.getOperand(0u));

        if (!value) {
          auto next = m_builder.rewriteDef(op->getDef(), a.getDef());
          return std::make_pair(true, m_builder.iter(next));
        } else {
          m_builder.rewriteOp(op->getDef(), Op::BNot(op->getType(), a.getDef()));
          return std::make_pair(true, op);
        }
      }

      /* !a != b -> a == b */
      if (a.getOpCode() == OpCode::eBNot) {
        m_builder.rewriteOp(op->getDef(), Op::BEq(ScalarType::eBool,
          SsaDef(a.getOperand(0u)), b.getDef()));
        return std::make_pair(true, op);
      }

      /* a != !b -> a == b */
      if (b.getOpCode() == OpCode::eBNot) {
        m_builder.rewriteOp(op->getDef(), Op::BEq(ScalarType::eBool,
          a.getDef(), SsaDef(b.getOperand(0u))));
        return std::make_pair(true, op);
      }
    } break;

    case OpCode::eBNot: {
      const auto& a = m_builder.getOpForOperand(*op, 0u);

      /* !!a -> a */
      if (a.getOpCode() == OpCode::eBNot) {
        auto next = m_builder.rewriteDef(op->getDef(), SsaDef(a.getOperand(0u)));
        return std::make_pair(true, m_builder.iter(next));
      }

      /* Flip comparison operators, except for floating point ones where
       * the operands can be NaN since ordering actually matters. */
      if (!isOnlyUse(m_builder, a.getDef(), op->getDef()))
        return std::make_pair(false, ++op);

      static const std::array<std::pair<OpCode, OpCode>, 9u> s_opcodePairs = {{
        { OpCode::eBEq, OpCode::eBNe },
        { OpCode::eFEq, OpCode::eFNe },
        { OpCode::eFGt, OpCode::eFLe },
        { OpCode::eFGe, OpCode::eFLt },
        { OpCode::eIEq, OpCode::eINe },
        { OpCode::eSGt, OpCode::eSLe },
        { OpCode::eSGe, OpCode::eSLt },
        { OpCode::eUGt, OpCode::eULe },
        { OpCode::eUGe, OpCode::eULt },
      }};

      auto opCode = [&a] {
        for (const auto& e : s_opcodePairs) {
          if (a.getOpCode() == e.first)
            return e.second;
          if (a.getOpCode() == e.second)
            return e.first;
        }

        return OpCode::eUnknown;
      } ();

      if (opCode == OpCode::eUnknown)
        return std::make_pair(false, ++op);

      /* Ensure that flipping the op is actually legal */
      const auto& a0 = m_builder.getOpForOperand(a, 0u);
      const auto& a1 = m_builder.getOpForOperand(a, 1u);

      OpFlags requiredFlags = 0u;

      if (opCode == OpCode::eFLt || opCode == OpCode::eFLe ||
          opCode == OpCode::eFGt || opCode == OpCode::eFGe)
        requiredFlags |= OpFlag::eNoNan;

      if ((getFpFlags(a0) & requiredFlags) != requiredFlags ||
          (getFpFlags(a1) & requiredFlags) != requiredFlags)
        return std::make_pair(false, ++op);

      Op newOp(opCode, op->getType());
      newOp.setFlags(op->getFlags());

      for (uint32_t i = 0u; i < a.getOperandCount(); i++)
        newOp.addOperand(a.getOperand(i));

      m_builder.rewriteOp(op->getDef(), std::move(newOp));
      return std::make_pair(true, op);
    } break;

    default:
      dxbc_spv_unreachable();
      break;
  }

  return std::make_pair(false, ++op);
}


std::pair<bool, Builder::iterator> ArithmeticPass::resolveIdentityCompareOp(Builder::iterator op) {
  if (!op->getType().isScalarType())
    return std::make_pair(false, ++op);

  /* Resolve isnan first since it's the only unary op */
  if (op->getOpCode() == OpCode::eFIsNan) {
    const auto& a = m_builder.getOpForOperand(*op, 0u);

    if (a.getFlags() & OpFlag::eNoNan) {
      auto next = m_builder.rewriteDef(op->getDef(), m_builder.makeConstant(false));
      return std::make_pair(true, m_builder.iter(next));
    }

    return std::make_pair(false, ++op);
  }

  /* For comparisons, we can only really do anything
   * if the operands are the same */
  const auto& a = m_builder.getOpForOperand(*op, 0u);
  const auto& b = m_builder.getOpForOperand(*op, 1u);

  if (a.getDef() != b.getDef())
    return std::make_pair(false, ++op);

  switch (op->getOpCode()) {
    case OpCode::eFEq:
    case OpCode::eFGe:
    case OpCode::eFLe: {
      auto isnan = m_builder.addBefore(op->getDef(),
        Op::FIsNan(op->getType(), a.getDef()).setFlags(op->getFlags()));
      m_builder.rewriteOp(op->getDef(), Op::BNot(op->getType(), isnan));
      return std::make_pair(true, op);
    }

    case OpCode::eFNe: {
      m_builder.rewriteOp(op->getDef(),
        Op::FIsNan(op->getType(), a.getDef()).setFlags(op->getFlags()));
      return std::make_pair(true, op);
    }

    case OpCode::eIEq:
    case OpCode::eSGe:
    case OpCode::eSLe:
    case OpCode::eUGe:
    case OpCode::eULe: {
      auto next = m_builder.rewriteDef(op->getDef(), m_builder.makeConstant(true));
      return std::make_pair(true, m_builder.iter(next));
    }

    case OpCode::eFLt:
    case OpCode::eFGt:
    case OpCode::eINe:
    case OpCode::eSLt:
    case OpCode::eSGt:
    case OpCode::eULt:
    case OpCode::eUGt: {
      auto next = m_builder.rewriteDef(op->getDef(), m_builder.makeConstant(false));
      return std::make_pair(true, m_builder.iter(next));
    }

    default:
      dxbc_spv_unreachable();
      return std::make_pair(false, ++op);
  }
}


std::pair<bool, Builder::iterator> ArithmeticPass::resolveIdentitySelect(Builder::iterator op) {
  const auto& cond = m_builder.getOpForOperand(*op, 0u);
  const auto& a = m_builder.getOpForOperand(*op, 1u);
  const auto& b = m_builder.getOpForOperand(*op, 2u);

  /* select(cond, a, a) -> a */
  if (a.getDef() == b.getDef()) {
    auto next = m_builder.rewriteDef(op->getDef(), a.getDef());
    return std::make_pair(true, m_builder.iter(next));
  }

  /* select(!cond, a, b) -> select(cond, b, a) */
  if (cond.getOpCode() == OpCode::eBNot) {
    m_builder.rewriteOp(op->getDef(), Op::Select(op->getType(),
      SsaDef(cond.getOperand(0u)), b.getDef(), a.getDef()).setFlags(op->getFlags()));
    return std::make_pair(true, op);
  }

  return propagateAbsSignSelect(op);
}


std::pair<bool, Builder::iterator> ArithmeticPass::resolveIsNanCheck(Builder::iterator op) {
  /* Look for a pattern that goes a < b || a == b || a > b, which is equivalent to
   * !(isnan(a) || isnan(b)). b will usually be a constant, so we can constant-fold. */
  if (op->getOpCode() != OpCode::eBOr)
    return std::make_pair(false, ++op);

  /* Find which operand is the 'or' */
  uint32_t orOperand = -1u;

  for (uint32_t i = 0u; i < 2u; i++) {
    if (m_builder.getOpForOperand(*op, i).getOpCode() == OpCode::eBOr) {
      if (orOperand != -1u)
        return std::make_pair(false, ++op);

      orOperand = i;
    }
  }

  if (orOperand == -1u)
    return std::make_pair(false, ++op);

  /* Ensure that all operands are the same */
  const auto& orOp = m_builder.getOpForOperand(*op, orOperand);

  const auto& v0 = m_builder.getOpForOperand(*op, 1u - orOperand);
  const auto& v1 = m_builder.getOpForOperand(orOp, 0u);
  const auto& v2 = m_builder.getOpForOperand(orOp, 1u);

  for (uint32_t i = 0u; i < 2u; i++) {
    if (v0.getOperand(i) != v1.getOperand(i) || v0.getOperand(i) != v2.getOperand(i))
      return std::make_pair(false, ++op);
  }

  /* Ensure that one of each op FEq, FLt and FGt are present */
  std::array<OpCode, 3> ops = { v0.getOpCode(), v1.getOpCode(), v2.getOpCode() };

  bool hasEq = false;
  bool hasLt = false;
  bool hasGt = false;

  for (auto op : ops) {
    hasEq = hasEq || op == OpCode::eFEq;
    hasLt = hasLt || op == OpCode::eFLt;
    hasGt = hasGt || op == OpCode::eFGt;
  }

  if (!hasEq || !hasLt || !hasGt)
    return std::make_pair(false, ++op);

  /* Rewrite op as nan check */
  auto aIsNan = m_builder.addBefore(op->getDef(), Op::FIsNan(op->getType(), SsaDef(v0.getOperand(0u))));
  auto bIsNan = m_builder.addBefore(op->getDef(), Op::FIsNan(op->getType(), SsaDef(v0.getOperand(1u))));
  auto anyNan = m_builder.addBefore(op->getDef(), Op::BOr(op->getType(), aIsNan, bIsNan));

  m_builder.rewriteOp(op->getDef(), Op::BNot(op->getType(), anyNan));
  return std::make_pair(false, op);
}


std::pair<bool, Builder::iterator> ArithmeticPass::resolveIdentityOp(Builder::iterator op) {
  switch (op->getOpCode()) {
    case OpCode::eFAbs:
    case OpCode::eFNeg:
    case OpCode::eFAdd:
    case OpCode::eFSub:
    case OpCode::eFMul:
    case OpCode::eFMulLegacy:
    case OpCode::eFDiv:
    case OpCode::eFMin:
    case OpCode::eFMax:
    case OpCode::eFRcp:
    case OpCode::eFRound:
    case OpCode::eFSin:
    case OpCode::eFCos:
    case OpCode::eIAbs:
    case OpCode::eINeg:
    case OpCode::eIAdd:
    case OpCode::eISub:
    case OpCode::eINot:
    case OpCode::eSMin:
    case OpCode::eSMax:
    case OpCode::eUMin:
    case OpCode::eUMax:
      return resolveIdentityArithmeticOp(op);

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
      return resolveIdentityCompareOp(op);

    case OpCode::eBAnd:
    case OpCode::eBOr:
    case OpCode::eBEq:
    case OpCode::eBNe:
    case OpCode::eBNot:
      return resolveIdentityBoolOp(op);

    case OpCode::eSelect:
      return resolveIdentitySelect(op);

    case OpCode::ePhi:
      return propagateAbsSignPhi(op);

    default:
      return std::make_pair(false, ++op);
  }
}


std::pair<bool, Builder::iterator> ArithmeticPass::reorderConstantOperandsCompareOp(Builder::iterator op) {
  const auto& a = m_builder.getOpForOperand(*op, 0u);
  const auto& b = m_builder.getOpForOperand(*op, 1u);

  if (!shouldFlipOperands(*op))
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

  if (!shouldFlipOperands(*op))
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
  auto operand = SsaDef(op->getOperand(cond ? 1u : 2u));

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


bool ArithmeticPass::isConstantSelect(const Op& op) const {
  if (op.getOpCode() != OpCode::eSelect)
    return false;

  return m_builder.getOpForOperand(op, 1u).isConstant() &&
         m_builder.getOpForOperand(op, 2u).isConstant() &&
         SsaDef(op.getOperand(1u)) != SsaDef(op.getOperand(2u));
}


bool ArithmeticPass::isZeroConstant(const Op& op) const {
  if (!op.isConstant())
    return false;

  for (uint32_t i = 0u; i < op.getOperandCount(); i++) {
    if (op.getOperand(i) != Operand(0u))
      return false;
  }

  return true;
}


bool ArithmeticPass::shouldFlipOperands(const Op& op) const {
  dxbc_spv_assert(op.getOperandCount() >= 2u);

  const auto& a = m_builder.getOpForOperand(op, 0u);
  const auto& b = m_builder.getOpForOperand(op, 1u);

  if (a.isConstant() && !b.isConstant())
    return true;

  if (a.isConstant() == b.isConstant())
    return a.getDef() > b.getDef();

  return false;
}


OpFlags ArithmeticPass::getFpFlags(const Op& op) const {
  auto type = op.getType().getBaseType(0u).getBaseType();
  auto flags = op.getFlags();

  switch (type) {
    case ScalarType::eF16: return flags | m_fp16Flags;
    case ScalarType::eF32: return flags | m_fp32Flags;
    case ScalarType::eF64: return flags | m_fp64Flags;
    default: return flags;
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
