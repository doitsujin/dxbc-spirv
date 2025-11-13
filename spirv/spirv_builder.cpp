#include <algorithm>
#include <cstring>
#include <optional>
#include <sstream>
#include <iostream>

#include "spirv_builder.h"

namespace dxbc_spv::spirv {

SpirvBuilder::SpirvBuilder(const ir::Builder& builder, ResourceMapping& mapping, const Options& options)
: m_builder(builder), m_mapping(mapping), m_options(options) {
  emitMemoryModel();
}


SpirvBuilder::~SpirvBuilder() {

}


void SpirvBuilder::buildSpirvBinary() {
  if (m_options.includeDebugNames)
    processDebugNames();

  if (m_options.maxCbvSize || m_options.maxCbvCount >= 0)
    demoteCbv();

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
  std::array<std::pair<size_t, const uint32_t*>, 11u> arrays = {{
    { m_capabilities.size(), m_capabilities.data() },
    { m_extensions.size(), m_extensions.data() },
    { m_imports.size(), m_imports.data() },
    { m_memoryModel.size(), m_memoryModel.data() },
    { m_entryPoint.size(), m_entryPoint.data() },
    { m_executionModes.size(), m_executionModes.data() },
    { m_source.size(), m_source.data() },
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
      m_debugNames.insert_or_assign(ir::SsaDef(op->getOperand(0u)), op->getDef());
    else if (op->getOpCode() == ir::OpCode::eSemantic)
      m_debugNames.insert({ ir::SsaDef(op->getOperand(0u)), op->getDef() });
  }
}


void SpirvBuilder::demoteCbv() {
  util::small_vector<ir::SsaDef, 16u> cbv;

  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == ir::OpCode::eDclCbv)
      cbv.push_back(iter->getDef());
  }

  std::sort(cbv.begin(), cbv.end(), [this] (ir::SsaDef a, ir::SsaDef b) {
    const auto& aOp = m_builder.getOp(a);
    const auto& bOp = m_builder.getOp(b);

    auto aSpace = uint32_t(aOp.getOperand(aOp.getFirstLiteralOperandIndex() + 0u));
    auto bSpace = uint32_t(bOp.getOperand(bOp.getFirstLiteralOperandIndex() + 0u));

    if (aSpace != bSpace)
      return aSpace < bSpace;

    auto aIndex = uint32_t(aOp.getOperand(aOp.getFirstLiteralOperandIndex() + 1u));
    auto bIndex = uint32_t(bOp.getOperand(bOp.getFirstLiteralOperandIndex() + 1u));
    return aIndex < bIndex;
  });

  int32_t cbvCount = 0;

  for (auto e : cbv) {
    const auto& op = m_builder.getOp(e);
    auto descriptorCount = uint32_t(op.getOperand(op.getFirstLiteralOperandIndex() + 2u));

    bool demote = false;

    if (m_options.maxCbvSize)
      demote = demote || op.getType().byteSize() > m_options.maxCbvSize;

    if (m_options.maxCbvCount >= 0) {
      demote = demote || !descriptorCount ||
        int64_t(cbvCount) + int64_t(descriptorCount) >= int64_t(m_options.maxCbvCount);
    }

    if (demote)
      m_storageBufferCbv.insert(op.getDef());
    else
      cbvCount += descriptorCount;
  }
}


bool SpirvBuilder::cbvAsSsbo(const ir::Op& op) const {
  dxbc_spv_assert(op.getOpCode() == ir::OpCode::eDclCbv);
  return m_storageBufferCbv.find(op.getDef()) != m_storageBufferCbv.end();
}


void SpirvBuilder::finalize() {
  if (m_stage == ir::ShaderStage::eHull)
    emitHsEntryPoint();
}


void SpirvBuilder::emitHsEntryPoint() {
  /* Declare invocation ID built-in as necessary */
  auto uintTypeId = getIdForType(ir::ScalarType::eU32);
  auto boolTypeId = getIdForType(ir::ScalarType::eBool);

  if (!m_tessControl.invocationId) {
    m_tessControl.invocationId = allocId();

    auto varTypeId = getIdForPtrType(uintTypeId, spv::StorageClassInput);

    pushOp(m_declarations, spv::OpVariable, varTypeId,
      m_tessControl.invocationId, spv::StorageClassInput);

    pushOp(m_decorations, spv::OpDecorate, m_tessControl.invocationId,
      spv::DecorationBuiltIn, spv::BuiltInInvocationId);

    addEntryPointId(m_tessControl.invocationId);
  }

  /* Emit actual shader entry point */
  SpirvFunctionTypeKey funcType = { };
  auto voidTypeId = getIdForType(ir::Type());

  pushOp(m_code, spv::OpFunction, voidTypeId, m_entryPointId,
    spv::FunctionControlMaskNone, getIdForFuncType(funcType));
  pushOp(m_code, spv::OpLabel, allocId());

  /* Call control point function */
  pushOp(m_code, spv::OpFunctionCall, voidTypeId, allocId(), m_tessControl.controlPointFuncId);

  /* Emit I/O barrier if control point outputs are read back */
  if (m_tessControl.needsIoBarrier) {
    pushOp(m_code, spv::OpControlBarrier,
      makeConstU32(spv::ScopeWorkgroup),
      makeConstU32(spv::ScopeWorkgroup),
      makeConstU32(spv::MemorySemanticsOutputMemoryMask |
                   spv::MemorySemanticsAcquireReleaseMask |
                   spv::MemorySemanticsMakeAvailableMask |
                   spv::MemorySemanticsMakeVisibleMask));
  }

  /* Call patch constant function on one thread */
  auto invocationId = allocId();
  pushOp(m_code, spv::OpLoad, uintTypeId, invocationId, m_tessControl.invocationId);

  auto condId = allocId();
  pushOp(m_code, spv::OpIEqual, boolTypeId, condId, invocationId, makeConstU32(0u));

  auto ifBlock = allocId();
  auto mergeBlock = allocId();

  pushOp(m_code, spv::OpSelectionMerge, mergeBlock, spv::SelectionControlMaskNone);
  pushOp(m_code, spv::OpBranchConditional, condId, ifBlock, mergeBlock);

  pushOp(m_code, spv::OpLabel, ifBlock);
  pushOp(m_code, spv::OpFunctionCall, voidTypeId, allocId(), m_tessControl.patchConstantFuncId);
  pushOp(m_code, spv::OpBranch, mergeBlock);

  pushOp(m_code, spv::OpLabel, mergeBlock);
  pushOp(m_code, spv::OpReturn);
  pushOp(m_code, spv::OpFunctionEnd);
}


