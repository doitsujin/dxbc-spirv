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
, m_options(options) {

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

  /* Set cursor to main function so that instructions will be emitted
   * in the correct location */
  builder.setCursor(m_entryPoint.mainFunc);
  return true;
}


bool Converter::finalize(ir::Builder& builder, ShaderType shaderType) {
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
