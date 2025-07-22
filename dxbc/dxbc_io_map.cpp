#include "dxbc_converter.h"
#include "dxbc_io_map.h"

#include "../util/util_log.h"

namespace dxbc_spv::dxbc {

IoMap::IoMap(Converter& converter)
: m_converter(converter) {

}


IoMap::~IoMap() {

}


bool IoMap::init(const Container& dxbc, ShaderInfo shaderInfo) {
  m_shaderInfo = shaderInfo;

  return initSignature(m_isgn, dxbc.getInputSignatureChunk())
      && initSignature(m_osgn, dxbc.getOutputSignatureChunk())
      && initSignature(m_psgn, dxbc.getPatchConstantSignatureChunk());
}


bool IoMap::handleDclStream(const Operand& operand) {
  /* Operand validation must be done by the converter itself */
  dxbc_spv_assert(operand.getRegisterType() == RegisterType::eStream &&
                  operand.getIndexDimensions() == 1u &&
                  operand.getIndexType(0u) == IndexType::eImm32);

  m_gsStream = operand.getIndex(0u);
  return true;
}


bool IoMap::handleDclIoVar(ir::Builder& builder, const Instruction& op) {
  dxbc_spv_assert(op.getDstCount());
  const auto& operand = op.getDst(0u);

  /* Determine register type being declared. Some of these are handled
   * externally since they do not map directly to a built-in value. */
  auto regType = normalizeRegisterType(operand.getRegisterType());

  if (regType == RegisterType::eRasterizer ||
      regType == RegisterType::eForkInstanceId ||
      regType == RegisterType::eJoinInstanceId)
    return true;

  /* If this declaration declares a built-in register, emit it directions,
   * we don't need to do anything else here. */
  if (!isRegularIoRegister(regType))
    return declareIoBuiltIn(builder, regType);

  return declareIoRegisters(builder, op, regType);
}


bool IoMap::handleDclIndexRange(ir::Builder& builder, const Instruction& op) {
  /* TODO implement */
  m_converter.logOpError(op, "Unhandled instruction.");
  return true;
}


ir::SsaDef IoMap::emitLoad(ir::Builder& builder, const Operand& operand, WriteMask componentMask) {
  (void)builder;
  (void)operand;
  (void)componentMask;

  dxbc_spv_unreachable();
  return ir::SsaDef();
}


bool IoMap::emitStore(ir::Builder& builder, const Operand& operand, ir::SsaDef value) {
  (void)builder;
  (void)operand;
  (void)value;

  dxbc_spv_unreachable();
  return false;
}


bool IoMap::declareIoBuiltIn(ir::Builder& builder, RegisterType regType) {
  switch (regType) {
    case RegisterType::ePrimitiveId: {
      return declareDedicatedBuiltIn(builder, regType,
        ir::ScalarType::eU32, ir::BuiltIn::ePrimitiveId, "SV_PrimitiveID");
    }

    case RegisterType::eDepth:
    case RegisterType::eDepthGe:
    case RegisterType::eDepthLe: {
      const char* semanticName = "SV_Depth";

      if (regType == RegisterType::eDepthGe) {
        builder.add(ir::Op::SetPsDepthGreaterEqual(m_converter.getEntryPoint()));
        semanticName = "SV_DepthGreaterEqual";
      } else if (regType == RegisterType::eDepthLe) {
        builder.add(ir::Op::SetPsDepthLessEqual(m_converter.getEntryPoint()));
        semanticName = "SV_DepthLessEqual";
      }

      return declareDedicatedBuiltIn(builder, regType,
        ir::ScalarType::eF32, ir::BuiltIn::eDepth, semanticName);
    }

    case RegisterType::eCoverageIn:
    case RegisterType::eCoverageOut: {
      return declareDedicatedBuiltIn(builder, regType,
        ir::ScalarType::eU32, ir::BuiltIn::eSampleMask, "SV_PrimitiveID");
    }

    case RegisterType::eControlPointId: {
      return declareDedicatedBuiltIn(builder, regType,
        ir::ScalarType::eU32, ir::BuiltIn::eTessControlPointId, "SV_OutputControlPointId");
    }

    case RegisterType::eTessCoord: {
      return declareDedicatedBuiltIn(builder, regType,
        ir::BasicType(ir::ScalarType::eF32, 3u), ir::BuiltIn::eTessCoord, "SV_DomainLocation");
    }

    case RegisterType::eThreadId: {
      return declareDedicatedBuiltIn(builder, regType,
        ir::BasicType(ir::ScalarType::eU32, 3u), ir::BuiltIn::eGlobalThreadId, "SV_DispatchThreadID");
    }

    case RegisterType::eThreadGroupId: {
      return declareDedicatedBuiltIn(builder, regType,
        ir::BasicType(ir::ScalarType::eU32, 3u), ir::BuiltIn::eWorkgroupId, "SV_GroupID");
    }

    case RegisterType::eThreadIdInGroup: {
      return declareDedicatedBuiltIn(builder, regType,
        ir::BasicType(ir::ScalarType::eU32, 3u), ir::BuiltIn::eLocalThreadId, "SV_GroupThreadID");
    }

    case RegisterType::eThreadIndexInGroup: {
      return declareDedicatedBuiltIn(builder, regType,
        ir::ScalarType::eU32, ir::BuiltIn::eLocalThreadIndex, "SV_GroupIndex");
    }

    case RegisterType::eGsInstanceId: {
      return declareDedicatedBuiltIn(builder, regType,
        ir::ScalarType::eU32, ir::BuiltIn::eGsInstanceId, "SV_GSInstanceID");
    }

    case RegisterType::eStencilRef: {
      return declareDedicatedBuiltIn(builder, regType,
        ir::ScalarType::eU32, ir::BuiltIn::eStencilRef, "SV_StencilRef");
    }

    case RegisterType::eInnerCoverage: {
      return declareDedicatedBuiltIn(builder, regType,
        ir::ScalarType::eBool, ir::BuiltIn::eIsFullyCovered, "SV_InnerCoverage");
    }

    case RegisterType::eCycleCounter:
      /* Unsupported */
      break;

    case RegisterType::eTemp:
    case RegisterType::eInput:
    case RegisterType::eOutput:
    case RegisterType::eIndexableTemp:
    case RegisterType::eImm32:
    case RegisterType::eImm64:
    case RegisterType::eSampler:
    case RegisterType::eResource:
    case RegisterType::eCbv:
    case RegisterType::eIcb:
    case RegisterType::eLabel:
    case RegisterType::eNull:
    case RegisterType::eRasterizer:
    case RegisterType::eStream:
    case RegisterType::eFunctionBody:
    case RegisterType::eFunctionTable:
    case RegisterType::eInterface:
    case RegisterType::eFunctionInput:
    case RegisterType::eFunctionOutput:
    case RegisterType::eForkInstanceId:
    case RegisterType::eJoinInstanceId:
    case RegisterType::eControlPointIn:
    case RegisterType::eControlPointOut:
    case RegisterType::ePatchConstant:
    case RegisterType::eThis:
    case RegisterType::eUav:
    case RegisterType::eTgsm:
      /* Invalid */
      break;
  }

  Logger::err("Unhandled built-in register type: ", m_converter.makeRegisterDebugName(regType, 0u, WriteMask()));
  return false;
}


bool IoMap::declareIoRegisters(ir::Builder& builder, const Instruction& op, RegisterType regType) {
  const auto& operand = op.getDst(0u);

  uint32_t indexCount = operand.getIndexDimensions();

  if (indexCount < 1u || indexCount > 2u)
    return m_converter.logOpError(op, "Invalid index dimension for I/O variable: ", indexCount);

  /* For arrayed inputs, the register index is encoded in the second
   * index for some reason, the first index encodes the array size. */
  uint32_t regIndex = operand.getIndex(indexCount - 1u);
  uint32_t arraySize = 0u;

  if (indexCount > 1u) {
    arraySize = operand.getIndex(0u);

    if (!arraySize || arraySize > MaxIoArraySize)
      return m_converter.logOpError(op, "Invalid I/O array size: ", arraySize);

    /* Use maximum array size for hull shader inputs since the patch
     * vertex count is not necessarily known at compile time. */
    if (regType == RegisterType::eControlPointIn && m_shaderInfo.getType() == ShaderType::eHull)
      arraySize = MaxIoArraySize;
  } else if (m_converter.m_hs.phase == HullShaderPhase::eControlPoint) {
    /* Control point outputs are non-arrayed in DXBC, so we need to
     * apply the putput control point count manually. */
    if (regType == RegisterType::eControlPointOut)
      arraySize = m_converter.m_hs.controlPointsOut;
  }

  /* Find appropriate signature based on the register type. */
  /* Declare regular I/O variables based on the signature */
  auto signature = selectSignature(regType);

  auto componentMask = operand.getWriteMask();
  auto sv = determineSysval(op);

  auto interpolation = determineInterpolationMode(op);

  bool result = true;

  if (sv == Sysval::eNone || sysvalNeedsMirror(regType, sv)) {
    result = result && declareIoSignatureVars(builder, signature,
      regType, regIndex, arraySize, componentMask, interpolation);
  }

  if (sv != Sysval::eNone && sysvalNeedsBuiltIn(regType, sv)) {
    result = result && declareIoSysval(builder, signature,
      regType, regIndex, arraySize, componentMask, sv, interpolation);
  }

  return result;
}


bool IoMap::declareIoSignatureVars(
        ir::Builder&            builder,
  const Signature*              signature,
        RegisterType            regType,
        uint32_t                regIndex,
        uint32_t                arraySize,
        WriteMask               componentMask,
        ir::InterpolationModes  interpolation) {
  dxbc_spv_assert(signature);

  /* Omit any overlapping declarations. This is relevant for hull shaders,
   * where partial declarations are common in fork/join phases. */
  for (const auto& e : m_ioVarMappings) {
    if (e.matches(regType, regIndex, componentMask) && e.sv == Sysval::eNone) {
      componentMask -= e.componentMask;

      if (!componentMask)
        return true;
    }
  }

  /* Determine declaration op to use */
  bool isInput = isInputRegister(regType);

  auto opCode = isInput
    ? ir::OpCode::eDclInput
    : ir::OpCode::eDclOutput;

  /* Determine I/O location */
  uint32_t locationIndex = mapLocation(regType, regIndex);

  /* Find and declare variables for all signature entries matching the given
   * declaration. If no signature entry is present, assume a float vec4. */
  auto entries = signature->filter([=] (const SignatureEntry& e) {
    return (e.getRegisterIndex() == int32_t(regIndex)) &&
           (e.getComponentMask() & componentMask);
  });

  for (auto e = entries; e != signature->end(); e++) {
    /* Create declaration instructio */
    ir::Type type = e->getVectorType();

    if (arraySize)
      type.addArrayDimension(arraySize);

    auto declaration = ir::Op(opCode, type)
      .addOperand(m_converter.getEntryPoint())
      .addOperand(locationIndex)
      .addOperand(e->computeComponentIndex());

    if (!type.getBaseType(0u).isFloatType())
      interpolation = ir::InterpolationMode::eFlat;

    addDeclarationArgs(declaration, regType, interpolation);

    /* Add mapping entry to the look-up table */
    auto& mapping = m_ioVarMappings.emplace_back();
    mapping.registerType = regType;
    mapping.rangeStart = regIndex;
    mapping.rangeEnd = regIndex + 1u;
    mapping.sv = Sysval::eNone;
    mapping.componentMask = e->getComponentMask();
    mapping.baseDef = builder.add(std::move(declaration));
    mapping.baseIndex = type.getBaseType(0u).isVector() ? 0 : -1;

    emitSemanticName(builder, mapping.baseDef, *e);
    emitDebugName(builder, mapping.baseDef, regType, regIndex, mapping.componentMask);

    componentMask -= mapping.componentMask;
  }

  if (componentMask) {
    auto name = m_converter.makeRegisterDebugName(regType, regIndex, componentMask);
    Logger::warn("Register ", name, " not declared in signature.");

    while (componentMask) {
      WriteMask nextMask = extractConsecutiveComponents(componentMask);

      /* We have no type information. Declare variable as
       * float32 for interpolation purposes */
      ir::Type type(ir::ScalarType::eF32, util::popcnt(uint8_t(nextMask)));

      if (arraySize)
        type.addArrayDimension(arraySize);

      auto declaration = ir::Op(opCode, type)
        .addOperand(m_converter.getEntryPoint())
        .addOperand(locationIndex)
        .addOperand(util::tzcnt(uint8_t(nextMask)));

      addDeclarationArgs(declaration, regType, interpolation);

      /* Add mapping entry to the look-up table */
      auto& mapping = m_ioVarMappings.emplace_back();
      mapping.registerType = regType;
      mapping.rangeStart = regIndex;
      mapping.rangeEnd = regIndex + 1u;
      mapping.sv = Sysval::eNone;
      mapping.componentMask = nextMask;
      mapping.baseDef = builder.add(std::move(declaration));
      mapping.baseIndex = type.getBaseType(0u).isVector() ? 0 : -1;

      emitDebugName(builder, mapping.baseDef, regType, regIndex, mapping.componentMask);

      componentMask -= nextMask;
    }
  }

  return true;
}


bool IoMap::declareIoSysval(
        ir::Builder&            builder,
  const Signature*              signature,
        RegisterType            regType,
        uint32_t                regIndex,
        uint32_t                arraySize,
        WriteMask               componentMask,
        Sysval                  sv,
        ir::InterpolationModes  interpolation) {
  dxbc_spv_assert(signature);

  /* Try to find signature element for the given register */
  auto iter = signature->filter([=] (const SignatureEntry& e) {
    return (e.getRegisterIndex() == int32_t(regIndex)) &&
           (e.getComponentMask() & componentMask);
  });

  auto signatureEntry = iter != signature->end() ? &(*iter) : nullptr;

  if (!signatureEntry) {
    auto name = m_converter.makeRegisterDebugName(regType, regIndex, componentMask);
    Logger::warn("No signature entry for register ", name);
  }

  switch (sv) {
    case Sysval::eNone:
      /* Invalid */
      break;

    case Sysval::ePosition: {
      ir::Type type(ir::ScalarType::eF32, 4u);

      if (arraySize)
        type.addArrayDimension(arraySize);

      return declareSimpleBuiltIn(builder, signatureEntry, regType,
        regIndex, componentMask, sv, type, ir::BuiltIn::ePosition, interpolation);
    }

    case Sysval::eClipDistance:
    case Sysval::eCullDistance: {
      return declareClipCullDistance(builder, signature, regType,
        regIndex, arraySize, componentMask, sv, interpolation);
    }

    case Sysval::eVertexId: {
      return declareSimpleBuiltIn(builder, signatureEntry, regType,
        regIndex, componentMask, sv, ir::ScalarType::eU32, ir::BuiltIn::eVertexId,
        ir::InterpolationMode::eFlat);
    }

    case Sysval::eInstanceId: {
      return declareSimpleBuiltIn(builder, signatureEntry, regType,
        regIndex, componentMask, sv, ir::ScalarType::eU32, ir::BuiltIn::eInstanceId,
        ir::InterpolationMode::eFlat);
    }

    case Sysval::ePrimitiveId: {
      return declareSimpleBuiltIn(builder, signatureEntry, regType,
        regIndex, componentMask, sv, ir::ScalarType::eU32, ir::BuiltIn::ePrimitiveId,
        ir::InterpolationMode::eFlat);
    }

    case Sysval::eRenderTargetId: {
      return declareSimpleBuiltIn(builder, signatureEntry, regType,
        regIndex, componentMask, sv, ir::ScalarType::eU32, ir::BuiltIn::eLayerIndex,
        ir::InterpolationMode::eFlat);
    }

    case Sysval::eViewportId: {
      return declareSimpleBuiltIn(builder, signatureEntry, regType,
        regIndex, componentMask, sv, ir::ScalarType::eU32, ir::BuiltIn::eViewportIndex,
        ir::InterpolationMode::eFlat);
    }

    case Sysval::eIsFrontFace: {
      return declareSimpleBuiltIn(builder, signatureEntry, regType,
        regIndex, componentMask, sv, ir::ScalarType::eBool, ir::BuiltIn::eIsFrontFace,
        ir::InterpolationMode::eFlat);
    }

    case Sysval::eSampleIndex: {
      return declareSimpleBuiltIn(builder, signatureEntry, regType,
        regIndex, componentMask, sv, ir::ScalarType::eU32, ir::BuiltIn::eSampleId,
        ir::InterpolationMode::eFlat);
    }

    case Sysval::eQuadU0EdgeTessFactor:
    case Sysval::eQuadV0EdgeTessFactor:
    case Sysval::eQuadU1EdgeTessFactor:
    case Sysval::eQuadV1EdgeTessFactor:
    case Sysval::eQuadUInsideTessFactor:
    case Sysval::eQuadVInsideTessFactor:
    case Sysval::eTriUEdgeTessFactor:
    case Sysval::eTriVEdgeTessFactor:
    case Sysval::eTriWEdgeTessFactor:
    case Sysval::eTriInsideTessFactor:
    case Sysval::eLineDetailTessFactor:
    case Sysval::eLineDensityTessFactor: {
      return declareTessFactor(builder, signatureEntry,
        regType, regIndex, componentMask, sv);
    }
  }

  Logger::err("Unhandled system value: ", sv);
  return false;
}


bool IoMap::declareSimpleBuiltIn(
        ir::Builder&            builder,
  const SignatureEntry*         signatureEntry,
        RegisterType            regType,
        uint32_t                regIndex,
        WriteMask               componentMask,
        Sysval                  sv,
  const ir::Type&               type,
        ir::BuiltIn             builtIn,
        ir::InterpolationModes  interpolation) {
  auto baseType = type.getBaseType(0u);

  auto opCode = isInputRegister(regType)
    ? ir::OpCode::eDclInputBuiltIn
    : ir::OpCode::eDclOutputBuiltIn;

  auto declaration = ir::Op(opCode, type)
    .addOperand(m_converter.getEntryPoint())
    .addOperand(builtIn);

  addDeclarationArgs(declaration, regType, interpolation);

  auto& mapping = m_ioVarMappings.emplace_back();
  mapping.registerType = regType;
  mapping.rangeStart = regIndex;
  mapping.rangeEnd = regIndex + 1u;
  mapping.sv = sv;
  mapping.componentMask = componentMask;
  mapping.baseDef = builder.add(std::move(declaration));
  mapping.baseIndex = baseType.isVector() ? 0 : -1;

  if (signatureEntry)
    emitSemanticName(builder, mapping.baseDef, *signatureEntry);

  emitDebugName(builder, mapping.baseDef, regType, regIndex, componentMask);
  return true;
}


bool IoMap::declareDedicatedBuiltIn(
        ir::Builder&            builder,
        RegisterType            regType,
  const ir::BasicType&          type,
        ir::BuiltIn             builtIn,
  const char*                   semanticName) {
  /* All dedicated built-ins supported as pixel shader inputs are
   * either integers or booleans, so there is no interpolation */
  auto interpolation = ir::InterpolationMode::eFlat;

  auto opCode = isInputRegister(regType)
    ? ir::OpCode::eDclInputBuiltIn
    : ir::OpCode::eDclOutputBuiltIn;

  auto declaration = ir::Op(opCode, type)
    .addOperand(m_converter.getEntryPoint())
    .addOperand(builtIn);

  addDeclarationArgs(declaration, regType, interpolation);

  /* Add mapping. These registers are assumed to not be indexed. */
  auto& mapping = m_ioVarMappings.emplace_back();
  mapping.registerType = regType;
  mapping.rangeStart = 0u;
  mapping.rangeEnd = 0u;
  mapping.sv = Sysval::eNone;
  mapping.componentMask = makeWriteMaskForComponents(type.getVectorSize());
  mapping.baseDef = builder.add(std::move(declaration));
  mapping.baseIndex = type.isVector() ? 0 : -1;

  if (semanticName)
    builder.add(ir::Op::Semantic(mapping.baseDef, 0u, semanticName));

  return true;
}


bool IoMap::declareClipCullDistance(
        ir::Builder&            builder,
  const Signature*              signature,
        RegisterType            regType,
        uint32_t                regIndex,
        uint32_t                arraySize,
        WriteMask               componentMask,
        Sysval                  sv,
        ir::InterpolationModes  interpolation) {
  /* For the registers that declare clip/cull distances, figure out
   * which components contribute, and determine the first component
   * of the current declaration. */
  uint32_t elementMask = 0u;
  uint32_t elementShift = -1u;

  auto entries = signature->filter([sv, cStream = m_gsStream] (const SignatureEntry& e) {
    auto signatureSv = (sv == Sysval::eClipDistance)
      ? SignatureSysval::eClipDistance
      : SignatureSysval::eCullDistance;

    return e.getStreamIndex() == cStream &&
           e.getSystemValue() == signatureSv;
  });

  for (auto e = entries; e != signature->end(); e++) {
    uint32_t shift = 4u * e->getSemanticIndex();
    elementMask |= uint32_t(uint8_t(e->getComponentMask())) << shift;

    if ((e->getRegisterIndex() == int32_t(regIndex)) && (e->getComponentMask() & componentMask))
      elementShift = shift;
  }

  /* Either no clip distances are declared properly at all, or some are
   * missing from the signature. Pray that the app only uses one register. */
  if (elementShift >= 32u) {
    auto name = m_converter.makeRegisterDebugName(regType, regIndex, componentMask);
    Logger::warn("Clip/Cull distance entries for ", name, " missing from signature.");

    elementMask = uint8_t(componentMask);
    elementShift = 0u;
  }

  /* Declare actual built-in variable as necessary */
  auto& def = (sv == Sysval::eClipDistance)
    ? m_clipDistance
    : m_cullDistance;

  if (!def) {
    auto type = ir::Type(ir::ScalarType::eF32)
      .addArrayDimension(util::popcnt(elementMask));

    if (arraySize)
      type.addArrayDimension(arraySize);

    auto builtIn = (sv == Sysval::eClipDistance)
      ? ir::BuiltIn::eClipDistance
      : ir::BuiltIn::eCullDistance;

    /* Determine opcode */
    auto opCode = isInputRegister(regType)
      ? ir::OpCode::eDclInputBuiltIn
      : ir::OpCode::eDclOutputBuiltIn;

    auto declaration = ir::Op(opCode, type)
      .addOperand(m_converter.getEntryPoint())
      .addOperand(builtIn);

    addDeclarationArgs(declaration, regType, interpolation);

    def = builder.add(std::move(declaration));

    if (entries != signature->end())
      emitSemanticName(builder, def, *entries);

    emitDebugName(builder, def, regType, regIndex, componentMask);
  }

  /* Create mapping entries. We actually need to create one entry for each
   * array element since the load/store logic may otherwise assume vectors. */
  uint32_t elementIndex = util::popcnt(util::bextract(elementMask, 0u, elementShift));

  for (auto component : componentMask) {
    auto& mapping = m_ioVarMappings.emplace_back();
    mapping.registerType = regType;
    mapping.rangeStart = regIndex;
    mapping.rangeEnd = regIndex + 1u;
    mapping.sv = sv;
    mapping.componentMask = component;
    mapping.baseDef = def;
    mapping.baseIndex = elementIndex;
  }

  return true;
}


bool IoMap::declareTessFactor(
        ir::Builder&            builder,
  const SignatureEntry*         signatureEntry,
        RegisterType            regType,
        uint32_t                regIndex,
        WriteMask               componentMask,
        Sysval                  sv) {
  /* Map the system value to something we can work with */
  bool isInsideFactor = sv == Sysval::eQuadUInsideTessFactor ||
                        sv == Sysval::eQuadVInsideTessFactor ||
                        sv == Sysval::eTriInsideTessFactor;

  auto arrayIndex = [sv] {
    switch (sv) {
      case Sysval::eQuadU0EdgeTessFactor:   return 0u;
      case Sysval::eQuadV0EdgeTessFactor:   return 1u;
      case Sysval::eQuadU1EdgeTessFactor:   return 2u;
      case Sysval::eQuadV1EdgeTessFactor:   return 3u;
      case Sysval::eQuadUInsideTessFactor:  return 0u;
      case Sysval::eQuadVInsideTessFactor:  return 1u;
      case Sysval::eTriUEdgeTessFactor:     return 0u;
      case Sysval::eTriVEdgeTessFactor:     return 1u;
      case Sysval::eTriWEdgeTessFactor:     return 2u;
      case Sysval::eTriInsideTessFactor:    return 0u;
      case Sysval::eLineDetailTessFactor:   return 1u;
      case Sysval::eLineDensityTessFactor:  return 0u;
      default: break;
    }

    dxbc_spv_unreachable();
    return 0u;
  } ();

  if (componentMask != componentMask.first()) {
    auto name = m_converter.makeRegisterDebugName(regType, regIndex, componentMask);
    Logger::err("Tessellation factor ", name, " not declared as scalar.");
    return false;
  }

  /* Declare actual built-in as necessary */
  auto& def = isInsideFactor
    ? m_tessFactorInner
    : m_tessFactorOuter;

  if (!def) {
    auto type = ir::Type(ir::ScalarType::eF32)
      .addArrayDimension(isInsideFactor ? 2u : 4u);

    auto opCode = isInputRegister(regType)
      ? ir::OpCode::eDclInputBuiltIn
      : ir::OpCode::eDclOutputBuiltIn;

    auto builtIn = isInsideFactor
      ? ir::BuiltIn::eTessFactorInner
      : ir::BuiltIn::eTessFactorOuter;

    auto declaration = ir::Op(opCode, type)
      .addOperand(m_converter.getEntryPoint())
      .addOperand(builtIn);

    def = builder.add(std::move(declaration));

    if (signatureEntry)
      emitSemanticName(builder, def, *signatureEntry);

    emitDebugName(builder, def, regType, regIndex, componentMask);
  }

  /* Add mapping */
  auto& mapping = m_ioVarMappings.emplace_back();
  mapping.registerType = regType;
  mapping.rangeStart = regIndex;
  mapping.rangeEnd = regIndex + 1u;
  mapping.sv = sv;
  mapping.componentMask = componentMask;
  mapping.baseDef = def;
  mapping.baseIndex = arrayIndex;

  return true;
}


void IoMap::emitSemanticName(ir::Builder& builder, ir::SsaDef def, const SignatureEntry& entry) const {
  builder.add(ir::Op::Semantic(def, entry.getSemanticIndex(), entry.getSemanticName()));
}


void IoMap::emitDebugName(ir::Builder& builder, ir::SsaDef def, RegisterType type, uint32_t index, WriteMask mask) const {
  if (!m_converter.m_options.includeDebugNames)
    return;

  auto name = m_converter.makeRegisterDebugName(type, index, mask);
  builder.add(ir::Op::DebugName(def, name.c_str()));
}


Sysval IoMap::determineSysval(const Instruction& op) const {
  auto opCode = op.getOpToken().getOpCode();

  bool hasSystemValue = opCode == OpCode::eDclInputSiv ||
                        opCode == OpCode::eDclInputSgv ||
                        opCode == OpCode::eDclInputPsSiv ||
                        opCode == OpCode::eDclInputPsSgv ||
                        opCode == OpCode::eDclOutputSiv ||
                        opCode == OpCode::eDclOutputSgv;

  if (!hasSystemValue)
    return Sysval::eNone;

  dxbc_spv_assert(op.getImmCount());
  return op.getImm(0u).getImmediate<Sysval>(0u);
}


ir::InterpolationModes IoMap::determineInterpolationMode(const Instruction& op) const {
  auto opCode = op.getOpToken().getOpCode();

  /* Assume basic linear interpolation for float types */
  bool hasInterpolation = opCode == OpCode::eDclInputPs ||
                          opCode == OpCode::eDclInputPsSiv ||
                          opCode == OpCode::eDclInputPsSgv;

  if (!hasInterpolation)
    return ir::InterpolationModes();

  /* Decode and translate interpolation mode flags from the opcode token */
  auto result = resolveInterpolationMode(op.getOpToken().getInterpolationMode());

  if (!result) {
    m_converter.logOpMessage(LogLevel::eWarn, op, "Unhandled interpolation mode.");
    return ir::InterpolationModes();
  }

  return *result;
}


RegisterType IoMap::normalizeRegisterType(RegisterType regType) const {
  /* Outside of hull shaders, I/O mapping is actually well-defined */
  if (m_shaderInfo.getType() != ShaderType::eHull)
    return regType;

  switch (regType) {
    case RegisterType::eOutput: {
      auto hsPhase = m_converter.m_hs.phase;

      return (hsPhase == HullShaderPhase::eControlPoint)
        ? RegisterType::eControlPointOut
        : RegisterType::ePatchConstant;
    }

    case RegisterType::eInput:
      return RegisterType::eControlPointIn;

    default:
      return regType;
  }
}


bool IoMap::sysvalNeedsMirror(RegisterType regType, Sysval sv) const {
  bool isInput = isInputRegister(regType);

  switch (sv) {
    case Sysval::eRenderTargetId:
    case Sysval::eViewportId: {
      /* These can only be read directly by the pixel shader, but can be
       * exported by various stages that may attempt to forward them.
       * Emit both the built-in and a mirror variable in that case. */
      return isInput
        ? m_shaderInfo.getType() != ShaderType::ePixel
        : m_shaderInfo.getType() != ShaderType::eGeometry;
    }

    default:
      /* For most system values, we only need a regular back-up variable
       * if we can't actually use the built-in declaration. */
      return !sysvalNeedsBuiltIn(regType, sv);
  }
}


bool IoMap::sysvalNeedsBuiltIn(RegisterType regType, Sysval sv) const {
  bool isInput = isInputRegister(regType);

  switch (sv) {
    case Sysval::ePosition:
    case Sysval::eClipDistance:
    case Sysval::eCullDistance: {
      return isInput
        ? m_shaderInfo.getType() != ShaderType::eDomain
        : m_shaderInfo.getType() != ShaderType::eHull;
    }

    case Sysval::eRenderTargetId:
    case Sysval::eViewportId: {
      return isInput
        ? m_shaderInfo.getType() == ShaderType::ePixel
        : m_shaderInfo.getType() != ShaderType::eHull;
    }

    case Sysval::eIsFrontFace:
    case Sysval::eSampleIndex:
      return isInput && m_shaderInfo.getType() == ShaderType::ePixel;

    case Sysval::eVertexId:
    case Sysval::eInstanceId:
      return isInput && m_shaderInfo.getType() == ShaderType::eVertex;

    case Sysval::ePrimitiveId: {
      if (isInput)
        return m_shaderInfo.getType() != ShaderType::eVertex;

      /* Special-case stages that can override the primitive ID */
      return m_shaderInfo.getType() == ShaderType::eDomain ||
             m_shaderInfo.getType() == ShaderType::eGeometry;
    }

    case Sysval::eQuadU0EdgeTessFactor:
    case Sysval::eQuadV0EdgeTessFactor:
    case Sysval::eQuadU1EdgeTessFactor:
    case Sysval::eQuadV1EdgeTessFactor:
    case Sysval::eQuadUInsideTessFactor:
    case Sysval::eQuadVInsideTessFactor:
    case Sysval::eTriUEdgeTessFactor:
    case Sysval::eTriVEdgeTessFactor:
    case Sysval::eTriWEdgeTessFactor:
    case Sysval::eTriInsideTessFactor:
    case Sysval::eLineDetailTessFactor:
    case Sysval::eLineDensityTessFactor: {
      return isInput
        ? m_shaderInfo.getType() == ShaderType::eDomain
        : m_shaderInfo.getType() == ShaderType::eHull;
    }

    case Sysval::eNone:
      /* Invalid */
      break;
  }

  Logger::err("Unhandled system value: ", sv);
  return false;
}


ir::Op& IoMap::addDeclarationArgs(ir::Op& declaration, RegisterType type, ir::InterpolationModes interpolation) const {
  auto isInput = isInputRegister(type);

  if (!isInput && m_shaderInfo.getType() == ShaderType::eGeometry)
    declaration.addOperand(m_gsStream);

  if (isInput && m_shaderInfo.getType() == ShaderType::ePixel)
    declaration.addOperand(interpolation);

  return declaration;
}


bool IoMap::isInputRegister(RegisterType type) const {
  switch (type) {
    case RegisterType::eInput:
    case RegisterType::ePrimitiveId:
    case RegisterType::eControlPointId:
    case RegisterType::eControlPointIn:
    case RegisterType::eTessCoord:
    case RegisterType::eThreadId:
    case RegisterType::eThreadGroupId:
    case RegisterType::eThreadIdInGroup:
    case RegisterType::eCoverageIn:
    case RegisterType::eGsInstanceId:
    case RegisterType::eThreadIndexInGroup:
    case RegisterType::eInnerCoverage:
      return true;

    case RegisterType::ePatchConstant:
      return m_shaderInfo.getType() == ShaderType::eDomain;

    default:
      return false;
  }
}


const Signature* IoMap::selectSignature(RegisterType type) const {
  switch (type) {
    case RegisterType::ePatchConstant:
      return &m_psgn;

    case RegisterType::eInput:
    case RegisterType::eControlPointIn:
      return &m_isgn;

    case RegisterType::eOutput:
    case RegisterType::eControlPointOut:
      return &m_osgn;

    default:
      return nullptr;
  }
}


uint32_t IoMap::mapLocation(RegisterType regType, uint32_t regIndex) const {
  if (!m_converter.m_options.uniqueIoLocations)
    return regIndex;

  if (regType == RegisterType::ePatchConstant) {
    const auto& signature = m_shaderInfo.getType() == ShaderType::eHull
      ? m_osgn
      : m_isgn;

    uint32_t index = 0u;

    for (const auto& e : signature)
      index = std::max(e.getRegisterIndex() + 1u, index);

    return index + regIndex;
  }

  return regIndex;
}


bool IoMap::initSignature(Signature& sig, util::ByteReader reader) {
  /* Chunk not present, this is fine */
  if (!reader)
    return true;

  if (!(sig = Signature(reader))) {
    Logger::err("Failed to parse signature chunk.");
    return false;
  }

  return true;
}


bool IoMap::isRegularIoRegister(RegisterType type) {
  return type == RegisterType::eInput ||
         type == RegisterType::eOutput ||
         type == RegisterType::eControlPointIn ||
         type == RegisterType::eControlPointOut ||
         type == RegisterType::ePatchConstant;
}

}
