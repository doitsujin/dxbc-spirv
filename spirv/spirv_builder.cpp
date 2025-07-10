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

    case ir::OpCode::eDclInput:
    case ir::OpCode::eDclOutput:
      return emitDclIoVar(op);

    case ir::OpCode::eDclInputBuiltIn:
    case ir::OpCode::eDclOutputBuiltIn:
      return emitDclBuiltInIoVar(op);

    case ir::OpCode::eDclParam:
      /* No-op, resolved when declaring function */
      return;

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

    case ir::OpCode::eSetCsWorkgroupSize:
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
    case ir::OpCode::eDclSampler:
    case ir::OpCode::eDclCbv:
    case ir::OpCode::eDclSrv:
    case ir::OpCode::eDclUav:
    case ir::OpCode::eDclUavCounter:
    case ir::OpCode::eDclLds:
    case ir::OpCode::eDclScratch:
    case ir::OpCode::eDclTmp:
    case ir::OpCode::eFunctionCall:
    case ir::OpCode::eBarrier:
    case ir::OpCode::eConvertFtoF:
    case ir::OpCode::eConvertFtoI:
    case ir::OpCode::eConvertItoF:
    case ir::OpCode::eConvertItoI:
    case ir::OpCode::eCast:
    case ir::OpCode::eConsumeAs:
    case ir::OpCode::eCheckSparseAccess:
    case ir::OpCode::eParamLoad:
    case ir::OpCode::eTmpLoad:
    case ir::OpCode::eTmpStore:
    case ir::OpCode::eSpecConstantLoad:
    case ir::OpCode::eDescriptorLoad:
    case ir::OpCode::eBufferLoad:
    case ir::OpCode::eBufferStore:
    case ir::OpCode::eBufferQuerySize:
    case ir::OpCode::eMemoryLoad:
    case ir::OpCode::eMemoryStore:
    case ir::OpCode::eLdsAtomic:
    case ir::OpCode::eBufferAtomic:
    case ir::OpCode::eImageAtomic:
    case ir::OpCode::eCounterAtomic:
    case ir::OpCode::eMemoryAtomic:
    case ir::OpCode::eImageLoad:
    case ir::OpCode::eImageStore:
    case ir::OpCode::eImageQuerySize:
    case ir::OpCode::eImageQueryMips:
    case ir::OpCode::eImageQuerySamples:
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
    case ir::OpCode::eFAbs:
    case ir::OpCode::eFNeg:
    case ir::OpCode::eFAdd:
    case ir::OpCode::eFSub:
    case ir::OpCode::eFMul:
    case ir::OpCode::eFMulLegacy:
    case ir::OpCode::eFMad:
    case ir::OpCode::eFMadLegacy:
    case ir::OpCode::eFDiv:
    case ir::OpCode::eFRcp:
    case ir::OpCode::eFSqrt:
    case ir::OpCode::eFRsq:
    case ir::OpCode::eFExp2:
    case ir::OpCode::eFLog2:
    case ir::OpCode::eFFract:
    case ir::OpCode::eFRound:
    case ir::OpCode::eFMin:
    case ir::OpCode::eFMax:
    case ir::OpCode::eFDot:
    case ir::OpCode::eFDotLegacy:
    case ir::OpCode::eFClamp:
    case ir::OpCode::eFSin:
    case ir::OpCode::eFCos:
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
    case ir::OpCode::eIBitCount:
    case ir::OpCode::eIBitReverse:
    case ir::OpCode::eIFindLsb:
    case ir::OpCode::eSFindMsb:
    case ir::OpCode::eUFindMsb:
    case ir::OpCode::eIAdd:
    case ir::OpCode::eIAddCarry:
    case ir::OpCode::eISub:
    case ir::OpCode::eISubBorrow:
    case ir::OpCode::eINeg:
    case ir::OpCode::eIMul:
    case ir::OpCode::eSDiv:
    case ir::OpCode::eUDiv:
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
    case ir::OpCode::Count:
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
  uint32_t defId = op.getDef().getId();
  uint32_t spvId = makeConstant(op.getType(), op, operandIndex);

  if (defId >= m_ssaDefsToId.size())
    m_ssaDefsToId.resize(defId + 1u);

  m_ssaDefsToId[defId] = spvId;
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


