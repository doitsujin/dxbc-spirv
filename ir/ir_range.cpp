#include <cmath>

#include "ir_range.h"
#include "ir_utils.h"

namespace dxbc_spv::ir {

RangeAnalysis::RangeAnalysis(Builder& builder, const Options& options)
: m_builder(builder), m_options(options) {
  m_flags.ensure(builder.getMaxValidDef());

  while (gatherFloatValues())
    continue;

  propagateSignedZero();
}


RangeAnalysis::~RangeAnalysis() {

}


bool RangeAnalysis::gatherFloatValues() {
  bool progress = false;

  for (const auto& op : m_builder) {
    bool status = gatherFloatValuesForOp(op);
    progress |= status;

    switch (op.getOpCode()) {
      case OpCode::eEntryPoint: {
        m_stage = ShaderStage(op.getOperand(op.getFirstLiteralOperandIndex()));
      } break;

      case OpCode::eFunction: {
        m_function = op.getDef();
      } break;

      case OpCode::eFunctionEnd: {
        m_function = SsaDef();
      } break;

      case OpCode::eLdsStore:
      case OpCode::eScratchStore:
      case OpCode::eOutputStore: {
        if (status) {
          /* Feed value info back to the variable in question */
          const auto& decl = m_builder.getOpForOperand(op, 0u);
          const auto& value = m_builder.getOpForOperand(op, 2u);

          m_flags.at(decl.getDef()).value |= m_flags.at(value.getDef()).value;
        }
      } break;

      case OpCode::eReturn: {
        if (status) {
          m_flags.at(m_function).value |= m_flags.at(op.getDef()).value;
          m_flags.at(m_function).sz |= m_flags.at(op.getDef()).sz & FloatSzMode::ePreserve;
        }
      } break;

      default:
        break;
    }
  }

  return progress;
}


bool RangeAnalysis::gatherFloatValuesForOp(const Op& op) {
  Flags flags = { determineFloatValuesForOp(op) };

  if (op.getFlags() & OpFlag::eNoNan)
    flags.value -= FloatValue::eNan;

  if (op.getFlags() & OpFlag::eNoInf)
    flags.value -= FloatValue::eNegInf | FloatValue::ePosInf;

  /* Op doesn't care about signed zero, but may produce it anyway
   * so don't remove the flag from the possible float values. */
  if (op.getFlags() & OpFlag::eNoSz)
    flags.sz = FloatSzMode::eEliminate;

  /* Check whether we actually added any flags */
  auto& oldFlags = m_flags.at(op.getDef());
  flags.value |= oldFlags.value;
  flags.sz |= oldFlags.sz;

  if (flags.value == oldFlags.value && flags.sz == oldFlags.sz)
    return false;

  if (flags.sz & FloatSzMode::ePreserve)
    preserveSignedZero(op);

  oldFlags = flags;
  return true;
}


RangeAnalysis::Flags RangeAnalysis::determineFloatValuesForOp(const Op& op) {
  switch (op.getOpCode()) {
    /* Undef depends on further lowering */
    case OpCode::eUndef: {
      Flags result = { };
      result.value = m_options.undefIsZero
        ? FloatValue::ePos0
        : FloatValue::eAny;
      return result;
    }

    /* For constants, classify the actual constant operands */
    case OpCode::eConstant: {
      Flags result = { };

      for (uint32_t i = 0u; i < op.getOperandCount(); i++) {
        auto type = op.getType().resolveFlattenedType(i);

        if (BasicType(type).isFloatType()) {
          auto [kind, negative, value] = [type, value = op.getOperand(i)] {
            switch (type) {
              case ScalarType::eF16: {
                auto f = float16_t(value);
                return std::tuple(f.classify(), bool(f.data >> 15u), double(f));
              }

              case ScalarType::eF32: {
                uint32_t u = { };
                auto f = float(value);
                std::memcpy(&u, &f, sizeof(u));
                return std::tuple(std::fpclassify(f), bool(u >> 31u), double(f));
              }

              case ScalarType::eF64: {
                uint64_t u = { };
                auto f = double(value);
                std::memcpy(&u, &f, sizeof(u));
                return std::tuple(std::fpclassify(f), bool(u >> 63u), double(f));
              }

              default:
                break;
            }

            dxbc_spv_unreachable();
            return std::tuple(0, false, 0.0);
          } ();

          auto posFinite = value ==  1.0 ? FloatValue::ePosEq1 : (value >  1.0 ? FloatValue::ePosGt1 : FloatValue::ePosLt1);
          auto negFinite = value == -1.0 ? FloatValue::eNegEq1 : (value < -1.0 ? FloatValue::eNegGt1 : FloatValue::eNegLt1);

          switch (kind) {
            case FP_NORMAL: {
              result.value |= negative ? negFinite : posFinite;
            } break;

            case FP_SUBNORMAL: {
              result.value |= negative ? (FloatValue::eNeg0 | negFinite)
                                       : (FloatValue::ePos0 | posFinite);
            } break;

            case FP_ZERO: {
              result.value |= negative ? FloatValue::eNeg0 : FloatValue::ePos0;
            } break;

            case FP_INFINITE: {
              result.value |= negative ? FloatValue::eNegInf : FloatValue::ePosInf;
            } break;

            case FP_NAN: {
              result.value |= FloatValue::eNan;
            } break;

            default: {
              /* Shouldn't happen obviously */
              result.value |= FloatValue::eAny;
            }
          }
        }
      }

      return result;
    }

    /* For outputs, we gather information based on stores */
    case OpCode::eDclOutput:
    case OpCode::eDclOutputBuiltIn:
      return Flags();

    /* These are (or at least should be) zero-initialized, so start
     * with that. Add more flags as store information comes in. */
    case OpCode::eDclLds:
    case OpCode::eDclScratch: {
      Flags result = { };
      if (isFloatOp(op))
        result.value = FloatValue::ePos0;
      return result;
    }

    /* These can be anything, client must decorate manually. */
    case OpCode::eParamLoad:
    case OpCode::ePushDataLoad:
    case OpCode::eDclSpecConstant: {
      Flags result = { };
      if (isFloatOp(op))
        result.value = FloatValue::eAny;
      return result;
    }

    /* For inputs, we can special-case a small number of built-ins */
    case OpCode::eInputLoad:
    case OpCode::eInterpolateAtCentroid:
    case OpCode::eInterpolateAtSample:
    case OpCode::eInterpolateAtOffset: {
      Flags result = { };
      result.value = FloatValue::eAny;

      const auto& src = m_builder.getOpForOperand(op, 0u);
      const auto& address = m_builder.getOpForOperand(op, 1u);

      if (src.getOpCode() == OpCode::eDclInputBuiltIn) {
        auto builtIn = BuiltIn(src.getOperand(src.getFirstLiteralOperandIndex()));

        switch (builtIn) {
          /* In pixel shaders, the position vector components have different ranges */
          case BuiltIn::ePosition: {
            if (m_stage == ShaderStage::ePixel) {
              dxbc_spv_assert(!address || address.isConstant());
              result.value = { };

              /* Pessimizing since most apps will set a positive
               * viewport/scissor, but we don't know for sure */
              if (!address || uint32_t(address.getOperand(0u)) < 2u)
                result.value |= FloatValue::ePos0 | FloatValue::ePosFinite | FloatValue::eNegFinite;

              /* .z can be anything, we don't necessarily have depth clipping */
              if (!address || uint32_t(address.getOperand(0u)) == 2u)
                result.value |= FloatValue::eAny - FloatValue::eNan;

              /* .w can't be anything special due to primitive clipping */
              if (!address || uint32_t(address.getOperand(0u)) == 3u)
                result.value |= FloatValue::ePosFinite;
            }
          } break;

          /* For clip distances we can't make too many assumptions because
           * helper invocations exist, but it should never be nan since no
           * fragment invocations would be generated in that case. */
          case BuiltIn::eClipDistance:
          case BuiltIn::eCullDistance: {
            if (m_stage == ShaderStage::ePixel)
              result.value = FloatValue::eAny - FloatValue::eNan;
          } break;

          /* Patches get culled with weird tess factors, including zero */
          case BuiltIn::eTessFactorInner:
          case BuiltIn::eTessFactorOuter: {
            result.value = FloatValue::ePosEq1 | FloatValue::ePosGt1;
          } break;

          /* Tess coord is [0..1] */
          case BuiltIn::eTessCoord: {
            result.value = FloatValue::ePos0 | FloatValue::ePosLt1 | FloatValue::ePosEq1;
          } break;

          default:
            break;
        }
      }

      return result;
    }

    /* For stores, we want to preserve the bit pattern */
    case OpCode::eLdsStore:
    case OpCode::eScratchStore:
    case OpCode::eOutputStore:
    case OpCode::eBufferStore:
    case OpCode::eImageStore:
    case OpCode::eMemoryStore: {
      Flags result = { };
      result.sz = FloatSzMode::ePreserve;
      return result;
    }

    /* Use info that we get from the declaration */
    case OpCode::eScratchLoad:
    case OpCode::eLdsLoad:
    case OpCode::eOutputLoad:
    case OpCode::eConstantLoad: {
      Flags result = { };
      result.value = getFlagsForOperand(op, 0u);
      return result;
    }

    /* Arbitrary memory loads, can be anything */
    case OpCode::eBufferLoad:
    case OpCode::eMemoryLoad:
    case OpCode::eImageLoad: {
      Flags result = { };
      if (isFloatOp(op))
        result.value = FloatValue::eAny;
      return result;
    }

    case OpCode::eImageSample:
    case OpCode::eImageGather: {
      Flags result = { };
      if (isFloatOp(op))
        result.value = FloatValue::eAny;
      result.sz = FloatSzMode::eEliminate;
      return result;
    }

    /* This is basically an FSub, this can produce nans out of
     * infinities and zeroes out of any regular value. */
    case OpCode::eDerivX:
    case OpCode::eDerivY: {
      Flags result = { };
      result.value = getFlagsForOperand(op, 0u);

      if (result.value & FloatValue::eAnyInf)
        result.value |= FloatValue::eNan;

      if (result.value & (FloatValue::eAnyFinite | FloatValue::eAny0))
        result.value |= FloatValue::eAnyFinite | FloatValue::eAny0;

      result.sz = FloatSzMode::eEliminate;
      return result;
    }

    /* Pass through as-is */
    case OpCode::eDrain: {
      Flags result = { };
      result.value = getFlagsForOperand(op, 0u);
      return result;
    }

    /* Preserve signed zero on function exit */
    case OpCode::eReturn: {
      Flags result = { };

      if (op.getOperandCount()) {
        result.value = getFlagsForOperand(op, 0u);
        result.sz = FloatSzMode::ePreserve;
      }

      return result;
    }

    /* For casts, if the input is a float conversion to f16 then we can
     * propagate, otherwise the result can be literally anything. */
    case OpCode::eCast: {
      Flags result = { };

      /* Preserve signed zero if source expression is a float */
      const auto& src = m_builder.getOpForOperand(op, 0u);

      if (isFloatOp(op))
        result.value = FloatValue::eAny;

      if (isFloatOp(src))
        result.sz = FloatSzMode::ePreserve;

      /* RTZ semantics, can't produce new infinities */
      if (src.getOpCode() == OpCode::eConvertF32toPackedF16)
        result.value = getFlagsForOperand(src, 0u);

      return result;
    }

    /* We may get round-to-even semantics when converting to a smaller
     * type, so consider the possibility of infinities being generated. */
    case OpCode::eConvertFtoF: {
      Flags result = { };
      result.value = getFlagsForOperand(op, 0u);

      const auto& src = m_builder.getOpForOperand(op, 0u);

      if (op.getType().byteSize() < src.getType().byteSize()) {
        if (result.value & FloatValue::ePosGt1) result.value |= FloatValue::ePosInf;
        if (result.value & FloatValue::eNegGt1) result.value |= FloatValue::eNegInf;
      }

      return result;
    }

    /* When converting to F16 we may generate infinity, otherwise the
     * result will always be a finite value including positive zero. */
    case OpCode::eConvertItoF: {
      Flags result = { };
      result.value = FloatValue::ePos0 | FloatValue::ePosEq1 | FloatValue::ePosGt1;

      const auto& src = m_builder.getOpForOperand(op, 0u);

      if (src.getType().getBaseType(0u).isSignedIntType())
        result.value |= FloatValue::eNegEq1 | FloatValue::eNegGt1;

      if (op.getType().getBaseType(0u).getBaseType() == ScalarType::eF16) {
        result.value |= FloatValue::ePosInf;

        if (result.value & FloatValue::eNeg)
          result.value |= FloatValue::eNegInf;
      }

      return result;
    }

    /* Converting to integer discards signed zero information */
    case OpCode::eConvertFtoI: {
      Flags result = { };
      result.sz = FloatSzMode::eEliminate;
      return result;
    }

    /* This produces an integer, so keep signed zeroes alive */
    case OpCode::eConvertF32toPackedF16: {
      Flags result = { };
      result.sz = FloatSzMode::ePreserve;
      return result;
    }

    /* The input is an integer, but it may come from a floating
     * point source, in which case we can propagate. */
    case OpCode::eConvertPackedF16toF32: {
      Flags result = { };
      result.value = FloatValue::eAny;

      const auto& src = m_builder.getOpForOperand(op, 0u);

      if (src.getOpCode() == OpCode::eCast) {
        const auto& arg = m_builder.getOpForOperand(src, 0u);

        if (arg.getType() == BasicType(ScalarType::eF16, 2u))
          result.value = getFlagsForOperand(src, 0u);
      } else if (src.getOpCode() == OpCode::eConvertF32toPackedF16) {
        result.value = getFlagsForOperand(src, 0u);
      }

      return result;
    }

    /* Propagate vector flags as-is, with one exception: If the source is
     * a compute LOD instruction, the first (clamped) value is always a
     * non-negative finite value. If the source composite is a complex
     * type assume that it can be anything. */
    case OpCode::eCompositeExtract: {
      const auto& src = m_builder.getOpForOperand(op, 0u);

      Flags result = { };
      result.value = getFlagsForOperand(op, 0u);

      if (src.getOpCode() == OpCode::eImageComputeLod) {
        const auto& address = m_builder.getOpForOperand(op, 1u);
        dxbc_spv_assert(address.isConstant() && address.getType().isScalarType());

        if (!uint32_t(address.getOperand(0u)))
          result.value = FloatValue::ePosFinite | FloatValue::ePos0;
      }

      return result;
    }

    /* Composite flags are a combination of all inputs */
    case OpCode::eCompositeInsert:
    case OpCode::eCompositeConstruct: {
      Flags result = { };

      for (uint32_t i = 0u; i < op.getOperandCount(); i++)
        result.value |= getFlagsForOperand(op, i);

      return result;
    }


    /* The function is already tagged for all possible inputs,
     * so ignore the parameters entirely */
    case OpCode::eFunctionCall: {
      Flags result = { };
      result.value = getFlagsForOperand(op, 0u);
      result.sz = FloatSzMode::ePreserve;
      return result;
    }

    /* Combination of the two operands */
    case OpCode::eSelect: {
      Flags result = { };
      result.value = getFlagsForOperand(op, 1u) |
                     getFlagsForOperand(op, 2u);
      return result;
    }

    /* Combination of any value operand */
    case OpCode::ePhi: {
      Flags result = { };

      forEachPhiOperand(op, [&] (SsaDef, SsaDef value) {
        result.value |= m_flags.at(value).value;
      });

      return result;
    }

    /* We handle the clamped component separately */
    case OpCode::eImageComputeLod: {
      Flags result = { };
      result.value = FloatValue::eAny - FloatValue::eNan;
      result.sz = FloatSzMode::eEliminate;
      return result;
    }

    /* Any of the negative options become positive */
    case OpCode::eFAbs: {
      Flags result = { };
      result.value = getFlagsForOperand(op, 0u);

      for (const auto& e : PosNegFlags) {
        if (result.value & e.second) {
          result.value |= e.first;
          result.value -= e.second;
        }
      }

      return result;
    }

    case OpCode::eFEq:
    case OpCode::eFNe:
    case OpCode::eFLt:
    case OpCode::eFLe:
    case OpCode::eFGt:
    case OpCode::eFGe: {
      Flags result = { };
      result.value = getFlagsForOperand(op, 0u) |
                     getFlagsForOperand(op, 1u);
      result.sz = FloatSzMode::eEliminate;
      return result;
    }

    case OpCode::eFIsNan: {
      Flags result = { };
      result.value = getFlagsForOperand(op, 0u);
      result.sz = FloatSzMode::eEliminate;
      return result;
    }

    case OpCode::eFNeg: {
      Flags result = { };
      result.value = flipSign(getFlagsForOperand(op, 0u));
      return result;
    }

    case OpCode::eFAdd:
    case OpCode::eFSub: {
      auto a = getFlagsForOperand(op, 0u);
      auto b = getFlagsForOperand(op, 1u);

      if (op.getOpCode() == OpCode::eFSub)
        b = flipSign(b);

      return handleAdd(a, b);
    }

    case OpCode::eFMul:
    case OpCode::eFMulLegacy: {
      auto a = getFlagsForOperand(op, 0u);
      auto b = getFlagsForOperand(op, 1u);

      bool isSame = SsaDef(op.getOperand(0u)) == SsaDef(op.getOperand(1u));
      return handleMul(a, b, isSame);
    }

    case OpCode::eFMad:
    case OpCode::eFMadLegacy: {
      auto a = getFlagsForOperand(op, 0u);
      auto b = getFlagsForOperand(op, 1u);
      auto c = getFlagsForOperand(op, 2u);

      bool isSame = SsaDef(op.getOperand(0u)) == SsaDef(op.getOperand(1u));
      return handleAdd(handleMul(a, b, isSame).value, c);
    }

    case OpCode::eFDiv: {
      preserveSignedZero(m_builder.getOpForOperand(op, 1u));

      auto a = getFlagsForOperand(op, 0u);
      auto b = getFlagsForOperand(op, 1u);

      Flags result = { };
      result.value = handleMul(a, handleRcp(b).value, false).value;
      result.sz = FloatSzMode::eEliminate;
      return result;
    }

    case OpCode::eFRcp: {
      preserveSignedZero(m_builder.getOpForOperand(op, 0u));

      auto a = getFlagsForOperand(op, 0u);
      return handleRcp(a);
    }

    case OpCode::eFSqrt: {
      auto a = getFlagsForOperand(op, 0u);
      return handleSqrt(a);
    }

    case OpCode::eFRsq: {
      /* rsq(-0) is -inf, so the order matters */
      preserveSignedZero(m_builder.getOpForOperand(op, 0u));

      auto a = getFlagsForOperand(op, 0u);
      return handleRcp(handleSqrt(a).value);
    }

    case OpCode::eFExp2: {
      auto a = getFlagsForOperand(op, 0u);

      Flags result = { };
      result.value = a & FloatValue::eNan;
      result.sz = FloatSzMode::eEliminate;

      if (a & FloatValue::eNegInf)
        result.value |= FloatValue::ePos0;

      if (a & FloatValue::ePosInf)
        result.value |= FloatValue::ePosInf;

      if (a & FloatValue::eAny0)
        result.value |= FloatValue::ePosEq1;

      if (a & (FloatValue::eNegFinite | FloatValue::eNeg0 | FloatValue::ePos0))
        result.value |= FloatValue::ePosLt1 | FloatValue::ePosEq1;

      if (a & (FloatValue::ePosFinite))
        result.value |= FloatValue::ePosGt1 | FloatValue::ePosEq1;

      return result;
    }

    case OpCode::eFLog2: {
      auto a = getFlagsForOperand(op, 0u);

      Flags result = { };
      result.value = a & (FloatValue::eNan | FloatValue::ePosInf);
      result.sz = FloatSzMode::eEliminate;

      if (a & FloatValue::ePosEq1)
        result.value |= FloatValue::ePos0;

      if (a & FloatValue::ePosGt1)
        result.value |= FloatValue::ePos0 | FloatValue::ePosFinite;

      if (a & FloatValue::ePosLt1)
        result.value |= FloatValue::eNeg0 | FloatValue::eNegFinite;

      if (a & (FloatValue::eAny0))
        result.value |= FloatValue::eNegInf;

      if (a & (FloatValue::eNegFinite | FloatValue::eNegInf))
        result.value |= FloatValue::eNan;

      return result;
    }

    case OpCode::eFFract: {
      auto a = getFlagsForOperand(op, 0u);

      Flags result = { };
      result.value = a & FloatValue::eNan;
      result.sz = FloatSzMode::eEliminate;

      if (a & FloatValue::eAnyInf)
        result.value |= FloatValue::eNan;

      if (a & (FloatValue::eAny0 | FloatValue::eAnyFinite))
        result.value |= FloatValue::ePos0 | FloatValue::ePosLt1;

      return result;
    }

    case OpCode::eFRound: {
      Flags result = { };
      result.value = getFlagsForOperand(op, 0u);
      return result;
    }

    case OpCode::eFDot:
    case OpCode::eFDotLegacy: {
      auto a = getFlagsForOperand(op, 0u);
      auto b = getFlagsForOperand(op, 1u);

      bool isSame = SsaDef(op.getOperand(0u)) == SsaDef(op.getOperand(1u));
      auto result = handleMul(a, b, isSame).value;
      return handleAdd(result, result);
    }

    case OpCode::eFMin: {
      auto a = getFlagsForOperand(op, 0u);
      auto b = getFlagsForOperand(op, 1u);

      return handleMin(a, b);
    }

    case OpCode::eFMax: {
      auto a = getFlagsForOperand(op, 0u);
      auto b = getFlagsForOperand(op, 1u);

      return handleMax(a, b);
    }

    case OpCode::eFClamp: {
      auto in = getFlagsForOperand(op, 0u);
      auto lo = getFlagsForOperand(op, 1u);
      auto hi = getFlagsForOperand(op, 2u);

      return handleMin(handleMax(in, lo).value, hi);
    }

    /* Sin/Cos return nan for infinite inputs, and may otherwise
     * return any finite value including zero. */
    case OpCode::eFSin:
    case OpCode::eFCos: {
      auto a = getFlagsForOperand(op, 0u);

      Flags result = { };

      if (a & (FloatValue::eNan | FloatValue::eAnyInf))
        result.value |= FloatValue::eNan;

      /* Some hardware disagrees on the |sin x| <= 1 part, but we can't cater to that */
      if (a & (FloatValue::eAnyFinite | FloatValue::eAny0)) {
        result.value |= FloatValue::ePosLt1 | FloatValue::ePosEq1 | FloatValue::ePos0 |
                        FloatValue::eNegLt1 | FloatValue::eNegEq1 | FloatValue::eNeg0;
      }

      /* cos(-0) -> 1, but sin(-0) -> -0 */
      if (op.getOpCode() == OpCode::eFCos)
        result.sz |= FloatSzMode::eEliminate;

      return result;
    }

    /* Integer / bool ops */
    case OpCode::eConvertItoI:
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
    case OpCode::eBAnd:
    case OpCode::eBOr:
    case OpCode::eBEq:
    case OpCode::eBNe:
    case OpCode::eBNot:
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
    case OpCode::eIBitCount:
    case OpCode::eIBitReverse:
    case OpCode::eIFindLsb:
    case OpCode::eSFindMsb:
    case OpCode::eUFindMsb:
    case OpCode::eIAdd:
    case OpCode::eIAddCarry:
    case OpCode::eISub:
    case OpCode::eISubBorrow:
    case OpCode::eIAbs:
    case OpCode::eINeg:
    case OpCode::eIMul:
    case OpCode::eSMulExtended:
    case OpCode::eUMulExtended:
    case OpCode::eUDiv:
    case OpCode::eUMod:
    case OpCode::eSMin:
    case OpCode::eSMax:
    case OpCode::eSClamp:
    case OpCode::eUMin:
    case OpCode::eUMax:
    case OpCode::eUClamp:
    case OpCode::eUMSad:
      return Flags();

    /* Other opcodes to ignore */
    case OpCode::eEntryPoint:
    case OpCode::eSemantic:
    case OpCode::eDebugName:
    case OpCode::eDebugMemberName:
    case OpCode::eSetCsWorkgroupSize:
    case OpCode::eSetGsInstances:
    case OpCode::eSetGsInputPrimitive:
    case OpCode::eSetGsOutputVertices:
    case OpCode::eSetGsOutputPrimitive:
    case OpCode::eSetPsEarlyFragmentTest:
    case OpCode::eSetPsDepthGreaterEqual:
    case OpCode::eSetPsDepthLessEqual:
    case OpCode::eSetTessPrimitive:
    case OpCode::eSetTessDomain:
    case OpCode::eSetTessControlPoints:
    case OpCode::eSetFpMode:
    case OpCode::eDclParam:
    case OpCode::eDclPushData:
    case OpCode::eDclInput:
    case OpCode::eDclInputBuiltIn:
    case OpCode::eDclSampler:
    case OpCode::eDclCbv:
    case OpCode::eDclSrv:
    case OpCode::eDclUav:
    case OpCode::eDclUavCounter:
    case OpCode::eDclXfb:
    case OpCode::eFunction:
    case OpCode::eFunctionEnd:
    case OpCode::eLabel:
    case OpCode::eBranch:
    case OpCode::eBranchConditional:
    case OpCode::eSwitch:
    case OpCode::eUnreachable:
    case OpCode::eBarrier:
    case OpCode::eCheckSparseAccess:
    case OpCode::eDescriptorLoad:
    case OpCode::eBufferQuerySize:
    case OpCode::eLdsAtomic:
    case OpCode::eBufferAtomic:
    case OpCode::eImageAtomic:
    case OpCode::eCounterAtomic:
    case OpCode::eMemoryAtomic:
    case OpCode::eImageQuerySize:
    case OpCode::eImageQueryMips:
    case OpCode::eImageQuerySamples:
    case OpCode::ePointer:
    case OpCode::eEmitVertex:
    case OpCode::eEmitPrimitive:
    case OpCode::eDemote:
    case OpCode::eRovScopedLockBegin:
    case OpCode::eRovScopedLockEnd:
      return Flags();

    /* Illegal opcodes that we shouldn't see here */
    case OpCode::eUnknown:
    case OpCode::eLastDeclarative:
    case OpCode::eDclTmp:
    case OpCode::eTmpLoad:
    case OpCode::eTmpStore:
    case OpCode::eScopedIf:
    case OpCode::eScopedElse:
    case OpCode::eScopedEndIf:
    case OpCode::eScopedLoop:
    case OpCode::eScopedLoopBreak:
    case OpCode::eScopedLoopContinue:
    case OpCode::eScopedEndLoop:
    case OpCode::eScopedSwitch:
    case OpCode::eScopedSwitchCase:
    case OpCode::eScopedSwitchDefault:
    case OpCode::eScopedSwitchBreak:
    case OpCode::eScopedEndSwitch:
    case OpCode::eConsumeAs:
    case OpCode::Count:
      break;
  }

  dxbc_spv_unreachable();
  return Flags();
}


RangeAnalysis::Flags RangeAnalysis::handleMinMax(size_t count, const FloatValue* flags, FloatValueFlags a, FloatValueFlags b) {
  for (size_t i = 0u; i < count; i++) {
    if (!a || !b || ((a & b) & flags[i]))
      break;

    a -= flags[i];
    b -= flags[i];
  }

  Flags result = { };
  result.value = a | b;

  if (!(result.value & FloatValue::eNeg0))
    result.sz |= FloatSzMode::eEliminate;

  return result;
}


RangeAnalysis::Flags RangeAnalysis::handleMin(FloatValueFlags a, FloatValueFlags b) {
  /* Eliminate flags that don't occur in both operands, large to small */
  static const std::array<FloatValue, 11u> s_flags = {{
    FloatValue::eNan,
    FloatValue::ePosInf,
    FloatValue::ePosGt1,
    FloatValue::ePosEq1,
    FloatValue::ePosLt1,
    FloatValue::ePos0,
    FloatValue::eNeg0,
    FloatValue::eNegLt1,
    FloatValue::eNegEq1,
    FloatValue::eNegGt1,
    FloatValue::eNegInf,
  }};

  return handleMinMax(s_flags.size(), s_flags.data(), a, b);
}


RangeAnalysis::Flags RangeAnalysis::handleMax(FloatValueFlags a, FloatValueFlags b) {
  /* Same as above, but in reverse order */
  static const std::array<FloatValue, 11u> s_flags = {{
    FloatValue::eNan,
    FloatValue::eNegInf,
    FloatValue::eNegGt1,
    FloatValue::eNegEq1,
    FloatValue::eNegLt1,
    FloatValue::eNeg0,
    FloatValue::ePos0,
    FloatValue::ePosLt1,
    FloatValue::ePosEq1,
    FloatValue::ePosGt1,
    FloatValue::ePosInf,
  }};

  return handleMinMax(s_flags.size(), s_flags.data(), a, b);
}


RangeAnalysis::Flags RangeAnalysis::handleAdd(FloatValueFlags a, FloatValueFlags b) {
  /* Propagate nan */
  Flags result = { };
  result.value = (a | b) & FloatValue::eNan;

  if (a == FloatValue::eNan || b == FloatValue::eNan)
    return result;

  /* inf - inf -> nan */
  if (((a & FloatValue::ePosInf) && (b & FloatValue::eNegInf)) ||
      ((a & FloatValue::eNegInf) && (b & FloatValue::ePosInf)))
    result.value |= FloatValue::eNan;

  /* inf + / - x -> inf */
  result.value |= (a | b) & FloatValue::eAnyInf;

  /* If both inputs can be -0, the result may be -0 as well. Otherwise,
   * we can safely discard the sign of any incoming zero operands, even
   * if the instruction itself can still produce signed zeroes. */
  if (((a & b) & FloatValue::eNeg0))
    result.value |= FloatValue::eNeg0;
  else
    result.sz |= FloatSzMode::eEliminate;

  /* Try to prove that the operands either have the same sign or different signs.
   * Ignore signed zero in both cases since it will not affect the result, except
   * in cases where both operands are -0, which we handled earlier. */
  bool bothPositive = !((a | b) & (FloatValue::eNeg - FloatValue::eAny0));
  bool bothNegative = !((a | b) & (FloatValue::ePos - FloatValue::eAny0));

  bool differentSigns = (!(a & (FloatValue::eNeg - FloatValue::eAny0)) && !(b & (FloatValue::ePos - FloatValue::eAny0))) ||
                        (!(a & (FloatValue::ePos - FloatValue::eAny0)) && !(b & (FloatValue::eNeg - FloatValue::eAny0)));

  if (bothPositive || bothNegative) {
    /* Make sure everything is positive so we only have to worry about stuff once */
    if (bothNegative) {
      a = flipSign(a);
      b = flipSign(b);
    }

    /* Adding two values less than 1 can still produce a result greater than 1 */
    if ((a & b) & FloatValue::ePos)
      result.value |= FloatValue::ePosGt1;

    /* The result can only be less than 1 if both operands are */
    if (maybeLessThanOne(a & b))
      result.value |= FloatValue::ePosLt1 | FloatValue::ePosEq1;

    /* If one operand can be exactly 1 and the other can be zero
     * or less than 1, the result may also be exactly 1 */
    if (((a & FloatValue::ePosEq1) && (b & (FloatValue::ePosLt1 | FloatValue::eAny0))) ||
        ((b & FloatValue::ePosEq1) && (a & (FloatValue::ePosLt1 | FloatValue::eAny0))))
      result.value |= FloatValue::ePosEq1;

    /* Adding large values may produce infinity */
    if (maybeGreaterThanOne(a & b))
      result.value |= FloatValue::ePosInf;

    /* Flip sign again if inputs were negative */
    if (bothNegative)
      result.value = flipSign(result.value);

    /* If both operands can be zero, the result may be zero.
     * Signed zero input is handled later as necessary. */
    if ((a & b) & FloatValue::eAny0)
      result.value |= FloatValue::ePos0;
  } else if (differentSigns) {
    /* Put the negative operand on the right to avoid duplication */
    if (a & (FloatValue::eNeg - FloatValue::eAny0))
      std::swap(a, b);

    /* Normalize operand flags to positive-only to simplify things */
    auto aNorm = a - (FloatValue::eNan | FloatValue::eNeg);
    auto bNorm = flipSign(b) - (FloatValue::eNan | FloatValue::eNeg);

    /* If both operands are less than or equal to 1, so is the result */
    auto rNorm = FloatValue::ePos0 | FloatValue::ePosLt1 | FloatValue::ePosEq1;

    if (maybeGreaterThanOne(a | b))
      rNorm |= FloatValue::ePosGt1;

    /* If we can prove that one operand is greater than or equal
     * to the other, we can assume the sign of the result. */
    bool isAgtB = uint16_t(aNorm.first()) > uint16_t(bNorm);
    bool isBgtA = uint16_t(bNorm.first()) > uint16_t(aNorm);

    if (!isAgtB)  /* Positive value may be less than or equal to negative value */
      result.value |= flipSign(rNorm);

    if (!isBgtA)  /* Positive value may be greater than or equal to negative value */
      result.value |= rNorm;
  } else {
    /* Signs and values can be literally anything */
    result.value |= FloatValue::eAnyFinite | FloatValue::eAny0;

    /* Adding large values may produce infinity */
    if (maybeGreaterThanOne(a & b))
      result.value |= FloatValue::ePosInf;
  }

  return result;
}


RangeAnalysis::Flags RangeAnalysis::handleMul(FloatValueFlags a, FloatValueFlags b, bool same) {
  /* Propagate NaN */
  Flags result = { };
  result.value = (a | b) & FloatValue::eNan;

  /* Special case: If one operand is exactly 1, 0, or -1, it does not change
   * the range of the other operand except potentially adding zero. Put any
   * such operand on the right side to avoid duplication. */
  if (!(a - (FloatValue::eAny0 | FloatValue::eAnyEq1)))
    std::swap(a, b);

  if (!(b - (FloatValue::eAny0 | FloatValue::eAnyEq1))) {
    result.value = a & FloatValue::eNan;

    if (b & FloatValue::ePos0) {
      if (a & FloatValue::ePos) result.value |= FloatValue::ePos0;
      if (a & FloatValue::eNeg) result.value |= FloatValue::eNeg0;
    }

    if (b & FloatValue::eNeg0) {
      if (a & FloatValue::ePos) result.value |= FloatValue::eNeg0;
      if (a & FloatValue::eNeg) result.value |= FloatValue::ePos0;
    }

    if (b & FloatValue::ePosEq1)
      result.value |= a;

    if (b & FloatValue::eNegEq1)
      result.value |= flipSign(a);
  } else {
    /* 0 * inf -> nan */
    if (((a & FloatValue::eAnyInf) && (b & FloatValue::eAny0)) ||
        ((b & FloatValue::eAnyInf) && (a & FloatValue::eAny0)))
      result.value |= FloatValue::eNan;

    /* x * inf -> inf */
    if (((a & FloatValue::eAnyInf) && (b & (FloatValue::eAnyInf | FloatValue::eAnyFinite))) ||
        ((b & FloatValue::eAnyInf) && (a & (FloatValue::eAnyInf | FloatValue::eAnyFinite))))
      result.value |= FloatValue::eAnyInf;

    /* large * large -> potentially inf */
    if (maybeGreaterThanOne(a & b))
      result.value |= FloatValue::eAnyInf;

    /* small * small -> potentially denorm or 0 */
    if (maybeLessThanOne(a & b))
      result.value |= FloatValue::eAny0;

    /* x * 0 -> 0 */
    if ((a | b) & FloatValue::eAny0)
      result.value |= FloatValue::eAny0;

    if ((a & FloatValue::eAnyFinite) && (b & FloatValue::eAnyFinite)) {
      /* If noth both operands are strictly less than or
       * greater than 1, the result can be exactly 1 */
      if (maybeLessEqualOne(a | b) && maybeGreaterEqualOne(a | b))
        result.value |= FloatValue::ePosEq1 | FloatValue::eNegEq1;

      /* x * large -> potentially large */
      if (maybeGreaterThanOne(a | b))
        result.value |= FloatValue::ePosGt1 | FloatValue::eNegGt1;

      /* x * small -> potentially small */
      if (maybeLessThanOne(a | b))
        result.value |= FloatValue::ePosLt1 | FloatValue::eNegLt1;
    }
  }

  /* Result can't be negative if the operand signs are the same */
  if (same || !maybeDifferentSign(a, b))
    result.value -= FloatValue::eNeg;

  /* -0 * -0 -> +0 */
  if (same)
    result.sz = FloatSzMode::eEliminate;

  return result;
}


RangeAnalysis::Flags RangeAnalysis::handleRcp(FloatValueFlags a) {
  static const std::array<std::pair<FloatValue, FloatValue>, 5u> s_rcpFlags = {{
    { FloatValue::eNegGt1, FloatValue::eNegLt1  },
    { FloatValue::ePosEq1, FloatValue::ePosEq1  },
    { FloatValue::ePosGt1, FloatValue::ePosLt1  },
    { FloatValue::eNegInf, FloatValue::eNeg0 },
    { FloatValue::ePosInf, FloatValue::ePos0 },
  }};

  Flags result = { };
  result.value = a & FloatValue::eNan;
  result.sz = FloatSzMode::eEliminate;

  for (const auto& e : s_rcpFlags) {
    if (a & e.first) result.value |= e.second;
    if (a & e.second) result.value |= e.first;
  }

  return result;
}


RangeAnalysis::Flags RangeAnalysis::handleSqrt(FloatValueFlags a) {
  Flags result = { };
  result.value = a & (FloatValue::eNan | FloatValue::eAny0 | FloatValue::ePosInf);

  /* Might produce denorm results for small finite values */
  if (a & FloatValue::ePosLt1)
    result.value |= FloatValue::ePos0 | FloatValue::ePosLt1 | FloatValue::ePosEq1;

  if (a & FloatValue::ePosEq1)
    result.value |= FloatValue::ePosEq1;

  if (a & FloatValue::ePosGt1)
    result.value |= FloatValue::ePosEq1 | FloatValue::ePosGt1;

  /* Negative non-zero input is nan */
  if (a & (FloatValue::eNegFinite | FloatValue::eNegInf))
    result.value |= FloatValue::eNan;

  return result;
}


void RangeAnalysis::preserveSignedZero(const Op& op) {
  /* Can't really tag constants, variables etc */
  if (op.isDeclarative())
    return;

  /* Skip instructions that are already tagged or cannot produce signed zero */
  auto& argFlags = m_flags.at(op.getDef());

  if (argFlags.sz & FloatSzMode::ePreserve)
    return;

  if (isFloatOp(op) && !(argFlags.value & FloatValue::eNeg0))
    return;

  argFlags.sz |= FloatSzMode::ePreserve;
  m_preserveQueue.push_back(op.getDef());
}


void RangeAnalysis::propagateSignedZero() {
  while (!m_preserveQueue.empty()) {
    const auto& op = m_builder.getOp(m_preserveQueue.back());
    m_preserveQueue.pop_back();

    /* If the instruction eliminates signed zero inputs, don't
     * tag operands for preserving signed zero. */
    if (m_flags.at(op.getDef()).sz & FloatSzMode::eEliminate)
      continue;

    /* Recursively tag operands that actually return a value, including
     * any composite ops that we don't really handle otherwise. */
    for (uint32_t i = 0u; i < op.getFirstLiteralOperandIndex(); i++) {
      const auto& arg = m_builder.getOpForOperand(op, i);

      if (m_flags.at(arg.getDef()).value)
        preserveSignedZero(arg);
    }
  }
}


bool RangeAnalysis::isFloatOp(const Op& op) {
  if (op.getFlags() & OpFlag::eSparseFeedback)
    return op.getType().getBaseType(1u).isFloatType();

  return op.getType().isBasicType() &&
         op.getType().getBaseType(0u).isFloatType();
}


std::ostream& operator << (std::ostream& os, FloatValue value) {
  switch (value) {
    case FloatValue::eNan:        return os << "nan";
    case FloatValue::ePosInf:     return os << "+inf";
    case FloatValue::eNegInf:     return os << "-inf";
    case FloatValue::ePosLt1:     return os << "<+1";
    case FloatValue::eNegLt1:     return os << ">-1";
    case FloatValue::ePosEq1:     return os << "+1";
    case FloatValue::eNegEq1:     return os << "-1";
    case FloatValue::ePosGt1:     return os << ">+1";
    case FloatValue::eNegGt1:     return os << "<-1";
    case FloatValue::ePos0:       return os << "+0";
    case FloatValue::eNeg0:       return os << "-0";
    case FloatValue::ePosFinite:  return os << "+f";
    case FloatValue::eNegFinite:  return os << "-f";
    case FloatValue::ePos:        return os << "+";
    case FloatValue::eNeg:        return os << "-";
    case FloatValue::eAny0:       return os << "+/-0";
    case FloatValue::eAnyEq1:     return os << "+/-1";
    case FloatValue::eAnyFinite:  return os << "+/-f";
    case FloatValue::eAnyInf:     return os << "+/-inf";
    case FloatValue::eAny:        return os << "any";
    case FloatValue::eFlagEnum:   break;
  }

  return os << "FloatValue(" << uint32_t(value) << ")";
}


std::ostream& operator << (std::ostream& os, FloatSzMode mode) {
  switch (mode) {
    case FloatSzMode::eEliminate: return os << "eliminate";
    case FloatSzMode::ePreserve:  return os << "preserve";
    case FloatSzMode::eFlagEnum:  break;
  }

  return os << "FloatSzMode(" << uint32_t(mode) << ")";
}


}
