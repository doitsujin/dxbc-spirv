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
  if (!initParser(m_parser, m_dxbc.getCodeChunk()) ||
      !m_ioMap.init(m_dxbc, m_parser.getShaderInfo()))
    return false;

  initialize(builder);

  while (m_parser) {
    Instruction op = m_parser.parseInstruction();

    if (!op || !convertInstruction(builder, op))
      return false;
  }

  return finalize(builder);
}


bool Converter::convertInstruction(ir::Builder& builder, const Instruction& op) {
  auto opCode = op.getOpToken().getOpCode();

  /* Increment instruction counter for debug purposes */
  m_instructionCount += 1u;

  switch (opCode) {
    case OpCode::eNop:
      return true;

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

    case OpCode::eIShl:
    case OpCode::eIShr:
    case OpCode::eUShr:
      return handleIntShift(builder, op);

    case OpCode::eBfi:
      return handleBitInsert(builder, op);

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

    case OpCode::eLdRaw:
    case OpCode::eLdRawS:
      return handleLdRaw(builder, op);

    case OpCode::eLdStructured:
    case OpCode::eLdStructuredS:
      return handleLdStructured(builder, op);

    case OpCode::eStoreRaw:
      return handleStoreRaw(builder, op);

    case OpCode::eStoreStructured:
      return handleStoreStructured(builder, op);

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

    case OpCode::eCall:
    case OpCode::eCallc:
    case OpCode::eDerivRtx:
    case OpCode::eDerivRty:
    case OpCode::eLabel:
    case OpCode::eLd:
    case OpCode::eLdMs:
    case OpCode::eCustomData:
    case OpCode::eResInfo:
    case OpCode::eSinCos:
    case OpCode::eUDiv:
    case OpCode::eLod:
    case OpCode::eGather4:
    case OpCode::eSamplePos:
    case OpCode::eSampleInfo:
    case OpCode::eInterfaceCall:
    case OpCode::eBufInfo:
    case OpCode::eDerivRtxCoarse:
    case OpCode::eDerivRtxFine:
    case OpCode::eDerivRtyCoarse:
    case OpCode::eDerivRtyFine:
    case OpCode::eGather4C:
    case OpCode::eGather4Po:
    case OpCode::eGather4PoC:
    case OpCode::eF32toF16:
    case OpCode::eF16toF32:
    case OpCode::eUAddc:
    case OpCode::eUSubb:
    case OpCode::eCountBits:
    case OpCode::eFirstBitHi:
    case OpCode::eFirstBitLo:
    case OpCode::eFirstBitShi:
    case OpCode::eUBfe:
    case OpCode::eIBfe:
    case OpCode::eBfRev:
    case OpCode::eSwapc:
    case OpCode::eDclFunctionBody:
    case OpCode::eDclFunctionTable:
    case OpCode::eDclInterface:
    case OpCode::eLdUavTyped:
    case OpCode::eStoreUavTyped:
    case OpCode::eAtomicAnd:
    case OpCode::eAtomicOr:
    case OpCode::eAtomicXor:
    case OpCode::eAtomicCmpStore:
    case OpCode::eAtomicIAdd:
    case OpCode::eAtomicIMax:
    case OpCode::eAtomicIMin:
    case OpCode::eAtomicUMax:
    case OpCode::eAtomicUMin:
    case OpCode::eImmAtomicAlloc:
    case OpCode::eImmAtomicConsume:
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
    case OpCode::eAbort:
    case OpCode::eDebugBreak:
    case OpCode::eMsad:
    case OpCode::eGather4S:
    case OpCode::eGather4CS:
    case OpCode::eGather4PoS:
    case OpCode::eGather4PoCS:
    case OpCode::eLdS:
    case OpCode::eLdMsS:
    case OpCode::eLdUavTypedS:
    case OpCode::eCheckAccessFullyMapped:
      /* TODO implement these */
      break;
  }

  /* TODO fix */
  return logOpError(op, "Unhandled opcode.") || true;
}


