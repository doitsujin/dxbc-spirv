#include "ir.h"

namespace dxbc_spv::ir {

Type& Type::addStructMember(BasicType type) {
  dxbc_spv_assert(!type.isVoidType());

  if (isVoidType())
    m_structSize = 0u;

  m_members.at(m_structSize++) = type;
  return *this;
}


Type& Type::addArrayDimension(uint32_t size) {
  m_sizes.at(m_dimensions++) = size;
  return *this;
}


Type Type::getSubType(uint32_t index) const {
  if (m_dimensions) {
    Type result = *this;
    result.m_dimensions -= 1u;
    return result;
  }

  if (m_structSize > 1u)
    return Type(getBaseType(index));

  BasicType base = getBaseType(0u);

  if (base.isVector())
    return Type(base.getBaseType());

  return Type();
}


ScalarType Type::resolveFlattenedType(uint32_t index) const {
  uint32_t componentCount = 0u;

  for (uint32_t i = 0u; i < m_structSize; i++)
    componentCount += getBaseType(i).getVectorSize();

  index %= componentCount;

  uint32_t start = 0u;

  for (uint32_t i = 0u; i < m_structSize; i++) {
    auto type = getBaseType(i);

    if (start + type.getVectorSize() > index)
      return type.getBaseType();

    start += type.getVectorSize();
  }

  return ScalarType::eVoid;
}


uint32_t Type::byteSize() const {
  uint32_t alignment = 0u;
  uint32_t size = 0u;

  for (uint32_t i = 0u; i < m_structSize; i++) {
    uint32_t memberAlignment = getBaseType(i).byteAlignment();

    size = util::align(size, memberAlignment);
    size += getBaseType(i).byteSize();

    alignment = std::max(alignment, memberAlignment);
  }

  size = util::align(size, alignment);

  for (uint32_t i = 0u; i < m_dimensions; i++) {
    if (getArraySize(i) || i + 1u < m_dimensions)
      size *= getArraySize(i);
  }

  return size;
}


uint32_t Type::byteAlignment() const {
  uint32_t alignment = 0u;

  for (uint32_t i = 0u; i < m_structSize; i++)
    alignment = std::max(alignment, getBaseType(i).byteAlignment());

  return alignment;
}


uint32_t Type::byteOffset(uint32_t member) const {
  dxbc_spv_assert(member < m_structSize);

  uint32_t offset = 0u;

  for (uint32_t i = 0u; i < member; i++) {
    uint32_t memberAlignment = getBaseType(i).byteAlignment();

    offset = util::align(offset, memberAlignment);
    offset += getBaseType(i).byteSize();
  }

  uint32_t memberAlignment = getBaseType(member).byteAlignment();
  return util::align(offset, memberAlignment);
}


bool Type::operator == (const Type& other) const {
  bool eq = m_dimensions == other.m_dimensions
         && m_structSize == other.m_structSize;

  for (uint32_t i = 0; i < m_dimensions && eq; i++)
    eq = m_sizes[i] == other.m_sizes[i];

  for (uint32_t i = 0; i < m_structSize && eq; i++)
    eq = m_members[i] == other.m_members[i];

  return eq;
}


bool Type::operator != (const Type& other) const {
  return !(this->operator == (other));
}


bool Operand::getToString(std::string& str) const {
  uint64_t lit = m_data;

  for (uint32_t i = 0u; i < sizeof(lit); i++) {
    char ch = char((lit >> (8u * i)) & 0xffu);

    if (!ch)
      return false;

    str += ch;
  }

  return true;
}


std::string Op::getLiteralString(uint32_t index) const {
  std::string str;

  for (uint32_t i = index; i < getOperandCount(); i++)
    getOperand(i).getToString(str);

  return str;
}


bool Op::isEquivalent(const Op& other) const {
  bool eq = m_opCode == other.m_opCode && m_flags == other.m_flags &&
    m_resultType == other.m_resultType &&
    m_operands.size() == other.m_operands.size();

  for (size_t i = 0u; i < m_operands.size() && eq; i++)
    eq = m_operands[i] == other.m_operands[i];

  return eq;
}


uint32_t Op::getFirstLiteralOperandIndex() const {
  switch (m_opCode) {
    case OpCode::eConstant:
    case OpCode::eDclInput:
    case OpCode::eDclInputBuiltIn:
    case OpCode::eDclOutput:
    case OpCode::eDclOutputBuiltIn:
    case OpCode::eDclSpecConstant:
    case OpCode::eDclPushData:
    case OpCode::eDclSampler:
    case OpCode::eDclCbv:
    case OpCode::eDclSrv:
    case OpCode::eDclUav:
      return 0u;

    case OpCode::eDebugName:
    case OpCode::eEntryPoint:
    case OpCode::eSetCsWorkgroupSize:
    case OpCode::eSetGsInstances:
    case OpCode::eSetGsInputPrimitive:
    case OpCode::eSetGsOutputVertices:
    case OpCode::eSetGsOutputPrimitive:
    case OpCode::eSetTessPrimitive:
    case OpCode::eSetTessDomain:
      return 1u;

    case OpCode::eLabel:
      return getOperandCount() ? getOperandCount() - 1u : 0u;

    default:
      return getOperandCount();
  }
}


Op Op::DebugName(SsaDef def, const char* name) {
  Op op(OpCode::eDebugName, Type());
  op.addOperand(Operand(def));

  uint64_t lit = uint8_t(name[0u]);

  for (size_t i = 1u; name[i]; i++) {
    if (!(i % 8u)) {
      op.addOperand(Operand(lit));
      lit = uint8_t(name[i]);
    } else {
      lit |= uint64_t(uint8_t(name[i])) << (8u * (i % 8u));
    }
  }

  op.addOperand(Operand(lit));
  return op;
}

}
