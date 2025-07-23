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


bool IoMap::handleHsPhase() {
  /* Nuke index ranges here since they are declared per phase.
   * This is particularly necessary for writable ranges, which
   * are commonly used for tess factors. */
  m_indexRanges.clear();
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

  /* If this declaration declares a built-in register, emit it
   * directly, we don't need to do anything else here. */
  if (!isRegularIoRegister(regType))
    return declareIoBuiltIn(builder, regType);

  return declareIoRegisters(builder, op, regType);
}


bool IoMap::handleDclIndexRange(ir::Builder& builder, const Instruction& op) {
  /* Find the register that the range is being declared for */
  dxbc_spv_assert(op.getDstCount() && op.getImmCount());
  const auto& operand = op.getDst(0u);

  /* Look at the non-normalized register type first since writable,
   * indexed registers are always declared as regular outputs. */
  bool isOutput = operand.getRegisterType() == RegisterType::eOutput;

  auto& mapping = m_indexRanges.emplace_back();
  mapping.regType = normalizeRegisterType(operand.getRegisterType());
  mapping.regIndex = operand.getIndex(operand.getIndexDimensions() - 1u);
  mapping.regCount = op.getImm(0u).getImmediate<uint32_t>(0u);
  mapping.componentMask = operand.getWriteMask();
  mapping.baseIndex = 0u;

  /* Trust the declared array size, we can come up with a way to
   * fix this for broken games if it becomes an issue. */
  uint32_t vertexCount = 0u;

  if (operand.getIndexDimensions() > 1u)
    vertexCount = operand.getIndex(0u);

  if (isOutput) {
    if (vertexCount)
      return m_converter.logOpError(op, "Output range declared as per-vertex array.");

    std::tie(mapping.baseType, mapping.baseDef) =
      emitDynamicStoreFunction(builder, mapping, vertexCount);
  } else {
    std::tie(mapping.baseType, mapping.baseDef) =
      emitDynamicLoadFunction(builder, mapping, vertexCount);
  }

  return bool(mapping.baseDef);
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


ir::SsaDef IoMap::determineIncomingVertexCount(ir::Builder& builder, uint32_t maxArraySize) {
  if (!maxArraySize)
    return ir::SsaDef();

  /* Otherwise, use the array declaration and if applicable,
   * the actual incoming vertex count */
  auto maxCount = builder.makeConstant(maxArraySize);

  if (!m_vertexCountIn) {
    auto builtIn = m_shaderInfo.getType() == ShaderType::eGeometry
      ? ir::BuiltIn::eGsVertexCountIn
      : ir::BuiltIn::eTessControlPointCountIn;

    m_vertexCountIn = builder.add(ir::Op::DclInputBuiltIn(ir::ScalarType::eU32, m_converter.getEntryPoint(), builtIn));

    if (m_converter.m_options.includeDebugNames)
      builder.add(ir::Op::DebugName(m_vertexCountIn, "vVertexCountIn"));
  }

  return builder.add(ir::Op::UMin(ir::ScalarType::eU32, maxCount,
    builder.add(ir::Op::InputLoad(ir::ScalarType::eU32, m_vertexCountIn, ir::SsaDef()))));;
}


bool IoMap::emitHsControlPointPhasePassthrough(ir::Builder& builder) {
  /* The control point count must match, or a pass-through wouldn't make much sense */
  if (m_converter.m_hs.controlPointsIn != m_converter.m_hs.controlPointsOut) {
    Logger::err("Input control point count ", m_converter.m_hs.controlPointsIn,
      " does not match output control point count ", m_converter.m_hs.controlPointsOut);
    return false;
  }

  /* Handle input declarations. Since some of these may be built-in vertex
   * shader outputs, we need to be careful with system values. */
  for (const auto& e : m_isgn) {
    auto sv = resolveSignatureSysval(e.getSystemValue(), e.getSemanticIndex());

    if (!sv) {
      Logger::err("Unhandled system value semantic ", e.getSemanticName(), e.getSemanticIndex());
      return false;
    }

    bool result = true;

    if (sv != Sysval::eNone && sysvalNeedsBuiltIn(RegisterType::eControlPointIn, *sv)) {
      result = declareIoSysval(builder, &m_isgn, RegisterType::eControlPointIn,
        e.getRegisterIndex(), m_converter.m_hs.controlPointsIn, e.getComponentMask(),
        *sv, ir::InterpolationModes());
    } else {
      result = declareIoSignatureVars(builder, &m_isgn, RegisterType::eControlPointIn,
        e.getRegisterIndex(), m_converter.m_hs.controlPointsIn, e.getComponentMask(),
        ir::InterpolationModes());
    }

    if (!result)
      return result;
  }

  /* Handle output declarations. This is easier since control point outputs
   * cannot be built-ins, so we can ignore system values. */
  for (const auto& e : m_osgn) {
    bool result = declareIoSignatureVars(builder, &m_osgn, RegisterType::eControlPointOut,
      e.getRegisterIndex(), m_converter.m_hs.controlPointsOut, e.getComponentMask(),
      ir::InterpolationModes());

    if (!result)
      return false;
  }

  /* Declare and load control point ID, which we need for indexing purposes */
  auto controlPointId = loadTessControlPointId(builder);

  if (!controlPointId) {
    Logger::err("Failed to emit control point pass-through function.");
    return false;
  }

  /* Emit loads and stores */
  for (const auto& e : m_osgn) {
    auto value = loadIoRegister(builder, e.getScalarType(),
      RegisterType::eControlPointIn, controlPointId, ir::SsaDef(),
      e.getRegisterIndex(), Swizzle::identity(), e.getComponentMask());

    if (!value) {
      Logger::err("Failed to emit control point pass-through function.");
      return false;
    }

    if (!storeIoRegister(builder, RegisterType::eControlPointOut,
        controlPointId, ir::SsaDef(), e.getRegisterIndex(), e.getComponentMask(), value))
      return false;
  }

  return true;
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
        ir::ScalarType::eU32, ir::BuiltIn::eTessControlPointId, "SV_OutputControlPointID");
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
  for (const auto& e : m_variables) {
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
    auto& mapping = m_variables.emplace_back();
    mapping.regType = regType;
    mapping.regIndex = regIndex;
    mapping.regCount = 1u;
    mapping.sv = Sysval::eNone;
    mapping.componentMask = e->getComponentMask();
    mapping.baseType = declaration.getType();
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
      auto& mapping = m_variables.emplace_back();
      mapping.regType = regType;
      mapping.regIndex = regIndex;
      mapping.regCount = 1u;
      mapping.sv = Sysval::eNone;
      mapping.componentMask = nextMask;
      mapping.baseType = declaration.getType();
      mapping.baseDef = builder.add(std::move(declaration));
      mapping.baseIndex = -1;

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
  auto opCode = isInputRegister(regType)
    ? ir::OpCode::eDclInputBuiltIn
    : ir::OpCode::eDclOutputBuiltIn;

  auto declaration = ir::Op(opCode, type)
    .addOperand(m_converter.getEntryPoint())
    .addOperand(builtIn);

  addDeclarationArgs(declaration, regType, interpolation);

  auto& mapping = m_variables.emplace_back();
  mapping.regType = regType;
  mapping.regIndex = regIndex;
  mapping.regCount = 1u;
  mapping.sv = sv;
  mapping.componentMask = componentMask;
  mapping.baseType = declaration.getType();
  mapping.baseDef = builder.add(std::move(declaration));
  mapping.baseIndex = -1;

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
  auto& mapping = m_variables.emplace_back();
  mapping.regType = regType;
  mapping.regIndex = 0u;
  mapping.regCount = 1u;
  mapping.sv = Sysval::eNone;
  mapping.componentMask = makeWriteMaskForComponents(type.getVectorSize());
  mapping.baseType = declaration.getType();
  mapping.baseDef = builder.add(std::move(declaration));
  mapping.baseIndex = -1;

  if (semanticName)
    builder.add(ir::Op::Semantic(mapping.baseDef, 0u, semanticName));

  emitDebugName(builder, mapping.baseDef, regType, 0u, mapping.componentMask);
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
    auto& mapping = m_variables.emplace_back();
    mapping.regType = regType;
    mapping.regIndex = regIndex;
    mapping.regCount = 1u;
    mapping.sv = sv;
    mapping.componentMask = component;
    mapping.baseType = builder.getOp(def).getType();
    mapping.baseDef = def;
    mapping.baseIndex = elementIndex++;
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
  auto& mapping = m_variables.emplace_back();
  mapping.regType = regType;
  mapping.regIndex = regIndex;
  mapping.regCount = 1u;
  mapping.sv = sv;
  mapping.componentMask = componentMask;
  mapping.baseType = builder.getOp(def).getType();
  mapping.baseDef = def;
  mapping.baseIndex = arrayIndex;

  return true;
}


ir::SsaDef IoMap::loadTessControlPointId(ir::Builder& builder) {
  if (!declareDedicatedBuiltIn(builder, RegisterType::eControlPointId,
      ir::ScalarType::eU32, ir::BuiltIn::eTessControlPointId, "SV_OutputControlPointID"))
    return ir::SsaDef();

  return loadIoRegister(builder, ir::ScalarType::eU32, RegisterType::eControlPointId,
    ir::SsaDef(), ir::SsaDef(), 0u, Swizzle(Component::eX), ComponentBit::eX);
}


ir::SsaDef IoMap::loadIoRegister(
        ir::Builder&            builder,
        ir::ScalarType          scalarType,
        RegisterType            regType,
        ir::SsaDef              vertexIndex,
        ir::SsaDef              regIndexRelative,
        uint32_t                regIndexAbsolute,
        Swizzle                 swizzle,
        WriteMask               writeMask) {
  auto returnType = m_converter.makeVectorType(scalarType, writeMask);

  std::array<ir::SsaDef, 4u> components = { };

  for (auto c : swizzle.getReadMask(writeMask)) {
    auto componentIndex = uint8_t(componentFromBit(c));

    const auto& list = regIndexRelative ? m_indexRanges : m_variables;
    const auto* var = findIoVar(list, regType, regIndexAbsolute, c);

    if (!var) {
      /* Shouldn't happen, but some custom DXBC emitters produce broken shaders */
      auto name = m_converter.makeRegisterDebugName(regType, regIndexAbsolute, c);
      Logger::warn("I/O variable ", name, " not found (dynamically indexed: ", regIndexRelative ? "yes" : "no", ").");

      components[componentIndex] = builder.add(ir::Op::Undef(scalarType));
    } else {
      /* Get basic type of the underlying variable */
      auto varOpCode = builder.getOp(var->baseDef).getOpCode();
      auto varScalarType = var->baseType.getBaseType(0u).getBaseType();

      /* Compute address vector for register */
      auto addressDef = computeRegisterAddress(builder, *var,
        vertexIndex, regIndexRelative, regIndexAbsolute, c);

      /* Emit actual load op depending on the variable type */
      auto opCode = [varOpCode] {
        switch (varOpCode) {
          case ir::OpCode::eDclInput:
          case ir::OpCode::eDclInputBuiltIn:
            return ir::OpCode::eInputLoad;

          case ir::OpCode::eDclOutput:
          case ir::OpCode::eDclOutputBuiltIn:
            return ir::OpCode::eOutputLoad;

          case ir::OpCode::eDclScratch:
            return ir::OpCode::eScratchLoad;

          default:
            return ir::OpCode::eUnknown;
        }
      } ();

      if (opCode == ir::OpCode::eUnknown) {
        auto name = m_converter.makeRegisterDebugName(regType, regIndexAbsolute, c);

        Logger::err("Failed to load I/O register ", name, ": Unhandled base op ", varOpCode);
        return ir::SsaDef();
      }

      auto scalar = builder.add(ir::Op(opCode, varScalarType)
        .addOperand(var->baseDef)
        .addOperand(addressDef));

      /* Convert to expected type if it doesn't match */
      if (varScalarType != scalarType)
        scalar = builder.add(ir::Op::ConsumeAs(scalarType, scalar));

      components[componentIndex] = scalar;
    }
  }

  return m_converter.composite(builder, returnType, components.data(), swizzle, writeMask);
}


bool IoMap::storeIoRegister(
        ir::Builder&            builder,
        RegisterType            regType,
        ir::SsaDef              vertexIndex,
        ir::SsaDef              regIndexRelative,
        uint32_t                regIndexAbsolute,
        WriteMask               writeMask,
        ir::SsaDef              value) {
  const auto& valueDef = builder.getOp(value);
  auto valueType = valueDef.getType();

  dxbc_spv_assert(valueType.isBasicType());

  /* Write each component individually */
  uint32_t componentIndex = 0u;

  for (auto c : writeMask) {
    /* Extract scalar to store */
    ir::SsaDef baseScalar = m_converter.extractFromVector(builder, value, componentIndex++);

    /* There may be multiple output variables for the same value. Iterate
     * over all matching vars and duplicate the stores as necessary. */
    const auto& list = regIndexRelative ? m_indexRanges : m_variables;

    for (const auto& var : list) {
      if (!var.matches(regType, regIndexAbsolute, c))
        continue;

      /* Convert scalar to required type */
      bool isFunction = builder.getOp(var.baseDef).getOpCode() == ir::OpCode::eFunction;

      auto srcScalarType = valueType.getBaseType(0u).getBaseType();
      auto dstScalarType = var.baseType.getBaseType(0u).getBaseType();

      auto scalar = baseScalar;

      if (srcScalarType != dstScalarType)
        scalar = builder.add(ir::Op::ConsumeAs(dstScalarType, scalar));

      /* Compute address vector for register */
      auto addressDef = computeRegisterAddress(builder, var,
        vertexIndex, regIndexRelative, regIndexAbsolute, c);

      if (!isFunction) {
        /* Emit plain output store if possible */
        builder.add(ir::Op::OutputStore(var.baseDef, addressDef, scalar));
      } else {
        /* Unroll address vector and emit function call. */
        const auto& addressOp = builder.getOp(addressDef);
        auto callOp = ir::Op::FunctionCall(ir::Type(), var.baseDef);

        for (uint32_t i = 0u; i < addressOp.getType().getBaseType(0u).getVectorSize(); i++)
          callOp.addParam(m_converter.extractFromVector(builder, value, i));

        callOp.addParam(scalar);

        builder.add(std::move(callOp));
      }
    }
  }

  return true;
}


ir::SsaDef IoMap::computeRegisterAddress(
        ir::Builder&            builder,
  const IoVarInfo&              var,
        ir::SsaDef              vertexIndex,
        ir::SsaDef              regIndexRelative,
        uint32_t                regIndexAbsolute,
        WriteMask               component) {
  /* Get type info for underlying variable */
  auto varType = var.baseType;

  if (vertexIndex)
    varType = varType.getSubType(0u);

  util::small_vector<ir::SsaDef, 3u> address;

  if (vertexIndex)
    address.push_back(vertexIndex);

  /* Index into register array if applicable */
  dxbc_spv_assert(var.baseIndex >= 0 || !varType.isArrayType());

  if (varType.isArrayType()) {
    uint32_t absoluteIndex = var.baseIndex + regIndexAbsolute - var.regIndex;
    ir::SsaDef elementIndex = regIndexRelative;

    if (elementIndex && absoluteIndex) {
      elementIndex = builder.add(ir::Op::IAdd(ir::ScalarType::eU32,
        elementIndex, builder.makeConstant(absoluteIndex)));
    } else if (!elementIndex) {
      elementIndex = builder.makeConstant(absoluteIndex);
    }

    address.push_back(elementIndex);
  }

  /* Index into vector components if applicable */
  auto varBaseType = varType.getBaseType(0u);

  if (varBaseType.isVector() && component) {
    uint32_t componentIndex = util::tzcnt(uint8_t(component.first())) - util::tzcnt(uint8_t(var.componentMask));
    address.push_back(builder.makeConstant(componentIndex));
  }

  return m_converter.buildVector(builder, ir::ScalarType::eU32, address.size(), address.data());
}


std::pair<ir::Type, ir::SsaDef> IoMap::emitDynamicLoadFunction(
        ir::Builder&            builder,
  const IoVarInfo&              var,
        uint32_t                vertexCount) {
  auto codeLocation = getCurrentFunction();
  auto scalarType = getIndexedBaseType(var);

  /* Declare scratch array */
  auto vectorType = m_converter.makeVectorType(scalarType, var.componentMask);
  auto scratchType = ir::Type(vectorType).addArrayDimension(var.regCount);

  if (vertexCount)
    scratchType.addArrayDimension(vertexCount);

  auto scratch = builder.add(ir::Op::DclScratch(scratchType, m_converter.getEntryPoint()));

  if (m_converter.m_options.includeDebugNames) {
    auto scratchName = m_converter.makeRegisterDebugName(var.regType, var.regIndex, var.componentMask) + std::string("_indexed");
    builder.add(ir::Op::DebugName(scratch, scratchName.c_str()));
  }

  /* Start building function */
  auto function = builder.addBefore(codeLocation, ir::Op::Function(ir::Type()));
  auto cursor = builder.setCursor(function);

  if (m_converter.m_options.includeDebugNames) {
    auto functionName = std::string("load_range_") +
      m_converter.makeRegisterDebugName(var.regType, var.regIndex, var.componentMask);
    builder.add(ir::Op::DebugName(function, functionName.c_str()));
  }

  /* In geometry and tessellation shaders, iterate over vertices */
  auto vertexCountDef = determineIncomingVertexCount(builder, vertexCount);
  auto vertexIndexDef = ir::SsaDef();
  auto vertexIndex = ir::SsaDef();

  if (vertexCountDef) {
    vertexIndexDef = builder.add(ir::Op::DclTmp(ir::ScalarType::eU32, m_converter.getEntryPoint()));
    builder.add(ir::Op::TmpStore(vertexIndexDef, builder.makeConstant(0u)));
    builder.add(ir::Op::ScopedLoop());

    vertexIndex = builder.add(ir::Op::TmpLoad(ir::ScalarType::eU32, vertexIndexDef));
  }

  /* Iterate over matching input variables and copy scalars over one by one */
  for (uint32_t i = 0u; i < var.regCount; i++) {
    uint32_t componentIndex = 0u;

    for (auto component : var.componentMask) {
      auto scalar = ir::SsaDef();
      auto srcVar = findIoVar(m_variables, var.regType, var.regIndex + i, component);

      if (!srcVar || !srcVar->baseDef) {
        scalar = builder.makeUndef(scalarType);
      } else {
        auto loadType = srcVar->baseType.getBaseType(0u).getBaseType();

        auto opCode = isInputRegister(var.regType)
          ? ir::OpCode::eInputLoad
          : ir::OpCode::eOutputLoad;

        auto srcAddress = computeRegisterAddress(builder, *srcVar,
          vertexIndex, ir::SsaDef(), var.regIndex + i, component);

        scalar = builder.add(ir::Op(opCode, loadType)
          .addOperand(srcVar->baseDef)
          .addOperand(srcAddress));

        if (loadType != scalarType)
          scalar = builder.add(ir::Op::ConsumeAs(scalarType, scalar));
      }

      /* Need to build the address manually since the destination
       * variable does not yet have its scratch variable assigned */
      util::small_vector<ir::SsaDef, 3u> dstAddress;

      if (vertexIndex)
        dstAddress.push_back(vertexIndex);

      dstAddress.push_back(builder.makeConstant(i));

      if (vectorType.isVector())
        dstAddress.push_back(builder.makeConstant(componentIndex++));

      auto dstAddressDef = m_converter.buildVector(builder, ir::ScalarType::eU32, dstAddress.size(), dstAddress.data());
      builder.add(ir::Op::ScratchStore(scratch, dstAddressDef, scalar));
    }
  }

  if (vertexCountDef) {
    vertexIndex = builder.add(ir::Op::IAdd(ir::ScalarType::eU32, vertexIndex, builder.makeConstant(1u)));
    builder.add(ir::Op::TmpStore(vertexIndexDef, vertexIndex));

    builder.add(ir::Op::ScopedIf(builder.add(ir::Op::UGe(vertexIndex, vertexCountDef))));
    builder.add(ir::Op::ScopedLoopBreak());
    builder.add(ir::Op::ScopedEndIf());
    builder.add(ir::Op::ScopedEndLoop());
  }

  builder.add(ir::Op::FunctionEnd());

  /* Emit function call at the start of the function */
  builder.setCursor(codeLocation);
  builder.add(ir::Op::FunctionCall(ir::Type(), function));

  if (cursor != codeLocation)
    builder.setCursor(cursor);

  return std::make_pair(scratchType, scratch);
}


std::pair<ir::Type, ir::SsaDef> IoMap::emitDynamicStoreFunction(
        ir::Builder&            builder,
  const IoVarInfo&              var,
        uint32_t                vertexCount) {
  auto codeLocation = getCurrentFunction();
  auto valueType = getIndexedBaseType(var);

  /* Number of vector components */
  auto componentCount = util::popcnt(uint8_t(var.componentMask));
  auto emulatedType = ir::Type(valueType, componentCount).addArrayDimension(var.regCount);

  /* Declare parameters. We may omit the component index if the output is scalar. */
  auto functionOp = ir::Op::Function(ir::Type());

  /* If we are writing tessellation control points, also take the vertex index
   * as a parameter */
  ir::SsaDef paramVertexIndex = { };

  if (m_converter.m_hs.phase == HullShaderPhase::eControlPoint) {
    paramVertexIndex = builder.add(ir::Op::DclParam(ir::ScalarType::eU32));
    functionOp.addParam(paramVertexIndex);

    if (m_converter.m_options.includeDebugNames)
      builder.add(ir::Op::DebugName(paramVertexIndex, "vtx"));

    emulatedType.addArrayDimension(vertexCount);
  }

  ir::SsaDef paramRegIndex = builder.add(ir::Op::DclParam(ir::ScalarType::eU32));
  functionOp.addParam(paramRegIndex);

  if (m_converter.m_options.includeDebugNames)
    builder.add(ir::Op::DebugName(paramRegIndex, "reg"));

  ir::SsaDef paramComponentIndex = { };

  if (componentCount > 1u) {
    paramComponentIndex = builder.add(ir::Op::DclParam(ir::ScalarType::eU32));
    functionOp.addParam(paramComponentIndex);

    if (m_converter.m_options.includeDebugNames)
      builder.add(ir::Op::DebugName(paramComponentIndex, "c"));
  }

  ir::SsaDef paramValue = builder.add(ir::Op::DclParam(valueType));
  functionOp.addParam(paramValue);

  if (m_converter.m_options.includeDebugNames)
    builder.add(ir::Op::DebugName(paramValue, "value"));

  /* Emit function instruction and start building code */
  auto function = builder.addBefore(codeLocation, std::move(functionOp));
  auto cursor = builder.setCursor(function);

  /* If necessary, load the vertex index */
  ir::SsaDef vertexIndex = { };

  if (paramVertexIndex)
    vertexIndex = builder.add(ir::Op::ParamLoad(ir::ScalarType::eU32, function, paramVertexIndex));

  /* Fold register index and component index into a single index for simplicity.
   * This pessimizes code gen, but we realistically should never see this anyway. */
  auto regIndex = builder.add(ir::Op::ParamLoad(ir::ScalarType::eU32, function, paramRegIndex));

  if (paramComponentIndex) {
    auto component = builder.add(ir::Op::ParamLoad(ir::ScalarType::eU32, function, paramComponentIndex));
    regIndex = builder.add(ir::Op::IMul(ir::ScalarType::eU32, regIndex, builder.makeConstant(componentCount)));
    regIndex = builder.add(ir::Op::IAdd(ir::ScalarType::eU32, regIndex, component));
  }

  /* Load value to store */
  auto value = builder.add(ir::Op::ParamLoad(valueType, function, paramValue));

  builder.add(ir::Op::ScopedSwitch(regIndex));

  for (uint32_t i = 0u; i < var.regCount; i++) {
    for (uint32_t j = 0u; j < componentCount; j++) {
      auto component = ComponentBit(uint8_t(var.componentMask.first()) << j);
      auto targetVar = findIoVar(m_variables, var.regType, var.regIndex + i, component);

      if (!targetVar || !targetVar->baseDef)
        continue;

      builder.add(ir::Op::ScopedSwitchCase(componentCount * i + j));

      /* If necessary. convert the incoming value to the target type */
      auto targetType = targetVar->baseType.getBaseType(0u).getBaseType();

      auto scalar = targetType != valueType
        ? builder.add(ir::Op::ConsumeAs(targetType, value))
        : value;

      auto address = computeRegisterAddress(builder, *targetVar,
        vertexIndex, ir::SsaDef(), var.regIndex + i, component);

      builder.add(ir::Op::OutputStore(targetVar->baseDef, address, scalar));
      builder.add(ir::Op::ScopedSwitchBreak());
    }
  }

  builder.add(ir::Op::ScopedEndSwitch());
  builder.add(ir::Op::FunctionEnd());

  if (m_converter.m_options.includeDebugNames) {
    auto functionName = std::string("store_range_") +
      m_converter.makeRegisterDebugName(var.regType, var.regIndex, var.componentMask);
    builder.add(ir::Op::DebugName(function, functionName.c_str()));
  }

  builder.setCursor(cursor);
  return std::make_pair(emulatedType, function);
}


ir::SsaDef IoMap::getCurrentFunction() const {
  if (m_shaderInfo.getType() == ShaderType::eHull)
    return m_converter.m_hs.phaseFunction;

  return m_converter.m_entryPoint.mainFunc;
}


ir::ScalarType IoMap::getIndexedBaseType(
  const IoVarInfo&              var) const {
  /* Use same type as the base I/O variable if possible. If we cannot
   * determine it for whatever reason, use u32 as a fallback. */
  auto baseVar = findIoVar(m_variables, var.regType, var.regIndex, var.componentMask);

  if (!baseVar || !baseVar->baseDef)
    return ir::ScalarType::eU32;

  return baseVar->baseType.getBaseType(0u).getBaseType();
}


const IoVarInfo* IoMap::findIoVar(const IoVarList& list, RegisterType regType, uint32_t regIndex, WriteMask mask) const {
  for (const auto& e : list) {
    if (e.matches(regType, regIndex, mask))
      return &e;
  }

  return nullptr;
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
        ? m_shaderInfo.getType() != ShaderType::eVertex &&
          m_shaderInfo.getType() != ShaderType::eDomain
        : m_shaderInfo.getType() != ShaderType::eHull;
    }

    case Sysval::eRenderTargetId:
    case Sysval::eViewportId: {
      return isInput
        ? m_shaderInfo.getType() == ShaderType::ePixel
        : m_shaderInfo.getType() != ShaderType::eHull &&
          m_shaderInfo.getType() != ShaderType::ePixel;
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
