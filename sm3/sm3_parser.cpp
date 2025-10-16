#include "sm3_parser.h"

#include "../util/util_log.h"

namespace dxbc_spv::sm3 {

/* If the operands differ between shading model versions, use the latest ones. */
static const std::array<InstructionLayout, 100> g_instructionLayouts = {{
  /* Nop */
  { },
  /* Mov */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Add */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Sub */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
 /* Mad */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Mul */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Rcp */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Rsq */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Dp3 */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Dp4 */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Min */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Max */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Slt */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Sge */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Exp */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Log */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Lit */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Dst */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Lrp */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Frc */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* M4x4 */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* M4x3 */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* M3x4 */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* M3x3 */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* M3x2 */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Call */
  { {{
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown },
  }} },
  /* CallNz */
  { {{
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown },
    { OperandKind::eSrcReg, ir::ScalarType::eBool },
  }} },
  /* Loop */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
  }} },
  /* Ret */
{ },
  /* EndLoop */
{ },
  /* Label */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown },
  }} },
  /* Dcl */
  { {{
    { OperandKind::eDcl,    ir::ScalarType::eUnknown },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown },
  }} },
  /* Pow */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Crs */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Sgn */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Abs */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Nrm */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* SinCos */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Rep */
  { {{
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
  }} },
  /* EndRep */
{ },
  /* If */
{ {{
  { OperandKind::eSrcReg, ir::ScalarType::eBool },
  }} },
  /* IfC */
  { {{
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Else */
  { },
  /* EndIf */
  { },
  /* Break */
  { },
  /* BreakC */
  { {{
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Mova */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown },
  }} },
  /* DefB */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eImm32,  ir::ScalarType::eBool },
  }} },
  /* DefI */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eI32 },
    { OperandKind::eImm32,  ir::ScalarType::eI32 },
  }} },

  /* TexCrd. Same opcode as the SM<1.4 instruction 'texcoord'. */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* TexKill */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
  }} },
  /* TexLd. Same opcode as the SM<1.4 instruction 'tex'. */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 }, // Only on SM2+
    { OperandKind::eSrcReg, ir::ScalarType::eSampler }, // Only on SM2+
  }} },
  /* TexBem */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* TexBemL */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* TexReg2Ar */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* TexReg2Gb */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* TexM3x2Pad */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* TexM3x2Tex */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* TexM3x3Pad */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* TexM3x3Tex */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Reserved0 */
{ },
  /* TexM3x3Spec */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* TexM3x3VSpec */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* ExpP */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* LogP */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Cnd */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Def */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eImm32,  ir::ScalarType::eF32 },
  }} },
  /* TexReg2Rgb */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* TexDp3Tex */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* TexM3x2Depth */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* TexDp3 */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* TexM3x3 */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* TexDepth */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
  }} },
  /* Cmp */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Bem */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Dp2Add */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* DsX */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* DsY */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* TexLdd */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* SetP */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* TexLdl */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* BreakP */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
  }} },
  /* Phase */
  { },
  /* Comment */
  { },
  /* End */
  { }
}};


const InstructionLayout* getInstructionLayout(OpCode op) {
  auto index = uint32_t(op);

  if (index >= uint32_t(OpCode::ePhase)) {
    index -= uint32_t(OpCode::ePhase);
    index += uint32_t(OpCode::eBreakP) + 1u;
  }
  if (index >= uint32_t(OpCode::eTexCrd)) {
    index -= uint32_t(OpCode::eTexCrd);
    index += uint32_t(OpCode::eDefI) + 1u;
  }

  return index < g_instructionLayouts.size()
    ? &g_instructionLayouts[index]
    : nullptr;
}


ShaderInfo::ShaderInfo(util::ByteReader& reader) {
  if (!reader.read(m_token))
    resetOnError();
}


bool ShaderInfo::write(util::ByteWriter& writer) const {
  return writer.write(m_token);
}


void ShaderInfo::resetOnError() {
  *this = ShaderInfo();
}




