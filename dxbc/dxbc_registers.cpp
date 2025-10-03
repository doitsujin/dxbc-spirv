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

  /* If scratch robustness is enabled, add an extra element so we can
   * use it as a fallback for out-of-bounds reads. */
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


bool RegisterFile::handleDclFunctionBody(
        ir::Builder&            builder,
  const Instruction&            op) {
  uint32_t fbIndex = op.getImm(0u).getImmediate<uint32_t>(0u);

  Operand operand(OperandInfo(), RegisterType::eFunctionBody, ComponentCount::e0Component);
  operand.addIndex(fbIndex);

  auto fbDef = declareEmptyFunction(builder, operand);

  if (fbIndex >= m_functionBodies.size())
    m_functionBodies.resize(fbIndex + 1u);

  m_functionBodies.at(fbIndex) = fbDef;
  return true;
}


bool RegisterFile::handleDclFunctionTable(
        ir::Builder&            builder,
  const Instruction&            op) {
  uint32_t ftIndex = op.getImm(0u).getImmediate<uint32_t>(0u);
  uint32_t ftSize = op.getImm(1u).getImmediate<uint32_t>(0u);

  if (ftIndex >= m_functionTables.size())
    m_functionTables.resize(ftIndex + 1u);

  auto& ft = m_functionTables.at(ftIndex);
  ft.resize(ftSize);

  for (uint32_t i = 0u; i < ftSize; i++) {
    Operand operand(OperandInfo(), RegisterType::eFunctionBody, ComponentCount::e0Component);
    operand.addIndex(op.getExtra(i).getImmediate<uint32_t>(0u));

    ft.at(i) = getFunctionForLabel(builder, op, operand);

    if (!ft.at(i))
      return false;
  }

  return true;
}


bool RegisterFile::handleDclInterface(
        ir::Builder&            builder,
  const Instruction&            op) {
  auto& iface = m_interfaces.emplace_back();
  iface.index = op.getImm(0u).getImmediate<uint32_t>(0u);

  auto ftSize = op.getImm(1u).getImmediate<uint32_t>(0u);
  auto metadata = op.getImm(2u).getImmediate<uint32_t>(0u);

  iface.count = util::bextract(metadata, 16u, 16u);
  iface.functions.resize(ftSize);

  for (uint32_t i = 0u; i < ftSize; i++) {
    if (!(iface.functions.at(i) = buildFcallFunction(builder, op, iface.index, iface.count, i)))
      return false;
  }

  return true;
}


