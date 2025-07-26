#include "dxbc_registers.h"
#include "dxbc_converter.h"

namespace dxbc_spv::dxbc {

RegisterFile::RegisterFile(Converter& converter)
: m_converter(converter) {

}


RegisterFile::~RegisterFile() {

}


void RegisterFile::handleHsPhase() {
  m_rRegs.clear();
  m_xRegs.clear();
}


bool RegisterFile::handleDclIndexableTemp(ir::Builder& builder, const Instruction& op) {
  /* dcl_indexable_temp:
   * (imm0) The register index to declare
   * (imm1) Array length
   * (imm2) Vector component count. In fxc-generated binaries, this
   *        is always 4, so we will ignore it and optimize later.
   */
  auto index = op.getImm(0u).getImmediate<uint32_t>(0u);
  auto arraySize = op.getImm(1u).getImmediate<uint32_t>(0u);

  if (!arraySize)
    return m_converter.logOpError(op, "Invalid array size: ", arraySize);

  /* Declare actual scratch variable */
  if (index >= m_xRegs.size())
    m_xRegs.resize(index + 1u);

  if (m_xRegs[index]) {
    auto name = m_converter.makeRegisterDebugName(RegisterType::eIndexableTemp, index, WriteMask());
    return m_converter.logOpError(op, "Register ", name, " already declared");
  }

  auto scratchType = ir::Type(ir::ScalarType::eUnknown, 4u).addArrayDimension(arraySize);
  m_xRegs[index] = builder.add(ir::Op::DclScratch(scratchType, m_converter.getEntryPoint()));

  /* Emit debug name */
  if (m_converter.m_options.includeDebugNames) {
    auto name = m_converter.makeRegisterDebugName(RegisterType::eIndexableTemp, index, WriteMask());
    builder.add(ir::Op::DebugName(m_xRegs[index], name.c_str()));
  }

  return true;
}


ir::SsaDef RegisterFile::emitLoad(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand,
        WriteMask               componentMask,
        ir::ScalarType          type) {
  auto swizzle = operand.getSwizzle();
  auto returnType = m_converter.makeVectorType(type, componentMask);

  if (operand.getIndexType(0u) != IndexType::eImm32) {
    m_converter.logOpError(op, "Register index must be immediate.");
    return ir::SsaDef();
  }

  auto regIndex = operand.getIndex(0u);
  auto arrayIndex = loadArrayIndex(builder, op, operand);

  /* Scalarize loads to not make things unnecessarily complicated
   * for no reason. Temp regs are scalar anyway. */
  std::array<ir::SsaDef, 4u> components = { };

  for (auto c : swizzle.getReadMask(componentMask)) {
    auto component = componentFromBit(c);

    ir::SsaDef scalar;

    if (operand.getRegisterType() == RegisterType::eIndexableTemp) {
      auto scratchReg = getIndexableTemp(regIndex);

      if (!scratchReg) {
        m_converter.logOpError(op, "Register not declared.");
        return ir::SsaDef();
      }

      /* Scratch is vec4, so use two indices */
      auto address = builder.add(ir::Op::CompositeConstruct(
        ir::Type(ir::ScalarType::eU32, 2u), arrayIndex, builder.makeConstant(uint32_t(component))));
      scalar = builder.add(ir::Op::ScratchLoad(ir::ScalarType::eUnknown, scratchReg, address));
    } else {
      auto tmpReg = getOrDeclareTemp(builder, regIndex, component);
      scalar = builder.add(ir::Op::TmpLoad(ir::ScalarType::eUnknown, tmpReg));
    }

    /* Convert to requested type */
    if (type != ir::ScalarType::eUnknown)
      scalar = builder.add(ir::Op::ConsumeAs(type, scalar));

    components[uint8_t(component)] = scalar;
  }

  return m_converter.composite(builder, returnType, components.data(), swizzle, componentMask);
}


bool RegisterFile::emitStore(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand,
        ir::SsaDef              value) {
  if (operand.getIndexType(0u) != IndexType::eImm32)
    return m_converter.logOpError(op, "Register index must be immediate.");

  const auto& valueDef = builder.getOp(value);
  auto valueType = valueDef.getType().getBaseType(0u);

  auto regIndex = operand.getIndex(0u);
  auto arrayIndex = loadArrayIndex(builder, op, operand);

  uint32_t componentIndex = 0u;

  for (auto c : operand.getWriteMask()) {
    auto component = componentFromBit(c);

    /* Extract scalar and 'convert' to unknown type */
    auto scalar = m_converter.extractFromVector(builder, value, componentIndex++);

    if (!valueType.isUnknownType())
      scalar = builder.add(ir::Op::ConsumeAs(ir::ScalarType::eUnknown, scalar));

    if (operand.getRegisterType() == RegisterType::eIndexableTemp) {
      auto scratchReg = getIndexableTemp(regIndex);

      if (!scratchReg)
        return m_converter.logOpError(op, "Register not declared.");

      /* Scratch is vec4, so use two indices */
      auto address = builder.add(ir::Op::CompositeConstruct(
        ir::Type(ir::ScalarType::eU32, 2u), arrayIndex, builder.makeConstant(uint32_t(component))));
      builder.add(ir::Op::ScratchStore(scratchReg, address, scalar));
    } else {
      auto tmpReg = getOrDeclareTemp(builder, regIndex, component);
      builder.add(ir::Op::TmpStore(tmpReg, scalar));
    }
  }

  return true;
}


ir::SsaDef RegisterFile::loadArrayIndex(ir::Builder& builder, const Instruction& op, const Operand& operand) {
  if (operand.getRegisterType() != RegisterType::eIndexableTemp)
    return ir::SsaDef();

  return m_converter.loadOperandIndex(builder, op, operand, 1u);
}


ir::SsaDef RegisterFile::getOrDeclareTemp(ir::Builder& builder, uint32_t index, Component component) {
  uint32_t tempIndex = 4u * index + uint8_t(component);

  if (tempIndex >= m_rRegs.size())
    m_rRegs.resize(tempIndex + 1u);

  if (!m_rRegs[tempIndex])
    m_rRegs[tempIndex] = builder.add(ir::Op::DclTmp(ir::ScalarType::eUnknown, m_converter.getEntryPoint()));

  return m_rRegs[tempIndex];
}


ir::SsaDef RegisterFile::getIndexableTemp(uint32_t index) {
  return index < m_xRegs.size() ? m_xRegs[index] : ir::SsaDef();
}

}
