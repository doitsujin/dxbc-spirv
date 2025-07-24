#include "dxbc_converter.h"
#include "dxbc_disasm.h"

namespace dxbc_spv::dxbc {

Converter::Converter(Container container, const Options& options)
: m_dxbc(std::move(container)), m_options(options), m_ioMap(*this) {
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
  if (!initParser(m_parser, m_dxbc.getCodeChunk()) || !m_ioMap.init(m_dxbc, m_parser.getShaderInfo()))
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

    case OpCode::eRet:
      return handleRet(builder);

    case OpCode::eAdd:
    case OpCode::eAnd:
    case OpCode::eBreak:
    case OpCode::eBreakc:
    case OpCode::eCall:
    case OpCode::eCallc:
    case OpCode::eCase:
    case OpCode::eContinue:
    case OpCode::eContinuec:
    case OpCode::eCut:
    case OpCode::eDefault:
    case OpCode::eDerivRtx:
    case OpCode::eDerivRty:
    case OpCode::eDiscard:
    case OpCode::eDiv:
    case OpCode::eDp2:
    case OpCode::eDp3:
    case OpCode::eDp4:
    case OpCode::eElse:
    case OpCode::eEmit:
    case OpCode::eEmitThenCut:
    case OpCode::eEndIf:
    case OpCode::eEndLoop:
    case OpCode::eEndSwitch:
    case OpCode::eEq:
    case OpCode::eExp:
    case OpCode::eFrc:
    case OpCode::eFtoI:
    case OpCode::eFtoU:
    case OpCode::eGe:
    case OpCode::eIAdd:
    case OpCode::eIf:
    case OpCode::eIEq:
    case OpCode::eIGe:
    case OpCode::eILt:
    case OpCode::eIMad:
    case OpCode::eIMax:
    case OpCode::eIMin:
    case OpCode::eIMul:
    case OpCode::eINe:
    case OpCode::eINeg:
    case OpCode::eIShl:
    case OpCode::eIShr:
    case OpCode::eItoF:
    case OpCode::eLabel:
    case OpCode::eLd:
    case OpCode::eLdMs:
    case OpCode::eLog:
    case OpCode::eLoop:
    case OpCode::eLt:
    case OpCode::eMad:
    case OpCode::eMin:
    case OpCode::eMax:
    case OpCode::eCustomData:
    case OpCode::eMov:
    case OpCode::eMovc:
    case OpCode::eMul:
    case OpCode::eNe:
    case OpCode::eNot:
    case OpCode::eOr:
    case OpCode::eResInfo:
    case OpCode::eRetc:
    case OpCode::eRoundNe:
    case OpCode::eRoundNi:
    case OpCode::eRoundPi:
    case OpCode::eRoundZ:
    case OpCode::eRsq:
    case OpCode::eSample:
    case OpCode::eSampleC:
    case OpCode::eSampleClz:
    case OpCode::eSampleL:
    case OpCode::eSampleD:
    case OpCode::eSampleB:
    case OpCode::eSqrt:
    case OpCode::eSwitch:
    case OpCode::eSinCos:
    case OpCode::eUDiv:
    case OpCode::eULt:
    case OpCode::eUGe:
    case OpCode::eUMul:
    case OpCode::eUMad:
    case OpCode::eUMax:
    case OpCode::eUMin:
    case OpCode::eUShr:
    case OpCode::eUtoF:
    case OpCode::eXor:
    case OpCode::eDclResource:
    case OpCode::eDclConstantBuffer:
    case OpCode::eDclSampler:
    case OpCode::eDclGsOutputPrimitiveTopology:
    case OpCode::eDclGsInputPrimitive:
    case OpCode::eDclMaxOutputVertexCount:
    case OpCode::eDclIndexableTemp:
    case OpCode::eLod:
    case OpCode::eGather4:
    case OpCode::eSamplePos:
    case OpCode::eSampleInfo:
    case OpCode::eEmitStream:
    case OpCode::eCutStream:
    case OpCode::eEmitThenCutStream:
    case OpCode::eInterfaceCall:
    case OpCode::eBufInfo:
    case OpCode::eDerivRtxCoarse:
    case OpCode::eDerivRtxFine:
    case OpCode::eDerivRtyCoarse:
    case OpCode::eDerivRtyFine:
    case OpCode::eGather4C:
    case OpCode::eGather4Po:
    case OpCode::eGather4PoC:
    case OpCode::eRcp:
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
    case OpCode::eBfi:
    case OpCode::eBfRev:
    case OpCode::eSwapc:
    case OpCode::eDclStream:
    case OpCode::eDclFunctionBody:
    case OpCode::eDclFunctionTable:
    case OpCode::eDclInterface:
    case OpCode::eDclThreadGroup:
    case OpCode::eDclUavTyped:
    case OpCode::eDclUavRaw:
    case OpCode::eDclUavStructured:
    case OpCode::eDclThreadGroupSharedMemoryRaw:
    case OpCode::eDclThreadGroupSharedMemoryStructured:
    case OpCode::eDclResourceRaw:
    case OpCode::eDclResourceStructured:
    case OpCode::eLdUavTyped:
    case OpCode::eStoreUavTyped:
    case OpCode::eLdRaw:
    case OpCode::eStoreRaw:
    case OpCode::eLdStructured:
    case OpCode::eStoreStructured:
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
    case OpCode::eSync:
    case OpCode::eDAdd:
    case OpCode::eDMax:
    case OpCode::eDMin:
    case OpCode::eDMul:
    case OpCode::eDEq:
    case OpCode::eDGe:
    case OpCode::eDLt:
    case OpCode::eDNe:
    case OpCode::eDMov:
    case OpCode::eDMovc:
    case OpCode::eDtoF:
    case OpCode::eFtoD:
    case OpCode::eEvalSnapped:
    case OpCode::eEvalSampleIndex:
    case OpCode::eEvalCentroid:
    case OpCode::eDclGsInstanceCount:
    case OpCode::eAbort:
    case OpCode::eDebugBreak:
    case OpCode::eDDiv:
    case OpCode::eDFma:
    case OpCode::eDRcp:
    case OpCode::eMsad:
    case OpCode::eDtoI:
    case OpCode::eDtoU:
    case OpCode::eItoD:
    case OpCode::eUtoD:
    case OpCode::eGather4S:
    case OpCode::eGather4CS:
    case OpCode::eGather4PoS:
    case OpCode::eGather4PoCS:
    case OpCode::eLdS:
    case OpCode::eLdMsS:
    case OpCode::eLdUavTypedS:
    case OpCode::eLdRawS:
    case OpCode::eLdStructuredS:
    case OpCode::eSampleLS:
    case OpCode::eSampleClzS:
    case OpCode::eSampleClampS:
    case OpCode::eSampleBClampS:
    case OpCode::eSampleDClampS:
    case OpCode::eSampleCClampS:
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

  return m_ioMap.handleHsPhase();
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


bool Converter::handleRet(ir::Builder& builder) {
  builder.add(ir::Op::Return());
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

  if (mod.isNonUniform())
    applyNonUniform(builder, def);

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
      type = ir::BasicType(scalarType, type.getVectorSize());
    }

    if (type.isFloatType()) {
      def = builder.add(ir::Op::FClamp(type, def,
        makeTypedConstant(builder, type, 0.0f),
        makeTypedConstant(builder, type, 1.0f)));
    } else {
      logOpMessage(LogLevel::eWarn, instruction, "Saturation applied to a non-float result.");
    }
  }

