#include "ir_builder.h"

#include "../util/util_hash.h"

namespace dxbc_spv::ir {

Builder::Builder() {
  /* Init dummy op for null def. */
  m_ops.emplace_back();
  m_metadata.emplace_back();
}


Builder::~Builder() {

}


SsaDef Builder::add(Op op) {
  bool isDeclarative = op.isDeclarative();
  auto [def, isDuplicate] = writeOp(std::move(op));

  if (isDuplicate)
    return def;

  auto& metadata = m_metadata.at(def.getId());

  if (isDeclarative && m_codeBlockStart) {
    metadata.next = m_codeBlockStart;
    metadata.prev = m_metadata.at(m_codeBlockStart.getId()).prev;
  } else {
    metadata.next = SsaDef();
    metadata.prev = m_code.tail;
  }

  insertNode(def);
  return def;
}


SsaDef Builder::addBefore(SsaDef ref, Op op) {
  auto [def, isDuplicate] = writeOp(std::move(op));

  if (isDuplicate)
    return def;

  auto& metadata = m_metadata.at(def.getId());
  metadata.next = ref;

  if (ref)
    metadata.prev = m_metadata.at(ref.getId()).prev;
  else
    metadata.prev = m_code.tail;

  insertNode(def);
  return def;
}


SsaDef Builder::addAfter(SsaDef ref, Op op) {
  auto [def, isDuplicate] = writeOp(std::move(op));

  if (isDuplicate)
    return def;

  auto& metadata = m_metadata.at(def.getId());
  metadata.prev = ref;

  if (ref)
    metadata.next = m_metadata.at(ref.getId()).next;
  else
    metadata.next = m_code.head;

  insertNode(def);
  return def;
}


void Builder::remove(SsaDef def) {
  auto op = getOp(def);

  dxbc_spv_assert(op);

  for (uint32_t i = 0u; i < op.getFirstLiteralOperandIndex(); i++)
    removeUse(SsaDef(op.getOperand(i)), def);

  if (op.isConstant())
    m_constants.erase(op);

  removeNode(def);
}


void Builder::removeOp(const Op& op) {
  remove(op.getDef());
}


void Builder::rewriteOp(SsaDef def, Op op) {
  auto& dstOp = m_ops.at(def.getId());

  dxbc_spv_assert(op && !op.isConstant());
  dxbc_spv_assert(dstOp && !dstOp.isConstant());

  for (uint32_t i = 0u; i < dstOp.getFirstLiteralOperandIndex(); i++)
    removeUse(SsaDef(dstOp.getOperand(i)), def);

  for (uint32_t i = 0u; i < op.getFirstLiteralOperandIndex(); i++)
    addUse(SsaDef(op.getOperand(i)), def);

  dstOp = op;
  dstOp.setSsaDef(def);
}


void Builder::rewriteDef(SsaDef oldDef, SsaDef newDef) {
  auto& oldMetadata = m_metadata.at(oldDef.getId());

  for (auto u : oldMetadata.uses) {
    auto& op = m_ops.at(u.getId());

    for (uint32_t i = 0u; i < op.getFirstLiteralOperandIndex(); i++) {
      if (SsaDef(op.getOperand(i)) == SsaDef(oldDef))
        op.setOperand(i, Operand(SsaDef(newDef)));
    }

    addUse(newDef, u);
  }

  oldMetadata.uses.clear();

  remove(oldDef);
}


std::pair<SsaDef, bool> Builder::writeOp(Op&& op) {
  if (op.isConstant()) {
    SsaDef def = lookupConstant(op);

    if (def)
      return std::make_pair(def, true);
  }

  SsaDef def = allocSsaDef();

  auto& dstOp = m_ops.at(def.getId());
  dstOp = std::move(op);
  dstOp.setSsaDef(def);

  for (uint32_t i = 0u; i < dstOp.getFirstLiteralOperandIndex(); i++)
    addUse(SsaDef(dstOp.getOperand(i)), def);

  if (dstOp.isConstant())
    m_constants.insert(dstOp);

  return std::make_pair(def, false);
}


void Builder::addUse(SsaDef target, SsaDef user) {
  if (!target)
    return;

  auto& metadata = m_metadata.at(target.getId());

  for (auto u : metadata.uses) {
    if (u == user)
      return;
  }

  metadata.uses.push_back(user);
}


void Builder::removeUse(SsaDef target, SsaDef user) {
  if (!target)
    return;

  auto& metadata = m_metadata.at(target.getId());

  for (auto i = metadata.uses.begin(); i != metadata.uses.end(); i++) {
    if (*i == user) {
      metadata.uses.erase(i);
      return;
    }
  }

  dxbc_spv_unreachable();
}


void Builder::insertNode(SsaDef def) {
  auto& metadata = m_metadata.at(def.getId());

  dxbc_spv_assert(((!metadata.prev && metadata.next == m_code.head) ||
    m_metadata.at(metadata.prev.getId()).next == metadata.next));
  dxbc_spv_assert(((!metadata.next && metadata.prev == m_code.tail) ||
    m_metadata.at(metadata.next.getId()).prev == metadata.prev));

  if (!getOp(def).isDeclarative()) {
    dxbc_spv_assert(!metadata.next || !getOp(metadata.next).isDeclarative());

    if (m_codeBlockStart == metadata.next)
      m_codeBlockStart = def;
  } else {
    dxbc_spv_assert(!metadata.prev || getOp(metadata.prev).isDeclarative());
  }

  if (metadata.prev)
    m_metadata.at(metadata.prev.getId()).next = def;
  else
    m_code.head = def;

  if (metadata.next)
    m_metadata.at(metadata.next.getId()).prev = def;
  else
    m_code.tail = def;
}


void Builder::removeNode(SsaDef def) {
  auto& metadata = m_metadata.at(def.getId());

  if (m_codeBlockStart == def)
    m_codeBlockStart = metadata.next;

  if (metadata.prev)
    m_metadata.at(metadata.prev.getId()).next = metadata.next;
  else
    m_code.head = metadata.next;

  if (metadata.next)
    m_metadata.at(metadata.next.getId()).prev = metadata.prev;
  else
    m_code.tail = metadata.prev;

  /* Reset op and metadata, and set up the metadata
   * entry as a free list for unique SSA defs. */
  m_ops.at(def.getId()) = Op();

  metadata = OpMetadata();
  metadata.next = m_free;

  m_free = def;
}


SsaDef Builder::allocSsaDef() {
  if (m_free) {
    dxbc_spv_assert(!getOp(m_free));

    SsaDef def = m_free;
    auto& metadata = m_metadata.at(def.getId());

    m_free = metadata.next;

    metadata.prev = SsaDef();
    metadata.next = SsaDef();
    return def;
  } else {
    auto id = getDefCount();

    m_metadata.emplace_back();
    m_ops.emplace_back();

    return SsaDef(id);
  }
}


SsaDef Builder::lookupConstant(const Op& op) const {
  auto e = m_constants.find(op);

  if (e != m_constants.end())
    return e->getDef();

  return SsaDef();
}


size_t Builder::ConstantHash::operator () (const Op& op) const {
  size_t v = uint16_t(op.getOpCode());
  v = util::hash_combine(v, uint8_t(op.getFlags()));
  v = util::hash_combine(v, std::hash<Type>()(op.getType()));

  for (uint32_t i = 0u; i < op.getOperandCount(); i++) {
    auto lit = uint64_t(op.getOperand(i));

    v = util::hash_combine(v, uint32_t(lit >> 32u));
    v = util::hash_combine(v, uint32_t(lit));
  }

  return v;
}


bool Builder::ConstantEq::operator () (const Op& a, const Op& b) const {
  return a.isEquivalent(b);
}


}