Operand::Operand(util::ByteReader& reader, const OperandInfo& info, Instruction& op, const ShaderInfo& shaderInfo)
  : Operand(info, RegisterType::eConst) {
  if (!reader.read(m_token)) {
    Logger::err("Failed to read operand token.");
    resetOnError();
    return;
  }

  if (info.kind == OperandKind::eImm32) {
    const Operand& dst = op.getDst();
    bool isScalar = dst.isScalar(shaderInfo);
    m_imm[0] = m_token;

    for (uint32_t i = 1; i < 4 && !isScalar; i++) {
      if (!reader.read(m_imm[i])) {
        Logger::err("Failed to read immediate value.");
        resetOnError();
        return;
      }
    }
  }

  if ((info.kind == OperandKind::eDstReg || info.kind == OperandKind::eSrcReg)
    && !util::bextract(m_token, 31u, 1u)) {
    Logger::err("Token is not an operand.");
    resetOnError();
    return;
  }

  if ((info.kind == OperandKind::eDstReg || info.kind == OperandKind::eSrcReg) && hasRelativeAddressing()) {
    RegisterType registerType = getRegisterType();
    if (registerType != RegisterType::eConst
      && registerType != RegisterType::eConst2
      && registerType != RegisterType::eConst3
      && registerType != RegisterType::eConst4
      && registerType != RegisterType::eOutput
      && registerType != RegisterType::eInput) {
      Logger::err("Invalid register specified for relative indexing.");
      resetOnError();
      return;
    }

    m_addressToken.emplace();
    if (hasExtraRelativeAddressingToken(info.kind, shaderInfo)) {
      // VS SM3 supports using the following registers as indices:
      // - a0 the dedicated address register, integer
      // - aL: the loop counter register, integer
      // The following registers can be indexed:
      // - vN: one of the input registers, float
      // - cN: one of the float constant registers, float
      // - oN: one of the output registers, float
      // Everything else only supports a subset of this.

      if (!reader.read(m_addressToken.value())) {
        Logger::err("Failed to read relative addressing token.");
        resetOnError();
        return;
      }

      if (getRelativeAddressingRegisterType() != RegisterType::eAddr && getRelativeAddressingRegisterType() != RegisterType::eLoop) {
        Logger::err("Invalid register specified as relative index.");
        resetOnError();
        return;
      }

      if (util::bextract(m_addressToken.value(), 0u, 11u) != 0u) {
        Logger::err("Relative addressing register index needs to be 0.");
        resetOnError();
        return;
      }

      if (util::bextract(m_addressToken.value(), 13u, 1u)) {
        Logger::err("Nested relative addressing is illegal.");
        resetOnError();
        return;
      }
    } else {
      // Always use a0
      util::binsert(m_addressToken.value(), uint32_t(RegisterType::eAddr), 28u, 3u);
    }
  }

  if (info.kind == OperandKind::eDstReg && op.isPredicated()) {
    m_predicateToken.emplace();
    if (!reader.read(m_predicateToken.value())) {
      Logger::err("Failed to read predicate token.");
      resetOnError();
      return;
    }

    if (!util::bextract(m_predicateToken.value(), 31u, 1u)) {
      Logger::err("Predicate token is not an operand.");
      resetOnError();
      return;
    }

    if (RegisterType((util::bextract(m_predicateToken.value(), 11u, 2u) << 3u)
      | util::bextract(m_predicateToken.value(), 28u, 3u)) != RegisterType::ePredicate) {
      Logger::err("Invalid predicate register type.");
      resetOnError();
      return;
    }

    if (util::bextract(m_predicateToken.value(), 0u, 11u) != 0u) {
      Logger::err("Invalid predicate register index.");
      resetOnError();
      return;
    }

    if (getPredicateModifier() != OperandModifier::eNone && getPredicateModifier() != OperandModifier::eNot) {
      Logger::err("Invalid modifier on the predicate token.");
      resetOnError();
      return;
    }
  }

  m_isValid = true;
}


