#pragma once

#include <iostream>
#include <vector>

#include "ir.h"
#include "ir_builder.h"

namespace dxbc_spv::ir {

/** Possible values for any given floating point value */
enum class FloatValue : uint16_t {
  /* Number may be nan */
  eNan          = (1u << 0),
  eNegInf       = (1u << 1),
  eNegGt1       = (1u << 2),
  eNegEq1       = (1u << 3),
  eNegLt1       = (1u << 4),
  eNeg0         = (1u << 5),
  ePos0         = (1u << 6),
  ePosLt1       = (1u << 7),
  ePosEq1       = (1u << 8),
  ePosGt1       = (1u << 9),
  ePosInf       = (1u << 10),

  /* Combination of flags */
  ePosFinite    = ePosLt1 | ePosEq1 | ePosGt1,
  eNegFinite    = eNegLt1 | eNegEq1 | eNegGt1,

  ePos          = ePos0 | ePosFinite | ePosInf,
  eNeg          = eNeg0 | eNegFinite | eNegInf,

  eAnyInf       = ePosInf | eNegInf,
  eAny0         = ePos0 | eNeg0,
  eAnyEq1       = ePosEq1 | eNegEq1,
  eAnyFinite    = ePosFinite | eNegFinite,

  /* Instruction can return any value */
  eAny          = eNan | eAnyInf | eAnyFinite | eAny0,

  eFlagEnum     = 0u
};

using FloatValueFlags = util::Flags<FloatValue>;


/** Signed zero behaviour flags for an instruction */
enum class FloatSzMode : uint16_t {
  /* If this flag is set, any signed zero inputs will be eliminated and can safely
   * be optimized. Serves as a barrier for preserve signed zero propagation. */
  eEliminate = (1u << 0),
  /* If set, the instruction requires signed zero results to be preserved. This
   * needs to be back-propagated until an instruction with the elimination flag
   * is encountered. Typically used for stores and other bit-preserving
   * instructions, but also if negative infinity can be produced. */
  ePreserve = (1u << 1),

  eFlagEnum = 0u
};

using FloatSzFlags = util::Flags<FloatSzMode>;


/** Pass to tag floating point operations with float control
 *  hints. Should be run *before* the final lowering passes
 *  to retain semantics of various instructions. */
class RangeAnalysis {
  static constexpr std::array<std::pair<FloatValue, FloatValue>, 5u> PosNegFlags = {{
    { FloatValue::ePosLt1,    FloatValue::eNegLt1    },
    { FloatValue::ePosEq1,    FloatValue::eNegEq1    },
    { FloatValue::ePosGt1,    FloatValue::eNegGt1    },
    { FloatValue::ePos0,      FloatValue::eNeg0      },
    { FloatValue::ePosInf,    FloatValue::eNegInf    },
  }};
public:

  struct Options {
    /* Whether to consider undef as a plain zero constant. */
    bool undefIsZero = false;
  };

  RangeAnalysis(Builder& builder, const Options& options);

  ~RangeAnalysis();

  RangeAnalysis             (const RangeAnalysis&) = delete;
  RangeAnalysis& operator = (const RangeAnalysis&) = delete;

  /** Queries possible values for a given float operation. */
  FloatValueFlags getFloatValue(SsaDef def) const {
    return m_flags.at(def).value;
  }

  /** Queries signed zero properties for an instruction */
  FloatSzFlags getFloatSignedZeroMode(SsaDef def) const {
    return m_flags.at(def).sz;
  }

private:

  struct Flags {
    FloatValueFlags value = { };
    FloatSzFlags    sz    = { };
  };

  Builder& m_builder;

  Options m_options;

  Container<Flags> m_flags;

  ShaderStage m_stage    = { };
  SsaDef      m_function = { };

  std::vector<SsaDef> m_preserveQueue;

  bool gatherFloatValues();

  bool gatherFloatValuesForOp(const Op& op);

  Flags determineFloatValuesForOp(const Op& op);

  FloatValueFlags getFlagsForOperand(const Op& op, uint32_t operand) const {
    return m_flags.at(m_builder.getOpForOperand(op, operand).getDef()).value;
  }

  Flags handleMinMax(size_t count, const FloatValue* flags, FloatValueFlags a, FloatValueFlags b);

  Flags handleMin(FloatValueFlags a, FloatValueFlags b);

  Flags handleMax(FloatValueFlags a, FloatValueFlags b);

  Flags handleAdd(FloatValueFlags a, FloatValueFlags b);

  Flags handleMul(FloatValueFlags a, FloatValueFlags b, bool same);

  Flags handleRcp(FloatValueFlags a);

  Flags handleSqrt(FloatValueFlags a);

  void preserveSignedZero(const Op& op);

  void propagateSignedZero();

  static bool maybeLessEqualOne(FloatValueFlags flags) {
    return bool(flags & (FloatValue::ePosLt1 | FloatValue::eNegLt1 | FloatValue::eAnyEq1 | FloatValue::eAny0));
  }

  static bool maybeLessThanOne(FloatValueFlags flags) {
    return bool(flags & (FloatValue::ePosLt1 | FloatValue::eNegLt1 | FloatValue::eAny0));
  }

  static bool maybeGreaterEqualOne(FloatValueFlags flags) {
    return bool(flags & (FloatValue::ePosGt1 | FloatValue::eNegGt1 | FloatValue::eAnyEq1 | FloatValue::eAnyInf));
  }

  static bool maybeGreaterThanOne(FloatValueFlags flags) {
    return bool(flags & (FloatValue::ePosGt1 | FloatValue::eNegGt1 | FloatValue::eAnyInf));
  }

  static bool maybePositive(FloatValueFlags flags) {
    return bool(flags & FloatValue::ePos);
  }

  static bool maybeNegative(FloatValueFlags flags) {
    return bool(flags & FloatValue::eNeg);
  }

  static bool maybeDifferentSign(FloatValueFlags a, FloatValueFlags b) {
    return maybePositive(a | b) && maybeNegative(a | b);
  }

  static FloatValueFlags flipSign(FloatValueFlags flags) {
    FloatValueFlags result = flags & FloatValue::eNan;

    for (auto e : PosNegFlags) {
      if (flags & e.first) result |= e.second;
      if (flags & e.second) result |= e.first;
    }

    return result;
  }

  static bool isFloatOp(const Op& op);

};

std::ostream& operator << (std::ostream& os, FloatValue value);
std::ostream& operator << (std::ostream& os, FloatSzMode mode);

}
