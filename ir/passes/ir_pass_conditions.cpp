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

  if (progress)
    m_builder.rewriteOp(op->getDef(), std::move(newOp));

  return std::make_pair(progress, ++op);
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

}
