#include "sm3_disasm.h"

#include <iostream>
#include <iomanip>
#include <sstream>

#include "sm3_parser.h"

namespace dxbc_spv::sm3 {

void Disassembler::disassembleOp(std::ostream& stream, const Instruction& op) {
  if (op.getOpCode() == OpCode::eComment) {
    disassembleComment(stream, op);
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
          disassembleOperand(stream, op, op.getDst());
        break;

      case OperandKind::eSrcReg:
        if ((inBounds = (nSrc < op.getSrcCount())))
          disassembleOperand(stream, op, op.getSrc(nSrc++));
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


std::string Disassembler::disassembleOp(const Instruction& op) {
  std::stringstream str;
  disassembleOp(str, op);
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


void Disassembler::disassembleOperand(std::ostream& stream, const Instruction& op, const Operand& arg) const {
  if (op.getOpCode() == OpCode::eDcl) {
    disassembleRegisterType(stream, arg.getRegisterType());
    disassembleRegisterAddressing(stream, arg);
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

  disassembleRegisterType(stream, arg.getRegisterType());
  disassembleRegisterAddressing(stream, arg);
  disassembleSwizzleWriteMask(stream, op, arg);

  if (arg.getInfo().kind == OperandKind::eSrcReg
    && (modifier == OperandModifier::eDz || modifier == OperandModifier::eDw)) {
    stream << " / ";
    disassembleRegisterType(stream, arg.getRegisterType());
    disassembleRegisterAddressing(stream, arg);
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

void Disassembler::disassembleRegisterAddressing(std::ostream& stream, const Operand& arg) const {
  if (arg.getRegisterType() == RegisterType::eMiscType) {
    if (arg.getIndex() == uint32_t(MiscTypeIndex::eMiscTypeFace)) {
      stream << "vFace";
    } else if (arg.getIndex() == uint32_t(MiscTypeIndex::eMiscTypePosition)) {
      stream << "vPosition";
    } else {
      stream << "(unhandled misc register index " << arg.getIndex() << ")";
    }
  } else if (arg.getRegisterType() != RegisterType::eLoop) {
    if (arg.hasRelativeAddressing()) {
      stream << "[";
      if (arg.getIndex() != 0u) {
        stream << arg.getIndex();
        stream << " + ";
      }
      RegisterType relAddrRegisterType = arg.getRelativeAddressingRegisterType();
      disassembleRegisterType(stream, relAddrRegisterType);
      if (relAddrRegisterType == RegisterType::eAddr) {
        stream << "0";
      }
      stream << ".";
      stream << arg.getRelativeAddressingSwizzle().x();
      stream << "]";
    } else {
      stream << arg.getIndex();
    }
  }
}

void Disassembler::disassembleRegisterType(std::ostream& stream, RegisterType registerType) const {
  switch (registerType) {
    case RegisterType::eTemp:          stream << "r";      break;
    case RegisterType::eInput:         stream << "v";      break;
    case RegisterType::eConst:
    case RegisterType::eConst2:
    case RegisterType::eConst3:
    case RegisterType::eConst4:        stream << "c";      break;
    case RegisterType::eAddr:
    // case RegisterType::eTexture: Same value
      stream << (m_info.getType() == ShaderType::eVertex ? "a" : "t");
      break;
    case RegisterType::eRasterizerOut: stream << "oPos";   break;
    case RegisterType::eAttributeOut:  stream << "o";      break;
    case RegisterType::eTexCoordOut:
    // case RegisterType::eOutput: Same value.
      stream << (m_info.getVersion().first == 3 ? "o" : "oT");
      break;
    case RegisterType::eConstBool:     stream << "b";      break;
    case RegisterType::eLoop:          stream << "aL";     break;
    case RegisterType::eMiscType:
      // Handled when printing the register index
      break;
    case RegisterType::ePredicate:     stream << "p";      break;
    case RegisterType::ePixelTexCoord: stream << "t";      break;
    case RegisterType::eConstInt:      stream << "i";      break;
    case RegisterType::eColorOut:      stream << "oC";     break;
    case RegisterType::eDepthOut:      stream << "oDepth"; break;
    case RegisterType::eSampler:       stream << "s";      break;
    case RegisterType::eTempFloat16:   stream << "half";   break;
    case RegisterType::eLabel:         stream << "l";      break;

    default:
      stream << "(unhandled register type " << uint32_t(registerType) << ")";
      break;
  }
}


void Disassembler::disassembleDeclaration(std::ostream& stream, const Instruction& op, const Operand& operand) const {
  const Operand& dst = op.getRawOperand(1u);
  auto registerType = dst.getRegisterType();
  if (registerType == RegisterType::eSampler) {
    switch (operand.getTextureType()) {
      case TextureType::eTexture2D:   stream << "_2d";   break;
      case TextureType::eTextureCube: stream << "_cube"; break;
      case TextureType::eTexture3D:   stream << "_3d";   break;
    }
    return;
  }

  if (registerType == RegisterType::eOutput
    || registerType == RegisterType::eInput) {
    switch (operand.getSemanticUsage()) {
      case SemanticUsage::ePosition:
        stream << "_position" << operand.getSemanticIndex();
        break;
      case SemanticUsage::eBlendWeight:
        stream << "_weight";
        break;
      case SemanticUsage::eBlendIndices:
        stream << "_blend";
        break;
      case SemanticUsage::eNormal:
        stream << "_normal" << operand.getSemanticIndex();
        break;
      case SemanticUsage::ePointSize:
        stream << "_psize";
        break;
      case SemanticUsage::eTexCoord:
        stream << "_texcoord" << operand.getSemanticIndex();
        break;
      case SemanticUsage::eTangent:
        stream << "_tangent";
        break;
      case SemanticUsage::eBinormal:
        stream << "_binormal";
        break;
      case SemanticUsage::eTessFactor:
        stream << "_tessfactor";
        break;
      case SemanticUsage::ePositionT:
        stream << "_positiont";
        break;
      case SemanticUsage::eColor:
        if (operand.getSemanticIndex() == 0) {
          stream << "_color";
        } else {
          stream << "_specular" << (operand.getSemanticIndex() - 1u);
        }
        break;
      case SemanticUsage::eFog:
        stream << "_fog";
        break;
      case SemanticUsage::eDepth:
        stream << "_depth";
        break;
      case SemanticUsage::eSample:
        stream << "_sample" << operand.getSemanticIndex();
        break;
      default:
        stream << "_unknown" << operand.getSemanticIndex();
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


void Disassembler::disassembleComment(std::ostream& stream, const Instruction& op) {
  stream << " Comment: " << op.getComment() << std::endl;
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
