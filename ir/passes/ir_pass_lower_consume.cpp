#include "ir_pass_lower_consume.h"

#include "../ir_utils.h"

namespace dxbc_spv::ir {

LowerConsumePass::LowerConsumePass(Builder& builder)
: m_builder(builder) {

}


LowerConsumePass::~LowerConsumePass() {

}


void LowerConsumePass::lowerConsume() {
  auto iter = m_builder.getCode().first;

  while (iter != m_builder.end()) {
    switch (iter->getOpCode()) {
      case OpCode::eConsumeAs: {
        iter = handleConsume(iter);
      } break;

      default:
        ++iter;
    }
  }

  /* This may result in redundant bitcasts being generated. */
  resolveCastChains();
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


std::pair<bool, SsaDef> LowerConsumePass::resolveCastChain(SsaDef def) {
  auto [progress, iter] = handleCastChain(m_builder.iter(def));
  return std::make_pair(progress, iter->getDef());
}


void LowerConsumePass::runLowerConsumePass(Builder& builder) {
  LowerConsumePass(builder).lowerConsume();
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

  /* Promote constant and undef right away */
  if (valueOp.isUndef()) {
    auto next = m_builder.rewriteDef(op->getDef(),
      m_builder.makeUndef(op->getType()));
    return std::make_pair(true, m_builder.iter(next));
  }

  if (valueOp.isConstant() && op->getType().isBasicType()) {
    auto dstType = op->getType().getBaseType(0u);

    auto constant = op->getOpCode() == OpCode::eConsumeAs
      ? consumeConstant(valueOp, dstType)
      : castConstant(valueOp, dstType);

    auto next = m_builder.rewriteDef(op->getDef(), m_builder.add(constant));
    return std::make_pair(true, m_builder.iter(next));
  }

  return std::make_pair(false, ++op);
}


Builder::iterator LowerConsumePass::handleConsume(Builder::iterator op) {
  dxbc_spv_assert(op->getType().isBasicType());

  auto [removed, next] = removeIfUnused(m_builder, op->getDef());

  if (removed)
    return m_builder.iter(next);

  const auto& valueOp = m_builder.getOp(SsaDef(op->getOperand(0u)));

  auto srcType = valueOp.getType().getBaseType(0u);
  auto dstType = op->getType().getBaseType(0u);

  /* Redundant consume */
  if (srcType == dstType)
    return m_builder.iter(m_builder.rewriteDef(op->getDef(), valueOp.getDef()));

  /* Promote constant and undef right away */
  if (valueOp.isUndef()) {
    return m_builder.iter(m_builder.rewriteDef(op->getDef(),
      m_builder.makeUndef(op->getType())));
  }

  if (valueOp.isConstant()) {
    return m_builder.iter(m_builder.rewriteDef(op->getDef(),
      m_builder.add(consumeConstant(valueOp, dstType))));
  }

  /* If both types have the same bit width, emit a plain case */
  if (srcType.byteSize() == dstType.byteSize()) {
    m_builder.rewriteOp(op->getDef(), Op::Cast(dstType, valueOp.getDef()));
    return ++op;
  }

  /* If both types are float, emit a plain float conversion. */
  if (srcType.isFloatType() && dstType.isFloatType()) {
    m_builder.rewriteOp(op->getDef(), Op::ConvertFtoF(dstType, valueOp.getDef()));
    return ++op;
  }

  /* If both types are integers, emit a plain integer conversion */
  if (srcType.isIntType() && dstType.isIntType()) {
    m_builder.rewriteOp(op->getDef(), Op::ConvertItoI(dstType, valueOp.getDef()));
    return ++op;
  }

  /* Otherwise, we need to normalize types to 32-bit first and then
   * perform appropriate conversions. */
  auto srcNormalized = normalizeTypeForConsume(srcType);
  auto dstNormalized = normalizeTypeForConsume(dstType);

  auto def = valueOp.getDef();

  if (srcType != srcNormalized) {
    auto opCode = srcType.isFloatType() ? OpCode::eConvertFtoF : OpCode::eConvertItoI;
    def = m_builder.addBefore(op->getDef(), Op(opCode, srcNormalized).addOperand(def));
  }

  def = m_builder.addBefore(op->getDef(), Op::Cast(dstNormalized, def));

  if (dstType != dstNormalized) {
    auto opCode = dstType.isFloatType() ? OpCode::eConvertFtoF : OpCode::eConvertItoI;
    def = m_builder.addBefore(op->getDef(), Op(opCode, dstType).addOperand(def));
  }

  return m_builder.iter(m_builder.rewriteDef(op->getDef(), def));
}

}
