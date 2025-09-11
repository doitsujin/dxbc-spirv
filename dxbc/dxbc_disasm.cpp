#include <cmath>
#include <iomanip>
#include <sstream>

#include "dxbc_disasm.h"

namespace dxbc_spv::dxbc {

void Disassembler::disassembleOp(std::ostream& stream, const Instruction& op) {
  if (opEndsNestedBlock(op))
    decrementIndentation();

  emitLineNumber(stream);
  emitIndentation(stream);

  disassembleOpcodeToken(stream, op);

  auto layout = op.getLayout(m_info);

  uint32_t nDst = 0u;
  uint32_t nSrc = 0u;
  uint32_t nImm = 0u;
  uint32_t nExtra = 0u;

  bool first = true;

  for (const auto& operand : layout.operands) {
    bool inBounds = false;

    stream << (std::exchange(first, false) ? " " : ", ");

    switch (operand.kind) {
      case OperandKind::eDstReg:
        if ((inBounds = (nDst < op.getDstCount())))
          disassembleOperand(stream, op, op.getDst(nDst++));
        break;

      case OperandKind::eSrcReg:
        if ((inBounds = (nSrc < op.getSrcCount())))
          disassembleOperand(stream, op, op.getSrc(nSrc++));
        break;

      case OperandKind::eImm32:
        if ((inBounds = (nImm < op.getImmCount()))) {
          if (!disassembleEnumOperand(stream, op, nImm))
            disassembleOperand(stream, op, op.getImm(nImm));

          nImm++;
        }
        break;

      case OperandKind::eExtra:
        if ((inBounds = (nExtra < op.getExtraCount()))) {
          if (!disassembleExtraOperand(stream, op, nExtra))
            disassembleOperand(stream, op, op.getExtra(nExtra));

          nExtra++;
        }
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
  auto token = op.getOpToken();
  stream << token.getOpCode();

  disassembleSampleControls(stream, token);
  disassembleResourceDim(stream, token);
  disassembleOpFlags(stream, token);
  disassemblePreciseMask(stream, token);
}


void Disassembler::disassemblePreciseMask(std::ostream& stream, const OpToken& token) const {
  auto mask = token.getPreciseMask();

  if (mask)
    stream << " [precise(" << mask << ")]";
}


void Disassembler::disassembleResourceDim(std::ostream& stream, const OpToken& token) const {
  auto dimToken = token.getResourceDimToken();

  if (!dimToken)
    return;

  /* Hide resource dim for ops that only operate on raw
   * and structured buffers anyway */
  auto opCode = token.getOpCode();

  if (opCode == OpCode::eLdRaw ||
      opCode == OpCode::eLdStructured ||
      opCode == OpCode::eStoreRaw ||
      opCode == OpCode::eStoreStructured)
    return;

  stream << " (" << dimToken.getDim() << ")";
}


void Disassembler::disassembleSampleControls(std::ostream& stream, const OpToken& token) const {
  auto controlToken = token.getSampleControlToken();

  if (!controlToken)
    return;

  /* Only show relevant components */
  uint32_t components = 3u;

  auto dimToken = token.getResourceDimToken();

  if (dimToken) {
    auto dim = resolveResourceDim(dimToken.getDim());

    if (dim)
      components = ir::resourceCoordComponentCount(*dim);
  }

  stream << "_aoffimmi(" << controlToken.u();
  if (components > 1u) stream << "," << controlToken.v();
  if (components > 2u) stream << "," << controlToken.w();
  stream << ')';
}


void Disassembler::disassembleOpFlags(std::ostream& stream, const OpToken& token) const {
  auto opCode = token.getOpCode();

  switch (opCode) {
    case OpCode::eDclInputPs:
    case OpCode::eDclInputPsSgv:
    case OpCode::eDclInputPsSiv: {
      stream << " (" << token.getInterpolationMode() << ")";
    } break;

    case OpCode::eDclGlobalFlags: {
      stream << ' ' << token.getGlobalFlags();
    } break;

    case OpCode::eDclGsInputPrimitive: {
      stream << ' ' << token.getPrimitiveType();
    } break;

    case OpCode::eDclGsOutputPrimitiveTopology: {
      stream << ' ' << token.getPrimitiveTopology();
    } break;

    case OpCode::eDclTessDomain: {
      stream << ' ' << token.getTessellatorDomain();
    } break;

    case OpCode::eDclTessPartitioning: {
      stream << ' ' << token.getTessellatorPartitioning();
    } break;

    case OpCode::eDclTessOutputPrimitive: {
      stream << ' ' << token.getTessellatorOutput();
    } break;

    case OpCode::eDclInputControlPointCount:
    case OpCode::eDclOutputControlPointCount: {
      stream << ' ' << token.getControlPointCount();
    } break;

    case OpCode::eDclSampler: {
      auto mode = token.getSamplerMode();

      if (mode != SamplerMode::eDefault)
        stream << " (" << token.getSamplerMode() << ")";
    } break;

    case OpCode::eDclConstantBuffer: {
      if (token.getCbvDynamicIndexingFlag())
        stream << " (dynamicallyindexed)";
    } break;

    case OpCode::eDclUavTyped:
    case OpCode::eDclUavRaw:
    case OpCode::eDclUavStructured: {
      for (auto f : token.getUavFlags())
        stream << '_' << f;
    } break;

    case OpCode::eSync: {
      /* For some reason, d3dcompiler will print the flags in
       * reverse order, so do the same here for consistency */
      auto flags = uint32_t(token.getSyncFlags());

      while (flags) {
        auto msb = 0x80000000u >> util::lzcnt32(flags);
        flags -= msb;

        stream << '_' << SyncFlag(msb);
      }
    } break;

    case OpCode::eSampleInfo: {
      stream << '_' << token.getReturnType();
    } break;

    case OpCode::eResInfo: {
      stream << '_' << token.getResInfoType();
    } break;

    case OpCode::eBreakc:
    case OpCode::eCallc:
    case OpCode::eContinuec:
    case OpCode::eDiscard:
    case OpCode::eIf:
    case OpCode::eRetc: {
      stream << '_' << token.getZeroTest();
    } break;

    case OpCode::eDerivRtx:
    case OpCode::eDerivRty:
    case OpCode::eDerivRtxFine:
    case OpCode::eDerivRtyFine:
    case OpCode::eDerivRtxCoarse:
    case OpCode::eDerivRtyCoarse:
    case OpCode::eMov:
    case OpCode::eMovc:
    case OpCode::eAdd:
    case OpCode::eDiv:
    case OpCode::eDp2:
    case OpCode::eDp3:
    case OpCode::eDp4:
    case OpCode::eExp:
    case OpCode::eFrc:
    case OpCode::eLog:
    case OpCode::eMad:
    case OpCode::eMax:
    case OpCode::eMin:
    case OpCode::eMul:
    case OpCode::eRoundNe:
    case OpCode::eRoundNi:
    case OpCode::eRoundPi:
    case OpCode::eRoundZ:
    case OpCode::eRcp:
    case OpCode::eRsq:
    case OpCode::eSinCos:
    case OpCode::eSqrt:
    case OpCode::eDAdd:
    case OpCode::eDMax:
    case OpCode::eDMin:
    case OpCode::eDMul:
    case OpCode::eDRcp:
    case OpCode::eDDiv:
    case OpCode::eDFma:
    case OpCode::eDMov:
    case OpCode::eDMovc: {
      if (token.isSaturated())
        stream << "_sat";
    } break;

    default:
      break;
  }
}


bool Disassembler::disassembleEnumOperand(std::ostream& stream, const Instruction& op, uint32_t index) const {
  const auto& operand = op.getImm(index);

  switch (op.getOpToken().getOpCode()) {
    case OpCode::eDclSampler: {
      if (index == 0u) {
        stream << "space:" << operand.getImmediate<uint32_t>(0u);
        return true;
      }
    } break;

    case OpCode::eDclResourceRaw:
    case OpCode::eDclResourceStructured:
    case OpCode::eDclUavRaw:
    case OpCode::eDclUavStructured: {
      if (index == 0u) {
        stream << "stride:" << operand.getImmediate<uint32_t>(0u);
        return true;
      }
    }
    [[fallthrough]];

    case OpCode::eDclConstantBuffer: {
      if (index == 0u) {
        stream << "size:" << operand.getImmediate<uint32_t>(0u);
        return true;
      }

      if (index == 1u) {
        stream << "space:" << operand.getImmediate<uint32_t>(0u);
        return true;
      }
    } break;

    case OpCode::eDclResource:
    case OpCode::eDclUavTyped: {
      if (index == 0u) {
        ResourceTypeToken token(operand.getImmediate<uint32_t>(0u));

        stream << op.getOpToken().getResourceDim()
              << "(" << token.x() << "," << token.y()
              << "," << token.z() << "," << token.w() << ")";
        return true;
      }

      if (index == 1u) {
        stream << "space:" << operand.getImmediate<uint32_t>(0u);
        return true;
      }
    } break;

    case OpCode::eDclInputSgv:
    case OpCode::eDclInputSiv:
    case OpCode::eDclInputPsSgv:
    case OpCode::eDclInputPsSiv:
    case OpCode::eDclOutputSgv:
    case OpCode::eDclOutputSiv: {
      if (index == 0u) {
        stream << operand.getImmediate<Sysval>(0u);
        return true;
      }
    } break;

    case OpCode::eDclFunctionBody: {
      if (index == 0u) {
        stream << "fb" << operand.getImmediate<uint32_t>(0u);
        return true;
      }
    } break;

    case OpCode::eDclFunctionTable: {
      if (index == 0u) {
        stream << "ft" << operand.getImmediate<uint32_t>(0u);
        return true;
      }
    } break;

    case OpCode::eDclInterface: {
      if (index == 0u) {
        stream << "fp" << operand.getImmediate<uint32_t>(0u);
        return true;
      }

      if (index == 2u) {
        auto imm = operand.getImmediate<uint32_t>(0u);
        stream << "[tbl: " << (imm & 0xffff) << "; len: " << (imm >> 16u) << "]";
        return true;
      }
    } break;

    default:
      break;
  }

  return false;
}


bool Disassembler::disassembleExtraOperand(std::ostream& stream, const Instruction& op, uint32_t index) const {
  const auto& operand = op.getExtra(index);

  switch (op.getOpToken().getOpCode()) {
    case OpCode::eDclFunctionTable: {
      stream << "fb" << operand.getImmediate<uint32_t>(0u);
    } return true;

    case OpCode::eDclInterface: {
      stream << "ft" << operand.getImmediate<uint32_t>(0u);
    } return true;

    default:
      return false;
  }
}


void Disassembler::disassembleOperand(std::ostream& stream, const Instruction& op, const Operand& arg) const {
  /** Handle modifiers */
  auto modifiers = arg.getModifiers();

  if (modifiers.isNonUniform())
    stream << "nonuniform(";

  if (modifiers.isNegated())
    stream << '-';

  if (modifiers.isAbsolute())
    stream << '|';

  /* Print basic register prefix */
  bool bindless = m_info.getVersion().first == 5u &&
                  m_info.getVersion().second >= 1u;

  switch (arg.getRegisterType()) {
    case RegisterType::eTemp:               stream << "r"; break;
    case RegisterType::eInput:              stream << "v"; break;
    case RegisterType::eOutput:             stream << "o"; break;
    case RegisterType::eIndexableTemp:      stream << "x"; break;
    case RegisterType::eSampler:            stream << (bindless ? "S" : "s"); break;
    case RegisterType::eResource:           stream << (bindless ? "T" : "t"); break;
    case RegisterType::eCbv:                stream << (bindless ? "CB" : "cb"); break;
    case RegisterType::eIcb:                stream << "icb"; break;
    case RegisterType::eLabel:              stream << "l"; break;
    case RegisterType::ePrimitiveId:        stream << "vPrim"; break;
    case RegisterType::eDepth:              stream << "ODepth"; break;
    case RegisterType::eNull:               stream << "null"; break;
    case RegisterType::eRasterizer:         stream << "vRasterizer"; break;
    case RegisterType::eCoverageOut:        stream << "oCoverage"; break;
    case RegisterType::eStream:             stream << "m"; break;
    case RegisterType::eFunctionBody:       stream << "fb"; break;
    case RegisterType::eFunctionTable:      stream << "ft"; break;
    case RegisterType::eInterface:          stream << "fp"; break;
    case RegisterType::eFunctionInput:      stream << "fi"; break;
    case RegisterType::eFunctionOutput:     stream << "fo"; break;
    case RegisterType::eControlPointId:     stream << "vControlPoint"; break;
    case RegisterType::eForkInstanceId:     stream << "vForkInstanceId"; break;
    case RegisterType::eJoinInstanceId:     stream << "vJoinInstanceId"; break;
    case RegisterType::eControlPointIn:     stream << "vicp"; break;
    case RegisterType::eControlPointOut:    stream << "vocp"; break;
    case RegisterType::ePatchConstant:      stream << "vpc"; break;
    case RegisterType::eTessCoord:          stream << "vDomain"; break;
    case RegisterType::eThis:               stream << "this"; break;
    case RegisterType::eUav:                stream << (bindless ? "U" : "u"); break;
    case RegisterType::eTgsm:               stream << "g"; break;
    case RegisterType::eThreadId:           stream << "vThreadID"; break;
    case RegisterType::eThreadGroupId:      stream << "vGroupID"; break;
    case RegisterType::eThreadIdInGroup:    stream << "vThreadIDInGroup"; break;
    case RegisterType::eThreadIndexInGroup: stream << "vThreadIDInGroupFlattened"; break;
    case RegisterType::eCoverageIn:         stream << "vCoverage"; break;
    case RegisterType::eGsInstanceId:       stream << "vInstanceID"; break;
    case RegisterType::eDepthGe:            stream << "oDepthGe"; break;
    case RegisterType::eDepthLe:            stream << "oDepthLe"; break;
    case RegisterType::eCycleCounter:       stream << "vCycleCounter"; break;
    case RegisterType::eStencilRef:         stream << "oStencilRef"; break;
    case RegisterType::eInnerCoverage:      stream << "vInnerCoverage"; break;

    case RegisterType::eImm32: disassembleImmediate(stream, arg); return;
    case RegisterType::eImm64: disassembleImmediate(stream, arg); return;

    default: stream << "(unhandled register type " << uint32_t(arg.getRegisterType()) << ")";
  }

  /** Print indices */
  bool flip = flipIndices(arg);

  for (uint32_t i = 0u; i < arg.getIndexDimensions(); i++) {
    uint32_t dim = flip ? (i ^ 1u) : i;

    auto indexType = arg.getIndexType(dim);

    bool useRelative = hasRelativeIndexing(indexType) ||
      arg.getRegisterType() == RegisterType::eThis;

    if (i || useRelative)
      stream << "[";

    if (hasRelativeIndexing(indexType)) {
      if (indexType != IndexType::eRelative)
        stream << arg.getIndex(dim) << "+";

      disassembleOperand(stream, op, op.getRawOperand(arg.getIndexOperand(dim)));
    } else {
      stream << arg.getIndex(dim);
    }

    if (i || useRelative)
      stream << "]";
  }

  /* Handle write mask or swizzle, depending on context */
  if (arg.getComponentCount() == ComponentCount::e4Component) {
    switch (arg.getSelectionMode()) {
      case SelectionMode::eMask:    stream << "." << arg.getWriteMask(); break;
      case SelectionMode::eSwizzle: stream << "." << arg.getSwizzle(); break;
      case SelectionMode::eSelect1: stream << "." << arg.getSwizzle().x(); break;
    }
  }

  /* Handle modifiers */
  if (modifiers.isAbsolute())
    stream << '|';

  if (modifiers.isNonUniform())
    stream << ')';

  if (modifiers.getPrecision() != MinPrecision::eNone)
    stream << " {" << modifiers.getPrecision() << "}";
}


void Disassembler::disassembleImmediate(std::ostream& stream, const Operand& arg) const {
  bool is64Bit = arg.getRegisterType() == RegisterType::eImm64;

  /* Determine number of components based on the operand token */
  uint32_t componentCount = 1u;

  if (arg.getComponentCount() == ComponentCount::e4Component)
    componentCount = is64Bit ? 2u : 4u;

  if (componentCount > 1u)
    stream << '(';

  for (uint32_t i = 0u; i < componentCount; i++) {
    auto type = arg.getInfo().type;

    if (i)
      stream << ", ";

    /* Try to resolve ambiguous types using min precision info */
    auto precision = arg.getModifiers().getPrecision();

    switch (precision) {
      case MinPrecision::eNone:
        break;

      case MinPrecision::eMin16Float:
      case MinPrecision::eMin10Float:
        if (type == ir::ScalarType::eUnknown)
          type = ir::ScalarType::eF32;
        break;

      case MinPrecision::eMin16Sint:
        if (type == ir::ScalarType::eUnknown || type == ir::ScalarType::eU32)
          type = ir::ScalarType::eI32;
        break;

      case MinPrecision::eMin16Uint:
        if (type == ir::ScalarType::eUnknown || type == ir::ScalarType::eU32)
          type = ir::ScalarType::eU32;
        break;
    }

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
}


bool Disassembler::flipIndices(const Operand& operand) {
  if (operand.getIndexDimensions() != 2u)
    return false;

  auto type = operand.getRegisterType();

  return type == RegisterType::eInput ||
         type == RegisterType::eControlPointIn ||
         type == RegisterType::eControlPointOut;
}


bool Disassembler::opBeginsNestedBlock(const Instruction& op) {
  auto opCode = op.getOpToken().getOpCode();

  return opCode == OpCode::eIf ||
         opCode == OpCode::eElse ||
         opCode == OpCode::eLoop ||
         opCode == OpCode::eSwitch ||
         opCode == OpCode::eCase ||
         opCode == OpCode::eDefault;
}


bool Disassembler::opEndsNestedBlock(const Instruction& op) {
  auto opCode = op.getOpToken().getOpCode();

  return opCode == OpCode::eElse ||
         opCode == OpCode::eCase ||
         opCode == OpCode::eDefault ||
         opCode == OpCode::eEndIf ||
         opCode == OpCode::eEndLoop ||
         opCode == OpCode::eEndSwitch;
}

}
