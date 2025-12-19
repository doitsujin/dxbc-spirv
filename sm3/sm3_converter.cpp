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
    case OpCode::eTexKill:
    case OpCode::eTexDepth:
    case OpCode::eLrp:
    case OpCode::eCmp:
    case OpCode::eCnd:
    case OpCode::eNrm:
    case OpCode::eSinCos:
    case OpCode::ePow:
    case OpCode::eDst:
    case OpCode::eDsX:
    case OpCode::eDsY:
    case OpCode::eCrs:
    case OpCode::eSetP:
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