bool Operand::write(util::ByteWriter& writer, const ShaderInfo& info) const {
  if (m_info.kind == OperandKind::eImm32) {
    auto componentCount = isScalar(info) ? 1u : 4u;
    for (uint32_t i = 0u; i < uint32_t(componentCount); i++) {
      bool lastWrite;
      if (m_info.type == ir::ScalarType::eBool)
        lastWrite = writer.write(getImmediate<bool>(i));
      else if (m_info.type == ir::ScalarType::eF32)
        lastWrite = writer.write(getImmediate<float>(i));
      else
        lastWrite = writer.write(getImmediate<uint32_t>(i));

      if (!lastWrite)
        return false;
    }
    return true;
  }

  if (!writer.write(m_token))
    return false;

  if ((m_info.kind == OperandKind::eSrcReg || m_info.kind == OperandKind::eDstReg)
    && hasRelativeAddressing()
    && hasExtraRelativeAddressingToken(m_info.kind, info)) {
    if (!writer.write(m_addressToken.value())) {
      return false;
    }
  }

  if (m_info.kind == OperandKind::eDstReg && isPredicated()) {
    if (!writer.write(m_predicateToken.value())) {
      return false;
    }
  }

  return true;
}


void Operand::resetOnError() {
  m_isValid = false;
}


Instruction::Instruction(util::ByteReader& reader, const ShaderInfo& info) {
  if (!reader.read(m_token))
    return;

  /* Determine operand layout based on the shader
   * model and opcode, and parse the operands. */
  auto layout = getLayout(info);

  /* Determine operand layout based on the shader
   * model and opcode, and parse the operands. */
  uint32_t tokenCount = getOperandTokenCount(info, layout);

  /* Get reader sub-range for the exact number of tokens required */
  auto byteSize = tokenCount * sizeof(uint32_t);
  auto tokenReader = reader.getRangeRelative(0u, byteSize);

  /* Advance base reader to the next instruction. */
  reader.skip(byteSize);

  for (auto operandInfo : layout.operands) {
    Operand operand(tokenReader, operandInfo, *this, info);

    if (!operand) {
        Logger::err("Failed to read operand.");
      resetOnError();
      return;
    }

    m_operands.push_back(operand);
  }

  if (getOpCode() == OpCode::eComment) {
    m_commentData.resize(tokenReader.getRemaining());
    memcpy(m_commentData.data(), tokenReader.getData(0u), tokenReader.getRemaining());
  }

  dxbc_spv_assert(getOpCode() == OpCode::eComment || tokenReader.getRemaining() == 0u);
  m_isValid = true;
}


uint32_t Instruction::getOperandTokenCount(const ShaderInfo& info, const InstructionLayout& layout) const {
  OpCode opcode = getOpCode();

  if (opcode == OpCode::eComment)
    return util::bextract(m_token, 16, 15);

  if (opcode == OpCode::eEnd)
    return 0;

  // SM2.0 and above has the length of the op in instruction count baked into it.
  // SM1.4 and below have fixed lengths and run off expectation.
  // Phase does not respect the following rules.
  if (opcode != OpCode::ePhase) {
    if (info.getVersion().first >= 2) {
      return util::bextract(m_token, 24, 4);
    } else {
      // SM1.4 barely supports relative addressing and when relative addressing is used,
      // it always uses the single RelAddr register anyway without further specifying it.
      // SM1.4 does not support predication so there are never and predicate tokens.
      uint32_t tokenCount = 0u;
      for (const auto& operand : layout.operands) {
        // We just need to handle 4 dimensional immediate operands.
        if (operand.kind == OperandKind::eImm32 && operand.type != ir::ScalarType::eBool) {
          tokenCount += 4u;
        } else {
          tokenCount += 1u;
        }
      }
      return tokenCount;
    }
  }
  return 0;
}


