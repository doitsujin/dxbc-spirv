#include "sm3_converter.h"

#include "sm3_disasm.h"

#include "../ir/ir_utils.h"

namespace dxbc_spv::sm3 {

constexpr uint32_t TextureStageCount = 8u;

/* Types in SM3
 * Integer:
 *   - Instructions:
 *     - loop and rep instructions: Both of those load it straight from a constant integer register.
 *   - Registers:
 *     - Constant integer: Only used for loop and rep. Can NOT be used to write the address register or for relative addressing.
 *     - Address register (a0): Only written to with mova (or mov on SM1.1) which takes in a float and has a specific rounding mode.
 *                              Used for relative addressing, can't be read directly.
 *     - Loop counter register (aL): Used to hold the loop index. Automatically populated in loops, can't be written to.
 *
 * Boolean:
 *   - Instructions:
 *     - if bool: Loads straight from a constant boolean register.
 *     - if pred: Loads predicate register.
 *     - callnz: Loads straight from a constant boolean register.
 *     - callnz pred: Loads predicate register.
 *     - breakp pred: Loads predicate register.
 *   - Registers:
 *     - pred: Can't be read directly. Can only be used with if pred or to make operations conditional with predication.
 *             Can only be written to with setp which compares two float registers.
 *             Can be altered with NOT modifier before application.
 *
 * Float:
 *   Everything else. Has partial precision flag in Dst operand.
 */

Converter::Converter(util::ByteReader code,
        SpecializationConstantLayout& specConstantsLayout,
  const Options& options)
: m_code(code)
, m_options(options)
, m_ioMap(*this)
, m_regFile(*this)
, m_resources(*this)
, m_specConstants(*this, specConstantsLayout){

}

Converter::~Converter() {

}

bool Converter::convertShader(ir::Builder& builder) {
  if (!initParser(m_parser, m_code))
    return false;

  auto shaderType = getShaderInfo().getType();

  initialize(builder, shaderType);

  Instruction op;
  if (m_parser)
    op = m_parser.parseInstruction();

  while (op) {
    /* Co-issued instructions are executed out of order.
     * So parse the next instruction, and execute it, if it's co-issued.
     * After that parse the next instruction and do the next loop iteration. */
    Instruction nextOp;
    if (m_parser) {
      nextOp = m_parser.parseInstruction();

      if (nextOp && nextOp.isCoissued()) {
        /* Execute the co-issued instruction first. */
        if (!convertInstruction(builder, nextOp)) {
          return false;
        }
        nextOp = Instruction();
        if (m_parser) {
          nextOp = m_parser.parseInstruction();
        }
      }
    }

    /* Execute the actual instruction. */
    if (!op || !convertInstruction(builder, op))
      return false;

    op = nextOp;
  }

  return finalize(builder, shaderType);
}


bool Converter::convertInstruction(ir::Builder& builder, const Instruction& op) {
  auto opCode = op.getOpCode();

  /* Increment instruction counter for debug purposes */
  m_instructionCount += 1u;

  switch (opCode) {
    case OpCode::eNop:
    case OpCode::eReserved0:
    case OpCode::ePhase:
    case OpCode::eEnd:
      return true;

    case OpCode::eTexM3x2Pad:
    case OpCode::eTexM3x3Pad:
      /* We don't need to do anything here, these are just padding instructions */
      return true;

    case OpCode::eComment:
      return handleComment(builder, op);

    case OpCode::eDef:
    case OpCode::eDefI:
    case OpCode::eDefB:
      return handleDef(builder, op);

    case OpCode::eDcl:
      return handleDcl(builder, op);

    case OpCode::eMov:
    case OpCode::eMova:
      return handleMov(builder, op);

    case OpCode::eAdd:
    case OpCode::eSub:
    case OpCode::eExp:
    case OpCode::eFrc:
    case OpCode::eLog:
    case OpCode::eLogP:
    case OpCode::eMax:
    case OpCode::eMin:
    case OpCode::eMul:
    case OpCode::eRcp:
    case OpCode::eRsq:
    case OpCode::eSgn:
    case OpCode::eAbs:
    case OpCode::eMad:
      return handleArithmetic(builder, op);

    case OpCode::eDp2Add:
    case OpCode::eDp3:
    case OpCode::eDp4:
      return handleDot(builder, op);

    case OpCode::eSlt:
    case OpCode::eSge:
      return handleCompare(builder, op);

    case OpCode::eLit:
      return handleLit(builder, op);

    case OpCode::eM4x4:
    case OpCode::eM4x3:
    case OpCode::eM3x4:
    case OpCode::eM3x3:
    case OpCode::eM3x2:
      return handleMatrixArithmetic(builder, op);

    case OpCode::eBem:
      return handleBem(builder, op);

    case OpCode::eTexCrd:
      return handleTexCoord(builder, op);

    case OpCode::eTexLd:
    case OpCode::eTexBem:
    case OpCode::eTexBemL:
    case OpCode::eTexReg2Ar:
    case OpCode::eTexReg2Gb:
    case OpCode::eTexM3x2Tex:
    case OpCode::eTexM3x3Tex:
    case OpCode::eTexM3x3Spec:
    case OpCode::eTexM3x3VSpec:
    case OpCode::eTexReg2Rgb:
    case OpCode::eTexDp3Tex:
    case OpCode::eTexM3x2Depth:
    case OpCode::eTexDp3:
    case OpCode::eTexM3x3:
    case OpCode::eTexLdd:
    case OpCode::eTexLdl:
      return handleTextureSample(builder, op);

    case OpCode::eTexKill:
      return handleTexKill(builder, op);

    case OpCode::eTexDepth:
      return handleTexDepth(builder, op);

    case OpCode::eLrp:
    case OpCode::eCmp:
    case OpCode::eCnd:
      return handleSelect(builder, op);

    case OpCode::eNrm:
      return handleNrm(builder, op);

    case OpCode::eSinCos:
      return handleSinCos(builder, op);

    case OpCode::ePow:
      return handlePow(builder, op);

    case OpCode::eDst:
      return handleDst(builder, op);

    case OpCode::eDsX:
    case OpCode::eDsY:
      return handleDerivatives(builder, op);

    case OpCode::eCrs:
      return handleCrs(builder, op);

    case OpCode::eSetP:
      return handleSetP(builder, op);

    case OpCode::eExpP:
    case OpCode::eIf:
    case OpCode::eIfC:
    case OpCode::eElse:
    case OpCode::eEndIf:
    case OpCode::eBreak:
    case OpCode::eBreakC:
    case OpCode::eBreakP:
    case OpCode::eLoop:
    case OpCode::eEndLoop:
    case OpCode::eRep:
    case OpCode::eEndRep:
      return logOpError(op, "OpCode ", opCode, " is not implemented.");

    case OpCode::eLabel:
    case OpCode::eCall:
    case OpCode::eCallNz:
    case OpCode::eRet:
      return logOpError(op, "Function calls aren't supported.");
  }

  return logOpError(op, "Unhandled opcode.");
}


bool Converter::initialize(ir::Builder& builder, ShaderType shaderType) {
  /* A valid debug namee is required for the main function */
  m_entryPoint.mainFunc = builder.add(ir::Op::Function(ir::ScalarType::eVoid));
  ir::SsaDef afterMainFunc = builder.add(ir::Op::FunctionEnd());
  builder.add(ir::Op::DebugName(m_entryPoint.mainFunc, "main"));

  /* Emit entry point instruction as the first instruction of the
   * shader. This is technically not needed, but makes things more
   * readable. */
  auto stage = resolveShaderStage(shaderType);

  auto entryPointOp = ir::Op::EntryPoint(m_entryPoint.mainFunc, stage);

  m_entryPoint.def = builder.addAfter(ir::SsaDef(), std::move(entryPointOp));

  /* Need to emit the shader name regardless of debug names as well */
  if (m_options.name)
    builder.add(ir::Op::DebugName(m_entryPoint.def, m_options.name));

  m_specConstants.setInsertCursor(afterMainFunc);
  m_resources.setInsertCursor(afterMainFunc);
  m_specConstants.initialize(builder);
  m_ioMap.initialize(builder);
  m_regFile.initialize(builder);
  m_resources.initialize(builder);

  if (getShaderInfo().getType() == ShaderType::ePixel) {
    m_psSharedData = emitSharedConstants(builder);
  }

  /* Set cursor to main function so that instructions will be emitted
   * in the correct location */
  builder.setCursor(m_entryPoint.mainFunc);
  return true;
}


bool Converter::finalize(ir::Builder& builder, ShaderType shaderType) {
  m_ioMap.finalize(builder);

  return true;
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


ir::SsaDef Converter::emitSharedConstants(ir::Builder& builder) {
  ir::Type bufferStruct = ir::Type();

  for (uint32_t i = 0u; i  < TextureStageCount; i++) {
    bufferStruct.addStructMember(ir::BasicType(ir::ScalarType::eF32, 4u));
    bufferStruct.addStructMember(ir::BasicType(ir::ScalarType::eF32, 2u));
    bufferStruct.addStructMember(ir::BasicType(ir::ScalarType::eF32, 2u));
    bufferStruct.addStructMember(ir::ScalarType::eF32);
    bufferStruct.addStructMember(ir::ScalarType::eF32);
  }

  auto buffer = builder.add(ir::Op::DclCbv(bufferStruct, getEntryPoint(), SpecialBindingsRegSpace, PSSharedDataCbvRegIdx, 1u));

  if (getOptions().includeDebugNames) {
    builder.add(ir::Op::DebugName(buffer, "PSSharedData"));
    for (uint32_t i = 0u; i < TextureStageCount; i++) {
      std::stringstream namestream;
      namestream << "Constant" << i;
      builder.add(ir::Op::DebugMemberName(buffer, i, namestream.str().c_str()));
      namestream.clear();
      namestream << "BumpEnvMat0_" << i;
      builder.add(ir::Op::DebugMemberName(buffer, i + 1u, namestream.str().c_str()));
      namestream.clear();
      namestream << "BumpEnvMat1_" << i;
      builder.add(ir::Op::DebugMemberName(buffer, i + 2u, namestream.str().c_str()));
      namestream.clear();
      namestream << "BumpEnvLScale" << i;
      builder.add(ir::Op::DebugMemberName(buffer, i + 3u, namestream.str().c_str()));
      namestream.clear();
      namestream << "BumpEnvLOffset" << i;
      builder.add(ir::Op::DebugMemberName(buffer, i + 4u, namestream.str().c_str()));
      namestream.clear();
    }
  }

  return buffer;
}


ir::SsaDef Converter::applyBumpMapping(ir::Builder& builder, uint32_t stageIdx, ir::SsaDef src0, ir::SsaDef src1) {
  /*
   * dst.x = src0.x + D3DTSS_BUMPENVMAT00(stage n) * src1.x
  *                 + D3DTSS_BUMPENVMAT10(stage n) * src1.y
  *
   * dst.y = src0.y + D3DTSS_BUMPENVMAT01(stage n) * src1.x
   *                + D3DTSS_BUMPENVMAT11(stage n) * src1.y
   */

  auto type = builder.getOp(src0).getType().getBaseType(0u);
  auto scalarType = type.getBaseType();
  dxbc_spv_assert(scalarType == builder.getOp(src1).getType().getBaseType(0u).getBaseType());

  auto descriptor = builder.add(ir::Op::DescriptorLoad(ir::ScalarType::eCbv, m_psSharedData, ir::SsaDef()));
  std::array<ir::SsaDef, 2> components = {};
  for (uint32_t i = 0u; i < components.size(); i++) {
    /* Load bump matrix */
    auto bumpEnvMat0 = builder.add(ir::Op::BufferLoad(ir::BasicType(ir::ScalarType::eF32, 2u), descriptor, builder.makeConstant(stageIdx * 5u + 1u), 16u));
    auto bumpEnvMat1 = builder.add(ir::Op::BufferLoad(ir::BasicType(ir::ScalarType::eF32, 2u), descriptor, builder.makeConstant(stageIdx * 5u + 2u), 8u));
    if (scalarType != ir::ScalarType::eF32) {
      bumpEnvMat0 = builder.add(ir::Op::ConsumeAs(ir::BasicType(scalarType, 2u), bumpEnvMat0));
      bumpEnvMat1 = builder.add(ir::Op::ConsumeAs(ir::BasicType(scalarType, 2u), bumpEnvMat1));
    }
    auto src1r = builder.add(ir::Op::CompositeExtract(scalarType, src1, builder.makeConstant(0u)));
    auto bumped0 = builder.add(OpFMul(scalarType, bumpEnvMat0, src1r));
    auto src1g = builder.add(ir::Op::CompositeExtract(scalarType, src1, builder.makeConstant(1u)));
    auto bumped1 = builder.add(OpFMul(scalarType, bumpEnvMat1, src1g));
    auto bumpedSum = builder.add(ir::Op::FAdd(scalarType, bumped0, bumped1));
    auto src0Component = builder.add(ir::Op::CompositeExtract(scalarType, src0, builder.makeConstant(i)));
    components[i] = builder.add(ir::Op::FAdd(scalarType, src0Component, bumpedSum));
  }
  return buildVector(builder, scalarType, components.size(), components.data());
}


ir::SsaDef Converter::normalizeVector(ir::Builder& builder, ir::SsaDef def) {
  auto type = builder.getOp(def).getType().getBaseType(0u);
  auto scalarType = type.getBaseType();
  uint32_t vecSize = type.getVectorSize();
  auto lengthSquared = builder.add(OpFDot(scalarType, def, def));
  auto lengthInv = builder.add(ir::Op::FRsq(scalarType, lengthSquared));
  return builder.add(ir::Op::FMul(type, def, broadcastScalar(builder, lengthInv, WriteMask((1u << vecSize) - 1u))));
}


ir::SsaDef Converter::emitComparison(ir::Builder& builder, ir::SsaDef a, ir::SsaDef b, ComparisonMode comparisonMode) {
  switch (comparisonMode) {
    case ComparisonMode::eNever:
      return builder.makeConstant(false);
      break;
    case ComparisonMode::eGreaterThan:
      return builder.add(ir::Op::FGt(ir::ScalarType::eBool, a, b));
      break;
    case ComparisonMode::eEqual:
      return builder.add(ir::Op::FEq(ir::ScalarType::eBool, a, b));
      break;
    case ComparisonMode::eGreaterEqual:
      return builder.add(ir::Op::FGe(ir::ScalarType::eBool, a, b));
      break;
    case ComparisonMode::eLessThan:
      return builder.add(ir::Op::FLt(ir::ScalarType::eBool, a, b));
      break;
    case ComparisonMode::eNotEqual:
      return builder.add(ir::Op::FNe(ir::ScalarType::eBool, a, b));
      break;
    case ComparisonMode::eLessEqual:
      return builder.add(ir::Op::FLe(ir::ScalarType::eBool, a, b));
      break;
    case ComparisonMode::eAlways:
      return builder.makeConstant(true);
      break;

    default:
      Logger::warn("Invalid comparison mode: '", uint32_t(comparisonMode), "'");
      return builder.makeConstant(true);
  }
}


bool Converter::handleComment(ir::Builder& builder, const Instruction& op) {
  /* The comment is always at the start of the shader from what we've seen,
   * so no need to get extra clever here. */
  if (m_options.includeDebugNames && op.getOpCode() == OpCode::eComment && !m_ctab) {
    auto ctabReader = util::ByteReader(op.getCommentData(), op.getCommentDataSize());
    m_ctab = ConstantTable(ctabReader);
    m_resources.emitNamedConstantRanges(builder, m_ctab);
  }
  return true;
}


bool Converter::handleDef(ir::Builder& builder, const Instruction& op) {
  /* def instructions define so-called immediate constants.
   * Immediate instructions take precedence over constants set using API methods. */
  dxbc_spv_assert(op.hasDst());
  dxbc_spv_assert(op.hasImm());
  auto dst = op.getDst();
  auto imm = op.getImm();

  m_resources.emitDefineConstant(builder, dst.getRegisterType(), dst.getIndex(), imm);

  return true;
}


bool Converter::handleDcl(ir::Builder& builder, const Instruction& op) {
  auto dst = op.getDst();
  switch (dst.getRegisterType()) {
    case RegisterType::eSampler:
      return m_resources.handleDclSampler(builder, op);

    case RegisterType::eAttributeOut:
    case RegisterType::eOutput:
    case RegisterType::eInput:
    case RegisterType::eTexture:
    case RegisterType::ePixelTexCoord:
    case RegisterType::eMiscType:
    case RegisterType::eColorOut:
      return m_ioMap.handleDclIoVar(builder, op);

    default:
      dxbc_spv_unreachable();
      return false;
  }
}


bool Converter::handleMov(ir::Builder& builder, const Instruction& op) {
  /* Mov always moves data from a float register to another float register.
   * There's just one exception: mova moves data from a float register to an address register
   * and rounds the float (RTN) in the process.
   * On SM1.1 mova doesn't exist and the regular mov has that responsibility. */

  const auto& dst = op.getDst();
  const auto& src = op.getSrc(0u);

  dxbc_spv_assert(op.getSrcCount() == 1u);
  dxbc_spv_assert(op.hasDst());

  WriteMask writeMask = dst.getWriteMask(getShaderInfo());

  /* Even when writing the address register, we need to load it as a float to round properly. */
  auto scalarType = dst.isPartialPrecision() ? ir::ScalarType::eMinF16 : ir::ScalarType::eF32;

  auto value = loadSrcModified(builder, op, src, writeMask, scalarType);

  if (!value)
    return false;

  /* Mova writes to the address register. On <= SM2.1 mov *can* write to the address register (which only exists for VS). */
  if (dst.getRegisterType() == RegisterType::eAddr && getShaderInfo().getType() == ShaderType::eVertex) {
    uint32_t componentCount = util::popcnt(uint8_t(writeMask));
    util::small_vector<ir::SsaDef, 4u> components;
    for (auto _ : writeMask) {
      auto componentIndex = components.size();
      auto scalarValue = ir::extractFromVector(builder, value, componentIndex);

      ir::SsaDef roundedValue;
      if (getShaderInfo().getVersion().first < 2 && getShaderInfo().getVersion().second < 2)
        /* Contrary to what the documentation says, we need to floor here. */
        roundedValue = builder.add(ir::Op::FRound(scalarType, scalarValue, ir::RoundMode::eNegativeInf));
      else
        roundedValue = builder.add(ir::Op::FRound(scalarType, scalarValue, ir::RoundMode::eNearestEven));

      components.push_back(builder.add(ir::Op::Cast(ir::ScalarType::eI32, roundedValue)));
    }
    value = buildVector(builder, ir::ScalarType::eI32, componentCount, components.data());
  }

  return storeDstModifiedPredicated(builder, op, dst, value);
}


bool Converter::handleArithmetic(ir::Builder& builder, const Instruction& op) {
  /* All instructions handled here will operate on float vectors of any kind. */
  auto opCode = op.getOpCode();

  dxbc_spv_assert(op.getSrcCount());
  dxbc_spv_assert(op.hasDst());

  /* Instruction type */
  const auto& dst = op.getDst();

  WriteMask writeMask = dst.getWriteMask(getShaderInfo());

  bool isPartialPrecision = dst.isPartialPrecision();
  switch (opCode) {
    /* Exp & Log are explicitly full-precision instructions. */
    case OpCode::eExp:
    case OpCode::eLog:  isPartialPrecision = false; break;
    /* LogP is an explicitly partial-precision instruction. */
    case OpCode::eLogP: isPartialPrecision = true;  break;
    default: break;
  }

  auto scalarType = isPartialPrecision ? ir::ScalarType::eMinF16 : ir::ScalarType::eF32;
  auto vectorType = makeVectorType(scalarType, writeMask);

  /* Load source operands */
  util::small_vector<ir::SsaDef, 3u> src;

  for (uint32_t i = 0u; i < op.getSrcCount(); i++) {
    auto value = loadSrcModified(builder, op, op.getSrc(i), writeMask, scalarType);

    if (!value)
      return false;

    src.push_back(value);
  }

  ir::Op result = [this, opCode, vectorType, &src] {
    switch (opCode) {
      case OpCode::eAdd:        return ir::Op::FAdd(vectorType, src.at(0u), src.at(1u));
      case OpCode::eSub:        return ir::Op::FSub(vectorType, src.at(0u), src.at(1u));
      case OpCode::eExp:        return ir::Op::FExp2(vectorType, src.at(0u));
      case OpCode::eFrc:        return ir::Op::FFract(vectorType, src.at(0u));
      case OpCode::eLog:        return ir::Op::FLog2(vectorType, src.at(0u));
      case OpCode::eLogP:       return ir::Op::FLog2(vectorType, src.at(0u));
      case OpCode::eMax:        return ir::Op::FMax(vectorType, src.at(0u), src.at(1u));
      case OpCode::eMin:        return ir::Op::FMin(vectorType, src.at(0u), src.at(1u));
      case OpCode::eMul:        return OpFMul(vectorType, src.at(0u), src.at(1u));
      case OpCode::eRcp:        return ir::Op::FRcp(vectorType, src.at(0u));
      case OpCode::eRsq:        return ir::Op::FRsq(vectorType, src.at(0u));
      case OpCode::eAbs:        return ir::Op::FAbs(vectorType, src.at(0u));
      case OpCode::eSgn:        return ir::Op::FAbs(vectorType, src.at(0u));
      case OpCode::eMad:        return OpFMad(vectorType, src.at(0u), src.at(1u), src.at(2u));
      default: break;
    }

    dxbc_spv_unreachable();
    return ir::Op();
  } ();

  auto resultDef = builder.add(std::move(result));

  if (m_options.fastFloatEmulation) {
    if (opCode == OpCode::eRcp || opCode == OpCode::eRsq || opCode == OpCode::eExp) {
      resultDef = builder.add(ir::Op::FMin(vectorType, resultDef,
        ir::makeTypedConstant(builder, vectorType, std::numeric_limits<float>::max())));
    } else if (opCode == OpCode::eLog || opCode == OpCode::eLogP) {
      resultDef = builder.add(ir::Op::FMax(vectorType, resultDef,
        ir::makeTypedConstant(builder, vectorType, -std::numeric_limits<float>::max())));
    }
  }

  return storeDstModifiedPredicated(builder, op, dst, resultDef);
}


bool Converter::handleDot(ir::Builder& builder, const Instruction& op) {
  /* Dp2/3/4 take two vector operands, produce a scalar, and replicate
   * that in all components included in the destination write mask.
   * Dp2Add takes a third vector operand and adds it.
   * (dst0) Result
   * (src0) First vector
   * (src1) Second vector */
  auto opCode = op.getOpCode();

  dxbc_spv_assert((opCode == OpCode::eDp2Add && op.getSrcCount() == 3u) || op.getSrcCount() == 2u);
  dxbc_spv_assert(op.hasDst());

  /* The opcode determines which source components to read,
   * since the write mask can be literally anything. */
  auto readMask = [opCode] {
    switch (opCode) {
      case OpCode::eDp2Add: return util::makeWriteMaskForComponents(2u);
      case OpCode::eDp3: return util::makeWriteMaskForComponents(3u);
      case OpCode::eDp4: return util::makeWriteMaskForComponents(4u);
      default: break;
    }

    dxbc_spv_unreachable();
    return WriteMask();
  } ();

  /* Load source vectors and pass them to the internal dot instruction as they are */
  const auto& dst = op.getDst();

  auto scalarType = dst.isPartialPrecision() ? ir::ScalarType::eMinF16 : ir::ScalarType::eF32;

  auto vectorA = loadSrcModified(builder, op, op.getSrc(0u), readMask, scalarType);
  auto vectorB = loadSrcModified(builder, op, op.getSrc(1u), readMask, scalarType);

  auto result = builder.add(OpFDot(scalarType, vectorA, vectorB));

  if (opCode == OpCode::eDp2Add) {
    /* src2 needs to have a replicate swizzle, so just get the first component. */
    auto summandC = loadSrcModified(builder, op, op.getSrc(2u), WriteMask(ComponentBit::eX), scalarType);
    result = builder.add(ir::Op::FAdd(scalarType, result, summandC));
  }

  WriteMask writeMask = dst.getWriteMask(getShaderInfo());
  result = broadcastScalar(builder, result, writeMask);
  return storeDstModifiedPredicated(builder, op, dst, result);
}


bool Converter::handleCompare(ir::Builder& builder, const Instruction& op) {
  /* All instructions handled here will operate on float vectors of any kind. */
  auto opCode = op.getOpCode();

  dxbc_spv_assert(op.getSrcCount() == 2u);
  dxbc_spv_assert(op.hasDst());

  /* Instruction type */
  const auto& dst = op.getDst();

  WriteMask writeMask = dst.getWriteMask(getShaderInfo());

  auto scalarType = dst.isPartialPrecision() ? ir::ScalarType::eMinF16 : ir::ScalarType::eF32;

  /* Load source operands */
  auto src0 = loadSrcModified(builder, op, op.getSrc(0u), writeMask, scalarType);
  auto src1 = loadSrcModified(builder, op, op.getSrc(1u), writeMask, scalarType);
  if (!src0 || !src1)
    return false;

  util::small_vector<ir::SsaDef, 4u> components;
  for (auto _ : writeMask) {
    auto index = components.size();
    /* It is done per-component. */
    auto src0c = ir::extractFromVector(builder, src0, index);
    auto src1c = ir::extractFromVector(builder, src1, index);
    ir::SsaDef cond;
    if (opCode == OpCode::eSlt)
      cond = builder.add(ir::Op::FLt(ir::ScalarType::eBool, src0c, src1c));
    else
      cond = builder.add(ir::Op::FGe(ir::ScalarType::eBool, src0c, src1c));

    components.push_back(builder.add(ir::Op::Select(scalarType, cond,
      makeTypedConstant(builder, scalarType, 1.0f),
      makeTypedConstant(builder, scalarType, 0.0f))));
  }
  auto result = buildVector(builder, scalarType, components.size(), components.data());

  return storeDstModifiedPredicated(builder, op, dst, result);
}


bool Converter::handleLit(ir::Builder& builder, const Instruction& op) {
  /* Calculates lighting coefficients from two dot products and an exponent. */
  const auto& dst = op.getDst();
  WriteMask writeMask = dst.getWriteMask(getShaderInfo());
  auto scalarType = dst.isPartialPrecision() ? ir::ScalarType::eMinF16 : ir::ScalarType::eF32;
  auto src = loadSrcModified(builder, op, op.getSrc(0u), writeMask, scalarType);

  auto xConst = builder.makeConstant(0u);
  auto yConst = builder.makeConstant(1u);
  auto wConst = builder.makeConstant(3u);

  auto srcX = builder.add(ir::Op::CompositeExtract(scalarType, src, xConst));
  auto srcY = builder.add(ir::Op::CompositeExtract(scalarType, src, yConst));
  auto srcW = builder.add(ir::Op::CompositeExtract(scalarType, src, wConst));

  /* power = clamp(src.w, -127.9961, 127.9961) */
  auto power = builder.add(ir::Op::FClamp(scalarType, srcW,
    builder.makeConstant(-127.9961f), builder.makeConstant(127.9961f)));

  auto zeroFConst = ir::makeTypedConstant(builder, scalarType, 0.0f);
  auto oneFConst = ir::makeTypedConstant(builder, scalarType, 1.0f);

  util::small_vector<ir::SsaDef, 4u> components;
  if (writeMask & ComponentBit::eX)
    /* dst.x = 1.0 */
    components.push_back(oneFConst);
  if (writeMask & ComponentBit::eY)
    /* dst.y = max(0.0, src.x) */
    components.push_back(builder.add(ir::Op::FMax(scalarType, srcX, zeroFConst)));
  if (writeMask & ComponentBit::eZ)
    /* dst.z = pow(src.y, power) : 0.0 */
    components.push_back(builder.add(ir::Op::FPow(scalarType, srcX, power)));
  if (writeMask & ComponentBit::eW)
    components.push_back(oneFConst);

  if (components.size() > 2u) {
    /* dst.z = src.x > 0.0 && src.y > 0.0 ? pow(src.y, src.w) : 0.0 */

    auto zTestX = builder.add(ir::Op::FGe(ir::ScalarType::eBool, srcX,zeroFConst));
    auto zTestY = builder.add(ir::Op::FGe(ir::ScalarType::eBool, srcY, zeroFConst));
    auto zTest = builder.add(ir::Op::BAnd(ir::ScalarType::eBool, zTestX, zTestY));

    components[2u] = builder.add(ir::Op::Select(scalarType, zTest, components[2u], zeroFConst));
  }

  auto result = buildVector(builder, scalarType, components.size(), components.data());
  return storeDstModifiedPredicated(builder, op, dst, result);
}


bool Converter::handleMatrixArithmetic(ir::Builder& builder, const Instruction& op) {
  /* All instructions handled here will operate on float vectors of any kind. */
  auto opCode = op.getOpCode();

  dxbc_spv_assert(op.getSrcCount() == 2);
  dxbc_spv_assert(op.hasDst());

  uint32_t rowCount;
  uint32_t columnCount;

  switch (opCode) {
    case OpCode::eM3x2:
      columnCount = 3u;
      rowCount = 2u;
      break;
    case OpCode::eM3x3:
      columnCount = 3u;
      rowCount = 3u;
      break;
    case OpCode::eM3x4:
      columnCount = 3u;
      rowCount = 4u;
      break;
    case OpCode::eM4x3:
      columnCount = 4u;
      rowCount = 3u;
      break;
    case OpCode::eM4x4:
      columnCount = 4u;
      rowCount = 4u;
      break;
    default:
      columnCount = 0u;
      rowCount = 0u;
      dxbc_spv_unreachable();
      break;
  }

  /* Instruction type */
  const auto& dst = op.getDst();

  auto scalarType = dst.isPartialPrecision() ? ir::ScalarType::eMinF16 : ir::ScalarType::eF32;

  /* Build a write mask that determines how many components we'll load. */
  WriteMask srcMask = WriteMask((1u << columnCount) - 1u);

  /* Load source operands */
  auto src0 = loadSrcModified(builder, op, op.getSrc(0u), srcMask, scalarType);
  Operand src1Operand = op.getSrc(1u);

  std::array<ir::SsaDef, 4u> components = { };
  for (uint32_t i = 0u; i < rowCount; i++) {
    /* Load matrix column */
    auto src1iOperand = src1Operand;
    src1iOperand.setIndex(src1iOperand.getIndex() + i);
    auto src1 = loadSrcModified(builder, op, src1iOperand, srcMask, scalarType);

    /* Calculate vector component */
    components[i] = builder.add(OpFDot(scalarType, src0, src1));
  }

  auto result = buildVector(builder, scalarType, rowCount, components.data());
  return storeDstModifiedPredicated(builder, op, dst, result);
}


bool Converter::handleBem(ir::Builder& builder, const Instruction& op) {
  /* Apply a fake bump environment-map transform. */
  dxbc_spv_assert(op.getSrcCount() == 2u);
  dxbc_spv_assert(op.hasDst());
  dxbc_spv_assert(!!m_psSharedData);
  auto dst = op.getDst();
  auto scalarType = dst.isPartialPrecision() ? ir::ScalarType::eMinF16 : ir::ScalarType::eF32;
  /* Dst register index determines the bumpmapping stage index. */
  auto stageIdx = dst.getIndex();
  WriteMask writeMask = dst.getWriteMask(m_parser.getShaderInfo());
  /* Write mask must be .xy */
  dxbc_spv_assert(writeMask == WriteMask(ComponentBit::eX | ComponentBit::eY));
  auto src0 = loadSrcModified(builder, op, op.getSrc(0u), writeMask, scalarType);
  auto src1 = loadSrcModified(builder, op, op.getSrc(1u), writeMask, scalarType);

  auto result = applyBumpMapping(builder, stageIdx, src0, src1);
  return storeDstModifiedPredicated(builder, op, dst, result);
}


bool Converter::handleTexCoord(ir::Builder& builder, const Instruction& op) {
  /* Reads texcoord data */
  const auto& dst = op.getDst();
  WriteMask writeMask = dst.getWriteMask(getShaderInfo());
  auto scalarType = dst.isPartialPrecision() ? ir::ScalarType::eMinF16 : ir::ScalarType::eF32;

  if (getShaderInfo().getVersion().first >= 2 || getShaderInfo().getVersion().second >= 4) {
    /* TexCrd (SM 1.4) */
    auto src = loadSrcModified(builder, op, op.getSrc(0u), writeMask, scalarType);
    return storeDstModifiedPredicated(builder, op, dst, src);

  } else {
    /* TexCoord (SM 1.1 - 1.3) */
    ir::BasicType vectorType = makeVectorType(scalarType, writeMask);
    auto src = m_ioMap.emitTexCoordLoad(builder, op, op.getDst().getIndex(), writeMask, Swizzle::identity(), scalarType);

    /* Saturate */
    src = builder.add(ir::Op::FClamp(
      vectorType,
      src,
      makeTypedConstant(builder, vectorType, 0.0f),
      makeTypedConstant(builder, vectorType, 1.0f)
    ));

    /* w = 1.0 */
    if (writeMask & ComponentBit::eW)
      src = builder.add(ir::Op::CompositeInsert(vectorType, src, builder.makeConstant(3u), ir::makeTypedConstant(builder, scalarType, 1.0f)));

    return storeDstModifiedPredicated(builder, op, dst, src);
  }
}


bool Converter::handleTextureSample(ir::Builder& builder, const Instruction& op) {
  ir::SsaDef result = ir::SsaDef();
  auto dst = op.getDst();
  auto opCode = op.getOpCode();
  auto scalarType = dst.isPartialPrecision() ? ir::ScalarType::eMinF16 : ir::ScalarType::eF32;

  switch (opCode) {
    case OpCode::eTexLd: {
      /* Regular texture sampling instruction */
      uint32_t samplerIdx;
      ir::SsaDef texCoord;
      ir::SsaDef lodBias = ir::SsaDef();
      ir::SsaDef lod = ir::SsaDef();

      if (getShaderInfo().getType() == ShaderType::eVertex) {
        lod = builder.makeConstant(0u);
      }

      if (getShaderInfo().getVersion().first <= 1u) {
        dxbc_spv_assert(getShaderInfo().getType() == ShaderType::ePixel);
        samplerIdx = dst.getIndex();
        if (getShaderInfo().getVersion().second >= 4u) {
          /* texld - ps_1_4 */
          auto src0 = op.getSrc(0u);
          texCoord = loadSrcModified(builder, op, src0, ComponentBit::eAll, scalarType);
        } else {
          /* tex - ps_1_1 - ps_1_3 */
          /* The destination register index decides the sampler and texture coord index. */
          texCoord = m_ioMap.emitTexCoordLoad(builder, op, samplerIdx, ComponentBit::eAll, Swizzle::identity(), scalarType);
        }
      } else {
        /* texld - sm_2_0 and up */
        auto src0 = op.getSrc(0u);
        auto src1 = op.getSrc(1u);
        texCoord = loadSrcModified(builder, op, src0, ComponentBit::eAll, scalarType);
        samplerIdx = src1.getIndex();

        switch (op.getTexLdMode()) {
          case TexLdMode::eProject:
            texCoord = m_resources.projectTexCoord(builder, samplerIdx, texCoord, false);
            break;
          case TexLdMode::eBias:
            lodBias = builder.add(ir::Op::CompositeExtract(scalarType, texCoord, builder.makeConstant(3u)));
            break;
          default: break;
        }
      }

      if (m_parser.getShaderInfo().getVersion().first <= 1u && m_parser.getShaderInfo().getVersion().second < 4u)
        texCoord = m_resources.projectTexCoord(builder, samplerIdx, texCoord, true);

      result = m_resources.emitSample(builder, samplerIdx, texCoord, lod, lodBias, ir::SsaDef(), ir::SsaDef(), scalarType);
    } break;

    case OpCode::eTexLdl: {
      /* Sample with explicit LOD */
      auto src0 = op.getSrc(0u);
      auto src1 = op.getSrc(1u);
      auto texCoord = loadSrcModified(builder, op, src0, ComponentBit::eAll, scalarType);
      uint32_t samplerIdx = src1.getIndex();
      auto lod = builder.add(ir::Op::CompositeExtract(scalarType, texCoord, builder.makeConstant(3u)));
      result = m_resources.emitSample(builder, samplerIdx, texCoord, lod, ir::SsaDef(), ir::SsaDef(), ir::SsaDef(), scalarType);
    } break;

    case OpCode::eTexLdd: {
      /* Sample with explicit derivatives */
      auto src0 = op.getSrc(0u);
      auto src1 = op.getSrc(1u);
      auto src2 = op.getSrc(2u);
      auto src3 = op.getSrc(3u);
      auto texCoord = loadSrcModified(builder, op, src0, ComponentBit::eAll, scalarType);
      uint32_t samplerIdx = src1.getIndex();
      auto dx = loadSrcModified(builder, op, src2, ComponentBit::eAll, scalarType);
      auto dy = loadSrcModified(builder, op, src3, ComponentBit::eAll, scalarType);
      result = m_resources.emitSample(builder, samplerIdx, texCoord, ir::SsaDef(), ir::SsaDef(), dx, dy, scalarType);
    } break;

    case OpCode::eTexReg2Ar:
    case OpCode::eTexReg2Gb:
    case OpCode::eTexReg2Rgb: {
      /* Sample with custom values used as texture coords (SM 1) */
      Swizzle swizzle = Swizzle::identity();
      switch (opCode) {
        case OpCode::eTexReg2Ar:  swizzle = Swizzle(Component::eW, Component::eX, Component::eX, Component::eX); break;
        case OpCode::eTexReg2Gb:  swizzle = Swizzle(Component::eY, Component::eZ, Component::eZ, Component::eZ); break;
        case OpCode::eTexReg2Rgb: swizzle = Swizzle(Component::eX, Component::eY, Component::eZ, Component::eZ); break;
        default: dxbc_spv_unreachable(); break;
      }

      auto src0 = op.getSrc(0u);
      auto texCoord = loadSrcModified(builder, op, src0, ComponentBit::eAll, scalarType);
      texCoord = swizzleVector(builder, texCoord, swizzle, ComponentBit::eAll);
      uint32_t samplerIdx = dst.getIndex();

      if (m_parser.getShaderInfo().getVersion().first <= 1u && m_parser.getShaderInfo().getVersion().second < 4u)
        texCoord = m_resources.projectTexCoord(builder, samplerIdx, texCoord, true);

      result = m_resources.emitSample(builder, samplerIdx, texCoord, ir::SsaDef(), ir::SsaDef(), ir::SsaDef(), ir::SsaDef(), scalarType);
    } break;

    case OpCode::eTexM3x2Tex:
    case OpCode::eTexM3x3Tex:
    case OpCode::eTexM3x3:
    case OpCode::eTexM3x2Depth:
    case OpCode::eTexM3x3Spec:
    case OpCode::eTexM3x3VSpec: {
      /* All of those instructions start with a matrix multiplication. */
      const uint32_t rows = opCode == OpCode::eTexM3x2Tex ? 2u : 3u;

      auto src0 = op.getSrc(0u);
      auto n = loadSrcModified(builder, op, src0, util::makeWriteMaskForComponents(3u), scalarType);

      std::array<ir::SsaDef, 4u> components = { };
      uint32_t lastIndex = dst.getIndex();
      for (uint32_t i = 0u; i < components.size(); i++) {
        if (i < rows) {
          auto mi = m_ioMap.emitTexCoordLoad(builder, op, lastIndex + i - rows - 1u, util::makeWriteMaskForComponents(3u), Swizzle::identity(), scalarType);
          components[i] = builder.add(OpFDot(scalarType, mi, n));
        } else {
          // w is defined to be 1.0 in eTexM3x3.
          components[i] = makeTypedConstant(builder, scalarType, 1.0f);
        }
      }

      if (opCode == OpCode::eTexM3x2Tex || opCode == OpCode::eTexM3x3Tex) {

        /* TexM3x2Tex / TexM3x3Tex do a matrix multiplication and use the result as texture coordinate. */
        auto texCoord = buildVector(builder, scalarType, rows, components.data());
        result = m_resources.emitSample(builder, lastIndex, texCoord, ir::SsaDef(), ir::SsaDef(), ir::SsaDef(), ir::SsaDef(), scalarType);

      } else if (opCode == OpCode::eTexM3x2Depth) {

        /* TexM3x2Depth doesn't actually sample. */
        auto z = components.at(0u);
        auto w = components.at(1u);
        result = buildVector(builder, scalarType, components.size(), components.data());
        auto isZero = builder.add(ir::Op::FEq(ir::ScalarType::eBool, w, builder.makeConstantZero(scalarType)));
        auto depth = builder.add(ir::Op::Select(scalarType, isZero,
          makeTypedConstant(builder, scalarType, 1.0f),
          builder.add(ir::Op::FDiv(scalarType, z, w))));
        m_ioMap.emitDepthStore(builder, op, depth);
        /* Docs: After running texm3x2depth, register t(m+1) is no longer available for use in the shader. */

      } else if (opCode == OpCode::eTexM3x3) {

        /* TexM3x3 doesn't actually sample. It just does the matrix multiplication and stores the result. */
        result = buildVector(builder, scalarType, components.size(), components.data());

      } else if (opCode == OpCode::eTexM3x3Spec || opCode == OpCode::eTexM3x3VSpec) {

        /* TexM3x3Spec / TexM3x3VSpec do a matrix multiplication and use the result as the normal vector
         * in a reflection calculation. */
        auto normal = buildVector(builder, scalarType, 3u, components.data());

        ir::SsaDef eyeRay;
        if (opCode == OpCode::eTexM3x3VSpec) {
          std::array<ir::SsaDef, 3u> eyeRayComponents = { };
          for (uint32_t i = 0u; i < eyeRayComponents.size(); i++) {
            eyeRayComponents[i] = m_ioMap.emitTexCoordLoad(builder, op, lastIndex + i - rows - 1u, WriteMask(ComponentBit::eW), Swizzle::identity(), scalarType);
          }
          eyeRay = buildVector(builder, scalarType, eyeRayComponents.size(), components.data());
        } else {
          eyeRay = loadSrcModified(builder, op, op.getSrc(1u), util::makeWriteMaskForComponents(3u), scalarType);
        }

        eyeRay = normalizeVector(builder, eyeRay);
        normal = normalizeVector(builder, normal);

        // 2*[(N*E)/(N*N)]*N - E
        auto vectorType = ir::BasicType(scalarType, 3u);

        auto nDotE = builder.add(OpFDot(scalarType, normal, eyeRay));
        auto nDotN = builder.add(OpFDot(scalarType, normal, normal));
        auto dotDiv = builder.add(ir::Op::FDiv(scalarType, nDotE, nDotN));
        auto twoDotDiv = builder.add(OpFMul(scalarType, makeTypedConstant(builder, scalarType, 2.0f), dotDiv));
        auto texCoord = builder.add(OpFMul(vectorType, normal, broadcastScalar(builder, twoDotDiv, util::makeWriteMaskForComponents(3u))));
        texCoord = builder.add(ir::Op::FSub(vectorType, texCoord, eyeRay));

        /* The sampling function requires a vec4. */
        std::array<ir::SsaDef, 4u> texCoordComponents = {
          builder.add(ir::Op::CompositeExtract(scalarType, texCoord, builder.makeConstant(0u))),
          builder.add(ir::Op::CompositeExtract(scalarType, texCoord, builder.makeConstant(1u))),
          builder.add(ir::Op::CompositeExtract(scalarType, texCoord, builder.makeConstant(2u))),
          makeTypedConstant(builder, scalarType, 0.0f),
        };
        texCoord = buildVector(builder, scalarType, texCoordComponents.size(), texCoordComponents.data());

        result = m_resources.emitSample(builder, dst.getIndex(), texCoord, ir::SsaDef(), ir::SsaDef(), ir::SsaDef(), ir::SsaDef(), scalarType);
      }
    } break;

    case OpCode::eTexDp3Tex:
    case OpCode::eTexDp3: {
      /* Calculate a dot product of the texcoord data and src0 (and optionally use that for 1D texture lookup) */
      auto src0 = op.getSrc(0u);
      auto m = m_ioMap.emitTexCoordLoad(builder, op, dst.getIndex(), util::makeWriteMaskForComponents(3u), Swizzle::identity(), scalarType);
      auto n = loadSrcModified(builder, op, src0, util::makeWriteMaskForComponents(3u), scalarType);
      auto dot = builder.add(OpFDot(scalarType, m, n));
      if (opCode == OpCode::eTexDp3Tex) {
        /* Sample texture at register index of dst using (dot, 0, 0, 0) as coordinates */
        std::array<ir::SsaDef, 4u> texCoordComponents = {
          dot,
          makeTypedConstant(builder, scalarType, 0.0f),
          makeTypedConstant(builder, scalarType, 0.0f),
          makeTypedConstant(builder, scalarType, 0.0f),
        };
        auto texCoord = buildVector(builder, scalarType, texCoordComponents.size(), texCoordComponents.data());
        result = m_resources.emitSample(builder, dst.getIndex(), texCoord, ir::SsaDef(), ir::SsaDef(), ir::SsaDef(), ir::SsaDef(), scalarType);
      } else {
        /* Replicates the dot product to all four color channels. Doesn't actually sample. */
        result = broadcastScalar(builder, dot, util::makeWriteMaskForComponents(4u));
      }
    } break;

    case OpCode::eTexBem:
    case OpCode::eTexBemL: {
      /* Apply a fake bump environment-map transform */
      uint32_t samplerIdx = dst.getIndex();
      auto texCoord = m_ioMap.emitTexCoordLoad(builder, op, samplerIdx, ComponentBit::eAll, Swizzle::identity(), scalarType);
      auto src0 = loadSrcModified(builder, op, op.getSrc(0u), WriteMask(ComponentBit::eX | ComponentBit::eY), scalarType);

      dxbc_spv_assert(getShaderInfo().getVersion().first < 2u);
      texCoord = m_resources.projectTexCoord(builder, samplerIdx, texCoord, true);

      auto bumpMappedTexCoord = applyBumpMapping(builder, samplerIdx, texCoord, src0);

      /* Insert it back into the original tex coord, so we have a z and w component in case we need them. */
      auto u = builder.add(ir::Op::CompositeExtract(scalarType, bumpMappedTexCoord, builder.makeConstant(0u)));
      auto v = builder.add(ir::Op::CompositeExtract(scalarType, bumpMappedTexCoord, builder.makeConstant(1u)));
      texCoord = builder.add(ir::Op::CompositeInsert(ir::BasicType(scalarType, 4u), texCoord, builder.makeConstant(0u), u));
      texCoord = builder.add(ir::Op::CompositeInsert(ir::BasicType(scalarType, 4u), texCoord, builder.makeConstant(1u), v));

      result = m_resources.emitSample(builder, samplerIdx, texCoord, ir::SsaDef(), ir::SsaDef(), ir::SsaDef(), ir::SsaDef(), scalarType);

      if (opCode == OpCode::eTexBemL) {
        /* Additionally does luminance correction */
        auto descriptor = builder.add(ir::Op::DescriptorLoad(ir::ScalarType::eCbv, m_psSharedData, ir::SsaDef()));
        auto bumpEnvLScale = builder.add(ir::Op::BufferLoad(ir::BasicType(ir::ScalarType::eF32, 2u), descriptor, builder.makeConstant(samplerIdx * 5u + 3u), 16u));
        auto bumpEnvLOffset = builder.add(ir::Op::BufferLoad(ir::BasicType(ir::ScalarType::eF32, 2u), descriptor, builder.makeConstant(samplerIdx * 5u + 2u), 8u));
        if (scalarType != ir::ScalarType::eF32) {
          bumpEnvLScale = builder.add(ir::Op::ConsumeAs(ir::BasicType(scalarType, 2u), bumpEnvLScale));
          bumpEnvLOffset = builder.add(ir::Op::ConsumeAs(ir::BasicType(scalarType, 2u), bumpEnvLOffset));
        }
        auto scale = builder.add(ir::Op::CompositeExtract(scalarType, result, builder.makeConstant(2u)));
        scale = builder.add(OpFMul(scalarType, scale, bumpEnvLScale));
        scale = builder.add(ir::Op::FAdd(scalarType, scale, bumpEnvLOffset));
        std::array<ir::SsaDef, 4u> scaledComponents = {};
        scale = builder.add(ir::Op::FClamp(scalarType, scale, ir::makeTypedConstant(builder, scalarType, 0.0f), ir::makeTypedConstant(builder, scalarType, 1.0f)));
        for (uint32_t i = 0u; i < 4u; i++) {
          auto resultComponent = builder.add(ir::Op::CompositeExtract(scalarType, result, builder.makeConstant(i)));
          scaledComponents[i] = builder.add(OpFMul(scalarType, resultComponent, scale));
        }
        result = buildVector(builder, scalarType, scaledComponents.size(), scaledComponents.data());
      }
    } break;

    default: {
      Logger::err("OpCode ", op.getOpCode(), " is not supported by handleTextureSample.");
      dxbc_spv_unreachable();
      return false;
    } break;
  }

  return storeDstModifiedPredicated(builder, op, dst, result);
}


bool Converter::handleTexKill(ir::Builder& builder, const Instruction& op) {
  /* Demotes if any of the first 3 components are less than 0.0. */
  dxbc_spv_assert(op.hasDst());
  auto writeMask = op.getDst().getWriteMask(getShaderInfo());
  writeMask &= ComponentBit::eX | ComponentBit::eY | ComponentBit::eZ;
  ir::SsaDef dst;

  auto [versionMajor, versionMinor] = getShaderInfo().getVersion();
  if (versionMajor <= 1u && versionMinor <= 3u) {
    dst = m_ioMap.emitTexCoordLoad(builder,
      op,
      op.getDst().getIndex(),
      writeMask,
      Swizzle::identity(),
      ir::ScalarType::eF32);
  } else {
    /* Yes, we're loading the dst as a src here. That's not a mistake. */
    dst = loadSrc(builder, op, op.getDst(), writeMask, Swizzle::identity(), ir::ScalarType::eF32);
  }

  ir::SsaDef cond = { };

  for (auto c : writeMask) {
    ir::SsaDef texCoordComponent = ir::extractFromVector(builder, dst, uint32_t(util::componentFromBit(c)));

    auto componentCond = builder.add(ir::Op::FLt(ir::ScalarType::eBool, texCoordComponent, builder.makeConstant(0.0f)));
    if (!cond)
      cond = componentCond;
    else
      cond = builder.add(ir::Op::BOr(ir::ScalarType::eBool, cond, componentCond));
  }

  auto ifDef = builder.add(ir::Op::ScopedIf(ir::SsaDef(), cond));
  builder.add(ir::Op::Demote());
  auto endIf = builder.add(ir::Op::ScopedEndIf(ifDef));
  builder.rewriteOp(ifDef, ir::Op(builder.getOp(ifDef)).setOperand(0u, endIf));
  return true;
}


bool Converter::handleTexDepth(ir::Builder& builder, const Instruction& op) {
  /* Writes the fragment depth */
  /* It always uses temporary register r5. */
  auto val = m_regFile.emitTempLoad(builder, 5u, Swizzle::identity(), ComponentBit::eX | ComponentBit::eY, ir::ScalarType::eF32);
  auto r = builder.add(ir::Op::CompositeExtract(ir::ScalarType::eF32, val, builder.makeConstant(0u)));
  auto g = builder.add(ir::Op::CompositeExtract(ir::ScalarType::eF32, val, builder.makeConstant(1u)));
  /* depth = r5.r / r5.g */
  auto depth = builder.add(ir::Op::FDiv(ir::ScalarType::eF32, r, g));
  /* if r5.g = 0, the result of r5.r / r5.g = 1.0. */
  auto cond = builder.add(ir::Op::FNe(ir::ScalarType::eF32, g, builder.makeConstant(0.0f)));
  depth = builder.add(ir::Op::Select(ir::ScalarType::eF32, cond, depth, builder.makeConstant(1.0f)));
  return m_ioMap.emitDepthStore(builder, op, depth);
}


ir::SsaDef Converter::loadSrc(ir::Builder& builder, const Instruction& op, const Operand& operand, WriteMask mask, Swizzle swizzle, ir::ScalarType type) {
  auto loadDef = ir::SsaDef();

  dxbc_spv_assert(!operand.hasRelativeAddressing()
    || operand.getRegisterType() == RegisterType::eInput
    || operand.getRegisterType() == RegisterType::eConst
    || operand.getRegisterType() == RegisterType::eConst2
    || operand.getRegisterType() == RegisterType::eConst3
    || operand.getRegisterType() == RegisterType::eConst4
    || (operand.getRegisterType() == RegisterType::eOutput && getShaderInfo().getType() == ShaderType::eVertex));

  switch (operand.getRegisterType()) {
    case RegisterType::eInput:
    case RegisterType::ePixelTexCoord:
    case RegisterType::eMiscType:
      loadDef = m_ioMap.emitLoad(builder, op, operand, mask, swizzle, type);
      break;

    case RegisterType::eAddr:
    /* case RegisterType::eTexture: Same Value */
      if (getShaderInfo().getType() == ShaderType::eVertex)
        /* RegisterType::eAddr */
        logOpError(op, "Address register cannot be loaded as a regular source register.");
      else
        loadDef = m_ioMap.emitLoad(builder, op, operand, mask, swizzle, type); /* RegisterType::eTexture */
      break;

    case RegisterType::eTemp:
    case RegisterType::eLoop:
      loadDef = m_regFile.emitTempLoad(builder,
        operand.getIndex(),
        swizzle,
        mask,
        type);
      break;

    case RegisterType::ePredicate:
      logOpError(op, "Predicate cannot be loaded as a regular source register.");
      break;

    case RegisterType::eConst:
    case RegisterType::eConst2:
    case RegisterType::eConst3:
    case RegisterType::eConst4:
    case RegisterType::eConstInt:
    case RegisterType::eConstBool:
      loadDef = m_resources.emitConstantLoad(builder, op, operand, mask, type);
      break;

    default:
      break;
  }

  if (!loadDef) {
    auto name = makeRegisterDebugName(operand.getRegisterType(), 0u, WriteMask());
    logOpError(op, "Failed to load operand: ", name);
    return loadDef;
  }

  return loadDef;
}


bool Converter::handleSelect(ir::Builder& builder, const Instruction& op) {
  dxbc_spv_assert(op.getSrcCount() == 3u);
  dxbc_spv_assert(op.hasDst());

  auto dst = op.getDst();
  WriteMask writeMask = dst.getWriteMask(m_parser.getShaderInfo());
  auto scalarType = dst.isPartialPrecision() ? ir::ScalarType::eMinF16 : ir::ScalarType::eF32;

  auto src0 = loadSrcModified(builder, op, op.getSrc(0u), writeMask, scalarType);
  auto src1 = loadSrcModified(builder, op, op.getSrc(1u), writeMask, scalarType);
  auto src2 = loadSrcModified(builder, op, op.getSrc(2u), writeMask, scalarType);

  auto type = makeVectorType(scalarType, writeMask);

  ir::SsaDef result;
  if (op.getOpCode() == OpCode::eLrp) {
    /* dest = src0 * (src1 - src2) + src2 */
    result = builder.add(ir::Op::FSub(type, src1, src2));
    result = builder.add(ir::Op::FAdd(type, result, src2));
    result = builder.add(OpFMul(type, src0, result));
  } else if (op.getOpCode() == OpCode::eCmp || op.getOpCode() == OpCode::eCnd) {
    util::small_vector<ir::SsaDef, 4u> components;
    for (auto _ : writeMask) {
      uint32_t componentIndex = components.size();
      auto conditionComponent = ir::extractFromVector(builder, src0, componentIndex);
      auto option1Component = ir::extractFromVector(builder, src1, componentIndex);
      auto option2Component = ir::extractFromVector(builder, src2, componentIndex);

      ir::SsaDef conditionBool;
      if (op.getOpCode() == OpCode::eCmp) {
        /* Cmp compares to 0.0 */
        conditionBool = builder.add(ir::Op::FGe(ir::ScalarType::eBool, conditionComponent, makeTypedConstant(builder, scalarType, 0.0f)));
      } else {
        /* Cnd compares to 0.5 */
        conditionBool = builder.add(ir::Op::FGt(ir::ScalarType::eBool, conditionComponent, makeTypedConstant(builder, scalarType, 0.5f)));
      }
      components.push_back(builder.add(ir::Op::Select(scalarType, conditionBool, option1Component, option2Component)));
    }
    result = ir::buildVector(builder, scalarType, components.size(), components.data());
  } else {
    Logger::err("OpCode ", op.getOpCode(), " is not supported by handleSelect.");
    dxbc_spv_unreachable();
    return false;
  }

  return storeDstModifiedPredicated(builder, op, dst, result);
}


bool Converter::handleNrm(ir::Builder& builder, const Instruction& op) {
  dxbc_spv_assert(op.getSrcCount() == 1u);
  dxbc_spv_assert(op.hasDst());

  auto dst = op.getDst();
  WriteMask writeMask = dst.getWriteMask(m_parser.getShaderInfo());
  auto scalarType = dst.isPartialPrecision() ? ir::ScalarType::eMinF16 : ir::ScalarType::eF32;

  auto src0 = loadSrcModified(builder, op, op.getSrc(0u), writeMask, scalarType);
  auto result = normalizeVector(builder, src0);

  if (m_options.fastFloatEmulation) {
    auto vectorType = ir::makeVectorType(scalarType, writeMask);
    result = builder.add(ir::Op::FMin(vectorType, result,
      ir::makeTypedConstant(builder, vectorType, std::numeric_limits<float>::max())));
  }

  return storeDstModifiedPredicated(builder, op, dst, result);
}


bool Converter::handleSinCos(ir::Builder& builder, const Instruction& op) {
  /* dst.x = cos(src)
   * dst.y = sin(src)
   * Before PS 3_0, shaders had to provide specific consts as src1 and src2. */
  uint32_t majorVersion = getShaderInfo().getVersion().first;
  dxbc_spv_assert((majorVersion >= 3u && op.getSrcCount() == 1u) || op.getSrcCount() == 3u);
  dxbc_spv_assert(op.hasDst());

  auto dst = op.getDst();
  Swizzle swizzle = op.getSrc(0u).getSwizzle(getShaderInfo());
  dxbc_spv_assert(swizzle.x() == swizzle.y() && swizzle.y() == swizzle.z() && swizzle.z() == swizzle.w());
  WriteMask writeMask = dst.getWriteMask(getShaderInfo());

  auto scalarType = dst.isPartialPrecision() ? ir::ScalarType::eMinF16 : ir::ScalarType::eF32;
  auto val = loadSrcModified(builder, op, op.getSrc(0u), ComponentBit::eX, scalarType);

  dxbc_spv_assert((writeMask & (ComponentBit::eZ | ComponentBit::eW)) == WriteMask());
  util::small_vector<ir::SsaDef, 2u> components = { };
  if (writeMask & ComponentBit::eX)
    components.push_back(builder.add(ir::Op::FCos(scalarType, val)));
  if (writeMask & ComponentBit::eY)
    components.push_back(builder.add(ir::Op::FSin(scalarType, val)));

  auto vec = buildVector(builder, scalarType, components.size(), components.data());
  return storeDstModifiedPredicated(builder, op, dst, vec);
}


bool Converter::handlePow(ir::Builder& builder, const Instruction& op) {
  /* abs(src0)^src1 */
  dxbc_spv_assert(op.getSrcCount() == 2u);
  dxbc_spv_assert(op.hasDst());

  auto dst = op.getDst();
  auto scalarType = dst.isPartialPrecision() ? ir::ScalarType::eMinF16 : ir::ScalarType::eF32;
  WriteMask writeMask = dst.getWriteMask(getShaderInfo());
  Swizzle src0Swizzle = op.getSrc(0u).getSwizzle(getShaderInfo());
  Swizzle src1Swizzle = op.getSrc(1u).getSwizzle(getShaderInfo());

  /* The swizzles must be replicate swizzles. */
  dxbc_spv_assert(src0Swizzle.x() == src0Swizzle.y() && src0Swizzle.y() == src0Swizzle.z() && src0Swizzle.z() == src0Swizzle.w());
  dxbc_spv_assert(src1Swizzle.x() == src1Swizzle.y() && src1Swizzle.y() == src1Swizzle.z() && src1Swizzle.z() == src1Swizzle.w());
  auto src0 = loadSrcModified(builder, op, op.getSrc(0u), ComponentBit::eX, scalarType);
  auto src1 = loadSrcModified(builder, op, op.getSrc(1u), ComponentBit::eX, scalarType);

  auto absSrc0 = builder.add(ir::Op::FAbs(scalarType, src0));
  auto val = builder.add(OpFPow(scalarType, absSrc0, src1));
  auto vec = broadcastScalar(builder, val, writeMask);
  return storeDstModifiedPredicated(builder, op, dst, vec);
}


bool Converter::handleDst(ir::Builder& builder, const Instruction& op) {
  /* Calculates a distance vector */
  dxbc_spv_assert(op.hasDst());
  dxbc_spv_assert(op.getSrcCount() == 2u);
  auto dst = op.getDst();
  WriteMask writeMask = dst.getWriteMask(m_parser.getShaderInfo());
  auto scalarType = dst.isPartialPrecision() ? ir::ScalarType::eMinF16 : ir::ScalarType::eF32;

  auto src0 = loadSrcModified(builder, op, op.getSrc(0u), ComponentBit::eAll, scalarType);
  auto src1 = loadSrcModified(builder, op, op.getSrc(1u), ComponentBit::eAll, scalarType);
  util::small_vector<ir::SsaDef, 4u> components = { };

  if (writeMask & ComponentBit::eX) {
    /* dst.x = 1.0 */
    components.push_back(makeTypedConstant(builder, scalarType, 1.0f));
  }
  if (writeMask & ComponentBit::eY) {
    /* dst.y = src0.y * src1.y */
    auto src0y = builder.add(ir::Op::CompositeExtract(scalarType, src0, builder.makeConstant(1u)));
    auto src1y = builder.add(ir::Op::CompositeExtract(scalarType, src1, builder.makeConstant(1u)));
    components.push_back(builder.add(OpFMul(scalarType, src0y, src1y)));
  }
  if (writeMask & ComponentBit::eZ) {
    /* dst.z = src0.z */
    auto src0z = builder.add(ir::Op::CompositeExtract(scalarType, src0, builder.makeConstant(2u)));
    components.push_back(src0z);
  }
  if (writeMask & ComponentBit::eW) {
    /* dst.w = src1.w */
    auto src1w = builder.add(ir::Op::CompositeExtract(scalarType, src1, builder.makeConstant(3u)));
    components.push_back(src1w);
  }

  auto result = buildVector(builder, scalarType, components.size(), components.data());
  return storeDstModifiedPredicated(builder, op, dst, result);
}


bool Converter::handleDerivatives(ir::Builder& builder, const Instruction& op) {
  dxbc_spv_assert(op.hasDst());
  dxbc_spv_assert(op.getSrcCount() == 1u);
  auto dst = op.getDst();
  WriteMask writeMask = dst.getWriteMask(m_parser.getShaderInfo());
  auto scalarType = dst.isPartialPrecision() ? ir::ScalarType::eMinF16 : ir::ScalarType::eF32;

  auto src0 = loadSrcModified(builder, op, op.getSrc(0u), ComponentBit::eAll, scalarType);

  auto type = makeVectorType(scalarType, writeMask);
  if (op.getOpCode() == OpCode::eDsX) {
    builder.add(ir::Op::DerivX(type, src0, ir::DerivativeMode::eDefault));
    return true;
  } else if (op.getOpCode() == OpCode::eDsY) {
    builder.add(ir::Op::DerivY(type, src0, ir::DerivativeMode::eDefault));
    return true;
  } else {
    Logger::err("OpCode ", op.getOpCode(), " is not supported by handleDerivatives.");
    dxbc_spv_unreachable();
    return false;
  }
}


bool Converter::handleCrs(ir::Builder& builder, const Instruction& op) {
  /* crs dst, src0, src1
   * dest.x = src0.y * src1.z - src0.z * src1.y;
   * dest.y = src0.z * src1.x - src0.x * src1.z;
   * dest.z = src0.x * src1.y - src0.y * src1.x;
   *
   * src0 & src1 must have the default swizzle.
   * Dst must have one of the following write masks: .x | .y | .z | .xy | .xz | .yz | .xyz.
   */
  dxbc_spv_assert(op.hasDst());
  dxbc_spv_assert(op.getSrcCount() == 2u);
  auto dst = op.getDst();
  WriteMask writeMask = dst.getWriteMask(m_parser.getShaderInfo());
  auto scalarType = dst.isPartialPrecision() ? ir::ScalarType::eMinF16 : ir::ScalarType::eF32;

  auto src0 = loadSrcModified(builder, op, op.getSrc(0u), ComponentBit::eX | ComponentBit::eY | ComponentBit::eZ, scalarType);
  auto src1 = loadSrcModified(builder, op, op.getSrc(1u), ComponentBit::eX | ComponentBit::eY | ComponentBit::eZ, scalarType);

  auto src0x = builder.add(ir::Op::CompositeExtract(scalarType, src0, builder.makeConstant(0u)));
  auto src0y = builder.add(ir::Op::CompositeExtract(scalarType, src0, builder.makeConstant(1u)));
  auto src0z = builder.add(ir::Op::CompositeExtract(scalarType, src0, builder.makeConstant(2u)));
  auto src1x = builder.add(ir::Op::CompositeExtract(scalarType, src1, builder.makeConstant(0u)));
  auto src1y = builder.add(ir::Op::CompositeExtract(scalarType, src1, builder.makeConstant(1u)));
  auto src1z = builder.add(ir::Op::CompositeExtract(scalarType, src1, builder.makeConstant(2u)));

  util::small_vector<ir::SsaDef, 4u> components;
  if (writeMask & ComponentBit::eX) {
    auto a = builder.add(OpFMul(scalarType, src0y, src1z));
    auto b = builder.add(OpFMul(scalarType, src0z, src1y));
    components.push_back(builder.add(ir::Op::FSub(scalarType, a, b)));
  }
  if (writeMask & ComponentBit::eY) {
    auto a = builder.add(OpFMul(scalarType, src0z, src1x));
    auto b = builder.add(OpFMul(scalarType, src0x, src1z));
    components.push_back(builder.add(ir::Op::FSub(scalarType, a, b)));
  }
  if (writeMask & ComponentBit::eZ) {
    auto a = builder.add(OpFMul(scalarType, src0x, src1y));
    auto b = builder.add(OpFMul(scalarType, src0y, src1x));
    components.push_back(builder.add(ir::Op::FSub(scalarType, a, b)));
  }
  auto res = composite(builder, makeVectorType(scalarType, writeMask), components.data(), Swizzle::identity(), writeMask);
  return storeDstModifiedPredicated(builder, op, dst, res);
}


bool Converter::handleSetP(ir::Builder& builder, const Instruction& op) {
  /* Assigns the result of a comparison to the predicate register */
  dxbc_spv_assert(op.hasDst());
  dxbc_spv_assert(op.getSrcCount() == 2u);
  auto dst = op.getDst();
  WriteMask writeMask = dst.getWriteMask(m_parser.getShaderInfo());
  auto scalarType = dst.isPartialPrecision() ? ir::ScalarType::eMinF16 : ir::ScalarType::eF32;

  auto src0 = loadSrcModified(builder, op, op.getSrc(0u), writeMask, scalarType);
  auto src1 = loadSrcModified(builder, op, op.getSrc(1u), writeMask, scalarType);

  util::small_vector<ir::SsaDef, 4u> components;
  for (auto _ : writeMask) {
    /* The comparison is done for each component separately. */
    ir::SsaDef src0c = ir::extractFromVector(builder, src0, uint32_t(components.size()));
    ir::SsaDef src1c = ir::extractFromVector(builder, src1, uint32_t(components.size()));

    auto comparison = emitComparison(builder, src0c, src1c, op.getComparisonMode());
    components.push_back(comparison);
  }

  auto result = composite(builder, makeVectorType(scalarType, writeMask), components.data(), Swizzle::identity(), writeMask);
  return storeDstModifiedPredicated(builder, op, dst, result);
}


ir::SsaDef Converter::applySrcModifiers(ir::Builder& builder, ir::SsaDef def, const Instruction& instruction, const Operand& operand, WriteMask mask) {
  auto modifiedDef = def;

  const auto& op = builder.getOp(def);
  auto type = op.getType().getBaseType(0u);
  bool isUnknown = type.isUnknownType();
  bool partialPrecision = instruction.hasDst() && instruction.getDst().isPartialPrecision();

  if (!type.isFloatType()) {
    type = ir::BasicType(partialPrecision ? ir::ScalarType::eMinF16 : ir::ScalarType::eF32, type.getVectorSize());
    modifiedDef = builder.add(ir::Op::ConsumeAs(type, modifiedDef));
  }

  auto mod = operand.getModifier();

  switch (mod) {
    case OperandModifier::eAbs: /* abs(r) */
    case OperandModifier::eAbsNeg: /* -abs(r) */
      modifiedDef = builder.add(ir::Op::FAbs(type, modifiedDef));

      if (mod == OperandModifier::eAbsNeg)
        modifiedDef = builder.add(ir::Op::FNeg(type, modifiedDef));
      break;

    case OperandModifier::eBias: /* r - 0.5 */
    case OperandModifier::eBiasNeg: { /* -(r - 0.5) */
      auto halfConst = ir::makeTypedConstant(builder, type, 0.5f);
      modifiedDef = builder.add(ir::Op::FSub(type, modifiedDef, halfConst));

      if (mod == OperandModifier::eBiasNeg)
        modifiedDef = builder.add(ir::Op::FNeg(type, modifiedDef));
    } break;

    case OperandModifier::eSign: /* fma(r, 2.0, -1.0) */
    case OperandModifier::eSignNeg: { /* -fma(r, 2.0, -1.0) */
      auto twoConst = ir::makeTypedConstant(builder, type, 2.0f);
      auto minusOneConst = ir::makeTypedConstant(builder, type, -1.0f);
      modifiedDef = builder.add(ir::Op::FMad(type, modifiedDef, twoConst, minusOneConst));

      if (mod == OperandModifier::eSignNeg)
        modifiedDef = builder.add(ir::Op::FNeg(type, modifiedDef));
    } break;

    case OperandModifier::eComp: { /* 1.0 - r */
      ir::SsaDef oneConst = ir::makeTypedConstant(builder, type, 1.0f);
      modifiedDef = builder.add(ir::Op::FSub(type, oneConst, modifiedDef));
    } break;

    case OperandModifier::eX2: /* r * 2.0 */
    case OperandModifier::eX2Neg: { /* -(r * 2.0) */
      ir::SsaDef twoConst = ir::makeTypedConstant(builder, type, 2.0f);
      modifiedDef = builder.add(ir::Op::FMul(type, modifiedDef, twoConst));

      if (mod == OperandModifier::eX2Neg)
        modifiedDef = builder.add(ir::Op::FAbs(type, modifiedDef));
    } break;

    case OperandModifier::eDz:
    case OperandModifier::eDw: {
      /* The Dz and Dw modifiers can only be applied to SM1.4 TexLd & TexCrd instructions.
       * Both of those only accept a texture coord register as argument and that is always
       * a float vec4. */
      uint32_t fullVec4ComponentIndex = mod == OperandModifier::eDz ? 2u : 3u;
      uint32_t componentIndex = 0u;

      for (auto c : mask) {
        if (util::componentFromBit(c) == Component(fullVec4ComponentIndex))
          break;

        componentIndex++;
      }

      auto indexConst = builder.makeConstant(componentIndex);
      auto zComp = builder.add(ir::Op::CompositeExtract(type.getBaseType(), modifiedDef, indexConst));
      auto zCompVec = ir::broadcastScalar(builder, zComp, mask);
      modifiedDef = builder.add(ir::Op::FDiv(type, modifiedDef, zCompVec));
    } break;

    case OperandModifier::eNeg:
      modifiedDef = builder.add(ir::Op::FNeg(type, modifiedDef));
    break;

    case OperandModifier::eNone:
      break;

    default:
      Logger::log(LogLevel::eError, "Unknown source register modifier: ", uint32_t(mod));
      break;
  }

  if (isUnknown) {
    type = ir::BasicType(ir::ScalarType::eUnknown, type.getVectorSize());
    modifiedDef = builder.add(ir::Op::ConsumeAs(type, modifiedDef));
  }

  return modifiedDef;
}


ir::SsaDef Converter::loadSrcModified(ir::Builder& builder, const Instruction& op, const Operand& operand, WriteMask mask, ir::ScalarType type) {
  Swizzle swizzle = operand.getSwizzle(getShaderInfo());
  Swizzle originalSwizzle = swizzle;
  WriteMask originalMask = mask;
  /* If the modifier divides by one of the components, that component needs to be loaded. */

  /* Dz & Dw need to get applied before the swizzle!
   * So if those are used, we load the whole vector and swizzle afterward. */
  bool hasPreSwizzleModifier = operand.getModifier() == OperandModifier::eDz || operand.getModifier() == OperandModifier::eDw;
  if (hasPreSwizzleModifier) {
    mask = WriteMask(ComponentBit::eAll);
    swizzle = Swizzle::identity();
  }

  auto value = loadSrc(builder, op, operand, mask, swizzle, type);
  auto modified = applySrcModifiers(builder, value, op, operand, mask);

  if (hasPreSwizzleModifier) {
    modified = swizzleVector(builder, modified, originalSwizzle, originalMask);
  }

  return modified;
}


bool Converter::storeDst(ir::Builder& builder, const Instruction& op, const Operand& operand, ir::SsaDef predicateVec, ir::SsaDef value) {
  WriteMask writeMask = operand.getWriteMask(getShaderInfo());
  writeMask = fixupWriteMask(builder, writeMask, value);

  switch (operand.getRegisterType()) {
    case RegisterType::eTemp:
      return m_regFile.emitStore(builder, operand, writeMask, predicateVec, value);

    case RegisterType::eAddr:
      if (getShaderInfo().getType() == ShaderType::eVertex)
        return m_regFile.emitStore(builder, operand, writeMask, predicateVec, value);
      else
        return m_ioMap.emitStore(builder, op, operand, writeMask, predicateVec, value);

    case RegisterType::eOutput:
    case RegisterType::eRasterizerOut:
    case RegisterType::eAttributeOut:
    case RegisterType::eColorOut:
    case RegisterType::eDepthOut:
      return m_ioMap.emitStore(builder, op, operand, writeMask, predicateVec, value);

    default: {
      auto name = makeRegisterDebugName(operand.getRegisterType(), 0u, writeMask);
      logOpError(op, "Unhandled destination operand: ", name);
    } return false;
  }

  return false;
}


ir::SsaDef Converter::applyDstModifiers(ir::Builder& builder, ir::SsaDef def, const Instruction& instruction, const Operand& operand) {
  ir::Op op = builder.getOp(def);
  auto type = op.getType().getBaseType(0u);
  int8_t shift = operand.getShift();

  /* Handle unknown type */
  if (type.isUnknownType() && (shift != 0 || operand.isSaturated())) {
    type = ir::BasicType(ir::ScalarType::eF32, type.getVectorSize());
    def = builder.add(ir::Op::ConsumeAs(type, def));
  }

  /* Apply shift */
  if (shift != 0) {
    if (!type.isFloatType()) {
      logOpMessage(LogLevel::eWarn, instruction, "Shift applied to a non-float result.");
    }

    float shiftAmount = shift < 0
            ? 1.0f / (1 << -shift)
            : float(1 << shift);

    def = builder.add(ir::Op::FMul(type, def, makeTypedConstant(builder, type, shiftAmount)));
  }

  /* Saturate dst */
  if (operand.isSaturated()) {
    if (!type.isFloatType()) {
      logOpMessage(LogLevel::eWarn, instruction, "Saturation applied to a non-float result.");
    }

    def = builder.add(ir::Op::FClamp(type, def,
      makeTypedConstant(builder, type, 0.0f),
      makeTypedConstant(builder, type, 1.0f)));
  }

  return def;
}


bool Converter::storeDstModifiedPredicated(ir::Builder& builder, const Instruction& op, const Operand& operand, ir::SsaDef value) {
  value = applyDstModifiers(builder, value, op, operand);

  ir::SsaDef predicate = ir::SsaDef();
  if (operand.isPredicated()) {
    /* Make sure we're not trying to load more predicate components than we can write. */
    WriteMask writeMask = operand.getWriteMask(getShaderInfo());
    writeMask = fixupWriteMask(builder, writeMask, value);

    /* Load predicate */
    predicate = m_regFile.emitPredicateLoad(builder, operand.getPredicateSwizzle(), writeMask);
    /* Apply predicate modifier (per component) if necessary */
    if (operand.getPredicateModifier() == OperandModifier::eNot) {
      util::small_vector<ir::SsaDef, 4u> components = { };
      for (auto c : writeMask) {
        auto component = componentFromBit(c);
        auto predicateComponent = ir::extractFromVector(builder, predicate, uint32_t(component));
        components.push_back(builder.add(ir::Op::BNot(ir::ScalarType::eBool, predicateComponent)));
      }
      predicate = buildVector(builder, ir::ScalarType::eBool, components.size(), components.data());
    } else if (operand.getPredicateModifier() != OperandModifier::eNone) {
      Logger::log(LogLevel::eError, "Unknown predicate modifier: ", uint32_t(operand.getPredicateModifier()));
      dxbc_spv_assert(false);
    }
  }

  return storeDst(builder, op, operand, predicate, value);
}


WriteMask Converter::fixupWriteMask(ir::Builder& builder, WriteMask writeMask, ir::SsaDef value) {
  /* Fix the write mask if it writes more components than the value has. */
  auto type = builder.getOp(value).getType().getBaseType(0u);

  if (util::popcnt(uint8_t(writeMask)) == type.getVectorSize()) {
    return writeMask;
  }

  /* Scalar values should be turned into vectors with broadcastScalar instead. */
  dxbc_spv_assert(type.getVectorSize() != 1u);

  WriteMask oldWriteMask = writeMask;

  WriteMask maxWriteMask = WriteMask(uint8_t((1u << type.getVectorSize()) - 1u) << util::tzcnt(uint8_t(writeMask)));
  writeMask &= maxWriteMask;

  if (writeMask != oldWriteMask) {
    Logger::warn("Disabling components in write mask because computed value does not have them: Before: ",
      oldWriteMask, ", after: ", writeMask);
  }

  return writeMask;
}


ir::SsaDef Converter::loadAddress(ir::Builder& builder, RegisterType registerType, Swizzle swizzle) {
  return m_regFile.emitAddressLoad(builder, registerType, swizzle);
}


void Converter::logOp(LogLevel severity, const Instruction& op) const {
  Disassembler::Options options = { };
  options.indent = false;
  options.lineNumbers = false;

  Disassembler disasm(options, getShaderInfo());
  auto instruction = disasm.disassembleOp(op, m_ctab);

  Logger::log(severity, "Line ", m_instructionCount, ": ", instruction);
}


std::string Converter::makeRegisterDebugName(RegisterType type, uint32_t index, WriteMask mask) const {
  auto shaderInfo = getShaderInfo();

  std::stringstream name;
  name << UnambiguousRegisterType { type, shaderInfo.getType(), shaderInfo.getVersion().first };

  const ConstantInfo* constantInfo = m_ctab.findConstantInfo(type, index);

  if (constantInfo != nullptr && m_options.includeDebugNames) {
    name << "_" << constantInfo->name;
    if (constantInfo->count > 1u) {
      name << index - constantInfo->index;
    }
  } else {
    if (type == RegisterType::eMiscType) {
      name << MiscTypeIndex(index);
    } else if (type == RegisterType::eRasterizerOut) {
      name << RasterizerOutIndex(index);
    } else if (type != RegisterType::eLoop && type != RegisterType::ePredicate) {
      name << index;
    }

    if (mask && mask != WriteMask(ComponentBit::eAll)) {
      name << "_" << mask;
    }
  }

  return name.str();
}

}
