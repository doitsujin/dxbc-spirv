#include <limits>
#include <iostream>

#include "ir_pass_lower_min16.h"

namespace dxbc_spv::ir {

template<typename T>
std::pair<Operand, Operand> getRange() {
  return std::make_pair(
    Operand(std::numeric_limits<T>::min()),
    Operand(std::numeric_limits<T>::max()));
}

template<>
std::pair<Operand, Operand> getRange<float16_t>() {
  return std::make_pair(
    Operand(float16_t::fromRaw(0xfbffu)),
    Operand(float16_t::fromRaw(0x7bffu)));
}


LowerMin16Pass::LowerMin16Pass(Builder& builder, const Options& options)
: m_builder(builder), m_options(options) {

}


LowerMin16Pass::~LowerMin16Pass() {

}


void LowerMin16Pass::run() {
  auto iter = m_builder.begin();

  while (iter != m_builder.end()) {
    switch (iter->getOpCode()) {
      case OpCode::eConstant:
        iter = handleConstant(iter);
        break;

      case OpCode::eUndef:
        iter = handleUndef(iter);
        break;

      case OpCode::eMinValue:
      case OpCode::eMaxValue:
        iter = handleMinMaxValue(iter);
        break;

      default:
        iter = handleOp(iter);
    }
  }
}


void LowerMin16Pass::runPass(Builder& builder, const Options& options) {
  LowerMin16Pass(builder, options).run();
}


Builder::iterator LowerMin16Pass::handleConstant(Builder::iterator op) {
  auto type = resolveType(op->getType());

  if (type == op->getType())
    return ++op;

  Op constant(OpCode::eConstant, type);

  /* Manually convert scalar operands as necessary */
  uint32_t scalars = op->getType().computeFlattenedScalarCount();

  for (uint32_t i = 0u; i < scalars; i++) {
    constant.addOperand(convertScalarConstant(op->getOperand(i),
      op->getType().resolveFlattenedType(i)));
  }

  /* Replace previous constant with the converted one */
  return m_builder.iter(m_builder.rewriteDef(op->getDef(),
    m_builder.add(std::move(constant))));
}


Builder::iterator LowerMin16Pass::handleUndef(Builder::iterator op) {
  auto type = resolveType(op->getType());

  if (type == op->getType())
    return ++op;

  /* Replace undef with undef of the resolved type */
  auto undef = m_builder.makeUndef(resolveType(op->getType()));
  return m_builder.iter(m_builder.rewriteDef(op->getDef(), undef));
}


Builder::iterator LowerMin16Pass::handleOp(Builder::iterator op) {
  /* Replace type but keep the instruction itself intact */
  m_builder.setOpType(op->getDef(), resolveType(op->getType()));
  return ++op;
}


Builder::iterator LowerMin16Pass::handleMinMaxValue(Builder::iterator op) {
  /* Lower to appropriate constant and remove instruction */
  auto range = resolveRange(op->getType().getBaseType(0u).getBaseType());
  auto value = op->getOpCode() == OpCode::eMinValue ? range.first : range.second;

  auto constant = m_builder.add(Op(OpCode::eConstant, op->getType()).addOperand(value));
  return m_builder.iter(m_builder.rewriteDef(op->getDef(), constant));
}


Operand LowerMin16Pass::convertScalarConstant(Operand srcValue, ScalarType srcType) const {
  /* Min precision constants use 32-bit literals */
  switch (srcType) {
    case ScalarType::eMinF10:
    case ScalarType::eMinF16: {
      if (!m_options.enableFloat16)
        return srcValue;

      return Operand(float16_t(float(srcValue)));
    }

    case ScalarType::eMinU16: {
      if (!m_options.enableInt16)
        return srcValue;

      return Operand(uint16_t(uint32_t(srcValue)));
    }

    case ScalarType::eMinI16: {
      if (!m_options.enableInt16)
        return srcValue;

      return Operand(int16_t(int32_t(srcValue)));
    }

    default:
      return srcValue;
  }
}


Type LowerMin16Pass::resolveType(Type type) const {
  Type result;

  for (uint32_t i = 0u; i < type.getStructMemberCount(); i++)
    result.addStructMember(resolveBasicType(type.getBaseType(i)));

  for (uint32_t i = 0u; i < type.getArrayDimensions(); i++)
    result.addArrayDimension(type.getArraySize(i));

  return result;
}


BasicType LowerMin16Pass::resolveBasicType(BasicType type) const {
  return BasicType(resolveScalarType(type.getBaseType()), type.getVectorSize());
}


ScalarType LowerMin16Pass::resolveScalarType(ScalarType type) const {
  switch (type) {
    case ScalarType::eMinF10:
    case ScalarType::eMinF16:
      return m_options.enableFloat16
        ? ScalarType::eF16
        : ScalarType::eF32;

    case ScalarType::eMinI16:
      return m_options.enableInt16
        ? ScalarType::eI16
        : ScalarType::eI32;

    case ScalarType::eMinU16:
      return m_options.enableInt16
        ? ScalarType::eU16
        : ScalarType::eU32;

    default:
      return type;
  }
}


std::pair<Operand, Operand> LowerMin16Pass::resolveRange(ScalarType type) const {
  switch (resolveScalarType(type)) {
    /* Explicit numeric types */
    case ScalarType::eU8:  return getRange<uint8_t>();
    case ScalarType::eU16: return getRange<uint16_t>();
    case ScalarType::eU32: return getRange<uint32_t>();
    case ScalarType::eU64: return getRange<uint64_t>();

    case ScalarType::eI8:  return getRange<int8_t>();
    case ScalarType::eI16: return getRange<int16_t>();
    case ScalarType::eI32: return getRange<int32_t>();
    case ScalarType::eI64: return getRange<int64_t>();

    case ScalarType::eF16: return getRange<float16_t>();
    case ScalarType::eF32: return getRange<float>();
    case ScalarType::eF64: return getRange<double>();

    /* Min precision types that we're trying to get rid of */
    case ScalarType::eMinF10:
    case ScalarType::eMinF16:
    case ScalarType::eMinI16:
    case ScalarType::eMinU16:
      break;

    /* Ambiguous integer types */
    case ScalarType::eAnyI8:
    case ScalarType::eAnyI16:
    case ScalarType::eAnyI32:
    case ScalarType::eAnyI64:
      break;

    /* non-numeric types */
    case ScalarType::eVoid:
    case ScalarType::eUnknown:
    case ScalarType::eBool:
      break;

    /* resource types */
    case ScalarType::eSampler:
    case ScalarType::eCbv:
    case ScalarType::eSrv:
    case ScalarType::eUav:
    case ScalarType::eUavCounter:
      break;

    /* invalid enum */
    case ScalarType::eCount:
      break;
  }

  dxbc_spv_unreachable();
  return std::make_pair(Operand(), Operand());
}

}
