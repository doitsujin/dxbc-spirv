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


void IoMap::handleHsPhase() {
  /* Nuke index ranges here since they are declared per phase.
   * This is particularly necessary for writable ranges, which
   * are commonly used for tess factors. */
  m_indexRanges.clear();
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

  /* Declare variable as normal and handle output register
   * stream assignments in geometry shaders. */
  bool result = declareIoRegisters(builder, op, regType);

  if (m_shaderInfo.getType() == ShaderType::eGeometry)
    result = result && handleGsOutputStreams(builder);

  return result;
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

  if (vertexCount && isOutput)
    return m_converter.logOpError(op, "Output range declared as per-vertex array.");

  /* As a special case, if the underlying declaration already is an
    * array and the declared range maps perfectly to it, we can address
    * it directly. Common for tessellation factors. */
  auto first = findIoVar(m_variables, mapping.regType, mapping.regIndex, -1, mapping.componentMask);

  if (first && first->baseType.isArrayType() && first->componentMask == mapping.componentMask && !vertexCount) {
    bool match = true;

    for (uint32_t i = 0u; i < mapping.regCount && match; i++) {
      auto var = findIoVar(m_variables, mapping.regType, mapping.regIndex + i, mapping.gsStream, mapping.componentMask);
      match = var && var->baseDef == first->baseDef && var->baseIndex == first->baseIndex + int32_t(i);
    }

    if (match) {
      mapping.baseType = first->baseType;
      mapping.baseDef = first->baseDef;
      mapping.baseIndex = first->baseIndex;
    }
  }

  if (!mapping.baseDef) {
    if (isOutput) {
      /* Emit function that performs per-component stores */
      std::tie(mapping.baseType, mapping.baseDef) =
        emitDynamicStoreFunction(builder, mapping, vertexCount);
    } else {
      /* Copy inputs to scratch array */
      std::tie(mapping.baseType, mapping.baseDef) =
        emitDynamicLoadFunction(builder, mapping, vertexCount);
    }
  }

  return bool(mapping.baseDef);
}


bool IoMap::handleEmitVertex(ir::Builder& builder, uint32_t stream) {
  /* Copy from temporary registers to the actual outputs */
  for (const auto& v : m_variables) {
    if (v.gsStream != int32_t(stream))
      continue;

    auto value = loadIoRegister(builder, ir::ScalarType::eUnknown, v.regType,
      ir::SsaDef(), ir::SsaDef(), v.regIndex, Swizzle::identity(), v.componentMask);

    if (!value)
      return false;

    if (!storeIoRegister(builder, v.regType, ir::SsaDef(), ir::SsaDef(),
        v.regIndex, stream, v.componentMask, value))
      return false;
  }

  /* Invalidate all emitted temporaries. Not *strictly* necessary,
   * but may avoid some inconsistet behaviour with broken app code. */
  for (const auto& v : m_variables) {
    if (v.gsStream != int32_t(stream))
      continue;

    for (auto c : v.componentMask) {
      auto* var = findIoVar(m_variables, RegisterType::eOutput, v.regIndex, -1, c);

      if (var)
        builder.add(ir::Op::TmpStore(var->baseDef, builder.makeUndef(var->baseType)));
    }
  }

  return true;
}