ir::SsaDef RegisterFile::getFunctionForLabel(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand) {
  auto labelIndex = operand.getIndex(0u);

  if (operand.getRegisterType() == RegisterType::eFunctionBody && labelIndex < m_functionBodies.size())
    return m_functionBodies.at(labelIndex);

  if (operand.getRegisterType() != RegisterType::eLabel) {
    auto name = m_converter.makeRegisterDebugName(operand.getRegisterType(), labelIndex, WriteMask());
    m_converter.logOpError(op, "Operand ", name, " is not a valid label.");
    return ir::SsaDef();
  }

  if (labelIndex >= m_labels.size())
    m_labels.resize(labelIndex + 1u);

  if (!m_labels.at(labelIndex))
    m_labels.at(labelIndex) = declareEmptyFunction(builder, operand);

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

  /* Clamp array index to array size - 1 if necessary */
  if (operand.getRegisterType() == RegisterType::eIndexableTemp) {
    auto scratchReg = getIndexableTemp(regIndex);

    if (!scratchReg) {
      m_converter.logOpError(op, "Register not declared.");
      return ir::SsaDef();
    }
  }

  /* Scalarize loads to not make things unnecessarily complicated
   * for no reason. Temp regs are scalar anyway. */
  std::array<ir::SsaDef, 4u> components = { };

  for (auto c : swizzle.getReadMask(componentMask)) {
    auto component = componentFromBit(c);

    ir::SsaDef scalar;

    if (operand.getRegisterType() == RegisterType::eIndexableTemp) {
      auto scratchReg = getIndexableTemp(regIndex);
      dxbc_spv_assert(scratchReg);

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

  /* Verify that the array index is in bounds and skip store if not */
  if (operand.getRegisterType() == RegisterType::eIndexableTemp) {
    auto scratchReg = getIndexableTemp(regIndex);

    if (!scratchReg)
      return m_converter.logOpError(op, "Register not declared.");
  }

  uint32_t componentIndex = 0u;

  for (auto c : operand.getWriteMask()) {
    auto component = componentFromBit(c);

    /* Extract scalar and 'convert' to unknown type */
    auto scalar = m_converter.extractFromVector(builder, value, componentIndex++);

    if (!valueType.isUnknownType())
      scalar = builder.add(ir::Op::ConsumeAs(ir::ScalarType::eUnknown, scalar));

    if (operand.getRegisterType() == RegisterType::eIndexableTemp) {
      auto scratchReg = getIndexableTemp(regIndex);
      dxbc_spv_assert(scratchReg);

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


bool RegisterFile::emitFcall(
        ir::Builder&            builder,
  const Instruction&            op) {
  uint32_t function = op.getImm(0u).getImmediate<uint32_t>(0u);
  uint32_t baseIndex = op.getSrc(0u).getIndex(0u);

  for (const auto& iface : m_interfaces) {
    if (baseIndex >= iface.index && baseIndex < iface.index + iface.count) {
      if (function >= iface.functions.size())
        return m_converter.logOpError(op, "Invalid function index ", function, " for fp", baseIndex, ".");

      /* Compute absolute interface index */
      auto fpIndex = m_converter.loadOperandIndex(builder, op, op.getSrc(0u), 1u);
      fpIndex = builder.add(ir::Op::IAdd(ir::ScalarType::eU32, fpIndex, builder.makeConstant(baseIndex)));
      builder.add(ir::Op::FunctionCall(ir::ScalarType::eVoid, iface.functions.at(function)).addParam(fpIndex));
      return true;
    }
  }

  return m_converter.logOpError(op, "Undeclared interface ", baseIndex);
}


ir::SsaDef RegisterFile::emitThisLoad(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand,
        WriteMask               componentMask,
        ir::ScalarType          scalarType) {
  auto thisFunction = buildThisFunction(builder);

  auto thisIndex = m_converter.loadOperandIndex(builder, op, operand, 0u);
  auto thisType = ir::BasicType(ir::ScalarType::eU32, 4u);

  auto result = builder.add(ir::Op::FunctionCall(thisType, thisFunction).addParam(thisIndex));

  util::small_vector<ir::SsaDef, 4u> scalars = { };

  for (auto c : componentMask) {
    uint32_t idx = uint8_t(operand.getSwizzle().map(c));

    auto& scalar = scalars.emplace_back();
    scalar = builder.add(ir::Op::CompositeExtract(ir::ScalarType::eU32, result, builder.makeConstant(idx)));
    scalar = builder.add(ir::Op::ConsumeAs(scalarType, scalar));
  }

  return m_converter.buildVector(builder, scalarType, scalars.size(), scalars.data());
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


ir::SsaDef RegisterFile::declareEmptyFunction(ir::Builder& builder, const Operand& operand) {
  auto code = builder.getCode().first;

  /* Declare new function at the top of the code section and immediately
   * end it. We will emit code to it once the label is actually declared. */
  auto def = builder.addBefore(code->getDef(), ir::Op::Function(ir::ScalarType::eVoid));
  builder.addBefore(code->getDef(), ir::Op::FunctionEnd());

  if (m_converter.m_options.includeDebugNames) {
    builder.add(ir::Op::DebugName(def, m_converter.makeRegisterDebugName(
      operand.getRegisterType(), operand.getIndex(0u), WriteMask()).c_str()));
  }

  return def;
}


ir::SsaDef RegisterFile::loadThisCb(ir::Builder& builder) {
  if (!m_thisCb) {
    auto type = ir::Type()
      .addStructMember(ir::ScalarType::eU32)
      .addStructMember(ir::ScalarType::eU32)
      .addArrayDimension(ThisCbSize);

    m_thisCb = builder.add(ir::Op::DclCbv(type,
      m_converter.getEntryPoint(),
      m_converter.m_options.classInstanceRegisterSpace,
      m_converter.m_options.classInstanceRegisterIndex, 1u));

    builder.add(ir::Op::DebugName(m_thisCb, "class_instances"));
    builder.add(ir::Op::DebugMemberName(m_thisCb, 0u, "data"));
    builder.add(ir::Op::DebugMemberName(m_thisCb, 1u, "ft"));
  }

  return builder.add(ir::Op::DescriptorLoad(ir::ScalarType::eCbv, m_thisCb, builder.makeConstant(0u)));
}


ir::SsaDef RegisterFile::buildFcallFunction(ir::Builder& builder, const Instruction& op, uint32_t fpIndex, uint32_t fpCount, uint32_t function) {
  auto code = builder.getCode().first;

  /* Absolute function pointer index */
  auto param = builder.add(ir::Op::DclParam(ir::ScalarType::eU32));
  builder.add(ir::Op::DebugName(param, "fp"));

  /* Declare actual function */
  std::stringstream debugName;
  debugName << "fcall_" << function << "_fp" << fpIndex;

  auto functionDef = builder.addBefore(code->getDef(),
    ir::Op::Function(ir::ScalarType::eVoid).addParam(param));

  auto cursor = builder.setCursor(functionDef);
  builder.add(ir::Op::DebugName(functionDef, debugName.str().c_str()));

  /* Load function table index */
  auto thisIndex = (fpCount > 1u)
    ? builder.add(ir::Op::ParamLoad(ir::ScalarType::eU32, functionDef, param))
    : builder.makeConstant(fpIndex);

  auto ftIndex = builder.add(ir::Op::BufferLoad(ir::ScalarType::eU32, loadThisCb(builder),
    builder.add(ir::Op::CompositeConstruct(ir::BasicType(ir::ScalarType::eU32, 2u), thisIndex, builder.makeConstant(1u))), 4u));

  /* Select function body to call */
  auto switchConstruct = builder.add(ir::Op::ScopedSwitch(ir::SsaDef(), ftIndex));

  for (uint32_t i = 0u; i < op.getExtraCount(); i++) {
    uint32_t ftIndex = op.getExtra(i).getImmediate<uint32_t>(0u);

    if (ftIndex >= m_functionTables.size() || m_functionTables.at(ftIndex).empty()) {
      m_converter.logOpError(op, "Function table ", ftIndex, " not declared.");
      return ir::SsaDef();
    }

    const auto& ft = m_functionTables.at(ftIndex);

    if (function >= ft.size()) {
      m_converter.logOpError(op, "Function index ", function, " exceeds function count in function table ", ftIndex, ".");
      return ir::SsaDef();
    }

    builder.add(ir::Op::ScopedSwitchCase(switchConstruct, ftIndex));
    builder.add(ir::Op::FunctionCall(ir::ScalarType::eVoid, ft.at(function)));
    builder.add(ir::Op::ScopedSwitchBreak(switchConstruct));
  }

  auto switchEnd = builder.add(ir::Op::ScopedEndSwitch(switchConstruct));
  builder.rewriteOp(switchConstruct, ir::Op(builder.getOp(switchConstruct)).setOperand(0u, switchEnd));

  builder.add(ir::Op::FunctionEnd());
  builder.setCursor(cursor);
  return functionDef;
}


ir::SsaDef RegisterFile::buildThisFunction(ir::Builder& builder) {
  if (m_thisFunction)
    return m_thisFunction;

  auto code = builder.getCode().first;

  /* Absolute function pointer index */
  auto param = builder.add(ir::Op::DclParam(ir::ScalarType::eU32));
  builder.add(ir::Op::DebugName(param, "idx"));

  m_thisFunction = builder.addBefore(code->getDef(), ir::Op::Function(
    ir::BasicType(ir::ScalarType::eU32, 4u)).addParam(param));

  auto cursor = builder.setCursor(m_thisFunction);
  builder.add(ir::Op::DebugName(m_thisFunction, "this"));

  auto thisIndex = builder.add(ir::Op::ParamLoad(ir::ScalarType::eU32, m_thisFunction, param));
  auto packedData = builder.add(ir::Op::BufferLoad(ir::ScalarType::eU32, loadThisCb(builder),
    builder.add(ir::Op::CompositeConstruct(ir::BasicType(ir::ScalarType::eU32, 2u), thisIndex, builder.makeConstant(0u))), 4u));

  ir::Op compositeOp(ir::OpCode::eCompositeConstruct, ir::BasicType(ir::ScalarType::eU32, 4u));
  compositeOp.addOperand(builder.add(ir::Op::UBitExtract(ir::ScalarType::eU32, packedData, builder.makeConstant(0u), builder.makeConstant(4u))));
  compositeOp.addOperand(builder.add(ir::Op::UBitExtract(ir::ScalarType::eU32, packedData, builder.makeConstant(16u), builder.makeConstant(16u))));
  compositeOp.addOperand(builder.add(ir::Op::UBitExtract(ir::ScalarType::eU32, packedData, builder.makeConstant(8u), builder.makeConstant(8u))));
  compositeOp.addOperand(builder.add(ir::Op::UBitExtract(ir::ScalarType::eU32, packedData, builder.makeConstant(4u), builder.makeConstant(4u))));

  builder.add(ir::Op::Return(compositeOp.getType(), builder.add(compositeOp)));
  builder.add(ir::Op::FunctionEnd());
  builder.setCursor(cursor);

  return m_thisFunction;
}

}
