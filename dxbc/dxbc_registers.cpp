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


bool RegisterFile::handleDclTgsmRaw(ir::Builder& builder, const Instruction& op) {
  /* dcl_tgsm_raw:
   * (dst0) The register to declare
   * (imm0) Byte count (not dword count)
   */
  auto byteCount = op.getImm(0u).getImmediate<uint32_t>(0u);

  if (!byteCount || byteCount > MaxTgsmSize)
    return m_converter.logOpError(op, "Invalid byte count for LDS: ", byteCount);

  auto type = ir::Type(ir::ScalarType::eUnknown)
    .addArrayDimension(byteCount / sizeof(uint32_t));

  return declareLds(builder, op, op.getDst(0u), type);
}


bool RegisterFile::handleDclTgsmStructured(ir::Builder& builder, const Instruction& op) {
  /* dcl_tgsm_structured:
   * (dst0) The register to declare
   * (imm0) Structure size, in bytes
   * (imm1) Structure count
   */
  auto structSize = op.getImm(0u).getImmediate<uint32_t>(0u);
  auto structCount = op.getImm(1u).getImmediate<uint32_t>(0u);

  if (!structSize || !structCount || structCount * structSize > MaxTgsmSize)
    return m_converter.logOpError(op, "Invalid structure size or count for LDS: ", structSize, "[", structCount, "]");

  auto type = ir::Type(ir::ScalarType::eUnknown)
    .addArrayDimension(structSize / sizeof(uint32_t))
    .addArrayDimension(structCount);

  return declareLds(builder, op, op.getDst(0u), type);
}


