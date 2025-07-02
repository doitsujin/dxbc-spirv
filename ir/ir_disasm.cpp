#include <iomanip>

#include "ir_disasm.h"

namespace dxbc_spv::ir {

void Disassembler::disassemble() {
  if (m_options.useDebugNames)
    resolveDebugNames();

  for (auto ins : m_builder)
    disassembleInstruction(ins);
}


void Disassembler::disassembleInstruction(const Op& op) {
  m_str << std::setw(24) << std::setfill(' ');
  disassembleDef(op.getDef());
  m_str << " = " << op.getOpCode();

  if (op.getFlags())
    m_str << " [" << op.getFlags() << "]";

  if (!op.getType().isVoidType() || op.getOperandCount())
    m_str << " " << op.getType();

  for (uint32_t i = 0u; i < op.getFirstLiteralOperandIndex(); i++)
    disassembleOperandDef(op, i);

  for (uint32_t i = op.getFirstLiteralOperandIndex(); i < op.getOperandCount(); i++)
    disassembleOperandLiteral(op, i);

  m_str << std::endl;
}


void Disassembler::resolveDebugNames() {
  auto decl = m_builder.getDeclarations();

  for (auto i = decl.first; i != decl.second; i++) {
    const auto& op = *i;

    if (op.getOpCode() == OpCode::eDebugName)
      m_debugNames.insert({ SsaDef(op.getOperand(0u)), op.getLiteralString(1u) });
  }
}


void Disassembler::disassembleDef(SsaDef def) {
  auto entry = m_debugNames.find(def);

  std::stringstream tmp;

  if (entry != m_debugNames.end())
    tmp << '%' << entry->second;
  else
    tmp << def;

  m_str << tmp.str();
}


void Disassembler::disassembleOperandDef(const Op& op, uint32_t index) {
  auto operand = SsaDef(op.getOperand(index));

  m_str << " ";

  if (op.getOpCode() == OpCode::eDebugName) {
    /* Don't display the debug name twice */
    m_str << operand;
    return;
  }

  disassembleDef(operand);
}


void Disassembler::disassembleOperandLiteral(const Op& op, uint32_t index) {
  auto operand = op.getOperand(index);

  if (op.getOpCode() == OpCode::eDebugName) {
    if (index == op.getFirstLiteralOperandIndex()) {
      m_str << " \"";
      m_str << op.getLiteralString(index);
      m_str << "\"";
    }

    return;
  }

  m_str << " ";

  if (op.getOpCode() == OpCode::eConstant) {
    ScalarType type = op.getType().resolveFlattenedType(index);

    switch (type) {
      case ScalarType::eBool: m_str << (bool(operand) ? "True" : "False"); return;

      case ScalarType::eI8:  m_str << int32_t(int8_t(operand)); return;
      case ScalarType::eI16: m_str << int32_t(int16_t(operand)); return;
      case ScalarType::eI32: m_str << int32_t(operand); return;
      case ScalarType::eI64: m_str << int64_t(operand); return;

      case ScalarType::eU8:  m_str << uint32_t(uint8_t(operand)); return;
      case ScalarType::eU16: m_str << uint32_t(uint16_t(operand)); return;
      case ScalarType::eU32: m_str << uint32_t(operand); return;
      case ScalarType::eU64: m_str << uint64_t(operand); return;

      case ScalarType::eF16: m_str << float(float16_t(operand)); return;
      case ScalarType::eF32: m_str << float(operand); return;
      case ScalarType::eF64: m_str << double(operand); return;

      default:;
    }
  }

  if (m_options.useEnumNames) {
    switch (op.getOpCode()) {
      case OpCode::eEntryPoint:
        if (index == 1u) { m_str << ShaderStage(operand); return; }
        break;

      case OpCode::eSetGsInputPrimitive:
      case OpCode::eSetGsOutputPrimitive:
      case OpCode::eSetTessDomain:
        if (index == 1u) { m_str << PrimitiveType(operand); return; }
        break;

      case OpCode::eSetTessPrimitive:
        if (index == 1u) { m_str << PrimitiveType(operand); return; }
        if (index == 2u) { m_str << TessWindingOrder(operand); return; }
        if (index == 3u) { m_str << TessPartitioning(operand); return; }
        break;

      case OpCode::eDclInput:
        if (index == 2u) { m_str << InterpolationModes(operand); return; }
        break;

      case OpCode::eDclInputBuiltIn:
        if (index == 0u) { m_str << BuiltIn(operand); return; }
        if (index == 1u) { m_str << InterpolationModes(operand); return; }
        break;

      case OpCode::eDclOutputBuiltIn:
        if (index == 0u) { m_str << BuiltIn(operand); return; }
        break;

      case OpCode::eDclPushData:
        if (index == 1u) { m_str << ShaderStageMask(operand); return; }
        break;

      case OpCode::eDclSrv:
        if (index == 3u) { m_str << ResourceKind(operand); return; }
        break;

      case OpCode::eDclUav:
        if (index == 3u) { m_str << ResourceKind(operand); return; }
        if (index == 4u) { m_str << UavFlags(operand); return; }
        break;

      case OpCode::eLabel:
        if (index == op.getFirstLiteralOperandIndex()) {
          m_str << Construct(operand);
          return;
        }
        break;

      case OpCode::eBarrier:
        if (index <= 1u) { m_str << Scope(operand); return; }
        if (index == 2u) { m_str << MemoryTypeFlags(operand); return; }
        break;

      case OpCode::eLdsAtomic:
      case OpCode::eBufferAtomic:
      case OpCode::eImageAtomic:
      case OpCode::eCounterAtomic:
      case OpCode::eMemoryAtomic:
        if (index == op.getFirstLiteralOperandIndex()) {
          m_str << AtomicOp(operand);
          return;
        }
        break;

      case OpCode::eDerivX:
      case OpCode::eDerivY:
        if (index == 1u) { m_str << DerivativeMode(operand); return; }
        return;

      default:;
    }
  }

  /* Interpret literal as unsigned integer by default */
  uint64_t lit = uint64_t(operand);

  if (lit <= 0xffffu)
    m_str << std::dec << lit;
  else
    m_str << "0x" << std::hex << lit << std::dec;
}

}