  return def;
}


ir::SsaDef Converter::loadSrc(ir::Builder& builder, const Instruction& op, const Operand& operand, WriteMask mask, ir::ScalarType type) {
  switch (operand.getRegisterType()) {
    case RegisterType::eNull:
      return ir::SsaDef();

    case RegisterType::eImm32:
    case RegisterType::eImm64:
    case RegisterType::eTemp:
    case RegisterType::eIndexableTemp:
    case RegisterType::eCbv:
    case RegisterType::eIcb:
    case RegisterType::eForkInstanceId:
    case RegisterType::eJoinInstanceId:
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
      return m_ioMap.emitLoad(builder, op, operand, mask, type);

    default: {
      auto name = makeRegisterDebugName(operand.getRegisterType(), 0u, WriteMask());
      logOpError(op, "Unhandled source operand: ", name);
    } return ir::SsaDef();
  }
}


ir::SsaDef Converter::loadSrcModified(ir::Builder& builder, const Instruction& op, const Operand& operand, WriteMask mask, ir::ScalarType type) {
  auto value = loadSrc(builder, op, operand, mask, type);
  return applySrcModifiers(builder, value, op, operand);
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
  switch (operand.getRegisterType()) {
    case RegisterType::eNull:
      return true;

    case RegisterType::eTemp:
    case RegisterType::eIndexableTemp:
      /* TODO implement */
      dxbc_spv_unreachable();
      return false;

    case RegisterType::eOutput:
    case RegisterType::eDepth:
    case RegisterType::eDepthLe:
    case RegisterType::eDepthGe:
    case RegisterType::eCoverageOut:
    case RegisterType::eStencilRef:
      return m_ioMap.emitStore(builder, op, operand, value);

    default: {
      auto name = makeRegisterDebugName(operand.getRegisterType(), 0u, operand.getWriteMask());
      logOpError(op, "Unhandled destination operand: ", name);
    } return false;
  }
}