InstructionLayout Instruction::getLayout(const ShaderInfo& info) const {
  auto layout = getInstructionLayout(getOpCode());

  if (!layout) {
    Logger::err("No layout known for opcode: ", getOpCode());
    return InstructionLayout();
  }

  /* Adjust operand counts for resource declarations */
  auto result = *layout;
  auto [major, minor] = info.getVersion();

  if (getOpCode() == OpCode::eSinCos && major <= 2u) {
    // Shader Model 2 SinCos has two additional src registers
    // that need to have the value of specific constants
    // for some reason.
    result.operands.push_back({ OperandKind::eSrcReg, ir::ScalarType::eF32 });
    result.operands.push_back({ OperandKind::eSrcReg, ir::ScalarType::eF32 });
  }

  if (getOpCode() == OpCode::eTexLd && major < 2u) {
    // TexLd/Tex (same opcode)
    result.operands.pop_back();
    result.operands.pop_back();
    result.operands.pop_back();
    if (minor < 4u) {
      // Tex (SM <1.4) only has the dst register.
      // This destination register has to be a texture register
      // and will contain the texture data afterward.
      // The index of it also determines the texture that will be sampled.
      result.operands.push_back({ OperandKind::eDstReg, ir::ScalarType::eF32 });
    } else if (minor == 4u) {
      // TexLd (SM 1.4) has separate dst/src registers.
      // Dst needs to be a temporary register and the index of it also determines the texture
      // that will be sampled.
      // Src provides the texture coordinates. It can be a texcoord register or a temporary register.
      result.operands.push_back({ OperandKind::eDstReg, ir::ScalarType::eF32 });
      result.operands.push_back({ OperandKind::eSrcReg, ir::ScalarType::eF32 });
    }
  }

  if (getOpCode() == OpCode::eTexCrd && major == 1u && minor < 4u) {
      // TexCrd/TexCoord (same opcode) are only available in SM1.
      // SM2+ can just access texcoord registers directly.
      // TexCoord (SM <1.4) does not take a separate source register.
      // The destination register has to be a texture register
      // and will contain the texture coord afterward.
      result.operands.pop_back();
  }

  if (getOpCode() == OpCode::eMov && major >= 3) {
    // Shader Model <2 doesn't have the mova instruction to move
    // a value to the address register. So the destination
    // *can* have an integer type.
    dxbc_spv_assert(!result.operands.empty());
    dxbc_spv_assert(result.operands[0].kind == OperandKind::eDstReg);
    result.operands[0].type = ir::ScalarType::eUnknown;
  }

  return result;
}


bool Instruction::write(util::ByteWriter& writer, const ShaderInfo& info) const {
  if (!writer.write(m_token))
    return false;

  /* Emit operands */
  auto layout = getLayout(info);

  if (layout.operands.size() != m_operands.size()) {
    Logger::err("Number of operands does not match layout.");
    return false;
  }

  for (uint32_t i = 0u; i < m_operands.size(); i++) {
    const auto& operandInfo = layout.operands[i];
    const auto& operand = m_operands[i];

    if (operandInfo.kind != operand.getInfo().kind) {
      Logger::err("Unexpected operand kind at position ", i);
      return false;
    }

    if (!operand.write(writer, info))
      return false;
  }

  return true;
}


void Instruction::resetOnError() {
  m_isValid = false;
}



Parser::Parser(util::ByteReader reader) {
  m_info   = ShaderInfo(reader);
  m_reader = util::ByteReader(reader);
}


Instruction Parser::parseInstruction() {
  dxbc_spv_assert(!m_isPastEnd);
  Instruction instruction = Instruction(m_reader, m_info);
  if (instruction.getOpCode() == OpCode::eEnd) {
    m_isPastEnd = true;
  }
  return instruction;
}



