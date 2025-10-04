#include "dxbc_converter.h"
#include "dxbc_disasm.h"

namespace dxbc_spv::dxbc {

Converter::Converter(Container container, const Options& options)
: m_dxbc      (std::move(container))
, m_options   (options)
, m_regFile   (*this)
, m_ioMap     (*this)
, m_resources (*this) {
  if (options.maxTessFactor != 0.0f) {
    if (isValidTessFactor(options.maxTessFactor))
      m_hs.maxTessFactor = options.maxTessFactor;
    else
      Logger::warn("Invalid tess factor ", options.maxTessFactor, ", ignoring option.");
  }
}


Converter::~Converter() {

}


bool Converter::convertShader(ir::Builder& builder) {
  if (!initParser(m_parser, m_dxbc.getCodeChunk()))
    return false;

  auto shaderType = m_parser.getShaderInfo().getType();

  if (!m_ioMap.init(m_dxbc, shaderType))
    return false;

  initialize(builder, shaderType);

  while (m_parser) {
    Instruction op = m_parser.parseInstruction();

    if (!op || !convertInstruction(builder, op))
      return false;
  }

  return finalize(builder, shaderType);
}


bool Converter::createPassthroughGs(ir::Builder& builder) {
  if (!initialize(builder, ShaderType::eGeometry))
    return false;

  if (!m_ioMap.init(m_dxbc, ShaderType::eGeometry))
    return false;

  /* Set up GS state, I/O map code is going to read this */
  m_gs.inputPrimitive = PrimitiveType::ePoint;
  m_gs.outputTopology = PrimitiveTopology::ePointList;
  m_gs.outputVertices = 1u;
  m_gs.streamMask = 0x1u;

  if (!m_ioMap.emitGsPassthrough(builder))
    return false;

  return emitGsStateSetup(builder);
}


bool Converter::convertInstruction(ir::Builder& builder, const Instruction& op) {
  auto opCode = op.getOpToken().getOpCode();

  /* Increment instruction counter for debug purposes */
  m_instructionCount += 1u;

  switch (opCode) {
    case OpCode::eNop:
      return true;

    case OpCode::eCustomData:
      return handleCustomData(builder, op);

    case OpCode::eDclTemps:
      /* Some applications with custom DXBC code do not honor the declared
       * temp limit, so ignore it and declare them on the fly. */
      return true;

    case OpCode::eDclIndexableTemp:
      return m_regFile.handleDclIndexableTemp(builder, op);

    case OpCode::eDclGlobalFlags:
      return handleDclGlobalFlags(builder, op);

    case OpCode::eDclInput:
    case OpCode::eDclInputSgv:
    case OpCode::eDclInputSiv:
    case OpCode::eDclInputPs:
    case OpCode::eDclInputPsSgv:
    case OpCode::eDclInputPsSiv:
    case OpCode::eDclOutput:
    case OpCode::eDclOutputSgv:
    case OpCode::eDclOutputSiv:
      return m_ioMap.handleDclIoVar(builder, op);

    case OpCode::eDclIndexRange:
      return m_ioMap.handleDclIndexRange(builder, op);

    case OpCode::eDclThreadGroupSharedMemoryRaw:
      return m_regFile.handleDclTgsmRaw(builder, op);

    case OpCode::eDclThreadGroupSharedMemoryStructured:
      return m_regFile.handleDclTgsmStructured(builder, op);

    case OpCode::eDclConstantBuffer:
      return m_resources.handleDclConstantBuffer(builder, op);

    case OpCode::eDclUavRaw:
    case OpCode::eDclResourceRaw:
      return m_resources.handleDclResourceRaw(builder, op);

    case OpCode::eDclResourceStructured:
    case OpCode::eDclUavStructured:
      return m_resources.handleDclResourceStructured(builder, op);

    case OpCode::eDclResource:
    case OpCode::eDclUavTyped:
      return m_resources.handleDclResourceTyped(builder, op);

    case OpCode::eDclSampler:
      return m_resources.handleDclSampler(builder, op);

    case OpCode::eHsDecls:
    case OpCode::eHsControlPointPhase:
    case OpCode::eHsForkPhase:
    case OpCode::eHsJoinPhase:
      return handleHsPhase(builder, op);

    case OpCode::eDclHsForkPhaseInstanceCount:
    case OpCode::eDclHsJoinPhaseInstanceCount:
      return handleHsPhaseInstanceCount(op);

    case OpCode::eDclInputControlPointCount:
    case OpCode::eDclOutputControlPointCount:
      return handleHsControlPointCount(op);

    case OpCode::eDclHsMaxTessFactor:
      return handleHsMaxTessFactor(op);

    case OpCode::eDclTessDomain:
      return handleTessDomain(op);

    case OpCode::eDclTessPartitioning:
      return handleTessPartitioning(op);

    case OpCode::eDclTessOutputPrimitive:
      return handleTessOutput(op);

    case OpCode::eDclStream:
      return handleStream(op);

    case OpCode::eDclGsInstanceCount:
      return handleGsInstanceCount(op);

    case OpCode::eDclGsInputPrimitive:
      return handleGsInputPrimitive(op);

    case OpCode::eDclGsOutputPrimitiveTopology:
      return handleGsOutputPrimitive(op);

    case OpCode::eDclMaxOutputVertexCount:
      return handleGsOutputVertexCount(op);

    case OpCode::eDclThreadGroup:
      return handleCsWorkgroupSize(builder, op);

    case OpCode::eMov:
    case OpCode::eDMov:
      return handleMov(builder, op);

    case OpCode::eMovc:
    case OpCode::eDMovc:
      return handleMovc(builder, op);

    case OpCode::eSwapc:
      return handleSwapc(builder, op);

    case OpCode::eAdd:
    case OpCode::eDiv:
    case OpCode::eExp:
    case OpCode::eFrc:
    case OpCode::eLog:
    case OpCode::eMax:
    case OpCode::eMin:
    case OpCode::eMul:
    case OpCode::eRcp:
    case OpCode::eRoundNe:
    case OpCode::eRoundNi:
    case OpCode::eRoundPi:
    case OpCode::eRoundZ:
    case OpCode::eRsq:
    case OpCode::eSqrt:
    case OpCode::eDAdd:
    case OpCode::eDMax:
    case OpCode::eDMin:
    case OpCode::eDMul:
    case OpCode::eDDiv:
    case OpCode::eDRcp:
      return handleFloatArithmetic(builder, op);

    case OpCode::eMad:
    case OpCode::eDFma:
      return handleFloatMad(builder, op);

    case OpCode::eDp2:
    case OpCode::eDp3:
    case OpCode::eDp4:
      return handleFloatDot(builder, op);

    case OpCode::eEq:
    case OpCode::eGe:
    case OpCode::eLt:
    case OpCode::eNe:
    case OpCode::eDEq:
    case OpCode::eDGe:
    case OpCode::eDLt:
    case OpCode::eDNe:
      return handleFloatCompare(builder, op);

    case OpCode::eFtoI:
    case OpCode::eFtoU:
    case OpCode::eItoF:
    case OpCode::eUtoF:
    case OpCode::eDtoI:
    case OpCode::eDtoU:
    case OpCode::eItoD:
    case OpCode::eUtoD:
    case OpCode::eDtoF:
    case OpCode::eFtoD:
      return handleFloatConvert(builder, op);

    case OpCode::eSinCos:
      return handleFloatSinCos(builder, op);

    case OpCode::eAnd:
    case OpCode::eIAdd:
    case OpCode::eIMad:
    case OpCode::eIMax:
    case OpCode::eIMin:
    case OpCode::eINeg:
    case OpCode::eNot:
    case OpCode::eOr:
    case OpCode::eUMad:
    case OpCode::eUMax:
    case OpCode::eUMin:
    case OpCode::eXor:
      return handleIntArithmetic(builder, op);

    case OpCode::eIMul:
    case OpCode::eUMul:
      return handleIntMultiply(builder, op);

    case OpCode::eUDiv:
      return handleIntDivide(builder, op);

    case OpCode::eUAddc:
    case OpCode::eUSubb:
      return handleIntExtended(builder, op);

    case OpCode::eIShl:
    case OpCode::eIShr:
    case OpCode::eUShr:
      return handleIntShift(builder, op);

    case OpCode::eUBfe:
    case OpCode::eIBfe:
      return handleBitExtract(builder, op);

    case OpCode::eBfi:
      return handleBitInsert(builder, op);

    case OpCode::eCountBits:
    case OpCode::eFirstBitHi:
    case OpCode::eFirstBitLo:
    case OpCode::eFirstBitShi:
    case OpCode::eBfRev:
      return handleBitOp(builder, op);

    case OpCode::eMsad:
      return handleMsad(builder, op);

    case OpCode::eF32toF16:
      return handleF32toF16(builder, op);

    case OpCode::eF16toF32:
      return handleF16toF32(builder, op);

    case OpCode::eIEq:
    case OpCode::eIGe:
    case OpCode::eILt:
    case OpCode::eINe:
    case OpCode::eULt:
    case OpCode::eUGe:
      return handleIntCompare(builder, op);

    case OpCode::eEvalSnapped:
    case OpCode::eEvalSampleIndex:
    case OpCode::eEvalCentroid:
      return m_ioMap.handleEval(builder, op);

    case OpCode::eDerivRtx:
    case OpCode::eDerivRty:
    case OpCode::eDerivRtxCoarse:
    case OpCode::eDerivRtyCoarse:
    case OpCode::eDerivRtxFine:
    case OpCode::eDerivRtyFine:
      return handleDerivatives(builder, op);

    case OpCode::eLdRaw:
    case OpCode::eLdRawS:
      return handleLdRaw(builder, op);

    case OpCode::eLdStructured:
    case OpCode::eLdStructuredS:
      return handleLdStructured(builder, op);

    case OpCode::eLd:
    case OpCode::eLdS:
    case OpCode::eLdMs:
    case OpCode::eLdMsS:
    case OpCode::eLdUavTyped:
    case OpCode::eLdUavTypedS:
      return handleLdTyped(builder, op);

    case OpCode::eStoreRaw:
      return handleStoreRaw(builder, op);

    case OpCode::eStoreStructured:
      return handleStoreStructured(builder, op);

    case OpCode::eStoreUavTyped:
      return handleStoreTyped(builder, op);

    case OpCode::eAtomicAnd:
    case OpCode::eAtomicOr:
    case OpCode::eAtomicXor:
    case OpCode::eAtomicCmpStore:
    case OpCode::eAtomicIAdd:
    case OpCode::eAtomicIMax:
    case OpCode::eAtomicIMin:
    case OpCode::eAtomicUMax:
    case OpCode::eAtomicUMin:
    case OpCode::eImmAtomicIAdd:
    case OpCode::eImmAtomicAnd:
    case OpCode::eImmAtomicOr:
    case OpCode::eImmAtomicXor:
    case OpCode::eImmAtomicExch:
    case OpCode::eImmAtomicCmpExch:
    case OpCode::eImmAtomicIMax:
    case OpCode::eImmAtomicIMin:
    case OpCode::eImmAtomicUMax:
    case OpCode::eImmAtomicUMin:
      return handleAtomic(builder, op);

    case OpCode::eImmAtomicAlloc:
    case OpCode::eImmAtomicConsume:
      return handleAtomicCounter(builder, op);

    case OpCode::eSample:
    case OpCode::eSampleClampS:
    case OpCode::eSampleC:
    case OpCode::eSampleCClampS:
    case OpCode::eSampleClz:
    case OpCode::eSampleClzS:
    case OpCode::eSampleL:
    case OpCode::eSampleLS:
    case OpCode::eSampleD:
    case OpCode::eSampleDClampS:
    case OpCode::eSampleB:
    case OpCode::eSampleBClampS:
      return handleSample(builder, op);

    case OpCode::eGather4:
    case OpCode::eGather4S:
    case OpCode::eGather4C:
    case OpCode::eGather4CS:
    case OpCode::eGather4Po:
    case OpCode::eGather4PoS:
    case OpCode::eGather4PoC:
    case OpCode::eGather4PoCS:
      return handleGather(builder, op);

    case OpCode::eLod:
      return handleQueryLod(builder, op);

    case OpCode::eCheckAccessFullyMapped:
      return handleCheckSparseAccess(builder, op);

    case OpCode::eBufInfo:
      return handleBufInfo(builder, op);

    case OpCode::eResInfo:
      return handleResInfo(builder, op);

    case OpCode::eSampleInfo:
      return handleSampleInfo(builder, op);

    case OpCode::eSamplePos:
      return handleSamplePos(builder, op);

    case OpCode::eBreak:
    case OpCode::eBreakc:
      return handleBreak(builder, op);

    case OpCode::eContinue:
    case OpCode::eContinuec:
      return handleContinue(builder, op);

    case OpCode::eIf:
      return handleIf(builder, op);

    case OpCode::eElse:
      return handleElse(builder, op);

    case OpCode::eEndIf:
      return handleEndIf(builder, op);

    case OpCode::eSwitch:
      return handleSwitch(builder, op);

    case OpCode::eCase:
      return handleCase(builder, op);

    case OpCode::eDefault:
      return handleDefault(builder, op);

    case OpCode::eEndSwitch:
      return handleEndSwitch(builder, op);

    case OpCode::eLoop:
      return handleLoop(builder);

    case OpCode::eEndLoop:
      return handleEndLoop(builder, op);

    case OpCode::eRet:
    case OpCode::eRetc:
      return handleRet(builder, op);

    case OpCode::eCall:
    case OpCode::eCallc:
      return handleCall(builder, op);

    case OpCode::eDiscard:
      return handleDiscard(builder, op);

    case OpCode::eCut:
    case OpCode::eCutStream:
    case OpCode::eEmit:
    case OpCode::eEmitStream:
    case OpCode::eEmitThenCut:
    case OpCode::eEmitThenCutStream:
      return handleGsEmitCut(builder, op);

    case OpCode::eSync:
      return handleSync(builder, op);

    case OpCode::eLabel:
      return handleLabel(builder, op);

    case OpCode::eDclFunctionBody:
      return m_regFile.handleDclFunctionBody(builder, op);

    case OpCode::eDclFunctionTable:
      return m_regFile.handleDclFunctionTable(builder, op);

    case OpCode::eDclInterface:
      return m_regFile.handleDclInterface(builder, op);

    case OpCode::eInterfaceCall:
      return m_regFile.emitFcall(builder, op);

    case OpCode::eAbort:
    case OpCode::eDebugBreak:
      break;
  }

  return logOpError(op, "Unhandled opcode.");
}


bool Converter::initialize(ir::Builder& builder, ShaderType shaderType) {
  /* A valid debug namee is required for the main function */
  m_entryPoint.mainFunc = builder.add(ir::Op::Function(ir::ScalarType::eVoid));
  builder.add(ir::Op::FunctionEnd());

  if (shaderType == ShaderType::eHull) {
    builder.add(ir::Op::DebugName(m_entryPoint.mainFunc, "control_point"));

    m_entryPoint.patchConstantFunc = builder.add(ir::Op::Function(ir::ScalarType::eVoid));
    builder.add(ir::Op::FunctionEnd());

    if (m_options.includeDebugNames)
      builder.add(ir::Op::DebugName(m_entryPoint.patchConstantFunc, "patch_const"));
  } else {
    builder.add(ir::Op::DebugName(m_entryPoint.mainFunc, "main"));
  }

  /* Emit entry point instruction as the first instruction of the
   * shader. This is technically not needed, but makes things more
   * readable. */
  auto stage = resolveShaderStage(shaderType);

  auto entryPointOp = (shaderType == ShaderType::eHull)
    ? ir::Op::EntryPoint(m_entryPoint.mainFunc, m_entryPoint.patchConstantFunc, stage)
    : ir::Op::EntryPoint(m_entryPoint.mainFunc, stage);

  m_entryPoint.def = builder.addAfter(ir::SsaDef(), std::move(entryPointOp));

  /* Need to emit the shader name regardless of debug names as well */
  if (m_options.name)
    builder.add(ir::Op::DebugName(m_entryPoint.def, m_options.name));

  /* Set cursor to main function so that instructions will be emitted
   * in the correct location */
  builder.setCursor(m_entryPoint.mainFunc);
  return true;
}


bool Converter::finalize(ir::Builder& builder, ShaderType shaderType) {
  m_resources.normalizeUavFlags(builder);

  emitFloatModes(builder);

  if (shaderType == ShaderType::eHull) {
    emitHsPatchConstantFunction(builder);

    if (!emitHsStateSetup(builder))
      return false;

    if (!m_hs.hasControlPointPhase && m_hs.controlPointsOut) {
      builder.setCursor(m_entryPoint.mainFunc);

      if (!m_ioMap.emitHsControlPointPhasePassthrough(builder))
        return false;
    }
  }

  if (shaderType == ShaderType::eDomain) {
    if (!emitDsStateSetup(builder))
      return false;
  }

  if (shaderType == ShaderType::eGeometry) {
    if (!emitGsStateSetup(builder))
      return false;
  }

  return true;
}


void Converter::emitFloatModes(ir::Builder& builder) {
  builder.add(ir::Op::SetFpMode(getEntryPoint(), ir::ScalarType::eF32,
    m_fpMode.defaultFlags, ir::RoundMode::eNearestEven, ir::DenormMode::eFlush));

  if (m_fpMode.hasFp16) {
    builder.add(ir::Op::SetFpMode(getEntryPoint(), ir::ScalarType::eF16,
      m_fpMode.defaultFlags, ir::RoundMode::eNearestEven, ir::DenormMode::ePreserve));
  }

  if (m_fpMode.hasFp64) {
    builder.add(ir::Op::SetFpMode(getEntryPoint(), ir::ScalarType::eF64,
      m_fpMode.defaultFlags, ir::RoundMode::eNearestEven, ir::DenormMode::ePreserve));
  }
}


bool Converter::emitHsStateSetup(ir::Builder& builder) {
  auto domain = resolveTessDomain(m_hs.domain);

  if (!domain) {
    Logger::err("Tessellator domain ", m_hs.domain, " not valid.");
    return false;
  }

  auto primitiveType = resolveTessOutput(m_hs.primitiveType);

  if (!domain) {
    Logger::err("Tessellator output primitive ", m_hs.primitiveType, " not valid.");
    return false;
  }

  auto partitioning = resolveTessPartitioning(m_hs.partitioning);

  if (!partitioning) {
    Logger::err("Tessellator partitioning ", m_hs.partitioning, " not valid.");
    return false;
  }

  builder.add(ir::Op::SetTessPrimitive(getEntryPoint(),
    primitiveType->first, primitiveType->second, *partitioning));

  builder.add(ir::Op::SetTessDomain(getEntryPoint(), *domain));

  builder.add(ir::Op::SetTessControlPoints(getEntryPoint(),
    m_hs.controlPointsIn, m_hs.controlPointsOut));
  return true;
}


void Converter::emitHsPatchConstantFunction(ir::Builder& builder) {
  builder.setCursor(m_entryPoint.patchConstantFunc);

  for (const auto& e : m_hs.phaseInstanceCounts) {
    for (uint32_t i = 0u; i < e.second; i++)
      builder.add(ir::Op::FunctionCall(ir::Type(), e.first).addParam(builder.makeConstant(i)));
  }

  m_ioMap.applyMaxTessFactor(builder);
}


bool Converter::emitDsStateSetup(ir::Builder& builder) {
  auto domain = resolveTessDomain(m_hs.domain);

  if (!domain) {
    Logger::err("Tessellator domain ", m_hs.domain, " not valid.");
    return false;
  }

  builder.add(ir::Op::SetTessDomain(getEntryPoint(), *domain));
  return true;
}


bool Converter::emitGsStateSetup(ir::Builder& builder) {
  /* Output-less geometry shader, weird but technically legal */
  if (m_gs.outputTopology == PrimitiveTopology::eUndefined && !m_gs.streamMask) {
    m_gs.outputTopology = PrimitiveTopology::ePointList;
    m_gs.outputVertices = 1u;
    m_gs.streamMask = 0x1u;
  }

  auto inputPrimitive = resolvePrimitiveType(m_gs.inputPrimitive);
  auto outputTopology = resolvePrimitiveTopology(m_gs.outputTopology);

  if (!inputPrimitive) {
    Logger::err("GS input primitive type ", m_gs.inputPrimitive, " not valid.");
    return false;
  }

  if (!outputTopology) {
    Logger::err("GS output primitive topology ", m_gs.outputTopology, " not valid.");
    return false;
  }

  builder.add(ir::Op::SetGsInstances(getEntryPoint(), m_gs.instanceCount));
  builder.add(ir::Op::SetGsInputPrimitive(getEntryPoint(), *inputPrimitive));
  builder.add(ir::Op::SetGsOutputPrimitive(getEntryPoint(), *outputTopology, m_gs.streamMask));
  builder.add(ir::Op::SetGsOutputVertices(getEntryPoint(), m_gs.outputVertices));
  return true;
}


bool Converter::handleCustomData(ir::Builder& builder, const Instruction& op) {
  if (op.getOpToken().getCustomDataType() != CustomDataType::eDclIcb) {
    logOpMessage(LogLevel::eDebug, op, "Skipping custom data block of type ", uint32_t(op.getOpToken().getCustomDataType()));
    return true;
  }

  /* We can only have one icb per shader module */
  if (m_icb)
    return logOpError(op, "Immediate constant buffer already declared.");

  /* ICB is always declared as a vec4 array, we can get rid of
   * unused vector components later. */
  auto [data, size] = op.getCustomData();
  uint32_t arraySize = size / 4u;

  auto type = ir::Type(ir::ScalarType::eUnknown, 4u).addArrayDimension(arraySize);

  if (m_options.lowerIcb) {
    m_icb = builder.add(ir::Op::DclCbv(type, m_entryPoint.def,
      m_options.icbRegisterSpace, m_options.icbRegisterIndex, 1u));
  } else {
    ir::Op constant(ir::OpCode::eConstant, type);

    for (size_t i = 0u; i < size; i++)
      constant.addOperand(data[i]);

    m_icb = builder.add(std::move(constant));
  }

  if (m_options.includeDebugNames)
    builder.add(ir::Op::DebugName(m_icb, "icb"));

  return true;
}


bool Converter::handleDclGlobalFlags(ir::Builder& builder, const Instruction& op) {
  auto flags = op.getOpToken().getGlobalFlags();

  if (flags & GlobalFlag::eRefactoringAllowed)
    m_fpMode.defaultFlags -= ir::OpFlag::ePrecise;

  if (flags & (GlobalFlag::eEnableFp64 | GlobalFlag::eEnableExtFp64))
    m_fpMode.hasFp64 = true;

  if (flags & GlobalFlag::eEnableMinPrecision)
    m_fpMode.hasFp16 = true;

  if (flags & GlobalFlag::eEarlyZ) {
    auto info = m_parser.getShaderInfo();

    if (info.getType() != ShaderType::ePixel)
      return logOpError(op, "Global flag '", GlobalFlag::eEarlyZ, "' only valid in pixel shaders.");

    builder.add(ir::Op::SetPsEarlyFragmentTest(m_entryPoint.def));
  }

  return true;
}


bool Converter::handleHsPhase(ir::Builder& builder, const Instruction& op) {
  auto opCode = op.getOpToken().getOpCode();

  auto phase = [opCode] {
    switch (opCode) {
      case OpCode::eHsDecls:              return HullShaderPhase::eDcl;
      case OpCode::eHsControlPointPhase:  return HullShaderPhase::eControlPoint;
      case OpCode::eHsForkPhase:          return HullShaderPhase::eFork;
      case OpCode::eHsJoinPhase:          return HullShaderPhase::eJoin;
      default: break;
    }

    dxbc_spv_unreachable();
    return HullShaderPhase::eNone;
  } ();

  if (phase == HullShaderPhase::eControlPoint) {
    if (m_hs.hasControlPointPhase)
      return logOpError(op, "Multiple control point phases in hull shader.");

    m_hs.hasControlPointPhase = true;
    m_hs.phaseFunction = m_entryPoint.mainFunc;

    builder.setCursor(m_hs.phaseFunction);
  }

  if (phase == HullShaderPhase::eFork || phase == HullShaderPhase::eJoin) {
    /* Declare function parameter for the fork/join function */
    m_hs.phaseInstanceId = builder.add(ir::Op::DclParam(ir::ScalarType::eU32));

    if (m_options.includeDebugNames) {
      auto name = makeRegisterDebugName(phase == HullShaderPhase::eFork
        ? RegisterType::eForkInstanceId
        : RegisterType::eJoinInstanceId, 0u, WriteMask());
      builder.add(ir::Op::DebugName(m_hs.phaseInstanceId, name.c_str()));
    }

    /* Declare fork/join phase function */
    m_hs.phaseIndex = (m_hs.phase == phase) ? m_hs.phaseIndex + 1u : 0u;
    m_hs.phaseFunction = builder.addBefore(m_entryPoint.patchConstantFunc,
      ir::Op::Function(ir::Type()).addParam(m_hs.phaseInstanceId));
    builder.addAfter(m_hs.phaseFunction, ir::Op::FunctionEnd());
    builder.setCursor(m_hs.phaseFunction);

    if (m_options.includeDebugNames) {
      auto name = phase == HullShaderPhase::eFork
        ? std::string("fork_") + std::to_string(m_hs.phaseIndex)
        : std::string("join_") + std::to_string(m_hs.phaseIndex);
      builder.add(ir::Op::DebugName(m_hs.phaseFunction, name.c_str()));
    }

    /* Assume a single instance until we find a declaration */
    m_hs.phaseInstanceCounts.push_back({ m_hs.phaseFunction, 1u });
  }

  m_hs.phase = phase;

  /* Re-program register files as necessary */
  m_regFile.handleHsPhase();
  m_ioMap.handleHsPhase();

  return true;
}


bool Converter::handleHsPhaseInstanceCount(const Instruction& op) {
  if (m_hs.phase != HullShaderPhase::eFork && m_hs.phase != HullShaderPhase::eJoin)
    return logOpError(op, "Instruction must occur inside a fork or join phase.");

  dxbc_spv_assert(op.getImmCount());
  auto instanceCount = op.getImm(0u).getImmediate<uint32_t>(0u);

  dxbc_spv_assert(!m_hs.phaseInstanceCounts.empty());
  m_hs.phaseInstanceCounts.back().second = instanceCount;
  return true;
}


bool Converter::handleHsControlPointCount(const Instruction& op) {
  auto opCode = op.getOpToken().getOpCode();
  auto controlPointCount = op.getOpToken().getControlPointCount();

  if (!isValidControlPointCount(controlPointCount))
    return logOpError(op, "Invalid control point count ", controlPointCount);

  auto& dst = (opCode == OpCode::eDclOutputControlPointCount
    ? m_hs.controlPointsOut
    : m_hs.controlPointsIn);

  dst = controlPointCount;
  return true;
}


bool Converter::handleHsMaxTessFactor(const Instruction& op) {
  dxbc_spv_assert(op.getImmCount());

  auto maxTessFactor = op.getImm(0u).getImmediate<float>(0.0f);

  if (!isValidTessFactor(maxTessFactor)) {
    logOpMessage(LogLevel::eWarn, op, "Invalid tess factor ", maxTessFactor, ", ignoring instruction.");
    return true;
  }

  m_hs.maxTessFactor = maxTessFactor;
  return true;
}


bool Converter::handleTessDomain(const Instruction& op) {
  m_hs.domain = op.getOpToken().getTessellatorDomain();
  return true;
}


bool Converter::handleTessPartitioning(const Instruction& op) {
  m_hs.partitioning = op.getOpToken().getTessellatorPartitioning();
  return true;
}


bool Converter::handleTessOutput(const Instruction& op) {
  m_hs.primitiveType = op.getOpToken().getTessellatorOutput();
  return true;
}


bool Converter::handleStream(const Instruction& op) {
  const auto& mreg = op.getDst(0u);

  if (mreg.getRegisterType() != RegisterType::eStream) {
    logOpError(op, "Invalid stream operand.");
    return false;
  }

  m_gs.streamIndex = mreg.getIndex(0u);
  m_gs.streamMask |= 1u << m_gs.streamIndex;
  return true;
}


bool Converter::handleGsInstanceCount(const Instruction& op) {
  m_gs.instanceCount = op.getImm(0u).getImmediate<uint32_t>(0u);
  return true;
}


bool Converter::handleGsInputPrimitive(const Instruction& op) {
  m_gs.inputPrimitive = op.getOpToken().getPrimitiveType();
  return true;
}


bool Converter::handleGsOutputPrimitive(const Instruction& op) {
  m_gs.outputTopology = op.getOpToken().getPrimitiveTopology();
  return true;
}


bool Converter::handleGsOutputVertexCount(const Instruction& op) {
  m_gs.outputVertices = op.getImm(0u).getImmediate<uint32_t>(0u);
  return true;
}


bool Converter::handleCsWorkgroupSize(ir::Builder& builder, const Instruction& op) {
  m_cs.workgroupSizeX = op.getImm(0u).getImmediate<uint32_t>(0u);
  m_cs.workgroupSizeY = op.getImm(1u).getImmediate<uint32_t>(0u);
  m_cs.workgroupSizeZ = op.getImm(2u).getImmediate<uint32_t>(0u);

  builder.add(ir::Op::SetCsWorkgroupSize(getEntryPoint(),
    m_cs.workgroupSizeX, m_cs.workgroupSizeY, m_cs.workgroupSizeZ));

  return true;
}


bool Converter::handleMov(ir::Builder& builder, const Instruction& op) {
  /* Mov can either move data without any modification, or
   * apply modifiers, in which case all operands are float. */
  const auto& dst = op.getDst(0u);
  const auto& src = op.getSrc(0u);

  bool hasModifiers = op.getOpToken().isSaturated() || hasAbsNegModifiers(src);
  auto defaultType = dst.getInfo().type;

  if (defaultType == ir::ScalarType::eUnknown && hasModifiers)
    defaultType = ir::ScalarType::eF32;

  auto type = determineOperandType(dst, defaultType, !is64BitType(defaultType));
  auto value = loadSrcModified(builder, op, src, dst.getWriteMask(), type);

  if (!value)
    return false;

  return storeDstModified(builder, op, dst, value);
}


bool Converter::handleMovc(ir::Builder& builder, const Instruction& op) {
  /* movc takes the following operands:
   * (dst0) Destination to move to
   * (src0) Condition, considered true if any bit is set per component
   * (src1) Operand to use if condition is true
   * (src2) Operand to use if condition is false
   */
  const auto& dst = op.getDst(0u);

  const auto& srcTrue = op.getSrc(1u);
  const auto& srcFalse = op.getSrc(2u);

  /* Determine register types based on modifier presence */
  bool hasModifiers = op.getOpToken().isSaturated() ||
    hasAbsNegModifiers(srcTrue) ||
    hasAbsNegModifiers(srcFalse);

  auto defaultType = dst.getInfo().type;

  if (defaultType == ir::ScalarType::eUnknown && hasModifiers)
    defaultType = ir::ScalarType::eF32;

  auto scalarType = determineOperandType(dst, defaultType, !is64BitType(defaultType));
  auto vectorType = makeVectorType(scalarType, dst.getWriteMask());

  /* For dmovc, we need to treat the condition as a 32-bit operand */
  auto condMask = dst.getWriteMask();

  if (is64BitType(defaultType))
    condMask = convertMaskTo32Bit(condMask);

  auto cond = loadSrc(builder, op, op.getSrc(0u), condMask, ir::ScalarType::eBool);

  auto valueTrue = loadSrcModified(builder, op, srcTrue, dst.getWriteMask(), scalarType);
  auto valueFalse = loadSrcModified(builder, op, srcFalse, dst.getWriteMask(), scalarType);

  if (!cond || !valueTrue || !valueFalse)
    return false;

  auto value = builder.add(ir::Op::Select(vectorType, cond, valueTrue, valueFalse));
  return storeDstModified(builder, op, dst, value);
}


bool Converter::handleSwapc(ir::Builder& builder, const Instruction& op) {
  /* swapc takes the following operands:
   * (dst0) First destination, basically cond ? src2 : src1
   * (dst1) Second destination, basically cond ? src1 : src2
   * (src0) Condition, considered true if any bit is set per component
   * (src1) First source
   * (src2) Second source
   */
  const auto& dstA = op.getDst(0u);
  const auto& dstB = op.getDst(1u);

  const auto& srcCond = op.getSrc(0u);

  const auto& srcA = op.getSrc(1u);
  const auto& srcB = op.getSrc(2u);

  /* Not sure if modifiers are supposed to be supported here */
  bool hasModifiers = op.getOpToken().isSaturated() ||
    hasAbsNegModifiers(srcA) || hasAbsNegModifiers(srcB);

  auto scalarType = hasModifiers
    ? ir::ScalarType::eF32
    : ir::ScalarType::eUnknown;

  /* Write masks for both operands can differ, so just do a component-wise swap */
  util::small_vector<ir::SsaDef, 4u> aScalars;
  util::small_vector<ir::SsaDef, 4u> bScalars;

  for (auto c : dstA.getWriteMask() | dstB.getWriteMask()) {
    auto cond = loadSrcModified(builder, op, srcCond, c, ir::ScalarType::eBool);

    auto a = loadSrcModified(builder, op, srcA, c, scalarType);
    auto b = loadSrcModified(builder, op, srcB, c, scalarType);

    if (dstA.getWriteMask() & c)
      aScalars.push_back(builder.add(ir::Op::Select(scalarType, cond, b, a)));

    if (dstB.getWriteMask() & c)
      bScalars.push_back(builder.add(ir::Op::Select(scalarType, cond, a, b)));
  }

  /* Write back result vectors */
  auto aVector = buildVector(builder, scalarType, aScalars.size(), aScalars.data());
  auto bVector = buildVector(builder, scalarType, bScalars.size(), bScalars.data());

  return (!aVector || storeDstModified(builder, op, dstA, aVector)) &&
         (!bVector || storeDstModified(builder, op, dstB, bVector));
}


bool Converter::handleFloatArithmetic(ir::Builder& builder, const Instruction& op) {
  /* All instructions handled here will operate on float vectors of any kind. */
  auto opCode = op.getOpToken().getOpCode();

  dxbc_spv_assert(op.getDstCount() == 1u);
  dxbc_spv_assert(op.getSrcCount());

  /* Instruction type */
  const auto& dst = op.getDst(0u);

  auto defaultType = dst.getInfo().type;
  bool is64Bit = is64BitType(defaultType);

  /* Some ops need to operate on 32-bit floats, so ignore min-precision
   * hints for those. This includes all 64-bit operations. */
  bool supportsMinPrecision = !is64Bit &&
                              opCode != OpCode::eExp &&
                              opCode != OpCode::eLog &&
                              opCode != OpCode::eRsq &&
                              opCode != OpCode::eSqrt;

  auto scalarType = determineOperandType(dst, defaultType, supportsMinPrecision);
  auto vectorType = makeVectorType(scalarType, dst.getWriteMask());

  /* Load source operands */
  util::small_vector<ir::SsaDef, 2u> src;

  for (uint32_t i = 0u; i < op.getSrcCount(); i++) {
    auto value = loadSrcModified(builder, op, op.getSrc(i), dst.getWriteMask(), scalarType);

    if (!value)
      return false;

    src.push_back(value);
  }

  ir::Op result = [opCode, vectorType, &src] {
    switch (opCode) {
      case OpCode::eDAdd:
      case OpCode::eAdd:        return ir::Op::FAdd(vectorType, src.at(0u), src.at(1u));
      case OpCode::eDDiv:
      case OpCode::eDiv:        return ir::Op::FDiv(vectorType, src.at(0u), src.at(1u));
      case OpCode::eExp:        return ir::Op::FExp2(vectorType, src.at(0u));
      case OpCode::eFrc:        return ir::Op::FFract(vectorType, src.at(0u));
      case OpCode::eLog:        return ir::Op::FLog2(vectorType, src.at(0u));
      case OpCode::eDMax:
      case OpCode::eMax:        return ir::Op::FMax(vectorType, src.at(0u), src.at(1u));
      case OpCode::eDMin:
      case OpCode::eMin:        return ir::Op::FMin(vectorType, src.at(0u), src.at(1u));
      case OpCode::eDMul:
      case OpCode::eMul:        return ir::Op::FMul(vectorType, src.at(0u), src.at(1u));
      case OpCode::eDRcp:
      case OpCode::eRcp:        return ir::Op::FRcp(vectorType, src.at(0u));
      case OpCode::eRoundNe:    return ir::Op::FRound(vectorType, src.at(0u), ir::RoundMode::eNearestEven);
      case OpCode::eRoundNi:    return ir::Op::FRound(vectorType, src.at(0u), ir::RoundMode::eNegativeInf);
      case OpCode::eRoundPi:    return ir::Op::FRound(vectorType, src.at(0u), ir::RoundMode::ePositiveInf);
      case OpCode::eRoundZ:     return ir::Op::FRound(vectorType, src.at(0u), ir::RoundMode::eZero);
      case OpCode::eRsq:        return ir::Op::FRsq(vectorType, src.at(0u));
      case OpCode::eSqrt:       return ir::Op::FSqrt(vectorType, src.at(0u));
      default: break;
    }

    dxbc_spv_unreachable();
    return ir::Op();
  } ();

  if (op.getOpToken().getPreciseMask())
    result.setFlags(ir::OpFlag::ePrecise);

  return storeDstModified(builder, op, dst, builder.add(std::move(result)));
}


bool Converter::handleFloatMad(ir::Builder& builder, const Instruction& op) {
  /* Mad and DFma take these operands:
   * (dst0) Result
   * (dst0) First number to multiply
   * (dst1) Second number to multiply
   * (dst2) Number to add to the product
   *
   * FXC is inconsistent in whether it emits Mad or separate multiply and
   * add instructions, which causes invariance issues. Default to separate
   * instructions, unless a precise modifier is used.
   */
  dxbc_spv_assert(op.getDstCount() == 1u);
  dxbc_spv_assert(op.getSrcCount() == 3u);

  /* Instruction type */
  const auto& dst = op.getDst(0u);

  auto defaultType = dst.getInfo().type;
  bool is64Bit = is64BitType(defaultType);

  auto scalarType = determineOperandType(dst, defaultType, !is64Bit);
  auto vectorType = makeVectorType(scalarType, dst.getWriteMask());

  auto factorA = loadSrcModified(builder, op, op.getSrc(0u), dst.getWriteMask(), scalarType);
  auto factorB = loadSrcModified(builder, op, op.getSrc(1u), dst.getWriteMask(), scalarType);
  auto addend = loadSrcModified(builder, op, op.getSrc(2u), dst.getWriteMask(), scalarType);

  if (!factorA || !factorB || !addend)
    return false;

  /* Due to inconsistent fxc code gen, we can never actually emit FMad here even
   * in the precise case. Some games will mix and match SM4 with SM5 shaders and
   * expect consistent result, where SM4 is implicitly precise in all cases. */
  auto mul = builder.add(ir::Op::FMul(vectorType, factorA, factorB));
  auto add = builder.add(ir::Op::FAdd(vectorType, mul, addend));

  if (op.getOpToken().getPreciseMask()) {
    builder.setOpFlags(mul, ir::OpFlag::ePrecise);
    builder.setOpFlags(add, ir::OpFlag::ePrecise);
  }

  return storeDstModified(builder, op, dst, add);
}


bool Converter::handleFloatDot(ir::Builder& builder, const Instruction& op) {
  /* Dp2/3/4 take two vector operands, produce a scalar, and replicate
   * that in all components included in the destination write mask.
   * (dst0) Result. Write mask may not be scalar.
   * (src0) First vector
   * (src1) Second vector
   */
  auto opCode = op.getOpToken().getOpCode();

  /* The opcode determines which source components to read,
   * since the write mask can be literally anything. */
  auto readMask = [opCode] {
    switch (opCode) {
      case OpCode::eDp2: return util::makeWriteMaskForComponents(2u);
      case OpCode::eDp3: return util::makeWriteMaskForComponents(3u);
      case OpCode::eDp4: return util::makeWriteMaskForComponents(4u);
      default: break;
    }

    dxbc_spv_unreachable();
    return WriteMask();
  } ();

  /* Load source vectors and pass them to the internal dot instruction as they are */
  const auto& dst = op.getDst(0u);

  auto scalarType = determineOperandType(dst, ir::ScalarType::eF32);

  auto vectorA = loadSrcModified(builder, op, op.getSrc(0u), readMask, scalarType);
  auto vectorB = loadSrcModified(builder, op, op.getSrc(1u), readMask, scalarType);

  auto result = builder.add(ir::Op::FDot(scalarType, vectorA, vectorB));

  if (op.getOpToken().getPreciseMask())
    builder.setOpFlags(result, ir::OpFlag::ePrecise);

  /* Apply result modifiers *before* broadcasting */
  result = applyDstModifiers(builder, result, op, dst);
  result = broadcastScalar(builder, result, dst.getWriteMask());
  return storeDst(builder, op, dst, result);
}


bool Converter::handleFloatCompare(ir::Builder& builder, const Instruction& op) {
  /* All instructions support two operands with modifiers and return a boolean. */
  auto opCode = op.getOpToken().getOpCode();

  dxbc_spv_assert(op.getDstCount() == 1u);
  dxbc_spv_assert(op.getSrcCount() == 2u);

  const auto& dst = op.getDst(0u);

  const auto& srcA = op.getSrc(0u);
  const auto& srcB = op.getSrc(1u);

  auto componentType = srcA.getInfo().type;
  dxbc_spv_assert(componentType == srcB.getInfo().type);

  bool is64Bit = is64BitType(componentType);

  /* If only one operand is marked as MinF16, promote to F32 to maintain precision. */
  auto srcAType = determineOperandType(srcA, componentType, !is64Bit);
  auto srcBType = determineOperandType(srcB, componentType, !is64Bit);

  if (srcAType != srcBType) {
    srcAType = ir::ScalarType::eF32;
    srcBType = ir::ScalarType::eF32;
  }

  /* Load operands. For the 64-bit variants of these instructions, we need to
   * promote the component mask that we're reading first. */
  auto srcReadMask = dst.getWriteMask();

  if (is64Bit)
    srcReadMask = convertMaskTo64Bit(srcReadMask);

  auto a = loadSrcModified(builder, op, srcA, srcReadMask, srcAType);
  auto b = loadSrcModified(builder, op, srcB, srcReadMask, srcBType);

  if (!a || !b)
    return false;

  /* Result type, make sure to use the correct mask for the component count */
  auto boolType = makeVectorType(ir::ScalarType::eBool, dst.getWriteMask());

  ir::Op result = [opCode, boolType, a, b, &builder] {
    switch (opCode) {
      case OpCode::eEq:
      case OpCode::eDEq:
        return ir::Op::FEq(boolType, a, b);

      case OpCode::eGe:
      case OpCode::eDGe:
        return ir::Op::FGe(boolType, a, b);

      case OpCode::eLt:
      case OpCode::eDLt:
        return ir::Op::FLt(boolType, a, b);

      case OpCode::eNe:
      case OpCode::eDNe:
        return ir::Op::FNe(boolType, a, b);

      default:
        break;
    }

    dxbc_spv_unreachable();
    return ir::Op();
  } ();

  if (op.getOpToken().getPreciseMask())
    result.setFlags(ir::OpFlag::ePrecise);

  /* Convert bool to DXBC integer vector */
  return storeDstModified(builder, op, dst, builder.add(std::move(result)));
}


bool Converter::handleFloatConvert(ir::Builder& builder, const Instruction& op) {
  /* Handles all float-to-int, int-to-float and float-to-float conversions. */
  auto opCode = op.getOpToken().getOpCode();

  dxbc_spv_assert(op.getDstCount() == 1u);
  dxbc_spv_assert(op.getSrcCount() == 1u);

  const auto& dst = op.getDst(0u);
  const auto& src = op.getSrc(0u);

  auto dstMask = dst.getWriteMask();
  auto srcMask = dstMask;

  /* Safe because we never apply min precision to 64-bit */
  auto dstType = makeVectorType(determineOperandType(dst), dstMask);
  auto srcType = makeVectorType(determineOperandType(src), srcMask);

  if (is64BitType(dstType))
    srcMask = convertMaskTo32Bit(srcMask);
  else if (is64BitType(srcType))
    srcMask = convertMaskTo64Bit(srcMask);

  /* Float-to-integer conversions ae saturating, which causes some problems with
   * handling infinity if the destination integer type has a larger range. Do not
   * allow a min-precision source in that case to avoid this. */
  bool dstIsFloat = dstType.isFloatType();
  bool srcIsFloat = srcType.isFloatType();

  if (srcIsFloat && !dstIsFloat)
    srcType = makeVectorType(determineOperandType(src, srcType.getBaseType(), false), srcMask);

  /* Load source operand and apply saturation as necessary. Clamping will flush
   * any NaN values to zero as required by D3D. */
  auto value = loadSrcModified(builder, op, src, srcMask, srcType.getBaseType());

  if (!value)
    return false;

  ir::Op result = [opCode, dstType, value, &builder] {
    switch (opCode) {
      case OpCode::eFtoI:
      case OpCode::eDtoI:
      case OpCode::eFtoU:
      case OpCode::eDtoU:
        return ir::Op::ConvertFtoI(dstType, value);

      case OpCode::eItoF:
      case OpCode::eItoD:
      case OpCode::eUtoF:
      case OpCode::eUtoD:
        return ir::Op::ConvertItoF(dstType, value);

      case OpCode::eDtoF:
      case OpCode::eFtoD:
        return ir::Op::ConvertFtoF(dstType, value);

      default:
        dxbc_spv_unreachable();
        return ir::Op();
    }
  } ();

  if (dstIsFloat && op.getOpToken().getPreciseMask())
    result.setFlags(ir::OpFlag::ePrecise);

  return storeDst(builder, op, dst, builder.add(std::move(result)));
}


bool Converter::handleFloatSinCos(ir::Builder& builder, const Instruction& op) {
  /* sincos takes the following operands:
   * (dst0) Sin result
   * (dst1) Cos result
   * (src0) Source operand
   */
  const auto& dstSin = op.getDst(0u);
  const auto& dstCos = op.getDst(1u);

  util::small_vector<ir::SsaDef, 4u> sinScalars;
  util::small_vector<ir::SsaDef, 4u> cosScalars;

  auto scalarType = ir::ScalarType::eF32;

  for (auto c : (dstSin.getWriteMask() | dstCos.getWriteMask())) {
    auto srcValue = loadSrcModified(builder, op, op.getSrc(0u), c, scalarType);

    if (dstSin.getWriteMask() & c) {
      sinScalars.push_back(builder.add(ir::Op::FSin(scalarType, srcValue)));

      if (op.getOpToken().getPreciseMask() & c)
        builder.setOpFlags(sinScalars.back(), ir::OpFlag::ePrecise);
    }

    if (dstCos.getWriteMask() & c) {
      cosScalars.push_back(builder.add(ir::Op::FCos(scalarType, srcValue)));

      if (op.getOpToken().getPreciseMask() & c)
        builder.setOpFlags(cosScalars.back(), ir::OpFlag::ePrecise);
    }
  }

  /* Either result operand may be null, only store the ones that are defined. */
  auto sinVector = buildVector(builder, scalarType, sinScalars.size(), sinScalars.data());
  auto cosVector = buildVector(builder, scalarType, cosScalars.size(), cosScalars.data());

  return (!sinVector || storeDstModified(builder, op, dstSin, sinVector)) &&
         (!cosVector || storeDstModified(builder, op, dstCos, cosVector));
}


bool Converter::handleIntArithmetic(ir::Builder& builder, const Instruction& op) {
  /* All these instructions operate on integer vectors. */
  auto opCode = op.getOpToken().getOpCode();

  dxbc_spv_assert(op.getDstCount() == 1u);
  dxbc_spv_assert(op.getSrcCount());

  /* Instruction type. Everything supports min precision here. */
  const auto& dst = op.getDst(0u);

  auto scalarType = determineOperandType(dst, ir::ScalarType::eU32);
  auto vectorType = makeVectorType(scalarType, dst.getWriteMask());

  /* Load source operands */
  util::small_vector<ir::SsaDef, 3u> src;

  for (uint32_t i = 0u; i < op.getSrcCount(); i++)
    src.push_back(loadSrcModified(builder, op, op.getSrc(i), dst.getWriteMask(), scalarType));

  ir::SsaDef resultDef = [opCode, vectorType, &builder, &src] {
    switch (opCode) {
      case OpCode::eAnd:  return builder.add(ir::Op::IAnd(vectorType, src.at(0u), src.at(1u)));
      case OpCode::eIAdd: return builder.add(ir::Op::IAdd(vectorType, src.at(0u), src.at(1u)));
      case OpCode::eIMax: return builder.add(ir::Op::SMax(vectorType, src.at(0u), src.at(1u)));
      case OpCode::eIMin: return builder.add(ir::Op::SMin(vectorType, src.at(0u), src.at(1u)));
      case OpCode::eINeg: return builder.add(ir::Op::INeg(vectorType, src.at(0u)));
      case OpCode::eNot:  return builder.add(ir::Op::INot(vectorType, src.at(0u)));
      case OpCode::eOr:   return builder.add(ir::Op::IOr(vectorType, src.at(0u), src.at(1u)));
      case OpCode::eUMax: return builder.add(ir::Op::UMax(vectorType, src.at(0u), src.at(1u)));
      case OpCode::eUMin: return builder.add(ir::Op::UMin(vectorType, src.at(0u), src.at(1u)));
      case OpCode::eXor:  return builder.add(ir::Op::IXor(vectorType, src.at(0u), src.at(1u)));

      case OpCode::eIMad:
      case OpCode::eUMad: return builder.add(ir::Op::IAdd(vectorType,
        builder.add(ir::Op::IMul(vectorType, src.at(0u), src.at(1u))), src.at(2u)));

      default: break;
    }

    dxbc_spv_unreachable();
    return ir::SsaDef();
  } ();

  return storeDst(builder, op, dst, resultDef);
}


bool Converter::handleIntMultiply(ir::Builder& builder, const Instruction& op) {
  /* imul and umul can operate either normally on 32-bit values,
   * or produce an extended 64-bit result.
   * (dst0) High result bits
   * (dst1) Low result bits
   * (src0) First source operand
   * (src1) Second source operand
   */
  const auto& dstHi = op.getDst(0u);
  const auto& dstLo = op.getDst(1u);

  if (dstHi.getRegisterType() == RegisterType::eNull) {
    /* Default to an unsigned type regardless of op type */
    auto scalarType = determineOperandType(dstLo, ir::ScalarType::eU32);

    if (scalarType == ir::ScalarType::eI32)
      scalarType = ir::ScalarType::eU32;

    const auto& srcA = loadSrcModified(builder, op, op.getSrc(0u), dstLo.getWriteMask(), scalarType);
    const auto& srcB = loadSrcModified(builder, op, op.getSrc(1u), dstLo.getWriteMask(), scalarType);

    auto resultType = makeVectorType(scalarType, dstLo.getWriteMask());
    auto resultDef = builder.add(ir::Op::IMul(resultType, srcA, srcB));

    return storeDst(builder, op, dstLo, resultDef);
  } else {
    auto scalarType = determineOperandType(dstHi, ir::ScalarType::eU32, false);

    /* Scalarize over merged write mask */
    util::small_vector<ir::SsaDef, 4u> loScalars;
    util::small_vector<ir::SsaDef, 4u> hiScalars;

    for (auto c : (dstHi.getWriteMask() | dstLo.getWriteMask())) {
      const auto& srcA = loadSrcModified(builder, op, op.getSrc(0u), c, scalarType);
      const auto& srcB = loadSrcModified(builder, op, op.getSrc(1u), c, scalarType);

      if (dstHi.getWriteMask() & c) {
        auto resultType = ir::BasicType(scalarType, 2u);
        auto resultDef = builder.add(ir::Op::SMulExtended(resultType, srcA, srcB));

        hiScalars.push_back(extractFromVector(builder, resultDef, 1u));

        if (dstLo.getWriteMask() & c)
          loScalars.push_back(extractFromVector(builder, resultDef, 0u));
      } else {
        /* Don't need to use extended mul if we discard the high bits */
        auto resultDef = builder.add(ir::Op::IMul(scalarType, srcA, srcB));
        loScalars.push_back(resultDef);
      }
    }

    bool success = storeDst(builder, op, dstHi,
      buildVector(builder, scalarType, hiScalars.size(), hiScalars.data()));

    if (!loScalars.empty()) {
      success = success && storeDst(builder, op, dstLo,
        buildVector(builder, scalarType, loScalars.size(), loScalars.data()));
    }

    return success;
  }
}


bool Converter::handleIntDivide(ir::Builder& builder, const Instruction& op) {
  /* udiv has the following operands:
   * (dst0) Quotient
   * (dst1) Remainder
   * (src0) Number to divide
   * (src1) Divisor
   *
   * Division by zero returns -1 in both operands.
   */
  const auto& dstDiv = op.getDst(0u);
  const auto& dstMod = op.getDst(1u);

  auto scalarType = dstDiv.getRegisterType() != RegisterType::eNull
    ? determineOperandType(dstDiv, ir::ScalarType::eU32)
    : determineOperandType(dstMod, ir::ScalarType::eU32);

  /* Process division one component at a time */
  auto zero = makeTypedConstant(builder, scalarType,  0u);
  auto neg1 = makeTypedConstant(builder, scalarType, -1u);

  util::small_vector<ir::SsaDef, 4u> divScalars;
  util::small_vector<ir::SsaDef, 4u> modScalars;

  for (auto c : (dstDiv.getWriteMask() | dstMod.getWriteMask())) {
    const auto& num = loadSrcModified(builder, op, op.getSrc(0u), c, scalarType);
    const auto& den = loadSrcModified(builder, op, op.getSrc(1u), c, scalarType);

    auto cond = builder.add(ir::Op::INe(ir::ScalarType::eBool, den, zero));

    if (dstDiv.getWriteMask() & c) {
      auto& scalar = divScalars.emplace_back();
      scalar = builder.add(ir::Op::UDiv(scalarType, num, den));
      scalar = builder.add(ir::Op::Select(scalarType, cond, scalar, neg1));
    }

    if (dstMod.getWriteMask() & c) {
      auto& scalar = modScalars.emplace_back();
      scalar = builder.add(ir::Op::UMod(scalarType, num, den));
      scalar = builder.add(ir::Op::Select(scalarType, cond, scalar, neg1));
    }
  }

  /* Either result operand may be null, only store the ones that are defined. */
  auto divVector = buildVector(builder, scalarType, divScalars.size(), divScalars.data());
  auto modVector = buildVector(builder, scalarType, modScalars.size(), modScalars.data());

  return (!divVector || storeDstModified(builder, op, dstDiv, divVector)) &&
         (!modVector || storeDstModified(builder, op, dstMod, modVector));
}


bool Converter::handleIntExtended(ir::Builder& builder, const Instruction& op) {
  /* uaddc and usubb have the following operands:
   * (dst0) Lower bits of the addition.
   * (dst1) Caarry or borrow bit (1 or 0).
   * (src0) First source operand
   * (src1) Second source operand
   */
  const auto& dstLo = op.getDst(0u);
  const auto& dstHi = op.getDst(1u);

  const auto& srcA = op.getSrc(0u);
  const auto& srcB = op.getSrc(1u);

  auto [extOp, baseOp] = [&op] {
    switch (op.getOpToken().getOpCode()) {
      case OpCode::eUAddc: return std::make_pair(ir::OpCode::eIAddCarry,  ir::OpCode::eIAdd);
      case OpCode::eUSubb: return std::make_pair(ir::OpCode::eISubBorrow, ir::OpCode::eISub);
      default:             break;
    }

    dxbc_spv_unreachable();
    return std::make_pair(ir::OpCode::eUnknown, ir::OpCode::eUnknown);
  } ();

  /* Scalarize since our IR will return vec2<u32>. */
  auto scalarType = ir::ScalarType::eU32;

  util::small_vector<ir::SsaDef, 4u> loScalars;
  util::small_vector<ir::SsaDef, 4u> hiScalars;

  for (auto c : (dstLo.getWriteMask() | dstHi.getWriteMask())) {
    auto a = loadSrcModified(builder, op, srcA, c, scalarType);
    auto b = loadSrcModified(builder, op, srcB, c, scalarType);

    if (dstHi.getWriteMask() & c) {
      auto result = builder.add(ir::Op(extOp, ir::BasicType(scalarType, 2u)).addOperands(a, b));
      hiScalars.push_back(builder.add(ir::Op::CompositeExtract(scalarType, result, builder.makeConstant(1u))));

      if (dstLo.getWriteMask() & c)
        loScalars.push_back(builder.add(ir::Op::CompositeExtract(scalarType, result, builder.makeConstant(0u))));
    } else {
      /* Use a simple add / sub if we don't need the carry bit */
      loScalars.push_back(builder.add(ir::Op(baseOp, scalarType).addOperands(a, b)));
    }
  }

  /* Write back results. */
  auto loVector = buildVector(builder, scalarType, loScalars.size(), loScalars.data());
  auto hiVector = buildVector(builder, scalarType, hiScalars.size(), hiScalars.data());

  return (!loVector || storeDstModified(builder, op, dstLo, loVector)) &&
         (!hiVector || storeDstModified(builder, op, dstHi, hiVector));
}


bool Converter::handleIntShift(ir::Builder& builder, const Instruction& op) {
  /* Shift instructions only use the lower 5 bits of the shift amount.
   * (dst0) Result value
   * (src0) Operand to shift
   * (src1) Shift amount
   */
  const auto& dst = op.getDst(0u);

  auto scalarType = determineOperandType(dst, ir::ScalarType::eU32);
  auto src = loadSrcModified(builder, op, op.getSrc(0u), dst.getWriteMask(), scalarType);
  auto amount = loadSrcBitCount(builder, op, op.getSrc(1u), dst.getWriteMask());

  auto opCode = [&op] {
    switch (op.getOpToken().getOpCode()) {
      case OpCode::eIShl: return ir::OpCode::eIShl;
      case OpCode::eIShr: return ir::OpCode::eSShr;
      case OpCode::eUShr: return ir::OpCode::eUShr;
      default:            break;
    }

    dxbc_spv_unreachable();
    return ir::OpCode::eUnknown;
  } ();

  auto resultOp = ir::Op(opCode, makeVectorType(scalarType, dst.getWriteMask()))
    .addOperand(src)
    .addOperand(amount);

  return storeDst(builder, op, dst, builder.add(std::move(resultOp)));
}


bool Converter::handleIntCompare(ir::Builder& builder, const Instruction& op) {
  /* All instructions support two operands with modifiers and return a boolean. */
  auto opCode = op.getOpToken().getOpCode();

  dxbc_spv_assert(op.getDstCount() == 1u);
  dxbc_spv_assert(op.getSrcCount() == 2u);

  const auto& dst = op.getDst(0u);

  const auto& srcA = op.getSrc(0u);
  const auto& srcB = op.getSrc(1u);

  /* Operand types may differ in signedness, but need to have the same
   * bit width. Get rid of min precision if necessary. */
  auto srcAType = determineOperandType(srcA, ir::ScalarType::eU32);
  auto srcBType = determineOperandType(srcB, ir::ScalarType::eU32);

  if (ir::BasicType(srcAType).isMinPrecisionType() != ir::BasicType(srcBType).isMinPrecisionType()) {
    srcAType = determineOperandType(srcA, ir::ScalarType::eU32, false);
    srcBType = determineOperandType(srcB, ir::ScalarType::eU32, false);
  }

  /* Load operands */
  auto a = loadSrcModified(builder, op, srcA, dst.getWriteMask(), srcAType);
  auto b = loadSrcModified(builder, op, srcB, dst.getWriteMask(), srcBType);

  if (!a || !b)
    return false;

  auto boolType = makeVectorType(ir::ScalarType::eBool, dst.getWriteMask());

  ir::Op result = [opCode, boolType, a, b, &builder] {
    switch (opCode) {
      case OpCode::eIEq: return ir::Op::IEq(boolType, a, b);
      case OpCode::eINe: return ir::Op::INe(boolType, a, b);
      case OpCode::eIGe: return ir::Op::SGe(boolType, a, b);
      case OpCode::eILt: return ir::Op::SLt(boolType, a, b);
      case OpCode::eUGe: return ir::Op::UGe(boolType, a, b);
      case OpCode::eULt: return ir::Op::ULt(boolType, a, b);
      default: break;
    }

    dxbc_spv_unreachable();
    return ir::Op();
  } ();

  return storeDstModified(builder, op, dst, builder.add(std::move(result)));
}


bool Converter::handleBitExtract(ir::Builder& builder, const Instruction& op) {
  /* ubfe takes the following operands:
   * (dst0) Result value
   * (src0) Number of bits to extract
   * (src1) Offset of bits to extract
   * (src2) Operand to extract from
   *
   * Like shift instructions, this only considers the 5 least
   * significant bits of the count and offset operands.
   */
  const auto& dst = op.getDst(0u);

  auto scalarType = determineOperandType(dst, ir::ScalarType::eU32, false);

  auto value = loadSrcModified(builder, op, op.getSrc(2u), dst.getWriteMask(), scalarType);
  auto offset = loadSrcBitCount(builder, op, op.getSrc(1u), dst.getWriteMask());
  auto count = loadSrcBitCount(builder, op, op.getSrc(0u), dst.getWriteMask());

  /* Clamp bit count so that offset + size are not larger than 32 */
  auto countType = builder.getOp(count).getType().getBaseType(0u);

  count = builder.add(ir::Op::UMin(countType, count,
    builder.add(ir::Op::ISub(countType, makeTypedConstant(builder, countType, 32), offset))));

  auto opCode = [&op] {
    switch (op.getOpToken().getOpCode()) {
      case OpCode::eUBfe: return ir::OpCode::eUBitExtract;
      case OpCode::eIBfe: return ir::OpCode::eSBitExtract;
      default:            break;
    }

    dxbc_spv_unreachable();
    return ir::OpCode::eUnknown;
  } ();

  auto resultDef = builder.add(ir::Op(opCode, makeVectorType(scalarType, dst.getWriteMask()))
    .addOperand(value)
    .addOperand(offset)
    .addOperand(count));

  return storeDst(builder, op, dst, resultDef);
}


bool Converter::handleBitInsert(ir::Builder& builder, const Instruction& op) {
  /* bfi takes the following operands:
   * (dst0) Result value
   * (src0) Number of bits to take from src2
   * (src1) Offset where to insert the bits
   * (src2) Operand to insert
   * (src3) Base operand
   *
   * Like shift instructions, this only considers the 5 least
   * significant bits of the count and offset operands.
   */
  const auto& dst = op.getDst(0u);

  auto scalarType = determineOperandType(dst, ir::ScalarType::eU32, false);

  auto base = loadSrcModified(builder, op, op.getSrc(3u), dst.getWriteMask(), scalarType);
  auto value = loadSrcModified(builder, op, op.getSrc(2u), dst.getWriteMask(), scalarType);

  auto offset = loadSrcBitCount(builder, op, op.getSrc(1u), dst.getWriteMask());
  auto count = loadSrcBitCount(builder, op, op.getSrc(0u), dst.getWriteMask());

  auto resultDef = builder.add(ir::Op::IBitInsert(
    makeVectorType(scalarType, dst.getWriteMask()),
    base, value, offset, count));

  return storeDst(builder, op, dst, resultDef);
}


bool Converter::handleBitOp(ir::Builder& builder, const Instruction& op) {
  /* All operations have a single integer destination and source operand.
   *
   * FirstBitHi counts bits from the MSB rather than the LSB and functions
   * more like a leading zero / one count. FirstBit operations return -1 if
   * the input is zero.
   */
  auto opCode = op.getOpToken().getOpCode();

  const auto& dst = op.getDst(0u);
  const auto& src = op.getSrc(0u);

  /* These pretty much need to run on 32-bit operands */
  auto dstType = determineOperandType(dst, ir::ScalarType::eI32, false);
  auto srcType = determineOperandType(src, ir::ScalarType::eU32, false);

  auto srcValue = loadSrcModified(builder, op, src, dst.getWriteMask(), srcType);

  auto dstVectorType = makeVectorType(dstType, dst.getWriteMask());
  auto srcVectorType = makeVectorType(srcType, dst.getWriteMask());

  auto result = [&] {
    switch (opCode) {
      case OpCode::eCountBits:
        return builder.add(ir::Op::IBitCount(dstVectorType, srcValue));

      case OpCode::eBfRev:
        return builder.add(ir::Op::IBitReverse(dstVectorType, srcValue));

      case OpCode::eFirstBitLo:
        return builder.add(ir::Op::IFindLsb(dstVectorType, srcValue));

      case OpCode::eFirstBitHi:
        return builder.add(ir::Op::UFindMsb(dstVectorType, srcValue));

      case OpCode::eFirstBitShi:
        return builder.add(ir::Op::SFindMsb(dstVectorType, srcValue));

      default:
        break;
    }

    dxbc_spv_unreachable();
    return ir::SsaDef();
  } ();

  /* Fix up findmsb results for zero inputs */
  if (opCode == OpCode::eFirstBitHi || opCode == OpCode::eFirstBitShi) {
    auto condType = makeVectorType(ir::ScalarType::eBool, dst.getWriteMask());

    auto bitCnt = makeTypedConstant(builder, dstVectorType, 32);
    auto maxBit = makeTypedConstant(builder, dstVectorType, 31);

    if (opCode == OpCode::eFirstBitShi) {
      auto cond = builder.add(ir::Op::INe(condType, result, makeTypedConstant(builder, dstVectorType, -1)));

      result = builder.add(ir::Op::Select(dstVectorType, cond, result, bitCnt));
      result = builder.add(ir::Op::ISub(dstVectorType, maxBit, result));
    } else {
      auto cond = builder.add(ir::Op::INe(condType, srcValue, makeTypedConstant(builder, srcVectorType, 0)));

      result = builder.add(ir::Op::Select(dstVectorType, cond, result, bitCnt));
      result = builder.add(ir::Op::ISub(dstVectorType, maxBit, result));
    }
  }

  return storeDstModified(builder, op, dst, result);
}


bool Converter::handleMsad(ir::Builder& builder, const Instruction& op) {
  /* msad takes the following operands:
   * (dst0) Accumulated result
   * (src0) Reference value (4x 8 bit)
   * (src1) Source value (4x 8 bit)
   * (src2) Accumulator
   */
  const auto& dst = op.getDst(0u);

  /* We could technically support min precision here, but since no real
   * content seems to use the instruction anyway, don't bother. */
  auto resultType = determineOperandType(dst, ir::ScalarType::eU32, false);

  /* Always load ref and source as 32-bit */
  auto ref = loadSrcModified(builder, op, op.getSrc(0u), dst.getWriteMask(), ir::ScalarType::eU32);
  auto src = loadSrcModified(builder, op, op.getSrc(1u), dst.getWriteMask(), ir::ScalarType::eU32);

  auto accum = loadSrcModified(builder, op, op.getSrc(2u), dst.getWriteMask(), resultType);

  auto vectorType = makeVectorType(resultType, dst.getWriteMask());
  auto result = builder.add(ir::Op::UMSad(vectorType, ref, src, accum));

  return storeDstModified(builder, op, dst, result);
}


bool Converter::handleF16toF32(ir::Builder& builder, const Instruction& op) {
  /* Performs a legacy F16 to F32 conversion one component at a time */
  const auto& dst = op.getDst(0u);
  const auto& src = op.getSrc(0u);

  auto dstType = determineOperandType(dst, ir::ScalarType::eF32);
  auto srcType = determineOperandType(src, ir::ScalarType::eU32);

  util::small_vector<ir::SsaDef, 4u> scalars = { };

  for (auto c : dst.getWriteMask()) {
    auto value = loadSrcModified(builder, op, src, c, srcType);
    value = builder.add(ir::Op::ConvertPackedF16toF32(ir::BasicType(dstType, 2u), value));
    value = builder.add(ir::Op::CompositeExtract(dstType, value, builder.makeConstant(0u)));
    scalars.push_back(value);
  }

  auto vector = buildVector(builder, dstType, scalars.size(), scalars.data());
  return storeDstModified(builder, op, dst, vector);
}


bool Converter::handleF32toF16(ir::Builder& builder, const Instruction& op) {
  /* Performs a legacy F32 to F16 conversion one component at a time
   *
   * Subsequent passes need to ensure round-to-zero behaviour, and may try
   * to merge packed conversions. Allow min-precision types on either side
   * of the operation, which may get lowered.
   */
  const auto& dst = op.getDst(0u);
  const auto& src = op.getSrc(0u);

  auto dstType = determineOperandType(dst, ir::ScalarType::eU32);
  auto srcType = determineOperandType(src, ir::ScalarType::eF32);

  util::small_vector<ir::SsaDef, 4u> scalars = { };

  for (auto c : dst.getWriteMask()) {
    auto value = loadSrcModified(builder, op, src, c, srcType);

    value = builder.add(ir::Op::CompositeConstruct(ir::BasicType(srcType, 2u),
      value, makeTypedConstant(builder, srcType, 0.0f)));

    value = builder.add(ir::Op::ConvertF32toPackedF16(dstType, value));
    scalars.push_back(value);
  }

  auto vector = buildVector(builder, dstType, scalars.size(), scalars.data());
  return storeDstModified(builder, op, dst, vector);
}


bool Converter::handleDerivatives(ir::Builder& builder, const Instruction& op) {
  /* deriv_rt[xy] take the following operands:
   * (dst0) Result value
   * (src0) Source value to compute derivative of
   */
  const auto& dst = op.getDst(0u);
  const auto& src = op.getSrc(0u);

  auto scalarType = determineOperandType(dst, ir::ScalarType::eF32, false);

  auto [opCode, mode] = [&op] {
    switch (op.getOpToken().getOpCode()) {
      case OpCode::eDerivRtx:       return std::make_pair(ir::OpCode::eDerivX, ir::DerivativeMode::eDefault);
      case OpCode::eDerivRty:       return std::make_pair(ir::OpCode::eDerivY, ir::DerivativeMode::eDefault);
      case OpCode::eDerivRtxCoarse: return std::make_pair(ir::OpCode::eDerivX, ir::DerivativeMode::eCoarse);
      case OpCode::eDerivRtyCoarse: return std::make_pair(ir::OpCode::eDerivY, ir::DerivativeMode::eCoarse);
      case OpCode::eDerivRtxFine:   return std::make_pair(ir::OpCode::eDerivX, ir::DerivativeMode::eFine);
      case OpCode::eDerivRtyFine:   return std::make_pair(ir::OpCode::eDerivY, ir::DerivativeMode::eFine);
      default:                      break;
    }

    dxbc_spv_unreachable();
    return std::make_pair(ir::OpCode::eUnknown, ir::DerivativeMode::eDefault);
  } ();

  auto value = loadSrcModified(builder, op, src, dst.getWriteMask(), scalarType);

  value = builder.add(ir::Op(opCode, makeVectorType(scalarType, dst.getWriteMask()))
    .addOperand(value).addOperand(mode));

  if (op.getOpToken().getPreciseMask())
    builder.setOpFlags(value, ir::OpFlag::ePrecise);

  return storeDstModified(builder, op, dst, value);
}


bool Converter::handleLdRaw(ir::Builder& builder, const Instruction& op) {
  /* ld_structured has the following operands:
   * (dst0) Result vector
   * (dst1) Sparse feedback value (scalar, optional)
   * (src0) Byte offset
   * (src1) Resource register (u# / t# / g#)
   */
  const auto& dstValue = op.getDst(0u);
  const auto& resource = op.getSrc(1u);

  auto byteOffset = loadSrcModified(builder, op, op.getSrc(0u), ComponentBit::eX, ir::ScalarType::eU32);
  auto dstType = determineOperandType(dstValue, ir::ScalarType::eUnknown);

  if (resource.getRegisterType() == RegisterType::eTgsm) {
    auto data = m_regFile.emitTgsmLoad(builder, op, resource,
      byteOffset, ir::SsaDef(), dstValue.getWriteMask(), dstType);
    return data && storeDstModified(builder, op, dstValue, data);
  } else {
    auto [data, feedback] = m_resources.emitRawStructuredLoad(builder, op,
      resource, byteOffset, ir::SsaDef(), dstValue.getWriteMask(), dstType);

    if (feedback && !storeDstModified(builder, op, op.getDst(1u), feedback))
      return false;

    if (resource.getRegisterType() == RegisterType::eUav)
      m_resources.setUavFlagsForLoad(builder, op, resource);

    return data && storeDstModified(builder, op, dstValue, data);
  }
}


bool Converter::handleLdStructured(ir::Builder& builder, const Instruction& op) {
  /* ld_structured has the following operands:
   * (dst0) Result vector
   * (dst1) Sparse feedback value (scalar, optional)
   * (src0) Structure index
   * (src1) Structure offset
   * (src2) Resource register (u# / t# / g#)
   */
  const auto& dstValue = op.getDst(0u);

  auto structIndex = loadSrcModified(builder, op, op.getSrc(0u), ComponentBit::eX, ir::ScalarType::eU32);
  auto structOffset = loadSrcModified(builder, op, op.getSrc(1u), ComponentBit::eX, ir::ScalarType::eU32);

  const auto& resource = op.getSrc(2u);

  auto dstType = determineOperandType(dstValue, ir::ScalarType::eUnknown);

  if (resource.getRegisterType() == RegisterType::eTgsm) {
    auto data = m_regFile.emitTgsmLoad(builder, op, resource,
      structIndex, structOffset, dstValue.getWriteMask(), dstType);
    return data && storeDstModified(builder, op, dstValue, data);
  } else {
    auto [data, feedback] = m_resources.emitRawStructuredLoad(builder, op,
      resource, structIndex, structOffset, dstValue.getWriteMask(), dstType);

    if (feedback && !storeDstModified(builder, op, op.getDst(1u), feedback))
      return false;

    if (resource.getRegisterType() == RegisterType::eUav)
      m_resources.setUavFlagsForLoad(builder, op, resource);

    return data && storeDstModified(builder, op, dstValue, data);
  }
}


bool Converter::handleLdTyped(ir::Builder& builder, const Instruction& op) {
  /* ld has the following operands:
   * (dst0) Result vector
   * (dst1) Sparse feedback value (scalar, optional)
   * (src0) Coordinate (element index for typed buffers).
   *        The .w coordinate provides the mip level if applicable.
   * (src1) Resource to load from
   * (src2) Sample index (for the _ms variants)
   */
  auto opCode = op.getOpToken().getOpCode();

  bool hasSparseFeedback = op.getDstCount() == 2u &&
    op.getDst(1u).getRegisterType() != RegisterType::eNull;

  const auto& dst = op.getDst(0u);
  const auto& address = op.getSrc(0u);
  const auto& resource = op.getSrc(1u);

  /* Load descriptor and get basic resource properties */
  auto resourceInfo = m_resources.emitDescriptorLoad(builder, op, resource);

  if (!resourceInfo.descriptor)
    return false;

  /* Load coordinates as unsigned integers */
  auto [coord, layer] = computeTypedCoordLayer(builder, op,
    address, resourceInfo.kind, ir::ScalarType::eU32);

  /* Load mip level index */
  bool hasMips = resource.getRegisterType() == RegisterType::eResource &&
    !ir::resourceIsMultisampled(resourceInfo.kind) &&
    !ir::resourceIsBuffer(resourceInfo.kind);

  ir::SsaDef mipLevel = { };

  if (hasMips)
    mipLevel = loadSrcModified(builder, op, address, ComponentBit::eW, ir::ScalarType::eU32);

  /* If applicable, load sample index */
  bool isMultisampledOp = opCode == OpCode::eLdMs || opCode == OpCode::eLdMsS;

  ir::SsaDef sampleIndex = { };

  if (isMultisampledOp && ir::resourceIsMultisampled(resourceInfo.kind))
    sampleIndex = loadSrcModified(builder, op, op.getSrc(2u), ComponentBit::eX, ir::ScalarType::eU32);

  /* Load immediate offset */
  ir::SsaDef offset = getImmediateTextureOffset(builder, op, resourceInfo.kind);

  /* Determine return type */
  ir::BasicType texelType(resourceInfo.type, 4u);
  ir::Type returnType(texelType);

  if (hasSparseFeedback)
    returnType = makeSparseFeedbackType(texelType);

  /* Emit actual load */
  auto loadOp = resourceIsBuffer(resourceInfo.kind)
    ? ir::Op::BufferLoad(returnType, resourceInfo.descriptor, coord, 0u)
    : ir::Op::ImageLoad(returnType, resourceInfo.descriptor,
        mipLevel, layer, coord, sampleIndex, offset);

  if (hasSparseFeedback)
    loadOp.setFlags(ir::OpFlag::eSparseFeedback);

  /* Take result apart and write it back to the destination registers */
  auto [feedback, value] = decomposeResourceReturn(builder, builder.add(std::move(loadOp)));

  if (hasSparseFeedback) {
    if (!storeDst(builder, op, op.getDst(1u), feedback))
      return false;
  }

  if (resource.getRegisterType() == RegisterType::eUav)
    m_resources.setUavFlagsForLoad(builder, op, resource);

  return storeDstModified(builder, op, dst, swizzleVector(builder, value, resource.getSwizzle(), dst.getWriteMask()));

}


bool Converter::handleStoreRaw(ir::Builder& builder, const Instruction& op) {
  /* store_raw has the following operands:
   * (dst0) Target resource
   * (src0) Byte address
   * (src1) Data to store
   */
  const auto& resource = op.getDst(0u);
  const auto& srcData = op.getSrc(1u);
  auto srcType = determineOperandType(srcData, ir::ScalarType::eUnknown);

  auto byteOffset = loadSrcModified(builder, op, op.getSrc(0u), ComponentBit::eX, ir::ScalarType::eU32);
  auto value = loadSrcModified(builder, op, srcData, resource.getWriteMask(), srcType);

  if (resource.getRegisterType() == RegisterType::eTgsm) {
    return m_regFile.emitTgsmStore(builder, op,
      resource, byteOffset, ir::SsaDef(), value);
  } else {
    m_resources.setUavFlagsForStore(builder, op, resource);

    return m_resources.emitRawStructuredStore(builder, op,
      resource, byteOffset, ir::SsaDef(), value);
  }
}


bool Converter::handleStoreStructured(ir::Builder& builder, const Instruction& op) {
  /* store_structured has the following operands:
   * (dst0) Target resource
   * (src0) Structure index
   * (src1) Structure offset
   * (src2) Data to store
   */
  const auto& resource = op.getDst(0u);

  auto structIndex = loadSrcModified(builder, op, op.getSrc(0u), ComponentBit::eX, ir::ScalarType::eU32);
  auto structOffset = loadSrcModified(builder, op, op.getSrc(1u), ComponentBit::eX, ir::ScalarType::eU32);

  const auto& srcData = op.getSrc(2u);
  auto srcType = determineOperandType(srcData, ir::ScalarType::eUnknown);

  auto value = loadSrcModified(builder, op, srcData, resource.getWriteMask(), srcType);

  if (resource.getRegisterType() == RegisterType::eTgsm) {
    return m_regFile.emitTgsmStore(builder, op,
      resource, structIndex, structOffset, value);
  } else {
    m_resources.setUavFlagsForStore(builder, op, resource);

    return m_resources.emitRawStructuredStore(builder, op,
      resource, structIndex, structOffset, value);
  }
}


bool Converter::handleStoreTyped(ir::Builder& builder, const Instruction& op) {
  /* store_uav_typed has the following operands:
   * (dst0) Target resource
   * (src0) Texture coordinate or element index
   * (src1) Value to store
   */
  const auto& resource = op.getDst(0u);
  const auto& address = op.getSrc(0u);

  /* Load resource descriptor */
  auto resourceInfo = m_resources.emitDescriptorLoad(builder, op, resource);

  if (!resourceInfo.descriptor)
    return false;

  /* Load coordinates as unsigned integers */
  auto [coord, layer] = computeTypedCoordLayer(builder, op,
    address, resourceInfo.kind, ir::ScalarType::eU32);

  /* Unconditionally load value as a vec4 and emit store */
  ir::SsaDef value = loadSrcModified(builder, op, op.getSrc(1u), ComponentBit::eAll, resourceInfo.type);

  if (ir::resourceIsBuffer(resourceInfo.kind))
    builder.add(ir::Op::BufferStore(resourceInfo.descriptor, coord, value, 0u));
  else
    builder.add(ir::Op::ImageStore(resourceInfo.descriptor, layer, coord, value));

  m_resources.setUavFlagsForStore(builder, op, resource);
  return true;
}


bool Converter::handleAtomic(ir::Builder& builder, const Instruction& op) {
  /* Atomic instructions have the following layout:
   * (dst0) Return value, for the imm_* variants. Omitted otherwise.
   * (dst1) UAV or TGSM register to perform the atomic on.
   * (src0) Address into the UAV or TGSM register.
   * (src1..n) Scalar operands.
   *
   * For images and typed buffers, we assume the type of the atomic operation
   * to be the same as the declared resource type, otherwise U32 is used.
   */
  auto opCode = op.getOpToken().getOpCode();

  /* Get target format and, if applicable, load resource descriptor. */
  const auto& target = op.getDst(op.getDstCount() - 1u);
  const auto& address = op.getSrc(0u);

  if (target.getRegisterType() != RegisterType::eUav &&
      target.getRegisterType() != RegisterType::eTgsm)
    return logOpError(op, "Invalid target register for atomic instruction.");

  ResourceProperties resource = { };
  resource.type = ir::ScalarType::eU32;

  if (target.getRegisterType() == RegisterType::eUav) {
    resource = m_resources.emitDescriptorLoad(builder, op, target);

    if (!resource.descriptor)
      return false;

    if (!ir::resourceIsTyped(resource.kind))
      resource.type = ir::ScalarType::eU32;
  }

  bool hasReturnValue = op.getDstCount() == 2u;
  auto returnType = hasReturnValue ? resource.type : ir::ScalarType::eVoid;

  /* Load operands and stick them into a vector as necessary */
  util::small_vector<ir::SsaDef, 2u> operandScalars;

  for (uint32_t i = 1u; i < op.getSrcCount(); i++)
    operandScalars.push_back(loadSrcModified(builder, op, op.getSrc(i), ComponentBit::eX, resource.type));

  auto operandVector = buildVector(builder, resource.type,
    operandScalars.size(), operandScalars.data());

  /* Determine atomic op to use based on the incoming opcode */
  auto atomicOpType = [opCode] {
    switch (opCode) {
      case OpCode::eAtomicAnd:
      case OpCode::eImmAtomicAnd:     return ir::AtomicOp::eAnd;
      case OpCode::eAtomicOr:
      case OpCode::eImmAtomicOr:      return ir::AtomicOp::eOr;
      case OpCode::eAtomicXor:
      case OpCode::eImmAtomicXor:     return ir::AtomicOp::eXor;
      case OpCode::eAtomicCmpStore:
      case OpCode::eImmAtomicCmpExch: return ir::AtomicOp::eCompareExchange;
      case OpCode::eImmAtomicExch:    return ir::AtomicOp::eExchange;
      case OpCode::eAtomicIAdd:
      case OpCode::eImmAtomicIAdd:    return ir::AtomicOp::eAdd;
      case OpCode::eAtomicIMin:
      case OpCode::eImmAtomicIMin:    return ir::AtomicOp::eSMin;
      case OpCode::eAtomicIMax:
      case OpCode::eImmAtomicIMax:    return ir::AtomicOp::eSMax;
      case OpCode::eAtomicUMin:
      case OpCode::eImmAtomicUMin:    return ir::AtomicOp::eUMin;
      case OpCode::eAtomicUMax:
      case OpCode::eImmAtomicUMax:    return ir::AtomicOp::eUMax;
      default:                        break;
    }

    dxbc_spv_unreachable();
    return ir::AtomicOp::eLoad;
  } ();

  /* Prepare atomic op. Address calculation is very annoying here since it
   * heavily depends on the resource type, and does not follow the regular
   * ld/store patterns for structured buffers. */
  ir::OpCode atomicOpCode = ir::OpCode::eLdsAtomic;

  if (target.getRegisterType() == RegisterType::eUav) {
    atomicOpCode = ir::resourceIsBuffer(resource.kind)
      ? ir::OpCode::eBufferAtomic
      : ir::OpCode::eImageAtomic;
  }

  ir::Op atomicOp(atomicOpCode, returnType);

  if (target.getRegisterType() == RegisterType::eTgsm) {
    auto [base, index] = m_regFile.computeTgsmAddress(builder, op, target, address);

    atomicOp.addOperand(base);
    atomicOp.addOperand(index);
  } else {
    atomicOp.addOperand(resource.descriptor);

    if (ir::resourceIsTyped(resource.kind)) {
      auto [coord, layer] = computeTypedCoordLayer(builder, op,
        address, resource.kind, ir::ScalarType::eU32);

      if (!ir::resourceIsBuffer(resource.kind))
        atomicOp.addOperand(layer);

      atomicOp.addOperand(coord);
    } else {
      atomicOp.addOperand(computeAtomicBufferAddress(builder, op, address, resource.kind));
    }

    m_resources.setUavFlagsForAtomic(builder, op, target);
  }

  atomicOp.addOperand(operandVector);
  atomicOp.addOperand(atomicOpType);

  auto atomicDef = builder.add(std::move(atomicOp));

  if (!hasReturnValue)
    return true;

  /* Write back result if requested */
  const auto& dst = op.getDst(0u);
  atomicDef = broadcastScalar(builder, atomicDef, dst.getWriteMask());
  return storeDstModified(builder, op, dst, atomicDef);
}


bool Converter::handleAtomicCounter(ir::Builder& builder, const Instruction& op) {
  /* imm_atomic_{alloc,consume} take the following operands:
   * (dst0) Returned value. For alloc, this is the old value, for consume,
   *        this returns the new value instead. The IR mirrors this.
   * (dst1) UAV resource
   */
  auto opCode = op.getOpToken().getOpCode();

  const auto& dst = op.getDst(0u);
  const auto& resource = op.getDst(1u);

  if (resource.getRegisterType() != RegisterType::eUav)
    return logOpError(op, "Resource must be a UAV.");

  auto descriptor = m_resources.emitUavCounterDescriptorLoad(builder, op, resource);

  if (!descriptor)
    return false;

  /* Emit atomic op */
  auto dstType = ir::ScalarType::eU32;

  auto atomicOp = [opCode] {
    switch (opCode) {
      case OpCode::eImmAtomicAlloc:   return ir::AtomicOp::eInc;
      case OpCode::eImmAtomicConsume: return ir::AtomicOp::eDec;
      default: break;
    }

    dxbc_spv_unreachable();
    return ir::AtomicOp::eLoad;
  } ();

  auto value = builder.add(ir::Op::CounterAtomic(atomicOp, dstType, descriptor));

  value = broadcastScalar(builder, value, dst.getWriteMask());
  return storeDstModified(builder, op, dst, value);
}


bool Converter::handleSample(ir::Builder& builder, const Instruction& op) {
  /* Sample operations have the following basic operand layout:
   * (dst0) Sampled destination value
   * (dst1) Sparse feedback value (for the _cl_s variants)
   * (src0) Texture coordinates and array layer
   * (src1) Texture register with swizzle
   * (src2) Sampler register
   * (src3...) Opcode-specific operands (LOD, bias, etc)
   * (srcMax) LOD clamp (for the _cl_s variants)
   */
  auto opCode = op.getOpToken().getOpCode();

  const auto& dst = op.getDst(0u);
  const auto& address = op.getSrc(0u);
  const auto& texture = op.getSrc(1u);
  const auto& sampler = op.getSrc(2u);

  auto textureInfo = m_resources.emitDescriptorLoad(builder, op, texture);
  auto samplerInfo = m_resources.emitDescriptorLoad(builder, op, sampler);

  if (!textureInfo.descriptor || !samplerInfo.descriptor)
    return false;

  bool hasSparseFeedback = op.getDstCount() == 2u &&
    op.getDst(1u).getRegisterType() != RegisterType::eNull;

  /* Load texture coordinates without the array layer first, then
   * load the layer separately if applicable. */
  auto [coord, layer] = computeTypedCoordLayer(builder, op,
    address, textureInfo.kind, ir::ScalarType::eF32);

  /* Handle immediate offset from the opcode token */
  ir::SsaDef offset = getImmediateTextureOffset(builder, op, textureInfo.kind);

  /* Handle explicit LOD index. */
  ir::SsaDef lodIndex = { };

  if (opCode == OpCode::eSampleClz || opCode == OpCode::eSampleClzS)
    lodIndex = builder.makeConstant(0.0f);
  else if (opCode == OpCode::eSampleL || opCode == OpCode::eSampleLS)
    lodIndex = loadSrcModified(builder, op, op.getSrc(3u), ComponentBit::eX, ir::ScalarType::eF32);

  /* Handle LOD bias for instructions that support it. */
  ir::SsaDef lodBias = { };

  if (opCode == OpCode::eSampleB || opCode == OpCode::eSampleBClampS)
    lodBias = loadSrcModified(builder, op, op.getSrc(3u), ComponentBit::eX, ir::ScalarType::eF32);

  /* Handle derivatives for instructions that provide them */
  ir::SsaDef derivX = { };
  ir::SsaDef derivY = { };

  if (opCode == OpCode::eSampleD || opCode == OpCode::eSampleDClampS) {
    auto coordComponentMask = util::makeWriteMaskForComponents(
      ir::resourceCoordComponentCount(textureInfo.kind));

    derivX = loadSrcModified(builder, op, op.getSrc(3u), coordComponentMask, ir::ScalarType::eF32);
    derivY = loadSrcModified(builder, op, op.getSrc(4u), coordComponentMask, ir::ScalarType::eF32);
  }

  /* Handle optionally provided LOD clamp for implicit LOD instructions.
   * This is an optional operand for sparse feedback instructions. */
  bool hasLodClamp = op.getDstCount() == 2u && !lodIndex &&
    op.getSrc(op.getSrcCount() - 1u).getRegisterType() != RegisterType::eNull;

  ir::SsaDef lodClamp = { };

  if (hasLodClamp) {
    lodClamp = loadSrcModified(builder, op, op.getSrc(op.getSrcCount() - 1u),
      ComponentBit::eX, ir::ScalarType::eF32);
  }

  /* Handle depth reference for depth-compare operations. */
  ir::SsaDef depthRef = { };

  bool isDepthCompare = opCode == OpCode::eSampleC ||
                        opCode == OpCode::eSampleCClampS ||
                        opCode == OpCode::eSampleClz ||
                        opCode == OpCode::eSampleClzS;

  if (isDepthCompare)
    depthRef = loadSrcModified(builder, op, op.getSrc(3u), ComponentBit::eX, ir::ScalarType::eF32);

  /* Determine return type of the sample operation itself. */
  ir::BasicType texelType(textureInfo.type, isDepthCompare ? 1u : 4u);
  ir::Type returnType(texelType);

  if (hasSparseFeedback)
    returnType = makeSparseFeedbackType(texelType);

  /* Set up actual sample op */
  auto sampleOp = ir::Op::ImageSample(returnType, textureInfo.descriptor, samplerInfo.descriptor,
    layer, coord, offset, lodIndex, lodBias, lodClamp, derivX, derivY, depthRef);

  if (hasSparseFeedback)
    sampleOp.setFlags(ir::OpFlag::eSparseFeedback);

  /* Take result apart and write it back to the destination registers */
  auto [feedback, value] = decomposeResourceReturn(builder, builder.add(std::move(sampleOp)));

  if (hasSparseFeedback) {
    if (!storeDst(builder, op, op.getDst(1u), feedback))
      return false;
  }

  return storeDstModified(builder, op, dst, swizzleVector(builder, value, texture.getSwizzle(), dst.getWriteMask()));
}


bool Converter::handleGather(ir::Builder& builder, const Instruction& op) {
  /* Sample operations have the following basic operand layout:
   * (dst0) Sampled destination value
   * (dst1) Sparse feedback value (for the _cl_s variants)
   * (src0) Texture coordinates and array layer
   * (src1) Texture register with swizzle
   * (src2) Sampler register with component selector
   * (src3) Depth reference, if applicable
   * (srcMax) LOD clamp (for the _cl_s variants)
   *
   * The _po variants have an additional offset parameter
   * immediately after texture coordinates.
   */
  auto opCode = op.getOpToken().getOpCode();

  bool hasProgrammableOffsets = opCode == OpCode::eGather4Po ||
                                opCode == OpCode::eGather4PoS ||
                                opCode == OpCode::eGather4PoC ||
                                opCode == OpCode::eGather4PoCS;

  uint32_t srcOperandOffset = hasProgrammableOffsets ? 1u : 0u;

  const auto& dst = op.getDst(0u);
  const auto& address = op.getSrc(0u);
  const auto& texture = op.getSrc(1u + srcOperandOffset);
  const auto& sampler = op.getSrc(2u + srcOperandOffset);

  auto textureInfo = m_resources.emitDescriptorLoad(builder, op, texture);
  auto samplerInfo = m_resources.emitDescriptorLoad(builder, op, sampler);

  if (!textureInfo.descriptor || !samplerInfo.descriptor)
    return false;

  bool hasSparseFeedback = op.getDstCount() == 2u &&
    op.getDst(1u).getRegisterType() != RegisterType::eNull;

  /* Load texture coordinates and array layer */
  auto [coord, layer] = computeTypedCoordLayer(builder, op,
    address, textureInfo.kind, ir::ScalarType::eF32);

  /* Load offset. The variants with programmable offsets cannot have an
   * immediate offset, and only the 6 least significant bits are honored. */
  ir::SsaDef offset = getImmediateTextureOffset(builder, op, textureInfo.kind);

  if (hasProgrammableOffsets) {
    offset = loadSrcModified(builder, op, op.getSrc(1u), ComponentBit::eX | ComponentBit::eY, ir::ScalarType::eI32);
    offset = builder.add(ir::Op::SBitExtract(ir::Type(ir::ScalarType::eI32, 2u),
      offset, builder.makeConstant(0u, 0u), builder.makeConstant(6u, 6u)));
  }

  /* Load depth reference, if applicable */
  bool isDepthCompare = opCode == OpCode::eGather4C ||
                        opCode == OpCode::eGather4CS ||
                        opCode == OpCode::eGather4PoC ||
                        opCode == OpCode::eGather4PoCS;

  ir::SsaDef depthRef = { };

  if (isDepthCompare)
    depthRef = loadSrcModified(builder, op, op.getSrc(3u + srcOperandOffset), ComponentBit::eX, ir::ScalarType::eF32);

  /* Determine return type of the gather operation. */
  ir::BasicType texelType(textureInfo.type, 4u);
  ir::Type returnType(texelType);

  if (hasSparseFeedback)
    returnType = makeSparseFeedbackType(texelType);

  /* Set up actual gather op */
  auto component = sampler.getSwizzle().map(Component::eX);

  auto gatherOp = ir::Op::ImageGather(returnType, textureInfo.descriptor,
    samplerInfo.descriptor, layer, coord, offset, depthRef, uint8_t(component));

  if (hasSparseFeedback)
    gatherOp.setFlags(ir::OpFlag::eSparseFeedback);

  /* Take result apart and write it back to the destination registers */
  auto [feedback, value] = decomposeResourceReturn(builder, builder.add(std::move(gatherOp)));

  if (hasSparseFeedback) {
    if (!storeDst(builder, op, op.getDst(1u), feedback))
      return false;
  }

  return storeDstModified(builder, op, dst, swizzleVector(builder, value, texture.getSwizzle(), dst.getWriteMask()));
}


bool Converter::handleQueryLod(ir::Builder& builder, const Instruction& op) {
  /* lod takes the following operands:
   * (dst0) Destination value (.xy take the clamped/unclamped lod, .zw are 0)
   * (src0) Texture coordinate
   * (src1) Texture register
   * (src2) Sampler register
   */
  const auto& dst = op.getDst(0u);
  const auto& address = op.getSrc(0u);
  const auto& texture = op.getSrc(1u);
  const auto& sampler = op.getSrc(2u);

  /* Load descriptors and resource info */
  auto textureInfo = m_resources.emitDescriptorLoad(builder, op, texture);
  auto samplerInfo = m_resources.emitDescriptorLoad(builder, op, sampler);

  if (!textureInfo.descriptor || !samplerInfo.descriptor)
    return false;

  /* Load coord and compute the actual LOD range */
  auto coord = computeTypedCoordLayer(builder, op, address, textureInfo.kind, ir::ScalarType::eF32).first;

  auto lodRange = builder.add(ir::Op::ImageComputeLod(ir::BasicType(ir::ScalarType::eF32, 2u),
    textureInfo.descriptor, samplerInfo.descriptor, coord));

  util::small_vector<ir::SsaDef, 4u> components;
  components.push_back(builder.add(ir::Op::CompositeExtract(ir::ScalarType::eF32, lodRange, builder.makeConstant(0u))));
  components.push_back(builder.add(ir::Op::CompositeExtract(ir::ScalarType::eF32, lodRange, builder.makeConstant(1u))));
  components.push_back(builder.makeConstant(0.0f));
  components.push_back(builder.makeConstant(0.0f));

  auto vectorType = makeVectorType(ir::ScalarType::eF32, dst.getWriteMask());
  auto vector = composite(builder, vectorType, components.data(), texture.getSwizzle(), dst.getWriteMask());
  return storeDstModified(builder, op, dst, vector);
}


bool Converter::handleCheckSparseAccess(ir::Builder& builder, const Instruction& op) {
  /* check_access_mapped operates on scalar operands.
   * (dst0) Destination value, treated as a boolean
   * (src0) Opaque sparse feedback value
   */
  const auto& dst = op.getDst(0u);
  const auto& src = op.getSrc(0u);

  /* Docs vaguely suggest that this instruction is supposed to be scalar,
   * but docs also forget to mention that this even has a dst parameter.
   * Be conservative and support vector operation anyway. */
  util::small_vector<ir::SsaDef, 4u> scalars = { };

  for (auto c : dst.getWriteMask()) {
    auto value = loadSrcModified(builder, op, src, c, ir::ScalarType::eU32);
    value = builder.add(ir::Op::CheckSparseAccess(value));
    scalars.push_back(boolToInt(builder, value));
  }

  auto vector = buildVector(builder, ir::ScalarType::eU32, scalars.size(), scalars.data());
  return storeDstModified(builder, op, dst, vector);
}


bool Converter::handleBufInfo(ir::Builder& builder, const Instruction& op) {
  /* bufinfo takes the following operands:
   * (dst0) Destination value (can be a vector)
   * (src0) Buffer resource to query
   *
   * For typed and structured buffers, semantics match that of our IR.
   * For byte address buffers, we need to return the byte count rather
   * than the number of elements in the array.
   */
  const auto& dst = op.getDst(0u);
  const auto& resource = op.getSrc(0u);

  /* Load descriptor and get basic resource type properties */
  auto resourceInfo = m_resources.emitDescriptorLoad(builder, op, resource);

  if (!ir::resourceIsBuffer(resourceInfo.kind))
    return logOpError(op, "bufinfo instruction not legal on textures.");

  auto value = builder.add(ir::Op::BufferQuerySize(resourceInfo.descriptor));

  if (resourceInfo.kind == ir::ResourceKind::eBufferRaw)
    value = builder.add(ir::Op::IShl(ir::ScalarType::eU32, value, builder.makeConstant(2u)));

  value = broadcastScalar(builder, value, dst.getWriteMask());
  return storeDstModified(builder, op, dst, value);
}


bool Converter::handleResInfo(ir::Builder& builder, const Instruction& op) {
  /* resinfo takes the following operands:
   * (dst0) Destination value
   * (src0) Mip level to query
   * (src1) Image resource to query
   *
   * The return vector has the format (w, h, d, mips), where mips will
   * always be 1 for UAVs. If the resource is arrayed, the array size
   * will be treated as an extra dimension.
   *
   * The return type modifier (_uint, _float, _rcpfloat) determines the
   * exact return type, with _rcpfloat only applying to the size and not
   * the array layers and mip count.
   *
   * If the mip level is out of bounds, the returned size will be 0,
   * but the total mip level count is still returned appropriately.
   */
  auto returnType = op.getOpToken().getResInfoType();

  const auto& dst = op.getDst(0u);
  const auto& mip = op.getSrc(0u);
  const auto& texture = op.getSrc(1u);

  /* Load descriptor and get basic resource type properties */
  auto textureInfo = m_resources.emitDescriptorLoad(builder, op, texture);

  auto coordDims = ir::resourceDimensions(textureInfo.kind);
  auto isLayered = ir::resourceIsLayered(textureInfo.kind);

  if (ir::resourceIsBuffer(textureInfo.kind))
    return logOpError(op, "resinfo instruction not legal on buffers.");

  /* Query actual mip level count, if applicable */
  bool hasMips = texture.getRegisterType() == RegisterType::eResource &&
    !ir::resourceIsMultisampled(textureInfo.kind);

  auto mipIndex = loadSrcModified(builder, op, mip, ComponentBit::eX, mip.getInfo().type);
  auto mipCount = hasMips
    ? builder.add(ir::Op::ImageQueryMips(ir::ScalarType::eU32, textureInfo.descriptor))
    : builder.makeConstant(1u);

  auto mipLevelInBounds = builder.add(ir::Op::ULt(ir::ScalarType::eBool, mipIndex, mipCount));

  /* Query resource size and layer count */
  auto sizeType = ir::Type()
    .addStructMember(ir::ScalarType::eU32, coordDims) /* size   */
    .addStructMember(ir::ScalarType::eU32);           /* layers */

  auto sizeInfo = builder.add(ir::Op::ImageQuerySize(sizeType,
    textureInfo.descriptor, hasMips ? mipIndex : ir::SsaDef()));

  /* Build actual result vector. Note that rcpfloat is indeed supposed to
   * return infinity in case the resource is unbound or the mip level is
   * out of bounds. */
  std::array<ir::SsaDef, 4u> components = { };
  uint32_t index = 0u;

  for (uint32_t i = 0u; i < coordDims; i++) {
    auto& scalar = components.at(index++);

    scalar = builder.add(ir::Op::CompositeExtract(ir::ScalarType::eU32, sizeInfo,
      coordDims > 1u ? builder.makeConstant(0u, i) : builder.makeConstant(0u)));

    scalar = builder.add(ir::Op::Select(ir::ScalarType::eU32,
      mipLevelInBounds, scalar, builder.makeConstant(0u)));

    if (returnType != ResInfoType::eUint) {
      scalar = builder.add(ir::Op::ConvertItoF(ir::ScalarType::eF32, scalar));

      if (returnType == ResInfoType::eRcpFloat)
        scalar = builder.add(ir::Op::FRcp(ir::ScalarType::eF32, scalar));
    }
  }

  /* If the resource type is arrayed, append the array size. For cubes,
   * we implicitly get the cube count. Note that rcpfloat does not apply. */
  if (isLayered) {
    auto& scalar = components.at(index++);

    scalar = builder.add(ir::Op::CompositeExtract(ir::ScalarType::eU32,
      sizeInfo, builder.makeConstant(1u)));

    scalar = builder.add(ir::Op::Select(ir::ScalarType::eU32,
      mipLevelInBounds, scalar, builder.makeConstant(0u)));

    if (returnType != ResInfoType::eUint)
      scalar = builder.add(ir::Op::ConvertItoF(ir::ScalarType::eF32, scalar));
  }

  /* Append zeroes until we reach the second-to-last field */
  while (index < 3u) {
    components.at(index++) = (returnType == ResInfoType::eUint)
      ? builder.makeConstant(0u)
      : builder.makeConstant(0.0f);
  }

  /* Convert mip count as necessary, rcpfloat does not apply and
   * bound-checking mip levels also does not apply. */
  auto& scalar = components.at(index++);
  scalar = mipCount;

  if (returnType != ResInfoType::eUint) {
    scalar = hasMips
      ? builder.add(ir::Op::ConvertItoF(ir::ScalarType::eF32, scalar))
      : builder.makeConstant(1.0f);
  }

  dxbc_spv_assert(index == 4u);

  /* Build and swizzle vector */
  auto vectorType = (returnType == ResInfoType::eUint)
    ? makeVectorType(ir::ScalarType::eU32, dst.getWriteMask())
    : makeVectorType(ir::ScalarType::eF32, dst.getWriteMask());

  auto vector = composite(builder, vectorType, components.data(), texture.getSwizzle(), dst.getWriteMask());
  return storeDstModified(builder, op, dst, vector);
}


bool Converter::handleSampleInfo(ir::Builder& builder, const Instruction& op) {
  /* sampleinfo takes the following operands:
   * (dst0) Destination value
   * (src0) Resource to query.
   *
   * Like resinfo, it can return the result as either a
   * floating point number or an unsigned integer.
   */
  auto returnType = op.getOpToken().getReturnType();

  const auto& dst = op.getDst(0u);
  const auto& resource = op.getSrc(0u);

  /* Apparently this is one of those instructions that pad the result
   * vector with zeroes before swizzling the result */
  std::array<ir::SsaDef, 4u> scalars = { };

  auto& sampleCount = scalars.at(0u);
  sampleCount = getSampleCount(builder, op, resource);

  if (!sampleCount)
    return false;

  if (returnType == ReturnType::eFloat)
    sampleCount = builder.add(ir::Op::ConvertItoF(ir::ScalarType::eF32, sampleCount));

  for (uint32_t i = 1u; i < scalars.size(); i++) {
    scalars.at(i) = returnType == ReturnType::eFloat
      ? builder.makeConstant(0.0f)
      : builder.makeConstant(0u);
  }

  /* Store result vector */
  auto vectorType = (returnType == ReturnType::eFloat)
    ? makeVectorType(ir::ScalarType::eF32, dst.getWriteMask())
    : makeVectorType(ir::ScalarType::eU32, dst.getWriteMask());

  auto vector = composite(builder, vectorType, scalars.data(), resource.getSwizzle(), dst.getWriteMask());
  return storeDstModified(builder, op, dst, vector);
}


bool Converter::handleSamplePos(ir::Builder& builder, const Instruction& op) {
  /* sampleinfo takes the following operands:
   * (dst0) Destination value
   * (src0) Resource to query
   * (src1) Sample index (as a scalar)
   */
  const auto& dst = op.getDst(0u);
  const auto& resource = op.getSrc(0u);

  /* The return vector is (x, y, 0, 0). If the provided sample index is
   * invalid, this will return a null vector, which we can implicitly
   * achieve by reading the first element of the look-up table. */
  auto sampleCount = getSampleCount(builder, op, resource);
  auto sampleIndex = loadSrcModified(builder, op, op.getSrc(1u), ComponentBit::eX, ir::ScalarType::eU32);

  auto cond = builder.add(ir::Op::ULt(ir::ScalarType::eBool, sampleIndex, sampleCount));
  auto lookupIndex = builder.add(ir::Op::IAdd(ir::ScalarType::eU32, sampleCount, sampleIndex));
  lookupIndex = builder.add(ir::Op::Select(ir::ScalarType::eU32, cond, lookupIndex, builder.makeConstant(0u)));

  auto lookupTable = declareSamplePositionLut(builder);
  auto samplePos = builder.add(ir::Op::ConstantLoad(
    ir::BasicType(ir::ScalarType::eF32, 2u), lookupTable, lookupIndex).setFlags(ir::OpFlag::eInBounds));

  /* Assemble result vector and store */
  std::array<ir::SsaDef, 4u> scalars = { };

  for (uint32_t i = 0u; i < 2u; i++) {
    scalars.at(i) = builder.add(ir::Op::CompositeExtract(
      ir::ScalarType::eF32, samplePos, builder.makeConstant(i)));
  }

  for (uint32_t i = 2u; i < scalars.size(); i++)
    scalars.at(i) = builder.makeConstant(0.0f);

  auto vectorType = makeVectorType(ir::ScalarType::eF32, dst.getWriteMask());
  auto vector = composite(builder, vectorType, scalars.data(), resource.getSwizzle(), dst.getWriteMask());
  return storeDstModified(builder, op, dst, vector);
}


bool Converter::handleIf(ir::Builder& builder, const Instruction& op) {
  auto cond = loadSrcConditional(builder, op, op.getSrc(0u));

  if (!cond)
    return false;

  auto construct = builder.add(ir::Op::ScopedIf(ir::SsaDef(), cond));
  m_controlFlow.push(construct);
  return true;
}


bool Converter::handleElse(ir::Builder& builder, const Instruction& op) {
  auto [construct, type] = m_controlFlow.getConstruct(builder);

  if (type != ir::OpCode::eScopedIf)
    return logOpError(op, "'Else' occurred outside of 'If'.");

  builder.add(ir::Op::ScopedElse(construct));
  return true;
}


bool Converter::handleEndIf(ir::Builder& builder, const Instruction& op) {
  auto [construct, type] = m_controlFlow.getConstruct(builder);

  if (type != ir::OpCode::eScopedIf)
    return logOpError(op, "'EndIf' occurred outside of 'If'.");

  auto constructEnd = builder.add(ir::Op::ScopedEndIf(construct));
  builder.rewriteOp(construct, ir::Op(builder.getOp(construct)).setOperand(0u, constructEnd));

  m_controlFlow.pop();
  return true;
}


bool Converter::handleLoop(ir::Builder& builder) {
  auto construct = builder.add(ir::Op::ScopedLoop(ir::SsaDef()));
  m_controlFlow.push(construct);
  return true;
}


bool Converter::handleEndLoop(ir::Builder& builder, const Instruction& op) {
  auto [construct, type] = m_controlFlow.getConstruct(builder);

  if (type != ir::OpCode::eScopedLoop)
    return logOpError(op, "'EndLoop' occurred outside of 'Loop'.");

  auto constructEnd = builder.add(ir::Op::ScopedEndLoop(construct));
  builder.rewriteOp(construct, ir::Op(builder.getOp(construct)).setOperand(0u, constructEnd));

  m_controlFlow.pop();
  return true;
}


bool Converter::handleSwitch(ir::Builder& builder, const Instruction& op) {
  /* Don't allow min precision here since we need 32-bit literals */
  auto src = op.getSrc(0u);
  auto srcType = determineOperandType(src, ir::ScalarType::eU32, false);

  auto selector = loadSrcModified(builder, op, src, ComponentBit::eX, srcType);
  auto construct = builder.add(ir::Op::ScopedSwitch(ir::SsaDef(), selector));

  m_controlFlow.push(construct);
  return true;
}


bool Converter::handleCase(ir::Builder& builder, const Instruction& op) {
  auto [construct, type] = m_controlFlow.getConstruct(builder);

  if (type != ir::OpCode::eScopedSwitch)
    return logOpError(op, "'Case' occurred outside of 'Switch'.");

  auto literal = op.getSrc(0u).getImmediate<uint32_t>(0u);
  builder.add(ir::Op::ScopedSwitchCase(construct, literal));

  return true;
}


bool Converter::handleDefault(ir::Builder& builder, const Instruction& op) {
  auto [construct, type] = m_controlFlow.getConstruct(builder);

  if (type != ir::OpCode::eScopedSwitch)
    return logOpError(op, "'Default' occurred outside of 'Switch'.");

  builder.add(ir::Op::ScopedSwitchDefault(construct));
  return true;
}


bool Converter::handleEndSwitch(ir::Builder& builder, const Instruction& op) {
  auto [construct, type] = m_controlFlow.getConstruct(builder);

  if (type != ir::OpCode::eScopedSwitch)
    return logOpError(op, "'EndSwitch' occurred outside of 'Switch'.");

  auto constructEnd = builder.add(ir::Op::ScopedEndSwitch(construct));
  builder.rewriteOp(construct, ir::Op(builder.getOp(construct)).setOperand(0u, constructEnd));

  m_controlFlow.pop();
  return true;
}


bool Converter::handleBreak(ir::Builder& builder, const Instruction& op) {
  auto [construct, type] = m_controlFlow.getBreakConstruct(builder);

  if (!construct)
    return logOpError(op, "'Break' occurred outside of 'Loop' or 'Switch'.");

  /* Begin conditional block */
  ir::SsaDef condBlock = { };

  if (op.getOpToken().getOpCode() == OpCode::eBreakc) {
    auto cond = loadSrcConditional(builder, op, op.getSrc(0u));

    if (!cond)
      return false;

    condBlock = builder.add(ir::Op::ScopedIf(ir::SsaDef(), cond));
  }

  /* Insert actual break instruction */
  auto breakOp = (type == ir::OpCode::eScopedLoop)
    ? ir::Op::ScopedLoopBreak(construct)
    : ir::Op::ScopedSwitchBreak(construct);

  builder.add(std::move(breakOp));

  /* End conditional block */
  if (condBlock) {
    auto condEnd = builder.add(ir::Op::ScopedEndIf(condBlock));
    builder.rewriteOp(condBlock, ir::Op(builder.getOp(condBlock)).setOperand(0u, condEnd));
  }

  return true;
}


bool Converter::handleContinue(ir::Builder& builder, const Instruction& op) {
  auto [construct, type] = m_controlFlow.getContinueConstruct(builder);

  if (!construct)
    return logOpError(op, "'Continue' occurred outside of 'Loop'.");

  /* Begin conditional block */
  ir::SsaDef condBlock = { };

  if (op.getOpToken().getOpCode() == OpCode::eContinuec) {
    auto cond = loadSrcConditional(builder, op, op.getSrc(0u));

    if (!cond)
      return false;

    condBlock = builder.add(ir::Op::ScopedIf(ir::SsaDef(), cond));
  }

  /* Insert actual continue instruction */
  builder.add(ir::Op::ScopedLoopContinue(construct));

  /* End conditional block */
  if (condBlock) {
    auto condEnd = builder.add(ir::Op::ScopedEndIf(condBlock));
    builder.rewriteOp(condBlock, ir::Op(builder.getOp(condBlock)).setOperand(0u, condEnd));
  }

  return true;
}


bool Converter::handleRet(ir::Builder& builder, const Instruction& op) {
  auto opCode = op.getOpToken().getOpCode();

  /* Begin conditional block */
  ir::SsaDef condBlock = { };

  if (opCode == OpCode::eRetc) {
    auto cond = loadSrcConditional(builder, op, op.getSrc(0u));

    if (!cond)
      return false;

    condBlock = builder.add(ir::Op::ScopedIf(ir::SsaDef(), cond));
  }

  /* Insert return instruction */
  builder.add(ir::Op::Return());

  /* End conditional block */
  if (condBlock) {
    auto condEnd = builder.add(ir::Op::ScopedEndIf(condBlock));
    builder.rewriteOp(condBlock, ir::Op(builder.getOp(condBlock)).setOperand(0u, condEnd));
  }

  return true;
}


bool Converter::handleCall(ir::Builder& builder, const Instruction& op) {
  auto opCode = op.getOpToken().getOpCode();

  /* Begin conditional block */
  ir::SsaDef condBlock = { };

  if (opCode == OpCode::eCallc) {
    auto cond = loadSrcConditional(builder, op, op.getSrc(0u));

    if (!cond)
      return false;

    condBlock = builder.add(ir::Op::ScopedIf(ir::SsaDef(), cond));
  }

  /* Insert function call instruction */
  auto function = m_regFile.getFunctionForLabel(builder, op, op.getSrc(op.getSrcCount() - 1u));
  builder.add(ir::Op::FunctionCall(ir::ScalarType::eVoid, function));

  /* End conditional block */
  if (condBlock) {
    auto condEnd = builder.add(ir::Op::ScopedEndIf(condBlock));
    builder.rewriteOp(condBlock, ir::Op(builder.getOp(condBlock)).setOperand(0u, condEnd));
  }

  return true;
}


bool Converter::handleDiscard(ir::Builder& builder, const Instruction& op) {
  /* Discard always takes a single operand:
   * (src0) Conditional that decides whether to discard or now.
   */
  auto cond = loadSrcConditional(builder, op, op.getSrc(0u));

  if (!cond)
    return false;

  auto condBlock = builder.add(ir::Op::ScopedIf(ir::SsaDef(), cond));
  builder.add(ir::Op::Demote());

  /* End conditional block */
  auto condEnd = builder.add(ir::Op::ScopedEndIf(condBlock));
  builder.rewriteOp(condBlock, ir::Op(builder.getOp(condBlock)).setOperand(0u, condEnd));

  return true;
}


bool Converter::handleGsEmitCut(ir::Builder& builder, const Instruction& op) {
  /* The Stream* variants of these instructions take a stream register
   * as a destination operand. */
  auto opCode = op.getOpToken().getOpCode();

  uint32_t streamIndex = 0u;

  if (opCode == OpCode::eCutStream ||
      opCode == OpCode::eEmitStream ||
      opCode == OpCode::eEmitThenCutStream) {
    const auto& mreg = op.getDst(0u);

    if (mreg.getRegisterType() != RegisterType::eStream) {
      logOpError(op, "Invalid stream operand.");
      return false;
    }

    streamIndex = mreg.getIndex(0u);
  }

  bool emitVertex = opCode == OpCode::eEmit ||
                    opCode == OpCode::eEmitStream ||
                    opCode == OpCode::eEmitThenCut ||
                    opCode == OpCode::eEmitThenCutStream;

  bool emitPrimitive = opCode == OpCode::eCut ||
                       opCode == OpCode::eCutStream ||
                       opCode == OpCode::eEmitThenCut ||
                       opCode == OpCode::eEmitThenCutStream;

  if (emitVertex && !m_ioMap.handleEmitVertex(builder, streamIndex)) {
    logOpError(op, "Failed to copy output registers.");
    return false;
  }

  if (emitVertex)
    builder.add(ir::Op::EmitVertex(streamIndex));

  if (emitPrimitive)
    builder.add(ir::Op::EmitPrimitive(streamIndex));

  return true;
}


bool Converter::handleSync(ir::Builder& builder, const Instruction& op) {
  auto syncFlags = op.getOpToken().getSyncFlags();

  /* Translate sync flags to memory scopes directly, we can
   * clean up unnecessary barrier flags in a dedicated pass. */
  auto execScope = ir::Scope::eThread;
  auto memScope = ir::Scope::eThread;
  auto memTypes = ir::MemoryTypeFlags();

  if (syncFlags & SyncFlag::eWorkgroupThreads)
    execScope = ir::Scope::eWorkgroup;

  if (syncFlags & SyncFlag::eWorkgroupMemory) {
    memScope = ir::Scope::eWorkgroup;
    memTypes |= ir::MemoryType::eLds;
  }

  if (syncFlags & SyncFlag::eUavMemoryLocal) {
    memScope = ir::Scope::eWorkgroup;
    memTypes |= ir::MemoryType::eUav;
  }

  if (syncFlags & SyncFlag::eUavMemoryGlobal) {
    memScope = ir::Scope::eGlobal;
    memTypes |= ir::MemoryType::eUav;
  }

  if (execScope != ir::Scope::eThread || memScope != ir::Scope::eThread)
    builder.add(ir::Op::Barrier(execScope, memScope, memTypes));

  return true;
}


bool Converter::handleLabel(ir::Builder& builder, const Instruction& op) {
  /* We (probably) already declared the function in question, just need
   * to start inserting instructions at the correct location. */
  auto function = m_regFile.getFunctionForLabel(builder, op, op.getDst(0u));

  if (!function)
    return false;

  builder.setCursor(function);
  return true;
}


void Converter::applyNonUniform(ir::Builder& builder, ir::SsaDef def) {
  /* Not sure which operands FXC may decorate with nonuniform. On resource
   * operands we handle it appropriately, but in case it appears on a regular
   * operand, decorate its instruction as nonuniform and propagate later. */
  if (!(builder.getOp(def).getFlags() & ir::OpFlag::eNonUniform)) {
    auto op = builder.getOp(def);
    op.setFlags(op.getFlags() | ir::OpFlag::eNonUniform);
    builder.rewriteOp(def, std::move(op));
  }
}


ir::SsaDef Converter::applySrcModifiers(ir::Builder& builder, ir::SsaDef def, const Instruction& instruction, const Operand& operand) {
  auto mod = operand.getModifiers();

  if (mod.isNonUniform()) {
    /* We already apply nonuniform modifiers to descriptor loads for CBV */
    if (operand.getRegisterType() != RegisterType::eCbv)
      applyNonUniform(builder, def);
  }

  if (mod.isAbsolute() || mod.isNegated()) {
    /* Ensure the operand has a type we can work with, and assume float32
     * if it is typeless, which is common on move instructions. */
    const auto& op = builder.getOp(def);
    auto type = op.getType().getBaseType(0u);

    bool isUnknown = type.isUnknownType();

    if (type.isNumericType()) {
      ir::OpFlags flags = 0u;

      if (instruction.getOpToken().getPreciseMask())
        flags |= ir::OpFlag::ePrecise;

      if (isUnknown) {
        type = ir::BasicType(ir::ScalarType::eF32, type.getVectorSize());
        def = builder.add(ir::Op::ConsumeAs(type, def));
      }

      if (mod.isAbsolute()) {
        def = builder.add(type.isFloatType()
          ? ir::Op::FAbs(type, def).setFlags(flags)
          : ir::Op::IAbs(type, def));
      }

      if (mod.isNegated()) {
        def = builder.add(type.isFloatType()
          ? ir::Op::FNeg(type, def).setFlags(flags)
          : ir::Op::INeg(type, def));
      }

      if (isUnknown) {
        type = ir::BasicType(ir::ScalarType::eUnknown, type.getVectorSize());
        def = builder.add(ir::Op::ConsumeAs(type, def));
      }
    }
  }

  return def;
}


ir::SsaDef Converter::applyDstModifiers(ir::Builder& builder, ir::SsaDef def, const Instruction& instruction, const Operand& operand) {
  auto mod = operand.getModifiers();

  if (mod.isNonUniform())
    applyNonUniform(builder, def);

  /* We should only ever call this on arithmetic instructions,
   * so this flag shouldn't conflict with anything else */
  auto opToken = instruction.getOpToken();

  if (opToken.isSaturated()) {
    auto type = builder.getOp(def).getType().getBaseType(0u);

    if (type.isUnknownType()) {
      auto scalarType = determineOperandType(operand, ir::ScalarType::eF32);
      type = makeVectorType(scalarType, operand.getWriteMask());
    }

    if (type.isFloatType()) {
      ir::OpFlags flags = 0u;

      if (instruction.getOpToken().getPreciseMask())
        flags |= ir::OpFlag::ePrecise;

      def = builder.add(ir::Op::FClamp(type, def,
        makeTypedConstant(builder, type, 0.0f),
        makeTypedConstant(builder, type, 1.0f)).setFlags(flags));
    } else {
      logOpMessage(LogLevel::eWarn, instruction, "Saturation applied to a non-float result.");
    }
  }

  return def;
}


ir::SsaDef Converter::loadImmediate(ir::Builder& builder, const Operand& operand, WriteMask mask, ir::ScalarType type) {
  ir::Op result(ir::OpCode::eConstant, makeVectorType(type, mask));

  /* Compress 64-bit component mask so we only load two components */
  if (operand.getRegisterType() == RegisterType::eImm64) {
    dxbc_spv_assert(isValid64BitMask(mask));
    mask &= ComponentBit::eX | ComponentBit::eZ;

    if (mask & ComponentBit::eZ) {
      mask |= ComponentBit::eY;
      mask -= ComponentBit::eZ;
    }
  }

  for (auto c : mask) {
    auto index = operand.getComponentCount() == ComponentCount::e4Component
      ? uint8_t(componentFromBit(c))
      : uint8_t(0u);

    /* Preserve bit pattern */
    auto value = operand.getRegisterType() == RegisterType::eImm32
      ? ir::Operand(operand.getImmediate<uint32_t>(index))
      : ir::Operand(operand.getImmediate<uint64_t>(index));

    result.addOperand(value);
  }

  return builder.add(std::move(result));
}


ir::SsaDef Converter::loadIcb(ir::Builder& builder, const Instruction& op, const Operand& operand, WriteMask mask, ir::ScalarType type) {
  util::small_vector<ir::SsaDef, 4u> scalars;

  auto index = loadOperandIndex(builder, op, operand, 0u);

  /* Load icb descriptor in case it is lowered */
  ir::SsaDef icbDescriptor = { };

  if (m_options.lowerIcb) {
    icbDescriptor = builder.add(ir::Op::DescriptorLoad(
      ir::ScalarType::eCbv, m_icb, builder.makeConstant(0u)));
  }

  if (builder.getOp(m_icb).isConstant()) {
    /* Don't bother deduplicating loads here, this is a private array anyway */
    for (auto c : mask) {
      uint32_t componentIndex = uint8_t(operand.getSwizzle().map(c));

      auto address = builder.add(ir::Op::CompositeConstruct(
        ir::BasicType(ir::ScalarType::eU32, 2u), index, builder.makeConstant(componentIndex)));

      auto& scalar = scalars.emplace_back();
      scalar = builder.add(ir::Op::ConstantLoad(ir::ScalarType::eUnknown, m_icb, address));
      scalar = builder.add(ir::Op::ConsumeAs(type, scalar));
    }
  } else {
    /* Read entire vec4 and extract used components */
    ir::BasicType unknownType(ir::ScalarType::eUnknown, 4u);

    auto vector = builder.add(ir::Op::BufferLoad(unknownType, icbDescriptor, index, 4u));

    for (auto c : mask) {
      uint32_t componentIndex = uint8_t(operand.getSwizzle().map(c));

      auto& scalar = scalars.emplace_back();
      scalar = builder.add(ir::Op::CompositeExtract(ir::ScalarType::eUnknown, vector, builder.makeConstant(componentIndex)));
      scalar = builder.add(ir::Op::ConsumeAs(type, scalar));
    }
  }

  return buildVector(builder, type, scalars.size(), scalars.data());
}


ir::SsaDef Converter::loadPhaseInstanceId(ir::Builder& builder, WriteMask mask, ir::ScalarType type) {
  auto def = builder.add(ir::Op::ParamLoad(ir::ScalarType::eU32, m_hs.phaseFunction, m_hs.phaseInstanceId));

  if (type != ir::ScalarType::eU32)
    def = builder.add(ir::Op::ConsumeAs(type, def));

  return broadcastScalar(builder, def, mask);
}


ir::SsaDef Converter::loadSrc(ir::Builder& builder, const Instruction& op, const Operand& operand, WriteMask mask, ir::ScalarType type) {
  /* Load non-constant 64-bit operands as scalar 32-bits and promote later */
  auto loadType = type;
  auto loadDef = ir::SsaDef();

  if (is64BitType(type) && operand.getRegisterType() != RegisterType::eImm64)
    loadType = ir::ScalarType::eU32;

  if (is64BitType(type) && !isValid64BitMask(mask)) {
    logOpError(op, "Invalid 64-bit component read mask: ", mask);
    return ir::SsaDef();
  }

  if (type == ir::ScalarType::eBool)
    loadType = ir::ScalarType::eU32;

  switch (operand.getRegisterType()) {
    case RegisterType::eNull:
      return ir::SsaDef();

    case RegisterType::eImm32:
    case RegisterType::eImm64:
      loadDef = loadImmediate(builder, operand, mask, loadType);
      break;

    case RegisterType::eTemp:
    case RegisterType::eIndexableTemp:
      loadDef = m_regFile.emitLoad(builder, op, operand, mask, loadType);
      break;

    case RegisterType::eForkInstanceId:
    case RegisterType::eJoinInstanceId:
      loadDef = loadPhaseInstanceId(builder, mask, loadType);
      break;

    case RegisterType::eCbv:
      loadDef = m_resources.emitConstantBufferLoad(builder, op, operand, mask, loadType);
      break;

    case RegisterType::eIcb:
      loadDef = loadIcb(builder, op, operand, mask, loadType);
      break;

    case RegisterType::eInput:
    case RegisterType::eOutput:
    case RegisterType::ePrimitiveId:
    case RegisterType::eDepth:
    case RegisterType::eDepthGe:
    case RegisterType::eDepthLe:
    case RegisterType::eCoverageIn:
    case RegisterType::eCoverageOut:
    case RegisterType::eControlPointId:
    case RegisterType::eControlPointIn:
    case RegisterType::eControlPointOut:
    case RegisterType::ePatchConstant:
    case RegisterType::eTessCoord:
    case RegisterType::eThreadId:
    case RegisterType::eThreadGroupId:
    case RegisterType::eThreadIdInGroup:
    case RegisterType::eThreadIndexInGroup:
    case RegisterType::eGsInstanceId:
    case RegisterType::eCycleCounter:
    case RegisterType::eStencilRef:
    case RegisterType::eInnerCoverage:
      loadDef = m_ioMap.emitLoad(builder, op, operand, mask, loadType);
      break;

    case RegisterType::eThis:
      loadDef = m_regFile.emitThisLoad(builder, op, operand, mask, loadType);
      break;

    default:
      break;
  }

  if (!loadDef) {
    auto name = makeRegisterDebugName(operand.getRegisterType(), 0u, WriteMask());
    logOpError(op, "Failed to load operand: ", name);
    return loadDef;
  }

  /* Resolve boolean and 64-bit types */
  if (type == ir::ScalarType::eBool)
    loadDef = intToBool(builder, loadDef);
  else if (type != loadType)
    loadDef = builder.add(ir::Op::Cast(makeVectorType(type, mask), loadDef));

  return loadDef;
}


ir::SsaDef Converter::loadSrcModified(ir::Builder& builder, const Instruction& op, const Operand& operand, WriteMask mask, ir::ScalarType type) {
  auto value = loadSrc(builder, op, operand, mask, type);
  return applySrcModifiers(builder, value, op, operand);
}


ir::SsaDef Converter::loadSrcConditional(ir::Builder& builder, const Instruction& op, const Operand& operand) {
  /* Load source as boolean operand and invert if necessary,
   * we can clean this up in an optimization pass later. */
  auto value = loadSrc(builder, op, operand, ComponentBit::eX, ir::ScalarType::eBool);

  if (!value)
    return ir::SsaDef();

  if (op.getOpToken().getZeroTest() == TestBoolean::eZero)
    value = builder.add(ir::Op::BNot(ir::ScalarType::eBool, value));

  return value;
}


ir::SsaDef Converter::loadSrcBitCount(ir::Builder& builder, const Instruction& op, const Operand& operand, WriteMask mask) {
  auto scalarType = determineOperandType(operand, ir::ScalarType::eU32);
  auto vectorType = makeVectorType(scalarType, mask);

  auto value = loadSrcModified(builder, op, operand, mask, scalarType);
  return builder.add(ir::Op::IAnd(vectorType, value, makeTypedConstant(builder, vectorType, BitCountMask)));
}


ir::SsaDef Converter::loadOperandIndex(ir::Builder& builder, const Instruction& op, const Operand& operand, uint32_t dim) {
  dxbc_spv_assert(dim < operand.getIndexDimensions());

  auto indexType = operand.getIndexType(dim);

  if (!hasRelativeIndexing(indexType))
    return builder.makeConstant(operand.getIndex(dim));

  /* Recursively load relative index */
  ir::SsaDef index = loadSrcModified(builder, op,
    op.getRawOperand(operand.getIndexOperand(dim)),
    ComponentBit::eX, ir::ScalarType::eU32);

  if (!hasAbsoluteIndexing(indexType))
    return index;

  auto base = operand.getIndex(dim);

  if (!base)
    return index;

  return builder.add(ir::Op::IAdd(ir::ScalarType::eU32, builder.makeConstant(base), index));
}


bool Converter::storeDst(ir::Builder& builder, const Instruction& op, const Operand& operand, ir::SsaDef value) {
  const auto& valueOp = builder.getOp(value);
  auto writeMask = operand.getWriteMask();

  if (is64BitType(valueOp.getType().getBaseType(0u))) {
    /* If the incoming operand is a 64-bit type, cast it to a 32-bit
     * vector before handing it off to the underlying register load/store. */
    auto storeType = makeVectorType(ir::ScalarType::eU32, writeMask);

    if (!isValid64BitMask(writeMask))
      return logOpError(op, "Invalid 64-bit component write mask: ", writeMask);

    value = builder.add(ir::Op::Cast(storeType, value));
  } else if (valueOp.getType().getBaseType(0u).isBoolType()) {
    /* Convert boolean results to the DXBC representation of -1. */
    value = boolToInt(builder, value);
  }

  switch (operand.getRegisterType()) {
    case RegisterType::eNull:
      return true;

    case RegisterType::eTemp:
    case RegisterType::eIndexableTemp:
      return m_regFile.emitStore(builder, op, operand, value);

    case RegisterType::eOutput:
    case RegisterType::eDepth:
    case RegisterType::eDepthLe:
    case RegisterType::eDepthGe:
    case RegisterType::eCoverageOut:
    case RegisterType::eStencilRef:
      return m_ioMap.emitStore(builder, op, operand, value);

    default: {
      auto name = makeRegisterDebugName(operand.getRegisterType(), 0u, writeMask);
      logOpError(op, "Unhandled destination operand: ", name);
    } return false;
  }
}


bool Converter::storeDstModified(ir::Builder& builder, const Instruction& op, const Operand& operand, ir::SsaDef value) {
  value = applyDstModifiers(builder, value, op, operand);
  return storeDst(builder, op, operand, value);
}


ir::SsaDef Converter::computeRawAddress(ir::Builder& builder, ir::SsaDef byteAddress, WriteMask componentMask) {
  const auto& offsetOp = builder.getOp(byteAddress);

  /* Explicit u32 for use as a constant */
  uint32_t componentIndex = uint8_t(componentFromBit(componentMask.first()));

  if (offsetOp.isConstant()) {
    /* If we know the struct offset is constant, just emit another constant */
    uint32_t dwordOffset = (uint32_t(offsetOp.getOperand(0u)) / sizeof(uint32_t)) + componentIndex;
    return builder.makeConstant(dwordOffset);
  } else {
    /* Otherwise, dynamically adjust the offset to be a dword index */
    auto result = builder.add(ir::Op::UShr(ir::ScalarType::eU32,
      byteAddress, builder.makeConstant(2u)));

    if (componentIndex) {
      result = builder.add(ir::Op::IAdd(ir::ScalarType::eU32, result,
        builder.makeConstant(componentIndex)));
    }

    return result;
  }
}


ir::SsaDef Converter::computeStructuredAddress(ir::Builder& builder, ir::SsaDef elementIndex, ir::SsaDef elementOffset, WriteMask componentMask) {
  auto type = ir::BasicType(ir::ScalarType::eU32, 2u);
  elementOffset = computeRawAddress(builder, elementOffset, componentMask);

  return builder.add(ir::Op::CompositeConstruct(type, elementIndex, elementOffset));
}


ir::SsaDef Converter::computeAtomicBufferAddress(ir::Builder& builder, const Instruction& op, const Operand& operand, ir::ResourceKind kind) {
  ir::SsaDef elementIndex = loadSrcModified(builder, op, operand, ComponentBit::eX, ir::ScalarType::eU32);

  if (kind == ir::ResourceKind::eBufferRaw)
    return computeRawAddress(builder, elementIndex, ComponentBit::eX);

  ir::SsaDef elementOffset = loadSrcModified(builder, op, operand, ComponentBit::eY, ir::ScalarType::eU32);
  return computeStructuredAddress(builder, elementIndex, elementOffset, ComponentBit::eX);
}


ir::SsaDef Converter::getSampleCount(ir::Builder& builder, const Instruction& op, const Operand& operand) {
  if (operand.getRegisterType() == RegisterType::eRasterizer) {
    return builder.add(ir::Op::InputLoad(ir::ScalarType::eU32,
      declareRasterizerSampleCount(builder), ir::SsaDef()));
  } else {
    /* We can might actually get a non-multisampled resource declaration
     * here, just return a sample count of 1 in that case. */
    auto textureInfo = m_resources.emitDescriptorLoad(builder, op, operand);

    if (ir::resourceIsBuffer(textureInfo.kind)) {
      logOpError(op, "Cannot query sample count of a buffer resource.");
      return ir::SsaDef();
    }

    if (!ir::resourceIsMultisampled(textureInfo.kind))
      return builder.makeConstant(1u);

    return builder.add(ir::Op::ImageQuerySamples(
      ir::ScalarType::eU32, textureInfo.descriptor));
  }
}


std::pair<ir::SsaDef, ir::SsaDef> Converter::computeTypedCoordLayer(ir::Builder& builder, const Instruction& op,
    const Operand& operand, ir::ResourceKind kind, ir::ScalarType type) {
  auto coordComponentCount = ir::resourceCoordComponentCount(kind);
  auto coordComponentMask = util::makeWriteMaskForComponents(coordComponentCount);

  ir::SsaDef coord = loadSrcModified(builder, op, operand, coordComponentMask, type);
  ir::SsaDef layer = { };

  if (ir::resourceIsLayered(kind)) {
    auto layerComponentMask = componentBit(Component(coordComponentCount));
    layer = loadSrcModified(builder, op, operand, layerComponentMask, type);
  }

  return std::make_pair(coord, layer);
}


ir::SsaDef Converter::boolToInt(ir::Builder& builder, ir::SsaDef def) {
  auto srcType = builder.getOp(def).getType().getBaseType(0u);
  dxbc_spv_assert(srcType.isBoolType());

  auto dstType = ir::BasicType(ir::ScalarType::eU32, srcType.getVectorSize());

  return builder.add(ir::Op::Select(dstType, def,
    makeTypedConstant(builder, dstType, -1),
    makeTypedConstant(builder, dstType,  0)));
}


ir::SsaDef Converter::intToBool(ir::Builder& builder, ir::SsaDef def) {
  auto srcType = builder.getOp(def).getType().getBaseType(0u);

  if (!srcType.isIntType()) {
    srcType = ir::BasicType(ir::ScalarType::eU32, srcType.getVectorSize());
    def = builder.add(ir::Op::ConsumeAs(srcType, def));
  }

  auto dstType = ir::BasicType(ir::ScalarType::eBool, srcType.getVectorSize());
  return builder.add(ir::Op::INe(dstType, def, makeTypedConstant(builder, srcType, 0u)));
}


ir::SsaDef Converter::getImmediateTextureOffset(ir::Builder& builder, const Instruction& op, ir::ResourceKind kind) {
  auto sampleControls = op.getOpToken().getSampleControlToken();

  if (!sampleControls)
    return ir::SsaDef();

  if (kind == ir::ResourceKind::eImageCube || kind == ir::ResourceKind::eImageCubeArray) {
    logOpMessage(LogLevel::eWarn, op, "Cube textures cannot support immediate offsets.");
    return ir::SsaDef();
  }

  auto componentCount = resourceCoordComponentCount(kind);

  if (componentCount < 1u || componentCount > 3u) {
    logOpMessage(LogLevel::eWarn, op, "Invalid resource kind for immediate offsets: ", kind);
    return ir::SsaDef();
  }

  ir::Op constant(ir::OpCode::eConstant, ir::BasicType(ir::ScalarType::eI32, componentCount));

  if (componentCount >= 1u)
    constant.addOperand(sampleControls.u());

  if (componentCount >= 2u)
    constant.addOperand(sampleControls.v());

  if (componentCount >= 3u)
    constant.addOperand(sampleControls.w());

  return builder.add(std::move(constant));
}


ir::SsaDef Converter::broadcastScalar(ir::Builder& builder, ir::SsaDef def, WriteMask mask) {
  if (mask == mask.first())
    return def;

  /* Determine vector type */
  auto type = builder.getOp(def).getType().getBaseType(0u);
  dxbc_spv_assert(type.isScalar());

  type = makeVectorType(type.getBaseType(), mask);

  if (type.isScalar())
    return def;

  /* Create vector */
  ir::Op op(ir::OpCode::eCompositeConstruct, type);

  for (uint32_t i = 0u; i < type.getVectorSize(); i++)
    op.addOperand(def);

  return builder.add(std::move(op));
}


ir::SsaDef Converter::swizzleVector(ir::Builder& builder, ir::SsaDef value, Swizzle swizzle, WriteMask writeMask) {
  const auto& valueOp = builder.getOp(value);
  dxbc_spv_assert(valueOp.getType().isBasicType());

  auto type = valueOp.getType().getBaseType(0u);

  if (type.isScalar())
    return broadcastScalar(builder, value, writeMask);

  /* Extract components one by one and then re-assemble vector */
  util::small_vector<ir::SsaDef, 4u> components;

  for (auto c : writeMask) {
    uint32_t componentIndex = uint8_t(swizzle.map(c));

    components.push_back(builder.add(ir::Op::CompositeExtract(type.getBaseType(),
      value, builder.makeConstant(componentIndex))));
  }

  return buildVector(builder, type.getBaseType(), components.size(), components.data());
}


std::pair<ir::SsaDef, ir::SsaDef> Converter::decomposeResourceReturn(ir::Builder& builder, ir::SsaDef value) {
  /* Sparse feedback first if applicable, then the value to match the type */
  const auto& valueOp = builder.getOp(value);

  if (!valueOp.getType().isStructType())
    return std::make_pair(ir::SsaDef(), valueOp.getDef());

  return std::make_pair(
    builder.add(ir::Op::CompositeExtract(valueOp.getType().getSubType(0u), value, builder.makeConstant(0u))),
    builder.add(ir::Op::CompositeExtract(valueOp.getType().getSubType(1u), value, builder.makeConstant(1u))));
}


ir::ScalarType Converter::determineOperandType(const Operand& operand, ir::ScalarType fallback, bool allowMinPrecision) const {
  /* Use base type from the instruction layout */
  auto type = operand.getInfo().type;

  /* If the operand is decorated as min precision, apply
   * it to the operand type if possible. */
  auto precision = operand.getModifiers().getPrecision();

  switch (precision) {
    case MinPrecision::eNone:
      break;

    /* Lower MinF10 to MinF16 directly since we don't really support Min10. */
    case MinPrecision::eMin10Float:
    case MinPrecision::eMin16Float: {
      if (type == ir::ScalarType::eF32 || type == ir::ScalarType::eUnknown)
        return allowMinPrecision ? ir::ScalarType::eMinF16 : ir::ScalarType::eF32;
    } break;

    case MinPrecision::eMin16Uint: {
      if (type == ir::ScalarType::eI32 || type == ir::ScalarType::eU32 || type == ir::ScalarType::eUnknown)
        return allowMinPrecision ? ir::ScalarType::eMinU16 : ir::ScalarType::eU32;
    } break;

    case MinPrecision::eMin16Sint: {
      if (type == ir::ScalarType::eI32 || type == ir::ScalarType::eU32 || type == ir::ScalarType::eUnknown)
        return allowMinPrecision ? ir::ScalarType::eMinI16 : ir::ScalarType::eI32;
    } break;
  }

  /* Use fallback type if we couldn't resolve the type. */
  if (type == ir::ScalarType::eUnknown)
    type = fallback;

  return type;
}


ir::SsaDef Converter::composite(ir::Builder& builder, ir::BasicType type,
  const ir::SsaDef* components, Swizzle swizzle, WriteMask mask) {
  /* Apply swizzle and mask and get components in the right order. */
  std::array<ir::SsaDef, 4u> scalars = { };

  uint32_t index = 0u;

  for (auto c : mask) {
    auto scalar = components[uint8_t(swizzle.map(c))];
    scalars[index++] = scalar;

    dxbc_spv_assert(scalar);
  }

  /* Component count must match, or be exactly 1 so that
   * we can broadcast a single component. */
  dxbc_spv_assert(index == type.getVectorSize() || index == 1u);

  if (type.isScalar())
    return scalars.at(0u);

  /* Build actual composite op */
  ir::Op op(ir::OpCode::eCompositeConstruct, type);

  for (uint32_t i = 0u; i < type.getVectorSize(); i++)
    op.addOperand(scalars.at(std::min(i, index - 1u)));

  return builder.add(std::move(op));
}


ir::SsaDef Converter::buildVector(ir::Builder& builder, ir::ScalarType scalarType, size_t count, const ir::SsaDef* scalars) {
  if (!count)
    return ir::SsaDef();

  if (count == 1u)
    return scalars[0u];

  ir::BasicType type(scalarType, count);

  ir::Op op(ir::OpCode::eCompositeConstruct, type);

  for (uint32_t i = 0u; i < type.getVectorSize(); i++)
    op.addOperand(scalars[i]);

  return builder.add(std::move(op));
}


ir::SsaDef Converter::extractFromVector(ir::Builder& builder, ir::SsaDef def, uint32_t component) {
  const auto& op = builder.getOp(def);

  if (op.getType().isScalarType())
    return def;

  if (op.getOpCode() == ir::OpCode::eCompositeConstruct)
    return ir::SsaDef(op.getOperand(component));

  return builder.add(ir::Op::CompositeExtract(op.getType().getSubType(component), def, builder.makeConstant(component)));
}


template<typename T>
ir::SsaDef Converter::makeTypedConstant(ir::Builder& builder, ir::BasicType type, T value) {
  ir::Op op(ir::OpCode::eConstant, type);

  ir::Operand scalar = [type, value] {
    switch (type.getBaseType()) {
      case ir::ScalarType::eBool: return ir::Operand(bool(value));
      case ir::ScalarType::eU8:   return ir::Operand(uint8_t(value));
      case ir::ScalarType::eU16:  return ir::Operand(uint16_t(value));
      case ir::ScalarType::eMinU16:
      case ir::ScalarType::eU32:  return ir::Operand(uint32_t(value));
      case ir::ScalarType::eU64:  return ir::Operand(uint64_t(value));
      case ir::ScalarType::eI8:   return ir::Operand(int8_t(value));
      case ir::ScalarType::eI16:  return ir::Operand(int16_t(value));
      case ir::ScalarType::eMinI16:
      case ir::ScalarType::eI32:  return ir::Operand(int32_t(value));
      case ir::ScalarType::eI64:  return ir::Operand(int64_t(value));
      case ir::ScalarType::eF16:  return ir::Operand(util::float16_t(value));
      case ir::ScalarType::eMinF16:
      case ir::ScalarType::eF32:  return ir::Operand(float(value));
      case ir::ScalarType::eF64:  return ir::Operand(double(value));
      default: break;
    }

    dxbc_spv_unreachable();
    return ir::Operand();
  } ();

  for (uint32_t i = 0u; i < type.getVectorSize(); i++)
    op.addOperand(scalar);

  return builder.add(std::move(op));
}


ir::SsaDef Converter::declareRasterizerSampleCount(ir::Builder& builder) {
  if (m_ps.sampleCount)
    return m_ps.sampleCount;

  m_ps.sampleCount = builder.add(ir::Op::DclInputBuiltIn(
    ir::ScalarType::eU32, getEntryPoint(), ir::BuiltIn::eSampleCount, ir::InterpolationMode::eFlat));

  if (m_options.includeDebugNames)
    builder.add(ir::Op::DebugName(m_ps.sampleCount, "vRasterizer"));

  return m_ps.sampleCount;
}


ir::SsaDef Converter::declareSamplePositionLut(ir::Builder& builder) {
  static const std::array<std::pair<float, float>, 32> s_samplePositions = {{
    /* Unbound resource */
    { 0.0f, 0.0f },
    /* 1 samples */
    { 0.0f, 0.0f },
    /* 2 samples */
    {  0.25f,  0.25f },
    { -0.25f, -0.25f },
    /* 4 samples */
    { -0.125f, -0.375f },
    {  0.375f, -0.125f },
    { -0.375f,  0.125f },
    {  0.125f,  0.375f },
    /* 8 samples */
    {  0.0625f, -0.1875f },
    { -0.0625f,  0.1875f },
    {  0.3125f,  0.0625f },
    { -0.1875f, -0.3125f },
    { -0.3125f,  0.3125f },
    { -0.4375f, -0.0625f },
    {  0.1875f,  0.4375f },
    {  0.4375f, -0.4375f },
    /* 16 samples */
    {  0.0625f,  0.0625f },
    { -0.0625f, -0.1875f },
    { -0.1875f,  0.1250f },
    {  0.2500f, -0.0625f },
    { -0.3125f, -0.1250f },
    {  0.1250f,  0.3125f },
    {  0.3125f,  0.1875f },
    {  0.1875f, -0.3125f },
    { -0.1250f,  0.3750f },
    {  0.0000f, -0.4375f },
    { -0.2500f, -0.3750f },
    { -0.3750f,  0.2500f },
    { -0.5000f,  0.0000f },
    {  0.4375f, -0.2500f },
    {  0.3750f,  0.4375f },
    { -0.4375f, -0.5000f },
  }};

  if (m_ps.samplePosArray)
    return m_ps.samplePosArray;

  auto type = ir::Type(ir::ScalarType::eF32, 2u).addArrayDimension(s_samplePositions.size());
  auto constant = ir::Op(ir::OpCode::eConstant, type);

  for (const auto& e : s_samplePositions)
    constant.addOperands(e.first, e.second);

  m_ps.samplePosArray = builder.add(std::move(constant));

  if (m_options.includeDebugNames)
    builder.add(ir::Op::DebugName(m_ps.samplePosArray, "samplePos"));

  return m_ps.samplePosArray;
}


std::string Converter::makeRegisterDebugName(RegisterType type, uint32_t index, WriteMask mask) const {
  auto stage = m_parser.getShaderInfo().getType();

  std::stringstream name;

  switch (type) {
    case RegisterType::eTemp:               name << "r" << index << (mask ? "_" : "") << mask; break;
    case RegisterType::eInput:
    case RegisterType::eControlPointIn:     name << "v" << index << (mask ? "_" : "") << mask; break;
    case RegisterType::eOutput:
    case RegisterType::eControlPointOut:    name << "o" << index << (mask ? "_" : "") << mask; break;
    case RegisterType::eIndexableTemp:      name << "x" << index << (mask ? "_" : "") << mask; break;
    case RegisterType::eSampler:            name << (isSm51() ? "S" : "s") << index; break;
    case RegisterType::eResource:           name << (isSm51() ? "T" : "t") << index; break;
    case RegisterType::eCbv:                name << (isSm51() ? "CB" : "cb") << index; break;
    case RegisterType::eIcb:                name << "icb"; break;
    case RegisterType::eLabel:              name << "l" << index; break;
    case RegisterType::ePrimitiveId:        name << "vPrim"; break;
    case RegisterType::eDepth:              name << "oDepth"; break;
    case RegisterType::eRasterizer:         name << "vRasterizer"; break;
    case RegisterType::eCoverageOut:        name << "oCoverage"; break;
    case RegisterType::eFunctionBody:       name << "fb" << index; break;
    case RegisterType::eFunctionTable:      name << "ft" << index; break;
    case RegisterType::eInterface:          name << "fp" << index; break;
    case RegisterType::eFunctionInput:      name << "fi" << index; break;
    case RegisterType::eFunctionOutput:     name << "fo" << index; break;
    case RegisterType::eControlPointId:     name << "vControlPoint"; break;
    case RegisterType::eForkInstanceId:     name << "vForkInstanceId"; break;
    case RegisterType::eJoinInstanceId:     name << "vJoinInstanceId"; break;
    case RegisterType::ePatchConstant:      name << (stage == ShaderType::eHull ? "opc" : "vpc") << index << (mask ? "_" : "") << mask; break;
    case RegisterType::eTessCoord:          name << "vDomain"; break;
    case RegisterType::eThis:               name << "this"; break;
    case RegisterType::eUav:                name << (isSm51() ? "U" : "u") << index; break;
    case RegisterType::eTgsm:               name << "g" << index; break;
    case RegisterType::eThreadId:           name << "vThreadID"; break;
    case RegisterType::eThreadGroupId:      name << "vGroupID"; break;
    case RegisterType::eThreadIdInGroup:    name << "vThreadIDInGroup"; break;
    case RegisterType::eThreadIndexInGroup: name << "vThreadIDInGroupFlattened"; break;
    case RegisterType::eCoverageIn:         name << "vCoverageIn"; break;
    case RegisterType::eGsInstanceId:       name << "vInstanceID"; break;
    case RegisterType::eDepthGe:            name << "oDepthGe"; break;
    case RegisterType::eDepthLe:            name << "oDepthLe"; break;
    case RegisterType::eCycleCounter:       name << "vCycleCounter"; break;
    case RegisterType::eStencilRef:         name << "oStencilRef"; break;
    case RegisterType::eInnerCoverage:      name << "vInnerCoverage"; break;

    default: name << "reg_" << uint32_t(type) << "_" << index << (mask ? "_" : "") << mask;
  }

  return name.str();
}


bool Converter::isSm51() const {
  auto [major, minor] = m_parser.getShaderInfo().getVersion();
  return major == 5u && minor >= 1u;
}


bool Converter::initParser(Parser& parser, util::ByteReader reader) {
  if (!reader) {
    Logger::err("No code chunk found in shader.");
    return false;
  }

  if (!(parser = Parser(reader))) {
    Logger::err("Failed to parse code chunk.");
    return false;
  }

  return true;
}


void Converter::logOp(LogLevel severity, const Instruction& op) const {
  Disassembler::Options options = { };
  options.indent = false;
  options.lineNumbers = false;

  Disassembler disasm(options, m_parser.getShaderInfo());
  auto instruction = disasm.disassembleOp(op);

  Logger::log(severity, "Line ", m_instructionCount, ": ", instruction);
}


WriteMask Converter::convertMaskTo32Bit(WriteMask mask) {
  dxbc_spv_assert(isValid64BitMask(mask));

  /* Instructions that operate on both 32-bit and 64-bit types do not
   * match masks in terms of component bits, but instead base it on
   * the number of components set in the write mask. */
  return util::makeWriteMaskForComponents(util::popcnt(uint8_t(mask)) / 2u);
}


WriteMask Converter::convertMaskTo64Bit(WriteMask mask) {
  return util::makeWriteMaskForComponents(util::popcnt(uint8_t(mask)) * 2u);
}


bool Converter::isValid64BitMask(WriteMask mask) {
  /* 64-bit masks must either have none or both bits of the .xy and .zw
   * sub-masks set. Check this by shifting the upper components to the
   * lower ones and comparing the resulting masks. */
  uint8_t a = uint8_t(mask) & 0b0101u;
  uint8_t b = uint8_t(mask) & 0b1010u;

  return a == (b >> 1u);
}


bool Converter::isValidControlPointCount(uint32_t n) {
  return n <= 32u;
}


bool Converter::isValidTessFactor(float f) {
  return f >= 1.0f && f <= 64.0f;
}


bool Converter::is64BitType(ir::BasicType type) {
  auto scalarType = type.getBaseType();

  return scalarType == ir::ScalarType::eF64 ||
         scalarType == ir::ScalarType::eU64 ||
         scalarType == ir::ScalarType::eI64;
}


bool Converter::hasAbsNegModifiers(const Operand& operand) {
  auto modifiers = operand.getModifiers();
  return modifiers.isAbsolute() || modifiers.isNegated();
}

}