void SpirvBuilder::emitInstruction(const ir::Op& op) {
  switch (op.getOpCode()) {
    case ir::OpCode::eEntryPoint:
      return emitEntryPoint(op);

    case ir::OpCode::eSemantic:
    case ir::OpCode::eDebugName:
    case ir::OpCode::eDebugMemberName:
      /* No-op here, we resolve debug names early */
      return;

    case ir::OpCode::eConstant:
      return emitConstant(op);

    case ir::OpCode::eUndef:
      return emitUndef(op);

    case ir::OpCode::eSetFpMode:
      return emitSetFpMode(op);

    case ir::OpCode::eSetCsWorkgroupSize:
      return emitSetCsWorkgroupSize(op);

    case ir::OpCode::eSetGsInstances:
      return emitSetGsInstances(op);

    case ir::OpCode::eSetGsInputPrimitive:
      return emitSetGsInputPrimitive(op);

    case ir::OpCode::eSetGsOutputVertices:
      return emitSetGsOutputVertices(op);

    case ir::OpCode::eSetGsOutputPrimitive:
      return emitSetGsOutputPrimitive(op);

    case ir::OpCode::eSetTessPrimitive:
      return emitSetTessPrimitive(op);

    case ir::OpCode::eSetTessDomain:
      return emitSetTessDomain(op);

    case ir::OpCode::eSetTessControlPoints:
      return emitSetTessControlPoints(op);

    case ir::OpCode::eSetPsDepthGreaterEqual:
    case ir::OpCode::eSetPsDepthLessEqual:
      return emitSetPsDepthMode(op);

    case ir::OpCode::eSetPsEarlyFragmentTest:
      return emitSetPsEarlyFragmentTest();

    case ir::OpCode::eDclSpecConstant:
      return emitDclSpecConstant(op);

    case ir::OpCode::eDclPushData:
      return emitDclPushData(op);

    case ir::OpCode::eDclLds:
      return emitDclLds(op);

    case ir::OpCode::eDclScratch:
      return emitDclScratch(op);

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

    case ir::OpCode::eDclXfb:
      return emitDclXfb(op);

    case ir::OpCode::ePushDataLoad:
      return emitPushDataLoad(op);

    case ir::OpCode::eConstantLoad:
      return emitConstantLoad(op);

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

    case ir::OpCode::eMemoryLoad:
      return emitMemoryLoad(op);

    case ir::OpCode::eMemoryStore:
      return emitMemoryStore(op);

    case ir::OpCode::eMemoryAtomic:
      return emitMemoryAtomic(op);

    case ir::OpCode::eCounterAtomic:
      return emitCounterAtomic(op);

    case ir::OpCode::eLdsAtomic:
      return emitLdsAtomic(op);

    case ir::OpCode::eFunction:
      return emitFunction(op);

    case ir::OpCode::eFunctionEnd:
      return emitFunctionEnd();

    case ir::OpCode::eFunctionCall:
      return emitFunctionCall(op);

    case ir::OpCode::eParamLoad:
      return emitParamLoad(op);

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

    case ir::OpCode::eImageStore:
      return emitImageStore(op);

    case ir::OpCode::eImageAtomic:
      return emitImageAtomic(op);

    case ir::OpCode::eImageSample:
      return emitImageSample(op);

    case ir::OpCode::eImageGather:
      return emitImageGather(op);

    case ir::OpCode::eImageComputeLod:
      return emitImageComputeLod(op);

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

    case ir::OpCode::eDerivX:
    case ir::OpCode::eDerivY:
      return emitDerivative(op);

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
    case ir::OpCode::eSLe:
    case ir::OpCode::eSGt:
    case ir::OpCode::eSGe:
    case ir::OpCode::eULt:
    case ir::OpCode::eULe:
    case ir::OpCode::eUGt:
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
    case ir::OpCode::eIBitCount:
    case ir::OpCode::eIBitReverse:
    case ir::OpCode::eIShl:
    case ir::OpCode::eSShr:
    case ir::OpCode::eUShr:
    case ir::OpCode::eIAdd:
    case ir::OpCode::eISub:
    case ir::OpCode::eINeg:
    case ir::OpCode::eIMul:
    case ir::OpCode::eUDiv:
    case ir::OpCode::eUMod:
      return emitSimpleArithmetic(op);

    case ir::OpCode::eIAbs:
    case ir::OpCode::eFAbs:
    case ir::OpCode::eFMad:
    case ir::OpCode::eFFract:
    case ir::OpCode::eFSin:
    case ir::OpCode::eFCos:
    case ir::OpCode::eFPow:
    case ir::OpCode::eFExp2:
    case ir::OpCode::eFLog2:
    case ir::OpCode::eFSqrt:
    case ir::OpCode::eFRsq:
    case ir::OpCode::eFMin:
    case ir::OpCode::eSMin:
    case ir::OpCode::eUMin:
    case ir::OpCode::eFMax:
    case ir::OpCode::eSMax:
    case ir::OpCode::eUMax:
    case ir::OpCode::eFClamp:
    case ir::OpCode::eSClamp:
    case ir::OpCode::eUClamp:
    case ir::OpCode::eIFindLsb:
    case ir::OpCode::eSFindMsb:
    case ir::OpCode::eUFindMsb:
    case ir::OpCode::eConvertF32toPackedF16:
    case ir::OpCode::eConvertPackedF16toF32:
      return emitExtendedGlslArithmetic(op);

    case ir::OpCode::eIAddCarry:
    case ir::OpCode::eISubBorrow:
    case ir::OpCode::eSMulExtended:
    case ir::OpCode::eUMulExtended:
      return emitExtendedIntArithmetic(op);

    case ir::OpCode::eFRcp:
      return emitFRcp(op);

    case ir::OpCode::eFRound:
      return emitFRound(op);

    case ir::OpCode::eInterpolateAtCentroid:
    case ir::OpCode::eInterpolateAtSample:
    case ir::OpCode::eInterpolateAtOffset:
      return emitInterpolation(op);

    case ir::OpCode::eBarrier:
      return emitBarrier(op);

    case ir::OpCode::eEmitVertex:
    case ir::OpCode::eEmitPrimitive:
      return emitGsEmit(op);

    case ir::OpCode::eDemote:
      return emitDemote();

    case ir::OpCode::eRovScopedLockBegin:
      return emitRovLockBegin(op);

    case ir::OpCode::eRovScopedLockEnd:
      return emitRovLockEnd(op);

    case ir::OpCode::ePointer:
      return emitPointer(op);

    case ir::OpCode::eDrain:
      return emitDrain(op);

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
    case ir::OpCode::eFDot:
    case ir::OpCode::eFDotLegacy:
    case ir::OpCode::eFMulLegacy:
    case ir::OpCode::eFMadLegacy:
    case ir::OpCode::eFPowLegacy:
    case ir::OpCode::eUMSad:
    case ir::OpCode::Count:
      /* Invalid opcodes */
      std::cerr << "Invalid opcode " << op.getOpCode() << std::endl;
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

  emitSourceName(op);

  m_stage = stage;
}


void SpirvBuilder::emitSourceName(const ir::Op& op) {
  auto [a, b] = m_builder.getUses(op.getDef());

  for (auto i = a; i != b; i++) {
    if (i->getOpCode() == ir::OpCode::eDebugName) {
      m_source.clear();

      auto name = i->getLiteralString(i->getFirstLiteralOperandIndex());

      auto fileNameId = allocId();
      m_source.push_back(makeOpcodeToken(spv::OpString, 2u + getStringDwordCount(name.c_str())));
      m_source.push_back(fileNameId);
      pushString(m_source, name.c_str());

      m_source.push_back(makeOpcodeToken(spv::OpSource, 4u));
      m_source.push_back(spv::SourceLanguageUnknown);
      m_source.push_back(0u);
      m_source.push_back(fileNameId);
    }
  }
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


void SpirvBuilder::emitUndef(const ir::Op& op) {
  setIdForDef(op.getDef(), getIdForConstantNull(op.getType()));
}


void SpirvBuilder::emitDclSpecConstant(const ir::Op& op) {
  dxbc_spv_assert(op.getType().isScalarType());

  auto typeId = getIdForType(op.getType());
  auto id = getIdForDef(op.getDef());

  auto specId = uint32_t(op.getOperand(1u));
  auto specValue = op.getOperand(2u);

  switch (op.getType().getBaseType(0u).getBaseType()) {
    case ir::ScalarType::eBool: {
      auto op = bool(specValue)
        ? spv::OpSpecConstantTrue
        : spv::OpSpecConstantFalse;

      pushOp(m_declarations, op, typeId, id);
    } break;

    case ir::ScalarType::eI8:
    case ir::ScalarType::eU8: {
      pushOp(m_declarations, spv::OpSpecConstant, typeId, id, uint8_t(specValue));
    } break;

    case ir::ScalarType::eI16:
    case ir::ScalarType::eU16:
    case ir::ScalarType::eF16: {
      pushOp(m_declarations, spv::OpSpecConstant, typeId, id, uint16_t(specValue));
    } break;

    case ir::ScalarType::eI32:
    case ir::ScalarType::eU32:
    case ir::ScalarType::eF32: {
      pushOp(m_declarations, spv::OpSpecConstant, typeId, id, uint32_t(specValue));
    } break;

    case ir::ScalarType::eI64:
    case ir::ScalarType::eU64:
    case ir::ScalarType::eF64: {
      /* Default value requires two opcode tokens */
      auto literal = uint64_t(specValue);

      pushOp(m_declarations, spv::OpSpecConstant, typeId, id,
        uint32_t(literal), uint32_t(literal >> 32u));
    } break;

    default:
      dxbc_spv_unreachable();
      break;
  }

  pushOp(m_decorations, spv::OpDecorate, id, spv::DecorationSpecId, specId);

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitDclPushData(const ir::Op& op) {
  auto type = op.getType();
  dxbc_spv_assert(!type.isArrayType());

  /* Map push data block to real offset based on shader stage mask */
  auto baseOffset = m_mapping.mapPushData(ir::ShaderStageMask(op.getOperand(2u)));
  auto byteOffset = uint32_t(op.getOperand(1u)) + baseOffset;

  /* Work out where to insert new entries */
  auto iter = std::find_if(m_pushData.members.begin(), m_pushData.members.end(),
    [byteOffset] (const PushDataInfo& info) {
      return byteOffset < info.offset;
    });

  if (type.isStructType()) {
    /* Unroll struct, we need to pack things into a different struct later. */
    PushDataInfo info = { };
    info.def = op.getDef();

    for (uint32_t i = 0u; i < type.getStructMemberCount(); i++) {
      info.member = i;
      info.offset = byteOffset + type.byteOffset(i);

      iter = m_pushData.members.insert(iter, info) + 1u;
    }
  } else {
    PushDataInfo info = { };
    info.def = op.getDef();
    info.member = 0u;
    info.offset = byteOffset;

    m_pushData.members.insert(iter, info);
  }
}


void SpirvBuilder::emitDclLds(const ir::Op& op) {
  /* Plain shared variable, use type as-is. */
  auto varId = getIdForDef(op.getDef());

  emitDebugName(op.getDef(), varId);

  auto typeId = getIdForType(op.getType());
  auto ptrTypeId = getIdForPtrType(typeId, spv::StorageClassWorkgroup);

  pushOp(m_declarations, spv::OpVariable, ptrTypeId, varId,
    spv::StorageClassWorkgroup, getIdForConstantNull(op.getType()));

  addEntryPointId(varId);
}


void SpirvBuilder::emitDclScratch(const ir::Op& op) {
  /* Declare scratch as a simpe private variable */
  auto varId = getIdForDef(op.getDef());

  emitDebugName(op.getDef(), varId);

  auto typeId = getIdForType(op.getType());
  auto ptrTypeId = getIdForPtrType(typeId, spv::StorageClassPrivate);

  pushOp(m_declarations, spv::OpVariable, ptrTypeId, varId,
    spv::StorageClassPrivate, getIdForConstantNull(op.getType()));

  addEntryPointId(varId);
}


void SpirvBuilder::emitDclIoVar(const ir::Op& op) {
  bool isInput = op.getOpCode() == ir::OpCode::eDclInput;

  auto storageClass = isInput
    ? spv::StorageClassInput
    : spv::StorageClassOutput;

  uint32_t typeId = getIdForType(op.getType());
  uint32_t varId = getIdForDef(op.getDef());

  emitDebugName(op.getDef(), varId);

  uint32_t ptrTypeId = getIdForPtrType(typeId, storageClass);

  pushOp(m_declarations, spv::OpVariable, ptrTypeId, varId, storageClass);

  uint32_t location = uint32_t(op.getOperand(1u));

  if (m_options.dualSourceBlending && !isInput && m_stage == ir::ShaderStage::ePixel)
    pushOp(m_decorations, spv::OpDecorate, varId, spv::DecorationIndex, std::exchange(location, 0u));

  pushOp(m_decorations, spv::OpDecorate, varId, spv::DecorationLocation, location);
  pushOp(m_decorations, spv::OpDecorate, varId, spv::DecorationComponent, uint32_t(op.getOperand(2u)));

  if (isInput && m_stage == ir::ShaderStage::ePixel) {
    auto interpolationModes = ir::InterpolationModes(op.getOperand(3u));
    emitInterpolationModes(varId, interpolationModes);
  }

  if (isMultiStreamGs() && !isInput) {
    pushOp(m_decorations, spv::OpDecorate, varId,
      spv::DecorationStream, uint32_t(op.getOperand(3u)));
  }

  if (isPatchConstant(op))
    pushOp(m_decorations, spv::OpDecorate, varId, spv::DecorationPatch);

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

  if (builtIn == ir::BuiltIn::eGsVertexCountIn)
    return;

  bool isInput = op.getOpCode() == ir::OpCode::eDclInputBuiltIn;
  auto storageClass = isInput ? spv::StorageClassInput : spv::StorageClassOutput;

  /* Override type for vertex and instance indices as signed integers.
   * Subtracting the respective base parameter from it will yield an
   * unsigned integer. */
  auto type = op.getType();

  if (builtIn == ir::BuiltIn::eVertexId || builtIn == ir::BuiltIn::eInstanceId)
    type = ir::ScalarType::eI32;

  /* Coverage mask is an array in SPIR-V */
  if (builtIn == ir::BuiltIn::eSampleMask)
    type.addArrayDimension(1u);

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
        if (m_stage != ir::ShaderStage::eGeometry)
          enableCapability(spv::CapabilityShaderLayer);
        return spv::BuiltInLayer;
      }

      case ir::BuiltIn::eViewportIndex: {
        enableCapability(spv::CapabilityMultiViewport);

        if (m_stage != ir::ShaderStage::eGeometry)
          enableCapability(spv::CapabilityShaderViewportIndex);

        return spv::BuiltInViewportIndex;
      }

      case ir::BuiltIn::eTessControlPointCountIn:
        return spv::BuiltInPatchVertices;

      case ir::BuiltIn::eGsInstanceId:
      case ir::BuiltIn::eTessControlPointId: {
        m_tessControl.invocationId = varId;
      } return spv::BuiltInInvocationId;

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

      case ir::BuiltIn::eDepth: {
        pushOp(m_executionModes, spv::OpExecutionMode, m_entryPointId, spv::ExecutionModeDepthReplacing);
      } return spv::BuiltInFragDepth;

      case ir::BuiltIn::eStencilRef: {
        enableCapability(spv::CapabilityStencilExportEXT);
        pushOp(m_executionModes, spv::OpExecutionMode, m_entryPointId, spv::ExecutionModeStencilRefReplacingEXT);
        return spv::BuiltInFragStencilRefEXT;
      }

      case ir::BuiltIn::eIsFullyCovered: {
        enableCapability(spv::CapabilityFragmentFullyCoveredEXT);
      } return spv::BuiltInFullyCoveredEXT;

      case ir::BuiltIn::eWorkgroupId:
        return spv::BuiltInWorkgroupId;

      case ir::BuiltIn::eGlobalThreadId:
        return spv::BuiltInGlobalInvocationId;

      case ir::BuiltIn::eLocalThreadId:
        return spv::BuiltInLocalInvocationId;

      case ir::BuiltIn::eLocalThreadIndex:
        return spv::BuiltInLocalInvocationIndex;

      case ir::BuiltIn::ePointSize:
        return spv::BuiltInPointSize;

      case ir::BuiltIn::eGsVertexCountIn:
        /* Handled elsewhere */
        break;
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

  /* Need to declare tess factors as patch constants */
  if (isPatchConstant(op))
    pushOp(m_decorations, spv::OpDecorate, varId, spv::DecorationPatch);

  /* Set up stream decoration for GS */
  if (isMultiStreamGs() && !isInput) {
    pushOp(m_decorations, spv::OpDecorate, varId,
      spv::DecorationStream, uint32_t(op.getOperand(2u)));
  }

  /* Some outputs need to be consistent between different shaders */
  if (!isInput && (op.getFlags() & ir::OpFlag::eInvariant))
    pushOp(m_decorations, spv::OpDecorate, varId, spv::DecorationInvariant);

  addEntryPointId(varId);
}


void SpirvBuilder::emitDclXfb(const ir::Op& op) {
  if (enableCapability(spv::CapabilityTransformFeedback))
    pushOp(m_executionModes, spv::OpExecutionMode, m_entryPointId, spv::ExecutionModeXfb);

  auto varId = getIdForDef(ir::SsaDef(op.getOperand(0u)));

  pushOp(m_decorations, spv::OpDecorate, varId, spv::DecorationXfbBuffer, uint32_t(op.getOperand(1u)));
  pushOp(m_decorations, spv::OpDecorate, varId, spv::DecorationXfbStride, uint32_t(op.getOperand(2u)));
  pushOp(m_decorations, spv::OpDecorate, varId, spv::DecorationOffset, uint32_t(op.getOperand(3u)));
}


void SpirvBuilder::emitDclSampler(const ir::Op& op) {
  auto samplerTypeId = getIdForSamplerType();

  defDescriptor(op, samplerTypeId, spv::StorageClassUniformConstant);
  m_descriptorTypes.insert({ op.getDef(), samplerTypeId });
}


void SpirvBuilder::emitDclCbv(const ir::Op& op) {
  /* Create a wrapper struct that we can decorate as a block */
  auto structTypeId = defType(op.getType(), true, op.getDef());

  if (!op.getType().isStructType()) {
    structTypeId = defStructWrapper(structTypeId);

    if (m_options.includeDebugNames)
      setDebugMemberName(structTypeId, 0u, "m");
  }

  if (m_options.includeDebugNames)
    emitDebugTypeName(op.getDef(), structTypeId, "_buf");

  pushOp(m_decorations, spv::OpDecorate, structTypeId, spv::DecorationBlock);

  auto storageClass = cbvAsSsbo(op)
    ? spv::StorageClassStorageBuffer
    : spv::StorageClassUniform;

  defDescriptor(op, structTypeId, storageClass);
  m_descriptorTypes.insert({ op.getDef(), structTypeId });
}


void SpirvBuilder::emitDclSrvUav(const ir::Op& op) {
  auto kind = getResourceKind(op);

  if (kind == ir::ResourceKind::eBufferRaw || kind == ir::ResourceKind::eBufferStructured) {
    /* Create wrapper struct, much like this works for CBV */
    auto structTypeId = defType(op.getType(), true, op.getDef());

    if (!op.getType().isStructType()) {
      structTypeId = defStructWrapper(structTypeId);

      if (m_options.includeDebugNames)
        setDebugMemberName(structTypeId, 0u, "m");
    }

    if (m_options.includeDebugNames)
      emitDebugTypeName(op.getDef(), structTypeId, "_buf");

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
              return spv::ImageFormatUnknown;
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

  emitDebugTypeName(op.getDef(), structTypeId, "_t");

  defDescriptor(op, structTypeId, spv::StorageClassStorageBuffer);
  m_descriptorTypes.insert({ op.getDef(), structTypeId });
}


uint32_t SpirvBuilder::getDescriptorArrayIndex(const ir::Op& op) {
  const auto& dclOp = m_builder.getOpForOperand(op, 0u);

  if (getDescriptorArraySize(dclOp) == 1u)
    return 0u; /* no array */

  if (op.getFlags() & ir::OpFlag::eNonUniform)
    enableCapability(spv::CapabilityShaderNonUniform);

  return getIdForDef(ir::SsaDef(op.getOperand(1u)));
}


uint32_t SpirvBuilder::getImageDescriptorPointer(const ir::Op& op) {
  const auto& dclOp = m_builder.getOpForOperand(op, 0u);
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


void SpirvBuilder::emitPushDataLoad(const ir::Op& op) {
  const auto& dclOp = m_builder.getOpForOperand(op, 0u);
  const auto& addressOp = m_builder.getOpForOperand(op, 1u);

  dxbc_spv_assert(!addressOp || addressOp.isConstant());

  /* Consume struct member index as necessary */
  uint32_t addressIndex = 0u;
  uint32_t member = 0u;

  if (dclOp.getType().isStructType())
    member = uint32_t(addressOp.getOperand(addressIndex++));

  /* Find which member this maps to within the push data struct */
  auto iter = std::find_if(m_pushData.members.begin(), m_pushData.members.end(),
    [&] (const PushDataInfo& e) {
      return e.def == dclOp.getDef() && e.member == member;
    });

  dxbc_spv_assert(iter != m_pushData.members.end());

  member = std::distance(m_pushData.members.begin(), iter);

  /* Emit access chain into push data block */
  auto typeId = getIdForType(op.getType());

  auto ptrTypeId = getIdForPtrType(typeId, spv::StorageClassPushConstant);
  auto ptrId = allocId();

  m_code.push_back(makeOpcodeToken(spv::OpInBoundsAccessChain,
    5u + addressOp.getOperandCount() - addressIndex));
  m_code.push_back(ptrTypeId);
  m_code.push_back(ptrId);
  m_code.push_back(getIdForPushDataBlock());
  m_code.push_back(makeConstU32(member));

  for (uint32_t i = addressIndex; i < addressOp.getOperandCount(); i++)
    m_code.push_back(makeConstU32(uint32_t(addressOp.getOperand(i))));

  /* Emit actual load */
  auto id = getIdForDef(op.getDef());
  pushOp(m_code, spv::OpLoad, typeId, id, ptrId);

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitConstantLoad(const ir::Op& op) {
  /* Declare private variable for constant data */
  const auto& constantOp = m_builder.getOpForOperand(op, 0u);
  dxbc_spv_assert(constantOp.isConstant());

  auto entry = m_constantVars.find(constantOp.getDef());

  uint32_t varId = 0u;

  if (entry == m_constantVars.end()) {
    varId = allocId();

    auto typeId = getIdForType(constantOp.getType());
    auto ptrTypeId = getIdForPtrType(typeId, spv::StorageClassPrivate);

    pushOp(m_declarations, spv::OpVariable, ptrTypeId, varId,
      spv::StorageClassPrivate, getIdForDef(constantOp.getDef()));

    addEntryPointId(varId);
    emitDebugName(constantOp.getDef(), varId);

    m_constantVars.insert({ constantOp.getDef(), varId });
  } else {
    varId = entry->second;
  }

  /* Emit access chain to access private variable */
  auto addressDef = ir::SsaDef(op.getOperand(1u));

  auto accessChainId = emitAccessChain(
    getAccessChainOp(op), spv::StorageClassPrivate,
    constantOp.getType(), varId, addressDef, 0u, false);

  auto id = getIdForDef(op.getDef());
  pushOp(m_code, spv::OpLoad, getIdForType(op.getType()), id, accessChainId);

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitDescriptorLoad(const ir::Op& op) {
  const auto& dclOp = m_builder.getOpForOperand(op, 0u);

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
  const auto& descriptorOp = m_builder.getOpForOperand(op, 0u);
  const auto& dclOp = m_builder.getOpForOperand(descriptorOp, 0u);

  bool isUav = descriptorOp.getType() == ir::ScalarType::eUav;
  bool isSparse = bool(op.getFlags() & ir::OpFlag::eSparseFeedback);

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

      if (getUavCoherentScope(uavFlags) != spv::ScopeInvocation)
        memoryOperands.flags |= spv::MemoryAccessNonPrivatePointerMask;
    }

    /* Emit access chains for loading the requested data. */
    util::small_vector<uint32_t, 4u> loadIds;

    auto storageClass = descriptorOp.getType() == ir::ScalarType::eCbv && !cbvAsSsbo(dclOp)
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
      loadIds.push_back(emitAccessChain(spv::OpAccessChain, storageClass, dclOp.getType(),
        getIdForDef(descriptorOp.getDef()), addressDef, 0u, hasWrapperStruct));
    } else {
      /* Need to emit multiple loads and assemble a vector later. */
      for (uint32_t i = 0u; i < accessType.getBaseType(0u).getVectorSize(); i++) {
        loadIds.push_back(emitAccessChain(spv::OpAccessChain, storageClass, dclOp.getType(),
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
  const auto& descriptorOp = m_builder.getOpForOperand(op, 0u);
  const auto& dclOp = m_builder.getOpForOperand(descriptorOp, 0u);

  const auto& valueOp = m_builder.getOpForOperand(op, 2u);

  auto addressDef = ir::SsaDef(op.getOperand(1u));
  auto uavFlags = getUavFlags(dclOp);

  if (declaresPlainBufferResource(dclOp)) {
    auto addressedType = traverseType(dclOp.getType(), addressDef);
    auto accessType = valueOp.getType();

    dxbc_spv_assert(addressedType == accessType ||
      (addressedType.isScalarType() && accessType.isVectorType()));

    /* Set up memory operands depending on resource usage */
    SpirvMemoryOperands memoryOperands = { };

    if (getUavCoherentScope(uavFlags) != spv::ScopeInvocation)
      memoryOperands.flags |= spv::MemoryAccessNonPrivatePointerMask;

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
      auto accessChainId = emitAccessChain(spv::OpAccessChain, spv::StorageClassStorageBuffer,
        dclOp.getType(), getIdForDef(descriptorOp.getDef()), addressDef, 0u, hasWrapperStruct);

      storeIds.push_back(std::make_pair(accessChainId, getIdForDef(valueOp.getDef())));
    } else {
      /* Extract scalars from vector and scalarize access chains */
      for (uint32_t i = 0u; i < accessType.getBaseType(0u).getVectorSize(); i++) {
        auto accessChainId = emitAccessChain(spv::OpAccessChain, spv::StorageClassStorageBuffer,
          dclOp.getType(), getIdForDef(descriptorOp.getDef()), addressDef, i, hasWrapperStruct);

        storeIds.push_back(std::make_pair(accessChainId,
          emitExtractComponent(valueOp.getDef(), i)));
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
  const auto& descriptorOp = m_builder.getOpForOperand(op, 0u);
  const auto& dclOp = m_builder.getOpForOperand(descriptorOp, 0u);

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
  const auto& descriptorOp = m_builder.getOpForOperand(op, 0u);
  const auto& dclOp = m_builder.getOpForOperand(descriptorOp, 0u);

  auto addressDef = ir::SsaDef(op.getOperand(1u));
  auto operandDef = ir::SsaDef(op.getOperand(2u));

  auto id = getIdForDef(op.getDef());

  if (declaresPlainBufferResource(dclOp)) {
    auto type = traverseType(dclOp.getType(), addressDef);

    dxbc_spv_assert(type.isScalarType());

    /* Emit access chain, this will always point to a scalar. */
    bool hasWrapperStruct = !dclOp.getType().isStructType();

    auto accessChainId = emitAccessChain(spv::OpAccessChain, spv::StorageClassStorageBuffer,
      dclOp.getType(), getIdForDef(descriptorOp.getDef()), addressDef, 0u, hasWrapperStruct);

    if (descriptorOp.getFlags() & ir::OpFlag::eNonUniform)
      pushOp(m_decorations, spv::OpDecorate, accessChainId, spv::DecorationNonUniform);

    emitAtomic(op, type, id, operandDef, accessChainId, spv::ScopeQueueFamily,
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

    emitAtomic(op, type, id, operandDef, ptrId, spv::ScopeQueueFamily,
      spv::MemorySemanticsImageMemoryMask);
  }
}


void SpirvBuilder::emitMemoryLoad(const ir::Op& op) {
  const auto& ptrOp = m_builder.getOpForOperand(op, 0u);
  auto addressDef = ir::SsaDef(op.getOperand(1u));

  /* Set up memory operands based on pointer properties */
  auto ptrFlags = ir::UavFlags(ptrOp.getOperand(1u));

  SpirvMemoryOperands memoryOperands = { };
  memoryOperands.flags |= spv::MemoryAccessAlignedMask;
  memoryOperands.alignment = uint32_t(op.getOperand(op.getFirstLiteralOperandIndex()));

  if (getUavCoherentScope(ptrFlags) != spv::ScopeInvocation)
    memoryOperands.flags |= spv::MemoryAccessNonPrivatePointerMask;

  /* Emit access chain */
  bool hasWrapperStruct = !ptrOp.getType().isStructType();

  auto accessChainId = emitAccessChain(spv::OpAccessChain, spv::StorageClassPhysicalStorageBuffer,
    ptrOp.getType(), getIdForDef(ptrOp.getDef()), addressDef, 0u, hasWrapperStruct);

  /* Emit load op */
  auto id = getIdForDef(op.getDef());

  m_code.push_back(makeOpcodeToken(spv::OpLoad, 4u + memoryOperands.computeDwordCount()));
  m_code.push_back(getIdForType(op.getType()));
  m_code.push_back(id);
  m_code.push_back(accessChainId);
  memoryOperands.pushTo(m_code);

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitMemoryStore(const ir::Op& op) {
  const auto& ptrOp = m_builder.getOpForOperand(op, 0u);

  auto addressDef = ir::SsaDef(op.getOperand(1u));
  auto valueDef = ir::SsaDef(op.getOperand(2u));

  /* Set up memory operands based on pointer properties */
  auto ptrFlags = ir::UavFlags(ptrOp.getOperand(1u));

  SpirvMemoryOperands memoryOperands = { };
  memoryOperands.flags |= spv::MemoryAccessAlignedMask;
  memoryOperands.alignment = uint32_t(op.getOperand(op.getFirstLiteralOperandIndex()));

  if (getUavCoherentScope(ptrFlags) != spv::ScopeInvocation)
    memoryOperands.flags |= spv::MemoryAccessNonPrivatePointerMask;

  /* Emit access chain */
  bool hasWrapperStruct = !ptrOp.getType().isStructType();

  auto accessChainId = emitAccessChain(spv::OpAccessChain, spv::StorageClassPhysicalStorageBuffer,
    ptrOp.getType(), getIdForDef(ptrOp.getDef()), addressDef, 0u, hasWrapperStruct);

  /* Emit store op */
  m_code.push_back(makeOpcodeToken(spv::OpStore, 3u + memoryOperands.computeDwordCount()));
  m_code.push_back(accessChainId);
  m_code.push_back(getIdForDef(valueDef));
  memoryOperands.pushTo(m_code);
}


void SpirvBuilder::emitMemoryAtomic(const ir::Op& op) {
  const auto& ptrOp = m_builder.getOpForOperand(op, 0u);

  auto addressDef = ir::SsaDef(op.getOperand(1u));
  auto operandDef = ir::SsaDef(op.getOperand(2u));

  auto type = traverseType(ptrOp.getType(), addressDef);

  /* Emit access chain */
  bool hasWrapperStruct = !ptrOp.getType().isStructType();

  auto accessChainId = emitAccessChain(spv::OpAccessChain, spv::StorageClassPhysicalStorageBuffer,
    ptrOp.getType(), getIdForDef(ptrOp.getDef()), addressDef, 0u, hasWrapperStruct);

  /* Emit atomic */
  emitAtomic(op, type, getIdForDef(op.getDef()), operandDef, accessChainId,
    spv::ScopeQueueFamily, spv::MemorySemanticsUniformMemoryMask);
}


void SpirvBuilder::emitCounterAtomic(const ir::Op& op) {
  const auto& descriptorOp = m_builder.getOpForOperand(op, 0u);
  const auto& dclOp = m_builder.getOpForOperand(descriptorOp, 0u);

  auto accessChainId = emitAccessChain(spv::OpAccessChain, spv::StorageClassStorageBuffer,
    dclOp.getType(), getIdForDef(descriptorOp.getDef()), ir::SsaDef(), 0u, true);

  if (descriptorOp.getFlags() & ir::OpFlag::eNonUniform)
    pushOp(m_decorations, spv::OpDecorate, accessChainId, spv::DecorationNonUniform);

  auto id = getIdForDef(op.getDef());

  auto atomicOp = ir::AtomicOp(op.getOperand(op.getFirstLiteralOperandIndex()));
  auto atomicId = atomicOp == ir::AtomicOp::eDec ? allocId() : id;

  emitAtomic(op, dclOp.getType(), atomicId, ir::SsaDef(), accessChainId,
    spv::ScopeQueueFamily, spv::MemorySemanticsUniformMemoryMask);

  /* For counter decrement, we need to return the new value */
  if (atomicOp == ir::AtomicOp::eDec)
    pushOp(m_code, spv::OpISub, getIdForType(op.getType()), id, atomicId, makeConstU32(1u));
}


void SpirvBuilder::emitLdsAtomic(const ir::Op& op) {
  /* Trivially traverse LDS type and emit atomic operation */
  const auto& dclOp = m_builder.getOpForOperand(op, 0u);

  auto addressDef = ir::SsaDef(op.getOperand(1u));
  auto operandDef = ir::SsaDef(op.getOperand(2u));

  auto type = traverseType(dclOp.getType(), addressDef);

  auto ptrId = emitAccessChain(getAccessChainOp(op), spv::StorageClassWorkgroup,
    dclOp.getType(), getIdForDef(dclOp.getDef()), addressDef, 0u, false);

  emitAtomic(op, type, getIdForDef(op.getDef()), operandDef, ptrId,
    spv::ScopeWorkgroup, spv::MemorySemanticsWorkgroupMemoryMask);
}


uint32_t SpirvBuilder::emitSampledImage(const ir::SsaDef& imageDef, const ir::SsaDef& samplerDef) {
  /* Determine image type for given image descriptor */
  const auto& imageOp = m_builder.getOp(imageDef);
  dxbc_spv_assert(imageOp.getType() == ir::ScalarType::eSrv);

  auto imageTypeId = m_descriptorTypes.at(ir::SsaDef(imageOp.getOperand(0u)));

  auto resultTypeId = getIdForSampledImageType(imageTypeId);
  auto resultId = allocId();

  pushOp(m_code, spv::OpSampledImage, resultTypeId, resultId,
    getIdForDef(imageDef), getIdForDef(samplerDef));

  auto flags = m_builder.getOp(imageDef).getFlags() |
               m_builder.getOp(samplerDef).getFlags();

  if (flags & ir::OpFlag::eNonUniform)
    pushOp(m_decorations, spv::OpDecorate, resultId, spv::DecorationNonUniform);

  return resultId;
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
  const auto& descriptorOp = m_builder.getOpForOperand(op, 0u);
  const auto& dclOp = m_builder.getOpForOperand(descriptorOp, 0u);

  auto id = getIdForDef(op.getDef());

  bool isUav = descriptorOp.getType() == ir::ScalarType::eUav;
  bool isSparse = bool(op.getFlags() & ir::OpFlag::eSparseFeedback);

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
  auto layerDef = ir::SsaDef(op.getOperand(2u));
  auto coordDef = ir::SsaDef(op.getOperand(3u));

  auto coordId = emitMergeImageCoordLayer(coordDef, layerDef);

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


void SpirvBuilder::emitImageStore(const ir::Op& op) {
  const auto& descriptorOp = m_builder.getOpForOperand(op, 0u);
  const auto& dclOp = m_builder.getOpForOperand(descriptorOp, 0u);

  /* Set up image operands */
  SpirvImageOperands imageOperands = { };
  setUavImageWriteOperands(imageOperands, dclOp);

  /* Build final coordinate vector */
  auto layerDef = ir::SsaDef(op.getOperand(1u));
  auto coordDef = ir::SsaDef(op.getOperand(2u));

  auto coordId = emitMergeImageCoordLayer(coordDef, layerDef);

  /* Value to store */
  auto valueDef = ir::SsaDef(op.getOperand(3u));

  /* Emit image store */
  m_code.push_back(makeOpcodeToken(spv::OpImageWrite, 4u + imageOperands.computeDwordCount()));
  m_code.push_back(getIdForDef(descriptorOp.getDef()));
  m_code.push_back(coordId);
  m_code.push_back(getIdForDef(valueDef));
  imageOperands.pushTo(m_code);
}


void SpirvBuilder::emitImageAtomic(const ir::Op& op) {
  const auto& descriptorOp = m_builder.getOpForOperand(op, 0u);
  const auto& dclOp = m_builder.getOpForOperand(descriptorOp, 0u);

  /* Build coordinate vector */
  auto layerDef = ir::SsaDef(op.getOperand(1u));
  auto coordDef = ir::SsaDef(op.getOperand(2u));
  auto operandDef = ir::SsaDef(op.getOperand(3u));

  auto coordId = emitMergeImageCoordLayer(coordDef, layerDef);

  /* OpImageTexelPointer takes a pointer to an image descriptor */
  auto type = dclOp.getType();

  auto ptrTypeId = getIdForPtrType(getIdForType(type), spv::StorageClassImage);
  auto ptrId = allocId();

  pushOp(m_code, spv::OpImageTexelPointer, ptrTypeId, ptrId,
    getImageDescriptorPointer(descriptorOp), coordId,
    makeConstU32(0u));

  /* Declare pointer as non-uniform if necessary */
  if (descriptorOp.getFlags() & ir::OpFlag::eNonUniform)
    pushOp(m_decorations, spv::OpDecorate, ptrId, spv::DecorationNonUniform);

  emitAtomic(op, type, getIdForDef(op.getDef()), operandDef, ptrId,
    spv::ScopeQueueFamily, spv::MemorySemanticsImageMemoryMask);
}


void SpirvBuilder::emitImageSample(const ir::Op& op) {
  auto imageDef = ir::SsaDef(op.getOperand(0u));
  auto samplerDef = ir::SsaDef(op.getOperand(1u));

  auto sampledImageId = emitSampledImage(imageDef, samplerDef);

  /* Build final coordinate vector */
  auto layerDef = ir::SsaDef(op.getOperand(2u));
  auto coordDef = ir::SsaDef(op.getOperand(3u));

  auto coordId = emitMergeImageCoordLayer(coordDef, layerDef);

  /* Set up image operands with optional arguments */
  SpirvImageOperands imageOperands = { };

  /* Handle constant offset */
  auto offsetDef = ir::SsaDef(op.getOperand(4u));

  if (offsetDef) {
    imageOperands.flags |= spv::ImageOperandsConstOffsetMask;
    imageOperands.constOffset = getIdForDef(offsetDef);
  }

  /* Handle explicit LOD index */
  auto lodIndexDef = ir::SsaDef(op.getOperand(5u));

  if (lodIndexDef) {
    imageOperands.flags |= spv::ImageOperandsLodMask;
    imageOperands.lodIndex = getIdForDef(lodIndexDef);
  }

  /* Handle LOD bias for implicit LOD */
  auto lodBiasDef = ir::SsaDef(op.getOperand(6u));

  if (lodBiasDef) {
    imageOperands.flags |= spv::ImageOperandsBiasMask;
    imageOperands.lodBias = getIdForDef(lodBiasDef);
  }

  /* Handle minimum LOD clamp for implicit LOD */
  auto lodClampDef = ir::SsaDef(op.getOperand(7u));

  if (lodClampDef) {
    enableCapability(spv::CapabilityMinLod);

    imageOperands.flags |= spv::ImageOperandsMinLodMask;
    imageOperands.minLod = getIdForDef(lodClampDef);
  }

  /* Handle derivatives for explicit LOD */
  auto derivXDef = ir::SsaDef(op.getOperand(8u));
  auto derivYDef = ir::SsaDef(op.getOperand(9u));

  if (derivXDef && derivYDef) {
    imageOperands.flags |= spv::ImageOperandsGradMask;
    imageOperands.gradX = getIdForDef(derivXDef);
    imageOperands.gradY = getIdForDef(derivYDef);
  }

  /* Depth reference. If not null, this is a depth-compare op. */
  auto depthCompareDef = ir::SsaDef(op.getOperand(10u));

  /* Select opcode based on op properties */
  bool isSparse = bool(op.getFlags() & ir::OpFlag::eSparseFeedback);

  if (isSparse)
    enableCapability(spv::CapabilitySparseResidency);

  bool isExplicitLod = imageOperands.flags & (spv::ImageOperandsLodMask | spv::ImageOperandsGradMask);
  bool isDepthCompare = bool(depthCompareDef);

  auto opCode = isDepthCompare
    ? (isExplicitLod
      ? (isSparse ? spv::OpImageSparseSampleDrefExplicitLod : spv::OpImageSampleDrefExplicitLod)
      : (isSparse ? spv::OpImageSparseSampleDrefImplicitLod : spv::OpImageSampleDrefImplicitLod))
    : (isExplicitLod
      ? (isSparse ? spv::OpImageSparseSampleExplicitLod : spv::OpImageSampleExplicitLod)
      : (isSparse ? spv::OpImageSparseSampleImplicitLod : spv::OpImageSampleImplicitLod));

  /* Emit actual image sample instruction */
  auto id = getIdForDef(op.getDef());

  m_code.push_back(makeOpcodeToken(opCode,
    5u + (isDepthCompare ? 1u : 0u) + imageOperands.computeDwordCount()));
  m_code.push_back(getIdForType(op.getType()));
  m_code.push_back(id);
  m_code.push_back(sampledImageId);
  m_code.push_back(coordId);

  if (isDepthCompare)
    m_code.push_back(getIdForDef(depthCompareDef));

  imageOperands.pushTo(m_code);

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitImageGather(const ir::Op& op) {
  auto imageDef = ir::SsaDef(op.getOperand(0u));
  auto samplerDef = ir::SsaDef(op.getOperand(1u));

  auto sampledImageId = emitSampledImage(imageDef, samplerDef);

  /* Build final coordinate vector */
  auto layerDef = ir::SsaDef(op.getOperand(2u));
  auto coordDef = ir::SsaDef(op.getOperand(3u));

  auto coordId = emitMergeImageCoordLayer(coordDef, layerDef);

  /* Set up image operands with optional arguments */
  SpirvImageOperands imageOperands = { };

  /* Handle offset. This may not actually be constant,  */
  auto offsetDef = ir::SsaDef(op.getOperand(4u));

  if (offsetDef) {
    if (m_builder.getOp(offsetDef).isConstant()) {
      imageOperands.flags |= spv::ImageOperandsConstOffsetMask;
      imageOperands.constOffset = getIdForDef(offsetDef);
    } else {
      enableCapability(spv::CapabilityImageGatherExtended);

      imageOperands.flags |= spv::ImageOperandsOffsetMask;
      imageOperands.dynamicOffset = getIdForDef(offsetDef);
    }
  }

  /* Depth reference. If not null, this is a depth-compare op. */
  auto depthCompareDef = ir::SsaDef(op.getOperand(5u));

  /* Component to gather */
  auto component = uint32_t(op.getOperand(6u));

  /* Select opcode */
  bool isSparse = bool(op.getFlags() & ir::OpFlag::eSparseFeedback);

  if (isSparse)
    enableCapability(spv::CapabilitySparseResidency);

  bool isDepthCompare = bool(depthCompareDef);

  auto opCode = isDepthCompare
    ? (isSparse ? spv::OpImageSparseDrefGather : spv::OpImageDrefGather)
    : (isSparse ? spv::OpImageSparseGather : spv::OpImageGather);

  /* Emit actual image gather instruction */
  auto id = getIdForDef(op.getDef());

  m_code.push_back(makeOpcodeToken(opCode, 6u + imageOperands.computeDwordCount()));
  m_code.push_back(getIdForType(op.getType()));
  m_code.push_back(id);
  m_code.push_back(sampledImageId);
  m_code.push_back(coordId);

  if (isDepthCompare)
    m_code.push_back(getIdForDef(depthCompareDef));
  else
    m_code.push_back(makeConstU32(component));

  imageOperands.pushTo(m_code);

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitImageComputeLod(const ir::Op& op) {
  enableCapability(spv::CapabilityImageQuery);

  auto imageDef = ir::SsaDef(op.getOperand(0u));
  auto samplerDef = ir::SsaDef(op.getOperand(1u));
  auto coordDef = ir::SsaDef(op.getOperand(2u));

  auto sampledImageId = emitSampledImage(imageDef, samplerDef);

  auto id = getIdForDef(op.getDef());

  pushOp(m_code, spv::OpImageQueryLod,
    getIdForType(op.getType()), id, sampledImageId,
    getIdForDef(coordDef));

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitImageQuerySize(const ir::Op& op) {
  enableCapability(spv::CapabilityImageQuery);

  const auto& descriptorOp = m_builder.getOpForOperand(op, 0u);
  const auto& dclOp = m_builder.getOpForOperand(descriptorOp, 0u);

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

  auto queryTypeId = getIdForType(ir::BasicType(scalarType, queryComponents));
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

  const auto& descriptorOp = m_builder.getOpForOperand(op, 0u);

  dxbc_spv_assert(descriptorOp.getType() == ir::ScalarType::eSrv);
  dxbc_spv_assert(!resourceIsMultisampled(getResourceKind(
    m_builder.getOpForOperand(descriptorOp, 0u))));

  auto id = getIdForDef(op.getDef());

  pushOp(m_code, spv::OpImageQueryLevels,
    getIdForType(op.getType()), id,
    getIdForDef(descriptorOp.getDef()));

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitImageQuerySamples(const ir::Op& op) {
  enableCapability(spv::CapabilityImageQuery);

  const auto& descriptorOp = m_builder.getOpForOperand(op, 0u);

  dxbc_spv_assert(resourceIsMultisampled(getResourceKind(
    m_builder.getOpForOperand(descriptorOp, 0u))));

  auto id = getIdForDef(op.getDef());

  pushOp(m_code, spv::OpImageQuerySamples,
    getIdForType(op.getType()), id,
    getIdForDef(descriptorOp.getDef()));

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitConvert(const ir::Op& op) {
  /* Everything can be a vector here */
  dxbc_spv_assert(op.getType().isBasicType());

  const auto& srcOp = m_builder.getOpForOperand(op, 0u);
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

  emitDebugName(op.getDef(), dstId);
}


void SpirvBuilder::emitDerivative(const ir::Op& op) {
  /* Select SPIR-V opcode based on the selected derivative mode */
  auto mode = ir::DerivativeMode(op.getOperand(op.getFirstLiteralOperandIndex()));

  if (mode != ir::DerivativeMode::eDefault)
    enableCapability(spv::CapabilityDerivativeControl);

  auto opCode = [&] {
    bool isX = op.getOpCode() == ir::OpCode::eDerivX;

    switch (mode) {
      case ir::DerivativeMode::eDefault:
        return isX ? spv::OpDPdx : spv::OpDPdy;

      case ir::DerivativeMode::eFine:
        return isX ? spv::OpDPdxFine : spv::OpDPdyFine;

      case ir::DerivativeMode::eCoarse:
        return isX ? spv::OpDPdxCoarse : spv::OpDPdyCoarse;
    }

    dxbc_spv_unreachable();
    return spv::OpNop;
  } ();

  /* Emit instruction */
  auto id = getIdForDef(op.getDef());

  pushOp(m_code, opCode, getIdForType(op.getType()), id,
    getIdForDef(ir::SsaDef(op.getOperand(0u))));

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitAtomic(const ir::Op& op, const ir::Type& type, uint32_t id,
    ir::SsaDef operandDef, uint32_t ptrId, spv::Scope scope, spv::MemorySemanticsMask memoryTypes) {
  dxbc_spv_assert(op.getType().isVoidType() || op.getType() == type);

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

  dxbc_spv_assert(m_builder.getOp(operandDef).getType() == (argCount
    ? ir::BasicType(type.getBaseType(0u).getBaseType(), argCount)
    : ir::BasicType()));

  /* Set up memory semantics */
  auto semantics = spv::MemorySemanticsAcquireReleaseMask;

  if (atomicOp == ir::AtomicOp::eLoad) {
    semantics = spv::MemorySemanticsAcquireMask |
                spv::MemorySemanticsMakeVisibleMask;
  } else if (atomicOp == ir::AtomicOp::eStore) {
    semantics = spv::MemorySemanticsReleaseMask |
                spv::MemorySemanticsMakeAvailableMask;
  }

  /* Decompose composite arg */
  std::array<uint32_t, 2u> argIds = { };

  for (uint32_t i = 0u; i < argCount; i++)
    argIds.at(i) = emitExtractComponent(operandDef, i);

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

  if (atomicOp == ir::AtomicOp::eCompareExchange) {
    m_code.push_back(makeConstU32(memoryTypes | spv::MemorySemanticsAcquireMask));

    /* Operands are in reverse order here */
    m_code.push_back(argIds.at(1u));
    m_code.push_back(argIds.at(0u));
  } else if (argCount) {
    m_code.push_back(argIds.at(0u));
  }

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitLabel(const ir::Op& op) {
  m_structure.blockLabel = op;

  pushOp(m_code, spv::OpLabel, getIdForDef(op.getDef()));
}


void SpirvBuilder::emitBranch(const ir::Op& op) {
  emitStructuredInfo(m_structure.blockLabel);

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
    const auto& constant = m_builder.getOpForOperand(op, i);
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
      getIdForDef(ir::SsaDef(op.getOperand(0u))));
  }

  m_structure.blockLabel = ir::Op();
}


void SpirvBuilder::emitPhi(const ir::Op& op) {
  m_code.push_back(makeOpcodeToken(spv::OpPhi, 3u + op.getOperandCount()));
  m_code.push_back(getIdForType(op.getType()));
  m_code.push_back(getIdForDef(op.getDef()));

  /* Phi operands are the other way around */
  for (uint32_t i = 0u; i < op.getOperandCount(); i += 2u) {
    m_code.push_back(getIdForDef(ir::SsaDef(op.getOperand(i + 1u))));
    m_code.push_back(getIdForDef(ir::SsaDef(op.getOperand(i))));
  }
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


void SpirvBuilder::emitBarrier(const ir::Op& op) {
  auto execScope = ir::Scope(op.getOperand(0u));
  auto memScope = ir::Scope(op.getOperand(1u));
  auto memTypes = ir::MemoryTypeFlags(op.getOperand(2u));

  if (execScope == ir::Scope::eThread) {
    pushOp(m_code, spv::OpMemoryBarrier,
      makeConstU32(translateScope(memScope)),
      makeConstU32(translateMemoryTypes(memTypes, spv::MemorySemanticsAcquireReleaseMask)));
  } else {
    pushOp(m_code, spv::OpControlBarrier,
      makeConstU32(translateScope(execScope)),
      makeConstU32(translateScope(memScope)),
      makeConstU32(translateMemoryTypes(memTypes, spv::MemorySemanticsAcquireReleaseMask)));
  }
}


void SpirvBuilder::emitGsEmit(const ir::Op& op) {
  bool needsStreamIndex = isMultiStreamGs();

  auto opCode = [&] {
    switch (op.getOpCode()) {
      case ir::OpCode::eEmitVertex:
        return needsStreamIndex
          ? spv::OpEmitStreamVertex
          : spv::OpEmitVertex;

      case ir::OpCode::eEmitPrimitive:
        return needsStreamIndex
          ? spv::OpEndStreamPrimitive
          : spv::OpEndPrimitive;

      default:
        dxbc_spv_unreachable();
        return spv::OpNop;
    }
  } ();

  m_code.push_back(makeOpcodeToken(opCode, needsStreamIndex ? 2u : 1u));

  if (needsStreamIndex) {
    auto streamIndex = uint32_t(op.getOperand(0u));
    m_code.push_back(makeConstU32(streamIndex));
  }
}


void SpirvBuilder::emitDemote() {
  enableCapability(spv::CapabilityDemoteToHelperInvocation);

  pushOp(m_code, spv::OpDemoteToHelperInvocation);
}


void SpirvBuilder::emitRovLockBegin(const ir::Op& op) {
  dxbc_spv_assert(m_stage == ir::ShaderStage::ePixel);

  /* Enable cap and execution mode for lock scope */
  auto scope = ir::RovScope(op.getOperand(1u));

  auto [cap, mode] = [&] {
    switch (scope) {
      case ir::RovScope::eSample:
        return std::make_pair(
          spv::CapabilityFragmentShaderSampleInterlockEXT,
          spv::ExecutionModeSampleInterlockOrderedEXT);

      case ir::RovScope::ePixel:
        return std::make_pair(
          spv::CapabilityFragmentShaderPixelInterlockEXT,
          spv::ExecutionModePixelInterlockOrderedEXT);

      case ir::RovScope::eVrsBlock:
        return std::make_pair(
          spv::CapabilityFragmentShaderShadingRateInterlockEXT,
          spv::ExecutionModeShadingRateInterlockOrderedEXT);

      case ir::RovScope::eFlagEnum:
        break;
    }

    dxbc_spv_unreachable();
    return std::make_pair(spv::Capability(), spv::ExecutionMode());
  } ();

  enableCapability(cap);

  pushOp(m_executionModes, spv::OpExecutionMode, m_entryPointId, mode);

  /* Begin locked scope */
  pushOp(m_code, spv::OpBeginInvocationInterlockEXT);

  /* Emit memory barrier to make prior writes visible */
  auto memoryTypes = ir::MemoryTypeFlags(op.getOperand(0u));
  dxbc_spv_assert(memoryTypes);

  pushOp(m_code, spv::OpMemoryBarrier, makeConstU32(spv::ScopeQueueFamily),
    makeConstU32(translateMemoryTypes(memoryTypes, spv::MemorySemanticsAcquireMask)));
}


void SpirvBuilder::emitRovLockEnd(const ir::Op& op) {
  dxbc_spv_assert(m_stage == ir::ShaderStage::ePixel);

  /* Emit memory barrier to make prior writes available */
  auto memoryTypes = ir::MemoryTypeFlags(op.getOperand(0u));
  dxbc_spv_assert(memoryTypes);

  pushOp(m_code, spv::OpMemoryBarrier, makeConstU32(spv::ScopeQueueFamily),
    makeConstU32(translateMemoryTypes(memoryTypes, spv::MemorySemanticsReleaseMask)));

  /* End locked scope */
  pushOp(m_code, spv::OpEndInvocationInterlockEXT);
}


void SpirvBuilder::emitPointer(const ir::Op& op) {
  auto addressDef = ir::SsaDef(op.getOperand(0u));
  auto flags = ir::UavFlags(op.getOperand(1u));

  auto id = getIdForDef(op.getDef());
  auto typeId = getIdForBdaType(op.getType(), flags);

  pushOp(m_code, spv::OpBitcast, typeId, id, getIdForDef(addressDef));

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitDrain(const ir::Op& op) {
  const auto& type = op.getType();

  if (!type.isVoidType()) {
    pushOp(m_code, spv::OpCopyObject,
      getIdForType(type),
      getIdForDef(op.getDef()),
      getIdForDef(ir::SsaDef(op.getOperand(0u))));
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


void SpirvBuilder::emitFpMode(const ir::Op& op, uint32_t id, uint32_t mask) {
  const auto& type = op.getType();

  if (!opSupportsFpMode(op))
    return;

  /* Find correct default mode for type */
  auto scalarType = type.getBaseType(0u).getBaseType();

  if (type.getBaseType(0u).isBoolType())
    scalarType = m_builder.getOpForOperand(op, 0u).getType().getBaseType(0u).getBaseType();

  auto desiredMode = op.getFlags();
  auto defaultMode = [&] {
    switch (scalarType) {
      case ir::ScalarType::eF16: return m_fpMode.f16;
      case ir::ScalarType::eF32: return m_fpMode.f32;
      case ir::ScalarType::eF64: return m_fpMode.f64;
      default: break;
    }

    dxbc_spv_unreachable();
    return ir::OpFlags();
  } ();

  /* Compare instructions do not use FP hints in our IR, but may in SPIR-V */
  if (type.getBaseType(0u).isBoolType())
    desiredMode = defaultMode;

  /* The notnan, notinf and nosz flags in SPIR-V extend to the operands rather than
   * just the result of the instruction, which does not match our IR. Scan all float
   * operands of the instruction for these flags and AND them together. */
  auto fpHints = desiredMode & (ir::OpFlag::eNoInf | ir::OpFlag::eNoNan | ir::OpFlag::eNoSz);

  if (fpHints) {
    desiredMode -= fpHints;

    for (uint32_t i = 0u; i < op.getFirstLiteralOperandIndex(); i++) {
      const auto& arg = m_builder.getOpForOperand(op, i);

      if (arg && arg.getType().isBasicType() && arg.getType().getBaseType(0u).isFloatType())
        fpHints &= arg.getFlags();
    }

    desiredMode |= fpHints;
  }

  if (m_options.floatControls2) {
    /* Fp mode flags in our IR are additive, only emit a decoration if
     * the instruction sets any flags not included in the default. */
    auto defaultFlags = getFpModeFlags(defaultMode);
    auto desiredFlags = getFpModeFlags(defaultMode | desiredMode) | mask;

    if (desiredFlags != defaultFlags) {
      enableCapability(spv::CapabilityFloatControls2);
      pushOp(m_decorations, spv::OpDecorate, id, spv::DecorationFPFastMathMode, desiredFlags);
    }
  } else {
    /* Enable no-contraction mode for the instruction if necessary */
    if ((defaultMode | desiredMode) & ir::OpFlag::ePrecise)
      pushOp(m_decorations, spv::OpDecorate, id, spv::DecorationNoContraction);
  }
}


bool SpirvBuilder::opSupportsFpMode(const ir::Op& op) {
  if (!op.getType().isBasicType())
    return false;

  /* Allow boolean types for comparisons */
  auto type = op.getType().getBaseType(0u);

  if (!type.isFloatType() && !type.isBoolType())
    return false;

  /* SPIR-V only allows FP mode decorations on specific instructions,
   * with float_controls2 this includes all extended instructions. */
  auto opCode = op.getOpCode();

  return opCode == ir::OpCode::eFAbs ||
         opCode == ir::OpCode::eFNeg ||
         opCode == ir::OpCode::eFAdd ||
         opCode == ir::OpCode::eFSub ||
         opCode == ir::OpCode::eFMul ||
         opCode == ir::OpCode::eFMad ||
         opCode == ir::OpCode::eFDiv ||
         opCode == ir::OpCode::eFRcp ||
         opCode == ir::OpCode::eFSqrt ||
         opCode == ir::OpCode::eFRsq ||
         opCode == ir::OpCode::eFExp2 ||
         opCode == ir::OpCode::eFLog2 ||
         opCode == ir::OpCode::eFFract ||
         opCode == ir::OpCode::eFRound ||
         opCode == ir::OpCode::eFMin ||
         opCode == ir::OpCode::eFMax ||
         opCode == ir::OpCode::eFClamp ||
         opCode == ir::OpCode::eFSin ||
         opCode == ir::OpCode::eFCos ||
         opCode == ir::OpCode::eFEq ||
         opCode == ir::OpCode::eFNe ||
         opCode == ir::OpCode::eFGt ||
         opCode == ir::OpCode::eFGe ||
         opCode == ir::OpCode::eFLt ||
         opCode == ir::OpCode::eFLe;
}


void SpirvBuilder::emitDebugName(ir::SsaDef def, uint32_t id) {
  if (!m_options.includeDebugNames)
    return;

  auto e = m_debugNames.find(def);

  if (e == m_debugNames.end())
    return;

  const auto& debugOp = m_builder.getOp(e->second);

  if (debugOp.getOpCode() == ir::OpCode::eDebugName) {
    setDebugName(id, debugOp.getLiteralString(1u).c_str());
  } else if (debugOp.getOpCode() == ir::OpCode::eSemantic) {
    auto index = uint32_t(debugOp.getOperand(1u));

    if (index) {
      std::stringstream str;
      str << debugOp.getLiteralString(2u);
      str << index;
      setDebugName(id, str.str().c_str());
    } else {
      setDebugName(id, debugOp.getLiteralString(2u).c_str());
    }
  } else {
    dxbc_spv_unreachable();
  }
}


void SpirvBuilder::emitDebugTypeName(ir::SsaDef def, uint32_t id, const char* suffix) {
  if (!m_options.includeDebugNames)
    return;

  auto e = m_debugNames.find(def);

  if (e == m_debugNames.end())
    return;

  const auto& debugOp = m_builder.getOp(e->second);

  if (debugOp.getOpCode() != ir::OpCode::eDebugName)
    return;

  std::stringstream str;
  str << debugOp.getLiteralString(1u);
  str << suffix;

  setDebugName(id, str.str().c_str());
}


void SpirvBuilder::emitDebugMemberNames(ir::SsaDef def, uint32_t structId) {
  auto [a, b] = m_builder.getUses(def);

  for (auto op = a; op != b; op++) {
    if (op->getOpCode() == ir::OpCode::eDebugMemberName) {
      dxbc_spv_assert(ir::SsaDef(op->getOperand(0u)) == def);

      auto member = uint32_t(op->getOperand(1u));
      setDebugMemberName(structId, member, op->getLiteralString(2u).c_str());
    }
  }
}


void SpirvBuilder::emitDebugPushDataName(const PushDataInfo& pushData, uint32_t structId, uint32_t memberIdx) {
  bool isStruct = m_builder.getOp(pushData.def).getType().isStructType();
  auto [a, b] = m_builder.getUses(pushData.def);

  for (auto op = a; op != b; op++) {
    if (op->getOpCode() == ir::OpCode::eDebugName && !isStruct) {
      dxbc_spv_assert(ir::SsaDef(op->getOperand(0u)) == pushData.def);

      setDebugMemberName(structId, memberIdx, op->getLiteralString(1u).c_str());
      return;
    } else if (op->getOpCode() == ir::OpCode::eDebugMemberName && isStruct) {
      dxbc_spv_assert(ir::SsaDef(op->getOperand(0u)) == pushData.def);

      if (pushData.member == uint32_t(op->getOperand(1u))) {
        setDebugMemberName(structId, memberIdx, op->getLiteralString(2u).c_str());
        return;
      }
    }
  }
}


void SpirvBuilder::emitFunction(const ir::Op& op) {
  uint32_t funcId = getIdForDef(op.getDef());
  emitDebugName(op.getDef(), funcId);

  /* Look up unique function type */
  SpirvFunctionTypeKey typeKey = { };
  typeKey.returnType = op.getType();

  for (uint32_t i = 0u; i < op.getOperandCount(); i++) {
    const auto& paramDef = m_builder.getOpForOperand(op, i);
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

    auto typeId = getIdForType(typeKey.paramTypes[i]);
    pushOp(m_code, spv::OpFunctionParameter, typeId, spvId);

    SpirvFunctionParameterKey key = { };
    key.funcDef = op.getDef();
    key.paramDef = paramDef;

    m_funcParamIds.insert({ key, spvId });
  }
}


void SpirvBuilder::emitFunctionEnd() {
  pushOp(m_code, spv::OpFunctionEnd);
}


void SpirvBuilder::emitFunctionCall(const ir::Op& op) {
  auto id = getIdForDef(op.getDef());

  m_code.push_back(makeOpcodeToken(spv::OpFunctionCall, 3u + op.getOperandCount()));
  m_code.push_back(getIdForType(op.getType()));
  m_code.push_back(id);

  for (uint32_t i = 0u; i < op.getOperandCount(); i++)
    m_code.push_back(getIdForDef(ir::SsaDef(op.getOperand(i))));

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitParamLoad(const ir::Op& op) {
  SpirvFunctionParameterKey key = { };
  key.funcDef = ir::SsaDef(op.getOperand(0u));
  key.paramDef = ir::SsaDef(op.getOperand(1u));

  dxbc_spv_assert(m_funcParamIds.find(key) != m_funcParamIds.end());

  auto typeId = getIdForType(op.getType());
  auto paramId = m_funcParamIds.at(key);
  auto id = paramId;

  if (hasIdForDef(op.getDef())) {
    id = getIdForDef(op.getDef());
    pushOp(m_code, spv::OpCopyObject, typeId, id, paramId);
  } else {
    setIdForDef(op.getDef(), paramId);
  }

  emitDebugName(op.getDef(), id);
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


uint32_t SpirvBuilder::emitAccessChain(spv::Op op, spv::StorageClass storageClass, const ir::Type& baseType,
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
  m_code.push_back(makeOpcodeToken(op, 4u + indexIds.size()));
  m_code.push_back(ptrTypeId);
  m_code.push_back(accessChainId);
  m_code.push_back(baseId);

    for (auto index : indexIds)
      m_code.push_back(index);

  return accessChainId;
}


uint32_t SpirvBuilder::emitRawStructuredElementAddress(const ir::Op& op, uint32_t stride) {
  dxbc_spv_assert(op.getType().isBasicType());

  if (op.isConstant())
    return makeConstU32(uint32_t(op.getOperand(0u)) * stride);

  auto baseId = 0u;

  if (op.getType().isScalarType()) {
    /* Use scalar index type as-is */
    baseId = getIdForDef(op.getDef());
  } else if (op.getOpCode() == ir::OpCode::eCompositeConstruct) {
    /* Resolve constants in composites directly */
    const auto& componentOp = m_builder.getOpForOperand(op, 0u);

    if (componentOp.isConstant())
      return makeConstU32(uint32_t(componentOp.getOperand(0u)) * stride);

    baseId = getIdForDef(componentOp.getDef());
  } else {
    /* Extract component */
    baseId = allocId();

    pushOp(m_code, spv::OpCompositeExtract,
      getIdForType(op.getType().getSubType(0u)), baseId,
      getIdForDef(op.getDef()), 0u);
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


uint32_t SpirvBuilder::emitStructuredByteOffset(const ir::Op& op, ir::Type type) {
  dxbc_spv_assert(op.getType().isBasicType());

  uint32_t constOffset = 0u;
  uint32_t resultId = 0u;

  for (uint32_t i = 1u; i < op.getType().getBaseType(0u).getVectorSize(); i++) {
    std::optional<uint32_t> memberIdx;

    if (op.isConstant()) {
      memberIdx = uint32_t(op.getOperand(i));
    } else if (op.getOpCode() == ir::OpCode::eCompositeConstruct) {
      const auto& componentOp = m_builder.getOpForOperand(op, i);

      if (componentOp.isConstant())
        memberIdx = uint32_t(componentOp.getOperand(0u));
    }

    if (memberIdx) {
      /* We can trivially determine the byte offset */
      constOffset += type.byteOffset(*memberIdx);
      type = type.getSubType(*memberIdx);
    } else {
      dxbc_spv_assert(type.isArrayType());
      type = type.getSubType(0u);

      /* Extract array index from component vector */
      auto indexId = allocId();

      pushOp(m_code, spv::OpCompositeExtract,
        getIdForType(op.getType().getSubType(0u)), indexId,
        getIdForDef(op.getDef()), i);

      /* Multiply with byte stride of the element type */
      auto offsetId = allocId();

      pushOp(m_code, spv::OpIMul,
        getIdForType(op.getType().getSubType(0u)), offsetId,
        indexId, makeConstU32(type.byteSize()));

      if (resultId) {
        auto sumId = allocId();

        pushOp(m_code, spv::OpIAdd,
          getIdForType(op.getType().getSubType(0u)), sumId,
          resultId, offsetId);

        resultId = sumId;
      } else {
        resultId = offsetId;
      }
    }
  }

  if (!resultId)
    return makeConstU32(constOffset);

  if (!constOffset)
    return resultId;

  auto sumId = allocId();

  pushOp(m_code, spv::OpIAdd,
    getIdForType(op.getType().getSubType(0u)), sumId,
    resultId, makeConstU32(constOffset));

  return sumId;
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
    auto byteOffsetId = emitRawStructuredElementAddress(addressOp,
      resourceOp.getType().getSubType(0u).byteSize());

    auto nullId = makeConstU32(0u);

    pushOp(m_code, spv::OpRawAccessChainNV, ptrTypeId,
      id, baseId, nullId, nullId, byteOffsetId,
      spv::RawAccessChainOperandsRobustnessPerComponentNVMask);
  } else if (kind == ir::ResourceKind::eBufferStructured) {
    dxbc_spv_assert(addressOp.getType().isBasicType());

    /* Compute byte size of the structure based on the resource type */
    auto structureType = resourceOp.getType().getSubType(0u);
    auto strideId = makeConstU32(structureType.byteSize());

    /* Compute structure index and byte offset into the structure */
    auto elementId = emitRawStructuredElementAddress(addressOp, 1u);
    auto byteOffsetId = emitStructuredByteOffset(addressOp, structureType);

    pushOp(m_code, spv::OpRawAccessChainNV, ptrTypeId,
      id, baseId, strideId, elementId, byteOffsetId,
      spv::RawAccessChainOperandsRobustnessPerElementNVMask);
  } else {
    /* Invalid kind */
    dxbc_spv_unreachable();
  }

  return id;
}


uint32_t SpirvBuilder::emitAccessChain(spv::Op op, spv::StorageClass storageClass, ir::SsaDef base, ir::SsaDef address, bool wrapped) {
  const auto& baseOp = m_builder.getOp(base);

  return emitAccessChain(op, storageClass, baseOp.getType(), getIdForDef(baseOp.getDef()), address, 0u, wrapped);
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


void SpirvBuilder::emitLoadGsVertexCountBuiltIn(const ir::Op& op) {
  auto constantId = makeConstU32(m_geometry.inputVertices);

  if (hasIdForDef(op.getDef())) {
    auto id = getIdForDef(op.getDef());

    auto uintTypeId = getIdForType(ir::ScalarType::eU32);
    pushOp(m_code, spv::OpCopyObject, uintTypeId, id, constantId);
  } else {
    setIdForDef(op.getDef(), constantId);
  }
}


void SpirvBuilder::emitLoadSamplePositionBuiltIn(const ir::Op& op) {
  /* Load requested components */
  auto type = op.getType().getBaseType(0u);
  auto typeId = getIdForType(op.getType());

  auto accessChainId = emitAccessChain(
    getAccessChainOp(op),
    getVariableStorageClass(op),
    ir::SsaDef(op.getOperand(0u)),
    ir::SsaDef(op.getOperand(1u)),
    false);

  auto loadId = allocId();
  pushOp(m_code, spv::OpLoad, typeId, loadId, accessChainId);

  /* Build offset to subtract */
  static const float constantOffset = 0.5f;

  SpirvConstant constant = { };
  constant.op = spv::OpConstant;
  constant.typeId = getIdForType(type.getBaseType());
  std::memcpy(&constant.constituents[0u], &constantOffset, sizeof(constantOffset));

  uint32_t offsetId = getIdForConstant(constant, 1u);

  if (type.isVector()) {
    constant.op = spv::OpConstantComposite;
    constant.typeId = typeId;

    for (uint32_t i = 0u; i < type.getVectorSize(); i++)
      constant.constituents[i] = offsetId;

    offsetId = getIdForConstant(constant, type.getVectorSize());
  }

  /* In Vulkan, the built-in returns sample positions insde the
   * [0..1] pixel grid, but we need to offset it to [-0.5..0.5]. */
  auto id = getIdForDef(op.getDef());
  pushOp(m_code, spv::OpFSub, typeId, id, loadId, offsetId);

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitLoadVariable(const ir::Op& op) {
  auto typeId = getIdForType(op.getType());

  /* Whether to index into a wrapper array */
  bool hasWrapperArray = false;

  /* Loading certain built-ins requires special care */
  if (op.getOpCode() == ir::OpCode::eInputLoad) {
    const auto& inputDcl = m_builder.getOpForOperand(op, 0u);

    if (inputDcl.getOpCode() == ir::OpCode::eDclInputBuiltIn) {
      auto builtIn = ir::BuiltIn(inputDcl.getOperand(1u));

      switch (builtIn) {
        case ir::BuiltIn::eVertexId:
        case ir::BuiltIn::eInstanceId:
          emitLoadDrawParameterBuiltIn(op, builtIn);
          return;

        case ir::BuiltIn::eGsVertexCountIn:
          emitLoadGsVertexCountBuiltIn(op);
          return;

        case ir::BuiltIn::eSamplePosition:
          emitLoadSamplePositionBuiltIn(op);
          return;

        case ir::BuiltIn::eSampleMask:
          hasWrapperArray = true;
          break;

        default:
          break;
      }
    }
  }

  /* Load regular variable */
  auto accessChainId = emitAccessChain(
    getAccessChainOp(op),
    getVariableStorageClass(op),
    ir::SsaDef(op.getOperand(0u)),
    ir::SsaDef(op.getOperand(1u)),
    hasWrapperArray);

  auto id = getIdForDef(op.getDef());

  SpirvMemoryOperands memoryOperands = { };

  if (op.getOpCode() == ir::OpCode::eLdsLoad)
    memoryOperands.flags |= spv::MemoryAccessNonPrivatePointerMask;

  m_code.push_back(makeOpcodeToken(spv::OpLoad, 4u + memoryOperands.computeDwordCount()));
  m_code.push_back(typeId);
  m_code.push_back(id);
  m_code.push_back(accessChainId);
  memoryOperands.pushTo(m_code);

  /* When loading a control point output in a hull shader, we
   * need to ensure that a barrier is properly inserted. */
  if (m_stage == ir::ShaderStage::eHull && op.getOpCode() == ir::OpCode::eOutputLoad) {
    const auto& outputDcl = m_builder.getOpForOperand(op, 0u);
    m_tessControl.needsIoBarrier |= !isPatchConstant(outputDcl);
  }

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitStoreVariable(const ir::Op& op) {
  /* Whether to index into a wrapper array */
  bool hasWrapperArray = false;

  if (op.getOpCode() == ir::OpCode::eOutputStore) {
    const auto& outputDcl = m_builder.getOpForOperand(op, 0u);

    if (outputDcl.getOpCode() == ir::OpCode::eDclOutputBuiltIn) {
      auto builtIn = ir::BuiltIn(outputDcl.getOperand(1u));
      hasWrapperArray = builtIn == ir::BuiltIn::eSampleMask;
    }
  }

  auto accessChainId = emitAccessChain(
    getAccessChainOp(op),
    getVariableStorageClass(op),
    ir::SsaDef(op.getOperand(0u)),
    ir::SsaDef(op.getOperand(1u)),
    hasWrapperArray);

  auto valueId = getIdForDef(ir::SsaDef(op.getOperand(2u)));

  SpirvMemoryOperands memoryOperands = { };

  if (op.getOpCode() == ir::OpCode::eLdsStore)
    memoryOperands.flags |= spv::MemoryAccessNonPrivatePointerMask;

  m_code.push_back(makeOpcodeToken(spv::OpStore, 3u + memoryOperands.computeDwordCount()));
  m_code.push_back(accessChainId);
  m_code.push_back(valueId);
  memoryOperands.pushTo(m_code);
}


void SpirvBuilder::emitCompositeOp(const ir::Op& op) {
  bool isInsert = op.getOpCode() == ir::OpCode::eCompositeInsert;

  const auto& addressOp = m_builder.getOpForOperand(op, 1u);
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

  emitDebugName(op.getDef(), spvId);
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

  emitDebugName(op.getDef(), spvId);
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
      case ir::OpCode::eSLe:          return spv::OpSLessThanEqual;
      case ir::OpCode::eSGt:          return spv::OpSGreaterThan;
      case ir::OpCode::eSGe:          return spv::OpSGreaterThanEqual;
      case ir::OpCode::eULt:          return spv::OpULessThan;
      case ir::OpCode::eULe:          return spv::OpULessThanEqual;
      case ir::OpCode::eUGt:          return spv::OpUGreaterThan;
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
      case ir::OpCode::eIBitCount:    return spv::OpBitCount;
      case ir::OpCode::eIBitReverse:  return spv::OpBitReverse;
      case ir::OpCode::eIShl:         return spv::OpShiftLeftLogical;
      case ir::OpCode::eSShr:         return spv::OpShiftRightArithmetic;
      case ir::OpCode::eUShr:         return spv::OpShiftRightLogical;
      case ir::OpCode::eIAdd:         return spv::OpIAdd;
      case ir::OpCode::eISub:         return spv::OpISub;
      case ir::OpCode::eINeg:         return spv::OpSNegate;
      case ir::OpCode::eIMul:         return spv::OpIMul;
      case ir::OpCode::eUDiv:         return spv::OpUDiv;
      case ir::OpCode::eUMod:         return spv::OpUMod;

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

  emitFpMode(op, id);
  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitExtendedGlslArithmetic(const ir::Op& op) {
  auto extOp = [&] {
    switch (op.getOpCode()) {
      case ir::OpCode::eIAbs: return GLSLstd450SAbs;
      case ir::OpCode::eFAbs: return GLSLstd450FAbs;
      case ir::OpCode::eFMad: return GLSLstd450Fma;
      case ir::OpCode::eFFract: return GLSLstd450Fract;
      case ir::OpCode::eFSin: return GLSLstd450Sin;
      case ir::OpCode::eFCos: return GLSLstd450Cos;
      case ir::OpCode::eFPow: return GLSLstd450Pow;
      case ir::OpCode::eFExp2: return GLSLstd450Exp2;
      case ir::OpCode::eFLog2: return GLSLstd450Log2;
      case ir::OpCode::eFSqrt: return GLSLstd450Sqrt;
      case ir::OpCode::eFRsq: return GLSLstd450InverseSqrt;
      case ir::OpCode::eFMin: return GLSLstd450NMin;
      case ir::OpCode::eSMin: return GLSLstd450SMin;
      case ir::OpCode::eUMin: return GLSLstd450UMin;
      case ir::OpCode::eFMax: return GLSLstd450NMax;
      case ir::OpCode::eSMax: return GLSLstd450SMax;
      case ir::OpCode::eUMax: return GLSLstd450UMax;
      case ir::OpCode::eFClamp: return GLSLstd450NClamp;
      case ir::OpCode::eSClamp: return GLSLstd450SClamp;
      case ir::OpCode::eUClamp: return GLSLstd450UClamp;
      case ir::OpCode::eIFindLsb: return GLSLstd450FindILsb;
      case ir::OpCode::eSFindMsb: return GLSLstd450FindSMsb;
      case ir::OpCode::eUFindMsb: return GLSLstd450FindUMsb;
      case ir::OpCode::eConvertF32toPackedF16: return GLSLstd450PackHalf2x16;
      case ir::OpCode::eConvertPackedF16toF32: return GLSLstd450UnpackHalf2x16;
      default: dxbc_spv_unreachable();
    }

    return GLSLstd450Bad;
  } ();

  /* Emit GLSL extended instruction */
  auto id = getIdForDef(op.getDef());

  m_code.push_back(makeOpcodeToken(spv::OpExtInst, 5u + op.getOperandCount()));
  m_code.push_back(getIdForType(op.getType()));
  m_code.push_back(id);
  m_code.push_back(importGlslExt());
  m_code.push_back(uint32_t(extOp));

  for (uint32_t i = 0u; i < op.getOperandCount(); i++)
    m_code.push_back(getIdForDef(ir::SsaDef(op.getOperand(i))));

  emitFpMode(op, id);
  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitExtendedIntArithmetic(const ir::Op& op) {
  dxbc_spv_assert(op.getType().isVectorType() && op.getType().getBaseType(0u).getVectorSize() == 2u);

  /* Figure out opcode */
  spv::Op opCode = [&] {
    switch (op.getOpCode()) {
      case ir::OpCode::eIAddCarry: return spv::OpIAddCarry;
      case ir::OpCode::eISubBorrow: return spv::OpISubBorrow;
      case ir::OpCode::eSMulExtended: return spv::OpSMulExtended;
      case ir::OpCode::eUMulExtended: return spv::OpUMulExtended;
      default: dxbc_spv_unreachable();
    }

    return spv::OpNop;
  } ();

  /* Emit actual operation that returns a struct */
  auto resultType = op.getType().getBaseType(0u);

  auto structTypeId = getIdForType(ir::Type()
    .addStructMember(resultType.getBaseType())
    .addStructMember(resultType.getBaseType()));

  auto opId = allocId();

  pushOp(m_code, opCode, structTypeId, opId,
    getIdForDef(ir::SsaDef(op.getOperand(0u))),
    getIdForDef(ir::SsaDef(op.getOperand(1u))));

  /* Repack result struct as a vector */
  auto scalarTypeId = getIdForType(resultType.getBaseType());

  auto loId = allocId();
  auto hiId = allocId();

  pushOp(m_code, spv::OpCompositeExtract, scalarTypeId, loId, opId, 0u);
  pushOp(m_code, spv::OpCompositeExtract, scalarTypeId, hiId, opId, 1u);

  auto resultTypeId = getIdForType(resultType);
  auto resultId = getIdForDef(op.getDef());

  pushOp(m_code, spv::OpCompositeConstruct, resultTypeId, resultId, loId, hiId);

  emitDebugName(op.getDef(), resultId);
}


void SpirvBuilder::emitFRcp(const ir::Op& op) {
  dxbc_spv_assert(op.getType().isBasicType());

  auto type = op.getType().getBaseType(0u);
  dxbc_spv_assert(type.isFloatType());

  /* Need to emit constant 1 based on the float type */
  ir::Op constantOp(ir::OpCode::eConstant, type);

  ir::Operand constantScalar = [&] {
    switch (type.getBaseType()) {
      case ir::ScalarType::eF16: return ir::Operand(util::float16_t(1.0f));
      case ir::ScalarType::eF32: return ir::Operand(1.0f);
      case ir::ScalarType::eF64: return ir::Operand(1.0);
      default: dxbc_spv_unreachable();
    }

    return ir::Operand();
  } ();

  for (uint32_t i = 0u; i < type.getVectorSize(); i++)
    constantOp.addOperand(constantScalar);

  uint32_t operandIndex = 0u;
  uint32_t constantOneId = makeBasicConst(type, constantOp, operandIndex);

  /* Emit FDiv instruction with the given constant */
  auto id = getIdForDef(op.getDef());

  pushOp(m_code, spv::OpFDiv, getIdForType(type), id, constantOneId,
    getIdForDef(ir::SsaDef(op.getOperand(0u))));

  emitFpMode(op, id, spv::FPFastMathModeAllowRecipMask);
  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitFRound(const ir::Op& op) {
  auto id = getIdForDef(op.getDef());

  auto valueDef = ir::SsaDef(op.getOperand(0u));
  auto roundingMode = ir::RoundMode(op.getOperand(1u));

  auto opCode = [&] {
    switch (roundingMode) {
      case ir::RoundMode::eZero: return GLSLstd450Trunc;
      case ir::RoundMode::eNearestEven: return GLSLstd450RoundEven;
      case ir::RoundMode::eNegativeInf: return GLSLstd450Floor;
      case ir::RoundMode::ePositiveInf: return GLSLstd450Ceil;
      case ir::RoundMode::eFlagEnum: break;
    }

    dxbc_spv_unreachable();
    return GLSLstd450Bad;
  } ();

  pushOp(m_code, spv::OpExtInst, getIdForType(op.getType()), id,
    importGlslExt(), opCode, getIdForDef(valueDef));

  emitFpMode(op, id);
  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitInterpolation(const ir::Op& op) {
  enableCapability(spv::CapabilityInterpolationFunction);

  dxbc_spv_assert(m_builder.getOpForOperand(op, 0u).getOpCode() == ir::OpCode::eDclInput ||
                  m_builder.getOpForOperand(op, 0u).getOpCode() == ir::OpCode::eDclInputBuiltIn);

  /* Determine opcode */
  auto extOp = [&] {
    switch (op.getOpCode()) {
      case ir::OpCode::eInterpolateAtCentroid: return GLSLstd450InterpolateAtCentroid;
      case ir::OpCode::eInterpolateAtSample: return GLSLstd450InterpolateAtSample;
      case ir::OpCode::eInterpolateAtOffset: return GLSLstd450InterpolateAtOffset;
      default: dxbc_spv_unreachable();
    }

    return GLSLstd450Bad;
  } ();

  /* If we're addressing a single vector or array component,
   * emit an access chain. The address must be scalar. */
  auto typeId = getIdForType(op.getType());

  auto ptrId = getIdForDef(ir::SsaDef(op.getOperand(0u)));
  auto addressId = getIdForDef(ir::SsaDef(op.getOperand(1u)));

  if (addressId) {
    auto accessChainId = allocId();

    m_code.push_back(makeOpcodeToken(spv::OpInBoundsAccessChain, 5u));
    m_code.push_back(getIdForPtrType(typeId, spv::StorageClassInput));
    m_code.push_back(accessChainId);
    m_code.push_back(ptrId);
    m_code.push_back(addressId);

    ptrId = accessChainId;
  }

  /* Emit GLSL extended instruction */
  auto id = getIdForDef(op.getDef());

  m_code.push_back(makeOpcodeToken(spv::OpExtInst, 4u + op.getOperandCount()));
  m_code.push_back(typeId);
  m_code.push_back(id);
  m_code.push_back(importGlslExt());
  m_code.push_back(uint32_t(extOp));
  m_code.push_back(ptrId);

  for (uint32_t i = 2u; i < op.getOperandCount(); i++)
    m_code.push_back(getIdForDef(ir::SsaDef(op.getOperand(i))));

  emitDebugName(op.getDef(), id);
}


void SpirvBuilder::emitSetFpMode(const ir::Op& op) {
  dxbc_spv_assert(op.getType().isScalarType() && op.getType().getBaseType(0u).isFloatType());

  /* Write back default op flags and get supported modes */
  auto scalarType = op.getType().getBaseType(0u).getBaseType();

  auto [supportedRoundModes, supportedDenormModes] = [&] {
    switch (scalarType) {
      case ir::ScalarType::eF16: {
        m_fpMode.f16 = op.getFlags();

        return std::make_pair(
          m_options.supportedRoundModesF16,
          m_options.supportedDenormModesF16);
      }

      case ir::ScalarType::eF32: {
        m_fpMode.f32 = op.getFlags();

        return std::make_pair(
          m_options.supportedRoundModesF32,
          m_options.supportedDenormModesF32);
      }

      case ir::ScalarType::eF64: {
        m_fpMode.f64 = op.getFlags();

        return std::make_pair(
          m_options.supportedRoundModesF64,
          m_options.supportedDenormModesF64);
      }

      default: break;
    }

    dxbc_spv_unreachable();
    return std::make_pair(ir::RoundModes(), ir::DenormModes());
  } ();

  /* Set up rounding mode */
  auto roundMode = ir::RoundMode(op.getOperand(1u));

  if (supportedRoundModes & roundMode) {
    auto mode = [&] {
      switch (roundMode) {
        case ir::RoundMode::eZero: {
          enableCapability(spv::CapabilityRoundingModeRTZ);
        } return spv::ExecutionModeRoundingModeRTZ;

        case ir::RoundMode::eNearestEven: {
          enableCapability(spv::CapabilityRoundingModeRTE);
        } return spv::ExecutionModeRoundingModeRTE;

        default:
          break;
      }

      dxbc_spv_unreachable();
      return spv::ExecutionMode();
    } ();

    pushOp(m_executionModes, spv::OpExecutionMode, m_entryPointId,
      mode, ir::bitWidth(scalarType));
  }

  /* Set up denorm mode */
  auto denormMode = ir::DenormMode(op.getOperand(2u));

  if (supportedDenormModes & denormMode) {
    auto mode = [&] {
      switch (denormMode) {
        case ir::DenormMode::eFlush: {
          enableCapability(spv::CapabilityDenormFlushToZero);
        } return spv::ExecutionModeDenormFlushToZero;

        case ir::DenormMode::ePreserve: {
          enableCapability(spv::CapabilityDenormPreserve);
        } return spv::ExecutionModeDenormPreserve;

        case ir::DenormMode::eFlagEnum:
          break;
      }

      dxbc_spv_unreachable();
      return spv::ExecutionMode();
    } ();

    pushOp(m_executionModes, spv::OpExecutionMode, m_entryPointId,
      mode, ir::bitWidth(scalarType));
  }

  if (m_options.floatControls2) {
    /* Set up default float control mode for the given type */
    enableCapability(spv::CapabilityFloatControls2);

    uint32_t flags = getFpModeFlags(op.getFlags());

    pushOp(m_executionModes, spv::OpExecutionModeId, m_entryPointId,
      spv::ExecutionModeFPFastMathDefault, getIdForType(scalarType), makeConstU32(flags));
  } else {
    /* If float controls 2 is not supported, check if we need to care about signed
     * zero, inf or nan values for the given type, and enable the mode if possible. */
    auto lenientFlags = ir::OpFlag::eNoNan | ir::OpFlag::eNoInf | ir::OpFlag::eNoSz;

    if ((op.getFlags() & lenientFlags) != lenientFlags) {
      bool supportsZeroInfNanPreserve =
        (scalarType == ir::ScalarType::eF16 && m_options.supportsZeroInfNanPreserveF16) ||
        (scalarType == ir::ScalarType::eF32 && m_options.supportsZeroInfNanPreserveF32) ||
        (scalarType == ir::ScalarType::eF64 && m_options.supportsZeroInfNanPreserveF64);

      if (supportsZeroInfNanPreserve)  {
        enableCapability(spv::CapabilitySignedZeroInfNanPreserve);

        pushOp(m_executionModes, spv::OpExecutionMode, m_entryPointId,
          spv::ExecutionModeSignedZeroInfNanPreserve, ir::bitWidth(scalarType));
      }
    }
  }
}


void SpirvBuilder::emitSetCsWorkgroupSize(const ir::Op& op) {
  auto x = uint32_t(op.getOperand(1u));
  auto y = uint32_t(op.getOperand(2u));
  auto z = uint32_t(op.getOperand(3u));

  pushOp(m_executionModes, spv::OpExecutionModeId, m_entryPointId,
    spv::ExecutionModeLocalSizeId,
    makeConstU32(x), makeConstU32(y), makeConstU32(z));
}


void SpirvBuilder::emitSetGsInstances(const ir::Op& op) {
  auto instanceCount = uint32_t(op.getOperand(1u));

  pushOp(m_executionModes, spv::OpExecutionMode, m_entryPointId,
    spv::ExecutionModeInvocations, instanceCount);
}


void SpirvBuilder::emitSetGsInputPrimitive(const ir::Op& op) {
  auto primitiveType = ir::PrimitiveType(op.getOperand(1u));
  m_geometry.inputVertices = ir::primitiveVertexCount(primitiveType);

  auto execMode = [&] {
    switch (primitiveType) {
      case ir::PrimitiveType::ePoints:        return spv::ExecutionModeInputPoints;
      case ir::PrimitiveType::eLines:         return spv::ExecutionModeInputLines;
      case ir::PrimitiveType::eLinesAdj:      return spv::ExecutionModeInputLinesAdjacency;
      case ir::PrimitiveType::eTriangles:     return spv::ExecutionModeTriangles;
      case ir::PrimitiveType::eTrianglesAdj:  return spv::ExecutionModeInputTrianglesAdjacency;
      /* Don't support patches here */
      default: dxbc_spv_unreachable();
    }

    return spv::ExecutionMode();
  } ();

  pushOp(m_executionModes, spv::OpExecutionMode, m_entryPointId, execMode);
}


void SpirvBuilder::emitSetGsOutputVertices(const ir::Op& op) {
  auto vertexCount = uint32_t(op.getOperand(1u));

  pushOp(m_executionModes, spv::OpExecutionMode, m_entryPointId,
    spv::ExecutionModeOutputVertices, vertexCount);
}


void SpirvBuilder::emitSetGsOutputPrimitive(const ir::Op& op) {
  auto primitiveType = ir::PrimitiveType(op.getOperand(1u));
  auto streamMask = uint32_t(ir::PrimitiveType(op.getOperand(2u)));

  auto execMode = [&] {
    switch (primitiveType) {
      case ir::PrimitiveType::ePoints: return spv::ExecutionModeOutputPoints;
      case ir::PrimitiveType::eLines: return spv::ExecutionModeOutputLineStrip;
      case ir::PrimitiveType::eTriangles: return spv::ExecutionModeOutputTriangleStrip;
      default: dxbc_spv_unreachable();
    }

    return spv::ExecutionMode();
  } ();

  pushOp(m_executionModes, spv::OpExecutionMode, m_entryPointId, execMode);

  if (streamMask != 0x1u)
    enableCapability(spv::CapabilityGeometryStreams);

  m_geometry.streamMask = streamMask;
}


void SpirvBuilder::emitSetPsDepthMode(const ir::Op& op) {
  dxbc_spv_assert(m_stage == ir::ShaderStage::ePixel);

  auto mode = op.getOpCode() == ir::OpCode::eSetPsDepthLessEqual
    ? spv::ExecutionModeDepthLess
    : spv::ExecutionModeDepthGreater;

  pushOp(m_executionModes, spv::OpExecutionMode, m_entryPointId, mode);
}


void SpirvBuilder::emitSetPsEarlyFragmentTest() {
  dxbc_spv_assert(m_stage == ir::ShaderStage::ePixel);

  pushOp(m_executionModes, spv::OpExecutionMode, m_entryPointId, spv::ExecutionModeEarlyFragmentTests);
}


void SpirvBuilder::emitSetTessPrimitive(const ir::Op& op) {
  dxbc_spv_assert(m_stage == ir::ShaderStage::eHull);

  /* Mapping to SPIR-V execution modes is non-trivial */
  auto primType = ir::PrimitiveType(op.getOperand(1u));
  auto winding = ir::TessWindingOrder(op.getOperand(2u));
  auto partitioning = ir::TessPartitioning(op.getOperand(3u));

  if (primType == ir::PrimitiveType::ePoints)
    pushOp(m_executionModes, spv::OpExecutionMode, m_entryPointId, spv::ExecutionModePointMode);

  if (primType == ir::PrimitiveType::eTriangles) {
    auto windingMode = winding == ir::TessWindingOrder::eCw
      ? spv::ExecutionModeVertexOrderCw
      : spv::ExecutionModeVertexOrderCcw;

    pushOp(m_executionModes, spv::OpExecutionMode, m_entryPointId, windingMode);
  }

  auto partitionMode = [&] {
    switch (partitioning) {
      case ir::TessPartitioning::eInteger:    return spv::ExecutionModeSpacingEqual;
      case ir::TessPartitioning::eFractOdd:   return spv::ExecutionModeSpacingFractionalOdd;
      case ir::TessPartitioning::eFractEven:  return spv::ExecutionModeSpacingFractionalEven;
    }

    dxbc_spv_unreachable();
    return spv::ExecutionMode();
  } ();

  pushOp(m_executionModes, spv::OpExecutionMode, m_entryPointId, partitionMode);
}


void SpirvBuilder::emitSetTessDomain(const ir::Op& op) {
  dxbc_spv_assert(m_stage == ir::ShaderStage::eHull ||
                  m_stage == ir::ShaderStage::eDomain);

  auto domain = ir::PrimitiveType(op.getOperand(1u));

  auto mode = [&] {
    switch (domain) {
      case ir::PrimitiveType::eLines:     return spv::ExecutionModeIsolines;
      case ir::PrimitiveType::eTriangles: return spv::ExecutionModeTriangles;
      case ir::PrimitiveType::eQuads:     return spv::ExecutionModeQuads;
      default: dxbc_spv_unreachable();
    }

    return spv::ExecutionMode();
  } ();

  pushOp(m_executionModes, spv::OpExecutionMode, m_entryPointId, mode);
}


void SpirvBuilder::emitSetTessControlPoints(const ir::Op& op) {
  dxbc_spv_assert(m_stage == ir::ShaderStage::eHull);

  auto vertexCount = uint32_t(op.getOperand(1u));

  pushOp(m_executionModes, spv::OpExecutionMode, m_entryPointId,
    spv::ExecutionModeOutputVertices, vertexCount);
}


uint32_t SpirvBuilder::emitExtractComponent(ir::SsaDef vectorDef, uint32_t index) {
  const auto& op = m_builder.getOp(vectorDef);
  dxbc_spv_assert(op.getType().isBasicType() && index < op.getType().getBaseType(0u).getVectorSize());

  if (op.getType().isScalarType())
    return getIdForDef(vectorDef);

  if (op.isConstant())
    return makeConstant(op.getType().getSubType(0u), op, index);

  if (op.getOpCode() == ir::OpCode::eCompositeConstruct)
    return getIdForDef(ir::SsaDef(op.getOperand(index)));

  auto id = allocId();

  pushOp(m_code, spv::OpCompositeExtract,
    getIdForType(op.getType().getBaseType(0u).getBaseType()), id,
    getIdForDef(vectorDef), index);

  return id;
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


bool SpirvBuilder::hasIdForDef(ir::SsaDef def) const {
  if (!def)
    return false;

  uint32_t defId = def.getId();
  return defId < m_ssaDefsToId.size() && m_ssaDefsToId[defId] != 0u;
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


uint32_t SpirvBuilder::defType(const ir::Type& type, bool explicitLayout, ir::SsaDef dclOp) {
  /* Don't re-declare non-aggregate types. We may end up here if a
   * resource declaration ends up using a plain vector or scalar. */
  if (type.isBasicType()) {
    auto e = m_types.find(type);

    if (e != m_types.end())
      return e->second;
  }

  auto id = allocId();

  if (type.isBasicType())
    m_types.insert({ type, id });

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

    if (m_options.includeDebugNames && dclOp)
      emitDebugMemberNames(dclOp, id);

    return id;
  }

  if (type.isArrayType()) {
    auto baseType = type.getSubType(0u);
    auto baseTypeId = explicitLayout && !baseType.isBasicType()
      ? defType(baseType, explicitLayout, dclOp)
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

    if (arraySize) {
      pushOp(m_declarations, spv::OpTypeArray, id, typeId, makeConstU32(arraySize));
    } else {
      enableCapability(spv::CapabilityRuntimeDescriptorArray);
      pushOp(m_declarations, spv::OpTypeRuntimeArray, id, typeId);
    }

    typeId = id;
  }

  auto ptrTypeId = getIdForPtrType(typeId, storageClass);
  auto varId = getIdForDef(op.getDef());

  pushOp(m_declarations, spv::OpVariable, ptrTypeId, varId, storageClass);

  /* Map binding and set */
  const auto& bindingOp = op.getOpCode() == ir::OpCode::eDclUavCounter
    ? m_builder.getOpForOperand(op, 1u)
    : op;

  auto resource = mapDescriptor(op, bindingOp);

  pushOp(m_decorations, spv::OpDecorate, varId, spv::DecorationDescriptorSet, resource.set);
  pushOp(m_decorations, spv::OpDecorate, varId, spv::DecorationBinding, resource.binding);

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

  if (op.getOpCode() == ir::OpCode::eDclCbv && cbvAsSsbo(op))
    pushOp(m_decorations, spv::OpDecorate, varId, spv::DecorationNonWritable);

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


uint32_t SpirvBuilder::getIdForSampledImageType(uint32_t imageTypeId) {
  auto entry = m_sampledImageTypeIds.find(imageTypeId);

  if (entry != m_sampledImageTypeIds.end())
    return entry->second;

  uint32_t id = allocId();

  pushOp(m_declarations, spv::OpTypeSampledImage, id, imageTypeId);
  m_sampledImageTypeIds.insert({ imageTypeId, id });
  return id;
}


uint32_t SpirvBuilder::getIdForBdaType(const ir::Type& type, ir::UavFlags flags) {
  SpirvBdaTypeKey key;
  key.type = type;
  key.flags = flags & (ir::UavFlag::eReadOnly | ir::UavFlag::eWriteOnly);

  auto entry = m_bdaTypeIds.find(key);

  if (entry != m_bdaTypeIds.end())
    return entry->second;

  /* Work out read-only or write-only state */
  spv::Decoration decoration = spv::Decoration();

  if (flags & ir::UavFlag::eReadOnly)
    decoration = spv::DecorationNonWritable;
  else if (flags & ir::UavFlag::eWriteOnly)
    decoration = spv::DecorationNonReadable;

  /* Declare type */
  uint32_t typeId = 0u;

  if (type.isBasicType())
    typeId = getIdForType(type);
  else
    typeId = defType(type, true);

  if (type.isStructType()) {
    if (key.flags) {
      for (uint32_t i = 0u; i < type.getStructMemberCount(); i++)
        pushOp(m_decorations, spv::OpMemberDecorate, typeId, i, decoration);
    }
  } else {
    typeId = defStructWrapper(typeId);

    if (key.flags)
      pushOp(m_decorations, spv::OpMemberDecorate, typeId, 0u, decoration);
  }

  pushOp(m_decorations, spv::OpDecorate, typeId, spv::DecorationBlock);

  /* Declare pointer type */
  uint32_t ptrTypeId = getIdForPtrType(typeId, spv::StorageClassPhysicalStorageBuffer);

  m_bdaTypeIds.insert({ key, ptrTypeId });
  return ptrTypeId;
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


uint32_t SpirvBuilder::getIdForConstantNull(const ir::Type& type) {
  SpirvConstant constant = { };
  constant.op = spv::OpConstantNull;
  constant.typeId = getIdForType(type);

  return getIdForConstant(constant, 0u);
}


spv::Scope SpirvBuilder::getUavCoherentScope(ir::UavFlags flags) {
  if (flags & (ir::UavFlag::eReadOnly | ir::UavFlag::eWriteOnly))
    return spv::ScopeInvocation;

  if (flags & ir::UavFlag::eCoherent)
    return spv::ScopeQueueFamily;

  if (m_stage == ir::ShaderStage::eCompute)
    return spv::ScopeWorkgroup;

  return spv::ScopeInvocation;
}


uint32_t SpirvBuilder::getIdForPushDataBlock() {
  if (!m_pushData.blockId) {
    /* Declare struct type with explicit layout */
    auto typeId = allocId();
    auto memberCount = uint32_t(m_pushData.members.size());

    /* Declare member types before messing around with the stream */
    util::small_vector<uint32_t, 64u> memberTypeIds;

    for (uint32_t i = 0u; i < memberCount; i++) {
      const auto& info = m_pushData.members[i];

      auto memberType = m_builder.getOp(info.def).getType().getBaseType(info.member);
      memberTypeIds.push_back(getIdForType(memberType));

      pushOp(m_decorations, spv::OpMemberDecorate, typeId, i, spv::DecorationOffset, info.offset);

      if (m_options.includeDebugNames)
        emitDebugPushDataName(info, typeId, i);
    }

    /* Insert actual struct type and decorations */
    m_declarations.push_back(makeOpcodeToken(spv::OpTypeStruct, 2u + memberCount));
    m_declarations.push_back(typeId);

    for (uint32_t i = 0u; i < memberCount; i++)
      m_declarations.push_back(memberTypeIds[i]);

    pushOp(m_decorations, spv::OpDecorate, typeId, spv::DecorationBlock);

    /* Declare push data variable */
    m_pushData.blockId = allocId();

    auto varTypeId = getIdForPtrType(typeId, spv::StorageClassPushConstant);
    pushOp(m_declarations, spv::OpVariable, varTypeId, m_pushData.blockId, spv::StorageClassPushConstant);

    addEntryPointId(m_pushData.blockId);

    /* Set debug name for the struct */
    if (m_options.includeDebugNames) {
      setDebugName(typeId, "push_data_t");
      setDebugName(m_pushData.blockId, "push_data");
    }
  }

  return m_pushData.blockId;
}


spv::Scope SpirvBuilder::translateScope(ir::Scope scope) {
  switch (scope) {
    case ir::Scope::eThread:
      return spv::ScopeInvocation;

    case ir::Scope::eQuad:
    case ir::Scope::eSubgroup:
      return spv::ScopeSubgroup;

    case ir::Scope::eWorkgroup:
      return spv::ScopeWorkgroup;

    case ir::Scope::eGlobal:
      return spv::ScopeQueueFamily;
  }

  dxbc_spv_unreachable();
  return spv::ScopeInvocation;
}


uint32_t SpirvBuilder::translateMemoryTypes(ir::MemoryTypeFlags memoryFlags, spv::MemorySemanticsMask base) {
  uint32_t result = 0u;

  if (memoryFlags & ir::MemoryType::eLds)
    result |= spv::MemorySemanticsWorkgroupMemoryMask;

  if (memoryFlags & ir::MemoryType::eUav) {
    result |= spv::MemorySemanticsUniformMemoryMask
           |  spv::MemorySemanticsImageMemoryMask;
  }

  if (memoryFlags) {
    result |= base;

    if (base != spv::MemorySemanticsReleaseMask)
      result |= spv::MemorySemanticsMakeVisibleMask;

    if (base != spv::MemorySemanticsAcquireMask)
      result |= spv::MemorySemanticsMakeAvailableMask;
  }

  return result;
}


uint32_t SpirvBuilder::makeScalarConst(ir::ScalarType type, const ir::Op& op, uint32_t& operandIndex) {
  switch (type) {
    case ir::ScalarType::eBool:
      return makeConstBool(bool(op.getOperand(operandIndex++)));

    case ir::ScalarType::eI8: {
      SpirvConstant constant = { };
      constant.op = spv::OpConstant;
      constant.typeId = getIdForType(type);
      constant.constituents[0u] = int8_t(op.getOperand(operandIndex++));
      return getIdForConstant(constant, 1u);
    }

    case ir::ScalarType::eU8: {
      SpirvConstant constant = { };
      constant.op = spv::OpConstant;
      constant.typeId = getIdForType(type);
      constant.constituents[0u] = uint8_t(op.getOperand(operandIndex++));
      return getIdForConstant(constant, 1u);
    }

    case ir::ScalarType::eI16: {
      SpirvConstant constant = { };
      constant.op = spv::OpConstant;
      constant.typeId = getIdForType(type);
      constant.constituents[0u] = int16_t(op.getOperand(operandIndex++));
      return getIdForConstant(constant, 1u);
    }

    case ir::ScalarType::eU16:
    case ir::ScalarType::eF16: {
      SpirvConstant constant = { };
      constant.op = spv::OpConstant;
      constant.typeId = getIdForType(type);
      constant.constituents[0u] = uint16_t(op.getOperand(operandIndex++));
      return getIdForConstant(constant, 1u);
    }

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


bool SpirvBuilder::enableCapability(spv::Capability cap) {
  if (m_enabledCaps.find(cap) != m_enabledCaps.end())
    return false;

  pushOp(m_capabilities, spv::OpCapability, cap);
  m_enabledCaps.insert(cap);

  switch (cap) {
    case spv::CapabilityFloatControls2:
      enableExtension("SPV_KHR_float_controls2");
      break;

    case spv::CapabilityFragmentShaderSampleInterlockEXT:
    case spv::CapabilityFragmentShaderPixelInterlockEXT:
    case spv::CapabilityFragmentShaderShadingRateInterlockEXT:
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

  return true;
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
    return getDescriptorArraySize(m_builder.getOpForOperand(op, 1u));

  dxbc_spv_assert(op.getOpCode() == ir::OpCode::eDclCbv ||
                  op.getOpCode() == ir::OpCode::eDclSrv ||
                  op.getOpCode() == ir::OpCode::eDclUav ||
                  op.getOpCode() == ir::OpCode::eDclSampler);

  return uint32_t(op.getOperand(3u));
}


void SpirvBuilder::setUavImageReadOperands(SpirvImageOperands& operands, const ir::Op& uavOp) {
  dxbc_spv_assert(uavOp.getOpCode() == ir::OpCode::eDclUav);

  auto uavFlags = getUavFlags(uavOp);

  if (getUavCoherentScope(uavFlags) != spv::ScopeInvocation)
    operands.flags |= spv::ImageOperandsNonPrivateTexelMask;
}


void SpirvBuilder::setUavImageWriteOperands(SpirvImageOperands& operands, const ir::Op& uavOp) {
  dxbc_spv_assert(uavOp.getOpCode() == ir::OpCode::eDclUav);

  auto uavFlags = getUavFlags(uavOp);

  if (getUavCoherentScope(uavFlags) != spv::ScopeInvocation)
    operands.flags |= spv::ImageOperandsNonPrivateTexelMask;
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
      const auto& indexOp = m_builder.getOpForOperand(addressOp, i);
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


bool SpirvBuilder::isMultiStreamGs() const {
  return m_geometry.streamMask > 1u;
}


bool SpirvBuilder::isPatchConstant(const ir::Op& op) const {
  bool consider = false;

  if (m_stage == ir::ShaderStage::eHull) {
    consider = op.getOpCode() == ir::OpCode::eDclOutput ||
               op.getOpCode() == ir::OpCode::eDclOutputBuiltIn;
  } else if (m_stage == ir::ShaderStage::eDomain) {
    consider = op.getOpCode() == ir::OpCode::eDclInput ||
               op.getOpCode() == ir::OpCode::eDclInputBuiltIn;
  }

  if (!consider)
    return false;

  bool isBuiltIn = op.getOpCode() == ir::OpCode::eDclInputBuiltIn ||
                   op.getOpCode() == ir::OpCode::eDclOutputBuiltIn;

  if (isBuiltIn) {
    auto builtIn = ir::BuiltIn(op.getOperand(1u));

    return builtIn == ir::BuiltIn::eTessFactorInner ||
           builtIn == ir::BuiltIn::eTessFactorOuter;
  } else {
    /* Control point I/O must use array types, patch constant I/O must not */
    return !op.getType().isArrayType();
  }
}


DescriptorBinding SpirvBuilder::mapDescriptor(const ir::Op& op, const ir::Op& bindingOp) const {
  /* Get descriptor type from actual op */
  auto type = [&] {
    switch (op.getOpCode()) {
      case ir::OpCode::eDclSampler:     return ir::ScalarType::eSampler;
      case ir::OpCode::eDclCbv:         return ir::ScalarType::eCbv;
      case ir::OpCode::eDclSrv:         return ir::ScalarType::eSrv;
      case ir::OpCode::eDclUav:         return ir::ScalarType::eUav;
      case ir::OpCode::eDclUavCounter:  return ir::ScalarType::eUavCounter;
      default: break;
    }

    dxbc_spv_unreachable();
    return ir::ScalarType::eUnknown;
  } ();

  /* Get register info from the parent op, if any */
  auto regSpace = uint32_t(bindingOp.getOperand(1u));
  auto regIndex = uint32_t(bindingOp.getOperand(2u));

  return m_mapping.mapDescriptor(type, regSpace, regIndex);
}


spv::Op SpirvBuilder::getAccessChainOp(const ir::Op& op) const {
  if (op.getFlags() & ir::OpFlag::eInBounds)
    return spv::OpInBoundsAccessChain;

  return spv::OpAccessChain;
}


uint32_t SpirvBuilder::getFpModeFlags(ir::OpFlags flags) {
  uint32_t result = 0u;

  if (!(flags & ir::OpFlag::ePrecise)) {
    result |= spv::FPFastMathModeAllowRecipMask
           |  spv::FPFastMathModeAllowContractMask
           |  spv::FPFastMathModeAllowReassocMask
           |  spv::FPFastMathModeAllowTransformMask;
  }

  if (flags & ir::OpFlag::eNoNan)
    result |= spv::FPFastMathModeNotNaNMask;

  if (flags & ir::OpFlag::eNoInf)
    result |= spv::FPFastMathModeNotInfMask;

  if (flags & ir::OpFlag::eNoSz)
    result |= spv::FPFastMathModeNSZMask;

  return result;
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