bool IoMap::handleEval(
        ir::Builder&            builder,
  const Instruction&            op) {
  /* Eval* instructions have the following operands:
   * (dst0) Interpolated result
   * (src0) Swizzled input to interpolate
   * (src1) Sample index or offset, if applicable */
  const auto& dst = op.getDst(0u);
  const auto& src = op.getSrc(0u);

  if (m_shaderInfo.getType() != ShaderType::ePixel) {
    m_converter.logOpError(op, "Eval instruction encountered outside of pixel shader.");
    return false;
  }

  auto opCode = [&op] {
    switch (op.getOpToken().getOpCode()) {
      case OpCode::eEvalCentroid:     return ir::OpCode::eInterpolateAtCentroid;
      case OpCode::eEvalSnapped:      return ir::OpCode::eInterpolateAtOffset;
      case OpCode::eEvalSampleIndex:  return ir::OpCode::eInterpolateAtSample;
      default:                        break;
    }

    dxbc_spv_unreachable();
    return ir::OpCode::eUnknown;
  } ();

  /* Load additional argument depending on the opcode */
  ir::SsaDef argument = { };

  if (opCode != ir::OpCode::eInterpolateAtCentroid) {
    const auto& src1 = op.getSrc(1u);

    if (opCode == ir::OpCode::eInterpolateAtOffset) {
      argument = m_converter.loadSrcModified(builder, op, src1,
        ComponentBit::eX | ComponentBit::eY, ir::ScalarType::eI32);

      if (!m_convertEvalOffsetFunction)
        m_convertEvalOffsetFunction = emitConvertEvalOffsetFunction(builder);

      argument = builder.add(ir::Op::FunctionCall(ir::BasicType(ir::ScalarType::eF32, 2u), m_convertEvalOffsetFunction)
        .addParam(builder.add(ir::Op::CompositeExtract(ir::ScalarType::eI32, argument, builder.makeConstant(0u))))
        .addParam(builder.add(ir::Op::CompositeExtract(ir::ScalarType::eI32, argument, builder.makeConstant(1u)))));
    } else if (opCode == ir::OpCode::eInterpolateAtSample) {
      argument = m_converter.loadSrcModified(builder, op, src1,
        ComponentBit::eX, ir::ScalarType::eU32);
    }
  }

  /* Handle actual interpolation */
  auto index = loadRegisterIndices(builder, op, src);
  dxbc_spv_assert(!index.vertexIndex);

  ir::SsaDef result = interpolateIoRegister(builder, opCode, dst.getInfo().type,
    index.regType, index.regIndexRelative, index.regIndexAbsolute,
    src.getSwizzle(), dst.getWriteMask(), argument);

  if (!result) {
    m_converter.logOpError(op, "Failed to process I/O load.");
    return false;
  }

  return m_converter.storeDstModified(builder, op, dst, result);
}


ir::SsaDef IoMap::emitLoad(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand,
        WriteMask               componentMask,
        ir::ScalarType          type) {
  auto index = loadRegisterIndices(builder, op, operand);

  auto result = loadIoRegister(builder, type, index.regType,
    index.vertexIndex, index.regIndexRelative, index.regIndexAbsolute,
    operand.getSwizzle(), componentMask);

  if (!result)
    m_converter.logOpError(op, "Failed to process I/O load.");

  return result;
}


