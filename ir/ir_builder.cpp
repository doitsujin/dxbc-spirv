#include "ir_builder.h"

#include "../util/util_hash.h"

namespace dxbc_spv::ir {

Builder::Builder() {

}


Builder::~Builder() {

}


SsaDef Builder::makeConstantZero(const Type& t) {
  Op constant(OpCode::eConstant, t);

  for (uint32_t i = 0u; i < t.computeFlattenedScalarCount(); i++)
    constant.addOperand(0u);

  return add(std::move(constant));
}


SsaDef Builder::add(Op op) {
  bool isDeclarative = op.isDeclarative();
  auto [def, isDuplicate] = writeOp(std::move(op));

  if (isDuplicate)
    return def;

  auto& metadata = m_ops.at(def);

  if (isDeclarative && m_codeBlockStart) {
    metadata.next = m_codeBlockStart;
    metadata.prev = m_ops.at(m_codeBlockStart).prev;
  } else if (m_cursor && m_cursor != m_code.tail) {
    metadata.next = m_ops.at(m_cursor).next;
    metadata.prev = m_cursor;

    m_cursor = def;
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

  auto& metadata = m_ops.at(def);
  metadata.next = ref;

  if (ref)
    metadata.prev = m_ops.at(ref).prev;
  else
    metadata.prev = m_code.tail;

  insertNode(def);
  return def;
}


SsaDef Builder::addAfter(SsaDef ref, Op op) {
  auto [def, isDuplicate] = writeOp(std::move(op));

  if (isDuplicate)
    return def;

  auto& metadata = m_ops.at(def);
  metadata.prev = ref;

  if (ref)
    metadata.next = m_ops.at(ref).next;
  else
    metadata.next = m_code.head;

  insertNode(def);
  return def;
}


SsaDef Builder::remove(SsaDef def) {
  auto op = getOp(def);

  dxbc_spv_assert(op);

  for (uint32_t i = 0u; i < op.getFirstLiteralOperandIndex(); i++)
    removeUse(SsaDef(op.getOperand(i)), def);

  if (op.isConstant() || op.isUndef())
    m_constants.erase(op);

  return removeNode(def);
}


SsaDef Builder::removeOp(const Op& op) {
  return remove(op.getDef());
}


void Builder::rewriteOp(SsaDef def, Op op) {
  auto& dstOp = m_ops.at(def).op;

  dxbc_spv_assert(op && !op.isConstant());
  dxbc_spv_assert(dstOp && !dstOp.isConstant());

  for (uint32_t i = 0u; i < dstOp.getFirstLiteralOperandIndex(); i++)
    removeUse(SsaDef(dstOp.getOperand(i)), def);

  for (uint32_t i = 0u; i < op.getFirstLiteralOperandIndex(); i++) {
    auto target = SsaDef(op.getOperand(i));
    dxbc_spv_assert(!target || getOp(target));

    addUse(target, def);
  }

  dstOp = op;
  dstOp.setSsaDef(def);
}


SsaDef Builder::rewriteDef(SsaDef oldDef, SsaDef newDef) {
  auto& oldMetadata = m_ops.at(oldDef);
  dxbc_spv_assert(oldDef != newDef);

  if (newDef) {
    dxbc_spv_assert(getOp(newDef));

    for (auto u : oldMetadata.uses) {
      auto& op = m_ops.at(u).op;

      for (uint32_t i = 0u; i < op.getFirstLiteralOperandIndex(); i++) {
        if (SsaDef(op.getOperand(i)) == SsaDef(oldDef))
          op.setOperand(i, Operand(SsaDef(newDef)));
      }

      addUse(newDef, u);
    }
  }

  oldMetadata.uses.clear();
  return remove(oldDef);
}


void Builder::reorderBefore(SsaDef ref, SsaDef first, SsaDef last) {
  dxbc_spv_assert(first && last);
  dxbc_spv_assert(getOp(first).isDeclarative() == getOp(last).isDeclarative());
  dxbc_spv_assert(!ref || getOp(ref).isDeclarative() == getOp(first).isDeclarative());

  unlinkNodes(first, last);

  auto& aMetadata = m_ops.at(first);
  auto& bMetadata = m_ops.at(last);

  aMetadata.prev = m_code.tail;
  bMetadata.next = ref;

  if (ref)
    aMetadata.prev = m_ops.at(ref).prev;

  insertNodes(first, last);

  if (m_codeBlockStart == ref)
    m_codeBlockStart = first;
}


void Builder::reorderAfter(SsaDef ref, SsaDef first, SsaDef last) {
  dxbc_spv_assert(first && last);
  dxbc_spv_assert(getOp(first).isDeclarative() == getOp(last).isDeclarative());
  dxbc_spv_assert(!ref || getOp(ref).isDeclarative() == getOp(first).isDeclarative());

  unlinkNodes(first, last);

  auto& aMetadata = m_ops.at(first);
  auto& bMetadata = m_ops.at(last);

  aMetadata.prev = ref;
  bMetadata.next = m_code.head;

  if (ref)
    bMetadata.next = m_ops.at(ref).next;

  insertNodes(first, last);

  if (m_codeBlockStart == ref)
    m_codeBlockStart = first;
}


void Builder::resetCursor() {
  m_cursor = SsaDef();
}


std::pair<SsaDef, bool> Builder::writeOp(Op&& op) {
  if (op.isConstant() || op.isUndef()) {
    SsaDef def = lookupConstant(op);

    if (def)
      return std::make_pair(def, true);
  }

  SsaDef def = allocSsaDef();

  auto& dstOp = m_ops.at(def).op;
  dstOp = std::move(op);
  dstOp.setSsaDef(def);

  for (uint32_t i = 0u; i < dstOp.getFirstLiteralOperandIndex(); i++) {
    auto target = SsaDef(dstOp.getOperand(i));
    dxbc_spv_assert(!target || getOp(target));

    addUse(target, def);
  }

  if (dstOp.isConstant() || dstOp.isUndef())
    m_constants.insert(dstOp);

  return std::make_pair(def, false);
}


void Builder::addUse(SsaDef target, SsaDef user) {
  if (!target)
    return;

  auto& metadata = m_ops.at(target);

  for (auto u : metadata.uses) {
    if (u == user)
      return;
  }

  metadata.uses.push_back(user);
}


void Builder::removeUse(SsaDef target, SsaDef user) {
  if (!target)
    return;

  auto& metadata = m_ops.at(target);

  for (auto i = metadata.uses.begin(); i != metadata.uses.end(); i++) {
    if (*i == user) {
      metadata.uses.erase(i);
      return;
    }
  }
}


void Builder::insertNode(SsaDef def) {
  insertNodes(def, def);
}


void Builder::insertNodes(SsaDef first, SsaDef last) {
  auto& aMetadata = m_ops.at(first);
  auto& bMetadata = m_ops.at(last);

  dxbc_spv_assert(((!aMetadata.prev && bMetadata.next == m_code.head) ||
    m_ops.at(aMetadata.prev).next == bMetadata.next));
  dxbc_spv_assert(((!bMetadata.next && aMetadata.prev == m_code.tail) ||
    m_ops.at(bMetadata.next).prev == aMetadata.prev));

  if (!getOp(first).isDeclarative()) {
    dxbc_spv_assert(!bMetadata.next || !getOp(bMetadata.next).isDeclarative());

    if (m_codeBlockStart == bMetadata.next)
      m_codeBlockStart = first;
  } else {
    dxbc_spv_assert(!aMetadata.prev || getOp(aMetadata.prev).isDeclarative());
  }

  if (aMetadata.prev)
    m_ops.at(aMetadata.prev).next = first;
  else
    m_code.head = first;

  if (bMetadata.next)
    m_ops.at(bMetadata.next).prev = last;
  else
    m_code.tail = last;
}


SsaDef Builder::removeNode(SsaDef def) {
  auto& metadata = m_ops.at(def);
  auto result = metadata.next;

  if (m_codeBlockStart == def)
    m_codeBlockStart = metadata.next;

  if (m_cursor == def) {
    m_cursor = metadata.prev;

    if (m_cursor && getOp(m_cursor).isDeclarative())
      m_cursor = SsaDef();
  }

  if (metadata.prev)
    m_ops.at(metadata.prev).next = metadata.next;
  else
    m_code.head = metadata.next;

  if (metadata.next)
    m_ops.at(metadata.next).prev = metadata.prev;
  else
    m_code.tail = metadata.prev;

  /* Reset op and metadata, and set up the metadata
   * entry as a free list for unique SSA defs. */
  m_ops.at(def).op = Op();

  metadata = OpMetadata();
  metadata.next = m_free;

  m_free = def;
  return result;
}


void Builder::unlinkNodes(SsaDef first, SsaDef last) {
  auto& aMetadata = m_ops.at(first);
  auto& bMetadata = m_ops.at(last);

  if (aMetadata.prev)
    m_ops.at(aMetadata.prev).next = bMetadata.next;
  else
    m_code.head = bMetadata.next;

  if (bMetadata.next)
    m_ops.at(bMetadata.next).prev = aMetadata.prev;
  else
    m_code.tail = aMetadata.prev;

  if (m_codeBlockStart == first)
    m_codeBlockStart = bMetadata.next;

  aMetadata.prev = SsaDef();
  bMetadata.next = SsaDef();
}


SsaDef Builder::allocSsaDef() {
  if (m_free) {
    dxbc_spv_assert(!getOp(m_free));

    SsaDef def = m_free;
    auto& metadata = m_ops.at(def);

    m_free = metadata.next;

    metadata.prev = SsaDef();
    metadata.next = SsaDef();
    return def;
  } else {
    return m_ops.allocSsaDef();
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