uint32_t SpirvBuilder::emitAccessChain(spv::StorageClass storageClass, ir::SsaDef base, ir::SsaDef address) {
  if (!address)
    return getIdForDef(base);

  /* Declare resulting pointer type */
  auto pointeeType = traverseType(m_builder.getOp(base).getType(), address);
  auto ptrTypeId = getIdForPtrType(getIdForType(pointeeType), storageClass);

  /* Ensure that the address is an integer scalar or vector.
   * We already validated everything when traversing the type. */
  const auto& addressOp = m_builder.getOp(address);
  auto addressType = addressOp.getType().getBaseType(0u);

  /* Allocate access chain */
  auto accessChainId = allocId();

  if (addressType.isScalar()) {
    /* Scalar operand, can use directly no matter what it is. */
    pushOp(m_code, spv::OpAccessChain, ptrTypeId, accessChainId,
      getIdForDef(base), getIdForDef(address));
  } else if (addressOp.isConstant()) {
    /* Unroll constant operands if possible */
    util::small_vector<uint32_t, 4u> indexIds;

    for (uint32_t i = 0u; i < addressType.getVectorSize(); i++)
      indexIds.push_back(makeConstU32(uint32_t(addressOp.getOperand(i))));

    /* Emit actual access chain instruction */
    m_code.push_back(makeOpcodeToken(spv::OpAccessChain, 4u + addressType.getVectorSize()));
    m_code.push_back(ptrTypeId);
    m_code.push_back(accessChainId);
    m_code.push_back(getIdForDef(base));

    for (auto index : indexIds)
      m_code.push_back(index);
  } else if (addressOp.getOpCode() == ir::OpCode::eCompositeConstruct) {
    /* Unroll vector operands if possible */
    m_code.push_back(makeOpcodeToken(spv::OpAccessChain, 4u + addressType.getVectorSize()));
    m_code.push_back(ptrTypeId);
    m_code.push_back(accessChainId);
    m_code.push_back(getIdForDef(base));

    for (uint32_t i = 0u; i < addressType.getVectorSize(); i++)
      m_code.push_back(getIdForDef(ir::SsaDef(addressOp.getOperand(i))));
  } else {
    /* Dynamically extract vector components */
    util::small_vector<uint32_t, 4u> indexIds;

    auto scalarTypeId = getIdForType(addressType.getBaseType());
    auto addressId = getIdForDef(address);

    for (uint32_t i = 0u; i < addressType.getVectorSize(); i++) {
      uint32_t componentId = allocId();
      pushOp(m_code, spv::OpCompositeExtract, scalarTypeId, componentId, addressId, i);
      indexIds.push_back(componentId);
    }

    /* Emit actual access chain instruction */
    m_code.push_back(makeOpcodeToken(spv::OpAccessChain, 4u + addressType.getVectorSize()));
    m_code.push_back(ptrTypeId);
    m_code.push_back(accessChainId);
    m_code.push_back(getIdForDef(base));

    for (auto index : indexIds)
      m_code.push_back(index);
  }

  return accessChainId;
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

  if (op.getOpCode() == ir::OpCode::eLdsLoad) {
    uint32_t scopeId = makeConstU32(spv::ScopeWorkgroup);

    pushOp(m_code, spv::OpLoad, typeId, id, accessChainId,
      spv::MemoryAccessNonPrivatePointerMask |
      spv::MemoryAccessMakePointerAvailableMask,
      scopeId);
  } else {
    pushOp(m_code, spv::OpLoad, typeId, id, accessChainId);
  }

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitStoreVariable(const ir::Op& op) {
  auto accessChainId = emitAccessChain(
    getVariableStorageClass(op),
    ir::SsaDef(op.getOperand(0u)),
    ir::SsaDef(op.getOperand(1u)));

  auto valueId = getIdForDef(ir::SsaDef(op.getOperand(2u)));

  if (op.getOpCode() == ir::OpCode::eLdsStore) {
    uint32_t scopeId = makeConstU32(spv::ScopeWorkgroup);

    pushOp(m_code, spv::OpStore, accessChainId, valueId,
      spv::MemoryAccessNonPrivatePointerMask |
      spv::MemoryAccessMakePointerVisibleMask,
      scopeId);
  } else {
    pushOp(m_code, spv::OpStore, accessChainId, valueId);
  }
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