bool IoMap::emitStore(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand,
        ir::SsaDef              value) {
  auto index = loadRegisterIndices(builder, op, operand);

  if (!storeIoRegister(builder, index.regType, index.vertexIndex,
      index.regIndexRelative, index.regIndexAbsolute, -1, operand.getWriteMask(), value))
    return m_converter.logOpError(op, "Failed to process I/O store.");

  return true;
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

    if (!storeIoRegister(builder, RegisterType::eControlPointOut, controlPointId,
        ir::SsaDef(), e.getRegisterIndex(), -1, e.getComponentMask(), value))
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
  auto stream = m_converter.m_gs.streamIndex;

  for (const auto& e : m_variables) {
    if (e.matches(regType, regIndex, stream, componentMask) && e.sv == Sysval::eNone) {
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
           (e.getComponentMask() & componentMask) &&
           (e.getStreamIndex() == stream);
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
        regIndex, ComponentBit::eAll, sv, type, ir::BuiltIn::ePosition, interpolation);
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

  auto stream = m_converter.m_gs.streamIndex;

  auto entries = signature->filter([sv, stream] (const SignatureEntry& e) {
    auto signatureSv = (sv == Sysval::eClipDistance)
      ? SignatureSysval::eClipDistance
      : SignatureSysval::eCullDistance;

    return e.getStreamIndex() == stream &&
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

    auto& list = regIndexRelative ? m_indexRanges : m_variables;
    auto* var = findIoVar(list, regType, regIndexAbsolute, -1, c);

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
          case ir::OpCode::eDclTmp:
            return ir::OpCode::eTmpLoad;

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

      auto loadOp = ir::Op(opCode, varScalarType).addOperand(var->baseDef);

      if (opCode != ir::OpCode::eTmpLoad)
        loadOp.addOperand(addressDef);

      auto scalar = builder.add(std::move(loadOp));

      /* Fix up pixel shader position.w semantics */
      if (m_shaderInfo.getType() == ShaderType::ePixel && var->sv == Sysval::ePosition && c == ComponentBit::eW)
        scalar = builder.add(ir::Op(ir::OpCode::eFRcp, varScalarType).addOperand(scalar));

      components[componentIndex] = convertScalar(builder, scalarType, scalar);
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
        int32_t                 stream,
        WriteMask               writeMask,
        ir::SsaDef              value) {
  dxbc_spv_assert(builder.getOp(value).getType().isBasicType());

  /* Write each component individually */
  uint32_t componentIndex = 0u;

  for (auto c : writeMask) {
    bool foundVar = false;

    /* Extract scalar to store */
    ir::SsaDef baseScalar = m_converter.extractFromVector(builder, value, componentIndex++);

    /* There may be multiple output variables for the same value. Iterate
     * over all matching vars and duplicate the stores as necessary. */
    const auto& list = regIndexRelative ? m_indexRanges : m_variables;

    for (const auto& var : list) {
      if (!var.matches(regType, regIndexAbsolute, stream, c))
        continue;

      /* Convert scalar to required type */
      auto dclOpCode = builder.getOp(var.baseDef).getOpCode();
      bool isFunction = dclOpCode == ir::OpCode::eFunction;

      auto dstScalarType = var.baseType.getBaseType(0u).getBaseType();
      auto scalar = convertScalar(builder, dstScalarType, baseScalar);

      /* Compute address vector for register */
      auto addressDef = computeRegisterAddress(builder, var,
        vertexIndex, regIndexRelative, regIndexAbsolute, c);

      if (!isFunction) {
        /* Emit plain store if possible */
        if (dclOpCode == ir::OpCode::eDclOutput || dclOpCode == ir::OpCode::eDclOutputBuiltIn)
          builder.add(ir::Op::OutputStore(var.baseDef, addressDef, scalar));
        else
          builder.add(ir::Op::TmpStore(var.baseDef, scalar));
      } else {
        /* Unroll address vector and emit function call. */
        const auto& addressOp = builder.getOp(addressDef);
        auto callOp = ir::Op::FunctionCall(ir::Type(), var.baseDef);

        for (uint32_t i = 0u; i < addressOp.getType().getBaseType(0u).getVectorSize(); i++)
          callOp.addParam(m_converter.extractFromVector(builder, value, i));

        callOp.addParam(scalar);

        builder.add(std::move(callOp));
      }

      foundVar = true;
    }

    if (!foundVar) {
      auto name = m_converter.makeRegisterDebugName(regType, regIndexAbsolute, c);
      Logger::warn("No match found for output variable ", name);
    }
  }

  return true;
}


ir::SsaDef IoMap::interpolateIoRegister(
        ir::Builder&            builder,
        ir::OpCode              opCode,
        ir::ScalarType          scalarType,
        RegisterType            regType,
        ir::SsaDef              regIndexRelative,
        uint32_t                regIndexAbsolute,
        Swizzle                 swizzle,
        WriteMask               writeMask,
        ir::SsaDef              argument) {
  std::array<ir::SsaDef, 4u> components = { };
  auto readMask = swizzle.getReadMask(writeMask);

  while (readMask) {
    auto& list = regIndexRelative ? m_indexRanges : m_variables;
    auto* var = findIoVar(list, regType, regIndexAbsolute, -1, readMask.first());

    if (!var) {
      /* Shouldn't happen, but some custom DXBC emitters produce broken shaders */
      auto name = m_converter.makeRegisterDebugName(regType, regIndexAbsolute, readMask);
      Logger::warn("I/O variable ", name, " not found (dynamically indexed: ", regIndexRelative ? "yes" : "no", ").");

      auto componentIndex = uint8_t(componentFromBit(readMask.first()));
      components[componentIndex] = builder.add(ir::Op::Undef(scalarType));
      readMask -= readMask.first();
    } else {
      /* Process entire variable at once to avoid redundant function calls */
      auto iterationMask = readMask & var->componentMask;

      if (regIndexRelative) {
        auto address = computeRegisterAddress(builder, *var,
          ir::SsaDef(), regIndexRelative, regIndexAbsolute, readMask.first());
        dxbc_spv_assert(builder.getOp(address).getType() == ir::BasicType(ir::ScalarType::eU32, 2u));

        /* Get or declare function for the given opcode and variable */
        auto functionDef = getInterpolationFunction(builder, *var, opCode);
        dxbc_spv_assert(functionDef);

        /* Build function call, passing the register index first and then
         * the extra arguments. */
        auto functionType = m_converter.makeVectorType(scalarType, var->componentMask);
        auto callOp = ir::Op::FunctionCall(functionType, functionDef);
        callOp.addParam(m_converter.extractFromVector(builder, address, 0u));

        if (argument)
          callOp.addParam(argument);

        auto functionResult = builder.add(std::move(callOp));

        /* Extract scalars from result vector */
        for (auto c : iterationMask) {
          auto indexInSrc = util::popcnt(uint8_t(var->componentMask) & (uint8_t(c) - 1u));

          auto componentIndex = uint8_t(componentFromBit(c));
          components[componentIndex] = m_converter.extractFromVector(builder, functionResult, indexInSrc);
        }
      } else {
        /* If we can address registers directly, just process
         * one component at a time as normal */
        for (auto c : iterationMask) {
          auto address = computeRegisterAddress(builder, *var,
            ir::SsaDef(), ir::SsaDef(), regIndexAbsolute, c);

          ir::Op op(opCode, scalarType);
          op.addOperand(var->baseDef);
          op.addOperand(address);

          if (argument)
            op.addOperand(argument);

          auto componentIndex = uint8_t(componentFromBit(c));
          components[componentIndex] = builder.add(std::move(op));
        }
      }

      readMask -= iterationMask;
    }
  }

  auto returnType = m_converter.makeVectorType(scalarType, writeMask);
  return m_converter.composite(builder, returnType, components.data(), swizzle, writeMask);
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
  auto loopConstruct = ir::SsaDef();

  auto vertexCountDef = determineIncomingVertexCount(builder, vertexCount);
  auto vertexIndexDef = ir::SsaDef();
  auto vertexIndex = ir::SsaDef();

  if (vertexCountDef) {
    vertexIndexDef = builder.add(ir::Op::DclTmp(ir::ScalarType::eU32, m_converter.getEntryPoint()));
    builder.add(ir::Op::TmpStore(vertexIndexDef, builder.makeConstant(0u)));
    loopConstruct = builder.add(ir::Op::ScopedLoop(ir::SsaDef()));

    vertexIndex = builder.add(ir::Op::TmpLoad(ir::ScalarType::eU32, vertexIndexDef));
  }

  /* Iterate over matching input variables and copy scalars over one by one */
  for (uint32_t i = 0u; i < var.regCount; i++) {
    uint32_t componentIndex = 0u;

    for (auto component : var.componentMask) {
      auto scalar = ir::SsaDef();
      auto srcVar = findIoVar(m_variables, var.regType, var.regIndex + i, var.gsStream, component);

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

        scalar = convertScalar(builder, scalarType, scalar);
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

    auto condConstruct = builder.add(ir::Op::ScopedIf(ir::SsaDef(),
      builder.add(ir::Op::UGe(ir::ScalarType::eBool, vertexIndex, vertexCountDef))));
    builder.add(ir::Op::ScopedLoopBreak(loopConstruct));

    auto condEnd = builder.add(ir::Op::ScopedEndIf(condConstruct));
    builder.rewriteOp(condConstruct, ir::Op(builder.getOp(condConstruct)).setOperand(0u, condEnd));

    auto loopEnd = builder.add(ir::Op::ScopedEndLoop(loopConstruct));
    builder.rewriteOp(loopConstruct, ir::Op(builder.getOp(loopConstruct)).setOperand(0u, loopEnd));
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

  auto switchConstruct = builder.add(ir::Op::ScopedSwitch(ir::SsaDef(), regIndex));

  for (uint32_t i = 0u; i < var.regCount; i++) {
    for (uint32_t j = 0u; j < componentCount; j++) {
      auto component = ComponentBit(uint8_t(var.componentMask.first()) << j);
      auto targetVar = findIoVar(m_variables, var.regType, var.regIndex + i, var.gsStream, component);

      if (!targetVar || !targetVar->baseDef)
        continue;

      builder.add(ir::Op::ScopedSwitchCase(switchConstruct, componentCount * i + j));

      /* If necessary. convert the incoming value to the target type */
      auto targetType = targetVar->baseType.getBaseType(0u).getBaseType();
      auto scalar = convertScalar(builder, targetType, value);

      auto address = computeRegisterAddress(builder, *targetVar,
        vertexIndex, ir::SsaDef(), var.regIndex + i, component);

      auto dclOp = builder.getOp(targetVar->baseDef).getOpCode();

      if (dclOp == ir::OpCode::eDclOutput || dclOp == ir::OpCode::eDclOutputBuiltIn)
        builder.add(ir::Op::OutputStore(targetVar->baseDef, address, scalar));
      else
        builder.add(ir::Op::TmpStore(targetVar->baseDef, scalar));

      builder.add(ir::Op::ScopedSwitchBreak(switchConstruct));
    }
  }

  auto switchEnd = builder.add(ir::Op::ScopedEndSwitch(switchConstruct));
  builder.rewriteOp(switchConstruct, ir::Op(builder.getOp(switchConstruct)).setOperand(0u, switchEnd));

  builder.add(ir::Op::FunctionEnd());

  if (m_converter.m_options.includeDebugNames) {
    auto functionName = std::string("store_range_") +
      m_converter.makeRegisterDebugName(var.regType, var.regIndex, var.componentMask);
    builder.add(ir::Op::DebugName(function, functionName.c_str()));
  }

  builder.setCursor(cursor);
  return std::make_pair(emulatedType, function);
}


ir::SsaDef IoMap::emitConvertEvalOffsetFunction(
        ir::Builder&            builder) {
  auto paramXDef = builder.add(ir::Op::DclParam(ir::ScalarType::eI32));
  auto paramYDef = builder.add(ir::Op::DclParam(ir::ScalarType::eI32));

  if (m_converter.m_options.includeDebugNames) {
    builder.add(ir::Op::DebugName(paramXDef, "x"));
    builder.add(ir::Op::DebugName(paramYDef, "y"));
  }

  auto functionDef = builder.addBefore(builder.getCode().first->getDef(),
    ir::Op::Function(ir::BasicType(ir::ScalarType::eF32, 2u)).addParam(paramXDef).addParam(paramYDef));

  auto cursor = builder.setCursor(functionDef);

  if (m_converter.m_options.includeDebugNames)
    builder.add(ir::Op::DebugName(functionDef, "convert_eval_offsets"));

  /* Incoming values have a range of [-0.5..0.5) */
  auto factor = builder.makeConstant(1.0f / 16.0f);

  std::array<ir::SsaDef, 2u> results = { paramXDef, paramYDef };

  for (uint32_t i = 0u; i < 2u; i++) {
    auto& r = results[i];
    r = builder.add(ir::Op::ParamLoad(ir::ScalarType::eI32, functionDef, r));
    r = builder.add(ir::Op::SBitExtract(ir::ScalarType::eI32, r, builder.makeConstant(0), builder.makeConstant(4)));
    r = builder.add(ir::Op::ConvertItoF(ir::ScalarType::eF32, r));
    r = builder.add(ir::Op::FMul(ir::ScalarType::eF32, r, factor));
  }

  auto result = builder.add(ir::Op::CompositeConstruct(
    ir::BasicType(ir::ScalarType::eF32, 2u), results[0u], results[1u]));
  builder.add(ir::Op::Return(ir::BasicType(ir::ScalarType::eF32, 2u), result));
  builder.add(ir::Op::FunctionEnd());

  builder.setCursor(cursor);
  return functionDef;
}


ir::SsaDef IoMap::emitInterpolationFunction(
        ir::Builder&            builder,
  const IoVarInfo&              var,
        ir::OpCode              opCode) {
  /* Build function declaration op */
  auto functionType = m_converter.makeVectorType(ir::ScalarType::eF32, var.componentMask);
  auto functionOp = ir::Op::Function(functionType);

  /* Register index relative to start of indexed range */
  auto paramReg = builder.add(ir::Op::DclParam(ir::ScalarType::eU32));
  functionOp.addParam(paramReg);

  if (m_converter.m_options.includeDebugNames)
    builder.add(ir::Op::DebugName(paramReg, "reg"));

  /* Declare extra argument depending on the function parameter type */
  ir::BasicType argType = { };
  const char* argName = nullptr;

  if (opCode == ir::OpCode::eInterpolateAtSample) {
    argType = ir::ScalarType::eU32;
    argName = "sample";
  } else if (opCode == ir::OpCode::eInterpolateAtOffset) {
    argType = ir::BasicType(ir::ScalarType::eF32, 2u);
    argName = "offset";
  }

  ir::SsaDef paramArg = { };

  if (!argType.isVoidType()) {
    paramArg = builder.add(ir::Op::DclParam(argType));
    functionOp.addParam(paramArg);

    if (m_converter.m_options.includeDebugNames && argName)
      builder.add(ir::Op::DebugName(paramArg, argName));
  }

  /* Declare function and temporary result variable */
  auto functionDef = builder.addBefore(builder.getCode().first->getDef(), std::move(functionOp));

  if (m_converter.m_options.includeDebugNames) {
    auto regName = m_converter.makeRegisterDebugName(var.regType, var.regIndex, var.componentMask) + std::string("_indexed");
    auto funcName = [opCode] {
      switch (opCode) {
        case ir::OpCode::eInterpolateAtCentroid:  return "eval_centroid_";
        case ir::OpCode::eInterpolateAtSample:    return "eval_sample_";
        case ir::OpCode::eInterpolateAtOffset:    return "eval_snapped_";
        default:                                  return "eval_undefined_";
      }
    } ();

    builder.add(ir::Op::DebugName(functionDef, (std::string(funcName) + regName).c_str()));
  }

  auto cursor = builder.setCursor(functionDef);

  /* Load function parameters */
  auto regIndex = builder.add(ir::Op::ParamLoad(ir::ScalarType::eU32, functionDef, paramReg));
  auto argValue = ir::SsaDef();

  if (paramArg)
    argValue = builder.add(ir::Op::ParamLoad(argType, functionDef, paramArg));

  auto switchConstruct = builder.add(ir::Op::ScopedSwitch(ir::SsaDef(), regIndex));

  /* Iterate over all registers in the range and emit interpolation for
   * all included registers and ranges. */
  for (uint32_t i = 0u; i < var.regCount; i++) {
    builder.add(ir::Op::ScopedSwitchCase(switchConstruct, i));

    auto result = interpolateIoRegister(builder, opCode, ir::ScalarType::eF32,
      var.regType, ir::SsaDef(), var.regIndex + i, Swizzle::identity(), var.componentMask, argValue);
    builder.add(ir::Op::Return(functionType, result));

    builder.add(ir::Op::ScopedSwitchBreak(switchConstruct));
  }

  auto switchEnd = builder.add(ir::Op::ScopedEndSwitch(switchConstruct));
  builder.rewriteOp(switchConstruct, ir::Op::ScopedSwitch(switchEnd, regIndex));

  builder.add(ir::Op::Return(functionType, builder.makeUndef(functionType)));
  builder.add(ir::Op::FunctionEnd());

  builder.setCursor(cursor);
  return functionDef;
}


ir::SsaDef IoMap::getInterpolationFunction(
        ir::Builder&            builder,
        IoVarInfo&              var,
        ir::OpCode              opCode) {
  auto& def = [&var, opCode] () -> ir::SsaDef& {
    switch (opCode) {
      case ir::OpCode::eInterpolateAtCentroid:  return var.evalCentroid;
      case ir::OpCode::eInterpolateAtSample:    return var.evalSample;
      case ir::OpCode::eInterpolateAtOffset:    return var.evalSnapped;
      default:                                  break;
    }

    dxbc_spv_unreachable();
    return var.evalCentroid;
  } ();

  if (!def)
    def = emitInterpolationFunction(builder, var, opCode);

  return def;
}


ir::SsaDef IoMap::getCurrentFunction() const {
  if (m_shaderInfo.getType() == ShaderType::eHull)
    return m_converter.m_hs.phaseFunction;

  return m_converter.m_entryPoint.mainFunc;
}


ir::ScalarType IoMap::getIndexedBaseType(
  const IoVarInfo&              var) {
  /* Use same type as the base I/O variable if possible. If we cannot
   * determine it for whatever reason, use u32 as a fallback. */
  auto baseVar = findIoVar(m_variables, var.regType, var.regIndex, var.gsStream, var.componentMask);

  if (!baseVar || !baseVar->baseDef)
    return ir::ScalarType::eU32;

  return baseVar->baseType.getBaseType(0u).getBaseType();
}


IoRegisterIndex IoMap::loadRegisterIndices(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand) {
  IoRegisterIndex result = { };
  result.regType = normalizeRegisterType(operand.getRegisterType());

  uint32_t dim = operand.getIndexDimensions();

  if (!dim)
    return result;

  if (dim > 1u)
    result.vertexIndex = m_converter.loadOperandIndex(builder, op, operand, 0u);

  if (result.regType == RegisterType::eControlPointOut && m_converter.m_hs.phase == HullShaderPhase::eControlPoint)
    result.vertexIndex = loadTessControlPointId(builder);

  if (hasAbsoluteIndexing(operand.getIndexType(dim - 1u)))
    result.regIndexAbsolute = operand.getIndex(dim - 1u);

  if (hasRelativeIndexing(operand.getIndexType(dim - 1u))) {
    result.regIndexRelative = m_converter.loadSrcModified(builder, op,
      op.getRawOperand(operand.getIndexOperand(dim - 1u)),
      ComponentBit::eX, ir::ScalarType::eU32);
  }

  return result;
}


ir::SsaDef IoMap::convertScalar(ir::Builder& builder, ir::ScalarType dstType, ir::SsaDef value) {
  const auto& srcType = builder.getOp(value).getType();
  dxbc_spv_assert(srcType.isScalarType());

  auto scalarType = srcType.getBaseType(0u).getBaseType();

  if (scalarType == dstType)
    return value;

  if (scalarType == ir::ScalarType::eBool)
    return m_converter.boolToInt(builder, value);

  if (dstType == ir::ScalarType::eBool)
    return m_converter.intToBool(builder, value);

  return builder.add(ir::Op::ConsumeAs(dstType, value));
}


IoVarInfo* IoMap::findIoVar(IoVarList& list, RegisterType regType, uint32_t regIndex, int32_t stream, WriteMask mask) {
  for (auto& e : list) {
    if (e.matches(regType, regIndex, stream, mask))
      return &e;
  }

  return nullptr;
}


void IoMap::emitSemanticName(ir::Builder& builder, ir::SsaDef def, const SignatureEntry& entry) const {
  builder.add(ir::Op::Semantic(def, entry.getSemanticIndex(), entry.getSemanticName()));
}


bool IoMap::handleGsOutputStreams(ir::Builder& builder) {
  /* For geometry shaders, output registers can alias with no stream info.
   * To work around this, we declare temporary variables for each register
   * component mapped to an output register, and copy on Emit. */
  for (; m_gsConvertedCount < m_variables.size(); m_gsConvertedCount++) {
    /* Can't use reference here since we'll invalidate it */
    auto var = m_variables.at(m_gsConvertedCount);

    if (var.regType != RegisterType::eOutput || var.gsStream >= 0 || !var.baseDef)
      continue;

    /* Only consider actual output declarations */
    const auto& dclOp = builder.getOp(var.baseDef);

    if (dclOp.getOpCode() != ir::OpCode::eDclOutput &&
        dclOp.getOpCode() != ir::OpCode::eDclOutputBuiltIn)
      continue;

    /* Assign current stream index to real output */
    int32_t stream = int32_t(m_converter.m_gs.streamIndex);
    m_variables.at(m_gsConvertedCount).gsStream = stream;

    /* Find or declare temporary variable for each component
     * of the given output */
    dxbc_spv_assert(var.regCount == 1u);

    for (auto c : var.componentMask) {
      auto* tmp = IoMap::findIoVar(m_variables, var.regType, var.regIndex, -1, c);

      if (!tmp) {
        auto& mapping = m_variables.emplace_back();
        mapping.regType = var.regType;
        mapping.regIndex = var.regIndex;
        mapping.regCount = var.regCount;
        mapping.gsStream = -1;
        mapping.componentMask = c;
        mapping.baseType = var.baseType;
        mapping.baseDef = builder.add(ir::Op::DclTmp(mapping.baseType, m_converter.getEntryPoint()));
      }
    }
  }

  return true;
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
    declaration.addOperand(m_converter.m_gs.streamIndex);

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
