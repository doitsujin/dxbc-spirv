#include <optional>

#include "ir_pass_eval.h"

namespace dxbc_spv::ir {

EvalCompileTimePass::EvalCompileTimePass(Builder& builder)
: m_builder(builder) {

}


EvalCompileTimePass::~EvalCompileTimePass() {

}


bool EvalCompileTimePass::run(bool positiveOnly) {
  bool feedback = false;
  auto iter = m_builder.getCode().first;

  while (iter != m_builder.getCode().second) {
    std::optional<bool> value;

    switch (iter->getOpCode()) {
      case OpCode::eEvalIsSame: {
        value = std::make_optional(evalIsSame(iter));
      } break;

      default:
        break;
    }

    if (value.has_value() && (value.value() || !positiveOnly)) {
      auto next = m_builder.rewriteDef(iter->getDef(), m_builder.makeConstant(value.value()));
      iter = m_builder.iter(next);

      feedback = true;
    } else {
      ++iter;
    }
  }

  return feedback;
}


bool EvalCompileTimePass::runPass(Builder& builder, bool positiveOnly) {
  return EvalCompileTimePass(builder).run(positiveOnly);
}


bool EvalCompileTimePass::evalIsSame(Builder::iterator op) {
  if (op->getOperandCount() < 2u)
    return true;

  /* Even if we can't fully evaluate yet, at least deduplicate the operands. */
  auto newOp = Op::EvalIsSame(ScalarType::eBool).addOperand(op->getOperand(0u));

  for (uint32_t i = 1u; i < op->getOperandCount(); i++) {
    if (op->getOperand(i) != op->getOperand(i - 1u)) {
      bool found = false;

      for (uint32_t j = 0u; j < newOp.getOperandCount() && !found; j++)
        found = op->getOperand(i) == newOp.getOperand(j);

      if (!found)
        newOp.addOperand(op->getOperand(i));
    }
  }

  if (newOp.getOperandCount() < 2u)
    return true;

  m_builder.rewriteOp(op->getDef(), std::move(newOp));
  return false;
}

}
