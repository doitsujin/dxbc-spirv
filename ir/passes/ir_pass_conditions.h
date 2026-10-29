#pragma once

#include <optional>

#include "../ir.h"
#include "../ir_builder.h"

namespace dxbc_spv::ir {

/* Pass to optimize boolean conditions in select ops as well as boolean ops. */
class ConditionPass {
  static constexpr size_t MaxRecursionDepth = 64;
public:

  ConditionPass(Builder& builder);

  ~ConditionPass();

  ConditionPass             (const ConditionPass&) = delete;
  ConditionPass& operator = (const ConditionPass&) = delete;

  bool run();

  static bool runPass(Builder& builder);

  /** Helper to check whether two conditions can be true at the same time.
   *  This is not exhaustive at all, only meant to check simple patterns
   *  such as a == 1 && a == 2.
   *  Returns false only if two conditions cannot be true at the same time. */
  static bool evaluateBooleanAnd(const Builder& builder, const Op& a, const Op& b);

private:

  struct Condition {
    SsaDef  def   = {};
    bool    value = false;
  };

  struct CompareRange {
    SsaDef    def = {};
    bool      isOnlyUse = false;
    uint64_t  min = -1;
    uint64_t  max =  0;

    bool isValid() const;

    std::optional<CompareRange> merge(const CompareRange& other) const;
  };

  Builder& m_builder;

  std::optional<bool> evaluateBoolExpression(const Op& expr, size_t count, const Condition* conditions, size_t depth);

  SsaDef evaluateBool(const Op& expr, size_t count, const Condition* conditions);

  SsaDef evaluateSelect(const Op& expr, size_t count, const Condition* conditions, size_t depth);

  SsaDef evaluateBitOr(const Op& expr, size_t count, const Condition* conditions, size_t depth);

  SsaDef evaluateOp(const Op& expr, size_t count, const Condition* conditions, size_t depth);

  SsaDef rewriteOp(const Op& base, const Op& expr, size_t count, const Condition* conditions, size_t depth);

  std::pair<bool, Builder::iterator> handleSelect(Builder::iterator op);

  std::pair<bool, Builder::iterator> handleBooleanOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveRangeCompare(Builder::iterator op);

  CompareRange makeCompareRange(const Op& op) const;

  Condition makeCondition(const Op& cond, bool value) const;

  bool contradicts(const Op& a, const Op& b) const;

};

}