bool Converter::initialize(ir::Builder& builder) {
  auto info = m_parser.getShaderInfo();

  /* A valid debug namee is required for the main function */
  m_entryPoint.mainFunc = builder.add(ir::Op::Function(ir::ScalarType::eVoid));
  builder.add(ir::Op::FunctionEnd());
  builder.add(ir::Op::DebugName(m_entryPoint.mainFunc, "main"));

  if (info.getType() == ShaderType::eHull) {
    m_entryPoint.patchConstantFunc = builder.add(ir::Op::Function(ir::ScalarType::eVoid));
    builder.add(ir::Op::FunctionEnd());

    if (m_options.includeDebugNames)
      builder.add(ir::Op::DebugName(m_entryPoint.patchConstantFunc, "patch_const"));
  }

  /* Emit entry point instruction as the first instruction of the
   * shader. This is technically not needed, but makes things more
   * readable. */
  auto stage = resolveShaderStage(info.getType());

  auto entryPointOp = (info.getType() == ShaderType::eHull)
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


bool Converter::finalize(ir::Builder& builder) {
  emitFloatModes(builder);

  if (m_parser.getShaderInfo().getType() == ShaderType::eHull) {
    emitHsPatchConstantFunction(builder);

    if (!emitHsStateSetup(builder))
      return false;

    if (!m_hs.hasControlPointPhase && m_hs.controlPointsOut) {
      builder.setCursor(m_entryPoint.mainFunc);

      if (!m_ioMap.emitHsControlPointPhasePassthrough(builder))
        return false;
    }
  }

  if (m_parser.getShaderInfo().getType() == ShaderType::eGeometry) {
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
}


bool Converter::emitGsStateSetup(ir::Builder& builder) {
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

  ir::SsaDef result;

  if (op.getOpToken().getPreciseMask()) {
    result = builder.add(ir::Op::FMad(vectorType,
      factorA, factorB, addend).setFlags(ir::OpFlag::ePrecise));
  } else {
    auto product = builder.add(ir::Op::FMul(vectorType, factorA, factorB));
    result = builder.add(ir::Op::FAdd(vectorType, product, addend));
  }

  return storeDstModified(builder, op, dst, result);
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
      case OpCode::eDp2: return makeWriteMaskForComponents(2u);
      case OpCode::eDp3: return makeWriteMaskForComponents(3u);
      case OpCode::eDp4: return makeWriteMaskForComponents(4u);
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
  applyDstModifiers(builder, result, op, dst);

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

  if (srcIsFloat && !dstIsFloat) {
    auto lowerBound = builder.add(ir::Op::ConvertItoF(srcType.getBaseType(),
      builder.add(ir::Op::MinValue(dstType.getBaseType()))));
    auto upperBound = builder.add(ir::Op::ConvertItoF(srcType.getBaseType(),
      builder.add(ir::Op::MaxValue(dstType.getBaseType()))));

    value = builder.add(ir::Op::FClamp(srcType, value,
      broadcastScalar(builder, lowerBound, srcMask),
      broadcastScalar(builder, upperBound, srcMask)));
  }

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
    auto scalarType = determineOperandType(dstLo, ir::ScalarType::eU32);

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


bool Converter::handleBitInsert(ir::Builder& builder, const Instruction& op) {
  /* bfi takes the following operands:
   * (dst0) Result value
   * (src0) Number of bits to take from src2
   * (src1) Offset where to insert the bits
   * (src2) Operand to insert
   * (src3) Base operand
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

    return data && storeDstModified(builder, op, dstValue, data);
  }
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
    return m_resources.emitRawStructuredStore(builder, op,
      resource, structIndex, structOffset, value);
  }
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
  auto coordComponentCount = ir::resourceCoordComponentCount(textureInfo.kind);
  auto coordComponentMask = makeWriteMaskForComponents(coordComponentCount);

  ir::SsaDef layer = { };
  ir::SsaDef coord = loadSrcModified(builder, op, address, coordComponentMask, ir::ScalarType::eF32);

  if (ir::resourceIsLayered(textureInfo.kind)) {
    auto layerComponentMask = componentBit(Component(coordComponentCount));
    layer = loadSrcModified(builder, op, address, layerComponentMask, ir::ScalarType::eF32);
  }

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

  if (op.getOpToken().getOpCode() == OpCode::eBreakc) {
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
    memTypes |= ir::MemoryType::eUavBuffer | ir::MemoryType::eUavImage;
  }

  if (syncFlags & SyncFlag::eUavMemoryGlobal) {
    memScope = ir::Scope::eGlobal;
    memTypes |= ir::MemoryType::eUavBuffer | ir::MemoryType::eUavImage;
  }

  if (execScope != ir::Scope::eThread || memScope != ir::Scope::eThread)
    builder.add(ir::Op::Barrier(execScope, memScope, memTypes));

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
      /* TODO implement */
      dxbc_spv_unreachable();
      return ir::SsaDef();

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
      case ir::ScalarType::eU32:  return ir::Operand(uint32_t(value));
      case ir::ScalarType::eU64:  return ir::Operand(uint64_t(value));
      case ir::ScalarType::eI8:   return ir::Operand(int8_t(value));
      case ir::ScalarType::eI16:  return ir::Operand(int16_t(value));
      case ir::ScalarType::eI32:  return ir::Operand(int32_t(value));
      case ir::ScalarType::eI64:  return ir::Operand(int64_t(value));
      case ir::ScalarType::eF16:  return ir::Operand(util::float16_t(value));
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
    case RegisterType::eInterface:          name << "i" << index; break;
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
  return makeWriteMaskForComponents(util::popcnt(uint8_t(mask)) / 2u);
}


WriteMask Converter::convertMaskTo64Bit(WriteMask mask) {
  return makeWriteMaskForComponents(util::popcnt(uint8_t(mask)) * 2u);
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
