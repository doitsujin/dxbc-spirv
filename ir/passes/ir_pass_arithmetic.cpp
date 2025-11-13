#include <algorithm>
#include <cmath>
#include <sstream>

#include "ir_pass_arithmetic.h"
#include "ir_pass_lower_consume.h"
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

    if (iter->getOpCode() == OpCode::eLabel)
      m_visitedBlocks.insert(iter->getDef());

    if (!status)
      std::tie(status, next) = constantFoldOp(iter);

    if (!status)
      std::tie(status, next) = reorderConstantOperandsOp(iter);

    if (!status)
      std::tie(status, next) = resolveIdentityOp(iter);

    if (!status)
      std::tie(status, next) = resolveBuiltInCompareOp(iter);

    if (!status)
      std::tie(status, next) = selectOp(iter);

    if (!status)
      std::tie(status, next) = resolveIntSignOp(iter);

    if (!status)
      std::tie(status, next) = vectorizeF32toF16(iter);

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
}


void ArithmeticPass::runLateLowering() {
  lowerInstructionsPostTransform();

  /* Some instructions operate on composites but
   * then get scalarized, fix that up immediately. */
  ScalarizePass::runResolveRedundantCompositesPass(m_builder);

  RemoveUnusedPass::runPass(m_builder);
}


void ArithmeticPass::propagateInvariance() {
  bool feedback = false;
  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if ((iter->getOpCode() == OpCode::eDclOutput || iter->getOpCode() == OpCode::eDclOutputBuiltIn) &&
        (iter->getFlags() & OpFlag::eInvariant)) {
      auto [useA, useB] = m_builder.getUses(iter->getDef());

      for (auto use = useA; use != useB; use++) {
        if (use->getOpCode() == OpCode::eOutputStore) {
          propagateInvariance(*use);
          feedback = true;
        }
      }
    }
  }

  if (!feedback)
    return;

  /* Remove flag from non-float ops */
  std::tie(a, b) = m_builder.getCode();

  for (auto iter = a; iter != b; iter++) {
    if ((iter->getFlags() & OpFlag::eInvariant) && !(iter->getType().isBasicType() && iter->getType().getBaseType(0u).isFloatType()))
      m_builder.setOpFlags(iter->getDef(), iter->getFlags() - OpFlag::eInvariant);
  }
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


void ArithmeticPass::runPropagateInvariancePass(Builder& builder) {
  ArithmeticPass(builder, Options()).propagateInvariance();
}