ir::SsaDef RegisterFile::getFunctionForLabel(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand) {
  /* It is unclear how labels are supposed to interact with the per-phase
   * register spaces in hull shaders. Since there currently are no known
   * uses of this in the wild, don't bother supporting this for now. */
  if (m_converter.m_parser.getShaderInfo().getType() == ShaderType::eHull) {
    m_converter.logOpError(op, "Subroutines not supported in hull shaders.");
    return ir::SsaDef();
  }

  auto labelIndex = operand.getIndex(0u);

  if (operand.getRegisterType() != RegisterType::eLabel) {
    auto name = m_converter.makeRegisterDebugName(operand.getRegisterType(), labelIndex, WriteMask());
    m_converter.logOpError(op, "Operand ", name, " is not a valid label.");
    return ir::SsaDef();
  }

  if (labelIndex >= m_labels.size())
    m_labels.resize(labelIndex + 1u);

  if (!m_labels.at(labelIndex)) {
    auto code = builder.getCode().first;

    /* Declare new function at the top of the code section and immediately
     * end it. We will emit code to it once the label is actually declared. */
    m_labels.at(labelIndex) = builder.addBefore(
      code->getDef(), ir::Op::Function(ir::ScalarType::eVoid));

    builder.addBefore(code->getDef(), ir::Op::FunctionEnd());
  }

  return m_labels.at(labelIndex);
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


ir::SsaDef RegisterFile::emitTgsmLoad(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand,
        ir::SsaDef              elementIndex,
        ir::SsaDef              elementOffset,
        WriteMask               componentMask,
        ir::ScalarType          scalarType) {
  auto tgsmReg = getTgsmRegister(op, operand);

  if (!tgsmReg)
    return ir::SsaDef();

  const auto& tgsmType = builder.getOp(tgsmReg).getType();
  bool isStructured = !tgsmType.getSubType(0u).isScalarType();

  /* Scalarize loads */
  std::array<ir::SsaDef, 4u> components = { };
  auto readMask = operand.getSwizzle().getReadMask(componentMask);

  for (auto c : readMask) {
    auto componentIndex = uint8_t(componentFromBit(c));

    auto address = isStructured
      ? m_converter.computeStructuredAddress(builder, elementIndex, elementOffset, c)
      : m_converter.computeRawAddress(builder, elementIndex, c);

    components[componentIndex] = builder.add(ir::Op::LdsLoad(ir::ScalarType::eUnknown, tgsmReg, address));

    if (scalarType != ir::ScalarType::eUnknown)
      components[componentIndex] = builder.add(ir::Op::ConsumeAs(scalarType, components[componentIndex]));
  }

  /* Create result vector */
  return m_converter.composite(builder,
    m_converter.makeVectorType(scalarType, componentMask),
    components.data(), operand.getSwizzle(), componentMask);
}


bool RegisterFile::emitTgsmStore(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand,
        ir::SsaDef              elementIndex,
        ir::SsaDef              elementOffset,
        ir::SsaDef              data) {
  auto tgsmReg = getTgsmRegister(op, operand);

  if (!tgsmReg)
    return false;

  const auto& tgsmType = builder.getOp(tgsmReg).getType();

  auto scalarType = tgsmType.getBaseType(0u).getBaseType();
  auto dataType = builder.getOp(data).getType().getBaseType(0u).getBaseType();

  bool isStructured = !tgsmType.getSubType(0u).isScalarType();

  /* Scalarize stores */
  uint32_t srcIndex = 0u;

  for (auto c : operand.getWriteMask()) {
    auto address = isStructured
      ? m_converter.computeStructuredAddress(builder, elementIndex, elementOffset, c)
      : m_converter.computeRawAddress(builder, elementIndex, c);

    auto scalar = m_converter.extractFromVector(builder, data, srcIndex++);

    if (scalarType != dataType)
      scalar = builder.add(ir::Op::ConsumeAs(scalarType, scalar));

    builder.add(ir::Op::LdsStore(tgsmReg, address, scalar));
  }

  return true;
}


std::pair<ir::SsaDef, ir::SsaDef> RegisterFile::computeTgsmAddress(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand,
  const Operand&                address) {
  auto tgsmReg = getTgsmRegister(op, operand);

  if (!tgsmReg)
    return std::make_pair(ir::SsaDef(), ir::SsaDef());

  const auto& tgsmType = builder.getOp(tgsmReg).getType();
  bool isStructured = !tgsmType.getSubType(0u).isScalarType();

  auto result = m_converter.computeAtomicBufferAddress(builder, op, address,
    isStructured ? ir::ResourceKind::eBufferStructured : ir::ResourceKind::eBufferRaw);

  return std::make_pair(tgsmReg, result);
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


ir::SsaDef RegisterFile::getTgsmRegister(const Instruction& op, const Operand& operand) {
  if (operand.getRegisterType() != RegisterType::eTgsm) {
    m_converter.logOpError(op, "Register not a valid TGSM register.");
    return ir::SsaDef();
  }

  uint32_t index = operand.getIndex(0u);

  if (index >= m_gRegs.size()) {
    m_converter.logOpError(op, "TGSM register not declared.");
    return ir::SsaDef();
  }

  return m_gRegs[index];
}


bool RegisterFile::declareLds(ir::Builder& builder, const Instruction& op, const Operand& operand, const ir::Type& type) {
  auto regIndex = operand.getIndex(0u);

  if (regIndex >= m_gRegs.size())
    m_gRegs.resize(regIndex + 1u);

  if (m_gRegs[regIndex]) {
    auto name = m_converter.makeRegisterDebugName(operand.getRegisterType(), regIndex, WriteMask());
    return m_converter.logOpError(op, "Register ", name, " already declared");
  }

  m_gRegs[regIndex] = builder.add(ir::Op::DclLds(type, m_converter.getEntryPoint()));

  /* Emit debug name */
  if (m_converter.m_options.includeDebugNames) {
    auto name = m_converter.makeRegisterDebugName(operand.getRegisterType(), regIndex, WriteMask());
    builder.add(ir::Op::DebugName(m_gRegs[regIndex], name.c_str()));
  }

  return true;
}

}
