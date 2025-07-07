#include <iomanip>

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

  if (op.getFlags()) {
    std::stringstream flags;
    flags << " [" << op.getFlags() << "] ";
    prefix = flags.str();
  }

  std::stringstream def;
  disassembleDef(def, op.getDef());

  std::stringstream lead;
  lead << op.getType() << " " << std::setw(5) << std::setfill(' ') << def.str();

  stream << prefix << std::setw(24 - std::min<size_t>(24u, prefix.size())) << std::setfill(' ') << lead.str();
  stream << " = " << op.getOpCode();

  for (uint32_t i = 0u; i < op.getFirstLiteralOperandIndex(); i++)
    disassembleOperandDef(stream, op, i);

  for (uint32_t i = op.getFirstLiteralOperandIndex(); i < op.getOperandCount(); i++)
    disassembleOperandLiteral(stream, op, i);

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
  auto entry = m_debugNames.find(def);

  if (entry != m_debugNames.end())
    stream << '%' << entry->second;
  else
    stream << def;
}


void Disassembler::disassembleOperandDef(std::ostream& stream, const Op& op, uint32_t index) const {
  auto operand = SsaDef(op.getOperand(index));

  stream << " ";

  if (op.getOpCode() == OpCode::eDebugName) {
    /* Don't display the debug name twice */
    stream << operand;
    return;
  }

  disassembleDef(stream, operand);
}


void Disassembler::disassembleOperandLiteral(std::ostream& stream, const Op& op, uint32_t index) const {
  auto operand = op.getOperand(index);

  if (op.getOpCode() == OpCode::eDebugName || op.getOpCode() == OpCode::eSemantic) {
    uint32_t stringIndex = op.getFirstLiteralOperandIndex();

    if (op.getOpCode() == OpCode::eSemantic)
      stringIndex += 1u;

    if (index > stringIndex)
      return;

    if (index == stringIndex) {
      stream << " \"";
      stream << op.getLiteralString(stringIndex);
      stream << "\"";
      return;
    }
  }

  stream << " ";

  if (op.getOpCode() == OpCode::eConstant) {
    ScalarType type = op.getType().resolveFlattenedType(index);

    switch (type) {
      case ScalarType::eBool: stream << (bool(operand) ? "True" : "False"); return;

      case ScalarType::eI8:  stream << int32_t(int8_t(operand)); return;
      case ScalarType::eI16: stream << int32_t(int16_t(operand)); return;
      case ScalarType::eI32: stream << int32_t(operand); return;
      case ScalarType::eI64: stream << int64_t(operand); return;

      case ScalarType::eU8:  stream << uint32_t(uint8_t(operand)); return;
      case ScalarType::eU16: stream << uint32_t(uint16_t(operand)); return;
      case ScalarType::eU32: stream << uint32_t(operand); return;
      case ScalarType::eU64: stream << uint64_t(operand); return;

      case ScalarType::eF16: stream << float(float16_t(operand)); return;
      case ScalarType::eF32: stream << float(operand); return;
      case ScalarType::eF64: stream << double(operand); return;

      default:;
    }
  }

  if (m_options.useEnumNames) {
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

      default:;
    }
  }

  /* Interpret literal as unsigned integer by default */
  uint64_t lit = uint64_t(operand);

  if (lit <= 0xffffu)
    stream << std::dec << lit;
  else
    stream << "0x" << std::hex << lit << std::dec;
}

}
