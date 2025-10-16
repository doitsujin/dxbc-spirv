#include "sm3_disasm.h"

#include <iostream>
#include <iomanip>
#include <sstream>

#include "sm3_parser.h"

namespace dxbc_spv::sm3 {

void Disassembler::disassembleOp(std::ostream& stream, const Instruction& op, const ConstantTable& ctab) {
  if (op.getOpCode() == OpCode::eComment) {
    return;
  }

  if (opEndsNestedBlock(op))
    decrementIndentation();

  emitLineNumber(stream);
  emitIndentation(stream);

  disassembleOpcodeToken(stream, op);

  auto layout = op.getLayout(m_info);

  uint32_t nDst  = 0u;
  uint32_t nSrc  = 0u;
  uint32_t nImm  = 0u;
  uint32_t nDcl  = 0u;

  bool first = true;

  for (const auto& operand : layout.operands) {
    bool inBounds = false;
    bool isFirst = std::exchange(first, false);

    if (operand.kind != OperandKind::eDcl) {
      if (isFirst || nDcl != 0u) {
        stream << " ";
      } else {
        stream << ", ";
      }
    }

    switch (operand.kind) {
      case OperandKind::eDstReg:
        if ((inBounds = nDst++ == 0u && op.hasDst()))
          disassembleOperand(stream, op, op.getDst(), ctab);
        break;

      case OperandKind::eSrcReg:
        if ((inBounds = (nSrc < op.getSrcCount())))
          disassembleOperand(stream, op, op.getSrc(nSrc++), ctab);
        break;

      case OperandKind::eDcl:
        if ((inBounds = nDcl++ == 0u && op.hasDcl()))
          disassembleDeclaration(stream, op, op.getDcl());
        break;

      case OperandKind::eImm32:
        if ((inBounds = nImm++ == 0u && op.hasImm()))
          disassembleImmediate(stream, op, op.getImm());
        break;

      default:
        break;
    }

    if (!inBounds)
      stream << "(undefined)";
  }

  if (opBeginsNestedBlock(op))
    incrementIndentation();
}


std::string Disassembler::disassembleOp(const Instruction& op, const ConstantTable& ctab) {
  std::stringstream str;
  disassembleOp(str, op, ctab);
  return str.str();
}


void Disassembler::disassembleOpcodeToken(std::ostream& stream, const Instruction& op) const {

  if (op.isCoissued()) {
    stream << "+ ";
  } else {
    stream << "  ";
  }

  if (op.isPredicated()) {
    const Operand& dst = op.getDst();
    stream << "(";
    if (dst.getPredicateModifier() == OperandModifier::eNot) {
        stream << "!";
    }
    stream << "p0";
    Swizzle swizzle = dst.getPredicateSwizzle();
    if (swizzle != Swizzle::identity()) {
      stream << "." << swizzle;
    }
    stream << ") ";
  }

  stream << op.getOpCode();

  if (op.hasDst()) {
    const auto& dst = op.getDst();
    int8_t shift = dst.getShift();
    if (shift > 0) {
      stream << "_x" << (1 << shift);
    } else if (shift < 0) {
      stream << "_d" << (1 << -shift);
    }

    if (dst.isPartialPrecision()) {
      stream << "_pp";
    }

    if (dst.isCentroid()) {
      stream << "_centroid";
    }

    if (dst.isSaturated()) {
      stream << "_sat";
    }
  }

  if (op.getOpCode() == OpCode::eIfc
    || op.getOpCode() == OpCode::eBreakC
    || op.getOpCode() == OpCode::eSetP) {
    switch (op.getComparisonMode()) {
      case ComparisonMode::eNever:        stream << "_false";   break;
      case ComparisonMode::eGreaterThan:  stream << "_gt";      break;
      case ComparisonMode::eEqual:        stream << "_eq";      break;
      case ComparisonMode::eGreaterEqual: stream << "_ge";      break;
      case ComparisonMode::eLessThan:     stream << "_lt";      break;
      case ComparisonMode::eNotEqual:     stream << "_ne";      break;
      case ComparisonMode::eLessEqual:    stream << "_le";      break;
      case ComparisonMode::eAlways:       stream << "_true";    break;
      default:                            stream << "_unknown"; break;
    }
  }
}


void Disassembler::disassembleOperand(std::ostream& stream, const Instruction& op, const Operand& arg, const ConstantTable& ctab) const {
  if (op.getOpCode() == OpCode::eDcl) {
    stream << UnambiguousRegisterType { arg.getRegisterType(), m_info.getType(), m_info.getVersion().first };
    disassembleRegisterAddressing(stream, arg, ctab);
    return;
  }

  /* Handle modifier */
  auto modifier = OperandModifier::eNone;
  std::string suffix;

  if (arg.getInfo().kind == OperandKind::eSrcReg) {
    modifier = arg.getModifier();
    switch (modifier) {
      case OperandModifier::eNeg:
        stream << "-";
        break;

      case OperandModifier::eBias:
        stream << "(";
        suffix = " - 0.5";
        break;

      case OperandModifier::eBiasNeg:
        stream << "-(";
        suffix = " - 0.5)";
        break;

      case OperandModifier::eSign:
        stream << "fma(";
        suffix = ", 2.0f, -1.0f)";
        break;

      case OperandModifier::eSignNeg:
        stream << "-fma(";
        suffix = ", 2.0f, -1.0f)";
        break;

      case OperandModifier::eComp:
        stream << "(1 - ";
        suffix = ")";
        break;

      case OperandModifier::eX2:
        stream << "(";
        suffix = " * 2)";
        break;

      case OperandModifier::eX2Neg:
        stream << "-(";
        suffix = " * 2)";
        break;

      case OperandModifier::eDz:
        stream << "(";
        suffix = ".z)";
        break;

      case OperandModifier::eDw:
        stream << "(";
        suffix = ".w)";
        break;

      case OperandModifier::eAbs:
        stream << "abs(";
        suffix = ")";
        break;

      case OperandModifier::eAbsNeg:
        stream << "-abs(";
        suffix = ")";
        break;

      case OperandModifier::eNot:
        stream << "!";
        break;

      default: break;
    }
  }

  stream << UnambiguousRegisterType { arg.getRegisterType(), m_info.getType(), m_info.getVersion().first };
  disassembleRegisterAddressing(stream, arg, ctab);
  disassembleSwizzleWriteMask(stream, op, arg);

  if (arg.getInfo().kind == OperandKind::eSrcReg
    && (modifier == OperandModifier::eDz || modifier == OperandModifier::eDw)) {
    stream << " / ";
    stream << UnambiguousRegisterType { arg.getRegisterType(), m_info.getType(), m_info.getVersion().first };
    disassembleRegisterAddressing(stream, arg, ctab);
    disassembleSwizzleWriteMask(stream, op, arg);
  }

  stream << suffix;
}

void Disassembler::disassembleSwizzleWriteMask(std::ostream& stream, const Instruction& op, const Operand& arg) const {
  switch (arg.getSelectionMode(m_info)) {
    case SelectionMode::eMask:
      if (arg.getWriteMask(m_info) != WriteMask(ComponentBit::eAll))
        stream << "." << arg.getWriteMask(m_info);
      break;
    case SelectionMode::eSwizzle: {
      Swizzle swizzle = arg.getSwizzle(m_info);
      WriteMask dstWriteMask = op.hasDst() ? op.getDst().getWriteMask(m_info) : WriteMask(ComponentBit::eAll);

      if (swizzle != Swizzle::identity()) {
        // Only print the components that are relevant according to the write mask.
        stream << ".";
        for (uint32_t i = 0u; i < 4u; i++) {
          if (dstWriteMask & WriteMask(ComponentBit(1u << i))) {
            stream << swizzle.get(i);
          }
        }
      }
    } break;
    case SelectionMode::eSelect1: break;
  }
}

void Disassembler::disassembleRegisterAddressing(std::ostream& stream, const Operand& arg, const ConstantTable& ctab) const {
  if (arg.getRegisterType() == RegisterType::eMiscType) {
    stream << MiscTypeIndex(arg.getIndex());
  } else if (arg.getRegisterType() == RegisterType::eRasterizerOut) {
    stream << RasterizerOutIndex(arg.getIndex());
  } else if (arg.getRegisterType() != RegisterType::eLoop) {
    if (arg.hasRelativeAddressing()) {
      const ConstantInfo* constantInfo = ctab.findConstantInfo(arg.getRegisterType(), arg.getIndex());
      if (constantInfo != nullptr) {
        dxbc_spv_assert(constantInfo->count > 1u);
        stream << arg.getIndex() << "_" << constantInfo->name;
      }

      stream << "[";
      if (arg.getIndex() != 0u) {
        stream << (constantInfo ? arg.getIndex() - constantInfo->index : arg.getIndex());

        stream << " + ";
      }
      RegisterType relAddrRegisterType = arg.getRelativeAddressingRegisterType();
      stream << UnambiguousRegisterType { relAddrRegisterType, m_info.getType(), m_info.getVersion().first };
      if (relAddrRegisterType == RegisterType::eAddr) {
        stream << "0";
      }
      stream << ".";
      stream << arg.getRelativeAddressingSwizzle().x();
      stream << "]";
    } else {
      stream << arg.getIndex();
      const ConstantInfo* constantInfo = ctab.findConstantInfo(arg.getRegisterType(), arg.getIndex());
      if (constantInfo != nullptr) {
        stream << "_" << constantInfo->name;
        if (constantInfo->count > 1u) {
          stream << "[" << (arg.getIndex() - constantInfo->index) << "]";
        }
      }
    }
  }
}


void Disassembler::disassembleDeclaration(std::ostream& stream, const Instruction& op, const Operand& operand) const {
  const Operand& dst = op.getRawOperand(1u);
  auto registerType = dst.getRegisterType();
  if (registerType == RegisterType::eSampler) {
    stream << "_" << operand.getTextureType();
    return;
  }

  if (registerType == RegisterType::eOutput
    || registerType == RegisterType::eInput) {
    stream << "_";
    SemanticUsage usage = operand.getSemanticUsage();
    uint32_t index = operand.getSemanticIndex();
    if (usage != SemanticUsage::eColor) {
      stream << usage;
    } else {
      if (index == 0) {
        stream << "_color";
      } else {
        stream << "_specular" << (index - 1u);
      }
    }

    if (usage == SemanticUsage::ePosition
      || usage == SemanticUsage::eNormal
      || usage == SemanticUsage::eTexCoord
      || usage == SemanticUsage::eSample
      || (usage != SemanticUsage::eBlendWeight
        && usage != SemanticUsage::eBlendIndices
        && usage != SemanticUsage::ePointSize
        && usage != SemanticUsage::eTangent
        && usage != SemanticUsage::eBinormal
        && usage != SemanticUsage::ePositionT
        && usage != SemanticUsage::eFog
        && usage != SemanticUsage::eDepth)) {
      stream << std::to_string(index);
    }
  }
}


void Disassembler::disassembleImmediate(std::ostream& stream, const Instruction& op, const Operand& arg) const {
  /* Determine number of components based on the operand token */
    const Operand& dst = op.getRawOperand(0u);
  uint32_t componentCount = dst.isScalar(m_info) ? 1u : 4u;

  if (componentCount > 1u)
    stream << '(';

  for (uint32_t i = 0u; i < componentCount; i++) {
    auto type = arg.getInfo().type;

    if (i)
      stream << ", ";

    /* Resolve ambiguous types based on context */
    if (type == ir::ScalarType::eUnknown) {
      auto kind = std::fpclassify(arg.getImmediate<float>(i));

      type = (kind == FP_INFINITE || kind == FP_NORMAL)
        ? ir::ScalarType::eF32
        : ir::ScalarType::eI32;
    }

    switch (type) {
      case ir::ScalarType::eBool:
      case ir::ScalarType::eI32: {
        auto si = arg.getImmediate<int32_t>(i);

        if (std::abs(si) >= 0x100000)
          stream << "0x" << std::hex << std::setw(8u) << std::setfill('0') << uint32_t(si);
        else
          stream << si;
      } break;

      case ir::ScalarType::eI64: {
        auto si = arg.getImmediate<int64_t>(i);

        if (std::abs(si) >= 0x100000)
          stream << "0x" << std::hex << std::setw(16u) << std::setfill('0') << uint64_t(si);
        else
          stream << si;
      } break;

      case ir::ScalarType::eU32: {
        auto ui = arg.getImmediate<uint32_t>(i);

        if (ui >= 0x100000u)
          stream << "0x" << std::hex << std::setw(8u) << std::setfill('0') << ui;
        else
          stream << ui;
      } break;

      case ir::ScalarType::eU64: {
        auto ui = arg.getImmediate<uint64_t>(i);

        if (ui >= 0x100000u)
          stream << "0x" << std::hex << std::setw(16u) << std::setfill('0') << ui;
        else
          stream << ui;
      } break;

      case ir::ScalarType::eF32: {
        auto f = arg.getImmediate<float>(i);

        if (std::isnan(f))
          stream << "0x" << std::hex << arg.getImmediate<uint32_t>(i);
        else
          stream << std::fixed << std::setw(8u) << f << "f";
      } break;

      case ir::ScalarType::eF64: {
        auto f = arg.getImmediate<double>(i);

        if (std::isnan(f))
          stream << "0x" << std::hex << arg.getImmediate<uint64_t>(i) << std::dec;
        else
          stream << std::fixed << std::setw(8u) << f;
      } break;

      default:
        stream << "(unhandled scalar type " << type << ") " << arg.getImmediate<uint32_t>(i);
        break;
    }

    /* Apparently there is no way to reset everything */
    stream << std::setfill(' ') << std::setw(0u) << std::dec;
  }

  if (componentCount > 1u)
    stream << ')';
}


void Disassembler::emitLineNumber(std::ostream& stream) {
  if (!m_options.lineNumbers)
    return;

  stream << std::setw(6u) << std::setfill(' ') << (++m_lineNumber) << ": "
         << std::setw(0u);
}


void Disassembler::emitIndentation(std::ostream& stream) const {
  if (!m_options.indent)
    return;

  for (uint32_t i = 0u; i < 2u * m_indentDepth; i++)
    stream << ' ';
}


void Disassembler::incrementIndentation() {
  m_indentDepth++;
}


void Disassembler::decrementIndentation() {
  if (m_indentDepth)
    m_indentDepth--;
  else
    std::cout << "Underflow" << '\n';
}



bool Disassembler::opBeginsNestedBlock(const Instruction& op) {
  auto opCode = op.getOpCode();

  return opCode == OpCode::eIf ||
         opCode == OpCode::eIfc ||
         opCode == OpCode::eElse ||
         opCode == OpCode::eLoop ||
         opCode == OpCode::eRep;
}


bool Disassembler::opEndsNestedBlock(const Instruction& op) {
  auto opCode = op.getOpCode();

  return opCode == OpCode::eElse ||
         opCode == OpCode::eEndIf ||
         opCode == OpCode::eEndLoop ||
         opCode == OpCode::eEndRep;
}

}
