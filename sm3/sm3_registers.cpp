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

      default:
        dxbc_spv_unreachable();
        return false;
    }

    builder.add(ir::Op::TmpStore(reg, scalar));

    componentIndex++;
  }

  return true;
}


}
