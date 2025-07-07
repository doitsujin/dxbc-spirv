#include "ir_validation.h"

namespace dxbc_spv::ir {

Validator::Validator(const Builder& builder)
: m_builder(builder), m_disasm(builder, Disassembler::Options()) {

}


Validator::~Validator() {

}


bool Validator::validateStructure(std::ostream& str) const {
  bool insideCodeBlock = false;

  SsaDef currentFunction = { };

  for (const auto& op : m_builder) {
    if (op.isDeclarative()) {
      if (insideCodeBlock) {
        str << "Declarative instruction found inside  code block." << std::endl;
        m_disasm.disassembleOp(str, op);
        return false;
      }
    } else {
      if (!insideCodeBlock)
        insideCodeBlock = true;

      switch (op.getOpCode()) {
        case OpCode::eFunction: {
          if (currentFunction) {
            str << "Function " << currentFunction << " not ended." << std::endl;
            m_disasm.disassembleOp(str, op);
            return false;
          }

          currentFunction = op.getDef();
        } break;

        case OpCode::eFunctionEnd: {
          if (!currentFunction) {
            str << "FunctionEnd encountered with no active function." << std::endl;
            m_disasm.disassembleOp(str, op);
            return false;
          }

          currentFunction = SsaDef();
        } break;

        default: {
          if (!currentFunction) {
            str << "Instruction encountered with no active function." << std::endl;
            m_disasm.disassembleOp(str, op);
            return false;
          }
        }
      }
    }

    for (uint32_t i = 0u; i < op.getFirstLiteralOperandIndex(); i++) {
      auto def = SsaDef(op.getOperand(i));

      if (def && !m_builder.getOp(def)) {
        str << "SSA def " << def << " does not reference valid instruction." << std::endl;
        m_disasm.disassembleOp(str, op);
        return false;
      }
    }
  }

  return true;
}


bool Validator::validateShaderIo(std::ostream& str) const {
  constexpr static uint32_t MaxLocations = 32u;
  constexpr static uint32_t MaxComponents = 4u;

  for (auto op = m_builder.getDeclarations().first; op != m_builder.getDeclarations().second; op++) {
    bool isIo = op->getOpCode() == OpCode::eDclInput ||
                op->getOpCode() == OpCode::eDclInputBuiltIn ||
                op->getOpCode() == OpCode::eDclOutput ||
                op->getOpCode() == OpCode::eDclOutputBuiltIn;

    if (!isIo)
      continue;

    const auto entryPointRef = SsaDef(op->getOperand(0u));
    const auto& entryPointOp = m_builder.getOp(entryPointRef);

    if (entryPointOp.getOpCode() != OpCode::eEntryPoint) {
      str << entryPointRef << " not a valid entry point." << std::endl;
      m_disasm.disassembleOp(str, *op);
      return false;
    }

    auto type = op->getType();
    auto base = type.getBaseType(0u);

    if (type.isStructType()) {
      str << type << " is not a valid type for shader I/O." << std::endl;
      m_disasm.disassembleOp(str, *op);
      return false;
    }

    /* Check that base type is allowed for shader I/O */
    ScalarType scalar = base.getBaseType();

    if (scalar != ScalarType::eBool && scalar != ScalarType::eU32 && scalar != ScalarType::eI32 && scalar != ScalarType::eF32) {
      str << scalar << " not a valid shader I/O type, must use 32-bit numeric types or bool." << std::endl;
      m_disasm.disassembleOp(str, *op);
      return false;
    }

    auto shaderStage = ShaderStage(entryPointOp.getOperand(entryPointOp.getFirstLiteralOperandIndex()));

    bool isBuiltIn = op->getOpCode() == OpCode::eDclInputBuiltIn ||
                     op->getOpCode() == OpCode::eDclOutputBuiltIn;

    bool isInput = op->getOpCode() == OpCode::eDclInput ||
                   op->getOpCode() == OpCode::eDclInputBuiltIn;

    /* Validate I/O location and component assignments */
    std::array<uint8_t, MaxLocations> componentMask = { };

    if (!isBuiltIn) {
      uint32_t location = uint32_t(op->getOperand(1u));
      uint32_t component = uint32_t(op->getOperand(2u));

      if (location >= MaxLocations) {
        str << "Location " << location << " not within allowed range of 0.." << (MaxLocations - 1u) << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }

      if (component + base.getVectorSize() > MaxComponents) {
        str << "Component index " << component << " too large to hold " << base << ", I/O locations have " << MaxComponents << " components." << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }

      if (shaderStage == ShaderStage::eVertex && isInput && component) {
        str << "Component index for vertex input must be 0." << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }

      uint32_t componentBits = ((1u << base.getVectorSize()) - 1u) << component;

      if (componentMask.at(location) & componentBits) {
        str << "I/O component overlap at location " << location << ", component " << component << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }

      componentMask.at(location) |= componentBits;
    }

    if (isInput) {
      bool hasInterpolationMode = op->getOperandCount() == (isBuiltIn ? 3u : 4u);

      if (shaderStage == ShaderStage::ePixel) {
        if (!hasInterpolationMode) {
          str << "Pixel shader inputs require interpolation mode." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }

        auto mode = InterpolationModes(op->getOperand(op->getOperandCount() - 1u));

        if (!base.isFloatType() && mode != InterpolationMode::eFlat) {
          str << "Integer inputs must use Flat interpolation." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }
      } else {
        if (hasInterpolationMode) {
          str << "Non-pixel shader inputs must not declare interpolation mode." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }
      }
    }

    Type expectedType;

    if (!isBuiltIn) {
      switch (shaderStage) {
        case ShaderStage::eVertex:
        case ShaderStage::ePixel: {
          expectedType = Type(base);
        } break;

        case ShaderStage::eGeometry: {
          expectedType = Type(base);

          if (isInput) {
            expectedType.addArrayDimension(primitiveVertexCount(
              findGsInputPrimitive(entryPointRef)));
          }
        } break;

        case ShaderStage::eHull: {
          expectedType = Type(base);

          if (isInput || type.isArrayType()) {
            uint32_t size = std::max(type.getArraySize(0u), 1u);
            expectedType.addArrayDimension(size);
          }
        } break;

        case ShaderStage::eDomain: {
          expectedType = Type(base);

          if (isInput && type.isArrayType()) {
            uint32_t size = std::max(type.getArraySize(0u), 1u);
            expectedType.addArrayDimension(size);
          }
        } break;

        default: {
          str << "Stage " << shaderStage << " cannot use I/O" << std::endl;
          m_disasm.disassembleOp(str, *op);
        } return false;
      }
    }

    if (!expectedType.isVoidType() && expectedType != type) {
      str << "Got type " << type << ", expected " << expectedType << std::endl;
      m_disasm.disassembleOp(str, *op);
      return false;
    }
  }

  return true;
}


bool Validator::validateLoadStoreOps(std::ostream& str) const {
  for (auto op = m_builder.getCode().first; op != m_builder.getCode().second; op++) {
    OpCode code = op->getOpCode();

    bool isLoad = code == OpCode::eParamLoad ||
                  code == OpCode::eTmpLoad ||
                  code == OpCode::eScratchLoad ||
                  code == OpCode::eLdsLoad ||
                  code == OpCode::ePushDataLoad ||
                  code == OpCode::eSpecConstantLoad ||
                  code == OpCode::eInputLoad ||
                  code == OpCode::eOutputLoad ||
                  code == OpCode::eDescriptorLoad ||
                  code == OpCode::eBufferLoad ||
                  code == OpCode::eMemoryLoad;

    bool isStore = code == OpCode::eTmpStore ||
                   code == OpCode::eScratchStore ||
                   code == OpCode::eLdsStore ||
                   code == OpCode::eOutputStore ||
                   code == OpCode::eBufferStore ||
                   code == OpCode::eMemoryStore;

    if (!isLoad && !isStore)
      continue;

    bool hasAddress = code == OpCode::eScratchLoad ||
                      code == OpCode::eScratchStore ||
                      code == OpCode::eLdsLoad ||
                      code == OpCode::eLdsStore ||
                      code == OpCode::ePushDataLoad ||
                      code == OpCode::eSpecConstantLoad ||
                      code == OpCode::eInputLoad ||
                      code == OpCode::eOutputLoad ||
                      code == OpCode::eOutputStore ||
                      code == OpCode::eBufferLoad ||
                      code == OpCode::eBufferStore ||
                      code == OpCode::eMemoryLoad ||
                      code == OpCode::eMemoryStore;

    bool isBuffer = code == OpCode::eBufferLoad ||
                    code == OpCode::eBufferStore;

    Type dclType = m_builder.getOp(SsaDef(op->getOperand(0u))).getType();
    Type valType = isStore
      ? m_builder.getOp(SsaDef(op->getOperand(op->getOperandCount() - 1u))).getType()
      : op->getType();

    Type expectedType = dclType;

    if (hasAddress) {
      auto addressRef = SsaDef(op->getOperand(1u));

      if (isBuffer) {
        /* The descriptor does not carry type information */
        const auto& descriptor = m_builder.getOp(SsaDef(op->getOperand(0u)));
        const auto& dcl = m_builder.getOp(SsaDef(descriptor.getOperand(0u)));

        expectedType = dcl.getType();

        /* Typed buffers are treated as implicitly unsized arrays */
        if ((dcl.getOpCode() == OpCode::eDclSrv || dcl.getOpCode() == OpCode::eDclUav) &&
            ResourceKind(dcl.getOperand(4u)) == ResourceKind::eBufferTyped)
          expectedType.addArrayDimension(0u);
      }

      if (addressRef) {
        const auto& address = m_builder.getOp(addressRef);

        auto addressType = address.getType();

        if (!addressType.isBasicType() || !addressType.getBaseType(0u).isIntType()) {
          str << "Load/store address must be scalar or vector of an integer type." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }

        auto addressBaseType = addressType.getBaseType(0u);

        for (uint32_t i = 0u; i < addressBaseType.getVectorSize(); i++) {
          bool isConstant = false;
          uint32_t constantValue = 0u;

          if ((isConstant = address.isConstant())) {
            constantValue = uint32_t(address.getOperand(0u));
          } else if (address.getOpCode() == OpCode::eCompositeConstruct) {
            const auto& component = m_builder.getOp(SsaDef(address.getOperand(i)));

            if ((isConstant = component.isConstant()))
              constantValue = uint32_t(component.getOperand(0u));
          }

          if (!expectedType.isArrayType() && !isConstant) {
            str << "Address into " << expectedType << " must be constant." << std::endl;
            m_disasm.disassembleOp(str, *op);
            return false;
          }

          expectedType = expectedType.getSubType(constantValue);
        }
      }
    } else if (code == OpCode::eDescriptorLoad) {
      const auto& dcl = m_builder.getOp(SsaDef(op->getOperand(0u)));
      const auto& index = m_builder.getOp(SsaDef(op->getOperand(1u)));

      uint32_t count = 0u;

      switch (dcl.getOpCode()) {
        case OpCode::eDclSampler:
          expectedType = ScalarType::eSampler;
          count = uint32_t(dcl.getOperand(3u));
          break;

        case OpCode::eDclCbv:
          expectedType = ScalarType::eCbv;
          count = uint32_t(dcl.getOperand(3u));
          break;

        case OpCode::eDclSrv:
          expectedType = ScalarType::eSrv;
          count = uint32_t(dcl.getOperand(3u));
          break;

        case OpCode::eDclUav:
          expectedType = ScalarType::eUav;
          count = uint32_t(dcl.getOperand(3u));
          break;

        case OpCode::eDclUavCounter: {
          const auto& uav = m_builder.getOp(SsaDef(op->getOperand(1u)));
          expectedType = ScalarType::eUavCounter;
          count = uint32_t(uav.getOperand(3u));
        } break;

        default:
          str << "Base operand for DescriptorLoad must be a resource." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
      }

      if (!index.getType().isScalarType() || !index.getType().getBaseType(0u).isIntType()) {
        str << "Descriptor index must be scalar integer." << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }

      if (count && index.isConstant()) {
        uint32_t indexValue = uint32_t(index.getOperand(0u));

        if (indexValue >= count) {
          str << "Descriptor index " << indexValue << " out of array bounds of array " << expectedType << "[" << count << "]" << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }
      }
    }

    if (valType != expectedType) {
      bool allowVectorAccessOnScalar = code == OpCode::eMemoryLoad ||
                                       code == OpCode::eMemoryStore;

      if (code == OpCode::eBufferLoad || code == OpCode::eBufferStore) {
        const auto& descriptor = m_builder.getOp(SsaDef(op->getOperand(0u)));
        const auto& dcl = m_builder.getOp(SsaDef(descriptor.getOperand(0u)));

        if (dcl.getOpCode() == OpCode::eDclSrv || dcl.getOpCode() == OpCode::eDclUav) {
          auto kind = ResourceKind(dcl.getOperand(4u));

          allowVectorAccessOnScalar = kind == ResourceKind::eBufferStructured ||
                                      kind == ResourceKind::eBufferRaw;
        }
      }

      if (!allowVectorAccessOnScalar || !valType.isVectorType() || valType.getSubType(0u) != expectedType) {
        str << "Got " << (isLoad ? "load" : "store") << " type " << valType << ", expected " << expectedType << "." << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }
    }
  }

  return true;
}


bool Validator::validateStructuredCfg(std::ostream& str) const {
  SsaDef currentBlock = { };

  for (auto op = m_builder.getCode().first; op != m_builder.getCode().second; op++) {
    switch (op->getOpCode()) {
      case OpCode::eFunction:
      case OpCode::eFunctionEnd: {
        if (currentBlock) {
          str << "Block " << currentBlock << " not ended." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }
      } break;

      case OpCode::eLabel: {
        if (currentBlock) {
          str << "Block " << currentBlock << " not ended." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }

        currentBlock = op->getDef();
      } break;

      case OpCode::eBranch:
      case OpCode::eBranchConditional:
      case OpCode::eSwitch:
      case OpCode::eUnreachable:
      case OpCode::eReturn: {
        if (!currentBlock) {
          str << "Instruction not inside any block." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }

        currentBlock = SsaDef();
      } break;

      default: {
        if (!currentBlock) {
          str << "Instruction not inside any block." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }
      }
    }
  }

  return true;
}


bool Validator::validateFinalIr(std::ostream& str) const {
  return validateStructure(str)
      && validateShaderIo(str)
      && validateLoadStoreOps(str)
      && validateStructuredCfg(str);
}


PrimitiveType Validator::findGsInputPrimitive(SsaDef entryPoint) const {
  for (auto op = m_builder.getDeclarations().first; op != m_builder.getDeclarations().second; op++) {
    if (op->getOpCode() == OpCode::eSetGsInputPrimitive && SsaDef(op->getOperand(0u)) == entryPoint)
      return PrimitiveType(op->getOperand(1u));
  }

  return PrimitiveType::ePoints;
}

}