bool Converter::storeDstModified(ir::Builder& builder, const Instruction& op, const Operand& operand, ir::SsaDef value) {
  value = applyDstModifiers(builder, value, op, operand);
  return storeDst(builder, op, operand, value);
}


ir::ScalarType Converter::determineOperandType(const Operand& operand, ir::ScalarType fallback) const {
  /* Use base type from the instruction layout */
  auto type = operand.getInfo().type;

  /* If the operand is decorated as min precision, apply
   * it to the operand type if possible. */
  auto precision = operand.getModifiers().getPrecision();

  switch (precision) {
    case MinPrecision::eNone:
      break;

    case MinPrecision::eMin10Float: {
      if (type == ir::ScalarType::eF32 ||
          type == ir::ScalarType::eUnknown)
        type = resolveMinPrecisionType(ir::ScalarType::eMinF10);
    } break;

    case MinPrecision::eMin16Float: {
      if (type == ir::ScalarType::eF32 ||
          type == ir::ScalarType::eUnknown)
        type = resolveMinPrecisionType(ir::ScalarType::eMinF16);
    } break;

    case MinPrecision::eMin16Uint: {
      if (type == ir::ScalarType::eU32 ||
          type == ir::ScalarType::eAnyI32 ||
          type == ir::ScalarType::eUnknown)
        type = resolveMinPrecisionType(ir::ScalarType::eMinU16);
    } break;

    case MinPrecision::eMin16Sint: {
      if (type == ir::ScalarType::eI32 ||
          type == ir::ScalarType::eAnyI32 ||
          type == ir::ScalarType::eUnknown)
        type = resolveMinPrecisionType(ir::ScalarType::eMinI16);
    } break;
  }

  /* Use fallback type if we couldn't resolve the type. */
  if (type == ir::ScalarType::eUnknown)
    type = fallback;

  return type;
}


ir::ScalarType Converter::resolveMinPrecisionType(ir::ScalarType type) const {
  switch (type) {
    case ir::ScalarType::eMinF10:
    case ir::ScalarType::eMinF16:
      return m_options.enableFp16
        ? ir::ScalarType::eF16
        : ir::ScalarType::eF32;

    case ir::ScalarType::eMinI16:
      return m_options.enableInt16
        ? ir::ScalarType::eI16
        : ir::ScalarType::eI32;

    case ir::ScalarType::eMinU16:
      return m_options.enableInt16
        ? ir::ScalarType::eU16
        : ir::ScalarType::eU32;

    default:
      return type;
  }
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
      case ir::ScalarType::eAnyI8:
      case ir::ScalarType::eI8:   return ir::Operand(int8_t(value));
      case ir::ScalarType::eAnyI16:
      case ir::ScalarType::eI16:  return ir::Operand(int16_t(value));
      case ir::ScalarType::eAnyI32:
      case ir::ScalarType::eI32:  return ir::Operand(int32_t(value));
      case ir::ScalarType::eAnyI64:
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
    case RegisterType::eTemp:               name << "r" << index << "_" << mask; break;
    case RegisterType::eInput:
    case RegisterType::eControlPointIn:     name << "v" << index << "_" << mask; break;
    case RegisterType::eOutput:
    case RegisterType::eControlPointOut:    name << "o" << index << "_" << mask; break;
    case RegisterType::eIndexableTemp:      name << "x" << index << "_" << mask; break;
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
    case RegisterType::ePatchConstant:      name << (stage == ShaderType::eHull ? "opc" : "vpc") << index << "_" << mask; break;
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


bool Converter::isValidControlPointCount(uint32_t n) {
  return n <= 32u;
}


bool Converter::isValidTessFactor(float f) {
  return f >= 1.0f && f <= 64.0f;
}

}