ConstantTable::ConstantTable(util::ByteReader reader) {
  util::FourCC fourCC;
  if (!reader || !reader.read(fourCC) || fourCC != util::FourCC("CTAB"))
    return;

  CommentConstantTable commentCtab;
  util::ByteReader ctabReader = reader.getRangeRelative(0u, sizeof(CommentConstantTable));
  if (!ctabReader)
    return;

  memcpy(&commentCtab, ctabReader.getData(0u), sizeof(CommentConstantTable));

  if (commentCtab.size != sizeof(CommentConstantTable))
    return;

  if (reader.getRemaining() <= commentCtab.creatorOffset)
    return;

  // Offsets are always from the start of the CommentConstantTable struct, so we must not move the reader offset.
  util::ByteReader creatorReader = reader.getRangeRelative(
    commentCtab.creatorOffset,
    reader.getRemaining() - commentCtab.creatorOffset
  );
  if (!creatorReader)
    return;

  std::string creator;
  creatorReader.readString(creator);
  if (creator.substr(0u, strlen("Microsoft")) != "Microsoft") {
    // Don't trust debug info in shaders that weren't compiled by FXC
    return;
  }

  for (uint32_t i = 0u; i < commentCtab.constantsCount; i++) {
    util::ByteReader constInfoReader = reader.getRangeRelative(
      commentCtab.constantInfoOffset + sizeof(CommentConstantInfo) * i,
      sizeof(CommentConstantInfo)
    );

    if (!constInfoReader)
      return;

    CommentConstantInfo commentConstantInfo;
    memcpy(&commentConstantInfo, constInfoReader.getData(0u), sizeof(CommentConstantInfo));

    if (reader.getRemaining() <= commentConstantInfo.nameOffset)
      return;

    util::ByteReader constantNameReader = reader.getRangeRelative(
      commentConstantInfo.nameOffset,
      reader.getRemaining() - commentConstantInfo.nameOffset
    );
    if (!constantNameReader)
      return;

    std::string constantName;
    constantNameReader.readString(constantName);

    ConstantInfo& constantInfo = m_constants[uint32_t(commentConstantInfo.registerSet)].emplace_back();
    constantInfo.name = constantName;
    constantInfo.index = commentConstantInfo.registerIndex;
    constantInfo.registerSet = commentConstantInfo.registerSet;
    constantInfo.count = commentConstantInfo.registerCount;
  }

  for (uint32_t i = 0u; i < m_constants.size(); i++) {
    std::sort(m_constants[i].begin(), m_constants[i].end(), [] (const ConstantInfo& a, const ConstantInfo& b) {
      return a.index < b.index || (a.index == b.index && a.count < b.count);
    });
  }
}


const ConstantInfo* ConstantTable::findConstantInfo(RegisterType registerType, uint32_t index) const {
  ConstantType constantType;
  switch (registerType) {
    case RegisterType::eConst:
    case RegisterType::eConst2:
    case RegisterType::eConst3:
    case RegisterType::eConst4:
      constantType = ConstantType::eFloat4;
      break;

    case RegisterType::eConstInt:
      constantType = ConstantType::eInt4;
      break;

    case RegisterType::eConstBool:
      constantType = ConstantType::eBool;
      break;

    case RegisterType::eSampler:
      constantType = ConstantType::eSampler;
      break;

    default:
      return nullptr;
  }

  const auto& ctab = m_constants[uint32_t(constantType)];
  if (ctab.empty()) {
    return nullptr;
  }

  uint32_t ctabIndex = 0u;
  for (uint32_t i = 0u; i < ctab.size(); i++) {
    if (ctab[i].index > index) {
      break;
    }
    ctabIndex = i;
  }

  const ConstantInfo& ctabEntry = ctab[ctabIndex];

  if (ctabEntry.index > index || ctabEntry.index + ctabEntry.count <= index) {
    return nullptr;
  }

  return &ctabEntry;
}



Builder::Builder(ShaderType type, uint32_t major, uint32_t minor)
: m_info(type, major, minor) {

}


Builder::~Builder() {

}


void Builder::add(Instruction ins) {
  m_instructions.push_back(std::move(ins));
}


bool Builder::write(util::ByteWriter& writer) const {
  if (!m_info.write(writer))
    return false;

  /* Emit instructions */
  for (const auto& e : m_instructions) {
    if (!e.write(writer, m_info))
      return false;
  }

  return true;
}



std::ostream& operator << (std::ostream& os, const ShaderInfo& shaderInfo) {
  return os << shaderInfo.getType() << "_" << shaderInfo.getVersion().first << "_" << shaderInfo.getVersion().second;
}

}
