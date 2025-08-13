#include "ir_disasm.h"

namespace dxbc_spv::ir {

Disassembler::Disassembler(const Builder& builder, const Options& options)
: m_builder(builder), m_options(options) {
  if (m_options.useDebugNames)
    resolveDebugNames();
}


Disassembler::~Disassembler() {

}


void Disassembler::disassemble(std::ostream& stream) const {
  for (auto ins : m_builder)
    disassembleOp(stream, ins);
}


void Disassembler::disassembleOp(std::ostream& stream, const Op& op) const {
  std::string prefix;

  if (!m_options.showConstants && (op.isUndef() ||
      (op.isConstant() && !op.getType().isArrayType())))
    return;

  if (!m_options.showDebugNames && op.getOpCode() == OpCode::eDebugName)
    return;

  if (op.getFlags()) {
    std::stringstream flags;
    flags << " [";
    { auto state = scopedColor(flags, util::ConsoleState::FgMagenta);
      flags << op.getFlags();
    }
    flags << "] ";
    prefix = flags.str();
  }

  std::stringstream def;
  disassembleDef(def, op.getDef());

  std::stringstream lead;
  if (!op.getType().isVoidType()) {
    auto state = scopedColor(lead, util::ConsoleState::FgCyan);
    lead << op.getType();
  }

  auto defStr = def.str();

  if (countChars(defStr) < 5u)
    defStr.insert(0u, 5u - countChars(defStr), ' ');

  lead << " " << defStr;

  auto leadStr = lead.str();

  if (countChars(prefix + leadStr) < 24u)
    leadStr.insert(0u, 24u - countChars(prefix + leadStr), ' ');

  stream << prefix << leadStr;
  stream << " = " << op.getOpCode();

  for (uint32_t i = 0u; i < op.getFirstLiteralOperandIndex(); i++) {
    stream << " ";
    disassembleOperandDef(stream, op, i);
  }

  for (uint32_t i = op.getFirstLiteralOperandIndex(); i < op.getOperandCount(); i++) {
    stream << " ";
    disassembleOperandLiteral(stream, op, i);
  }

  stream << std::endl;
}


std::string Disassembler::disassemble() const {
  std::stringstream str;
  disassemble(str);
  return str.str();
}


std::string Disassembler::disassembleOp(const Op& op) const {
  std::stringstream str;
  disassembleOp(str, op);
  return str.str();
}


void Disassembler::resolveDebugNames() {
  auto decl = m_builder.getDeclarations();

  for (auto i = decl.first; i != decl.second; i++) {
    const auto& op = *i;

    if (op.getOpCode() == OpCode::eDebugName)
      m_debugNames.insert({ SsaDef(op.getOperand(0u)), op.getLiteralString(1u) });
  }
}


void Disassembler::disassembleDef(std::ostream& stream, SsaDef def) const {
  auto state = scopedColor(stream, util::ConsoleState::FgYellow);
  auto entry = m_debugNames.find(def);

  if (entry != m_debugNames.end()) {
    stream << '%' << entry->second;
    return;
  }

  if (!def)
    state = scopedColor(stream, util::ConsoleState::FgBlack, util::ConsoleState::EffectDim);

  stream << def;
}


void Disassembler::disassembleOperandDef(std::ostream& stream, const Op& op, uint32_t index) const {
  auto operand = SsaDef(op.getOperand(index));

  if (op.getOpCode() == OpCode::eDebugName) {
    /* Don't display the debug name twice */
    auto state = scopedColor(stream, util::ConsoleState::FgYellow);
    stream << operand;
    return;
  }

  if (m_options.resolveConstants) {
    const auto& def = m_builder.getOp(operand);

    if (def.isUndef() || (def.isConstant() && !def.getType().isArrayType())) {
      stream << "%[";

      { auto state = scopedColor(stream, util::ConsoleState::FgCyan);
        stream << def.getType();
      }

      stream << "(";

      if (def.isConstant()) {
        for (uint32_t i = 0u; i < def.getOperandCount(); i++) {
          stream << (i ? "," : "");
          disassembleOperandLiteral(stream, def, i);
        }

      } else {
        auto state = scopedColor(stream, util::ConsoleState::FgRed);
        stream << "?";
      }

      stream << ")]";
      return;
    }
  }

  disassembleDef(stream, operand);
}


void Disassembler::disassembleOperandLiteral(std::ostream& stream, const Op& op, uint32_t index) const {
  auto operand = op.getOperand(index);

  if (op.getOpCode() == OpCode::eSemantic ||
      op.getOpCode() == OpCode::eDebugName ||
      op.getOpCode() == OpCode::eDebugMemberName) {
    uint32_t stringIndex = op.getFirstLiteralOperandIndex();

    if (op.getOpCode() == OpCode::eSemantic ||
        op.getOpCode() == OpCode::eDebugMemberName)
      stringIndex += 1u;

    if (index > stringIndex)
      return;

    if (index == stringIndex) {
      stream << "\"";
      { auto state = scopedColor(stream, util::ConsoleState::FgGreen);
        stream << op.getLiteralString(stringIndex);
      }
      stream << "\"";
      return;
    }
  }

  if (op.getOpCode() == OpCode::eConstant) {
    auto state = scopedColor(stream, util::ConsoleState::FgRed);

    if (op.getType().isVoidType())
      return;

    ScalarType type = op.getType().resolveFlattenedType(index);

    switch (type) {
      case ScalarType::eBool: stream << (bool(operand) ? "True" : "False"); return;

      case ScalarType::eI8:  stream << int32_t(int8_t(operand)); return;
      case ScalarType::eI16: stream << int32_t(int16_t(operand)); return;
      case ScalarType::eMinI16:
      case ScalarType::eI32: stream << int32_t(operand); return;
      case ScalarType::eI64: stream << int64_t(operand); return;

      case ScalarType::eU8:  stream << uint32_t(uint8_t(operand)); return;
      case ScalarType::eU16: stream << uint32_t(uint16_t(operand)); return;
      case ScalarType::eMinU16:
      case ScalarType::eU32: stream << uint32_t(operand); return;
      case ScalarType::eU64: stream << uint64_t(operand); return;

      case ScalarType::eF16: stream << float(float16_t(operand)); return;
      case ScalarType::eMinF16:
      case ScalarType::eF32: stream << float(operand); return;
      case ScalarType::eF64: stream << double(operand); return;

      default:;
    }
  }

  if (m_options.useEnumNames) {
    auto state = scopedColor(stream, util::ConsoleState::FgBlue);

    switch (op.getOpCode()) {
      case OpCode::eEntryPoint:
        if (index == op.getFirstLiteralOperandIndex()) { stream << ShaderStage(operand); return; }
        break;

      case OpCode::eSetGsInputPrimitive:
      case OpCode::eSetGsOutputPrimitive:
      case OpCode::eSetTessDomain:
        if (index == 1u) { stream << PrimitiveType(operand); return; }
        break;

      case OpCode::eSetTessPrimitive:
        if (index == 1u) { stream << PrimitiveType(operand); return; }
        if (index == 2u) { stream << TessWindingOrder(operand); return; }
        if (index == 3u) { stream << TessPartitioning(operand); return; }
        break;

      case OpCode::eSetFpMode:
        if (index == 1u) { stream << RoundMode(operand); return; }
        if (index == 2u) { stream << DenormMode(operand); return; }
        break;

      case OpCode::eDclInput:
        if (index == 3u) { stream << InterpolationModes(operand); return; }
        break;

      case OpCode::eDclInputBuiltIn:
        if (index == 1u) { stream << BuiltIn(operand); return; }
        if (index == 2u) { stream << InterpolationModes(operand); return; }
        break;

      case OpCode::eDclOutputBuiltIn:
        if (index == 1u) { stream << BuiltIn(operand); return; }
        break;

      case OpCode::eDclPushData:
        if (index == 2u) { stream << ShaderStageMask(operand); return; }
        break;

      case OpCode::eDclSrv:
        if (index == 4u) { stream << ResourceKind(operand); return; }
        break;

      case OpCode::eDclUav:
        if (index == 4u) { stream << ResourceKind(operand); return; }
        if (index == 5u) { stream << UavFlags(operand); return; }
        break;

      case OpCode::eLabel:
        if (index == op.getFirstLiteralOperandIndex()) {
          stream << Construct(operand);
          return;
        }
        break;

      case OpCode::eBarrier:
        if (index <= 1u) { stream << Scope(operand); return; }
        if (index == 2u) { stream << MemoryTypeFlags(operand); return; }
        break;

      case OpCode::eLdsAtomic:
      case OpCode::eBufferAtomic:
      case OpCode::eImageAtomic:
      case OpCode::eCounterAtomic:
      case OpCode::eMemoryAtomic:
        if (index == op.getFirstLiteralOperandIndex()) {
          stream << AtomicOp(operand);
          return;
        }
        break;

      case OpCode::eDerivX:
      case OpCode::eDerivY:
        if (index == 1u) { stream << DerivativeMode(operand); return; }
        return;

      case OpCode::eFRound:
        if (index == 1u) { stream << RoundMode(operand); return; }
        return;

      case OpCode::ePointer:
        if (index == 1u) { stream << UavFlags(operand); return; }
        return;

      case OpCode::eRovScopedLockBegin:
        if (index == 0u) { stream << MemoryTypeFlags(operand); return; }
        if (index == 1u) { stream << RovScope(operand); return; }
        return;

      case OpCode::eRovScopedLockEnd:
        if (index == 0u) { stream << MemoryTypeFlags(operand); return; }
        return;

      default:;
    }
  }

  /* Interpret literal as unsigned integer by default */
  auto state = scopedColor(stream, util::ConsoleState::FgRed);
  uint64_t lit = uint64_t(operand);

  if (lit <= 0xffffu)
    stream << std::dec << lit;
  else
    stream << "0x" << std::hex << lit << std::dec;
}

util::ConsoleState Disassembler::scopedColor(std::ostream& stream, uint32_t fg, uint32_t effect) const {
  if (!m_options.coloredOutput)
    return util::ConsoleState();

  return util::ConsoleState(stream, fg, effect);
}

size_t Disassembler::countChars(const std::string& str) {
  size_t n = 0u;

  bool insideEscapeSequence = false;

  for (char ch : str) {
    if (ch == '\033') {
      insideEscapeSequence = true;
      continue;
    }

    if (insideEscapeSequence) {
      insideEscapeSequence = ch != 'm';
      continue;
    }

    n++;
  }

  return n;
}

}
