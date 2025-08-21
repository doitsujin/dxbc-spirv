#include "ir_serialize.h"

#include "../util/util_bit.h"
#include "../util/util_vle.h"

namespace dxbc_spv::ir {

Serializer::Serializer(const Builder& builder)
: m_builder(builder), m_defMap(m_builder.getDefCount()) {
  /* Map SSA defs to instruction indices */
  for (const auto& op : m_builder)
    m_defMap.at(op.getDef().getId()) = m_opCount++;
}


size_t Serializer::computeSerializedSize() const {
  size_t size = util::vle::encodedSize(m_opCount);

  for (const auto& op : m_builder)
    size += computeEncodedOpSize(op);

  return size;
}


bool Serializer::serialize(uint8_t* data, size_t size) const {
  size_t offset = 0u;

  if (!writeVle(data, size, offset, m_opCount))
    return false;

  for (const auto& op : m_builder) {
    if (!encodeOp(op, data, size, offset))
      return false;
  }

  return true;
}


size_t Serializer::computeEncodedTypeSize(const Type& type) const {
  auto header = getTypeHeaderToken(type);

  /* Basic types don't need more than one byte each */
  size_t size = util::vle::encodedSize(header);
  size += type.getStructMemberCount();

  for (uint32_t i = 0u; i < type.getArrayDimensions(); i++)
    size += util::vle::encodedSize(type.getArraySize(i));

  return size;
}


size_t Serializer::computeEncodedOpTokenSize(const Op& op) const {
  return util::vle::encodedSize(getOpToken(op)) +
         util::vle::encodedSize(op.getOperandCount());
}


size_t Serializer::computeEncodedOpSize(const Op& op) const {
  size_t size = computeEncodedOpTokenSize(op) + computeEncodedTypeSize(op.getType());

  uint32_t refCount = op.getFirstLiteralOperandIndex();
  uint32_t argCount = op.getOperandCount();

  uint32_t opIndex = m_defMap.at(op.getDef().getId());

  for (uint32_t i = 0u; i < refCount; i++)
    size += util::vle::encodedSize(computeRelativeDef(opIndex, SsaDef(op.getOperand(i))));

  for (uint32_t i = refCount; i < argCount; i++)
    size += util::vle::encodedSize(uint64_t(op.getOperand(i)));

  return size;
}


int64_t Serializer::computeRelativeDef(uint32_t opIndex, SsaDef argDef) const {
  /* Self-references are never valid, so we can use 0 to encode null operands */
  if (!argDef)
    return 0;

  /* Encode the delta to the instruction ID in such a way that references
   * to prior instructions are encoded as positive integers. */
  return int64_t(opIndex) - int64_t(m_defMap.at(argDef.getId()));
}


bool Serializer::encodeType(const Type& type, uint8_t* dstData, size_t dstSize, size_t& dstOffset) const {
  auto header = getTypeHeaderToken(type);

  if (!writeVle(dstData, dstSize, dstOffset, header))
    return false;

  for (uint32_t i = 0u; i < type.getArrayDimensions(); i++) {
    if (!writeVle(dstData, dstSize, dstOffset, type.getArraySize(i)))
      return false;
  }

  if (dstOffset + type.getStructMemberCount() > dstSize)
    return false;

  for (uint32_t i = 0u; i < type.getStructMemberCount(); i++) {
    BasicType base = type.getBaseType(i);

    uint8_t byte = uint8_t(base.getBaseType());
    byte |= (base.getVectorSize() - 1u) << ScalarTypeBits;

    dstData[dstOffset++] = byte;
  }

  return true;
}


bool Serializer::encodeOp(const Op& op, uint8_t* dstData, size_t dstSize, size_t& dstOffset) const {
  uint32_t refCount = op.getFirstLiteralOperandIndex();
  uint32_t argCount = op.getOperandCount();

  if (!writeVle(dstData, dstSize, dstOffset, getOpToken(op)) ||
      !writeVle(dstData, dstSize, dstOffset, op.getOperandCount()))
    return false;

  if (!encodeType(op.getType(), dstData, dstSize, dstOffset))
    return false;

  uint32_t opIndex = m_defMap.at(op.getDef().getId());

  for (uint32_t i = 0u; i < refCount; i++) {
    if (!writeVle(dstData, dstSize, dstOffset, computeRelativeDef(opIndex, SsaDef(op.getOperand(i)))))
      return false;
  }

  for (uint32_t i = refCount; i < argCount; i++) {
    if (!writeVle(dstData, dstSize, dstOffset, uint64_t(op.getOperand(i))))
      return false;
  }

  return true;
}


uint64_t Serializer::getOpToken(const Op& op) const {
  uint64_t token = uint16_t(op.getOpCode());
  token |= uint64_t(uint8_t(op.getFlags())) << OpCodeBits;
  return token;
}


uint64_t Serializer::getTypeHeaderToken(const Type& type) const {
  return (uint64_t(type.getArrayDimensions())) |
         (uint64_t(type.getStructMemberCount()) << 2u);
}


template<typename T>
bool Serializer::writeBytes(uint8_t* dstData, size_t dstSize, size_t& dstOffset, T data) {
  if (dstOffset + sizeof(data) > dstSize)
    return false;

  for (size_t i = 0u; i < sizeof(data); i++) {
    dstData[dstOffset + i] = uint8_t(data);
    data >>= 8u;
  }

  dstOffset += sizeof(data);
  return true;
}


bool Serializer::writeVle(uint8_t* dstData, size_t dstSize, size_t& dstOffset, uint64_t sym) {
  size_t written = util::vle::encode(sym, &dstData[dstOffset], dstSize - dstOffset);

  if (!written)
    return false;

  dstOffset += written;
  return true;
}




Deserializer::Deserializer(const uint8_t* data, size_t size)
: m_data(data), m_size(size) {

}


bool Deserializer::deserializeOpCount(uint32_t& count) {
  uint64_t sym = 0u;

  if (!readVle(sym))
    return false;

  count = uint32_t(sym);
  return true;
}


bool Deserializer::deserializeOp(Op& op, SsaDef def) {
  uint64_t opToken = 0u;
  uint64_t operandCount = 0u;

  if (!readVle(opToken) || !readVle(operandCount))
    return false;

  Type type = { };

  if (!deserializeType(type))
    return false;

  OpCode opCode = OpCode(util::bextract(opToken, 0u, OpCodeBits));
  OpFlags opFlags = OpFlags(opToken >> OpCodeBits);

  /* Initialize op with default operands before parsing those. We need
   * to query the instruction layout, so the operand count must be known. */
  op = Op(opCode, opFlags, type, operandCount, nullptr);
  op.setSsaDef(def);

  /* Parse SSA operands */
  for (uint32_t i = 0u; i < op.getFirstLiteralOperandIndex(); i++) {
    uint64_t sym = 0u;

    if (!readVle(sym))
      return false;

    SsaDef operand = { };

    if (sym)
      operand = SsaDef(def.getId() - uint32_t(sym));

    op.setOperand(i, Operand(operand));
  }

  /* Parse literal operands */
  for (uint32_t i = op.getFirstLiteralOperandIndex(); i < op.getOperandCount(); i++) {
    uint64_t sym = 0u;

    if (!readVle(sym))
      return false;

    op.setOperand(i, Operand(sym));
  }

  return true;
}


bool Deserializer::deserialize(Builder& builder) {
  builder = Builder();

  uint32_t opCount = 0u;

  if (!deserializeOpCount(opCount))
    return false;

  /* Serialize into local array first and only add dummy ops. This is necessary
   * since adding instructions with forward references directly would fail. */
  std::vector<Op> ops;
  ops.reserve(opCount);

  uint32_t index = 0u;

  while (!atEnd()) {
    auto& op = ops.emplace_back();

    SsaDef def(++index);

    if (!deserializeOp(op, def))
      return false;

    auto result = op.isConstant()
      ? builder.add(op)
      : builder.add(Op(op.getOpCode(), op.getType()));

    dxbc_spv_assert(result == def);
    (void)result;
  }

  /* Add actual ops to builder */
  for (auto& op : ops) {
    auto def = op.getDef();

    if (!op.isConstant())
      builder.rewriteOp(def, std::move(op));
  }

  /* Ensure the op count is valid */
  return index == opCount;
}


bool Deserializer::deserializeType(Type& type) {
  uint64_t header = 0u;

  if (!readVle(header))
    return false;

  uint32_t arrayDimensions = header & 0x3u;
  uint32_t structSize = header >> 2u;

  if (structSize > Type::MaxStructMembers || arrayDimensions > Type::MaxArrayDimensions)
    return false;

  type = Type();

  /* Read array dimensions as variable-length integers */
  for (uint32_t i = 0u; i < arrayDimensions; i++) {
    uint64_t sym = 0u;

    if (!readVle(sym))
      return false;

    type.addArrayDimension(sym);
  }

  /* Read struct members as basic types */
  if (m_offset + structSize > m_size)
    return false;

  for (uint32_t i = 0u; i < structSize; i++) {
    uint8_t sym = m_data[m_offset++];

    type.addStructMember(BasicType(
      ScalarType(sym & ((1u << ScalarTypeBits) - 1u)),
      (sym >> ScalarTypeBits) + 1u));
  }

  return true;
}


template<typename T>
bool Deserializer::readBytes(T& data) {
  if (m_offset + sizeof(data) > m_size)
    return false;

  T sym = 0u;

  for (size_t i = 0u; i < sizeof(data); i++)
    sym |= T(m_data[m_offset + i]) << (8u * i);

  data = sym;

  m_offset += sizeof(data);
  return true;
}


bool Deserializer::readVle(uint64_t& sym) {
  size_t read = util::vle::decode(sym, &m_data[m_offset], m_size - m_offset);

  if (!read)
    return false;

  m_offset += read;
  return true;
}

}
