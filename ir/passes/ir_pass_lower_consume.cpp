#include "ir_pass_lower_consume.h"

namespace dxbc_spv::ir {

LowerConsumePass::LowerConsumePass(Builder& builder)
: m_builder(builder) {

}


LowerConsumePass::~LowerConsumePass() {

}


bool LowerConsumePass::resolveCastChains() {
  auto iter = m_builder.getCode().first;
  bool feedback = false;

  while (iter != m_builder.end()) {
    if (iter->getOpCode() == OpCode::eConsumeAs ||
        iter->getOpCode() == OpCode::eCast) {
      auto [progress, next] = handleCastChain(iter);
      feedback |= progress;
      iter = next;
    } else {
      ++iter;
    }
  }

  return feedback;
}


bool LowerConsumePass::runResolveCastChainsPass(Builder& builder) {
  return LowerConsumePass(builder).resolveCastChains();
}


std::pair<bool, Builder::iterator> LowerConsumePass::handleCastChain(Builder::iterator op) {
  const auto& valueOp = m_builder.getOp(SsaDef(op->getOperand(0u)));

  /* If the consumed value is already in the correct type, use
   * it directly and remove the cast instruction */
  if (valueOp.getType() == op->getType()) {
    auto next = m_builder.rewriteDef(op->getDef(), valueOp.getDef());
    return std::make_pair(true, m_builder.iter(next));
  }

  /* If the argument is another cast, use its operand instead, remove
   * the source instruction if it goes unused. */
  if (valueOp.getOpCode() == op->getOpCode()) {
    auto fusedOp = Op(op->getOpCode(), op->getType()).addOperand(valueOp.getOperand(0u));
    m_builder.rewriteOp(op->getDef(), std::move(fusedOp));

    if (!m_builder.getUseCount(valueOp.getDef()))
      m_builder.remove(valueOp.getDef());

    /* Same iterator, we might merge more */
    return std::make_pair(true, op);
  }

  return std::make_pair(false, ++op);
}

}
