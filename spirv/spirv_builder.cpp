#include <cstring>
#include <iostream>

#include "spirv_builder.h"

namespace dxbc_spv::spirv {

SpirvBuilder::SpirvBuilder(const ir::Builder& builder, const Options& options)
: m_builder(builder), m_options(options) {
  emitMemoryModel();
}


SpirvBuilder::~SpirvBuilder() {

}


void SpirvBuilder::buildSpirvBinary() {
  if (m_options.includeDebugNames)
    processDebugNames();

  for (const auto& op : m_builder)
    emitInstruction(op);

  finalize();
}


std::vector<uint32_t> SpirvBuilder::getSpirvBinary() const {
  size_t size = 0u;
  getSpirvBinary(size, nullptr);

  std::vector<uint32_t> binary(size / sizeof(uint32_t));
  getSpirvBinary(size, binary.data());
  return binary;
}


void SpirvBuilder::getSpirvBinary(size_t& size, void* data) const {
  std::array<std::pair<size_t, const uint32_t*>, 10u> arrays = {{
    { m_capabilities.size(), m_capabilities.data() },
    { m_extensions.size(), m_extensions.data() },
    { m_imports.size(), m_imports.data() },
    { m_memoryModel.size(), m_memoryModel.data() },
    { m_entryPoint.size(), m_entryPoint.data() },
    { m_executionModes.size(), m_executionModes.data() },
    { m_debug.size(), m_debug.data() },
    { m_decorations.size(), m_decorations.data() },
    { m_declarations.size(), m_declarations.data() },
    { m_code.size(), m_code.data() },
  }};

  /* Compute total required size */
  size_t dwordCount = sizeof(m_header) / sizeof(uint32_t);

  for (const auto& e : arrays)
    dwordCount += e.first;

  size_t byteCount = dwordCount * sizeof(uint32_t);

  if (data) {
    /* Copy as many dwords as we can fit into the buffer */
    size = std::min(size, byteCount);
    dwordCount = size / sizeof(uint32_t);

    if (size >= sizeof(m_header))
      std::memcpy(data, &m_header, sizeof(m_header));

    size_t dwordIndex = sizeof(m_header) / sizeof(uint32_t);

    if (dwordIndex < dwordCount) {
      for (const auto& e : arrays) {
        size_t dwordsToCopy = std::min(e.first, dwordCount - dwordIndex);
        std::memcpy(reinterpret_cast<unsigned char*>(data) + dwordIndex * sizeof(uint32_t),
          e.second, dwordsToCopy * sizeof(uint32_t));

        dwordIndex += dwordsToCopy;
      }
    }
  } else {
    /* Write back total byte size */
    size = byteCount;
  }
}


void SpirvBuilder::processDebugNames() {
  auto decls = m_builder.getDeclarations();

  for (auto op = decls.first; op != decls.second; op++) {
    if (op->getOpCode() == ir::OpCode::eDebugName)
      m_debugNames.insert({ ir::SsaDef(op->getOperand(0u)), op->getDef() });
  }
}


void SpirvBuilder::finalize() {

}


void SpirvBuilder::emitInstruction(const ir::Op& op) {
  switch (op.getOpCode()) {
    case ir::OpCode::eEntryPoint:
      return emitEntryPoint(op);

    case ir::OpCode::eDebugName:
    case ir::OpCode::eSemantic:
      /* No-op here, we resolve debug names early */
      return;

    case ir::OpCode::eConstant:
      return emitConstant(op);

    case ir::OpCode::eSetCsWorkgroupSize:
      return emitSetCsWorkgroupSize(op);

    case ir::OpCode::eDclInput:
    case ir::OpCode::eDclOutput:
      return emitDclIoVar(op);

    case ir::OpCode::eDclInputBuiltIn:
    case ir::OpCode::eDclOutputBuiltIn:
      return emitDclBuiltInIoVar(op);

    case ir::OpCode::eDclParam:
      /* No-op, resolved when declaring function */
      return;

    case ir::OpCode::eDclSampler:
      return emitDclSampler(op);

    case ir::OpCode::eDclCbv:
      return emitDclCbv(op);

    case ir::OpCode::eDclSrv:
    case ir::OpCode::eDclUav:
      return emitDclSrvUav(op);

    case ir::OpCode::eDclUavCounter:
      return emitDclUavCounter(op);

    case ir::OpCode::eDescriptorLoad:
      return emitDescriptorLoad(op);

    case ir::OpCode::eBufferLoad:
      return emitBufferLoad(op);

    case ir::OpCode::eBufferStore:
      return emitBufferStore(op);

    case ir::OpCode::eBufferQuerySize:
      return emitBufferQuery(op);

    case ir::OpCode::eBufferAtomic:
      return emitBufferAtomic(op);

    case ir::OpCode::eCounterAtomic:
      return emitCounterAtomic(op);

    case ir::OpCode::eFunction:
      return emitFunction(op);

    case ir::OpCode::eFunctionEnd:
      return emitFunctionEnd();

    case ir::OpCode::eLabel:
      return emitLabel(op);

    case ir::OpCode::eBranch:
      return emitBranch(op);

    case ir::OpCode::eBranchConditional:
      return emitBranchConditional(op);

    case ir::OpCode::eSwitch:
      return emitSwitch(op);

    case ir::OpCode::eUnreachable:
      return emitUnreachable();

    case ir::OpCode::eReturn:
      return emitReturn(op);

    case ir::OpCode::ePhi:
      return emitPhi(op);

    case ir::OpCode::eScratchLoad:
    case ir::OpCode::eLdsLoad:
    case ir::OpCode::ePushDataLoad:
    case ir::OpCode::eInputLoad:
    case ir::OpCode::eOutputLoad:
      return emitLoadVariable(op);

    case ir::OpCode::eScratchStore:
    case ir::OpCode::eLdsStore:
    case ir::OpCode::eOutputStore:
      return emitStoreVariable(op);

    case ir::OpCode::eCompositeInsert:
    case ir::OpCode::eCompositeExtract:
      return emitCompositeOp(op);

    case ir::OpCode::eCompositeConstruct:
      return emitCompositeConstruct(op);

    case ir::OpCode::eImageLoad:
      return emitImageLoad(op);

    case ir::OpCode::eImageQuerySize:
      return emitImageQuerySize(op);

    case ir::OpCode::eImageQueryMips:
      return emitImageQueryMips(op);

    case ir::OpCode::eImageQuerySamples:
      return emitImageQuerySamples(op);

    case ir::OpCode::eCheckSparseAccess:
      return emitCheckSparseAccess(op);

    case ir::OpCode::eConvertFtoF:
    case ir::OpCode::eConvertFtoI:
    case ir::OpCode::eConvertItoF:
    case ir::OpCode::eConvertItoI:
    case ir::OpCode::eCast:
      return emitConvert(op);

    case ir::OpCode::eFEq:
    case ir::OpCode::eFNe:
    case ir::OpCode::eFLt:
    case ir::OpCode::eFLe:
    case ir::OpCode::eFGt:
    case ir::OpCode::eFGe:
    case ir::OpCode::eFIsNan:
    case ir::OpCode::eIEq:
    case ir::OpCode::eINe:
    case ir::OpCode::eSLt:
    case ir::OpCode::eSGe:
    case ir::OpCode::eULt:
    case ir::OpCode::eUGe:
    case ir::OpCode::eBAnd:
    case ir::OpCode::eBOr:
    case ir::OpCode::eBEq:
    case ir::OpCode::eBNe:
    case ir::OpCode::eBNot:
    case ir::OpCode::eSelect:
    case ir::OpCode::eFNeg:
    case ir::OpCode::eFAdd:
    case ir::OpCode::eFSub:
    case ir::OpCode::eFMul:
    case ir::OpCode::eFDiv:
    case ir::OpCode::eIAnd:
    case ir::OpCode::eIOr:
    case ir::OpCode::eIXor:
    case ir::OpCode::eINot:
    case ir::OpCode::eIBitInsert:
    case ir::OpCode::eUBitExtract:
    case ir::OpCode::eSBitExtract:
    case ir::OpCode::eIShl:
    case ir::OpCode::eSShr:
    case ir::OpCode::eUShr:
    case ir::OpCode::eIAdd:
    case ir::OpCode::eISub:
    case ir::OpCode::eINeg:
    case ir::OpCode::eIMul:
    case ir::OpCode::eSDiv:
    case ir::OpCode::eUDiv:
      return emitSimpleArithmetic(op);

    case ir::OpCode::eSetGsInstances:
    case ir::OpCode::eSetGsInputPrimitive:
    case ir::OpCode::eSetGsOutputVertices:
    case ir::OpCode::eSetGsOutputPrimitive:
    case ir::OpCode::eSetPsEarlyFragmentTest:
    case ir::OpCode::eSetPsDepthGreaterEqual:
    case ir::OpCode::eSetPsDepthLessEqual:
    case ir::OpCode::eSetTessPrimitive:
    case ir::OpCode::eSetTessDomain:
    case ir::OpCode::eDclSpecConstant:
    case ir::OpCode::eDclPushData:
    case ir::OpCode::eDclLds:
    case ir::OpCode::eDclScratch:
    case ir::OpCode::eFunctionCall:
    case ir::OpCode::eBarrier:
    case ir::OpCode::eParamLoad:
    case ir::OpCode::eSpecConstantLoad:
    case ir::OpCode::eMemoryLoad:
    case ir::OpCode::eMemoryStore:
    case ir::OpCode::eLdsAtomic:
    case ir::OpCode::eImageAtomic:
    case ir::OpCode::eMemoryAtomic:
    case ir::OpCode::eImageStore:
    case ir::OpCode::eImageSample:
    case ir::OpCode::eImageGather:
    case ir::OpCode::eImageComputeLod:
    case ir::OpCode::ePointer:
    case ir::OpCode::ePointerAddress:
    case ir::OpCode::eEmitVertex:
    case ir::OpCode::eEmitPrimitive:
    case ir::OpCode::eDemote:
    case ir::OpCode::eInterpolateAtCentroid:
    case ir::OpCode::eInterpolateAtSample:
    case ir::OpCode::eInterpolateAtOffset:
    case ir::OpCode::eDerivX:
    case ir::OpCode::eDerivY:
    case ir::OpCode::eRovScopedLockBegin:
    case ir::OpCode::eRovScopedLockEnd:
    case ir::OpCode::eFAbs:
    case ir::OpCode::eFDot:
    case ir::OpCode::eFDotLegacy:
    case ir::OpCode::eFMulLegacy:
    case ir::OpCode::eFMad:
    case ir::OpCode::eFMadLegacy:
    case ir::OpCode::eFRcp:
    case ir::OpCode::eFSqrt:
    case ir::OpCode::eFRsq:
    case ir::OpCode::eFExp2:
    case ir::OpCode::eFLog2:
    case ir::OpCode::eFFract:
    case ir::OpCode::eFMin:
    case ir::OpCode::eFMax:
    case ir::OpCode::eFClamp:
    case ir::OpCode::eFSin:
    case ir::OpCode::eFCos:
    case ir::OpCode::eFRound:
    case ir::OpCode::eIBitCount:
    case ir::OpCode::eIBitReverse:
    case ir::OpCode::eIFindLsb:
    case ir::OpCode::eSFindMsb:
    case ir::OpCode::eUFindMsb:
    case ir::OpCode::eIAddCarry:
    case ir::OpCode::eISubBorrow:
    case ir::OpCode::eSMin:
    case ir::OpCode::eSMax:
    case ir::OpCode::eSClamp:
    case ir::OpCode::eUMin:
    case ir::OpCode::eUMax:
    case ir::OpCode::eUClamp:
    case ir::OpCode::eUMSad:
      /* TODO implement */
      std::cerr << "Unimplemented opcode " << op.getOpCode() << std::endl;
      break;

    case ir::OpCode::eUnknown:
    case ir::OpCode::eLastDeclarative:
    case ir::OpCode::eDclTmp:
    case ir::OpCode::eTmpLoad:
    case ir::OpCode::eTmpStore:
    case ir::OpCode::eScopedIf:
    case ir::OpCode::eScopedElse:
    case ir::OpCode::eScopedEndIf:
    case ir::OpCode::eScopedLoop:
    case ir::OpCode::eScopedLoopBreak:
    case ir::OpCode::eScopedLoopContinue:
    case ir::OpCode::eScopedEndLoop:
    case ir::OpCode::eScopedSwitch:
    case ir::OpCode::eScopedSwitchCase:
    case ir::OpCode::eScopedSwitchDefault:
    case ir::OpCode::eScopedSwitchBreak:
    case ir::OpCode::eScopedEndSwitch:
    case ir::OpCode::eConsumeAs:
    case ir::OpCode::Count:
      /* Invalid opcodes */
      break;
  }

  dxbc_spv_unreachable();
}


void SpirvBuilder::emitEntryPoint(const ir::Op& op) {
  auto stage = ir::ShaderStage(op.getOperand(op.getFirstLiteralOperandIndex()));
  m_entryPointId = getIdForDef(ir::SsaDef(op.getOperand(0u)));

  spv::ExecutionModel executionModel = [&] {
    switch (stage) {
      case ir::ShaderStage::eVertex:
        return spv::ExecutionModelVertex;

      case ir::ShaderStage::ePixel: {
        pushOp(m_executionModes, spv::OpExecutionMode, m_entryPointId,
          spv::ExecutionModeOriginUpperLeft);
      } return spv::ExecutionModelFragment;

      case ir::ShaderStage::eGeometry: {
        enableCapability(spv::CapabilityGeometry);
      } return spv::ExecutionModelGeometry;

      case ir::ShaderStage::eHull: {
        enableCapability(spv::CapabilityTessellation);

        m_tessControl.controlPointFuncId = getIdForDef(ir::SsaDef(op.getOperand(0u)));
        m_tessControl.patchConstantFuncId = getIdForDef(ir::SsaDef(op.getOperand(1u)));

        m_entryPointId = allocId();
      } return spv::ExecutionModelTessellationControl;

      case ir::ShaderStage::eDomain: {
        enableCapability(spv::CapabilityTessellation);
      } return spv::ExecutionModelTessellationEvaluation;

      case ir::ShaderStage::eCompute:
        return spv::ExecutionModelGLCompute;

      case ir::ShaderStage::eFlagEnum:
        break;
    }

    dxbc_spv_unreachable();
    return spv::ExecutionModel();
  } ();

  /* Emit entry point instruction */
  const char* name = "main";

  m_entryPoint.push_back(makeOpcodeToken(spv::OpEntryPoint, 3u + getStringDwordCount(name)));
  m_entryPoint.push_back(executionModel);
  m_entryPoint.push_back(m_entryPointId);
  pushString(m_entryPoint, name);

  m_stage = stage;
}


void SpirvBuilder::emitConstant(const ir::Op& op) {
  uint32_t operandIndex = 0u;

  /* Deduplicate constants once more and assign the ID. This will reliably
   * work because constants are defined before any of their uses in the IR. */
  uint32_t spvId = makeConstant(op.getType(), op, operandIndex);

  setIdForDef(op.getDef(), spvId);
}


void SpirvBuilder::emitInterpolationModes(uint32_t id, ir::InterpolationModes modes) {
  static const std::array<std::pair<ir::InterpolationMode, spv::Decoration>, 4u> s_modes = {{
    { ir::InterpolationMode::eFlat,           spv::DecorationFlat           },
    { ir::InterpolationMode::eCentroid,       spv::DecorationCentroid       },
    { ir::InterpolationMode::eSample,         spv::DecorationSample         },
    { ir::InterpolationMode::eNoPerspective,  spv::DecorationNoPerspective  },
  }};

  if (!modes)
    return;

  if (modes & ir::InterpolationMode::eSample)
    enableCapability(spv::CapabilitySampleRateShading);

  for (const auto& mode : s_modes) {
    if (modes & mode.first)
      pushOp(m_decorations, spv::OpDecorate, id, mode.second);
  }
}


void SpirvBuilder::emitDclIoVar(const ir::Op& op) {
  auto storageClass = op.getOpCode() == ir::OpCode::eDclInput
    ? spv::StorageClassInput
    : spv::StorageClassOutput;

  uint32_t typeId = getIdForType(op.getType());
  uint32_t varId = getIdForDef(op.getDef());

  emitDebugName(op.getDef(), varId);

  uint32_t ptrTypeId = getIdForPtrType(typeId, storageClass);

  pushOp(m_declarations, spv::OpVariable, ptrTypeId, varId, storageClass);

  pushOp(m_decorations, spv::OpDecorate, varId, spv::DecorationLocation, uint32_t(op.getOperand(1u)));
  pushOp(m_decorations, spv::OpDecorate, varId, spv::DecorationComponent, uint32_t(op.getOperand(2u)));

  if (op.getOpCode() == ir::OpCode::eDclInput && m_stage == ir::ShaderStage::ePixel) {
    auto interpolationModes = ir::InterpolationModes(op.getOperand(3u));
    emitInterpolationModes(varId, interpolationModes);
  }

  addEntryPointId(varId);
}


uint32_t SpirvBuilder::emitBuiltInDrawParameter(spv::BuiltIn builtIn) {
  uint32_t ptrTypeId = getIdForPtrType(getIdForType(ir::ScalarType::eI32), spv::StorageClassInput);
  uint32_t varId = allocId();

  pushOp(m_declarations, spv::OpVariable, ptrTypeId, varId, spv::StorageClassInput);
  pushOp(m_decorations, spv::OpDecorate, varId, spv::DecorationBuiltIn, builtIn);

  addEntryPointId(varId);
  return varId;
}


void SpirvBuilder::emitDclBuiltInIoVar(const ir::Op& op) {
  auto builtIn = ir::BuiltIn(op.getOperand(1u));

  bool isInput = op.getOpCode() == ir::OpCode::eDclInputBuiltIn;
  auto storageClass = isInput ? spv::StorageClassInput : spv::StorageClassOutput;

  /* Override type for vertex and instance indices as signed integers.
   * Subtracting the respective base parameter from it will yield an
   * unsigned integer. */
  auto type = op.getType();

  if (builtIn == ir::BuiltIn::eVertexId || builtIn == ir::BuiltIn::eInstanceId)
    type = ir::ScalarType::eI32;

  uint32_t typeId = getIdForType(type);
  uint32_t varId = getIdForDef(op.getDef());

  emitDebugName(op.getDef(), varId);

  /* Declare actual variable */
  uint32_t ptrTypeId = getIdForPtrType(typeId, storageClass);
  pushOp(m_declarations, spv::OpVariable, ptrTypeId, varId, storageClass);

  /* Declare and handle built-in */
  spv::BuiltIn spvBuiltIn = [&] {
    switch (builtIn) {
      case ir::BuiltIn::ePosition:
        return m_stage == ir::ShaderStage::ePixel
          ? spv::BuiltInFragCoord : spv::BuiltInPosition;

      case ir::BuiltIn::eVertexId: {
        m_drawParams.baseVertex = emitBuiltInDrawParameter(spv::BuiltInBaseVertex);

        enableCapability(spv::CapabilityDrawParameters);
        return spv::BuiltInVertexIndex;
      }

      case ir::BuiltIn::eInstanceId: {
        m_drawParams.baseInstance = emitBuiltInDrawParameter(spv::BuiltInBaseInstance);

        enableCapability(spv::CapabilityDrawParameters);
        return spv::BuiltInInstanceIndex;
      }

      case ir::BuiltIn::eClipDistance: {
        enableCapability(spv::CapabilityClipDistance);
        return spv::BuiltInClipDistance;
      }

      case ir::BuiltIn::eCullDistance: {
        enableCapability(spv::CapabilityCullDistance);
        return spv::BuiltInCullDistance;
      }

      case ir::BuiltIn::ePrimitiveId: {
        if (m_stage == ir::ShaderStage::ePixel)
          enableCapability(spv::CapabilityGeometry);
        return spv::BuiltInPrimitiveId;
      }

      case ir::BuiltIn::eLayerIndex: {
        if ((m_stage != ir::ShaderStage::eGeometry || isInput) && m_stage != ir::ShaderStage::ePixel)
          enableCapability(spv::CapabilityShaderLayer);
        return spv::BuiltInLayer;
      }

      case ir::BuiltIn::eViewportIndex: {
        enableCapability(spv::CapabilityMultiViewport);

        if ((m_stage != ir::ShaderStage::eGeometry || isInput) && m_stage != ir::ShaderStage::ePixel)
          enableCapability(spv::CapabilityShaderViewportIndex);

        return spv::BuiltInViewportIndex;
      }

      case ir::BuiltIn::eGsInstanceId:
      case ir::BuiltIn::eTessControlPointId:
        return spv::BuiltInInvocationId;

      case ir::BuiltIn::eTessCoord:
        return spv::BuiltInTessCoord;

      case ir::BuiltIn::eTessFactorInner:
        return spv::BuiltInTessLevelInner;

      case ir::BuiltIn::eTessFactorOuter:
        return spv::BuiltInTessLevelOuter;

      case ir::BuiltIn::eSampleCount:
        /* must be lowered */
        break;

      case ir::BuiltIn::eSampleId: {
        enableCapability(spv::CapabilitySampleRateShading);
        return spv::BuiltInSampleId;
      }

      case ir::BuiltIn::eSamplePosition: {
        enableCapability(spv::CapabilitySampleRateShading);
        return spv::BuiltInSamplePosition;
      }

      case ir::BuiltIn::eSampleMask:
        return spv::BuiltInSampleMask;

      case ir::BuiltIn::eIsFrontFace:
        return spv::BuiltInFrontFacing;

      case ir::BuiltIn::eDepth:
        return spv::BuiltInFragDepth;

      case ir::BuiltIn::eStencilRef: {
        enableCapability(spv::CapabilityStencilExportEXT);
        return spv::BuiltInFragStencilRefEXT;
      }

      case ir::BuiltIn::eWorkgroupId:
        return spv::BuiltInWorkgroupId;

      case ir::BuiltIn::eGlobalThreadId:
        return spv::BuiltInGlobalInvocationId;

      case ir::BuiltIn::eLocalThreadId:
        return spv::BuiltInLocalInvocationId;

      case ir::BuiltIn::eLocalThreadIndex:
        return spv::BuiltInLocalInvocationIndex;
    }

    dxbc_spv_unreachable();
    return spv::BuiltIn();
  }();

  pushOp(m_decorations, spv::OpDecorate, varId, spv::DecorationBuiltIn, spvBuiltIn);

  /* Set up interpolation modes as neccessary */
  if (isInput && m_stage == ir::ShaderStage::ePixel) {
    auto interpolationModes = ir::InterpolationModes(op.getOperand(2u));
    emitInterpolationModes(varId, interpolationModes);
  }

  addEntryPointId(varId);
}


void SpirvBuilder::emitDclSampler(const ir::Op& op) {
  auto samplerTypeId = getIdForSamplerType();

  defDescriptor(op, samplerTypeId, spv::StorageClassUniformConstant);
  m_descriptorTypes.insert({ op.getDef(), samplerTypeId });
}


void SpirvBuilder::emitDclCbv(const ir::Op& op) {
  /* Create a wrapper struct that we can decorate as a block */
  auto structTypeId = defType(op.getType(), true);

  if (!op.getType().isStructType()) {
    structTypeId = defStructWrapper(structTypeId);

    if (m_options.includeDebugNames)
      setDebugMemberName(structTypeId, 0u, "m");
  }

  pushOp(m_decorations, spv::OpDecorate, structTypeId, spv::DecorationBlock);

  defDescriptor(op, structTypeId, spv::StorageClassUniform);
  m_descriptorTypes.insert({ op.getDef(), structTypeId });
}


void SpirvBuilder::emitDclSrvUav(const ir::Op& op) {
  auto kind = getResourceKind(op);

  if (kind == ir::ResourceKind::eBufferRaw || kind == ir::ResourceKind::eBufferStructured) {
    /* Create wrapper struct, much like this works for CBV */
    auto structTypeId = defType(op.getType(), true);

    if (!op.getType().isStructType()) {
      structTypeId = defStructWrapper(structTypeId);

      if (m_options.includeDebugNames)
        setDebugMemberName(structTypeId, 0u, "m");
    }

    pushOp(m_decorations, spv::OpDecorate, structTypeId, spv::DecorationBlock);

    defDescriptor(op, structTypeId, spv::StorageClassStorageBuffer);
    m_descriptorTypes.insert({ op.getDef(), structTypeId });
  } else {
    dxbc_spv_assert(op.getType().isScalarType());

    /* Scalar type used for image access operations */
    SpirvImageTypeKey key = { };
    key.sampledTypeId = getIdForType(op.getType());

    /* Determine dimension based on resource kind */
    bool isUav = op.getOpCode() == ir::OpCode::eDclUav;

    key.dim = [&] {
      switch (kind) {
        case ir::ResourceKind::eBufferTyped: {
          enableCapability(isUav
            ? spv::CapabilityImageBuffer
            : spv::CapabilitySampledBuffer);
        } return spv::DimBuffer;

        case ir::ResourceKind::eImage1D:
        case ir::ResourceKind::eImage1DArray: {
          enableCapability(isUav
            ? spv::CapabilityImage1D
            : spv::CapabilitySampled1D);
        } return spv::Dim1D;

        case ir::ResourceKind::eImage2D:
        case ir::ResourceKind::eImage2DArray:
        case ir::ResourceKind::eImage2DMS:
        case ir::ResourceKind::eImage2DMSArray:
          return spv::Dim2D;

        case ir::ResourceKind::eImageCube:
          return spv::DimCube;

        case ir::ResourceKind::eImageCubeArray: {
          enableCapability(spv::CapabilitySampledCubeArray);
        } return spv::DimCube;

        case ir::ResourceKind::eImage3D:
          return spv::Dim3D;

        default:
          dxbc_spv_unreachable();
          return spv::Dim();
      }
    } ();

    /* Determine whether this is a sampled or storage image. */
    key.sampled = isUav ? 2u : 1u;

    /* Determine arrayed-ness and multisampled-ness */
    key.arrayed = ir::resourceIsLayered(kind) ? 1u : 0u;
    key.ms = ir::resourceIsMultisampled(kind) ? 1u : 0u;

    /* Determine fixed image format for certain UAV use cases. */
    key.format = spv::ImageFormatUnknown;

    if (isUav) {
      auto uavFlags = getUavFlags(op);

      if (uavFlags & ir::UavFlag::eFixedFormat) {
        key.format = [&] {
          switch (op.getType().getBaseType(0u).getBaseType()) {
            case ir::ScalarType::eF32: return spv::ImageFormatR32f;
            case ir::ScalarType::eU32: return spv::ImageFormatR32ui;
            case ir::ScalarType::eI32: return spv::ImageFormatR32i;

            case ir::ScalarType::eU64: {
              enableCapability(spv::CapabilityInt64ImageEXT);
            } return spv::ImageFormatR64ui;

            case ir::ScalarType::eI64: {
              enableCapability(spv::CapabilityInt64ImageEXT);
            } return spv::ImageFormatR64i;

            default:
              dxbc_spv_unreachable();
          }
        } ();
      }

      /* Enable without-format caps depending on resource access */
      if (key.format == spv::ImageFormatUnknown) {
        if (!(uavFlags & ir::UavFlag::eWriteOnly))
          enableCapability(spv::CapabilityStorageImageReadWithoutFormat);
        if (!(uavFlags & ir::UavFlag::eReadOnly))
          enableCapability(spv::CapabilityStorageImageWriteWithoutFormat);
      }
    }

    /* Declare actual image type and descriptor */
    auto imageTypeId = getIdForImageType(key);

    defDescriptor(op, imageTypeId, spv::StorageClassUniformConstant);
    m_descriptorTypes.insert({ op.getDef(), imageTypeId });
  }
}


void SpirvBuilder::emitDclUavCounter(const ir::Op& op) {
  auto structTypeId = defStructWrapper(getIdForType(op.getType()));
  pushOp(m_decorations, spv::OpDecorate, structTypeId, spv::DecorationBlock);

  defDescriptor(op, structTypeId, spv::StorageClassStorageBuffer);
  m_descriptorTypes.insert({ op.getDef(), structTypeId });
}


uint32_t SpirvBuilder::getDescriptorArrayIndex(const ir::Op& op) {
  const auto& dclOp = m_builder.getOp(ir::SsaDef(op.getOperand(0u)));

  if (getDescriptorArraySize(dclOp) == 1u)
    return 0u; /* no array */

  if (op.getFlags() & ir::OpFlag::eNonUniform)
    enableCapability(spv::CapabilityShaderNonUniform);

  return getIdForDef(ir::SsaDef(op.getOperand(1u)));
}


uint32_t SpirvBuilder::getImageDescriptorPointer(const ir::Op& op) {
  const auto& dclOp = m_builder.getOp(ir::SsaDef(op.getOperand(0u)));
  dxbc_spv_assert(!declaresPlainBufferResource(dclOp));

  auto resourceId = getIdForDef(dclOp.getDef());
  auto indexId = getDescriptorArrayIndex(op);

  if (!indexId)
    return resourceId;

  auto typeId = m_descriptorTypes.at(dclOp.getDef());
  auto ptrTypeId = getIdForPtrType(typeId, spv::StorageClassUniformConstant);

  auto ptrId = allocId();
  pushOp(m_code, spv::OpAccessChain, ptrTypeId, ptrId, resourceId, indexId);
  return ptrId;
}


void SpirvBuilder::emitDescriptorLoad(const ir::Op& op) {
  const auto& dclOp = m_builder.getOp(ir::SsaDef(op.getOperand(0u)));

  auto typeId = m_descriptorTypes.at(dclOp.getDef());
  auto id = getIdForDef(op.getDef());

  if (declaresPlainBufferResource(dclOp)) {
    auto resourceId = getIdForDef(dclOp.getDef());
    auto indexId = getDescriptorArrayIndex(op);

    auto storageClass = op.getType() == ir::ScalarType::eCbv
      ? spv::StorageClassUniform
      : spv::StorageClassStorageBuffer;

    auto ptrTypeId = getIdForPtrType(typeId, storageClass);

    if (!indexId) {
      /* No access chain needed, chain into buffer directly */
      setIdForDef(op.getDef(), resourceId);
    } else {
      /* Access chain into the descriptor array as necessary, then let
       * subsequent access chain address the buffer element itself. */
      m_code.push_back(makeOpcodeToken(spv::OpAccessChain, 5u));
      m_code.push_back(ptrTypeId);
      m_code.push_back(id);
      m_code.push_back(resourceId);
      m_code.push_back(indexId);
    }
  } else {
    /* Loading image or sampler descriptors otuside of the block where they are used
     * is technically against spec, but vkd3d-proton has been relying on this for
     * years, so it should be safe to do. */
    auto ptrId = getImageDescriptorPointer(op);
    pushOp(m_code, spv::OpLoad, typeId, id, ptrId);

    /* Decorate result as non-uniform as necessary */
    if (op.getFlags() & ir::OpFlag::eNonUniform)
      pushOp(m_decorations, spv::OpDecorate, id, spv::DecorationNonUniform);
  }
}


void SpirvBuilder::emitBufferLoad(const ir::Op& op) {
  /* Get op that loaded the descriptor, and the resource declaration */
  const auto& descriptorOp = m_builder.getOp(ir::SsaDef(op.getOperand(0u)));
  const auto& dclOp = m_builder.getOp(ir::SsaDef(descriptorOp.getOperand(0u)));

  bool isUav = descriptorOp.getType() == ir::ScalarType::eUav;
  bool isSparse = op.getFlags() & ir::OpFlag::eSparseFeedback;

  if (isSparse)
    enableCapability(spv::CapabilitySparseResidency);

  auto id = getIdForDef(op.getDef());
  auto addressDef = ir::SsaDef(op.getOperand(1u));

  if (declaresPlainBufferResource(dclOp)) {
    dxbc_spv_assert(!isSparse);

    auto addressedType = traverseType(dclOp.getType(), addressDef);
    auto accessType = op.getType();

    dxbc_spv_assert(addressedType == accessType ||
      (addressedType.isScalarType() && accessType.isVectorType()));

    /* Set up memory operands depending on the resource type */
    SpirvMemoryOperands memoryOperands = { };

    if (isUav) {
      auto uavFlags = getUavFlags(dclOp);

      if (!(uavFlags & ir::UavFlag::eReadOnly)) {
        memoryOperands.flags |= spv::MemoryAccessNonPrivatePointerMask
                             |  spv::MemoryAccessMakePointerVisibleMask;
        memoryOperands.makeVisible = makeConstU32((uavFlags & ir::UavFlag::eCoherent)
          ? spv::ScopeQueueFamily : spv::ScopeWorkgroup);
      }
    }

    /* Emit access chains for loading the requested data. */
    util::small_vector<uint32_t, 4u> loadIds;

    auto storageClass = descriptorOp.getType() == ir::ScalarType::eCbv
      ? spv::StorageClassUniform
      : spv::StorageClassStorageBuffer;

    bool hasWrapperStruct = !dclOp.getType().isStructType();

    if (m_options.nvRawAccessChains && descriptorOp.getType() != ir::ScalarType::eCbv) {
      loadIds.push_back(emitRawAccessChainNv(storageClass, accessType, dclOp,
        getIdForDef(descriptorOp.getDef()), addressDef));

      memoryOperands.flags |= spv::MemoryAccessAlignedMask;
      memoryOperands.alignment = uint32_t(op.getOperand(2u));

      addressedType = accessType;
    } else if (addressedType == accessType) {
      /* Trivial case, we can emit a single load. */
      loadIds.push_back(emitAccessChain(storageClass, dclOp.getType(),
        getIdForDef(descriptorOp.getDef()), addressDef, 0u, hasWrapperStruct));
    } else {
      /* Need to emit multiple loads and assemble a vector later. */
      for (uint32_t i = 0u; i < accessType.getBaseType(0u).getVectorSize(); i++) {
        loadIds.push_back(emitAccessChain(storageClass, dclOp.getType(),
          getIdForDef(descriptorOp.getDef()), addressDef, i, hasWrapperStruct));
      }
    }

    /* Emit actual loads. */
    for (auto& accessId : loadIds) {
      uint32_t loadId = loadIds.size() > 1u ? allocId() : id;

      /* Decorate access chains as nonuniform as necessary */
      if (descriptorOp.getFlags() & ir::OpFlag::eNonUniform)
        pushOp(m_decorations, spv::OpDecorate, accessId, spv::DecorationNonUniform);

      m_code.push_back(makeOpcodeToken(spv::OpLoad, 4u + memoryOperands.computeDwordCount()));
      m_code.push_back(getIdForType(addressedType));
      m_code.push_back(loadId);
      m_code.push_back(accessId);
      memoryOperands.pushTo(m_code);

      accessId = loadId;
    }

    /* Assemble composite if necessary */
    if (loadIds.size() > 1u) {
      m_code.push_back(makeOpcodeToken(spv::OpCompositeConstruct, 3u + loadIds.size()));
      m_code.push_back(getIdForType(accessType));
      m_code.push_back(id);

      for (auto loadId : loadIds)
        m_code.push_back(loadId);
    }
  } else {
    /* Set up image operands analogous to the memory operands above */
    SpirvImageOperands imageOperands = { };

    if (isUav)
      setUavImageReadOperands(imageOperands, dclOp);

    /* Select correct opcode to emit */
    auto opCode = isUav
      ? (isSparse ? spv::OpImageSparseRead : spv::OpImageRead)
      : (isSparse ? spv::OpImageSparseFetch : spv::OpImageFetch);

    m_code.push_back(makeOpcodeToken(opCode, 5u + imageOperands.computeDwordCount()));
    m_code.push_back(getIdForType(op.getType()));
    m_code.push_back(id);
    m_code.push_back(getIdForDef(descriptorOp.getDef()));
    m_code.push_back(getIdForDef(addressDef));
    imageOperands.pushTo(m_code);
  }

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitBufferStore(const ir::Op& op) {
  /* Get op that loaded the descriptor, and the resource declaration */
  const auto& descriptorOp = m_builder.getOp(ir::SsaDef(op.getOperand(0u)));
  const auto& dclOp = m_builder.getOp(ir::SsaDef(descriptorOp.getOperand(0u)));

  const auto& valueOp = m_builder.getOp(ir::SsaDef(op.getOperand(2u)));

  auto addressDef = ir::SsaDef(op.getOperand(1u));
  auto uavFlags = getUavFlags(dclOp);

  if (declaresPlainBufferResource(dclOp)) {
    auto addressedType = traverseType(dclOp.getType(), addressDef);
    auto accessType = valueOp.getType();

    dxbc_spv_assert(addressedType == accessType ||
      (addressedType.isScalarType() && accessType.isVectorType()));

    /* Set up memory operands depending on resource usage */
    SpirvMemoryOperands memoryOperands = { };

    if (!(uavFlags & ir::UavFlag::eWriteOnly)) {
      memoryOperands.flags |= spv::MemoryAccessNonPrivatePointerMask
                           |  spv::MemoryAccessMakePointerAvailableMask;
      memoryOperands.makeAvailable = makeConstU32((uavFlags & ir::UavFlag::eCoherent)
        ? spv::ScopeQueueFamily : spv::ScopeWorkgroup);
    }

    /* Pairs of scalarized access chains and values to store */
    util::small_vector<std::pair<uint32_t, uint32_t>, 4u> storeIds;

    bool hasWrapperStruct = !dclOp.getType().isStructType();

    if (m_options.nvRawAccessChains && descriptorOp.getType() != ir::ScalarType::eCbv) {
      auto accessChainId = emitRawAccessChainNv(spv::StorageClassStorageBuffer,
        accessType, dclOp, getIdForDef(descriptorOp.getDef()), addressDef);

      memoryOperands.flags |= spv::MemoryAccessAlignedMask;
      memoryOperands.alignment = uint32_t(op.getOperand(3u));

      storeIds.push_back(std::make_pair(accessChainId, getIdForDef(valueOp.getDef())));
    } else if (addressedType == accessType) {
      /* Trivial case, we can emit a single store */
      auto accessChainId = emitAccessChain(spv::StorageClassStorageBuffer, dclOp.getType(),
        getIdForDef(descriptorOp.getDef()), addressDef, 0u, hasWrapperStruct);

      storeIds.push_back(std::make_pair(accessChainId, getIdForDef(valueOp.getDef())));
    } else {
      /* Extract scalars from vector and scalarize access chains */
      auto valueId = getIdForDef(valueOp.getDef());

      for (uint32_t i = 0u; i < accessType.getBaseType(0u).getVectorSize(); i++) {
        auto accessChainId = emitAccessChain(spv::StorageClassStorageBuffer, dclOp.getType(),
          getIdForDef(descriptorOp.getDef()), addressDef, i, hasWrapperStruct);
        auto componentId = allocId();

        pushOp(m_code, spv::OpCompositeExtract,
          getIdForType(addressedType), componentId, valueId, i);

        storeIds.push_back(std::make_pair(accessChainId, componentId));
      }
    }

    /* Emit stores and decorate access chains as nonuniform if necessary */
    for (const auto& e : storeIds) {
      if (descriptorOp.getFlags() & ir::OpFlag::eNonUniform)
        pushOp(m_decorations, spv::OpDecorate, e.first, spv::DecorationNonUniform);

      m_code.push_back(makeOpcodeToken(spv::OpStore, 3u + memoryOperands.computeDwordCount()));
      m_code.push_back(e.first);
      m_code.push_back(e.second);
      memoryOperands.pushTo(m_code);
    }
  } else {
    /* Set up image operands analogous to the memory operands above */
    SpirvImageOperands imageOperands = { };
    setUavImageWriteOperands(imageOperands, dclOp);

    /* Emit actual image store */
    m_code.push_back(makeOpcodeToken(spv::OpImageWrite, 4u + imageOperands.computeDwordCount()));
    m_code.push_back(getIdForDef(descriptorOp.getDef()));
    m_code.push_back(getIdForDef(addressDef));
    m_code.push_back(getIdForDef(valueOp.getDef()));
    imageOperands.pushTo(m_code);
  }
}


void SpirvBuilder::emitBufferQuery(const ir::Op& op) {
  /* Get op that loaded the descriptor, and the resource declaration */
  const auto& descriptorOp = m_builder.getOp(ir::SsaDef(op.getOperand(0u)));
  const auto& dclOp = m_builder.getOp(ir::SsaDef(descriptorOp.getOperand(0u)));

  auto id = getIdForDef(op.getDef());

  if (declaresPlainBufferResource(dclOp)) {
    pushOp(m_code, spv::OpArrayLength,
      getIdForType(op.getType()), id,
      getIdForDef(descriptorOp.getDef()), 0u);
  } else {
    enableCapability(spv::CapabilityImageQuery);

    pushOp(m_code, spv::OpImageQuerySize,
      getIdForType(op.getType()), id,
      getIdForDef(descriptorOp.getDef()));
  }
}


void SpirvBuilder::emitBufferAtomic(const ir::Op& op) {
  /* Get op that loaded the descriptor, and the resource declaration */
  const auto& descriptorOp = m_builder.getOp(ir::SsaDef(op.getOperand(0u)));
  const auto& dclOp = m_builder.getOp(ir::SsaDef(descriptorOp.getOperand(0u)));

  auto addressDef = ir::SsaDef(op.getOperand(1u));

  if (declaresPlainBufferResource(dclOp)) {
    auto type = traverseType(dclOp.getType(), addressDef);

    dxbc_spv_assert(type.isScalarType());

    /* Emit access chain, this will always point to a scalar. */
    bool hasWrapperStruct = !dclOp.getType().isStructType();

    auto accessChainId = emitAccessChain(spv::StorageClassStorageBuffer, dclOp.getType(),
      getIdForDef(descriptorOp.getDef()), addressDef, 0u, hasWrapperStruct);

    if (descriptorOp.getFlags() & ir::OpFlag::eNonUniform)
      pushOp(m_decorations, spv::OpDecorate, accessChainId, spv::DecorationNonUniform);

    emitAtomic(op, type, 2u, accessChainId, spv::ScopeQueueFamily,
      spv::MemorySemanticsUniformMemoryMask);
  } else {
    /* OpImageTexelPointer is annoying and takes a pointer to an image descriptor,
     * rather than an actually loaded image descriptor, so we have to re-evaluate
     * the descriptor load op here. */
    auto type = dclOp.getType();

    auto ptrTypeId = getIdForPtrType(getIdForType(type), spv::StorageClassImage);
    auto ptrId = allocId();

    pushOp(m_code, spv::OpImageTexelPointer, ptrTypeId, ptrId,
      getImageDescriptorPointer(descriptorOp), getIdForDef(addressDef),
      makeConstU32(0u));

    /* Need to declare the pointer op we pass to the atomic as non-uniform */
    if (descriptorOp.getFlags() & ir::OpFlag::eNonUniform)
      pushOp(m_decorations, spv::OpDecorate, ptrId, spv::DecorationNonUniform);

    emitAtomic(op, type, 2u, ptrId, spv::ScopeQueueFamily,
      spv::MemorySemanticsImageMemoryMask);
  }
}


void SpirvBuilder::emitCounterAtomic(const ir::Op& op) {
  const auto& descriptorOp = m_builder.getOp(ir::SsaDef(op.getOperand(0u)));
  const auto& dclOp = m_builder.getOp(ir::SsaDef(descriptorOp.getOperand(0u)));

  auto accessChainId = emitAccessChain(spv::StorageClassStorageBuffer,
    dclOp.getType(), getIdForDef(descriptorOp.getDef()), ir::SsaDef(), 0u, true);

  if (descriptorOp.getFlags() & ir::OpFlag::eNonUniform)
    pushOp(m_decorations, spv::OpDecorate, accessChainId, spv::DecorationNonUniform);

  emitAtomic(op, dclOp.getType(), 1u, accessChainId, spv::ScopeQueueFamily,
    spv::MemorySemanticsUniformMemoryMask);
}


uint32_t SpirvBuilder::emitMergeImageCoordLayer(const ir::SsaDef& coordDef, const ir::SsaDef& layerDef) {
  if (!layerDef)
    return getIdForDef(coordDef);

  const auto& coordOp = m_builder.getOp(coordDef);
  const auto& layerOp = m_builder.getOp(layerDef);

  dxbc_spv_assert(coordOp.getType().isBasicType());
  dxbc_spv_assert(layerOp.getType().isScalarType());

  /* SPIR-V explicitly allows concatenating vectors with CompositeConstruct */
  auto coordType = coordOp.getType().getBaseType(0u);

  auto mergedType = ir::BasicType(coordType.getBaseType(), coordType.getVectorSize() + 1u);
  auto mergedId = allocId();

  pushOp(m_code, spv::OpCompositeConstruct,
    getIdForType(mergedType), mergedId,
    getIdForDef(coordOp.getDef()),
    getIdForDef(layerOp.getDef()));

  return mergedId;
}


void SpirvBuilder::emitImageLoad(const ir::Op& op) {
  const auto& descriptorOp = m_builder.getOp(ir::SsaDef(op.getOperand(0u)));
  const auto& dclOp = m_builder.getOp(ir::SsaDef(descriptorOp.getOperand(0u)));

  auto id = getIdForDef(op.getDef());

  bool isUav = descriptorOp.getType() == ir::ScalarType::eUav;
  bool isSparse = op.getFlags() & ir::OpFlag::eSparseFeedback;

  if (isSparse)
    enableCapability(spv::CapabilitySparseResidency);

  /* Set up image operands */
  SpirvImageOperands imageOperands = { };

  if (isUav)
    setUavImageReadOperands(imageOperands, dclOp);

  auto mipDef = ir::SsaDef(op.getOperand(1u));

  if (mipDef) {
    imageOperands.flags |= spv::ImageOperandsLodMask;
    imageOperands.lodIndex = getIdForDef(mipDef);
  }

  auto sampleDef = ir::SsaDef(op.getOperand(4u));

  if (sampleDef) {
    imageOperands.flags |= spv::ImageOperandsSampleMask;
    imageOperands.sampleId = getIdForDef(sampleDef);
  }

  auto offsetDef = ir::SsaDef(op.getOperand(5u));

  if (offsetDef) {
    imageOperands.flags |= spv::ImageOperandsConstOffsetMask;
    imageOperands.constOffset = getIdForDef(offsetDef);
  }

  /* Build final coordinate vector */
  auto coordId = emitMergeImageCoordLayer(
    ir::SsaDef(op.getOperand(3u)),
    ir::SsaDef(op.getOperand(2u)));

  /* Select correct opcode */
  auto opCode = isUav
    ? (isSparse ? spv::OpImageSparseRead : spv::OpImageRead)
    : (isSparse ? spv::OpImageSparseFetch : spv::OpImageFetch);

  m_code.push_back(makeOpcodeToken(opCode, 5u + imageOperands.computeDwordCount()));
  m_code.push_back(getIdForType(op.getType()));
  m_code.push_back(id);
  m_code.push_back(getIdForDef(descriptorOp.getDef()));
  m_code.push_back(coordId);
  imageOperands.pushTo(m_code);

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitImageQuerySize(const ir::Op& op) {
  enableCapability(spv::CapabilityImageQuery);

  const auto& descriptorOp = m_builder.getOp(ir::SsaDef(op.getOperand(0u)));
  const auto& dclOp = m_builder.getOp(ir::SsaDef(descriptorOp.getOperand(0u)));

  auto kind = getResourceKind(dclOp);

  /* Use image dimension rather than component counts, they differ for cubes */
  uint32_t sizeDimensions = ir::resourceDimensions(kind);

  auto sizeType = op.getType().getSubType(0u);
  dxbc_spv_assert(sizeType.isBasicType());

  auto scalarType = sizeType.getBaseType(0u).getBaseType();
  auto layerType = op.getType().getSubType(1u);
  dxbc_spv_assert(layerType == scalarType);

  /* SPIR-V returns a vector with the layer count in the last component. We also
   * need to specify the mip level to query for resources that can support mips. */
  uint32_t queryComponents = sizeDimensions + (resourceIsLayered(kind) ? 1u : 0u);

  auto queryTypeId = getIdForType(ir::BasicType(sizeType.getBaseType(0u).getBaseType(), queryComponents));
  auto queryId = allocId();

  if (descriptorOp.getType() == ir::ScalarType::eSrv && !resourceIsMultisampled(kind)) {
    auto mipDef = ir::SsaDef(op.getOperand(1u));

    dxbc_spv_assert(mipDef);

    pushOp(m_code, spv::OpImageQuerySizeLod, queryTypeId, queryId,
      getIdForDef(descriptorOp.getDef()), getIdForDef(mipDef));
  } else {
    pushOp(m_code, spv::OpImageQuerySize,
      queryTypeId, queryId, getIdForDef(descriptorOp.getDef()));
  }

  uint32_t sizeId, layerId;

  if (resourceIsLayered(kind)) {
    /* Extract size vector */
    auto sizeTypeId = getIdForType(sizeType);
    sizeId = allocId();

    if (sizeDimensions > 1u) {
      m_code.push_back(makeOpcodeToken(spv::OpVectorShuffle, 5u + sizeDimensions));
      m_code.push_back(sizeTypeId);
      m_code.push_back(sizeId);
      m_code.push_back(queryId);
      m_code.push_back(queryId);

      for (uint32_t i = 0u; i < sizeDimensions; i++)
        m_code.push_back(i);
    } else {
      pushOp(m_code, spv::OpCompositeExtract, sizeTypeId,
        sizeId, queryId, 0u);
    }

    /* Extract layer count */
    auto layerTypeId = getIdForType(layerType);
    layerId = allocId();

    pushOp(m_code, spv::OpCompositeExtract, layerTypeId,
      layerId, queryId, sizeDimensions);
  } else {
    /* Assign constant 1 as the layer count and use size as-is */
    auto layerTypeId = getIdForType(layerType);

    SpirvConstant constant = { };
    constant.op = spv::OpConstant;
    constant.typeId = layerTypeId;
    constant.constituents[0u] = 1u;

    sizeId = queryId;
    layerId = getIdForConstant(constant, 1u);
  }

  /* Assemble final result struct */
  auto resultTypeId = getIdForType(op.getType());
  auto id = getIdForDef(op.getDef());

  pushOp(m_code, spv::OpCompositeConstruct,
    resultTypeId, id, sizeId, layerId);

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitImageQueryMips(const ir::Op& op) {
  enableCapability(spv::CapabilityImageQuery);

  const auto& descriptorOp = m_builder.getOp(ir::SsaDef(op.getOperand(0u)));

  dxbc_spv_assert(descriptorOp.getType() == ir::ScalarType::eSrv);
  dxbc_spv_assert(!resourceIsMultisampled(getResourceKind(
    m_builder.getOp(ir::SsaDef(descriptorOp.getOperand(0u))))));

  auto id = getIdForDef(op.getDef());

  pushOp(m_code, spv::OpImageQueryLevels,
    getIdForType(op.getType()), id,
    getIdForDef(descriptorOp.getDef()));

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitImageQuerySamples(const ir::Op& op) {
  enableCapability(spv::CapabilityImageQuery);

  const auto& descriptorOp = m_builder.getOp(ir::SsaDef(op.getOperand(0u)));

  dxbc_spv_assert(resourceIsMultisampled(getResourceKind(
    m_builder.getOp(ir::SsaDef(descriptorOp.getOperand(0u))))));

  auto id = getIdForDef(op.getDef());

  pushOp(m_code, spv::OpImageQuerySamples,
    getIdForType(op.getType()), id,
    getIdForDef(descriptorOp.getDef()));

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitConvert(const ir::Op& op) {
  /* Everything can be a vector here */
  dxbc_spv_assert(op.getType().isBasicType());

  const auto& srcOp = m_builder.getOp(ir::SsaDef(op.getOperand(0u)));
  dxbc_spv_assert(srcOp.getType().isBasicType());

  auto srcId = getIdForDef(srcOp.getDef());

  spv::Op spvOp = [&] {
    const auto& dstType = op.getType().getBaseType(0u);
    const auto& srcType = srcOp.getType().getBaseType(0u);

    switch (op.getOpCode()) {
      case ir::OpCode::eCast:
        return spv::OpBitcast;

      case ir::OpCode::eConvertFtoF:
        return spv::OpFConvert;

      case ir::OpCode::eConvertFtoI:
        return dstType.isSignedIntType()
          ? spv::OpConvertFToS
          : spv::OpConvertFToU;

      case ir::OpCode::eConvertItoF:
        return srcType.isSignedIntType()
          ? spv::OpConvertSToF
          : spv::OpConvertUToF;

      case ir::OpCode::eConvertItoI: {
        if (srcType.byteSize() == dstType.byteSize())
          return spv::OpBitcast;

        if (srcType.isSignedIntType())
          return spv::OpSConvert;

        if (dstType.isUnsignedIntType())
          return spv::OpUConvert;

        /* Cursed case where we do a zero-extension on a signed type. SPIR-V requires
         * OpUConvert to have an unsigned destination type, so we need to insert a
         * signed conversion into a bitcast here. */
        ir::ScalarType unsignedType = dstType.getBaseType();

        switch (dstType.getBaseType()) {
          case ir::ScalarType::eI8:  unsignedType = ir::ScalarType::eU8; break;
          case ir::ScalarType::eI16: unsignedType = ir::ScalarType::eU16; break;
          case ir::ScalarType::eI32: unsignedType = ir::ScalarType::eU32; break;
          case ir::ScalarType::eI64: unsignedType = ir::ScalarType::eU64; break;
          default:
            dxbc_spv_unreachable();
        }

        auto tmpTypeId = getIdForType(ir::BasicType(unsignedType, srcType.getVectorSize()));
        auto tmpId = allocId();

        pushOp(m_code, spv::OpUConvert, tmpTypeId, tmpId, srcId);

        srcId = tmpId;
        return spv::OpBitcast;
      }

      default:
        dxbc_spv_unreachable();
        return spv::Op();
    }
  } ();

  auto dstTypeId = getIdForType(op.getType());
  auto dstId = getIdForDef(op.getDef());

  pushOp(m_code, spvOp, dstTypeId, dstId, srcId);
}


void SpirvBuilder::emitAtomic(const ir::Op& op, const ir::Type& type, uint32_t operandIndex,
    uint32_t ptrId, spv::Scope scope, spv::MemorySemanticsMask memoryTypes) {
  dxbc_spv_assert(op.getType().isVoidType() || op.getType() == type);

  auto id = getIdForDef(op.getDef());

  /* Work out SPIR-V op code and argument count based on atomic op literal */
  auto atomicOp = ir::AtomicOp(op.getOperand(op.getFirstLiteralOperandIndex()));

  auto [opCode, argCount] = [&] {
    switch (atomicOp) {
      case ir::AtomicOp::eLoad:             return std::make_pair(spv::OpAtomicLoad, 0u);
      case ir::AtomicOp::eStore:            return std::make_pair(spv::OpAtomicStore, 1u);
      case ir::AtomicOp::eExchange:         return std::make_pair(spv::OpAtomicExchange, 1u);
      case ir::AtomicOp::eCompareExchange:  return std::make_pair(spv::OpAtomicCompareExchange, 2u);
      case ir::AtomicOp::eAdd:              return std::make_pair(spv::OpAtomicIAdd, 1u);
      case ir::AtomicOp::eSub:              return std::make_pair(spv::OpAtomicISub, 1u);
      case ir::AtomicOp::eSMin:             return std::make_pair(spv::OpAtomicSMin, 1u);
      case ir::AtomicOp::eSMax:             return std::make_pair(spv::OpAtomicSMax, 1u);
      case ir::AtomicOp::eUMin:             return std::make_pair(spv::OpAtomicUMin, 1u);
      case ir::AtomicOp::eUMax:             return std::make_pair(spv::OpAtomicUMax, 1u);
      case ir::AtomicOp::eAnd:              return std::make_pair(spv::OpAtomicAnd, 1u);
      case ir::AtomicOp::eOr:               return std::make_pair(spv::OpAtomicOr, 1u);
      case ir::AtomicOp::eXor:              return std::make_pair(spv::OpAtomicXor, 1u);
      case ir::AtomicOp::eInc:              return std::make_pair(spv::OpAtomicIIncrement, 0u);
      case ir::AtomicOp::eDec:              return std::make_pair(spv::OpAtomicIDecrement, 0u);
    }

    dxbc_spv_unreachable();
    return std::make_pair(spv::OpNop, 0u);
  } ();

  dxbc_spv_assert(operandIndex + argCount == op.getFirstLiteralOperandIndex());

  /* Set up memory semantics */
  auto semantics = spv::MemorySemanticsAcquireReleaseMask;

  if (atomicOp == ir::AtomicOp::eLoad)
    semantics = spv::MemorySemanticsAcquireMask;
  else if (atomicOp == ir::AtomicOp::eStore)
    semantics = spv::MemorySemanticsReleaseMask;

  /* Emit atomic instruction */
  uint32_t operandCount = 4u + argCount;

  if (atomicOp != ir::AtomicOp::eStore)
    operandCount += 2u;  /* result type and id */

  if (atomicOp == ir::AtomicOp::eCompareExchange)
    operandCount += 1u;  /* unequal semantics */

  m_code.push_back(makeOpcodeToken(opCode, operandCount));

  if (atomicOp != ir::AtomicOp::eStore) {
    m_code.push_back(getIdForType(type));
    m_code.push_back(id);
  }

  m_code.push_back(ptrId);
  m_code.push_back(makeConstU32(scope));
  m_code.push_back(makeConstU32(memoryTypes | semantics));

  if (atomicOp == ir::AtomicOp::eCompareExchange)
    m_code.push_back(makeConstU32(memoryTypes | spv::MemorySemanticsAcquireMask));

  for (uint32_t i = 0u; i < argCount; i++)
    m_code.push_back(getIdForDef(ir::SsaDef(op.getOperand(operandIndex + i))));

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitLabel(const ir::Op& op) {
  m_structure.blockLabel = op;

  pushOp(m_code, spv::OpLabel, getIdForDef(op.getDef()));
}


void SpirvBuilder::emitBranch(const ir::Op& op) {
  pushOp(m_code, spv::OpBranch, getIdForDef(ir::SsaDef(op.getOperand(0u))));

  m_structure.blockLabel = ir::Op();
}


void SpirvBuilder::emitBranchConditional(const ir::Op& op) {
  emitStructuredInfo(m_structure.blockLabel);

  pushOp(m_code, spv::OpBranchConditional,
    getIdForDef(ir::SsaDef(op.getOperand(0u))),
    getIdForDef(ir::SsaDef(op.getOperand(1u))),
    getIdForDef(ir::SsaDef(op.getOperand(2u))));

  m_structure.blockLabel = ir::Op();
}


void SpirvBuilder::emitSwitch(const ir::Op& op) {
  emitStructuredInfo(m_structure.blockLabel);

  m_code.push_back(makeOpcodeToken(spv::OpSwitch, 1u + op.getOperandCount()));
  m_code.push_back(getIdForDef(ir::SsaDef(op.getOperand(0u))));
  m_code.push_back(getIdForDef(ir::SsaDef(op.getOperand(1u))));

  for (uint32_t i = 2u; i < op.getOperandCount(); i += 2u) {
    const auto& constant = m_builder.getOp(ir::SsaDef(op.getOperand(i)));
    dxbc_spv_assert(constant.isConstant());

    m_code.push_back(uint32_t(constant.getOperand(0u)));
    m_code.push_back(getIdForDef(ir::SsaDef(op.getOperand(i + 1u))));
  }

  m_structure.blockLabel = ir::Op();
}


void SpirvBuilder::emitUnreachable() {
  pushOp(m_code, spv::OpUnreachable);

  m_structure.blockLabel = ir::Op();
}


void SpirvBuilder::emitReturn(const ir::Op& op) {
  if (op.getType().isVoidType()) {
    pushOp(m_code, spv::OpReturn);
  } else {
    pushOp(m_code, spv::OpReturnValue,
      getIdForType(op.getType()),
      getIdForDef(ir::SsaDef(op.getOperand(0u))));
  }

  m_structure.blockLabel = ir::Op();
}


void SpirvBuilder::emitPhi(const ir::Op& op) {
  emitStructuredInfo(m_structure.blockLabel);

  m_code.push_back(makeOpcodeToken(spv::OpPhi, 2u + op.getOperandCount()));
  m_code.push_back(getIdForType(op.getType()));
  m_code.push_back(getIdForDef(op.getDef()));

  /* Phi operands are the other way around */
  for (uint32_t i = 0u; i < op.getOperandCount(); i += 2u) {
    m_code.push_back(getIdForDef(ir::SsaDef(op.getOperand(i + 1u))));
    m_code.push_back(getIdForDef(ir::SsaDef(op.getOperand(i))));
  }

  m_structure.blockLabel = ir::Op();
}


void SpirvBuilder::emitStructuredInfo(const ir::Op& op) {
  auto construct = ir::Construct(op.getFirstLiteralOperandIndex());

  switch (construct) {
    case ir::Construct::eNone:
      return;

    case ir::Construct::eStructuredSelection: {
      pushOp(m_code, spv::OpSelectionMerge,
        getIdForDef(ir::SsaDef(op.getOperand(0u))), 0u);
    } break;

    case ir::Construct::eStructuredLoop: {
      pushOp(m_code, spv::OpLoopMerge,
        getIdForDef(ir::SsaDef(op.getOperand(0u))),
        getIdForDef(ir::SsaDef(op.getOperand(1u))), 0u);
    } break;
  }
}


void SpirvBuilder::emitMemoryModel() {
  enableCapability(spv::CapabilityShader);
  enableCapability(spv::CapabilityPhysicalStorageBufferAddresses);
  enableCapability(spv::CapabilityVulkanMemoryModel);

  pushOp(m_memoryModel, spv::OpMemoryModel,
    spv::AddressingModelPhysicalStorageBuffer64,
    spv::MemoryModelVulkan);
}


void SpirvBuilder::emitDebugName(ir::SsaDef def, uint32_t id) {
  if (!m_options.includeDebugNames)
    return;

  auto e = m_debugNames.find(def);

  if (e == m_debugNames.end())
    return;

  const auto& debugOp = m_builder.getOp(e->second);
  dxbc_spv_assert(debugOp.getOpCode() == ir::OpCode::eDebugName);

  setDebugName(id, debugOp.getLiteralString(1u).c_str());
}


void SpirvBuilder::emitFunction(const ir::Op& op) {
  uint32_t funcId = getIdForDef(op.getDef());
  emitDebugName(op.getDef(), funcId);

  /* Look up unique function type */
  SpirvFunctionTypeKey typeKey = { };
  typeKey.returnType = op.getType();

  for (uint32_t i = 0u; i < op.getOperandCount(); i++) {
    const auto& paramDef = m_builder.getOp(ir::SsaDef(op.getOperand(i)));
    dxbc_spv_assert(paramDef.getOpCode() == ir::OpCode::eDclParam);

    typeKey.paramTypes.push_back(paramDef.getType());
  }

  /* Emit function declaration */
  pushOp(m_code, spv::OpFunction,
    getIdForType(typeKey.returnType),
    funcId, 0u, getIdForFuncType(typeKey));

  /* Emit function parameters */
  for (uint32_t i = 0u; i < op.getOperandCount(); i++) {
    auto paramDef = ir::SsaDef(op.getOperand(i));
    auto spvId = allocId();

    emitDebugName(paramDef, spvId);

    pushOp(m_code, spv::OpFunctionParameter,
      getIdForType(typeKey.paramTypes[i]), spvId);

    SpirvFunctionParameterKey key = { };
    key.funcDef = op.getDef();
    key.paramDef = paramDef;

    m_funcParamIds.insert({ key, spvId });
  }
}


void SpirvBuilder::emitFunctionEnd() {
  pushOp(m_code, spv::OpFunctionEnd);
}


uint32_t SpirvBuilder::emitAddressOffset(ir::SsaDef def, uint32_t offset) {
  if (!offset)
    return getIdForDef(def);

  const auto& op = m_builder.getOp(def);
  dxbc_spv_assert(op.getType().isScalarType());

  if (op.isConstant())
    return makeConstU32(uint32_t(op.getOperand(0u)) + offset);

  uint32_t typeId = getIdForType(op.getType());
  uint32_t id = allocId();

  pushOp(m_code, spv::OpIAdd, typeId, id, getIdForDef(def), makeConstU32(offset));
  return id;
}


uint32_t SpirvBuilder::emitAccessChain(spv::StorageClass storageClass, const ir::Type& baseType,
    uint32_t baseId, ir::SsaDef address, uint32_t offset, bool wrapperStruct) {
  if (!address && !wrapperStruct)
    return baseId;

  /* Declare resulting pointer type */
  auto pointeeType = traverseType(baseType, address);
  auto ptrTypeId = getIdForPtrType(getIdForType(pointeeType), storageClass);

  /* Allocate access chain */
  auto accessChainId = allocId();

  /* Ensure that the address is an integer scalar or vector.
   * We already validated everything when traversing the type. */
  const auto& addressOp = m_builder.getOp(address);
  auto addressType = addressOp.getType().getBaseType(0u);

  /* Number of operands for access chains */
  util::small_vector<uint32_t, 5u> indexIds;

  if (wrapperStruct)
    indexIds.push_back(makeConstU32(0u));

  if (!address) {
    /* Nothing to do in this case, just unwrap the struct */
  } else if (addressType.isScalar()) {
    /* Scalar operand, can use directly no matter what it is. */
    indexIds.push_back(emitAddressOffset(address, offset));
  } else if (addressOp.isConstant()) {
    /* Unroll constant operands if possible */
    for (uint32_t i = 0u; i < addressType.getVectorSize(); i++) {
      auto constantValue = uint32_t(addressOp.getOperand(i));

      if (i + 1u == addressType.getVectorSize())
        constantValue += offset;

      indexIds.push_back(makeConstU32(constantValue));
    }
  } else if (addressOp.getOpCode() == ir::OpCode::eCompositeConstruct) {
    /* Unroll vector operands if possible */
    for (uint32_t i = 0u; i < addressType.getVectorSize(); i++) {
      auto def = ir::SsaDef(addressOp.getOperand(i));

      uint32_t id = (i + 1u == addressType.getVectorSize())
        ? emitAddressOffset(def, offset)
        : getIdForDef(def);

      indexIds.push_back(id);
    }
  } else {
    /* Dynamically extract vector components */
    auto scalarTypeId = getIdForType(addressType.getBaseType());
    auto addressId = getIdForDef(address);

    for (uint32_t i = 0u; i < addressType.getVectorSize(); i++) {
      uint32_t componentId = allocId();
      pushOp(m_code, spv::OpCompositeExtract, scalarTypeId, componentId, addressId, i);

      if (i + 1u == addressType.getVectorSize() && offset) {
        auto offsetId = allocId();

        pushOp(m_code, spv::OpIAdd, scalarTypeId, offsetId,
          componentId, makeConstU32(offset));

        indexIds.push_back(offsetId);
      } else {
        indexIds.push_back(componentId);
      }
    }
  }

  /* Emit actual access chain instruction */
  m_code.push_back(makeOpcodeToken(spv::OpAccessChain, 4u + indexIds.size()));
  m_code.push_back(ptrTypeId);
  m_code.push_back(accessChainId);
  m_code.push_back(baseId);

    for (auto index : indexIds)
      m_code.push_back(index);

  return accessChainId;
}


uint32_t SpirvBuilder::emitRawStrutcuredElementAddress(const ir::Op& op, uint32_t depth, uint32_t stride) {
  dxbc_spv_assert(op.getType().isBasicType());

  if (op.isConstant())
    return makeConstU32(uint32_t(op.getOperand(depth)) * stride);

  auto baseId = 0u;

  if (op.getType().isScalarType()) {
    /* Use scalar index type as-is */
    baseId = getIdForDef(op.getDef());
  } else if (op.getOpCode() == ir::OpCode::eCompositeConstruct) {
    /* Resolve constants in composites directly */
    const auto& componentOp = m_builder.getOp((ir::SsaDef(op.getOperand(depth))));

    if (componentOp.isConstant())
      return makeConstU32(uint32_t(componentOp.getOperand(0u)) * stride);

    baseId = getIdForDef(componentOp.getDef());
  } else {
    /* Extract component */
    baseId = allocId();

    pushOp(m_code, spv::OpCompositeExtract,
      getIdForType(op.getType().getSubType(0u)), baseId,
      getIdForDef(op.getDef()), depth);
  }

  if (stride == 1u)
    return baseId;

  /* Multiply index value with stride if necessary */
  auto id = allocId();

  pushOp(m_code, spv::OpIMul,
    getIdForType(ir::ScalarType::eU32), id,
    baseId, makeConstU32(stride));

  return id;
}


uint32_t SpirvBuilder::emitRawAccessChainNv(spv::StorageClass storageClass, const ir::Type& type, const ir::Op& resourceOp, uint32_t baseId, ir::SsaDef address) {
  enableCapability(spv::CapabilityRawAccessChainsNV);

  const auto& addressOp = m_builder.getOp(address);

  /* We can load and store any suitable scalar or vector type directly */
  auto ptrTypeId = getIdForPtrType(getIdForType(type), storageClass);
  auto id = allocId();

  /* Resource declarations must be an array or nested array */
  dxbc_spv_assert(resourceOp.getType().isUnboundedArray());

  auto kind = getResourceKind(resourceOp);

  if (kind == ir::ResourceKind::eBufferRaw) {
    dxbc_spv_assert(resourceOp.getType().getSubType(0u).isScalarType());
    dxbc_spv_assert(addressOp.getType().isScalarType());

    /* Compute byte offset from the given element index */
    auto byteOffsetId = emitRawStrutcuredElementAddress(addressOp, 0u,
      resourceOp.getType().getSubType(0u).byteSize());

    auto nullId = makeConstU32(0u);

    pushOp(m_code, spv::OpRawAccessChainNV, ptrTypeId,
      id, baseId, nullId, nullId, byteOffsetId,
      spv::RawAccessChainOperandsRobustnessPerComponentNVMask);
  } else if (kind == ir::ResourceKind::eBufferStructured) {
    dxbc_spv_assert(addressOp.getType().isVectorType());

    /* Compute byte size of the structure based on the resource type */
    auto structureType = resourceOp.getType().getSubType(0u);
    dxbc_spv_assert(structureType.isSizedArray());

    auto strideId = makeConstU32(structureType.byteSize());

    /* Compute structure index and byte offset into the structure */
    auto elementId = emitRawStrutcuredElementAddress(addressOp, 0u, 1u);
    auto byteOffsetId = emitRawStrutcuredElementAddress(addressOp, 1u,
      structureType.getSubType(0u).byteSize());

    pushOp(m_code, spv::OpRawAccessChainNV, ptrTypeId,
      id, baseId, strideId, elementId, byteOffsetId,
      spv::RawAccessChainOperandsRobustnessPerElementNVMask);
  } else {
    /* Invalid kind */
    dxbc_spv_unreachable();
  }

  return id;
}


uint32_t SpirvBuilder::emitAccessChain(spv::StorageClass storageClass, ir::SsaDef base, ir::SsaDef address) {
  const auto& baseOp = m_builder.getOp(base);

  return emitAccessChain(storageClass, baseOp.getType(), getIdForDef(baseOp.getDef()), address, 0u, false);
}


void SpirvBuilder::emitCheckSparseAccess(const ir::Op& op) {
  auto id = getIdForDef(op.getDef());

  pushOp(m_code, spv::OpImageSparseTexelsResident,
    getIdForType(op.getType()), id,
    getIdForDef(ir::SsaDef(op.getOperand(0u))));

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitLoadDrawParameterBuiltIn(const ir::Op& op, ir::BuiltIn builtIn) {
  /* Can't index into scalar built-ins */
  dxbc_spv_assert(!ir::SsaDef(op.getOperand(1u)));

  /* The actual built-ins are declared as signed integers */
  auto intTypeId = getIdForType(ir::ScalarType::eI32);

  auto indexId = allocId();
  auto baseId = allocId();

  pushOp(m_code, spv::OpLoad, intTypeId, indexId, getIdForDef(ir::SsaDef(op.getOperand(0u))));
  pushOp(m_code, spv::OpLoad, intTypeId, baseId, builtIn == ir::BuiltIn::eVertexId
    ? m_drawParams.baseVertex : m_drawParams.baseInstance);

  /* Subtract base index and return the correct type */
  auto resultTypeId = getIdForType(op.getType());
  auto id = getIdForDef(op.getDef());

  pushOp(m_code, spv::OpISub, resultTypeId, id, indexId, baseId);
}


void SpirvBuilder::emitLoadVariable(const ir::Op& op) {
  auto typeId = getIdForType(op.getType());

  /* Loading draw parameter built-ins requires special care */
  if (op.getOpCode() == ir::OpCode::eInputLoad) {
    const auto& inputDcl = m_builder.getOp(ir::SsaDef(op.getOperand(0u)));

    if (inputDcl.getOpCode() == ir::OpCode::eDclInputBuiltIn) {
      auto builtIn = ir::BuiltIn(inputDcl.getOperand(1u));

      if (builtIn == ir::BuiltIn::eVertexId || builtIn == ir::BuiltIn::eInstanceId) {
        emitLoadDrawParameterBuiltIn(op, builtIn);
        return;
      }
    }
  }

  /* Load regular variable */
  auto accessChainId = emitAccessChain(
    getVariableStorageClass(op),
    ir::SsaDef(op.getOperand(0u)),
    ir::SsaDef(op.getOperand(1u)));

  auto id = getIdForDef(op.getDef());

  SpirvMemoryOperands memoryOperands = { };

  if (op.getOpCode() == ir::OpCode::eLdsLoad) {
    memoryOperands.flags |= spv::MemoryAccessNonPrivatePointerMask
                         |  spv::MemoryAccessMakePointerVisibleMask;
    memoryOperands.makeVisible = makeConstU32(spv::ScopeWorkgroup);
  }

  m_code.push_back(makeOpcodeToken(spv::OpLoad, 4u + memoryOperands.computeDwordCount()));
  m_code.push_back(typeId);
  m_code.push_back(id);
  m_code.push_back(accessChainId);
  memoryOperands.pushTo(m_code);

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitStoreVariable(const ir::Op& op) {
  auto accessChainId = emitAccessChain(
    getVariableStorageClass(op),
    ir::SsaDef(op.getOperand(0u)),
    ir::SsaDef(op.getOperand(1u)));

  auto valueId = getIdForDef(ir::SsaDef(op.getOperand(2u)));

  SpirvMemoryOperands memoryOperands = { };

  if (op.getOpCode() == ir::OpCode::eLdsStore) {
    memoryOperands.flags |= spv::MemoryAccessNonPrivatePointerMask
                         |  spv::MemoryAccessMakePointerAvailableMask;
    memoryOperands.makeAvailable = makeConstU32(spv::ScopeWorkgroup);
  }

  m_code.push_back(makeOpcodeToken(spv::OpStore, 3u + memoryOperands.computeDwordCount()));
  m_code.push_back(accessChainId);
  m_code.push_back(valueId);
  memoryOperands.pushTo(m_code);
}


void SpirvBuilder::emitCompositeOp(const ir::Op& op) {
  bool isInsert = op.getOpCode() == ir::OpCode::eCompositeInsert;

  const auto& addressOp = m_builder.getOp(ir::SsaDef(op.getOperand(1u)));
  dxbc_spv_assert(addressOp.isConstant() && addressOp.getType().isBasicType());

  auto typeId = getIdForType(op.getType());
  auto spvId = getIdForDef(op.getDef());

  /* Emit actual composite instruction */
  m_code.push_back(makeOpcodeToken(
    (isInsert ? spv::OpCompositeInsert : spv::OpCompositeExtract),
    (isInsert ? 5u : 4u) + addressOp.getOperandCount()));
  m_code.push_back(typeId);
  m_code.push_back(spvId);

  /* Value ID to insert */
  if (isInsert)
    m_code.push_back(getIdForDef(ir::SsaDef(op.getOperand(2u))));

  /* Composite ID */
  m_code.push_back(getIdForDef(ir::SsaDef(op.getOperand(0u))));

  /* Indexing literals */
  for (uint32_t i = 0u; i < addressOp.getOperandCount(); i++)
    m_code.push_back(uint32_t(addressOp.getOperand(i)));
}


void SpirvBuilder::emitCompositeConstruct(const ir::Op& op) {
  auto type = op.getType();

  dxbc_spv_assert(type.isVectorType() || type.isStructType());

  auto typeId = getIdForType(type);
  auto spvId = getIdForDef(op.getDef());

  m_code.push_back(makeOpcodeToken(spv::OpCompositeConstruct, 3u + op.getOperandCount()));
  m_code.push_back(typeId);
  m_code.push_back(spvId);

  for (uint32_t i = 0u; i < op.getOperandCount(); i++)
    m_code.push_back(getIdForDef(ir::SsaDef(op.getOperand(i))));
}


void SpirvBuilder::emitSimpleArithmetic(const ir::Op& op) {
  auto id = getIdForDef(op.getDef());

  /* Figure out opcode */
  spv::Op opCode = [&] {
    switch (op.getOpCode()) {
      case ir::OpCode::eFEq:          return spv::OpFOrdEqual;
      case ir::OpCode::eFNe:          return spv::OpFUnordNotEqual;
      case ir::OpCode::eFLt:          return spv::OpFOrdLessThan;
      case ir::OpCode::eFLe:          return spv::OpFOrdLessThanEqual;
      case ir::OpCode::eFGt:          return spv::OpFOrdGreaterThan;
      case ir::OpCode::eFGe:          return spv::OpFOrdGreaterThanEqual;
      case ir::OpCode::eFIsNan:       return spv::OpIsNan;
      case ir::OpCode::eIEq:          return spv::OpIEqual;
      case ir::OpCode::eINe:          return spv::OpINotEqual;
      case ir::OpCode::eSLt:          return spv::OpSLessThan;
      case ir::OpCode::eSGe:          return spv::OpSGreaterThanEqual;
      case ir::OpCode::eULt:          return spv::OpULessThan;
      case ir::OpCode::eUGe:          return spv::OpUGreaterThanEqual;
      case ir::OpCode::eBAnd:         return spv::OpLogicalAnd;
      case ir::OpCode::eBOr:          return spv::OpLogicalOr;
      case ir::OpCode::eBEq:          return spv::OpLogicalEqual;
      case ir::OpCode::eBNe:          return spv::OpLogicalNotEqual;
      case ir::OpCode::eBNot:         return spv::OpLogicalNot;
      case ir::OpCode::eSelect:       return spv::OpSelect;
      case ir::OpCode::eFNeg:         return spv::OpFNegate;
      case ir::OpCode::eFAdd:         return spv::OpFAdd;
      case ir::OpCode::eFSub:         return spv::OpFSub;
      case ir::OpCode::eFMul:         return spv::OpFMul;
      case ir::OpCode::eFDiv:         return spv::OpFDiv;
      case ir::OpCode::eIAnd:         return spv::OpBitwiseAnd;
      case ir::OpCode::eIOr:          return spv::OpBitwiseOr;
      case ir::OpCode::eIXor:         return spv::OpBitwiseXor;
      case ir::OpCode::eINot:         return spv::OpNot;
      case ir::OpCode::eIBitInsert:   return spv::OpBitFieldInsert;
      case ir::OpCode::eUBitExtract:  return spv::OpBitFieldUExtract;
      case ir::OpCode::eSBitExtract:  return spv::OpBitFieldSExtract;
      case ir::OpCode::eIShl:         return spv::OpShiftLeftLogical;
      case ir::OpCode::eSShr:         return spv::OpShiftRightArithmetic;
      case ir::OpCode::eUShr:         return spv::OpShiftRightLogical;
      case ir::OpCode::eIAdd:         return spv::OpIAdd;
      case ir::OpCode::eISub:         return spv::OpISub;
      case ir::OpCode::eINeg:         return spv::OpSNegate;
      case ir::OpCode::eIMul:         return spv::OpIMul;
      case ir::OpCode::eSDiv:         return spv::OpSDiv;
      case ir::OpCode::eUDiv:         return spv::OpUDiv;

      default:
        dxbc_spv_unreachable();
        return spv::OpNop;
    }
  } ();

  /* Emit instruction and operands as-is */
  m_code.push_back(makeOpcodeToken(opCode, 3u + op.getOperandCount()));
  m_code.push_back(getIdForType(op.getType()));
  m_code.push_back(id);

  for (uint32_t i = 0u; i < op.getOperandCount(); i++)
    m_code.push_back(getIdForDef(ir::SsaDef(op.getOperand(i))));

  if (op.getFlags() & ir::OpFlag::ePrecise)
    pushOp(m_declarations, spv::OpDecorate, id, spv::DecorationNoContraction);

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitSetCsWorkgroupSize(const ir::Op& op) {
  auto x = uint32_t(op.getOperand(1u));
  auto y = uint32_t(op.getOperand(2u));
  auto z = uint32_t(op.getOperand(3u));

  pushOp(m_executionModes, spv::OpExecutionModeId, m_entryPointId,
    spv::ExecutionModeLocalSizeId,
    makeConstU32(x), makeConstU32(y), makeConstU32(z));
}


uint32_t SpirvBuilder::importGlslExt() {
  if (!m_glslExtId) {
    m_glslExtId = allocId();

    const char* name = "GLSL.std.450";
    m_imports.push_back(makeOpcodeToken(spv::OpExtInstImport, 2u + getStringDwordCount(name)));
    m_imports.push_back(m_glslExtId);
    pushString(m_imports, name);
  }

  return m_glslExtId;
}


uint32_t SpirvBuilder::allocId() {
  return m_header.boundIds++;
}


void SpirvBuilder::setIdForDef(ir::SsaDef def, uint32_t id) {
  if (!def)
    return;

  uint32_t defId = def.getId();

  if (defId >= m_ssaDefsToId.size())
    m_ssaDefsToId.resize(defId + 1u);

  m_ssaDefsToId[defId] = id;
}


uint32_t SpirvBuilder::getIdForDef(ir::SsaDef def) {
  if (!def)
    return 0u;

  uint32_t defId = def.getId();

  if (defId >= m_ssaDefsToId.size())
    m_ssaDefsToId.resize(defId + 1u);

  auto& spirvId = m_ssaDefsToId[defId];

  if (!spirvId)
    spirvId = allocId();

  return spirvId;
}


uint32_t SpirvBuilder::getIdForType(const ir::Type& type) {
  auto entry = m_types.find(type);

  if (entry != m_types.end())
    return entry->second;

  uint32_t id = defType(type, false);
  m_types.insert({ type, id });
  return id;
}


uint32_t SpirvBuilder::defType(const ir::Type& type, bool explicitLayout) {
  auto id = allocId();

  if (type.isVoidType()) {
    pushOp(m_declarations, spv::OpTypeVoid, id);
    return id;
  }

  if (type.isScalarType()) {
    uint32_t sign = type.getBaseType(0u).isSignedIntType() ? 1u : 0u;

    switch (type.getBaseType(0u).getBaseType()) {
      case ir::ScalarType::eBool: {
        pushOp(m_declarations, spv::OpTypeBool, id);
      } return id;

      case ir::ScalarType::eI8:
      case ir::ScalarType::eU8: {
        enableCapability(spv::CapabilityInt8);
        pushOp(m_declarations, spv::OpTypeInt, id, 8u, sign);
      } return id;

      case ir::ScalarType::eI16:
      case ir::ScalarType::eU16: {
        enableCapability(spv::CapabilityInt16);
        pushOp(m_declarations, spv::OpTypeInt, id, 16u, sign);
      } return id;

      case ir::ScalarType::eI32:
      case ir::ScalarType::eU32: {
        pushOp(m_declarations, spv::OpTypeInt, id, 32u, sign);
      } return id;

      case ir::ScalarType::eI64:
      case ir::ScalarType::eU64: {
        enableCapability(spv::CapabilityInt64);
        pushOp(m_declarations, spv::OpTypeInt, id, 64u, sign);
      } return id;

      case ir::ScalarType::eF16: {
        enableCapability(spv::CapabilityFloat16);
        pushOp(m_declarations, spv::OpTypeFloat, id, 16u);
      } return id;

      case ir::ScalarType::eF32: {
        pushOp(m_declarations, spv::OpTypeFloat, id, 32u);
      } return id;

      case ir::ScalarType::eF64: {
        enableCapability(spv::CapabilityFloat64);
        pushOp(m_declarations, spv::OpTypeFloat, id, 64u);
      } return id;

      default:
        dxbc_spv_unreachable();
        return 0u;
    }
  }

  if (type.isVectorType()) {
    auto baseType = type.getBaseType(0u);
    auto baseTypeId = getIdForType(baseType.getBaseType());

    pushOp(m_declarations, spv::OpTypeVector, id,
      baseTypeId, baseType.getVectorSize());
    return id;
  }

  if (type.isStructType()) {
    util::small_vector<uint32_t, 16u> memberTypeIds;

    for (uint32_t i = 0u; i < type.getStructMemberCount(); i++)
      memberTypeIds.push_back(getIdForType(type.getBaseType(i)));

    m_declarations.push_back(makeOpcodeToken(spv::OpTypeStruct, 2u + type.getStructMemberCount()));
    m_declarations.push_back(id);

    for (uint32_t i = 0u; i < type.getStructMemberCount(); i++) {
      m_declarations.push_back(memberTypeIds[i]);

      if (explicitLayout) {
        pushOp(m_decorations, spv::OpMemberDecorate, id, i,
          spv::DecorationOffset, type.byteOffset(i));
      }
    }

    return id;
  }

  if (type.isArrayType()) {
    auto baseType = type.getSubType(0u);
    auto baseTypeId = explicitLayout && !baseType.isBasicType()
      ? defType(baseType, explicitLayout)
      : getIdForType(baseType);

    if (type.isSizedArray()) {
      pushOp(m_declarations, spv::OpTypeArray, id, baseTypeId,
        makeConstU32(type.computeTopLevelMemberCount()));
    } else {
      pushOp(m_declarations, spv::OpTypeRuntimeArray, id, baseTypeId);
    }

    if (explicitLayout) {
      pushOp(m_decorations, spv::OpDecorate, id,
        spv::DecorationArrayStride, baseType.byteSize());
    }

    return id;
  }

  dxbc_spv_unreachable();
  return 0u;
}


uint32_t SpirvBuilder::defStructWrapper(uint32_t typeId) {
  uint32_t id = allocId();

  pushOp(m_declarations, spv::OpTypeStruct, id, typeId);
  pushOp(m_decorations, spv::OpMemberDecorate, id, 0u,
    spv::DecorationOffset, 0u);

  return id;
}


uint32_t SpirvBuilder::defDescriptor(const ir::Op& op, uint32_t typeId, spv::StorageClass storageClass) {
  uint32_t arraySize = getDescriptorArraySize(op);

  if (arraySize != 1u) {
    auto id = allocId();

    if (arraySize)
      pushOp(m_declarations, spv::OpTypeArray, id, typeId, makeConstU32(arraySize));
    else
      pushOp(m_declarations, spv::OpTypeRuntimeArray, id, typeId);

    typeId = id;
  }

  auto ptrTypeId = getIdForPtrType(typeId, storageClass);
  auto varId = getIdForDef(op.getDef());

  pushOp(m_declarations, spv::OpVariable, ptrTypeId, varId, storageClass);

  /* TODO map binding / set */
  const auto& bindingOp = op.getOpCode() == ir::OpCode::eDclUavCounter
    ? m_builder.getOp(ir::SsaDef(op.getOperand(1u)))
    : op;

  auto spaceId = uint32_t(bindingOp.getOperand(1u));
  auto regId = uint32_t(bindingOp.getOperand(2u));

  pushOp(m_decorations, spv::OpDecorate, varId, spv::DecorationDescriptorSet, spaceId);
  pushOp(m_decorations, spv::OpDecorate, varId, spv::DecorationBinding, regId);

  /* Declare resource as read-only or write-only as necessary */
  if (op.getOpCode() == ir::OpCode::eDclSrv && declaresPlainBufferResource(op))
    pushOp(m_decorations, spv::OpDecorate, varId, spv::DecorationNonWritable);

  if (op.getOpCode() == ir::OpCode::eDclUav) {
    auto uavFlags = getUavFlags(op);

    if (uavFlags & ir::UavFlag::eReadOnly)
      pushOp(m_decorations, spv::OpDecorate, varId, spv::DecorationNonWritable);
    if (uavFlags & ir::UavFlag::eWriteOnly)
      pushOp(m_decorations, spv::OpDecorate, varId, spv::DecorationNonReadable);
  }

  emitDebugName(op.getDef(), varId);

  addEntryPointId(varId);
  return varId;
}


uint32_t SpirvBuilder::getIdForPtrType(uint32_t typeId, spv::StorageClass storageClass) {
  SpirvPointerTypeKey key = { typeId, storageClass };

  auto entry = m_ptrTypes.find(key);

  if (entry != m_ptrTypes.end())
    return entry->second;

  uint32_t id = allocId();

  pushOp(m_declarations, spv::OpTypePointer, id, storageClass, typeId);

  m_ptrTypes.insert({ key, id });
  return id;
}


uint32_t SpirvBuilder::getIdForFuncType(const SpirvFunctionTypeKey& key) {
  auto entry = m_funcTypes.find(key);

  if (entry != m_funcTypes.end())
    return entry->second;

  uint32_t returnTypeId = getIdForType(key.returnType);

  util::small_vector<uint32_t, 4u> paramTypeIds;

  for (const auto& t : key.paramTypes)
    paramTypeIds.push_back(getIdForType(t));

  uint32_t id = allocId();

  m_declarations.push_back(makeOpcodeToken(spv::OpTypeFunction, 3u + key.paramTypes.size()));
  m_declarations.push_back(id);
  m_declarations.push_back(returnTypeId);

  for (const auto& t : paramTypeIds)
    m_declarations.push_back(t);

  m_funcTypes.insert({ key, id });
  return id;
}


uint32_t SpirvBuilder::getIdForImageType(const SpirvImageTypeKey& key) {
  auto entry = m_imageTypes.find(key);

  if (entry != m_imageTypes.end())
    return entry->second;

  uint32_t id = allocId();

  pushOp(m_declarations, spv::OpTypeImage, id,
    key.sampledTypeId, uint32_t(key.dim), 0u, /* depth */
    key.arrayed, key.ms, key.sampled, uint32_t(key.format));

  m_imageTypes.insert({ key, id });
  return id;
}


uint32_t SpirvBuilder::getIdForSamplerType() {
  if (m_samplerTypeId)
    return m_samplerTypeId;

  m_samplerTypeId = allocId();

  pushOp(m_declarations, spv::OpTypeSampler, m_samplerTypeId);
  return m_samplerTypeId;
}


uint32_t SpirvBuilder::getIdForConstant(const SpirvConstant& constant, uint32_t memberCount) {
  auto entry = m_constants.find(constant);

  if (entry != m_constants.end())
    return entry->second;

  uint32_t id = allocId();

  m_declarations.push_back(makeOpcodeToken(constant.op, 3u + memberCount));
  m_declarations.push_back(constant.typeId);
  m_declarations.push_back(id);

  for (uint32_t i = 0u; i < memberCount; i++)
    m_declarations.push_back(constant.constituents[i]);

  m_constants.insert({ constant, id });
  return id;
}


uint32_t SpirvBuilder::makeScalarConst(ir::ScalarType type, const ir::Op& op, uint32_t& operandIndex) {
  switch (type) {
    case ir::ScalarType::eBool:
      return makeConstBool(bool(op.getOperand(operandIndex++)));

    case ir::ScalarType::eI8:
    case ir::ScalarType::eU8:
    case ir::ScalarType::eI16:
    case ir::ScalarType::eU16:
    case ir::ScalarType::eF16:
    case ir::ScalarType::eI32:
    case ir::ScalarType::eU32:
    case ir::ScalarType::eF32: {
      SpirvConstant constant = { };
      constant.op = spv::OpConstant;
      constant.typeId = getIdForType(type);
      constant.constituents[0u] = uint32_t(op.getOperand(operandIndex++));
      return getIdForConstant(constant, 1u);
    }

    case ir::ScalarType::eI64:
    case ir::ScalarType::eU64:
    case ir::ScalarType::eF64: {
      auto literal = uint64_t(op.getOperand(operandIndex++));

      SpirvConstant constant = { };
      constant.op = spv::OpConstant;
      constant.typeId = getIdForType(type);
      constant.constituents[0u] = uint32_t(literal);
      constant.constituents[1u] = uint32_t(literal >> 32u);
      return getIdForConstant(constant, 2u);
    }

    default:
      dxbc_spv_unreachable();
      return 0u;
  }
}


uint32_t SpirvBuilder::makeBasicConst(ir::BasicType type, const ir::Op& op, uint32_t& operandIndex) {
  if (type.isScalar())
    return makeScalarConst(type.getBaseType(), op, operandIndex);

  SpirvConstant constant = { };
  constant.op = spv::OpConstantComposite;
  constant.typeId = getIdForType(type);

  for (uint32_t i = 0u; i < type.getVectorSize(); i++)
    constant.constituents[i] = makeScalarConst(type.getBaseType(), op, operandIndex);

  return getIdForConstant(constant, type.getVectorSize());
}


uint32_t SpirvBuilder::makeConstant(const ir::Type& type, const ir::Op& op, uint32_t& operandIndex) {
  if (type.isBasicType())
    return makeBasicConst(type.getBaseType(0u), op, operandIndex);

  /* Recursively emit member constants */
  util::small_vector<uint32_t, 16u> memberIds = { };

  for (uint32_t i = 0u; i < type.computeTopLevelMemberCount(); i++)
    memberIds.push_back(makeConstant(type.getSubType(i), op, operandIndex));

  /* Don't bother deduplicating struct or array constants,
   * we already do this at an IR level. */
  uint32_t typeId = getIdForType(type);
  uint32_t id = allocId();

  m_declarations.push_back(makeOpcodeToken(spv::OpConstantComposite, 3u + memberIds.size()));
  m_declarations.push_back(typeId);
  m_declarations.push_back(id);

  for (auto memberId : memberIds)
    m_declarations.push_back(memberId);

  return id;
}


uint32_t SpirvBuilder::makeConstBool(bool value) {
  SpirvConstant constant = { };
  constant.op = value ? spv::OpConstantTrue : spv::OpConstantFalse;
  constant.typeId = getIdForType(ir::ScalarType::eBool);
  return getIdForConstant(constant, 0u);
}


uint32_t SpirvBuilder::makeConstU32(uint32_t value) {
  SpirvConstant constant = { };
  constant.op = spv::OpConstant;
  constant.typeId = getIdForType(ir::ScalarType::eU32);
  constant.constituents[0u] = value;
  return getIdForConstant(constant, 1u);
}


uint32_t SpirvBuilder::makeConstI32(int32_t value) {
  SpirvConstant constant = { };
  constant.op = spv::OpConstant;
  constant.typeId = getIdForType(ir::ScalarType::eI32);
  constant.constituents[0u] = value;
  return getIdForConstant(constant, 1u);
}


uint32_t SpirvBuilder::makeConstNull(uint32_t typeId) {
  SpirvConstant constant = { };
  constant.op = spv::OpConstantNull;
  constant.typeId = typeId;
  return getIdForConstant(constant, 0u);
}


uint32_t SpirvBuilder::makeUndef(uint32_t typeId) {
  SpirvConstant constant = { };
  constant.op = spv::OpUndef;
  constant.typeId = typeId;
  return getIdForConstant(constant, 0u);
}


void SpirvBuilder::setDebugName(uint32_t id, const char* name) {
  m_debug.push_back(makeOpcodeToken(spv::OpName, 2u + getStringDwordCount(name)));
  m_debug.push_back(id);
  pushString(m_debug, name);
}


void SpirvBuilder::setDebugMemberName(uint32_t id, uint32_t member, const char* name) {
  m_debug.push_back(makeOpcodeToken(spv::OpMemberName, 3u + getStringDwordCount(name)));
  m_debug.push_back(id);
  m_debug.push_back(member);
  pushString(m_debug, name);
}


void SpirvBuilder::enableCapability(spv::Capability cap) {
  if (m_enabledCaps.find(cap) != m_enabledCaps.end())
    return;

  pushOp(m_capabilities, spv::OpCapability, cap);
  m_enabledCaps.insert(cap);

  switch (cap) {
    case spv::CapabilityFragmentShaderSampleInterlockEXT:
    case spv::CapabilityFragmentShaderPixelInterlockEXT:
      enableExtension("SPV_EXT_fragment_shader_interlock");
      break;

    case spv::CapabilityStencilExportEXT:
      enableExtension("SPV_EXT_shader_stencil_export");
      break;

    case spv::CapabilityFragmentFullyCoveredEXT:
      enableExtension("SPV_EXT_fragment_fully_covered");
      break;

    case spv::CapabilityRawAccessChainsNV:
      enableExtension("SPV_NV_raw_access_chains");
      break;

    default: ;
  }
}


void SpirvBuilder::enableExtension(const char* name) {
  if (m_enabledExt.find(name) != m_enabledExt.end())
    return;

  m_enabledExt.emplace(name);

  m_extensions.push_back(makeOpcodeToken(spv::OpExtension,
    1u + getStringDwordCount(name)));
  pushString(m_extensions, name);
}


void SpirvBuilder::addEntryPointId(uint32_t id) {
  dxbc_spv_assert(!m_entryPoint.empty());

  m_entryPoint.front() += 1u << spv::WordCountShift;
  m_entryPoint.push_back(id);
}


bool SpirvBuilder::declaresPlainBufferResource(const ir::Op& op) {
  if (op.getOpCode() == ir::OpCode::eDclCbv ||
      op.getOpCode() == ir::OpCode::eDclUavCounter)
    return true;

  if (op.getOpCode() == ir::OpCode::eDclSrv ||
      op.getOpCode() == ir::OpCode::eDclUav) {
    auto kind = getResourceKind(op);

    return kind == ir::ResourceKind::eBufferStructured ||
           kind == ir::ResourceKind::eBufferRaw;
  }

  return false;
}


uint32_t SpirvBuilder::getDescriptorArraySize(const ir::Op& op) {
  if (op.getOpCode() == ir::OpCode::eDclUavCounter)
    return getDescriptorArraySize(m_builder.getOp(ir::SsaDef(op.getOperand(1u))));

  dxbc_spv_assert(op.getOpCode() == ir::OpCode::eDclCbv ||
                  op.getOpCode() == ir::OpCode::eDclSrv ||
                  op.getOpCode() == ir::OpCode::eDclUav ||
                  op.getOpCode() == ir::OpCode::eDclSampler);

  return uint32_t(op.getOperand(3u));
}


void SpirvBuilder::setUavImageReadOperands(SpirvImageOperands& operands, const ir::Op& uavOp) {
  dxbc_spv_assert(uavOp.getOpCode() == ir::OpCode::eDclUav);

  auto uavFlags = getUavFlags(uavOp);

  if (!(uavFlags & ir::UavFlag::eReadOnly)) {
    operands.flags |= spv::ImageOperandsNonPrivateTexelMask
                   |  spv::ImageOperandsMakeTexelVisibleMask;
    operands.makeVisible = makeConstU32((uavFlags & ir::UavFlag::eCoherent)
      ? spv::ScopeQueueFamily : spv::ScopeWorkgroup);
  }
}


void SpirvBuilder::setUavImageWriteOperands(SpirvImageOperands& operands, const ir::Op& uavOp) {
  dxbc_spv_assert(uavOp.getOpCode() == ir::OpCode::eDclUav);

  auto uavFlags = getUavFlags(uavOp);

  if (!(uavFlags & ir::UavFlag::eWriteOnly)) {
    operands.flags |= spv::ImageOperandsNonPrivateTexelMask
                   |  spv::ImageOperandsMakeTexelAvailableMask;
    operands.makeAvailable = makeConstU32((uavFlags & ir::UavFlag::eCoherent)
      ? spv::ScopeQueueFamily : spv::ScopeWorkgroup);
  }
}


ir::Type SpirvBuilder::traverseType(ir::Type type, ir::SsaDef address) const {
  if (!address)
    return type;

  const auto& addressOp = m_builder.getOp(address);
  dxbc_spv_assert(addressOp.getType().isBasicType());

  auto addressType = addressOp.getType().getBaseType(0u);
  dxbc_spv_assert(addressType.isIntType());

  if (addressOp.isConstant()) {
    /* Constant indices only, trivial case. */
    for (uint32_t i = 0u; i < addressType.getVectorSize(); i++)
      type = type.getSubType(uint32_t(addressOp.getOperand(i)));

    return type;
  } else if (addressOp.getOpCode() == ir::OpCode::eCompositeConstruct) {
    /* Mixture of constant and dynamic indexing, handle appropriately. */
    for (uint32_t i = 0u; i < addressType.getVectorSize(); i++) {
      const auto& indexOp = m_builder.getOp(ir::SsaDef(addressOp.getOperand(i)));
      dxbc_spv_assert(type.isArrayType() || indexOp.isConstant());

      uint32_t index = 0u;

      if (indexOp.isConstant()) {
        dxbc_spv_assert(indexOp.getType().isScalarType());
        index = uint32_t(indexOp.getOperand(0u));
      }

      type = type.getSubType(index);
    }

    return type;
  } else {
    /* Indices can be anything, shouldn't really happen but w/e. */
    for (uint32_t i = 0u; i < addressType.getVectorSize(); i++) {
      dxbc_spv_assert(type.isArrayType());
      type = type.getSubType(0u);
    }

    return type;
  }
}


ir::UavFlags SpirvBuilder::getUavFlags(const ir::Op& op) {
  dxbc_spv_assert(op.getOpCode() == ir::OpCode::eDclUav);
  return ir::UavFlags(op.getOperand(5u));
}


ir::ResourceKind SpirvBuilder::getResourceKind(const ir::Op& op) {
  dxbc_spv_assert(op.getOpCode() == ir::OpCode::eDclSrv || op.getOpCode() == ir::OpCode::eDclUav);
  return ir::ResourceKind(op.getOperand(4u));
}


spv::StorageClass SpirvBuilder::getVariableStorageClass(const ir::Op& op) {
  switch (op.getOpCode()) {
    case ir::OpCode::eParamLoad:
      return spv::StorageClassFunction;

    case ir::OpCode::eTmpLoad:
    case ir::OpCode::eTmpStore:
    case ir::OpCode::eScratchLoad:
    case ir::OpCode::eScratchStore:
      return spv::StorageClassPrivate;

    case ir::OpCode::eLdsLoad:
    case ir::OpCode::eLdsStore:
      return spv::StorageClassWorkgroup;

    case ir::OpCode::ePushDataLoad:
      return spv::StorageClassPushConstant;

    case ir::OpCode::eInputLoad:
      return spv::StorageClassInput;

    case ir::OpCode::eOutputLoad:
    case ir::OpCode::eOutputStore:
      return spv::StorageClassOutput;

    default:
      dxbc_spv_unreachable();
      return spv::StorageClass();
  }
}


uint32_t SpirvBuilder::makeOpcodeToken(spv::Op op, uint32_t len) {
  return uint32_t(op) | (len << spv::WordCountShift);
}


uint32_t SpirvBuilder::getStringDwordCount(const char* str) {
  return std::strlen(str) / sizeof(uint32_t) + 1u;
}


template<typename T, typename... Args>
void SpirvBuilder::pushOp(T& container, spv::Op op, Args... args) {
  container.push_back(makeOpcodeToken(op, 1u + sizeof...(args)));
  (container.push_back(args), ...);
}


template<typename T>
void SpirvBuilder::pushString(T& container, const char* str) {
  uint32_t dword = 0u;

  for (uint32_t i = 0u; str[i]; i++) {
    dword |= uint32_t(uint8_t(str[i])) << (8u * (i % sizeof(dword)));

    if (!((i + 1u) % sizeof(dword))) {
      container.push_back(dword);
      dword = 0u;
    }
  }

  container.push_back(dword);
}

}