void ArithmeticPass::lowerInstructionsPreTransform() {
  fuseMultiplyAdd();

  auto iter = m_builder.begin();

  while (iter != m_builder.end()) {
    switch (iter->getOpCode()) {
      case OpCode::eSetGsInputPrimitive: {
        auto primType = PrimitiveType(iter->getOperand(iter->getFirstLiteralOperandIndex()));
        m_gsInputVertexCount = primitiveVertexCount(primType);

        ++iter;
      } continue;

      case OpCode::eFClamp:
      case OpCode::eSClamp:
      case OpCode::eUClamp: {
        iter = lowerClamp(iter);
      } continue;

      case OpCode::eFSin:
      case OpCode::eFCos: {
        if (m_options.lowerSinCos) {
          iter = lowerSinCos(iter);
          continue;
        }
      } break;

      case OpCode::eInputLoad: {
        iter = lowerInputBuiltIn(iter);
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
      case OpCode::eFMin:
      case OpCode::eSMin:
      case OpCode::eUMin: {
        iter = tryFuseClamp(iter);
      } continue;

      case OpCode::eFDot:
      case OpCode::eFDotLegacy: {
        if (m_options.lowerDot) {
          iter = lowerDot(iter);
          continue;
        }
      } break;

      case OpCode::eFMulLegacy:
      case OpCode::eFMadLegacy:
      case OpCode::eFPowLegacy: {
        if (m_options.lowerMulLegacy) {
          iter = lowerLegacyOp(iter);
          continue;
        }
      } break;

      case OpCode::eConvertFtoI: {
        if (m_options.lowerConvertFtoI) {
          iter = lowerConvertFtoI(iter);
          continue;
        }
      } break;

      case OpCode::eConvertItoF: {
        if (m_options.hasNvUnsignedItoFBug) {
          iter = lowerConvertItoF(iter);
          continue;
        }
      } break;

      case OpCode::eConvertF32toPackedF16: {
        iter = lowerF32toF16(iter);
      } continue;

      case OpCode::eConvertPackedF16toF32: {
        iter = lowerF16toF32(iter);
      } continue;

      case OpCode::eUMSad: {
        if (m_options.lowerMsad) {
          iter = lowerMsad(iter);
          continue;
        }
      } break;

      default:
        break;
    }

    ++iter;
  }
}


void ArithmeticPass::fuseMultiplyAdd() {
  auto iter = m_builder.getCode().first;

  while (iter != m_builder.end()) {
    switch (iter->getOpCode()) {
      case OpCode::eFAdd:
      case OpCode::eFSub: {
        iter = fuseMad(iter);
      } continue;

      default:
        ++iter;
    }
  }
}


void ArithmeticPass::propagateInvariance(const Op& base) {
  util::small_vector<SsaDef, 1024> queue;
  queue.push_back(base.getDef());

  while (!queue.empty()) {
    const auto& next = m_builder.getOp(queue.back());
    queue.pop_back();

    if (next.getFlags() & OpFlag::eInvariant)
      continue;

    m_builder.setOpFlags(next.getDef(), next.getFlags() | OpFlag::eInvariant);

    for (uint32_t i = 0u; i < next.getFirstLiteralOperandIndex(); i++) {
      const auto& arg = m_builder.getOpForOperand(next, i);

      if (!arg.isDeclarative() && !arg.getType().isVoidType())
        queue.push_back(arg.getDef());
    }

    switch (next.getOpCode()) {
      case OpCode::eScratchLoad: {
        /* Declare all scratch stores as invariant too */
        const auto& scratch = m_builder.getOpForOperand(next, 0u);
        auto [a, b] = m_builder.getUses(scratch.getDef());

        for (auto iter = a; iter != b; iter++) {
          if (iter->getOpCode() == OpCode::eScratchStore)
            queue.push_back(iter->getDef());
        }
      } break;

      case OpCode::ePhi: {
        /* Branch conditions must be invariant too */
        forEachPhiOperand(next, [&] (SsaDef block, SsaDef) {
          auto [a, b] = m_builder.getUses(block);

          for (auto iter = a; iter != b; iter++) {
            if (iter->getOpCode() == OpCode::eBranchConditional ||
                iter->getOpCode() == OpCode::eSwitch)
              queue.push_back(m_builder.getOpForOperand(*iter, 0u).getDef());
          }
        });
      } break;

      default:
        break;
    }
  }
}


Builder::iterator ArithmeticPass::fuseMad(Builder::iterator op) {
  if (getFpFlags(*op) & (OpFlag::ePrecise | OpFlag::eInvariant))
    return ++op;

  std::optional<uint32_t> fmulOperand = { };

  for (uint32_t i = 0u; i < op->getFirstLiteralOperandIndex(); i++) {
    const auto& operand = m_builder.getOpForOperand(*op, i);

    if (operand.getOpCode() == OpCode::eFMul ||
        operand.getOpCode() == OpCode::eFMulLegacy) {
      if (getFpFlags(operand) & (OpFlag::ePrecise | OpFlag::eInvariant))
        return ++op;

      if (!fmulOperand && isOnlyUse(m_builder, operand.getDef(), op->getDef()))
        fmulOperand = i;
    }
  }

  if (!fmulOperand)
    return ++op;

  /* c + a * b -> FMad(a, b, c)
     a * b + c -> FMad(a, b, c) */
  auto a = SsaDef(m_builder.getOpForOperand(*op, *fmulOperand).getOperand(0u));
  auto b = SsaDef(m_builder.getOpForOperand(*op, *fmulOperand).getOperand(1u));
  auto c = SsaDef(op->getOperand(1u - *fmulOperand));

  if (op->getOpCode() == OpCode::eFSub) {
    if (!(*fmulOperand)) {
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

  /* Emit fused op and constant-fold negations where possible. */
  auto madOp = m_builder.getOpForOperand(*op, *fmulOperand).getOpCode() == OpCode::eFMulLegacy
    ? OpCode::eFMadLegacy
    : OpCode::eFMad;

  auto fusedOp = Op(madOp, op->getType()).setFlags(op->getFlags()).addOperands(a, b, c);
  m_builder.rewriteOp(op->getDef(), fusedOp);

  for (uint32_t i = 0u; i < fusedOp.getOperandCount(); i++)
    constantFoldOp(m_builder.iter(SsaDef(fusedOp.getOperand(i))));

  return ++op;
}


Builder::iterator ArithmeticPass::lowerLegacyOp(Builder::iterator op) {
  auto function = op->getOpCode() == OpCode::eFPowLegacy
    ? buildPowLegacyFunc(op->getType().getBaseType(0u))
    : buildMulLegacyFunc(op->getOpCode(), op->getType().getBaseType(0u));

  auto functionCall = Op::FunctionCall(op->getType(), function).setFlags(op->getFlags());

  for (uint32_t i = 0u; i < op->getOperandCount(); i++)
    functionCall.addParam(SsaDef(op->getOperand(i)));

  m_builder.rewriteOp(op->getDef(), std::move(functionCall));
  return ++op;
}


Builder::iterator ArithmeticPass::lowerDot(Builder::iterator op) {
  const auto& srcA = m_builder.getOpForOperand(*op, 0u);
  const auto& srcB = m_builder.getOpForOperand(*op, 1u);

  dxbc_spv_assert(srcA.getType() == srcB.getType());

  /* Look up existing dot function */
  auto vectorType = srcA.getType().getBaseType(0u);
  auto resultType = op->getType().getBaseType(0u).getBaseType();

  auto e = std::find_if(m_dotFunctions.begin(), m_dotFunctions.end(), [
    cOpCode     = op->getOpCode(),
    cVectorType = vectorType,
    cResultType = resultType
  ] (const DotFunc& fn) {
    return fn.opCode      == cOpCode &&
           fn.vectorType  == cVectorType &&
           fn.resultType  == cResultType;
  });

  if (e == m_dotFunctions.end()) {
    DotFunc fn = { };
    fn.opCode = op->getOpCode();
    fn.vectorType = srcA.getType().getBaseType(0u);
    fn.resultType = resultType;

    /* Declare actual conversion function */
    auto paramA = m_builder.add(Op::DclParam(vectorType));
    auto paramB = m_builder.add(Op::DclParam(vectorType));

    m_builder.add(Op::DebugName(paramA, "a"));
    m_builder.add(Op::DebugName(paramB, "b"));

    fn.function = m_builder.addBefore(m_builder.getCode().first->getDef(),
      Op::Function(op->getType()).addParam(paramA).addParam(paramB));

    std::stringstream debugName;
    debugName << "dp" << vectorType.getVectorSize() << "_" << resultType;

    if (resultType != vectorType.getBaseType())
      debugName << "_" << vectorType.getBaseType();

    if (op->getOpCode() == OpCode::eFDotLegacy)
      debugName << "_legacy";

    m_builder.add(Op::DebugName(fn.function, debugName.str().c_str()));

    m_builder.setCursor(fn.function);
    m_builder.add(Op::Label());

    auto vectorA = m_builder.add(Op::ParamLoad(vectorType, fn.function, paramA));
    auto vectorB = m_builder.add(Op::ParamLoad(vectorType, fn.function, paramB));

    /* Determine which opcodes to use */
    bool isLegacy = op->getOpCode() == OpCode::eFDotLegacy;

    auto mulOp = isLegacy ? OpCode::eFMulLegacy : OpCode::eFMul;
    auto madOp = isLegacy ? OpCode::eFMadLegacy : OpCode::eFMad;

    /* Mark the multiply-add chain as precise so that compilers don't screw around with
     * it, otherwise we run into rendering issues in e.g. Trails through Daybreak. */
    SsaDef result = { };

    auto a = m_builder.add(Op::CompositeExtract(vectorType.getBaseType(), vectorA, m_builder.makeConstant(0u)));
    auto b = m_builder.add(Op::CompositeExtract(vectorType.getBaseType(), vectorB, m_builder.makeConstant(0u)));

    if (isLegacy && m_options.lowerMulLegacy)
      result = m_builder.add(emitMulLegacy(op->getType(), a, b).setFlags(OpFlag::ePrecise));
    else
      result = m_builder.add(Op(mulOp, op->getType()).setFlags(OpFlag::ePrecise).addOperands(a, b));

    for (uint32_t i = 1u; i < srcA.getType().getBaseType(0u).getVectorSize(); i++) {
      a = m_builder.add(Op::CompositeExtract(vectorType.getBaseType(), vectorA, m_builder.makeConstant(i)));
      b = m_builder.add(Op::CompositeExtract(vectorType.getBaseType(), vectorB, m_builder.makeConstant(i)));

      if (isLegacy && m_options.lowerMulLegacy)
        result = m_builder.add(emitMadLegacy(op->getType(), a, b, result).setFlags(OpFlag::ePrecise));
      else
        result = m_builder.add(Op(madOp, op->getType()).setFlags(OpFlag::ePrecise).addOperands(a, b, result));
    }

    m_builder.add(Op::Return(op->getType(), result));
    m_builder.add(Op::FunctionEnd());

    e = m_dotFunctions.insert(m_dotFunctions.end(), fn);
  }

  m_builder.rewriteOp(op->getDef(), Op::FunctionCall(op->getType(), e->function)
    .setFlags(op->getFlags())
    .addParam(srcA.getDef())
    .addParam(srcB.getDef()));
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
    ConvertFunc fn = { };
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

  m_builder.rewriteOp(op->getDef(), Op::FunctionCall(dstType, e->function)
    .setFlags(op->getFlags())
    .addParam(src.getDef()));
  return ++op;
}


Builder::iterator ArithmeticPass::lowerConvertItoF(Builder::iterator op) {
  const auto& src = m_builder.getOpForOperand(*op, 0u);

  /* Look up existing function for int-to-float conversion */
  auto srcType = src.getType().getBaseType(0u).getBaseType();
  auto dstType = op->getType().getBaseType(0u).getBaseType();

  auto e = std::find_if(m_convertFunctions.begin(), m_convertFunctions.end(),
    [dstType, srcType] (const ConvertFunc& fn) {
      return fn.dstType == dstType &&
             fn.srcType == srcType;
    });

  if (e == m_convertFunctions.end()) {
    ConvertFunc fn = { };
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

    /* Nvidia is broken as of 580.76.05 and will treat UtoF inputs as signed
     * with our code for some reason. Cast to the next largest integer type
     * to work around the issue. */
    auto v = m_builder.add(Op::ParamLoad(srcType, fn.function, param));

    if (srcType == ScalarType::eU32)
      v = m_builder.add(Op::ConvertItoI(ScalarType::eU64, v));
    else if (srcType == ScalarType::eU16)
      v = m_builder.add(Op::ConvertItoI(ScalarType::eU32, v));

    v = m_builder.add(Op::ConvertItoF(dstType, v));

    m_builder.add(Op::Return(dstType, v));
    m_builder.add(Op::FunctionEnd());

    e = m_convertFunctions.insert(m_convertFunctions.end(), fn);
  }

  m_builder.rewriteOp(op->getDef(), Op::FunctionCall(dstType, e->function)
    .setFlags(op->getFlags())
    .addParam(src.getDef()));
  return ++op;
}


Builder::iterator ArithmeticPass::lowerF32toF16(Builder::iterator op) {
  /* There are a number of different patterns here since we allow min
   * precision. In particular, we have to deal with f16 inputs here
   * regardless of the lowering option. */
  const auto& a = m_builder.getOpForOperand(*op, 0u);
  dxbc_spv_assert(a.getType().isVectorType() && op->getType().isScalarType());

  auto srcType = a.getType().getBaseType(0u).getBaseType();
  dxbc_spv_assert(a.getType() == BasicType(srcType, 2u));

  auto dstType = op->getType().getBaseType(0u).getBaseType();
  dxbc_spv_assert(dstType == ScalarType::eU32 || dstType == ScalarType::eU16);

  if (srcType == ScalarType::eF16) {
    if (dstType == ScalarType::eU32) {
      /* Input is already f16, bitcast input vector to u32. */
      m_builder.rewriteOp(op->getDef(), Op::Cast(dstType, a.getDef()));
    } else {
      /* Extract first component and cast to u16. */
      auto component = m_builder.addBefore(op->getDef(),
        Op::CompositeExtract(srcType, a.getDef(), m_builder.makeConstant(0u)));

      m_builder.rewriteOp(op->getDef(), Op::Cast(dstType, component));
    }

    return ++op;
  } else {
    dxbc_spv_assert(srcType == ScalarType::eF32);

    Op newOp = *op;

    if (m_options.lowerF32toF16) {
      newOp = Op::FunctionCall(ScalarType::eU32, buildF32toF16Func())
        .setFlags(op->getFlags())
        .addParam(a.getDef());
    }

    /* If necessary, insert an integer conversion */
    if (dstType != ScalarType::eU32) {
      auto result = m_builder.addBefore(op->getDef(),
        std::move(newOp.setType(ScalarType::eU32)));
      newOp = Op::ConvertItoI(dstType, result);
    }

    m_builder.rewriteOp(op->getDef(), std::move(newOp));
    return ++op;
  }
}


Builder::iterator ArithmeticPass::lowerF16toF32(Builder::iterator op) {
  const auto& a = m_builder.getOpForOperand(*op, 0u);
  dxbc_spv_assert(a.getType().isScalarType() && op->getType().isVectorType());

  auto srcType = a.getType().getBaseType(0u).getBaseType();
  dxbc_spv_assert(srcType == ScalarType::eU32 || srcType == ScalarType::eU16);

  auto dstType = op->getType().getBaseType(0u).getBaseType();
  dxbc_spv_assert(op->getType() == BasicType(dstType, 2u));

  /* Normalize input type */
  auto newOp = *op;

  if (srcType == ScalarType::eU16) {
    auto input = m_builder.addBefore(op->getDef(), Op::ConvertItoI(ScalarType::eU32, a.getDef()));
    newOp = Op::ConvertPackedF16toF32(op->getType(), input).setFlags(op->getFlags());
  }

  /* Rewrite as bitcast if the output type is f16 already */
  if (dstType == ScalarType::eF16)
    newOp = Op::Cast(op->getType(), SsaDef(newOp.getOperand(0u)));

  m_builder.rewriteOp(op->getDef(), std::move(newOp));
  return ++op;
}


Builder::iterator ArithmeticPass::lowerMsad(Builder::iterator op) {
  if (!m_msadFunction) {
    auto refParam = m_builder.add(Op::DclParam(ScalarType::eU32));
    auto srcParam = m_builder.add(Op::DclParam(ScalarType::eU32));
    auto accumParam = m_builder.add(Op::DclParam(ScalarType::eU32));

    m_builder.add(Op::DebugName(refParam, "ref"));
    m_builder.add(Op::DebugName(srcParam, "src"));
    m_builder.add(Op::DebugName(accumParam, "accum"));

    m_msadFunction = m_builder.addBefore(m_builder.getCode().first->getDef(),
      Op::Function(ScalarType::eU32).addParam(refParam).addParam(srcParam).addParam(accumParam));
    m_builder.add(Op::DebugName(m_msadFunction, "msad"));

    m_builder.setCursor(m_msadFunction);
    m_builder.add(Op::Label());

    auto ref = m_builder.add(Op::ParamLoad(ScalarType::eU32, m_msadFunction, refParam));
    auto src = m_builder.add(Op::ParamLoad(ScalarType::eU32, m_msadFunction, srcParam));
    auto accum = m_builder.add(Op::ParamLoad(ScalarType::eU32, m_msadFunction, accumParam));

    for (uint32_t i = 0u; i < 4u; i++) {
      auto refByte = m_builder.add(Op::UBitExtract(ScalarType::eU32, ref, m_builder.makeConstant(8u * i), m_builder.makeConstant(8u)));
      auto srcByte = m_builder.add(Op::UBitExtract(ScalarType::eU32, src, m_builder.makeConstant(8u * i), m_builder.makeConstant(8u)));

      auto absDiff = m_builder.add(Op::ISub(ScalarType::eU32,
        m_builder.add(Op::UMax(ScalarType::eU32, refByte, srcByte)),
        m_builder.add(Op::UMin(ScalarType::eU32, refByte, srcByte))));

      auto isNonZero = m_builder.add(Op::INe(ScalarType::eBool, refByte, m_builder.makeConstant(0u)));
      absDiff = m_builder.add(Op::Select(ScalarType::eU32, isNonZero, absDiff, m_builder.makeConstant(0u)));

      accum = m_builder.add(Op::IAdd(ScalarType::eU32, accum, absDiff));
    }

    m_builder.add(Op::Return(ScalarType::eU32, accum));
    m_builder.add(Op::FunctionEnd());
  }

  /* Msad lowering returns u32, convert as necessary */
  auto newOp = Op::FunctionCall(ScalarType::eU32, m_msadFunction)
    .setFlags(op->getFlags())
    .addOperand(SsaDef(op->getOperand(0u)))
    .addOperand(SsaDef(op->getOperand(1u)))
    .addOperand(SsaDef(op->getOperand(2u)));

  m_builder.rewriteOp(op->getDef(), std::move(newOp));
  return ++op;
}


Builder::iterator ArithmeticPass::lowerInputBuiltIn(Builder::iterator op) {
  auto builtIn = getBuiltInInput(*op);

  if (builtIn == BuiltIn::eGsVertexCountIn && m_options.lowerGsVertexCountIn) {
    dxbc_spv_assert(m_gsInputVertexCount);

    auto next = m_builder.rewriteDef(op->getDef(), m_builder.makeConstant(m_gsInputVertexCount));
    return m_builder.iter(next);
  }

  return ++op;
}


Builder::iterator ArithmeticPass::lowerSinCos(Builder::iterator op) {
  if (!m_sincosFunction) {
    BasicType resultType(ScalarType::eF32, 2u);

    auto param = m_builder.add(Op::DclParam(ScalarType::eF32));
    m_builder.add(Op::DebugName(param, "x"));

    m_sincosFunction = m_builder.addBefore(m_builder.getCode().first->getDef(),
      Op::Function(resultType).addParam(param));
    m_builder.add(Op::DebugName(m_sincosFunction, "sincos"));

    m_builder.setCursor(m_sincosFunction);
    m_builder.add(Op::Label());

    auto x = m_builder.add(Op::ParamLoad(ScalarType::eF32, m_sincosFunction, param));

    /* Normalize input to multiple of pi/4 */
    auto xNorm = m_builder.add(Op::FMul(ScalarType::eF32,
      m_builder.add(Op::FAbs(ScalarType::eF32, x).setFlags(OpFlag::eNoSz)),
      m_builder.makeConstant(float(4.0f / pi))).setFlags(OpFlag::ePrecise | OpFlag::eNoSz));

    /* Almost everything operates on values between [0..1) here, but may be nan */
    OpFlags commonFlags = OpFlag::ePrecise | OpFlag::eNoSz | OpFlag::eNoInf;

    auto xFract = m_builder.add(Op::FFract(ScalarType::eF32, xNorm).setFlags(OpFlag::eNoSz));
    auto xInt = m_builder.add(Op::ConvertFtoI(ScalarType::eU32, xNorm));

    /* Mirror input along x axis as necessary */
    auto mirror = m_builder.add(Op::INe(ScalarType::eBool,
      m_builder.add(Op::IAnd(ScalarType::eU32, xInt, m_builder.makeConstant(1u))),
      m_builder.makeConstant(0u)));

    xFract = m_builder.add(Op::Select(ScalarType::eF32, mirror,
      m_builder.add(Op::FSub(ScalarType::eF32, m_builder.makeConstant(1.0f), xFract).setFlags(commonFlags)),
      xFract).setFlags(OpFlag::eNoInf | OpFlag::eNoSz));

    /* Compute taylor series for fractional part */
    auto xFract_2 = m_builder.add(Op::FMul(ScalarType::eF32, xFract, xFract).setFlags(commonFlags));
    auto xFract_4 = m_builder.add(Op::FMul(ScalarType::eF32, xFract_2, xFract_2).setFlags(commonFlags));
    auto xFract_6 = m_builder.add(Op::FMul(ScalarType::eF32, xFract_4, xFract_2).setFlags(commonFlags));

    auto taylor = m_builder.add(Op::FMul(ScalarType::eF32, xFract_6, m_builder.makeConstant(-sincosTaylorFactor(7))).setFlags(commonFlags));
    taylor = m_builder.add(Op::FMad(ScalarType::eF32, xFract_4, m_builder.makeConstant(sincosTaylorFactor(5)), taylor).setFlags(commonFlags));
    taylor = m_builder.add(Op::FMad(ScalarType::eF32, xFract_2, m_builder.makeConstant(-sincosTaylorFactor(3)), taylor).setFlags(commonFlags));
    taylor = m_builder.add(Op::FAdd(ScalarType::eF32, m_builder.makeConstant(sincosTaylorFactor(1)), taylor).setFlags(commonFlags));
    taylor = m_builder.add(Op::FMul(ScalarType::eF32, taylor, xFract).setFlags(commonFlags));

    /* Compute co-function based on sin^2 + cos^2 = 1. This fma result
     * is always greater than 0, so the sqrt trick is safe. */
    auto coMad = m_builder.add(Op::FMad(ScalarType::eF32,
      m_builder.add(Op::FNeg(ScalarType::eF32, taylor).setFlags(OpFlag::eNoSz | OpFlag::eNoInf)), taylor,
      m_builder.makeConstant(1.0f)).setFlags(commonFlags));

    auto coFunc = m_builder.add(Op::FMul(ScalarType::eF32, coMad,
      m_builder.add(Op::FRsq(ScalarType::eF32, coMad).setFlags(commonFlags))).setFlags(commonFlags));

    /* Determine whether the taylor series was used for sine or cosine and assign the correct result */
    auto funcIsSin = m_builder.add(Op::IEq(ScalarType::eBool,
      m_builder.add(Op::IAnd(ScalarType::eU32,
        m_builder.add(Op::IAdd(ScalarType::eU32, xInt, m_builder.makeConstant(1u))),
        m_builder.makeConstant(2u))),
      m_builder.makeConstant(0u)));

    auto sin = m_builder.add(Op::Select(ScalarType::eF32, funcIsSin, taylor, coFunc).setFlags(OpFlag::eNoInf | OpFlag::eNoSz));
    auto cos = m_builder.add(Op::Select(ScalarType::eF32, funcIsSin, coFunc, taylor).setFlags(OpFlag::eNoInf | OpFlag::eNoSz));

    /* Determine whether sine is negative. Interpret the input as a
     * signed integer in order to propagate signed zeroes properly. */
    auto inputNeg = m_builder.add(Op::SLt(ScalarType::eBool,
      m_builder.add(Op::Cast(ScalarType::eI32, x)),
      m_builder.makeConstant(0)));

    auto sinNeg = m_builder.add(Op::INe(ScalarType::eBool,
      m_builder.add(Op::IAnd(ScalarType::eU32, xInt, m_builder.makeConstant(4u))),
      m_builder.makeConstant(0u)));

    sinNeg = m_builder.add(Op::BNe(ScalarType::eBool, sinNeg, inputNeg));

    /* Determine whether cosine is negative */
    auto cosNeg = m_builder.add(Op::INe(ScalarType::eBool,
      m_builder.add(Op::IAnd(ScalarType::eU32,
        m_builder.add(Op::IAdd(ScalarType::eU32, xInt, m_builder.makeConstant(2u))),
        m_builder.makeConstant(4u))),
      m_builder.makeConstant(0u)));

    sin = m_builder.add(Op::Select(ScalarType::eF32, sinNeg, m_builder.add(Op::FNeg(ScalarType::eF32, sin).setFlags(OpFlag::eNoInf)), sin).setFlags(OpFlag::eNoInf));
    cos = m_builder.add(Op::Select(ScalarType::eF32, cosNeg, m_builder.add(Op::FNeg(ScalarType::eF32, cos).setFlags(OpFlag::eNoInf)), cos).setFlags(OpFlag::eNoInf));

    m_builder.add(Op::Return(resultType, m_builder.add(Op::CompositeConstruct(resultType, sin, cos))));
    m_builder.add(Op::FunctionEnd());
  }

  /* Forward to custom sincos function */
  auto sincosCall = m_builder.addBefore(op->getDef(),
    Op::FunctionCall(BasicType(ScalarType::eF32, 2u), m_sincosFunction)
      .addParam(SsaDef(op->getOperand(0u))).setFlags(op->getFlags() | OpFlag::eNoInf));

  /* The result vector is vec2(sin, cos) */
  auto componentIndex = op->getOpCode() == OpCode::eFCos ? 1u : 0u;
  m_builder.rewriteOp(op->getDef(), Op::CompositeExtract(
    op->getType(), sincosCall, m_builder.makeConstant(componentIndex)));
  return ++op;
}


Op ArithmeticPass::emitMulLegacy(const Type& type, SsaDef a, SsaDef b) {
  /* a * b -> (b == 0 ? 0 : a) * (a == 0 ? 0 : b) */
  auto zero = m_builder.makeConstantZero(type);

  auto aEq0 = m_builder.add(Op::FEq(ScalarType::eBool, a, zero));
  auto bEq0 = m_builder.add(Op::FEq(ScalarType::eBool, b, zero));

  auto aOperand = m_builder.add(Op::Select(type, bEq0, zero, a));
  auto bOperand = m_builder.add(Op::Select(type, aEq0, zero, b));

  return Op::FMul(type, aOperand, bOperand);
}


Op ArithmeticPass::emitMadLegacy(const Type& type, SsaDef a, SsaDef b, SsaDef c) {
  auto op = emitMulLegacy(type, a, b);

  return Op::FMad(type, SsaDef(op.getOperand(0u)), SsaDef(op.getOperand(1u)), c);
}


SsaDef ArithmeticPass::buildMulLegacyFunc(OpCode opCode, BasicType type) {
  auto entry = std::find_if(m_mulLegacyFunctions.begin(), m_mulLegacyFunctions.end(),
    [opCode, type] (const MulLegacyFunc& func) {
      return func.opCode == opCode && func.type == type;
    });

  if (entry == m_mulLegacyFunctions.end()) {
    entry = &m_mulLegacyFunctions.emplace_back();
    entry->opCode = opCode;
    entry->type = type;

    SsaDef paramA = m_builder.add(Op::DclParam(type));
    SsaDef paramB = m_builder.add(Op::DclParam(type));
    SsaDef paramC = { };

    m_builder.add(Op::DebugName(paramA, "a"));
    m_builder.add(Op::DebugName(paramB, "b"));

    if (opCode == OpCode::eFMadLegacy) {
      paramC = m_builder.add(Op::DclParam(type));
      m_builder.add(Op::DebugName(paramC, "c"));
    }

    std::stringstream debugName;
    debugName << (opCode == OpCode::eFMadLegacy ? "mad_legacy_" : "mul_legacy_");
    debugName << type;

    auto functionOp = Op::Function(type).addParam(paramA).addParam(paramB);

    if (paramC)
      functionOp.addParam(paramC);

    entry->function = m_builder.addBefore(m_builder.getCode().first->getDef(), std::move(functionOp));
    m_builder.add(Op::DebugName(entry->function, debugName.str().c_str()));

    m_builder.setCursor(entry->function);
    m_builder.add(Op::Label());

    SsaDef a = m_builder.add(Op::ParamLoad(type, entry->function, paramA));
    SsaDef b = m_builder.add(Op::ParamLoad(type, entry->function, paramB));
    SsaDef c = { };

    if (paramC)
      c = m_builder.add(Op::ParamLoad(type, entry->function, paramC));

    Op resultOp(OpCode::eCompositeConstruct, type);

    for (uint32_t i = 0u; i < type.getVectorSize(); i++) {
      auto aScalar = a;
      auto bScalar = b;
      auto cScalar = c;

      if (type.isVector()) {
        aScalar = m_builder.add(Op::CompositeExtract(type.getBaseType(), a, m_builder.makeConstant(i)));
        bScalar = m_builder.add(Op::CompositeExtract(type.getBaseType(), b, m_builder.makeConstant(i)));

        if (c)
          cScalar = m_builder.add(Op::CompositeExtract(type.getBaseType(), c, m_builder.makeConstant(i)));
      }

      auto resultScalar = m_builder.add(opCode == OpCode::eFMadLegacy
        ? emitMadLegacy(type.getBaseType(), aScalar, bScalar, cScalar)
        : emitMulLegacy(type.getBaseType(), aScalar, bScalar));

      resultOp.addOperand(resultScalar);
    }

    auto result = SsaDef(resultOp.getOperand(0u));

    if (type.isVector())
      result = m_builder.add(std::move(resultOp));

    m_builder.add(Op::Return(type, result));
    m_builder.add(Op::FunctionEnd());
  }

  return entry->function;
}


SsaDef ArithmeticPass::buildPowLegacyFunc(BasicType type) {
  auto entry = std::find_if(m_powLegacyFunctions.begin(), m_powLegacyFunctions.end(),
    [type] (const PowLegacyFunc& func) {
      return func.type == type;
    });

  if (entry == m_powLegacyFunctions.end()) {
    dxbc_spv_assert(type.isScalar());

    entry = &m_powLegacyFunctions.emplace_back();
    entry->type = type;

    SsaDef paramA = m_builder.add(Op::DclParam(type));
    SsaDef paramB = m_builder.add(Op::DclParam(type));

    m_builder.add(Op::DebugName(paramA, "a"));
    m_builder.add(Op::DebugName(paramB, "b"));

    std::stringstream debugName;
    debugName << "pow_legacy_" << type;

    entry->function = m_builder.addBefore(m_builder.getCode().first->getDef(),
      Op::Function(type).addParam(paramA).addParam(paramB));

    m_builder.add(Op::DebugName(entry->function, debugName.str().c_str()));

    m_builder.setCursor(entry->function);
    m_builder.add(Op::Label());

    SsaDef a = m_builder.add(Op::ParamLoad(type, entry->function, paramA));
    SsaDef b = m_builder.add(Op::ParamLoad(type, entry->function, paramB));

    SsaDef result = m_builder.add(Op::FLog2(type, a));
    result = m_builder.add(emitMulLegacy(type, b, result));
    result = m_builder.add(Op::FExp2(type, result));

    m_builder.add(Op::Return(type, result));
    m_builder.add(Op::FunctionEnd());
  }

  return entry->function;
}


SsaDef ArithmeticPass::buildF32toF16Func() {
  if (!m_f32tof16Function) {
    auto param = m_builder.add(Op::DclParam(BasicType(ScalarType::eF32, 2u)));
    m_builder.add(Op::DebugName(param, "v"));

    m_f32tof16Function = m_builder.addBefore(m_builder.getCode().first->getDef(),
      Op::Function(ScalarType::eU32).addParam(param));
    m_builder.add(Op::DebugName(m_f32tof16Function, "f32_to_f16"));

    m_builder.setCursor(m_f32tof16Function);
    m_builder.add(Op::Label());

    /* f32tof16 requires RTZ semantics, but we have no way to guarantee that.
     * At least ensure that only infinity inputs will result in infinity output. */
    auto v = m_builder.add(Op::ParamLoad(BasicType(ScalarType::eF32, 2u), m_f32tof16Function, param));
    auto fltMaxConst = m_builder.makeConstant(std::numeric_limits<float>::max());

    std::array<SsaDef, 2u> components = { };

    for (uint32_t i = 0u; i < 2u; i++) {
      auto scalar = m_builder.add(Op::CompositeExtract(ScalarType::eF32, v, m_builder.makeConstant(i)));

      /* This check implicitly fails for nan inputs as well */
      auto isFinite = m_builder.add(Op::FLe(ScalarType::eBool,
        m_builder.add(Op::FAbs(ScalarType::eF32, scalar)), fltMaxConst));

      auto clamped = m_builder.add(Op::FClamp(ScalarType::eF32, scalar, m_builder.makeConstant(-65504.0f), m_builder.makeConstant(65504.0f)));
      components.at(i) = m_builder.add(Op::Select(ScalarType::eF32, isFinite, clamped, scalar));
    }

    /* Build input vector for f32tof16 instruction */
    auto input = m_builder.add(Op::CompositeConstruct(
      BasicType(ScalarType::eF32, 2u), components.at(0u), components.at(1u)));

    auto result = m_builder.add(Op::ConvertF32toPackedF16(ScalarType::eU32, input));
    m_builder.add(Op::Return(ScalarType::eU32, result));
    m_builder.add(Op::FunctionEnd());
  }

  return m_f32tof16Function;
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


std::pair<bool, Builder::iterator> ArithmeticPass::selectCompare(Builder::iterator op) {
  if (!op->getType().isScalarType())
    return std::make_pair(false, ++op);

  /* Find a select operand */
  uint32_t selectOperand = -1u;

  for (uint32_t i = 0u; i < op->getOperandCount(); i++) {
    if (m_builder.getOpForOperand(*op, i).getOpCode() == OpCode::eSelect)
      selectOperand = i;
  }

  if (selectOperand == -1u)
    return std::make_pair(false, ++op);

  const auto& a = m_builder.getOpForOperand(*op, selectOperand);
  const auto& b = m_builder.getOpForOperand(*op, selectOperand ^ 1u);

  /* select(cond, a, b) == c -> select(cond, a == c, b == c)
   * select(cond, a, b) != c -> select(cond, a != c, b != c) */
  auto condDef = SsaDef(a.getOperand(0u));
  auto tDef = SsaDef(a.getOperand(1u));
  auto fDef = SsaDef(a.getOperand(2u));

  tDef = m_builder.addBefore(op->getDef(), Op(op->getOpCode(), op->getType()).addOperands(tDef, b.getDef()));
  fDef = m_builder.addBefore(op->getDef(), Op(op->getOpCode(), op->getType()).addOperands(fDef, b.getDef()));

  /* Avoid looping indefinitely if we create a new instance of the same pattern */
  auto ref = op->getDef();

  if (m_builder.getOpForOperand(tDef, 0u).getOpCode() != OpCode::eSelect &&
      m_builder.getOpForOperand(fDef, 0u).getOpCode() != OpCode::eSelect)
    ref = tDef;

  m_builder.rewriteOp(op->getDef(), Op::Select(op->getType(), condDef, tDef, fDef));
  return std::make_pair(true, m_builder.iter(ref));
}


std::pair<bool, Builder::iterator> ArithmeticPass::selectBitOp(Builder::iterator op) {
  if (!op->getType().isScalarType())
    return std::make_pair(false, ++op);

  switch (op->getOpCode()) {
    case OpCode::eIAbs:
    case OpCode::eINot:
    case OpCode::eINeg: {
      const auto& a = m_builder.getOpForOperand(*op, 0u);

      if (isConstantSelect(a)) {
        auto trueDef = m_builder.addBefore(op->getDef(),
          Op(op->getOpCode(), op->getType()).addOperand(SsaDef(a.getOperand(1u))));
        auto falseDef = m_builder.addBefore(op->getDef(),
          Op(op->getOpCode(), op->getType()).addOperand(SsaDef(a.getOperand(2u))));

        m_builder.rewriteOp(op->getDef(), Op::Select(op->getType(),
          SsaDef(a.getOperand(0u)), trueDef, falseDef).setFlags(a.getFlags()));

        return std::make_pair(true, m_builder.iter(trueDef));
      }
    } break;

    case OpCode::eIAnd:
    case OpCode::eIOr:
    case OpCode::eIXor: {
      const auto& a = m_builder.getOpForOperand(*op, 0u);
      const auto& b = m_builder.getOpForOperand(*op, 1u);

      if (isConstantSelect(a) && isConstantSelect(b)) {
        /* Handle patterns such as:
         * select(c0, a, 0) & select(c1, a, 0) -> select(c0 && c1, a, 0)
         * select(c0, a, 0) | select(c1, a, 0) -> select(c0 || c1, a, 0)
         * select(c0, a, 0) ^ select(c1, a, 0) -> select(c0 != c1, a, 0) */
        auto at = SsaDef(a.getOperand(1u));
        auto af = SsaDef(a.getOperand(2u));
        auto bt = SsaDef(b.getOperand(1u));
        auto bf = SsaDef(b.getOperand(2u));

        if (((at == bt && af == bf) || (at == bf && af == bt)) &&
            (isConstantValue(m_builder.getOp(at), 0) ||
             isConstantValue(m_builder.getOp(af), 0))) {
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

      /* Aggressively fold these specific ops into constant select to resolve cases
       * where a single condition is used to produce multiple non-boolean values. */
      for (uint32_t i = 0u; i < op->getOperandCount(); i++) {
        const auto& a = m_builder.getOpForOperand(*op, i);
        const auto& b = m_builder.getOpForOperand(*op, i ^ 1u);

        if (isConstantSelect(a)) {
          auto trueDef = m_builder.addBefore(op->getDef(), Op(op->getOpCode(), op->getType())
            .addOperands(SsaDef(a.getOperand(1u)), b.getDef()));
          auto falseDef = m_builder.addBefore(op->getDef(), Op(op->getOpCode(), op->getType())
            .addOperands(SsaDef(a.getOperand(2u)), b.getDef()));

          m_builder.rewriteOp(op->getDef(), Op::Select(op->getType(),
            SsaDef(a.getOperand(0u)), trueDef, falseDef).setFlags(a.getFlags()));

          return std::make_pair(true, m_builder.iter(trueDef));
        }
      }
    } break;

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
      const auto& a = m_builder.getOpForOperand(*op, 0u);
      const auto& b = m_builder.getOpForOperand(*op, 1u);

      if (isConstantSelect(a) && b.isConstant()) {
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


std::pair<bool, Builder::iterator> ArithmeticPass::selectPhi(Builder::iterator op) {
  /* Boolean phis already are in the format that we want */
  if (!op->getType().isBasicType() || op->getType().getBaseType(0u).isBoolType())
    return std::make_pair(false, ++op);

  /* Determine whether a phi can only return one of two different constant values.
   * This is common for simple conditional boolean assignments in DXBC. */
  util::small_vector<SsaDef, 2u> constants;

  for (uint32_t i = 1u; i < op->getOperandCount(); i += 2u) {
    const auto& arg = m_builder.getOpForOperand(*op, i);
    util::small_vector<SsaDef, 2u> incoming;

    if (arg.isConstant()) {
      /* Add operand itself */
      incoming.push_back(arg.getDef());
    } else if (arg.getOpCode() == OpCode::eSelect) {
      /* Select condition must be scalar */
      if (!m_builder.getOpForOperand(arg, 0u).getType().isScalarType())
        return std::make_pair(false, ++op);

      /* Add true and false operands */
      incoming.push_back(SsaDef(arg.getOperand(1u)));
      incoming.push_back(SsaDef(arg.getOperand(2u)));
    } else {
      /* Unsupported op */
      return std::make_pair(false, ++op);
    }

    for (auto candidate : incoming) {
      if (!m_builder.getOp(candidate).isConstant())
        return std::make_pair(false, ++op);

      bool found = false;

      for (auto constant : constants)
        found = found || constant == candidate;

      if (!found && constants.size() == 2u)
        return std::make_pair(false, ++op);

      if (!found)
        constants.push_back(candidate);
    }
  }

  if (constants.size() != 2u)
    return std::make_pair(false, ++op);

  /* Iterate over phi operands again and build new boolean phi */
  Op newPhi(OpCode::ePhi, ScalarType::eBool);

  forEachPhiOperand(*op, [&] (SsaDef block, SsaDef value) {
    const auto& arg = m_builder.getOp(value);

    if (arg.isConstant()) {
      /* Select will assume the first constant is the 'true' condition */
      newPhi.addPhi(block, m_builder.makeConstant(value == constants.at(0u)));
    } else {
      dxbc_spv_assert(arg.getOpCode() == OpCode::eSelect);

      auto condDef = SsaDef(arg.getOperand(0u));
      auto tDef = SsaDef(arg.getOperand(1u));
      auto fDef = SsaDef(arg.getOperand(2u));

      if (tDef == constants.at(0u)) {
        dxbc_spv_assert(fDef == constants.at(1u));
        newPhi.addPhi(block, condDef);
      } else if (fDef == constants.at(0u)) {
        dxbc_spv_assert(tDef == constants.at(1u));

        /* Need to invert the condition */
        condDef = m_builder.addAfter(value, Op::BNot(ScalarType::eBool, condDef));
        newPhi.addPhi(block, condDef);
      } else {
        dxbc_spv_unreachable();
      }
    }
  });

  auto selectCond = m_builder.addAfter(op->getDef(), std::move(newPhi));

  /* Insert Select op at the end of the phi block */
  auto ref = selectCond;

  while (m_builder.getOp(ref).getOpCode() == OpCode::ePhi)
    ref = m_builder.getNext(ref);

  auto next = m_builder.rewriteDef(op->getDef(), m_builder.addBefore(ref,
    Op::Select(op->getType(), selectCond, constants.at(0u), constants.at(1u))));

  return std::make_pair(true, m_builder.iter(next));
}


std::pair<bool, Builder::iterator> ArithmeticPass::selectFDot(Builder::iterator op) {
  /* A common pattern in DXBC shaders is dot(v, vec4(i == 0, i == 1, i == 2, i == 3)),
   * where the latter is the result of an immediate constant buffer optimization.
   * Jump Space relies on us further optimizing this pattern in order to avoid an
   * inf * 0 = nan situation. */
  if (op->getOpCode() != OpCode::eFDotLegacy && (getFpFlags(*op) & OpFlag::ePrecise))
    return std::make_pair(false, ++op);

  std::optional<uint32_t> selectOperand = { };

  for (uint32_t i = 0u; i < op->getOperandCount(); i++) {
    const auto& arg = m_builder.getOpForOperand(*op, i);

    if (arg.getOpCode() != OpCode::eCompositeConstruct)
      continue;

    bool isSelectOperand = true;

    for (uint32_t j = 0u; j < arg.getOperandCount(); j++) {
      if (!isFloatSelect(m_builder.getOpForOperand(arg, j))) {
        isSelectOperand = false;
        break;
      }
    }

    if (isSelectOperand) {
      selectOperand = i;
      break;
    }
  }

  if (!selectOperand)
    return std::make_pair(false, ++op);

  /* Rewrite dot product as a plain sequence of multiply and add
   * operations that we can then optimize individually. */
  const auto& select = m_builder.getOpForOperand(*op, *selectOperand);
  const auto& values = m_builder.getOpForOperand(*op, *selectOperand ^ 1u);

  dxbc_spv_assert(select.getOpCode() == OpCode::eCompositeConstruct);

  SsaDef reference = { };
  SsaDef result = { };

  bool isLegacy = op->getOpCode() == OpCode::eFDotLegacy;

  for (uint32_t i = 0u; i < select.getOperandCount(); i++) {
    auto a = m_builder.addBefore(op->getDef(), Op::CompositeExtract(op->getType(), values.getDef(), m_builder.makeConstant(i)));
    auto b = m_builder.getOpForOperand(select, i).getDef();

    auto product = m_builder.addBefore(op->getDef(), isLegacy
      ? Op::FMulLegacy(op->getType(), a, b).setFlags(op->getFlags())
      : Op::FMul(op->getType(), a, b).setFlags(op->getFlags()));

    if (result) {
      result = m_builder.addBefore(op->getDef(),
        Op::FAdd(op->getType(), result, product).setFlags(op->getFlags()));
    } else {
      result = product;
      reference = product;
    }
  }

  m_builder.rewriteDef(op->getDef(), result);
  return std::make_pair(true, m_builder.iter(reference));
}


std::pair<bool, Builder::iterator> ArithmeticPass::selectFMul(Builder::iterator op) {
  if (getFpFlags(*op) & OpFlag::ePrecise)
    return std::make_pair(false, ++op);

  /* This transform avoids generating new NaNs if a is infinite.
   * a * select(cond, b, 0) -> select(cond, a * b, 0 * b)
   * a * select(cond, 0, b) -> select(cond, 0 * b, a * b) */
  std::optional<uint32_t> selectOperand = { };

  for (uint32_t i = 0u; i < 2u; i++) {
    if (isFloatSelect(m_builder.getOpForOperand(*op, i))) {
      selectOperand = i;
      break;
    }
  }

  if (!selectOperand)
    return std::make_pair(false, ++op);

  const auto& select = m_builder.getOpForOperand(*op, *selectOperand);
  const auto& value = m_builder.getOpForOperand(*op, *selectOperand ^ 1u);

  auto cond = m_builder.getOpForOperand(select, 0u).getDef();
  auto a = m_builder.getOpForOperand(select, 1u).getDef();
  auto b = m_builder.getOpForOperand(select, 2u).getDef();

  a = m_builder.addBefore(op->getDef(), Op(op->getOpCode(), op->getType()).addOperands(value.getDef(), a).setFlags(op->getFlags()));
  b = m_builder.addBefore(op->getDef(), Op(op->getOpCode(), op->getType()).addOperands(value.getDef(), b).setFlags(op->getFlags()));

  m_builder.rewriteOp(op->getDef(), Op::Select(op->getType(), cond, a, b).setFlags(op->getFlags()));
  return std::make_pair(true, op);
}


std::pair<bool, Builder::iterator> ArithmeticPass::selectFAdd(Builder::iterator op) {
  if (getFpFlags(*op) & OpFlag::ePrecise)
    return std::make_pair(false, ++op);

  /* If we can statically prove that c0 and c1 are mutually exclusive:
   * select(c0, a, 0) + select(c1, b, c) -> select(c0, a, select(c1, b, c))
   * select(c0, a, b) + select(c1, c, 0) -> select(c1, c, select(c0, a, b))
   * Note that this optimization may eliminate a denorm flush. */
  const auto& selectA = m_builder.getOpForOperand(*op, 0u);
  const auto& selectB = m_builder.getOpForOperand(*op, 1u);

  if (selectA.getOpCode() != OpCode::eSelect || selectB.getOpCode() != OpCode::eSelect)
    return std::make_pair(false, ++op);

  const auto& ac = m_builder.getOpForOperand(selectA, 0u);
  const auto& at = m_builder.getOpForOperand(selectA, 1u);
  const auto& af = m_builder.getOpForOperand(selectA, 2u);

  const auto& bc = m_builder.getOpForOperand(selectB, 0u);
  const auto& bt = m_builder.getOpForOperand(selectB, 1u);
  const auto& bf = m_builder.getOpForOperand(selectB, 2u);

  if (evalBAnd(ac, bc) != std::make_optional(false))
    return std::make_pair(false, ++op);

  if (isConstantValue(bf, 0)) {
    m_builder.rewriteOp(op->getDef(), Op::Select(op->getType(), bc.getDef(), bt.getDef(), selectA.getDef()).setFlags(op->getFlags()));
    return std::make_pair(true, op);
  }

  if (isConstantValue(af, 0)) {
    m_builder.rewriteOp(op->getDef(), Op::Select(op->getType(), ac.getDef(), at.getDef(), selectB.getDef()).setFlags(op->getFlags()));
    return std::make_pair(true, op);
  }

  return std::make_pair(false, ++op);
}


std::pair<bool, Builder::iterator> ArithmeticPass::selectOp(Builder::iterator op) {
  switch (op->getOpCode()) {
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

    case OpCode::ePhi:
      return selectPhi(op);

    case OpCode::eFDot:
    case OpCode::eFDotLegacy:
      return selectFDot(op);

    case OpCode::eFMul:
    case OpCode::eFMulLegacy:
      return selectFMul(op);

    case OpCode::eFAdd:
      return selectFAdd(op);

    default:
      return std::make_pair(false, ++op);
  }
}


std::pair<bool, Builder::iterator> ArithmeticPass::resolveCastOp(Builder::iterator op) {
  auto [status, next] = LowerConsumePass(m_builder).resolveCastChain(op->getDef());

  if (status)
    return std::make_pair(status, m_builder.iter(next));

  /* Skip any further processing if there are any casts using this cast, resolve
   * those first. */
  auto [x, y] = m_builder.getUses(op->getDef());

  for (auto i = x; i != y; i++) {
    if (i->getOpCode() == OpCode::eCast)
      return std::make_pair(false, ++op);
  }

  /* Fold casts of phi ops into phi operands if all operands are bit-preserving ops.
   * However, only fold into already visited blocks to avoid feedback loops. */
  const auto& arg = m_builder.getOpForOperand(*op, 0u);

  if (arg.getOpCode() == OpCode::eSelect) {
    /* Don't fold if there are non-cast uses already since we'd
     * just duplicate the instruction for no good reason. */
    auto [x, y] = m_builder.getUses(arg.getDef());

    for (auto i = x; i != y; i++) {
      if (i->getOpCode() != OpCode::eCast)
        return std::make_pair(false, ++op);
    }

    /* Also only fold if there is something to gain, i.e. if any operand
     * is another cast, constant, or select that can be handled recursively. */
    const auto& tOp = m_builder.getOpForOperand(arg, 1u);
    const auto& fOp = m_builder.getOpForOperand(arg, 2u);

    if (!isBitPreservingOp(tOp) && !isBitPreservingOp(fOp))
      return std::make_pair(false, ++op);

    /* Override the *old* select instruction as one using the new type. This is
     * safe because we know all uses are casts, and we can eliminate any casts
     * that became redundant later. */
    auto tDef = m_builder.addBefore(arg.getDef(), Op::Cast(op->getType(), tOp.getDef()));
    auto fDef = m_builder.addBefore(arg.getDef(), Op::Cast(op->getType(), fOp.getDef()));

    m_builder.rewriteOp(arg.getDef(), Op::Select(op->getType(),
      SsaDef(arg.getOperand(0u)), tDef, fDef).setFlags(arg.getFlags()));

    return std::make_pair(true, m_builder.iter(tDef));
  }

  if (arg.getOpCode() == OpCode::ePhi) {
    /* Much like with selects, only fold if all uses of the phi are casts. */
    auto [x, y] = m_builder.getUses(arg.getDef());

    for (auto i = x; i != y; i++) {
      if (i->getOpCode() != OpCode::eCast)
        return std::make_pair(false, ++op);
    }

    bool canFold = true;

    forEachPhiOperand(arg, [&] (SsaDef block, SsaDef value) {
      canFold = canFold && isBitPreservingOp(m_builder.getOp(value)) &&
        (m_visitedBlocks.find(block) != m_visitedBlocks.end());
    });

    if (!canFold)
      return std::make_pair(false, ++op);

    /* Build new phi instruction */
    Op newPhi(OpCode::ePhi, op->getType());

    forEachPhiOperand(arg, [&] (SsaDef block, SsaDef value) {
      auto terminator = block;

      while (!isBlockTerminator(m_builder.getOp(terminator).getOpCode()))
        terminator = m_builder.getNext(terminator);

      value = m_builder.addBefore(terminator, Op::Cast(op->getType(), value));
      newPhi.addPhi(block, value);
    });

    m_builder.rewriteOp(arg.getDef(), std::move(newPhi));
    return std::make_pair(true, op);
  }

  return std::make_pair(false, ++op);
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
      if (a.getOpCode() == OpCode::eISub || (a.getOpCode() == OpCode::eFSub && (getFpFlags(a) & OpFlag::eNoSz))) {
        m_builder.rewriteOp(op->getDef(), Op(a.getOpCode(), op->getType())
          .setFlags(op->getFlags() | a.getFlags())
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
    } break;

    case OpCode::eFMul:
    case OpCode::eFMulLegacy: {
      const auto& a = m_builder.getOpForOperand(*op, 0u);
      const auto& b = m_builder.getOpForOperand(*op, 1u);

      if (!(getFpFlags(*op) & OpFlag::ePrecise) &&  b.isConstant() &&
          op->getType().getBaseType(0u).isScalar()) {
        /* a * 0 -> 0 */
        if (getConstantAsFloat(b, 0u) == 0.0) {
          auto next = m_builder.rewriteDef(op->getDef(), m_builder.makeConstantZero(op->getType()));
          return std::make_pair(true, m_builder.iter(next));
        }

        /* a * 1 -> a */
        if (getConstantAsFloat(b, 0u) == 1.0) {
          auto next = m_builder.rewriteDef(op->getDef(), a.getDef());
          return std::make_pair(true, m_builder.iter(next));
        }

        /* a * -1 -> -a */
        if (getConstantAsFloat(b, 0u) == -1.0) {
          m_builder.rewriteOp(op->getDef(), Op::FNeg(op->getType(), a.getDef()).setFlags(a.getFlags()));
          return std::make_pair(true, op);
        }
      }
    } break;

    case OpCode::eFDiv: {
      const auto& a = m_builder.getOpForOperand(*op, 0u);
      const auto& b = m_builder.getOpForOperand(*op, 1u);

      /* +/- 1.0 / a -> rcp(a) */
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
            m_builder.rewriteOp(op->getDef(),
              Op::FNeg(op->getType(), rcpDef).setFlags(op->getFlags()));
            return std::make_pair(true, op);
          }
        }
      }
    } break;

    case OpCode::eUMin: {
      const auto& a = m_builder.getOpForOperand(*op, 0u);
      const auto& b = m_builder.getOpForOperand(*op, 1u);

      if (b.isConstant() && op->getType().isScalarType()) {
        /* umin(a & b, c) -> a & b if b <= c */
        if (a.getOpCode() == OpCode::eIAnd) {
          const auto& a1 = m_builder.getOpForOperand(a, 1u);

          if (a1.isConstant() && uint64_t(a1.getOperand(0u)) <= uint64_t(b.getOperand(0u))) {
            auto next = m_builder.rewriteDef(op->getDef(), a.getDef());
            return std::make_pair(true, m_builder.iter(next));
          }
        }

        /* umin(bfe(b, o, cnt), c) -> bfe(b, o, cnt) if (1u << cnt) - 1u <= c */
        if (a.getOpCode() == OpCode::eUBitExtract) {
          const auto& cnt = m_builder.getOpForOperand(a, 2u);

          if (cnt.isConstant() && (uint64_t(1u) << uint64_t(cnt.getOperand(0u))) - 1u <= uint64_t(b.getOperand(0u))) {
            auto next = m_builder.rewriteDef(op->getDef(), a.getDef());
            return std::make_pair(true, m_builder.iter(next));
          }
        }
      }
    } [[fallthrough]];

    case OpCode::eFMin:
    case OpCode::eSMin:
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

    case OpCode::eISub: {
      const auto& a = m_builder.getOpForOperand(*op, 0u);
      const auto& b = m_builder.getOpForOperand(*op, 1u);

      if (a.isConstant() && (b.getOpCode() == OpCode::eIAdd || b.getOpCode() == OpCode::eISub)) {
        const auto& b0 = m_builder.getOpForOperand(b, 0u);
        const auto& b1 = m_builder.getOpForOperand(b, 1u);

        /* c0 - (c1 + a) -> (c0 - c1) - a
         * c0 - (c1 - a) -> (c0 - c1) + a */
        if (b0.isConstant()) {
          auto constDef = m_builder.addBefore(op->getDef(), Op::ISub(op->getType(), a.getDef(), b0.getDef()));

          m_builder.rewriteOp(op->getDef(), b.getOpCode() == OpCode::eIAdd
            ? Op::ISub(op->getType(), constDef, b1.getDef())
            : Op::IAdd(op->getType(), constDef, b1.getDef()));

          return std::make_pair(true, m_builder.iter(constDef));
        }

        /* c0 - (a + c1) -> (c0 - c1) - a
         * c0 - (a - c1) -> (c0 + c1) - a */
        if (b1.isConstant()) {
          auto constDef = m_builder.addBefore(op->getDef(), b.getOpCode() == OpCode::eIAdd
            ? Op::ISub(op->getType(), a.getDef(), b1.getDef())
            : Op::IAdd(op->getType(), a.getDef(), b1.getDef()));

          m_builder.rewriteOp(op->getDef(), Op::ISub(op->getType(), constDef, b0.getDef()));
          return std::make_pair(true, m_builder.iter(constDef));
        }
      }
    } [[fallthrough]];

    case OpCode::eIAdd: {
      const auto& a = m_builder.getOpForOperand(*op, 0u);
      const auto& b = m_builder.getOpForOperand(*op, 1u);

      /* (a + const) + b -> a + (const + b)
       * (a + const) - b -> a + (const - b)
       * (a - const) + b -> a - (const - b)
       * (a - const) - b -> a - (const + b) */
      if (b.isConstant() && (a.getOpCode() == OpCode::eIAdd || a.getOpCode() == OpCode::eISub)) {
        const auto& a0 = m_builder.getOpForOperand(a, 0u);
        const auto& a1 = m_builder.getOpForOperand(a, 1u);

        if (a1.isConstant()) {
          auto constDef = m_builder.addBefore(op->getDef(), a.getOpCode() == op->getOpCode()
            ? Op::IAdd(op->getType(), a1.getDef(), b.getDef())
            : Op::ISub(op->getType(), a1.getDef(), b.getDef()));

          m_builder.rewriteOp(op->getDef(), a.getOpCode() == OpCode::eISub
            ? Op::ISub(op->getType(), a0.getDef(), constDef)
            : Op::IAdd(op->getType(), a0.getDef(), constDef));

          return std::make_pair(true, m_builder.iter(constDef));
        }
      }
    } [[fallthrough]];

    case OpCode::eFAdd:
    case OpCode::eFSub: {
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
      if (b.isConstant()) {
        Op constant(OpCode::eConstant, b.getType());

        bool isConstantNegative = false;
        bool isConstantPositive = false;

        for (uint32_t i = 0u; i < b.getOperandCount(); i++) {
          auto baseType = b.getType().getBaseType(0u);

          if (baseType.isIntType()) {
            auto value = getConstantAsSint(b, i);

            isConstantNegative = isConstantNegative || value < 0;
            isConstantPositive = isConstantPositive || value > 0;

            auto negOperand = makeScalarOperand(b.getType(), -value);
            auto posOperand = makeScalarOperand(b.getType(), value);

            constant.addOperand(negOperand);

            /* Negating the minimum representable value results in a
             * negative number again, ensure that we ignore that case. */
            if (value && negOperand == posOperand)
              isConstantPositive = true;
          } else {
            dxbc_spv_assert(baseType.isFloatType());

            auto signBit = uint64_t(1u) << (byteSize(baseType.getBaseType()) * 8u - 1u);

            auto posOperand = b.getOperand(i);
            auto negOperand = Operand(signBit ^ uint64_t(posOperand));

            isConstantNegative = isConstantNegative || (uint64_t(posOperand) & signBit);
            isConstantPositive = isConstantPositive || !(uint64_t(posOperand) & signBit);

            constant.addOperand(negOperand);
          }
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

    case OpCode::eINot: {
      /* ~(~a) -> a */
      const auto& a = m_builder.getOpForOperand(*op, 0u);

      if (a.getOpCode() == OpCode::eINot) {
        auto next = m_builder.rewriteDef(op->getDef(), SsaDef(a.getOperand(0u)));
        return std::make_pair(true, m_builder.iter(next));
      }
    } break;

    case OpCode::eIAnd: {
      const auto& a = m_builder.getOpForOperand(*op, 0u);
      const auto& b = m_builder.getOpForOperand(*op, 1u);

      /* a & 0 -> 0 */
      if (isConstantValue(b, 0)) {
        auto next = m_builder.rewriteDef(op->getDef(), b.getDef());
        return std::make_pair(true, m_builder.iter(next));
      }

      /* a & -1 -> a */
      if (isConstantValue(b, -1)) {
        auto next = m_builder.rewriteDef(op->getDef(), a.getDef());
        return std::make_pair(true, m_builder.iter(next));
      }
    } break;

    case OpCode::eIOr: {
      const auto& a = m_builder.getOpForOperand(*op, 0u);
      const auto& b = m_builder.getOpForOperand(*op, 1u);

      /* a | 0 -> a */
      if (isConstantValue(b, 0)) {
        auto next = m_builder.rewriteDef(op->getDef(), a.getDef());
        return std::make_pair(true, m_builder.iter(next));
      }

      /* a & -1 -> -1 */
      if (isConstantValue(b, -1)) {
        auto next = m_builder.rewriteDef(op->getDef(), b.getDef());
        return std::make_pair(true, m_builder.iter(next));
      }
    } break;

    case OpCode::eIXor: {
      const auto& a = m_builder.getOpForOperand(*op, 0u);
      const auto& b = m_builder.getOpForOperand(*op, 1u);

      /* a ^ 0 -> a */
      if (isConstantValue(b, 0)) {
        auto next = m_builder.rewriteDef(op->getDef(), a.getDef());
        return std::make_pair(true, m_builder.iter(next));
      }

      /* a ^ -1 -> ~a */
      if (isConstantValue(b, -1)) {
        m_builder.rewriteOp(op->getDef(), Op::INot(op->getType(), a.getDef()));
        return std::make_pair(true, op);
      }
    } break;

    case OpCode::eIShl:
    case OpCode::eSShr:
    case OpCode::eUShr: {
      const auto& value = m_builder.getOpForOperand(*op, 0u);
      const auto& shift = m_builder.getOpForOperand(*op, 1u);

      /* a << 0 -> a */
      if (isConstantValue(shift, 0)) {
        auto next = m_builder.rewriteDef(op->getDef(), value.getDef());
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

    if (getFpFlags(a) & OpFlag::eNoNan) {
      auto next = m_builder.rewriteDef(op->getDef(), m_builder.makeConstant(false));
      return std::make_pair(true, m_builder.iter(next));
    }

    return std::make_pair(false, ++op);
  }

  /* If b and c are constants, we can evaluate:
   * and(a, b) < c -> true if b < c
   * min(a, b) < c -> true if b < c
   * bfe(a, b, c) < d -> true if (1u << c) <= d
   * These are common patterns for bound-checking. */
  const auto& a = m_builder.getOpForOperand(*op, 0u);
  const auto& b = m_builder.getOpForOperand(*op, 1u);

  if (op->getOpCode() == OpCode::eULt && b.isConstant()) {
    if (a.getOpCode() == OpCode::eUMin || a.getOpCode() == OpCode::eIAnd) {
      const auto& a1 = m_builder.getOpForOperand(a, 1u);

      if (a1.isConstant() && uint64_t(a1.getOperand(0u)) < uint64_t(b.getOperand(0u))) {
        auto next = m_builder.rewriteDef(op->getDef(), m_builder.makeConstant(true));
        return std::make_pair(true, m_builder.iter(next));
      }
    }

    if (a.getOpCode() == OpCode::eUBitExtract) {
      const auto& cnt = m_builder.getOpForOperand(a, 2u);

      if (cnt.isConstant() && (uint64_t(1u) << uint64_t(cnt.getOperand(0u))) <= uint64_t(b.getOperand(0u))) {
        auto next = m_builder.rewriteDef(op->getDef(), m_builder.makeConstant(true));
        return std::make_pair(true, m_builder.iter(next));
      }
    }
  }

  /* (a + c0) == c1 -> a == c1 - c0
   * (a - c0) == c1 -> a == c1 + c0
   * (c0 - a) == c1 -> a == c0 - c1 */
  if (b.isConstant() && (a.getOpCode() == OpCode::eIAdd || a.getOpCode() == OpCode::eISub) && (isOnlyUse(m_builder, a.getDef(), op->getDef()))) {
    const auto& a0 = m_builder.getOpForOperand(a, 0u);
    const auto& a1 = m_builder.getOpForOperand(a, 1u);

    if (a1.isConstant()) {
      auto constDef = m_builder.addBefore(op->getDef(), a.getOpCode() == OpCode::eIAdd
        ? Op::ISub(b.getType(), b.getDef(), a1.getDef())
        : Op::IAdd(b.getType(), b.getDef(), a1.getDef()));

      m_builder.rewriteOp(op->getDef(), Op(op->getOpCode(), op->getType()).addOperands(a0.getDef(), constDef).setFlags(op->getFlags()));
      return std::make_pair(true, m_builder.iter(constDef));
    }

    if (a0.isConstant() && a.getOpCode() == OpCode::eISub) {
      auto constDef = m_builder.addBefore(op->getDef(), Op::ISub(b.getType(), a0.getDef(), b.getDef()));
      m_builder.rewriteOp(op->getDef(), Op(op->getOpCode(), op->getType()).addOperands(a1.getDef(), constDef).setFlags(op->getFlags()));
      return std::make_pair(true, m_builder.iter(constDef));
    }
  }

  if (op->getOpCode() == OpCode::eIEq || op->getOpCode() == OpCode::eINe) {
    /* ufindmsb(x) == -1 -> x == 0 */
    if ((a.getOpCode() == OpCode::eUFindMsb || a.getOpCode() == OpCode::eIFindLsb) && isConstantValue(b, -1)) {
      m_builder.rewriteOp(op->getDef(), Op(op->getOpCode(), op->getType())
        .setFlags(op->getFlags())
        .addOperand(m_builder.getOpForOperand(a, 0u).getDef())
        .addOperand(m_builder.makeConstantZero(a.getType())));
      return std::make_pair(true, op);
    }

    /* findmsb(x) == 32 -> false. Happens while optimizing findmsb patterns. */
    if (a.getOpCode() == OpCode::eUFindMsb || a.getOpCode() == OpCode::eSFindMsb || a.getOpCode() == OpCode::eIFindLsb) {
      dxbc_spv_assert(a.getType().isScalarType());
      int64_t bitCount = 8u * a.getType().getBaseType(0u).byteSize();

      if (getConstantAsSint(b, 0u) >= bitCount) {
        m_builder.rewriteDef(op->getDef(), m_builder.makeConstant(op->getOpCode() == OpCode::eINe));
        return std::make_pair(true, op);
      }
    }
  }

  /* op(-a, const) = op(-const, a) */
  if (a.getOpCode() == OpCode::eINeg && b.isConstant()) {
    /* For inequalities, this is only valid if the type is signed */
    if (a.getType().getBaseType(0u).isSignedIntType() || (op->getOpCode() == OpCode::eIEq || op->getOpCode() == OpCode::eINe)) {
      auto valueDef = m_builder.getOpForOperand(a, 0u).getDef();

      if (!isOnlyUse(m_builder, valueDef, a.getDef())) {
        auto constDef = m_builder.addBefore(op->getDef(), Op::INeg(b.getType(), b.getDef()));

        m_builder.rewriteOp(op->getDef(), Op(op->getOpCode(), op->getType())
          .setFlags(op->getFlags())
          .addOperand(constDef)
          .addOperand(valueDef));

        return std::make_pair(true, m_builder.iter(constDef));
      }
    }
  }

  /* For comparisons, we can only really do anything
   * if the operands are the same */
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

  /* boolean select(cond, a, b) -> (cond && a) || (!cond && b) */
  if (op->getType().getBaseType(0u).isBoolType()) {
    auto tDef = m_builder.addBefore(op->getDef(), Op::BAnd(op->getType(), cond.getDef(), a.getDef()));
    auto fDef = m_builder.addBefore(op->getDef(), Op::BAnd(op->getType(),
      m_builder.addBefore(op->getDef(), Op::BNot(op->getType(), cond.getDef())), b.getDef()));

    m_builder.rewriteOp(op->getDef(), Op::BOr(op->getType(), tDef, fDef));
    return std::make_pair(true, m_builder.iter(tDef));
  }

  if (a.getOpCode() == OpCode::eSelect && b.getOpCode() == OpCode::eSelect) {
    /* select(c0, select(c1, a, b), select(c2, a, b)) -> select(select(c0, c1, c2), a, b)
     * select(c0, select(c1, a, b), select(c2, b, a)) -> select(select(c0, c1, !c2), a, b) */
    auto at = SsaDef(a.getOperand(1u));
    auto bt = SsaDef(b.getOperand(1u));
    auto af = SsaDef(a.getOperand(2u));
    auto bf = SsaDef(b.getOperand(2u));

    if ((at == bt && af == bf) || (at == bf && af == bt)) {
      auto ac = SsaDef(a.getOperand(0u));
      auto bc = SsaDef(b.getOperand(0u));

      /* Invert b condition if operands are flipped */
      if (at != bt)
        bc = m_builder.addBefore(op->getDef(), Op::BNot(m_builder.getOp(bc).getType(), bc));

      auto condSel = m_builder.addBefore(op->getDef(),
        Op::Select(m_builder.getOp(ac).getType(), cond.getDef(), ac, bc));

      m_builder.rewriteOp(op->getDef(), Op::Select(op->getType(), condSel, at, af));
      return std::make_pair(true, m_builder.iter(condSel));
    }
  } else if (a.getOpCode() == OpCode::eSelect) {
    /* select(c0, select(c1, a, b), b) -> select(c0 && c1, a, b)
     * select(c0, select(c1, b, a), b) -> select(c0 && !c1, a, b) */
    auto ac = SsaDef(a.getOperand(0u));
    auto at = SsaDef(a.getOperand(1u));
    auto af = SsaDef(a.getOperand(2u));

    if (at == b.getDef() || af == b.getDef()) {
      bool invert = at == b.getDef();

      if (invert)
        ac = m_builder.addBefore(op->getDef(), Op::BNot(cond.getType(), ac));

      ac = m_builder.addBefore(op->getDef(), Op::BAnd(cond.getType(), cond.getDef(), ac));
      m_builder.rewriteOp(op->getDef(), Op::Select(op->getType(), ac, invert ? af : at, b.getDef()));
      return std::make_pair(true, m_builder.iter(ac));
    }

    /* select(c0, select(c0, a, _), b) -> select(c0, a, b) */
    if (ac == cond.getDef()) {
      m_builder.rewriteOp(op->getDef(), Op::Select(op->getType(), cond.getDef(), at, b.getDef()));
      return std::make_pair(true, op);
    }
  } else if (b.getOpCode() == OpCode::eSelect) {
    /* select(c0, a, select(c1, a, b)) -> select(c0 || c1, a, b)
     * select(c0, a, select(c1, b, a)) -> select(c0 || !c1, a, b) */
    auto bc = SsaDef(b.getOperand(0u));
    auto bt = SsaDef(b.getOperand(1u));
    auto bf = SsaDef(b.getOperand(2u));

    if (bt == a.getDef() || bf == a.getDef()) {
      bool invert = bf == a.getDef();

      if (invert)
        bc = m_builder.addBefore(op->getDef(), Op::BNot(cond.getType(), bc));

      bc = m_builder.addBefore(op->getDef(), Op::BOr(cond.getType(), cond.getDef(), bc));
      m_builder.rewriteOp(op->getDef(), Op::Select(op->getType(), bc, a.getDef(), invert ? bt : bf));
      return std::make_pair(true, m_builder.iter(bc));
    }

    /* select(c0, a, select(c0, _, b)) -> select(c0, a, b) */
    if (bc == cond.getDef()) {
      m_builder.rewriteOp(op->getDef(), Op::Select(op->getType(), cond.getDef(), a.getDef(), bf));
      return std::make_pair(true, op);
    }
  }

  /* select(a == b, a, b) -> b
   * select(a != b, a, b) -> a */
  if (cond.getOpCode() == OpCode::eIEq || cond.getOpCode() == OpCode::eINe) {
    auto c0 = m_builder.getOpForOperand(cond, 0u).getDef();
    auto c1 = m_builder.getOpForOperand(cond, 1u).getDef();

    if ((c0 == a.getDef() && c1 == b.getDef()) || (c0 == b.getDef() && c1 == a.getDef())) {
      auto next = m_builder.rewriteDef(op->getDef(), cond.getOpCode() == OpCode::eIEq ? b.getDef() : a.getDef());
      return std::make_pair(true, m_builder.iter(next));
    }
  }

  /* select(x != 0, ufindmsb(x), -1) -> ufindmsb(x) */
  if (isConstantValue(b, -1u)) {
    SsaDef msbOperand = { };

    if (a.getOpCode() == OpCode::eUFindMsb) {
      msbOperand = m_builder.getOpForOperand(a, 0u).getDef();
    } else if (a.getOpCode() == OpCode::eCast) {
      const auto& castOperand = m_builder.getOpForOperand(a, 0u);

      if (castOperand.getOpCode() == OpCode::eUFindMsb)
        msbOperand = m_builder.getOpForOperand(castOperand, 0u).getDef();
    }

    if (msbOperand && cond.getOpCode() == OpCode::eINe &&
        m_builder.getOpForOperand(cond, 0u).getDef() == msbOperand &&
        isConstantValue(m_builder.getOpForOperand(cond, 1u), 0)) {
      auto next = m_builder.rewriteDef(op->getDef(), a.getDef());
      return std::make_pair(true, m_builder.iter(next));
    }
  }

  return std::make_pair(false, ++op);
}


std::pair<bool, Builder::iterator> ArithmeticPass::resolveIdentityF16toF32(Builder::iterator op) {
  const auto& a = m_builder.getOpForOperand(*op, 0u);

  /* f16tof32(iand(x, 0xffff)) -> vec2(f16tof32(x).x, 0)
   * f16tof32(ubfe(x, 0, 16)) -> vec2(f16tof32(x).x, 0)
   * f16tof32(ushr(x, 16)) -> vec2(f16tof32(x).y, 0)
   * f16tof32(ubfe(x, 16, 16)) -> vec2(f16tof32(x).y, 0) */
  bool extractsLo = false;
  bool extractsHi = false;

  if (a.getOpCode() == OpCode::eIAnd)
    extractsLo = isConstantValue(m_builder.getOpForOperand(a, 1u), 0xffff);

  if (a.getOpCode() == OpCode::eUShr)
    extractsHi = isConstantValue(m_builder.getOpForOperand(a, 1u), 16);

  if (a.getOpCode() == OpCode::eUBitExtract) {
    if (isConstantValue(m_builder.getOpForOperand(a, 2u), 16)) {
      extractsLo = isConstantValue(m_builder.getOpForOperand(a, 1u), 0);
      extractsHi = isConstantValue(m_builder.getOpForOperand(a, 1u), 16);
    }
  }

  if (!extractsLo && !extractsHi)
    return std::make_pair(false, ++op);

  /* Use orginal operand for the conversion */
  auto conversion = m_builder.addBefore(op->getDef(),
    Op(op->getOpCode(), op->getType()).setFlags(op->getFlags())
      .addOperand(SsaDef(a.getOperand(0u))));

  /* Extract high or low component, depending on what we're using */
  auto scalarType = op->getType().getSubType(0u);

  auto component = m_builder.addBefore(op->getDef(),
    Op::CompositeExtract(scalarType, conversion, m_builder.makeConstant(extractsHi ? 1u : 0u)));

  /* Build vector with the high component being 0 */
  m_builder.rewriteOp(op->getDef(), Op::CompositeConstruct(op->getType(), component,
    m_builder.add(Op(OpCode::eConstant, scalarType).addOperand(Operand()))));

  return std::make_pair(true, op);
}


std::pair<bool, Builder::iterator> ArithmeticPass::resolveIdentityConvertFtoF(Builder::iterator op) {
  const auto& a = m_builder.getOpForOperand(*op, 0u);

  /* Eliminate round-trips or double-conversions involving a larger
   * type in between. Keep smaller conversions since those should
   * still quantize and affect the result */
  if (a.getOpCode() == OpCode::eConvertFtoF) {
    const auto& src = m_builder.getOpForOperand(a, 0u);

    if (a.getType().byteSize() > op->getType().byteSize()) {
      if (src.getType() == op->getType()) {
        auto next = m_builder.rewriteDef(op->getDef(), src.getDef());
        return std::make_pair(true, m_builder.iter(next));
      } else {
        m_builder.rewriteOp(op->getDef(), Op::ConvertFtoF(
          op->getType(), src.getDef()).setFlags(op->getFlags()));
        return std::make_pair(true, op);
      }
    }

    return std::make_pair(false, ++op);
  }

  /* Similarly, eliminate useless round-trips involving f16tof32 */
  if (a.getOpCode() == OpCode::eCompositeExtract) {
    const auto& src = m_builder.getOpForOperand(a, 0u);

    if (src.getOpCode() == OpCode::eConvertPackedF16toF32 &&
        src.getType() == BasicType(ScalarType::eF32, 2u) &&
        op->getType() == ScalarType::eF16) {
      auto conversion = m_builder.addBefore(op->getDef(),
        Op::ConvertPackedF16toF32(BasicType(ScalarType::eF16, 2u), SsaDef(src.getOperand(0u))));
      m_builder.rewriteOp(op->getDef(), Op::CompositeExtract(op->getType(), conversion, SsaDef(a.getOperand(1u))));
      return std::make_pair(true, m_builder.iter(conversion));
    }
  }

  return std::make_pair(false, ++op);
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
  return std::make_pair(true, op);
}


std::pair<bool, Builder::iterator> ArithmeticPass::resolveCompositeExtract(Builder::iterator op) {
  /* Only handle composite constructs that we may generate during passes. */
  const auto& composite = m_builder.getOpForOperand(*op, 0u);
  const auto& component = m_builder.getOpForOperand(*op, 1u);

  if (composite.getOpCode() != OpCode::eCompositeConstruct || !component.getType().isScalarType())
    return std::make_pair(false, ++op);

  auto index = getConstantAsUint(component, 0u);
  auto next = m_builder.rewriteDef(op->getDef(), SsaDef(composite.getOperand(index)));
  return std::make_pair(true, m_builder.iter(next));
}


std::pair<bool, Builder::iterator> ArithmeticPass::resolveIdentityOp(Builder::iterator op) {
  switch (op->getOpCode()) {
    case OpCode::eCast:
      return resolveCastOp(op);

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
    case OpCode::eIAnd:
    case OpCode::eIOr:
    case OpCode::eIXor:
    case OpCode::eIAbs:
    case OpCode::eINeg:
    case OpCode::eIAdd:
    case OpCode::eISub:
    case OpCode::eINot:
    case OpCode::eIShl:
    case OpCode::eSShr:
    case OpCode::eUShr:
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

    case OpCode::eConvertPackedF16toF32:
      return resolveIdentityF16toF32(op);

    case OpCode::eConvertFtoF:
      return resolveIdentityConvertFtoF(op);

    case OpCode::eCompositeExtract:
      return resolveCompositeExtract(op);

    default:
      return std::make_pair(false, ++op);
  }
}


std::pair<bool, Builder::iterator> ArithmeticPass::resolveBuiltInCompareOp(Builder::iterator op) {
  switch (op->getOpCode()) {
    case OpCode::eIEq:
    case OpCode::eINe:
    case OpCode::eULt:
    case OpCode::eULe:
    case OpCode::eUGt:
    case OpCode::eUGe: {
      const auto& a = m_builder.getOpForOperand(*op, 0u);
      const auto& b = m_builder.getOpForOperand(*op, 1u);

      auto aBuiltIn = getBuiltInInput(a);
      auto bBuiltIn = getBuiltInInput(b);

      if (!aBuiltIn)
        break;

      std::optional<bool> constant;

      if (aBuiltIn == BuiltIn::eTessControlPointId && bBuiltIn == BuiltIn::eTessControlPointCountIn) {
        constant = op->getOpCode() == OpCode::eINe ||
                   op->getOpCode() == OpCode::eULt ||
                   op->getOpCode() == OpCode::eULe;
      }

      if (aBuiltIn == BuiltIn::eTessControlPointCountIn && bBuiltIn == BuiltIn::eTessControlPointId) {
        constant = op->getOpCode() == OpCode::eINe ||
                   op->getOpCode() == OpCode::eUGt ||
                   op->getOpCode() == OpCode::eUGe;
      }

      if (aBuiltIn == BuiltIn::eTessControlPointCountIn && isConstantValue(b, 0)) {
        constant = op->getOpCode() == OpCode::eINe ||
                   op->getOpCode() == OpCode::eUGt ||
                   op->getOpCode() == OpCode::eUGe;
      }

      if (aBuiltIn == BuiltIn::eGsVertexCountIn && isConstantValue(b, 0)) {
        constant = op->getOpCode() == OpCode::eINe ||
                   op->getOpCode() == OpCode::eUGt ||
                   op->getOpCode() == OpCode::eUGe;
      }

      if (constant) {
        auto next = m_builder.rewriteDef(op->getDef(), m_builder.makeConstant(*constant));
        return std::make_pair(true, m_builder.iter(next));
      }
    } break;

    default:
      break;
  }

  return std::make_pair(false, ++op);
}


std::pair<bool, Builder::iterator> ArithmeticPass::vectorizeF32toF16(Builder::iterator op) {
  SsaDef lo = { };
  SsaDef hi = { };

  if (!op->getType().isScalarType())
    return std::make_pair(false, ++op);

  switch (op->getOpCode()) {
    case OpCode::eIAdd:
    case OpCode::eIOr: {
      /* a | (b << 16), a + (b * 65536), or any combination thereof */
      for (uint32_t i = 0u; i < op->getOperandCount(); i++) {
        const auto& arg = m_builder.getOpForOperand(*op, i);

        switch (arg.getOpCode()) {
          case OpCode::eIShl: {
            if (isConstantValue(m_builder.getOpForOperand(arg, 1u), 16))
              hi = SsaDef(arg.getOperand(0u));
          } break;

          case OpCode::eIMul: {
            if (isConstantValue(m_builder.getOpForOperand(arg, 1u), 65536))
              hi = SsaDef(arg.getOperand(0u));
          } break;

          default:
            lo = arg.getDef();
        }
      }
    } break;

    case OpCode::eIBitInsert: {
      /* bfi(a, b, 16, 16) */
      const auto& offset = m_builder.getOpForOperand(*op, 2u);
      const auto& count = m_builder.getOpForOperand(*op, 3u);

      if (!isConstantValue(offset, 16) || !isConstantValue(count, 16))
        return std::make_pair(false, ++op);

      lo = SsaDef(op->getOperand(0u));
      hi = SsaDef(op->getOperand(1u));
    } break;

    default:
      return std::make_pair(false, ++op);
  }

  if (!lo || !hi)
    return std::make_pair(false, ++op);

  /* Some games are a little bit speshul and apply masking
   * to the intermediate results before merging */
  SsaDef loMask = m_builder.makeConstant(-1u);
  SsaDef hiMask = m_builder.makeConstant(-1u);

  if (lo && m_builder.getOp(lo).getOpCode() == OpCode::eIAnd && m_builder.getOpForOperand(lo, 1u).isConstant()) {
    loMask = m_builder.getOpForOperand(lo, 1u).getDef();
    lo = m_builder.getOpForOperand(lo, 0u).getDef();
  }

  if (hi && m_builder.getOp(hi).getOpCode() == OpCode::eIAnd && m_builder.getOpForOperand(hi, 1u).isConstant()) {
    hiMask = m_builder.getOpForOperand(hi, 1u).getDef();
    hi = m_builder.getOpForOperand(hi, 0u).getDef();
  }

  /* Ensure that both operands are actually valid in this context */
  if (m_builder.getOp(lo).getOpCode() != OpCode::eConvertF32toPackedF16 ||
      m_builder.getOp(hi).getOpCode() != OpCode::eConvertF32toPackedF16)
    return std::make_pair(false, ++op);

  /* Statically prove that the low part is actually a 16-bit result */
  auto loArg = m_builder.getOpForOperand(lo, 0u);
  auto hiArg = m_builder.getOpForOperand(hi, 0u);

  if (loArg.getType() != hiArg.getType())
    return std::make_pair(false, ++op);

  if (loArg.getOpCode() != OpCode::eCompositeConstruct ||
      !isConstantValue(m_builder.getOpForOperand(loArg, 1u), 0u))
    return std::make_pair(false, ++op);

  /* Build input vector and perform conversion */
  auto loValue = m_builder.addBefore(op->getDef(), Op::CompositeExtract(loArg.getType().getSubType(0u), loArg.getDef(), m_builder.makeConstant(0u)));
  auto hiValue = m_builder.addBefore(op->getDef(), Op::CompositeExtract(hiArg.getType().getSubType(0u), hiArg.getDef(), m_builder.makeConstant(0u)));

  auto vector = m_builder.addBefore(op->getDef(), Op::CompositeConstruct(loArg.getType(), loValue, hiValue));
  auto result = m_builder.addBefore(op->getDef(), Op::ConvertF32toPackedF16(op->getType(), vector));

  /* Build and apply mask, constant-fold later if possible */
  auto mask = m_builder.addBefore(op->getDef(), Op::IBitInsert(op->getType(),
    loMask, hiMask, m_builder.makeConstant(16u), m_builder.makeConstant(16u)));

  m_builder.rewriteOp(op->getDef(), Op::IAnd(op->getType(), result, mask).setFlags(op->getFlags()));
  return std::make_pair(true, m_builder.iter(loValue));
}


std::pair<bool, Builder::iterator> ArithmeticPass::resolveIntSignCompareOp(Builder::iterator op) {
  /* cast(a) == cast(b) -> a == b if source types are the same */
  const auto& a = m_builder.getOpForOperand(*op, 0u);
  const auto& b = m_builder.getOpForOperand(*op, 1u);

  if (a.getOpCode() != OpCode::eCast)
    return std::make_pair(false, ++op);

  if (b.getOpCode() == OpCode::eCast) {
    /* Ensure that the operand types are compatible with the
     * respective cast type and both are the same type */
    const auto& aSrc = m_builder.getOpForOperand(a, 0u);
    const auto& bSrc = m_builder.getOpForOperand(b, 0u);

    if (aSrc.getType() != bSrc.getType() ||
        !checkIntTypeCompatibility(a.getType(), aSrc.getType()) ||
        !checkIntTypeCompatibility(b.getType(), bSrc.getType()))
      return std::make_pair(false, ++op);

    m_builder.rewriteOp(op->getDef(), Op(op->getOpCode(), op->getType())
      .addOperands(aSrc.getDef(), bSrc.getDef()));
    return std::make_pair(true, op);
  }

  if (b.isConstant()) {
    /* If the second operand is a constant, simply cast it */
    const auto& aSrc = m_builder.getOpForOperand(a, 0u);

    if (!checkIntTypeCompatibility(a.getType(), aSrc.getType()))
      return std::make_pair(false, ++op);

    auto bSrc = m_builder.add(castConstant(b, aSrc.getType().getBaseType(0u)));

    m_builder.rewriteOp(op->getDef(), Op(op->getOpCode(), op->getType())
      .addOperands(aSrc.getDef(), bSrc));
    return std::make_pair(true, op);
  }

  return std::make_pair(false, ++op);
}


std::pair<bool, Builder::iterator> ArithmeticPass::resolveIntSignBinaryOp(Builder::iterator op, bool considerConstants) {
  const auto& a = m_builder.getOpForOperand(*op, 0u);
  const auto& b = m_builder.getOpForOperand(*op, 1u);

  /* If both operands are casts from the same type, rewrite the op
   * to operate on the source type of both operands instead. */
  if (a.getOpCode() == OpCode::eCast && b.getOpCode() == OpCode::eCast) {
    const auto& aSrc = m_builder.getOpForOperand(a, 0u);
    const auto& bSrc = m_builder.getOpForOperand(b, 0u);

    if (aSrc.getType() == bSrc.getType() &&
        checkIntTypeCompatibility(a.getType(), aSrc.getType()) &&
        checkIntTypeCompatibility(b.getType(), bSrc.getType())) {
      auto newDef = m_builder.addBefore(op->getDef(),
        Op(op->getOpCode(), aSrc.getType()).addOperands(aSrc.getDef(), bSrc.getDef()));

      m_builder.rewriteOp(op->getDef(), Op::Cast(op->getType(), newDef));
      return std::make_pair(true, m_builder.iter(newDef));
    }
  }

  /* If at least one incoming operand is signed, including constants,
   * promote the entrire instruction to a signed type. */
  auto resultType = op->getType().getBaseType(0u);

  if (resultType.isSignedIntType())
    return std::make_pair(false, ++op);

  bool hasSignedInput = false;

  if (a.getOpCode() == OpCode::eCast) {
    const auto& aSrc = m_builder.getOpForOperand(a, 0u);

    if (checkIntTypeCompatibility(a.getType(), aSrc.getType()))
      hasSignedInput = hasSignedInput || aSrc.getType().getBaseType(0u).isSignedIntType();
  }

  if (b.getOpCode() == OpCode::eCast) {
    const auto& bSrc = m_builder.getOpForOperand(b, 0u);

    if (checkIntTypeCompatibility(b.getType(), bSrc.getType()))
      hasSignedInput = hasSignedInput || bSrc.getType().getBaseType(0u).isSignedIntType();
  }

  if (b.isConstant() && considerConstants) {
    for (uint32_t i = 0u; i < resultType.getVectorSize(); i++)
      hasSignedInput = hasSignedInput || getConstantAsSint(b, i) < 0;
  }

  if (!hasSignedInput)
    return std::make_pair(false, ++op);

  /* Determine signed type to use for the op */
  auto scalarType = [cScalarType = resultType.getBaseType()] {
    switch (cScalarType) {
      case ScalarType::eU8:  return ScalarType::eI8;
      case ScalarType::eU16: return ScalarType::eI16;
      case ScalarType::eU32: return ScalarType::eI32;
      case ScalarType::eU64: return ScalarType::eI64;
      default:               return ScalarType::eVoid;
    }
  } ();

  if (scalarType == ScalarType::eVoid)
    return std::make_pair(false, ++op);

  resultType = BasicType(scalarType, resultType.getVectorSize());

  /* Cast both operands to the desired signed type */
  auto aCast = m_builder.addBefore(op->getDef(), Op::Cast(resultType, a.getDef()));
  auto bCast = m_builder.addBefore(op->getDef(), Op::Cast(resultType, b.getDef()));

  auto newDef = m_builder.addBefore(op->getDef(),
    Op(op->getOpCode(), resultType).addOperands(aCast, bCast));

  m_builder.rewriteOp(op->getDef(), Op::Cast(op->getType(), newDef));
  return std::make_pair(true, m_builder.iter(aCast));
}


std::pair<bool, Builder::iterator> ArithmeticPass::resolveIntSignUnaryOp(Builder::iterator op) {
  /* op(cast(a)) -> cast(op(a)) */
  const auto& a = m_builder.getOpForOperand(*op, 0u);

  if (a.getOpCode() != OpCode::eCast)
    return std::make_pair(false, ++op);

  /* Ensure that the source type has the same bit width */
  const auto& aSrc = m_builder.getOpForOperand(a, 0u);

  if (!checkIntTypeCompatibility(a.getType(), aSrc.getType()))
    return std::make_pair(false, ++op);

  auto newDef = m_builder.addBefore(op->getDef(),
    Op(op->getOpCode(), aSrc.getType()).addOperand(aSrc.getDef()));

  m_builder.rewriteOp(op->getDef(), Op::Cast(op->getType(), newDef));
  return std::make_pair(true, m_builder.iter(newDef));
}


std::pair<bool, Builder::iterator> ArithmeticPass::resolveIntSignUnaryConsumeOp(Builder::iterator op) {
  /* op(cast(a)) -> op(a), return type does not change for these ops */
  const auto& a = m_builder.getOpForOperand(*op, 0u);

  if (a.getOpCode() != OpCode::eCast)
    return std::make_pair(false, ++op);

  /* Ensure that the source type has the same bit width. We do not change
   * the result type of the op since it does not depend on its operand. */
  const auto& aSrc = m_builder.getOpForOperand(a, 0u);

  if (!checkIntTypeCompatibility(a.getType(), aSrc.getType()))
    return std::make_pair(false, ++op);

  m_builder.rewriteOp(op->getDef(),
    Op(op->getOpCode(), op->getType()).addOperand(aSrc.getDef()));
  return std::make_pair(true, op);
}


std::pair<bool, Builder::iterator> ArithmeticPass::resolveIntSignShiftOp(Builder::iterator op) {
  const auto& a = m_builder.getOpForOperand(*op, 0u);
  const auto& b = m_builder.getOpForOperand(*op, 1u);

  /* Check shift amount independently, it does not affect the
   * result type so we can simply eliminate the cast. */
  if (b.getOpCode() == OpCode::eCast) {
    const auto& bSrc = m_builder.getOpForOperand(b, 0u);

    if (checkIntTypeCompatibility(b.getType(), bSrc.getType())) {
      m_builder.rewriteOp(op->getDef(),
        Op(op->getOpCode(), op->getType()).addOperands(a.getDef(), bSrc.getDef()));
      return std::make_pair(true, op);
    }
  }

  if (a.getOpCode() != OpCode::eCast)
    return std::make_pair(false, ++op);

  /* The first operand determines the result type. */
  const auto& aSrc = m_builder.getOpForOperand(a, 0u);

  if (!checkIntTypeCompatibility(a.getType(), aSrc.getType()))
    return std::make_pair(false, ++op);

  auto newDef = m_builder.addBefore(op->getDef(),
    Op(op->getOpCode(), aSrc.getType()).addOperands(aSrc.getDef(), b.getDef()));

  m_builder.rewriteOp(op->getDef(), Op::Cast(op->getType(), newDef));
  return std::make_pair(true, m_builder.iter(newDef));
}


std::pair<bool, Builder::iterator> ArithmeticPass::resolveIntSignOp(Builder::iterator op) {
  switch (op->getOpCode()) {
    case OpCode::eIEq:
    case OpCode::eINe:
      return resolveIntSignCompareOp(op);

    case OpCode::eIAdd:
    case OpCode::eISub:
    case OpCode::eIMul:
      return resolveIntSignBinaryOp(op, true);

    case OpCode::eIAnd:
    case OpCode::eIOr:
    case OpCode::eIXor:
      return resolveIntSignBinaryOp(op, false);

    case OpCode::eINot:
    case OpCode::eIBitReverse:
      return resolveIntSignUnaryOp(op);

    case OpCode::eIBitCount:
    case OpCode::eIFindLsb:
      return resolveIntSignUnaryConsumeOp(op);

    case OpCode::eIShl:
      return resolveIntSignShiftOp(op);

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
        case OpCode::eFNeg:
        case OpCode::eFAbs: {
          auto scalarType = op->getType().getBaseType(0u).getBaseType();
          auto signBit = uint64_t(1u) << (8u * byteSize(scalarType) - 1u);

          auto value = uint64_t(m_builder.getOpForOperand(*op, 0u).getOperand(i));

          if (op->getOpCode() == OpCode::eFNeg)
            value ^= signBit;
          else
            value &= ~signBit;

          return Operand(value);
        }

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
    case OpCode::eFAbs:
    case OpCode::eFNeg:
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


bool ArithmeticPass::isFloatSelect(const Op& op) const {
  if (op.getOpCode() != OpCode::eSelect)
    return false;

  const auto& a = m_builder.getOpForOperand(op, 1u);
  const auto& b = m_builder.getOpForOperand(op, 2u);

  if (!isConstantValue(a, 0) && !isConstantValue(b, 0))
    return false;

  return true;
}


bool ArithmeticPass::isConstantSelect(const Op& op) const {
  if (op.getOpCode() != OpCode::eSelect)
    return false;

  return m_builder.getOpForOperand(op, 1u).isConstant() &&
         m_builder.getOpForOperand(op, 2u).isConstant() &&
         SsaDef(op.getOperand(1u)) != SsaDef(op.getOperand(2u));
}


bool ArithmeticPass::isConstantValue(const Op& op, int64_t value) const {
  if (!op.isConstant())
    return false;

  for (uint32_t i = 0u; i < op.getOperandCount(); i++) {
    if (op.getOperand(i) != makeScalarOperand(op.getType(), value))
      return false;
  }

  return true;
}


std::optional<bool> ArithmeticPass::evalBAnd(const Op& a, const Op& b) const {
  if ((a.isConstant() && !bool(a.getOperand(0u))) ||
      (b.isConstant() && !bool(b.getOperand(0u))))
    return std::make_optional(false);

  /* av == c0 && av == c1 where c0 != c1 */
  if (a.getOpCode() == OpCode::eIEq && b.getOpCode() == OpCode::eIEq &&
      m_builder.getOpForOperand(a, 0u).getDef() == m_builder.getOpForOperand(b, 0u).getDef() &&
      m_builder.getOpForOperand(a, 1u).isConstant() && m_builder.getOpForOperand(b, 1u).isConstant() &&
      m_builder.getOpForOperand(a, 1u).getDef() != m_builder.getOpForOperand(b, 1u).getDef())
    return std::make_optional(false);

  return std::nullopt;
}


bool ArithmeticPass::shouldFlipOperands(const Op& op) const {
  dxbc_spv_assert(op.getOperandCount() >= 2u);

  const auto& a = m_builder.getOpForOperand(op, 0u);
  const auto& b = m_builder.getOpForOperand(op, 1u);

  /* Ensure that constant operands are always on the right */
  if (a.isConstant() != b.isConstant())
    return a.isConstant();

  /* Help CSE by further normalizing expressions within the shader.
   * Don't flip float operands since doing so breaks invariance. */
  if (a.getType().isBasicType() && !a.getType().getBaseType(0u).isFloatType())
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


std::optional<BuiltIn> ArithmeticPass::getBuiltInInput(const Op& op) const {
  if (op.getOpCode() != OpCode::eInputLoad)
    return std::nullopt;

  const auto& dcl = m_builder.getOpForOperand(op, 0u);

  if (dcl.getOpCode() != OpCode::eDclInputBuiltIn)
    return std::nullopt;

  return std::make_optional(BuiltIn(dcl.getOperand(1u)));
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


bool ArithmeticPass::checkIntTypeCompatibility(const Type& a, const Type& b) {
  if (!a.isBasicType() || !b.isBasicType())
    return false;

  BasicType aType = a.getBaseType(0u);
  BasicType bType = b.getBaseType(0u);

  if (aType.getVectorSize() != bType.getVectorSize())
    return false;

  static const std::array<std::pair<ScalarType, ScalarType>, 4u> s_types = {{
    { ScalarType::eI8,  ScalarType::eU8  },
    { ScalarType::eI16, ScalarType::eU16 },
    { ScalarType::eI32, ScalarType::eU32 },
    { ScalarType::eI64, ScalarType::eU64 },
  }};

  for (const auto& e : s_types) {
    if ((e.first == aType.getBaseType() && e.second == bType.getBaseType()) ||
        (e.first == bType.getBaseType() && e.second == aType.getBaseType()))
      return true;
  }

  return false;
}


bool ArithmeticPass::isBitPreservingOp(const Op& op) {
  return op.getOpCode() == OpCode::eConstant ||
         op.getOpCode() == OpCode::eUndef ||
         op.getOpCode() == OpCode::eCast ||
         op.getOpCode() == OpCode::eSelect ||
         op.getOpCode() == OpCode::ePhi;
}

}
