#include <limits>
#include <iostream>

#include "ir_pass_lower_min16.h"

namespace dxbc_spv::ir {

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

}
