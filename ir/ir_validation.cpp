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

  std::array<uint8_t, 2u * MaxLocations> componentMask = { };

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
      uint32_t baseLocation = isInput ? 0u : MaxLocations;

      if (componentMask.at(location + baseLocation) & componentBits) {
        str << "I/O component overlap at location " << location << ", component " << component << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }

      componentMask.at(location + baseLocation) |= componentBits;
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
            bool isControlPointId = isBuiltIn &&
              (BuiltIn(op->getOperand(1u)) == BuiltIn::eTessControlPointId ||
               BuiltIn(op->getOperand(1u)) == BuiltIn::ePrimitiveId);

            if (!isControlPointId) {
              uint32_t size = std::max(type.getArraySize(0u), 1u);
              expectedType.addArrayDimension(size);
            }
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


bool Validator::validateResources(std::ostream& str) const {
  for (auto op = m_builder.getDeclarations().first; op != m_builder.getDeclarations().second; op++) {
    if (op->getOpCode() == OpCode::eDclCbv ||
        op->getOpCode() == OpCode::eDclSrv ||
        op->getOpCode() == OpCode::eDclUav ||
        op->getOpCode() == OpCode::eDclUavCounter ||
        op->getOpCode() == OpCode::eDclSampler) {
      auto epDef = SsaDef(op->getOperand(0u));

      if (!epDef || m_builder.getOp(epDef).getOpCode() != OpCode::eEntryPoint) {
        str << epDef << " is not a valid entry point." << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }
    }

    if (op->getOpCode() == OpCode::eDclSrv || op->getOpCode() == OpCode::eDclUav) {
      auto kind = ResourceKind(op->getOperand(4u));
      auto type = op->getType();

      if (kind == ResourceKind::eBufferRaw) {
        if (!type.isUnboundedArray() || !type.getSubType(0u).isScalarType()) {
          str << type << " is not a valid type for raw buffers." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }
      } else if (kind == ResourceKind::eBufferStructured) {
        if (!type.isUnboundedArray()) {
          str << type << " is not a valid type for structured buffers." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }
      } else {
        if (!type.isScalarType()) {
          str << type << " is not a valid type for typed resources." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }
      }
    }

    if (op->getOpCode() == OpCode::eDclUav) {
      auto kind = ResourceKind(op->getOperand(4u));
      auto flags = UavFlags(op->getOperand(5u));

      if (kind == ResourceKind::eBufferRaw || kind == ResourceKind::eBufferStructured) {
        if (flags & UavFlag::eFixedFormat) {
          str << UavFlag::eFixedFormat << " only allowed on typed resources." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }
      }
    }

    if (op->getOpCode() == OpCode::eDclUavCounter && op->getType() != ScalarType::eU32) {
      str << "UAV counters must be declared as u32." << std::endl;
      m_disasm.disassembleOp(str, *op);
      return false;
    }

    if (op->getOpCode() == OpCode::eDclSampler && op->getType() != Type()) {
      str << "Samplers must be declared as void." << std::endl;
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
                      code == OpCode::eInputLoad ||
                      code == OpCode::eOutputLoad ||
                      code == OpCode::eOutputStore ||
                      code == OpCode::eBufferLoad ||
                      code == OpCode::eBufferStore ||
                      code == OpCode::eMemoryLoad ||
                      code == OpCode::eMemoryStore;

    bool isBuffer = code == OpCode::eBufferLoad ||
                    code == OpCode::eBufferStore;

    uint32_t dclArgIndex = code == OpCode::eParamLoad ? 1u : 0u;

    /* Check whether the base instruction is valid */
    const auto& dclOp = m_builder.getOp(SsaDef(op->getOperand(dclArgIndex)));

    bool dclIsValid = true;

    switch (code) {
      case OpCode::eTmpLoad:
      case OpCode::eTmpStore:
        dclIsValid = dclOp.getOpCode() == OpCode::eDclTmp;
        break;

      case OpCode::eScratchLoad:
      case OpCode::eScratchStore:
        dclIsValid = dclOp.getOpCode() == OpCode::eDclScratch;
        break;

      case OpCode::ePushDataLoad:
        dclIsValid = dclOp.getOpCode() == OpCode::eDclPushData;
        break;

      case OpCode::eInputLoad:
        dclIsValid = dclOp.getOpCode() == OpCode::eDclInput ||
                     dclOp.getOpCode() == OpCode::eDclInputBuiltIn;
        break;

      case OpCode::eLdsLoad:
      case OpCode::eLdsStore:
        dclIsValid = dclOp.getOpCode() == OpCode::eDclLds;
        break;

      case OpCode::eOutputLoad:
      case OpCode::eOutputStore:
        dclIsValid = dclOp.getOpCode() == OpCode::eDclOutput ||
                     dclOp.getOpCode() == OpCode::eDclOutputBuiltIn;
        break;

      case OpCode::eBufferLoad:
      case OpCode::eBufferStore:
        dclIsValid = dclOp.getOpCode() == OpCode::eDescriptorLoad;
        break;

      case OpCode::eMemoryLoad:
      case OpCode::eMemoryStore:
        dclIsValid = dclOp.getOpCode() == OpCode::ePointer;
        break;


      default:
        break;
    }

    if (!dclIsValid) {
      str << "Base op " << dclOp.getOpCode() << " not valid for instruction." << std::endl;
      m_disasm.disassembleOp(str, *op);
      return false;
    }

    Type dclType = dclOp.getType();
    Type valType = isStore
      ? m_builder.getOp(SsaDef(op->getOperand(op->getFirstLiteralOperandIndex() - 1u))).getType()
      : op->getType();

    Type expectedType = dclType;

    if (hasAddress) {
      auto addressRef = SsaDef(op->getOperand(1u));

      if (isBuffer) {
        /* The descriptor does not carry type information */
        const auto& descriptor = m_builder.getOp(SsaDef(op->getOperand(0u)));
        const auto& dcl = m_builder.getOpForOperand(descriptor, 0u);

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
            const auto& component = m_builder.getOpForOperand(address, i);

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
        const auto& dcl = m_builder.getOpForOperand(descriptor, 0u);

        if (dcl.getOpCode() == OpCode::eDclSrv || dcl.getOpCode() == OpCode::eDclUav) {
          auto kind = ResourceKind(dcl.getOperand(4u));

          allowVectorAccessOnScalar = kind == ResourceKind::eBufferStructured ||
                                      kind == ResourceKind::eBufferRaw;

          if (kind == ResourceKind::eBufferTyped)
            expectedType = BasicType(expectedType.getBaseType(0u).getBaseType(), 4u);
        }
      }

      if (code == OpCode::eBufferLoad && (op->getFlags() & OpFlag::eSparseFeedback))
        expectedType = Type().addStructMember(ScalarType::eU32).addStructMember(expectedType.getBaseType(0u));

      if (valType != expectedType) {
        if (!allowVectorAccessOnScalar || !valType.isVectorType() || valType.getSubType(0u) != expectedType) {
          str << "Got " << (isLoad ? "load" : "store") << " type " << valType << ", expected " << expectedType << "." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }
      }
    }
  }

  return true;
}


bool Validator::validateImageOps(std::ostream& str) const {
  for (auto op = m_builder.getCode().first; op != m_builder.getCode().second; op++) {
    auto opCode = op->getOpCode();

    bool isImageSrvOp = opCode == OpCode::eImageLoad ||
                        opCode == OpCode::eImageQuerySize ||
                        opCode == OpCode::eImageQueryMips ||
                        opCode == OpCode::eImageQuerySamples ||
                        opCode == OpCode::eImageSample ||
                        opCode == OpCode::eImageGather ||
                        opCode == OpCode::eImageComputeLod;

    bool isImageUavOp = opCode == OpCode::eImageLoad ||
                        opCode == OpCode::eImageStore ||
                        opCode == OpCode::eImageAtomic ||
                        opCode == OpCode::eImageQuerySize;

    if (!isImageSrvOp && !isImageUavOp)
      continue;

    /* Check that the descriptor type is valid for the resource declaration */
    auto imageDescriptorRef = SsaDef(op->getOperand(0u));
    const auto& imageDescriptor = m_builder.getOp(imageDescriptorRef);

    if (imageDescriptor.getOpCode() != OpCode::eDescriptorLoad) {
      str << imageDescriptorRef << " is not a valid image descriptor." << std::endl;
      m_disasm.disassembleOp(str, *op);
      return false;
    }

    if (imageDescriptor.getType() == ScalarType::eSrv) {
      if (!isImageSrvOp) {
        str << "Invalid operation on image SRV." << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }
    } else if (imageDescriptor.getType() == ScalarType::eUav) {
      if (!isImageUavOp) {
        str << "Invalid operation on image UAV." << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }
    } else {
      str << imageDescriptor.getType() << " is not a valid image descriptor type." << std::endl;
      m_disasm.disassembleOp(str, *op);
      return false;
    }

    /* Check that the image declaration is valid */
    const auto& image = m_builder.getOpForOperand(imageDescriptor, 0u);

    auto kind = ResourceKind(image.getOperand(4u));
    auto flags = UavFlags(image.getOperand(5u));

    bool isValidUavKind = kind == ResourceKind::eImage1D ||
                          kind == ResourceKind::eImage1DArray ||
                          kind == ResourceKind::eImage2D ||
                          kind == ResourceKind::eImage2DArray ||
                          kind == ResourceKind::eImage3D;

    bool isValidSrvKind = kind == ResourceKind::eImage2DMS ||
                          kind == ResourceKind::eImage2DMSArray ||
                          kind == ResourceKind::eImageCube ||
                          kind == ResourceKind::eImageCubeArray ||
                          isValidUavKind;

    if (((imageDescriptor.getType() == ScalarType::eSrv) && !isValidSrvKind) ||
        ((imageDescriptor.getType() == ScalarType::eUav) && !isValidUavKind)) {
      str << kind << " is not a valid kind for descriptor type " << imageDescriptor.getType() << std::endl;
      m_disasm.disassembleOp(str, *op);
      return false;
    }

    /* Validate actual opcodes */
    ScalarType imageType = image.getType().getBaseType(0u).getBaseType();
    SsaDef sampler, mip, layer, coord, sample, offset, dref;
    Type expectedType;

    switch (opCode) {
      case OpCode::eImageLoad: {
        mip = SsaDef(op->getOperand(1u));
        layer = SsaDef(op->getOperand(2u));
        coord = SsaDef(op->getOperand(3u));
        sample = SsaDef(op->getOperand(4u));
        offset = SsaDef(op->getOperand(5u));

        expectedType = BasicType(imageType, 4u);
      } break;

      case OpCode::eImageStore: {
        layer = SsaDef(op->getOperand(1u));
        coord = SsaDef(op->getOperand(2u));

        expectedType = Type();

        /* Validate store value type */
        const auto& valueOp = m_builder.getOp(SsaDef(op->getOperand(3u)));

        if (valueOp.getType() != BasicType(imageType, 4u)) {
          str << "Store value must be " << BasicType(imageType, 4u) << " for given image." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }
      } break;

      case OpCode::eImageAtomic: {
        if (!(flags & UavFlag::eFixedFormat)) {
          str << UavFlag::eFixedFormat << " not set on image for atomic access." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }

        if (op->getType() != Type() && op->getType() != imageType) {
          str << "Invalid return type for image of type " << imageType << "." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }

        layer = SsaDef(op->getOperand(1u));
        coord = SsaDef(op->getOperand(2u));

        for (uint32_t i = 3u; i < op->getFirstLiteralOperandIndex(); i++) {
          if (m_builder.getOp(SsaDef(op->getOperand(i))).getType() != imageType) {
            str << "Invalid operand type for image of type " << imageType << "." << std::endl;
            m_disasm.disassembleOp(str, *op);
            return false;
          }
        }

        if (!op->getType().isVoidType())
          expectedType = imageType;
      } break;

      case OpCode::eImageQuerySize: {
        mip = SsaDef(op->getOperand(1u));

        expectedType = Type()
          .addStructMember(BasicType(ScalarType::eU32, resourceDimensions(kind)))
          .addStructMember(ScalarType::eU32);
      } break;

      case OpCode::eImageQueryMips: {
        if (resourceIsMultisampled(kind) || imageDescriptor.getType() == ScalarType::eUav) {
          str << "Image cannot have more than one mip level." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }

        expectedType = ScalarType::eU32;
      } break;

      case OpCode::eImageQuerySamples: {
        if (!resourceIsMultisampled(kind)) {
          str << "Image is not multisampled." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }

        expectedType = ScalarType::eU32;
      } break;

      case OpCode::eImageSample: {
        sampler = SsaDef(op->getOperand(1u));
        layer = SsaDef(op->getOperand(2u));
        coord = SsaDef(op->getOperand(3u));
        offset = SsaDef(op->getOperand(4u));
        dref = SsaDef(op->getOperand(10u));

        const auto& lodIndex = m_builder.getOp(SsaDef(op->getOperand(5u)));
        const auto& lodBias = m_builder.getOp(SsaDef(op->getOperand(6u)));
        const auto& lodClamp = m_builder.getOp(SsaDef(op->getOperand(7u)));

        const auto& derivX = m_builder.getOp(SsaDef(op->getOperand(8u)));
        const auto& derivY = m_builder.getOp(SsaDef(op->getOperand(9u)));

        if (bool(derivX) != bool(derivY)) {
          str << "Either none or both derivatives must be defined." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }

        if (lodIndex && derivX) {
          str << "Cannot define both LOD and derivatives." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }

        if (lodIndex && (lodClamp || lodBias)) {
          str << "Cannot define both LOD and LOD clamp or bias." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }

        if (dref && (derivX || lodBias)) {
          str << "Cannot use derivatives or LOD bias with depth compare." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }

        if (lodIndex && (!lodIndex.getType().isScalarType() || !lodIndex.getType().getBaseType(0u).isFloatType())) {
          str << "LOD index must be scalar float." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }

        if (lodBias && (!lodBias.getType().isScalarType() || !lodBias.getType().getBaseType(0u).isFloatType())) {
          str << "LOD bias must be scalar float." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }

        if (lodClamp && (!lodClamp.getType().isScalarType() || !lodClamp.getType().getBaseType(0u).isFloatType())) {
          str << "LOD clamp must be scalar float." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }

        auto coordCount = resourceCoordComponentCount(kind);

        if (derivX && (derivX.getType() != derivY.getType() ||
            !derivX.getType().isBasicType() ||
            !derivX.getType().getBaseType(0u).isFloatType() ||
            derivX.getType().getBaseType(0u).getVectorSize() != coordCount)) {
          str << "Derivatives must be scalar or vector of float type with " << coordCount << " components." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }

        expectedType = BasicType(imageType, dref ? 1u : 4u);
      } break;

      case OpCode::eImageGather: {
        sampler = SsaDef(op->getOperand(1u));
        layer = SsaDef(op->getOperand(2u));
        coord = SsaDef(op->getOperand(3u));
        offset = SsaDef(op->getOperand(4u));
        dref = SsaDef(op->getOperand(5u));

        uint32_t component = uint32_t(op->getOperand(6u));

        if (component >= (dref ? 1u : 4u)) {
          str << "Gather component out of bounds." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }

        expectedType = BasicType(imageType, 4u);
      } break;

      case OpCode::eImageComputeLod: {
        sampler = SsaDef(op->getOperand(1u));
        coord = SsaDef(op->getOperand(2u));

        expectedType = BasicType(ScalarType::eF32, 2u);
      } break;

      default:
        break;
    }

    /* Validate sampler */
    if (opCode == OpCode::eImageSample || opCode == OpCode::eImageGather || opCode == OpCode::eImageComputeLod) {
      const auto& samplerDescriptor = m_builder.getOp(sampler);

      if (samplerDescriptor.getOpCode() != OpCode::eDescriptorLoad ||
          samplerDescriptor.getType() != ScalarType::eSampler) {
        str << sampler << " is not a valid sampler descriptor." << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }
    }

    /* Validate array layer */
    if (opCode == OpCode::eImageLoad || opCode == OpCode::eImageStore || opCode == OpCode::eImageAtomic ||
        opCode == OpCode::eImageSample || opCode == OpCode::eImageGather) {
      const auto& layerOp = m_builder.getOp(layer);
      bool imageHasLayers = resourceIsLayered(kind);

      if (imageHasLayers != bool(layerOp)) {
        str << "Array layer " << (imageHasLayers ? "required" : "not allowed") << " for image kind " << kind << "." << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }

      bool layerNeedsFloatType = bool(sampler);

      if (layerOp && (!layerOp.getType().isScalarType() || (layerOp.getType().getBaseType(0u).isFloatType() != layerNeedsFloatType))) {
        str << "Array layer must be scalar " << (layerNeedsFloatType ? "float" : "integer") << " type." << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }
    }

    /* Validate sample index */
    if (opCode == OpCode::eImageLoad) {
      const auto& sampleOp = m_builder.getOp(sample);
      bool imageHasSample = resourceIsMultisampled(kind);

      if (imageHasSample != bool(sampleOp)) {
        str << "Sample index " << (imageHasSample ? "required" : "not allowed") << " for image kind " << kind << "." << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }

      if (sampleOp && (!sampleOp.getType().isScalarType() || !sampleOp.getType().getBaseType(0u).isIntType())) {
        str << "Sample index must be scalar integer type." << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }
    }

    /* Validate mip level parameter */
    if (opCode == OpCode::eImageLoad || opCode == OpCode::eImageQuerySize) {
      const auto& mipOp = m_builder.getOp(mip);

      bool imageHasMips = !resourceIsMultisampled(kind) &&
        imageDescriptor.getType() == ScalarType::eSrv;

      if (imageHasMips != bool(mipOp)) {
        str << "Mip level " << (imageHasMips ? "required" : "not allowed") << " for image kind " << kind << "." << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }

      if (mipOp && (!mipOp.getType().isScalarType() || !mipOp.getType().getBaseType(0u).isIntType())) {
        str << "Mip level must be scalar integer type." << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }
    }

    /* Validate offset parameter */
    if (offset) {
      const auto& offsetOp = m_builder.getOp(offset);
      auto coordCount = resourceCoordComponentCount(kind);

      if (!offsetOp.getType().isBasicType() ||
          !offsetOp.getType().getBaseType(0u).isSignedIntType() ||
          offsetOp.getType().getBaseType(0u).getVectorSize() != coordCount) {
        str << "Texel offset must be a scalar or vector of a signed integer type." << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }

      if (opCode != OpCode::eImageGather && !offsetOp.isConstant()) {
        str << "Texel offset must be constant." << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }
    }

    /* Validate coordinate parameter */
    if (opCode == OpCode::eImageLoad || opCode == OpCode::eImageStore || opCode == OpCode::eImageAtomic ||
        opCode == OpCode::eImageSample || opCode == OpCode::eImageGather || opCode == OpCode::eImageComputeLod) {
      const auto& coordOp = m_builder.getOp(coord);

      auto coordCount = resourceCoordComponentCount(kind);
      bool needsFloatCoord = bool(sampler);

      if (!coordOp.getType().isBasicType() ||
          (coordOp.getType().getBaseType(0u).isFloatType() != needsFloatCoord) ||
          (coordOp.getType().getBaseType(0u).getVectorSize() != coordCount)) {
        str << "Coordinate must be " << (coordCount == 1u ? "scalar" : "vector") << " of " << (needsFloatCoord ? "float" : "integer") << " type." << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }
    }

    /* Validate depth reference */
    if (dref) {
      const auto& drefOp = m_builder.getOp(dref);

      if (!drefOp.getType().isScalarType() ||
          !drefOp.getType().getBaseType(0u).isFloatType()) {
        str << "Depth reference value must be scalar float." << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }
    }

    /* Validate sparse feedback */
    if (op->getFlags() & OpFlag::eSparseFeedback) {
      if (opCode != OpCode::eImageLoad && opCode != OpCode::eImageSample && opCode != OpCode::eImageGather) {
        str << OpFlag::eSparseFeedback << " not allowed on instruction." << std::endl;
        m_disasm.disassembleOp(str, *op);
        return false;
      }

      expectedType = Type()
        .addStructMember(ScalarType::eU32)
        .addStructMember(expectedType.getBaseType(0u));
    }

    /* Validate return type */
    if (op->getType() != expectedType) {
      str << "Expected return type " << expectedType << "." << std::endl;
      m_disasm.disassembleOp(str, *op);
      return false;
    }
  }

  return true;
}


bool Validator::validateCompositeOps(std::ostream& str) const {
  for (auto op = m_builder.getCode().first; op != m_builder.getCode().second; op++) {
    switch (op->getOpCode()) {
      case OpCode::eCompositeConstruct: {
        auto type = op->getType();

        uint32_t aggregateSize = type.computeTopLevelMemberCount();

        if (type.isScalarType()) {
          str << type << " is not a composite type." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }

        if (op->getOperandCount() != aggregateSize) {
          str << type << " has " << aggregateSize << " elements, but " << op->getOperandCount() << " were provided." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }

        for (uint32_t i = 0u; i < aggregateSize; i++) {
          auto operandType = m_builder.getOp(SsaDef(op->getOperand(i))).getType();
          auto expectedType = type.getSubType(i);

          if (operandType != expectedType) {
            str << "Expected " << expectedType << " for operand " << i << ", but got " << operandType << "." << std::endl;
            m_disasm.disassembleOp(str, *op);
            return false;
          }
        }
      } break;

      case OpCode::eCompositeInsert:
      case OpCode::eCompositeExtract: {
        auto compositeType = m_builder.getOp(SsaDef(op->getOperand(0u))).getType();
        auto valueType = op->getType();
        auto expectedType = compositeType;

        if (op->getOpCode() == OpCode::eCompositeInsert) {
          if (op->getType() != compositeType) {
            str << "Return type must match composite type " << compositeType << "." << std::endl;
            m_disasm.disassembleOp(str, *op);
            return false;
          }

          valueType = m_builder.getOp(SsaDef(op->getOperand(2u))).getType();
        }

        const auto& address = m_builder.getOp(SsaDef(op->getOperand(1u)));
        auto addressType = address.getType();

        if (!addressType.isBasicType() || !addressType.getBaseType(0u).isIntType()) {
          str << "Address must be scalar or vector of an integer type." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }

        for (uint32_t i = 0u; i < addressType.getBaseType(0u).getVectorSize(); i++) {
          auto component = decomposeAddress(address, i);

          if (!expectedType.isArrayType() && !component.isConstant()) {
            str << "Dynamic indexing not supported for type " << expectedType << "." << std::endl;
            m_disasm.disassembleOp(str, *op);
            return false;
          }

          expectedType = expectedType.getSubType(component.isConstant() ? uint32_t(component.getOperand(0u)) : 0u);
        }

        if (valueType != expectedType) {
          str << "Got member type " << valueType << ", expected " << expectedType << "." << std::endl;
          m_disasm.disassembleOp(str, *op);
          return false;
        }
      } break;

      default:
        break;
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
      && validateResources(str)
      && validateLoadStoreOps(str)
      && validateImageOps(str)
      && validateCompositeOps(str)
      && validateStructuredCfg(str);
}


PrimitiveType Validator::findGsInputPrimitive(SsaDef entryPoint) const {
  for (auto op = m_builder.getDeclarations().first; op != m_builder.getDeclarations().second; op++) {
    if (op->getOpCode() == OpCode::eSetGsInputPrimitive && SsaDef(op->getOperand(0u)) == entryPoint)
      return PrimitiveType(op->getOperand(1u));
  }

  return PrimitiveType::ePoints;
}


Op Validator::decomposeAddress(const Op& op, uint32_t member) const {
  if (op.getType().isScalarType())
    return member ? Op() : op;

  if (op.isConstant()) {
    return Op(OpCode::eConstant, op.getType().getSubType(member))
      .addOperand(op.getOperand(member));
  }

  if (op.getOpCode() == OpCode::eCompositeConstruct)
    return m_builder.getOpForOperand(op, member);

  return Op();
}

}
