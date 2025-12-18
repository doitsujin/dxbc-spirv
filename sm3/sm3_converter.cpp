#include "sm3_converter.h"

#include "sm3_disasm.h"

#include "../ir/ir_utils.h"

namespace dxbc_spv::sm3 {

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
  const Options& options)
: m_code(code)
, m_options(options)
, m_ioMap(*this)
, m_regFile(*this) {

}

Converter::~Converter() {

}

bool Converter::convertShader(ir::Builder& builder) {
  if (!initParser(m_parser, m_code))
    return false;

  auto shaderType = getShaderInfo().getType();

  initialize(builder, shaderType);

  while (m_parser) {
    Instruction op = m_parser.parseInstruction();

    /* Execute the actual instruction. */
    if (!op || !convertInstruction(builder, op))
      return false;
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
    case OpCode::eDef:
    case OpCode::eDefI:
    case OpCode::eDefB:
    case OpCode::eDcl:
    case OpCode::eMov:
    case OpCode::eMova:
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
    case OpCode::eDp2Add:
    case OpCode::eDp3:
    case OpCode::eDp4:
    case OpCode::eSlt:
    case OpCode::eSge:
    case OpCode::eLit:
    case OpCode::eM4x4:
    case OpCode::eM4x3:
    case OpCode::eM3x4:
    case OpCode::eM3x3:
    case OpCode::eM3x2:
    case OpCode::eBem:
    case OpCode::eTexCrd:
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

  m_ioMap.initialize(builder);
  m_regFile.initialize(builder);

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
    case RegisterType::eAddr:
    case RegisterType::eTemp:
    case RegisterType::eLoop:
    case RegisterType::ePredicate:
    case RegisterType::eConst:
    case RegisterType::eConst2:
    case RegisterType::eConst3:
    case RegisterType::eConst4:
    case RegisterType::eConstInt:
    case RegisterType::eConstBool:
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
    case RegisterType::eAddr:
    case RegisterType::eOutput:
    case RegisterType::eRasterizerOut:
    case RegisterType::eAttributeOut:
    case RegisterType::eColorOut:
    case RegisterType::eDepthOut:
      break;

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
    /* Predication is unimplemented. */
    dxbc_spv_assert(false);
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
