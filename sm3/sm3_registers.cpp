#include "sm3_registers.h"

#include "sm3_converter.h"

#include "../ir/ir_utils.h"

namespace dxbc_spv::sm3 {

RegisterFile::RegisterFile(Converter &converter)
: m_converter(converter) {
}

RegisterFile::~RegisterFile() {

}

void RegisterFile::initialize(ir::Builder& builder) {
  uint32_t addressRegisterComponents = m_converter.getShaderInfo().getVersion().first >= 2u ? 4u : 1u;

  for (uint32_t i = 0u; i < addressRegisterComponents; i++) {
    m_a0Reg[i] = builder.add(ir::Op::DclTmp(ir::ScalarType::eI32, m_converter.getEntryPoint()));

    if (m_converter.getOptions().includeDebugNames) {
      std::string name = m_converter.makeRegisterDebugName(RegisterType::eAddr, 0u, util::componentBit(Component(i)));
      builder.add(ir::Op::DebugName(m_a0Reg[i], name.c_str()));
    }
  }

  m_aLReg = builder.add(ir::Op::DclTmp(ir::ScalarType::eI32, m_converter.getEntryPoint()));

  if (m_converter.getOptions().includeDebugNames) {
    std::string name = m_converter.makeRegisterDebugName(RegisterType::eLoop, 0u, ComponentBit::eAll);
    builder.add(ir::Op::DebugName(m_aLReg, name.c_str()));
  }

  for (uint32_t i = 0u; i < 4u; i++) {
    m_pReg[i] = builder.add(ir::Op::DclTmp(ir::ScalarType::eBool, m_converter.getEntryPoint()));

    if (m_converter.getOptions().includeDebugNames) {
      std::string name = m_converter.makeRegisterDebugName(RegisterType::ePredicate, 0u, util::componentBit(Component(i)));
      builder.add(ir::Op::DebugName(m_pReg[i], name.c_str()));
    }
  }
}

ir::SsaDef RegisterFile::getOrDeclareTemp(ir::Builder& builder, uint32_t index, Component component) {
  uint32_t tempIndex = 4u * index + uint8_t(component);

  if (tempIndex >= m_rRegs.size())
    m_rRegs.resize(tempIndex + 1);

  if (!m_rRegs[tempIndex]) {
    m_rRegs[tempIndex] = builder.add(ir::Op::DclTmp(ir::ScalarType::eUnknown, m_converter.getEntryPoint()));

    if (m_converter.getOptions().includeDebugNames) {
      std::string name = m_converter.makeRegisterDebugName(RegisterType::eTemp, index, util::componentBit(component));
      builder.add(ir::Op::DebugName(m_rRegs[tempIndex], name.c_str()));
    }
  }

  return m_rRegs[tempIndex];
}

ir::SsaDef RegisterFile::emitTempLoad(
  ir::Builder& builder,
  uint32_t regIndex,
  Swizzle swizzle,
  WriteMask componentMask,
  ir::ScalarType type
) {
  auto returnType = makeVectorType(type, componentMask);

  std::array<ir::SsaDef, 4u> components = { };

  for (auto c : swizzle.getReadMask(componentMask)) {
    auto component = componentFromBit(c);

    auto tmpReg = getOrDeclareTemp(builder, regIndex, component);
    auto scalar = builder.add(ir::Op::TmpLoad(ir::ScalarType::eUnknown, tmpReg));

    /* Convert to requested type */
    scalar = builder.add(ir::Op::ConsumeAs(type, scalar));

    components[uint8_t(component)] = scalar;
  }

  return composite(builder, returnType, components.data(), swizzle, componentMask);
}


ir::SsaDef RegisterFile::emitPredicateLoad(
            ir::Builder&            builder,
            Swizzle                 swizzle,
            WriteMask               componentMask) {
  auto returnType = makeVectorType(ir::ScalarType::eBool, componentMask);

  std::array<ir::SsaDef, 4u> components = { };
  for (auto c : swizzle.getReadMask(componentMask)) {
    auto component = componentFromBit(c);
    components[uint8_t(component)] = builder.add(ir::Op::TmpLoad(ir::ScalarType::eBool, m_pReg[uint8_t(component)]));
  }

  return composite(builder, returnType, components.data(), swizzle, componentMask);
}


ir::SsaDef RegisterFile::emitAddressLoad(
            ir::Builder&            builder,
            RegisterType            registerType,
            Swizzle                 swizzle) {
  Component component = swizzle.get(0u);

  if (registerType == RegisterType::eAddr) {
    /* The a0 register only has a single component on SM1.1 */
    dxbc_spv_assert(m_converter.getShaderInfo().getVersion().first >= 2u
        || component == Component::eX);

    dxbc_spv_assert(m_a0Reg[uint8_t(component)]);

    return builder.add(ir::Op::TmpLoad(ir::ScalarType::eI32, m_a0Reg[uint8_t(component)]));
  } else if (registerType == RegisterType::eLoop) {
    dxbc_spv_assert(component == Component::eX);

    return builder.add(ir::Op::TmpLoad(ir::ScalarType::eI32, m_aLReg));
  } else {
    ShaderInfo info = m_converter.getShaderInfo();

    Logger::err("Register type: ",
      UnambiguousRegisterType { registerType, info.getType(), info.getVersion().first },
      " not supported as a relative index");

    dxbc_spv_unreachable();
    return ir::SsaDef();
  }
}


bool RegisterFile::emitStore(
          ir::Builder&            builder,
    const Operand&                operand,
          WriteMask               writeMask,
          ir::SsaDef              predicateVec,
          ir::SsaDef              value) {
  const auto& valueDef = builder.getOp(value);
  auto valueType = valueDef.getType().getBaseType(0u);

  auto regIndex = operand.getIndex();

  uint32_t componentIndex = 0u;

  for (auto c : writeMask) {
    auto component = componentFromBit(c);

    /* Extract scalar and 'convert' to unknown type */
    auto scalar = extractFromVector(builder, value, componentIndex);

    ir::SsaDef reg;

    switch (operand.getRegisterType()) {
      case RegisterType::eTemp: {
        reg = getOrDeclareTemp(builder, regIndex, component);

        if (!valueType.isUnknownType())
          scalar = builder.add(ir::Op::ConsumeAs(ir::ScalarType::eUnknown, scalar));
      } break;

      case RegisterType::eAddr: {
        dxbc_spv_assert(m_converter.getShaderInfo().getVersion().first >= 2u || c == ComponentBit::eX);
        dxbc_spv_assert(m_converter.getShaderInfo().getType() == ShaderType::eVertex);

        if (!valueType.isIntType()) {
          /* a0 can be written to using the mova instruction on SM2+
           * or the mov instruction on SM1.1. */

          if (valueType.isUnknownType())
            scalar = builder.add(ir::Op::ConsumeAs(ir::ScalarType::eF32, scalar));

          scalar = builder.add(ir::Op::FRound(ir::ScalarType::eF32, scalar, ir::RoundMode::eNearestEven));
          scalar = builder.add(ir::Op::ConvertFtoI(ir::ScalarType::eI32, scalar));
        }

        reg = m_a0Reg[uint8_t(component)];
      } break;

      case RegisterType::ePredicate: {
        dxbc_spv_assert(valueType.isBoolType());
        reg = m_pReg[uint8_t(component)];
      } break;

      default: dxbc_spv_assert(false); return false;
    }

    ir::SsaDef predicateIf = ir::SsaDef();

    if (predicateVec) {
      /* Check if the matching component of the predicate register vector is true first. */
      auto condComponent = extractFromVector(builder, predicateVec, componentIndex);
      predicateIf = builder.add(ir::Op::ScopedIf(ir::SsaDef(), condComponent));
    }

    builder.add(ir::Op::TmpStore(reg, scalar));

    if (predicateIf) {
      auto predicateIfEnd = builder.add(ir::Op::ScopedEndIf(predicateIf));
      builder.rewriteOp(predicateIf, ir::Op(builder.getOp(predicateIf)).setOperand(0u, predicateIfEnd));
    }

    componentIndex++;
  }

  return true;
}


ir::SsaDef RegisterFile::emitLoopCounterLoad(
            ir::Builder&            builder) {
  return builder.add(ir::Op::TmpLoad(ir::ScalarType::eI32, m_aLReg));
}


void RegisterFile::emitLoopCounterStore(
            ir::Builder&            builder,
            ir::SsaDef              value) {
  auto op = builder.getOp(value);
  auto type = op.getType();

  if (type != ir::ScalarType::eI32)
    value = builder.add(ir::Op::ConsumeAs(ir::ScalarType::eI32, value));

  builder.add(ir::Op::TmpStore(m_aLReg, value));
}


}
