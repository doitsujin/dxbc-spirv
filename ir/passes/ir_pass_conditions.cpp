#include "../ir_utils.h"

#include "ir_pass_conditions.h"
#include "ir_pass_remove_unused.h"

namespace dxbc_spv::ir {

ConditionPass::ConditionPass(Builder& builder)
: m_builder(builder) {

}


ConditionPass::~ConditionPass() {

}


bool ConditionPass::run() {
  bool progress = false;

  auto iter = m_builder.getCode().first;

  while (iter != m_builder.getCode().second) {
    bool status = false;

    switch (iter->getOpCode()) {
      case OpCode::eSelect: {
        std::tie(status, iter) = handleSelect(iter);
      } break;

      case OpCode::eBAnd:
      case OpCode::eBOr: {
        std::tie(status, iter) = handleBooleanOp(iter);
      } break;

      default:
        ++iter;
    }

    progress |= status;
  }

  if (progress)
    RemoveUnusedPass::runPass(m_builder);

  return progress;
}


bool ConditionPass::runPass(Builder& builder) {
  return ConditionPass(builder).run();
}


bool ConditionPass::evaluateBooleanAnd(const Builder& builder, const Op& a, const Op& b) {
  if (a.isConstant() && b.isConstant())
    return bool(a.getOperand(0)) == bool(b.getOperand(0));

  /* av == c0 && av == c1 where c0 != c1 */
  if (a.getOpCode() == OpCode::eIEq && b.getOpCode() == OpCode::eIEq) {
    const auto& v0 = builder.getOpForOperand(a, 0);
    const auto& v1 = builder.getOpForOperand(b, 0);

    const auto& c0 = builder.getOpForOperand(a, 1);
    const auto& c1 = builder.getOpForOperand(b, 1);

    if (v0.getDef() == v1.getDef() && c0.isConstant() && c1.isConstant())
      return c0.getDef() == c1.getDef();
  }

  /* a == b && a != b */
  if ((a.getOpCode() == OpCode::eIEq && b.getOpCode() == OpCode::eINe)
   || (a.getOpCode() == OpCode::eINe && b.getOpCode() == OpCode::eIEq)
   || (a.getOpCode() == OpCode::eFEq && b.getOpCode() == OpCode::eFNe)
   || (a.getOpCode() == OpCode::eFNe && b.getOpCode() == OpCode::eFEq)) {
    const auto& a0 = builder.getOpForOperand(a, 0).getDef();
    const auto& a1 = builder.getOpForOperand(a, 1).getDef();

    const auto& b0 = builder.getOpForOperand(b, 0).getDef();
    const auto& b1 = builder.getOpForOperand(b, 1).getDef();

    if ((a0 == b0 && a1 == b1) || (a0 == b1 && a1 == b0))
      return false;
  }

  /* Unknown, assume both may be true */
  return true;
}


std::optional<bool> ConditionPass::evaluateBoolExpression(const Op& expr, size_t count, const Condition* conditions, size_t depth) {
  if (depth > MaxRecursionDepth)
    return std::nullopt;

  /* Check negation first because that will generally not occur in
   * the list on its own */
  if (expr.getOpCode() == OpCode::eBNot) {
    auto value = evaluateBoolExpression(m_builder.getOpForOperand(expr, 0), count, conditions, depth + 1u);

    if (value.has_value())
      return std::make_optional(!value.value());
  }

  /* Check whether we know anything about the condition itself */
  for (size_t i = 0u; i < count; i++) {
    if (conditions[i].def == expr.getDef())
      return std::make_optional(conditions[i].value);
  }

  /* Check whether we can infer that the condition cannot be true
   * at the same time as another */
  for (size_t i = 0u; i < count; i++) {
    if (conditions[i].value && contradicts(expr, m_builder.getOp(conditions[i].def)))
      return std::make_optional(false);
  }

  /* Recursively evaluate boolean and / or */
  if (expr.getOpCode() == OpCode::eBAnd || expr.getOpCode() == OpCode::eBOr) {
    auto check = expr.getOpCode() == OpCode::eBOr;

    auto a = evaluateBoolExpression(m_builder.getOpForOperand(expr, 0), count, conditions, depth + 1);
    auto b = evaluateBoolExpression(m_builder.getOpForOperand(expr, 1), count, conditions, depth + 1);

    if (a == std::make_optional(check) || b == std::make_optional(check))
      return std::make_optional(check);
  }

  /* Recursively evaluate boolean eq / ne */
  if (expr.getOpCode() == OpCode::eBEq || expr.getOpCode() == OpCode::eBNe) {
    auto check = expr.getOpCode() == OpCode::eBEq;

    auto a = evaluateBoolExpression(m_builder.getOpForOperand(expr, 0), count, conditions, depth + 1);
    auto b = evaluateBoolExpression(m_builder.getOpForOperand(expr, 1), count, conditions, depth + 1);

    if (a.has_value() && b.has_value())
      return std::make_optional((a == b) == check);
  }

  return std::nullopt;
}


SsaDef ConditionPass::evaluateBool(const Op& expr, size_t count, const Condition* conditions) {
  auto value = evaluateBoolExpression(expr, count, conditions, 0);

  if (value.has_value())
    return m_builder.makeConstant(value.value());

  return expr.getDef();
}


SsaDef ConditionPass::evaluateSelect(const Op& expr, size_t count, const Condition* conditions, size_t depth) {
  const auto& cond = m_builder.getOpForOperand(expr, 0);

  auto selector = evaluateBoolExpression(cond, count, conditions, 0);

  if (selector.has_value()) {
    auto value = m_builder.getOpForOperand(expr, selector.value() ? 1u : 2u);
    return evaluateOp(value, count, conditions, depth + 1u);
  }

  return expr.getDef();
}


SsaDef ConditionPass::evaluateBitOr(const Op& expr, size_t count, const Condition* conditions, size_t depth) {
  for (uint32_t i = 0u; i < expr.getFirstLiteralOperandIndex(); i++) {
    const auto& oldArg = m_builder.getOpForOperand(expr, i);
    const auto& newArg = m_builder.getOp(evaluateOp(oldArg, count, conditions, depth + 1u));

    if (newArg.isConstant() && !uint64_t(newArg.getOperand(0)))
      return m_builder.getOpForOperand(expr, i ^ 1u).getDef();
  }

  return expr.getDef();
}


SsaDef ConditionPass::evaluateOp(const Op& expr, size_t count, const Condition* conditions, size_t depth) {
  if (depth > MaxRecursionDepth)
    return expr.getDef();

  if (expr.getType() == ScalarType::eBool) {
    auto def = evaluateBool(expr, count, conditions);

    if (def != expr.getDef())
      return def;
  }

  switch (expr.getOpCode()) {
    case OpCode::eIOr:
      return evaluateBitOr(expr, count, conditions, depth);

    case OpCode::eSelect:
      return evaluateSelect(expr, count, conditions, depth);

    default:
      return expr.getDef();
  }
}


SsaDef ConditionPass::rewriteOp(const Op& base, const Op& expr, size_t count, const Condition* conditions, size_t depth) {
  auto def = evaluateOp(expr, count, conditions, depth);

  if (def != expr.getDef())
    return def;

  if (!isOnlyUse(m_builder, expr.getDef(), base.getDef()) || depth > MaxRecursionDepth)
    return expr.getDef();

  if (expr.isDeclarative() || expr.getOpCode() == OpCode::ePhi)
    return expr.getDef();

  /* Recursively rewrite operands */
  bool progress = false;

  Op newOp = expr;

  for (uint32_t i = 0u; i < expr.getFirstLiteralOperandIndex(); i++) {
    const auto& oldArg = m_builder.getOpForOperand(expr, i);
    const auto& newArg = m_builder.getOp(rewriteOp(expr, oldArg, count, conditions, depth));

    if (oldArg.getDef() != newArg.getDef()) {
      newOp.setOperand(i, newArg.getDef());
      progress = true;
    }
  }

  if (progress)
    return m_builder.addAfter(expr.getDef(), std::move(newOp));

  return expr.getDef();
}


std::pair<bool, Builder::iterator> ConditionPass::handleSelect(Builder::iterator op) {
  auto newOp = *op;

  bool progress = false;

  /* Rewrite operands with the the assumption that the selector
   * is true on the left side and false on the right side. */
  const auto& selector = m_builder.getOpForOperand(*op, 0);

  for (uint32_t i = 1u; i < 3u; i++) {
    auto cond = makeCondition(selector, i == 1u);

    const auto& oldArg = m_builder.getOpForOperand(*op, i);
    const auto& newArg = m_builder.getOp(rewriteOp(*op, oldArg, 1, &cond, 0));

    if (newArg.getDef() != oldArg.getDef()) {
      newOp.setOperand(i, newArg.getDef());
      progress = true;
    }
  }

  if (progress)
    m_builder.rewriteOp(op->getDef(), std::move(newOp));

  return std::make_pair(progress, ++op);
}


std::pair<bool, Builder::iterator> ConditionPass::handleBooleanOp(Builder::iterator op) {
  bool progress = false;

  Op newOp = *op;

  for (uint32_t i = 0u; i < 2u; i++) {
    const auto& a = m_builder.getOpForOperand(*op, i);
    const auto& b = m_builder.getOpForOperand(*op, i ^ 1);

    small_vector<Condition, 256> conditions;

    small_vector<SsaDef, 64> stack;
    stack.push_back(b.getDef());

    while (!stack.empty()) {
      const auto& next = m_builder.getOp(stack.back());
      stack.pop_back();

      /* For BAnd, if the condition is false, the entire expression evaluates to
       * false, so we can fold it to true in all other subexpressions. Similarly,
       * for BOr, if any give condition is true, the expression evaluates to true,
       * and any other subexpression will only affect the result if the condition
       * itself is false. */
      conditions.push_back(makeCondition(next, op->getOpCode() == OpCode::eBAnd));

      if (next.getOpCode() == op->getOpCode()) {
        stack.push_back(m_builder.getOpForOperand(next, 0).getDef());
        stack.push_back(m_builder.getOpForOperand(next, 1).getDef());
      }
    }

    auto aDef = rewriteOp(*op, a, conditions.size(), conditions.data(), 0);

    if (aDef != a.getDef()) {
      newOp.setOperand(i, aDef);
      progress = true;
    }
  }

  if (progress) {
    m_builder.rewriteOp(op->getDef(), std::move(newOp));
    return std::make_pair(true, ++op);
  }

  return resolveRangeCompare(op);
}




std::pair<bool, Builder::iterator> ConditionPass::resolveRangeCompare(Builder::iterator op) {
  if (op->getOpCode() != OpCode::eBOr)
    return std::make_pair(false, ++op);

  /* Resolves integer compare chains such as:
   * - a == 1 || a == 2 || ... || a == n => a >= 1 && a <= n
   * - a <= 3 || a == 4 => a <= 4
   * - a >= 6 || a == 5 => a >= 5
   * - (a >= 1 && a <= 2) || a == 3 => (a >= 1 && a <= 3) */
  small_vector<CompareRange, 256> compareRanges;

  small_vector<SsaDef, 64> stack;
  stack.push_back(op->getDef());

  bool hasValidRange = false;

  while (!stack.empty()) {
    const auto& op = m_builder.getOp(stack.back());
    stack.pop_back();

    if (op.getOpCode() == OpCode::eBOr) {
      for (uint32_t i = 0u; i < op.getFirstLiteralOperandIndex(); i++) {
        const auto& arg = m_builder.getOpForOperand(op, i);

        if (isOnlyUse(m_builder, arg.getDef(), op.getDef())) {
          stack.push_back(arg.getDef());
        } else {
          auto range = makeCompareRange(arg);
          compareRanges.push_back(range);

          hasValidRange |= range.isValid();
        }
      }
    } else {
      auto range = makeCompareRange(op);
      compareRanges.push_back(range);

      hasValidRange |= range.isValid();
    }
  }

  if (!hasValidRange || compareRanges.size() <= 1u)
    return std::make_pair(false, ++op);

  /* Sort ranges in such a way that we can trivially merge them */
  std::sort(compareRanges.begin(), compareRanges.end(),
    [] (const CompareRange& a, const CompareRange& b) {
      if (a.def != b.def)
        return a.def < b.def;

      return a.min < b.min;
    });

  /* Check whether we can compare any ranges. If not, we need to
   * exit immediately to not incorrectly report progress. */
  bool canMergeRanges = false;

  for (size_t i = 1u; i < compareRanges.size(); i++) {
    if (compareRanges[i].merge(compareRanges[i - 1])) {
      canMergeRanges = true;
      break;
    }
  }

  if (!canMergeRanges)
    return std::make_pair(false, ++op);

  /* Compact adjacent ranges and emit range checks. */
  SsaDef def = {};

  while (!compareRanges.empty()) {
    auto curr = compareRanges.back();
    compareRanges.pop_back();

    auto cond = curr.def;

    if (curr.isValid()) {
      while (!compareRanges.empty()) {
        auto next = compareRanges.back();
        auto merged = curr.merge(next);

        if (!merged.has_value())
          break;

        curr = merged.value();
        compareRanges.pop_back();
      }

      const auto& type = m_builder.getOp(curr.def).getType().getBaseType(0u);

      if (curr.min == curr.max) {
        cond = m_builder.addBefore(op->getDef(), Op::IEq(ScalarType::eBool,
          curr.def, makeTypedConstant(m_builder, type, curr.min)));
      } else {
        uint64_t maxVal = (uint64_t(2u) << (bitWidth(type.getBaseType()) - 1u)) - 1u;

        auto ge = (curr.min > 0)
          ? m_builder.addBefore(op->getDef(), Op::UGe(ScalarType::eBool, curr.def, makeTypedConstant(m_builder, type, curr.min)))
          : m_builder.makeConstant(true);

        auto le = (curr.max < maxVal)
          ? m_builder.addBefore(op->getDef(), Op::ULt(ScalarType::eBool, curr.def, makeTypedConstant(m_builder, type, curr.max + 1u)))
          : m_builder.makeConstant(true);

        cond = m_builder.addBefore(op->getDef(), Op::BAnd(ScalarType::eBool, ge, le));
      }
    }

    def = (!def) ? cond : m_builder.addBefore(op->getDef(),
      Op::BOr(ScalarType::eBool, def, cond));
  }

  auto next = m_builder.rewriteDef(op->getDef(), def);
  return std::make_pair(true, m_builder.iter(next));
}


ConditionPass::CompareRange ConditionPass::makeCompareRange(const Op& op) const {
  CompareRange result = {};
  result.def = op.getDef();

  switch (op.getOpCode()) {
    case OpCode::eIEq:
    case OpCode::eULt:
    case OpCode::eULe:
    case OpCode::eUGt:
    case OpCode::eUGe: {
      const auto& a = m_builder.getOpForOperand(op, 0u);
      const auto& b = m_builder.getOpForOperand(op, 1u);

      if (!b.isConstant() || !b.getType().getBaseType(0u).isUnsignedIntType())
        return result;

      auto value = uint64_t(b.getOperand(0u));
      result.def = a.getDef();

      if (op.getOpCode() == OpCode::eIEq) {
        result.min = value;
        result.max = value;
      } else if (op.getOpCode() == OpCode::eULt || op.getOpCode() == OpCode::eULe) {
        result.min = 0;
        result.max = value - (op.getOpCode() == OpCode::eULt ? 1u : 0u);
      } else if (op.getOpCode() == OpCode::eUGt || op.getOpCode() == OpCode::eUGe) {
        result.min = value + (op.getOpCode() == OpCode::eUGt ? 1u : 0u);
        result.max = -1;
      }

      return result;
    }

    case OpCode::eBAnd: {
      auto a = makeCompareRange(m_builder.getOpForOperand(op, 0u));
      auto b = makeCompareRange(m_builder.getOpForOperand(op, 1u));

      if (a.def != b.def)
        return result;

      auto min = std::max(a.min, b.min);
      auto max = std::min(a.max, b.max);

      if (min > max)
        return result;

      result.def = a.def;
      result.min = min;
      result.max = max;
      return result;
    }

    default:
      return result;
  }
}


ConditionPass::Condition ConditionPass::makeCondition(const Op& cond, bool value) const {
  if (cond.getOpCode() == OpCode::eBNot)
    return Condition { m_builder.getOpForOperand(cond, 0).getDef(), !value };
  else
    return Condition { cond.getDef(), value };
}


bool ConditionPass::contradicts(const Op& a, const Op& b) const {
  return !evaluateBooleanAnd(m_builder, a, b);
}


bool ConditionPass::CompareRange::isValid() const {
  return min <= max;
}


std::optional<ConditionPass::CompareRange> ConditionPass::CompareRange::merge(const CompareRange& other) const {
  /* Need to be careful to avoid overflows here */
  if ((def == other.def)
   && ((other.min > max && other.min == max + 1u)
    || (min > other.max && min == other.max + 1u))) {
    CompareRange result = {};
    result.def = def;
    result.min = std::min(min, other.min);
    result.max = std::max(max, other.max);
    return std::make_optional(result);
  } else {
    return std::nullopt;
  }
}

}
