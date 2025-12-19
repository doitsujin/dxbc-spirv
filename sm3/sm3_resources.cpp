#include "sm3_resources.h"

#include "sm3_converter.h"

#include "../ir/ir_utils.h"

#include "../util/util_log.h"

namespace dxbc_spv::sm3 {

constexpr uint32_t MaxFloatConstantsVS       = 256;
constexpr uint32_t MaxFloatConstantsPS       = 224;
constexpr uint32_t MaxOtherConstants         = 16;
constexpr uint32_t MaxFloatConstantsSoftware = 8192;
constexpr uint32_t MaxOtherConstantsSoftware = 2048;

ResourceMap::ResourceMap(Converter& converter)
: m_converter (converter) {

}


ResourceMap::~ResourceMap() {

}


void ResourceMap::initialize(ir::Builder& builder) {
  ShaderType shaderType = m_converter.getShaderInfo().getType();
  uint32_t hwvpFloatConstantsCount = (shaderType == ShaderType::ePixel ? MaxFloatConstantsPS : MaxFloatConstantsVS);
  uint32_t hwvpConstantsArraySize = hwvpFloatConstantsCount + MaxOtherConstants;

  bool isSwvp = m_converter.getOptions().isSWVP;

  if (isSwvp) {
    /* SWVP allows using a lot of constants so we put each type in its own constant buffer to make optimal use of
     * robustness2 to keep them small. */
    auto floatVec4Type = ir::Type(ir::ScalarType::eF32, 4u);
    auto floatArrayType = ir::Type(floatVec4Type).addArrayDimension(MaxFloatConstantsSoftware);
    m_floatConstants.bufferDef = builder.add(ir::Op::DclCbv(floatArrayType, m_converter.getEntryPoint(), ConstantBufferRegSpace, FloatSWVPCbvRegIdx, 1u));

    auto intVec4Type = ir::Type(ir::ScalarType::eI32, 4u);
    auto intArrayType = ir::Type(intVec4Type).addArrayDimension(MaxOtherConstantsSoftware);
    m_intConstants.bufferDef = builder.add(ir::Op::DclCbv(intArrayType, m_converter.getEntryPoint(), ConstantBufferRegSpace, IntSWVPCbvRegIdx, 1u));

    auto boolBitMasksArrayType = ir::Type(ir::ScalarType::eU32).addArrayDimension((MaxOtherConstantsSoftware + 31u) / 32u);
    m_boolConstants.bufferDef = builder.add(ir::Op::DclCbv(boolBitMasksArrayType, m_converter.getEntryPoint(), ConstantBufferRegSpace, BoolSWVPCbvRegIdx, 1u));

    if (m_converter.getOptions().includeDebugNames) {
      builder.add(ir::Op::DebugName(m_floatConstants.bufferDef, "cF"));
      builder.add(ir::Op::DebugName(m_intConstants.bufferDef, "cI"));
      builder.add(ir::Op::DebugName(m_boolConstants.bufferDef, "cB"));
    }
  } else {
    /* HWVP allows using a lot of float constants but only few int and bool constants.
     * Bool constants get turned into specialization constants.
     * Int and float constants get put into the same constant buffer with int constants at the beginning so
     * float constants that weren't defined by the application can use robustness2 and the buffer stays small. */
    auto floatBufferMemberType = ir::Type(ir::ScalarType::eUnknown, 4u);
    auto constantsArrayType = ir::Type(floatBufferMemberType).addArrayDimension(hwvpConstantsArraySize);
    auto constantsBufferDef = builder.add(ir::Op::DclCbv(constantsArrayType, m_converter.getEntryPoint(), ConstantBufferRegSpace, FloatIntHWVPCbvRegIdx, 1u));
    m_floatConstants.bufferDef = constantsBufferDef;
    m_intConstants.bufferDef = constantsBufferDef;

    if (m_converter.getOptions().includeDebugNames) {
      builder.add(ir::Op::DebugName(m_floatConstants.bufferDef, "cI_F"));
    }
  }

  auto& floatRange = m_floatConstants.constantRanges.emplace_back();
  floatRange.startIndex = 0u;
  floatRange.count = isSwvp ? MaxFloatConstantsSoftware : hwvpFloatConstantsCount;
  floatRange.namedBufferDef = m_floatConstants.bufferDef;

  auto& intRange = m_intConstants.constantRanges.emplace_back();
  intRange.startIndex = 0u;
  intRange.count = isSwvp ? MaxOtherConstantsSoftware : MaxOtherConstants;
  intRange.namedBufferDef = m_intConstants.bufferDef;

  if (isSwvp) {
    auto& boolRange = m_boolConstants.constantRanges.emplace_back();
    boolRange.startIndex = 0u;
    boolRange.count = (MaxOtherConstantsSoftware + 31u) / 32u;
    boolRange.namedBufferDef = m_boolConstants.bufferDef;
  }
}


void ResourceMap::emitNamedConstantRanges(ir::Builder& builder, const ConstantTable& ctab) {
  ShaderType shaderType = m_converter.getShaderInfo().getType();
  uint32_t hwvpFloatConstantsCount = (shaderType == ShaderType::ePixel ? MaxFloatConstantsPS : MaxFloatConstantsVS);
  uint32_t hwvpConstantsArraySize = hwvpFloatConstantsCount + MaxOtherConstants;

  bool isSwvp = m_converter.getOptions().isSWVP;

  /* If debug names are enabled, generate one buffer per named constant. They will all have the same regIdx. */
  const auto& ctabFloatEntries = ctab.entries()[uint32_t(ConstantType::eFloat4)];

  for (const auto& entry : ctabFloatEntries) {
    auto& range = m_floatConstants.constantRanges.emplace_back();
    range.startIndex = entry.index;
    range.count = entry.count;
    auto floatVec4Type = ir::Type(isSwvp ? ir::ScalarType::eF32 : ir::ScalarType::eUnknown, 4u);
    auto floatArrayType = ir::Type(floatVec4Type).addArrayDimension(isSwvp ? MaxFloatConstantsSoftware : hwvpConstantsArraySize);
    range.namedBufferDef = builder.add(ir::Op::DclCbv(floatArrayType, m_converter.getEntryPoint(), ConstantBufferRegSpace, isSwvp ? FloatSWVPCbvRegIdx : FloatIntHWVPCbvRegIdx, 1u));
    builder.add(ir::Op::DebugName(range.namedBufferDef, entry.name.c_str()));
  }

  const auto& ctabIntEntries = ctab.entries()[uint32_t(ConstantType::eInt4)];

  for (const auto& entry : ctabIntEntries) {
    auto& range = m_intConstants.constantRanges.emplace_back();
    range.startIndex = entry.index;
    range.count      = entry.count;
    auto intVec4Type = ir::Type(isSwvp ? ir::ScalarType::eI32 : ir::ScalarType::eUnknown, 4u);
    auto intArrayType = ir::Type(intVec4Type).addArrayDimension(isSwvp ? MaxOtherConstantsSoftware : hwvpConstantsArraySize);
    range.namedBufferDef = builder.add(ir::Op::DclCbv(intArrayType, m_converter.getEntryPoint(), ConstantBufferRegSpace, isSwvp ? IntSWVPCbvRegIdx : FloatIntHWVPCbvRegIdx, 1u));
    builder.add(ir::Op::DebugName(range.namedBufferDef, entry.name.c_str()));
  }

  if (isSwvp) {
    const auto& ctabBoolEntries = ctab.entries()[uint32_t(ConstantType::eBool)];

    for (const auto& entry : ctabBoolEntries) {
      auto& range = m_boolConstants.constantRanges.emplace_back();
      range.startIndex = entry.index;
      range.count      = entry.count;
      auto boolBitMasksArrayType = ir::Type(ir::ScalarType::eU32).addArrayDimension(MaxOtherConstantsSoftware);
      range.namedBufferDef = builder.add(ir::Op::DclCbv(boolBitMasksArrayType, m_converter.getEntryPoint(), ConstantBufferRegSpace, BoolSWVPCbvRegIdx, 1u));
      builder.add(ir::Op::DebugName(range.namedBufferDef, entry.name.c_str()));
    }
  }
}


ir::SsaDef ResourceMap::emitConstantLoad(
          ir::Builder&            builder,
    const Instruction&            op,
    const Operand&                operand,
          WriteMask               componentMask,
          ir::ScalarType          scalarType) {

  ShaderInfo info = m_converter.getShaderInfo();
  bool isSwvp = m_converter.getOptions().isSWVP && info.getType() == ShaderType::eVertex;

  uint32_t registerIndex = operand.getIndex();

  RegisterType registerType = operand.getRegisterType();

  /* Relative addressing is only supported for float constants */
  dxbc_spv_assert(registerType == RegisterType::eConst
    || registerType == RegisterType::eConst2
    || registerType == RegisterType::eConst3
    || registerType == RegisterType::eConst4
    || !operand.hasRelativeAddressing());

  /* Inline constants that were defined in the shader. */
  if (!operand.hasRelativeAddressing()) {
    ir::ScalarType predefinedType;
    ir::SsaDef predefinedConstant = ir::SsaDef();

    switch (registerType) {
      case RegisterType::eConst:
      case RegisterType::eConst2:
      case RegisterType::eConst3:
      case RegisterType::eConst4:
        predefinedType = ir::ScalarType::eF32;

        for (const auto& c : m_floatConstants.definedConstants) {
          if (c.index == registerIndex) {
            predefinedConstant = c.def;
            break;
          }
        }
        break;
      case RegisterType::eConstInt:
        predefinedType = ir::ScalarType::eI32;

        for (const auto& c : m_intConstants.definedConstants) {
          if (c.index == registerIndex) {
            predefinedConstant = c.def;
            break;
          }
        }
        break;
      case RegisterType::eConstBool:
        predefinedType = ir::ScalarType::eBool;

        for (const auto& c : m_boolConstants.definedConstants) {
          if (c.index == registerIndex) {
            predefinedConstant = c.def;
            break;
          }
        }
        break;

      default:
        dxbc_spv_unreachable();
    }

    if (predefinedConstant) {
      if (scalarType != predefinedType) {
        std::array<ir::SsaDef, 4u> components;

        for (uint32_t i = 0u; i < 4u; i++) {
          auto c = extractFromVector(builder, predefinedConstant, i);
          components[i] = builder.add(ir::Op::ConsumeAs(scalarType, c));
        }

        return composite(builder, makeVectorType(scalarType, componentMask), components.data(), operand.getSwizzle(info), componentMask);
      } else {
        return swizzleVector(builder, predefinedConstant, operand.getSwizzle(info), componentMask);
      }
    }
  }

  /* Bool constants are implemented using specialization constants on HWVP VS or PS */
  if (registerType == RegisterType::eConstBool && !isSwvp) {
    dxbc_spv_assert(scalarType == ir::ScalarType::eBool);

    auto bit = m_converter.m_specConstants.get(builder,
      info.getType() == ShaderType::eVertex ? SpecConstantId::eSpecVertexShaderBools : SpecConstantId::eSpecPixelShaderBools,
      builder.makeConstant(operand.getIndex()), builder.makeConstant(1u));

    auto boolVal = builder.add(ir::Op::INe(ir::ScalarType::eBool, bit, builder.makeConstant(0u)));

    if (util::popcnt(uint8_t(componentMask)) == 1u) {
      return boolVal;
    } else {
      return broadcastScalar(builder, boolVal, componentMask);
    }
  }

  const util::small_vector<ConstantRange, 8u>* ranges = nullptr;
  uint32_t constantTypeOffset = 0u;
  uint32_t accessedCount = 0u;
  ir::ScalarType bufferElementScalarType = ir::ScalarType::eUnknown;

  switch (registerType) {
    case RegisterType::eConst:
    case RegisterType::eConst2:
    case RegisterType::eConst3:
    case RegisterType::eConst4:
      ranges = &m_floatConstants.constantRanges;

      if (isSwvp)
        bufferElementScalarType = ir::ScalarType::eF32;

      /* In HWVP float constants and int constants are packed into the same buffer and the int constants come first. */
      constantTypeOffset = isSwvp ? 0u : MaxOtherConstants;

      if (operand.hasRelativeAddressing()) {
        if (info.getType() == ShaderType::eVertex) {
          accessedCount = (isSwvp ? MaxFloatConstantsSoftware : MaxFloatConstantsVS) - 1u;
        } else {
          accessedCount = MaxFloatConstantsPS - 1u;
        }
      }

      m_floatConstants.maxAccessedConstant = std::max(m_floatConstants.maxAccessedConstant, registerIndex + accessedCount);
      break;

    case RegisterType::eConstInt:
      ranges = &m_intConstants.constantRanges;

      if (isSwvp)
        bufferElementScalarType = ir::ScalarType::eI32;

      if (operand.hasRelativeAddressing())
          accessedCount = (info.getType() == ShaderType::eVertex && isSwvp ? MaxOtherConstantsSoftware : MaxOtherConstants) - 1u;

      m_intConstants.maxAccessedConstant = std::max(m_intConstants.maxAccessedConstant, registerIndex + accessedCount);
      break;

    case RegisterType::eConstBool:
      ranges = &m_boolConstants.constantRanges;
      if (operand.hasRelativeAddressing())
        accessedCount = (info.getType() == ShaderType::eVertex && isSwvp ? MaxOtherConstantsSoftware : MaxOtherConstants) - 1u;

      m_boolConstants.maxAccessedConstant = std::max(m_boolConstants.maxAccessedConstant, registerIndex + accessedCount);
      break;

    default:
      dxbc_spv_unreachable();

      Logger::err("Register type: ",
        UnambiguousRegisterType { registerType, info.getType(), info.getVersion().first },
        " is not a constant register type.");
  }

  if (ranges == nullptr)
    return ir::SsaDef();

  const ConstantRange* bestRange = nullptr;

  for (const auto& range : *ranges) {
    if (registerIndex >= range.startIndex
      && registerIndex < range.startIndex + range.count
      && (bestRange == nullptr || range.count < bestRange->count)) {
      bestRange = &range;
    }
  }

  if (bestRange == nullptr) {
    dxbc_spv_unreachable();
    Logger::err("Cannot find constant: ", registerIndex, " in ranges.");
    return ir::SsaDef();
  }

  uint32_t arrayIndex = constantTypeOffset;
  ir::SsaDef offset;
  arrayIndex += registerType == RegisterType::eConstBool ? (operand.getIndex() / 32u) : operand.getIndex();

  if (!operand.hasRelativeAddressing()) {
    offset = builder.makeConstant(arrayIndex);
  } else {
    dxbc_spv_assert(registerType == RegisterType::eConst
      || registerType == RegisterType::eConst2
      || registerType == RegisterType::eConst3
      || registerType == RegisterType::eConst4);
    auto relAddr = m_converter.loadAddress(builder,
      operand.getRelativeAddressingRegisterType(),
      operand.getRelativeAddressingSwizzle());
    offset = builder.makeConstant(int32_t(arrayIndex));
    offset = builder.add(ir::Op::IAdd(ir::ScalarType::eI32, offset, relAddr));
    offset = builder.add(ir::Op::Cast(ir::ScalarType::eU32, offset));
  }

  auto descriptor = builder.add(ir::Op::DescriptorLoad(ir::ScalarType::eCbv, bestRange->namedBufferDef, builder.makeConstant(0u)));

  if (registerType == RegisterType::eConstBool) {
    /* Bool constants are not an array of vec4s unlike float & int.
     * Instead they are set as an array of scalar bools on the API side and we store them as bit masks. */
    dxbc_spv_assert(scalarType == ir::ScalarType::eBool);

    auto dword = builder.add(ir::Op::BufferLoad(
      ir::ScalarType::eU32, descriptor, offset, 4u));

    auto bit = builder.add(ir::Op::UBitExtract(
      ir::ScalarType::eU32,
      dword,
      builder.makeConstant(operand.getIndex() % 32u),
      builder.makeConstant(1u)));

    auto boolVal = builder.add(ir::Op::INe(ir::ScalarType::eBool, bit, builder.makeConstant(0u)));

    if (util::popcnt(uint8_t(componentMask)) == 1u) {
      return boolVal;
    } else {
      return broadcastScalar(builder, boolVal, componentMask);
    }
  }

  auto readMask = operand.getSwizzle(info).getReadMask(componentMask);

  std::array<ir::SsaDef, 4u> components = { };

  if (readMask == ComponentBit::eAll) {
    /* Read entire vector in one go, no need to addres into scalars */
    auto result = builder.add(ir::Op::BufferLoad(
      ir::BasicType(bufferElementScalarType, 4u), descriptor, offset, 16u));

    for (uint32_t i = 0u; i < components.size(); i++)
      components[i] = extractFromVector(builder, result, i);
  } else {
    /* Absolute component alignment, in dwords */
    constexpr uint32_t ComponentAlignments = 0x1214;

    while (readMask) {
      /* Consecutive blocks of components to read */
      auto block = util::extractConsecutiveComponents(readMask);
      auto blockType = ir::BasicType(bufferElementScalarType, util::popcnt(uint8_t(block)));

      /* First component in the block */
      auto componentIndex = uint8_t(componentFromBit(block.first()));
      auto blockAlignment = 4u * util::bextract(ComponentAlignments, 4u * componentIndex, 4u);

      /* Build address vector with the component index */
      auto address = builder.add(ir::Op::CompositeConstruct(
        ir::BasicType(ir::ScalarType::eU32, 2u), offset,
        builder.makeConstant(uint32_t(componentIndex))));

      /* Emit actual buffer load for the block and write back scalars */
      auto result = builder.add(ir::Op::BufferLoad(blockType, descriptor, address, blockAlignment));

      for (uint32_t i = 0u; i < blockType.getVectorSize(); i++)
        components[componentIndex + i] = extractFromVector(builder, result, i);

      readMask -= block;
    }
  }

  /* Convert scalars to the requested type */
  for (auto& scalar : components) {
    if (scalar && scalarType != bufferElementScalarType)
      scalar = builder.add(ir::Op::ConsumeAs(scalarType, scalar));
  }

  /* Build result vector */
  return composite(builder,
    makeVectorType(scalarType, componentMask),
    components.data(), operand.getSwizzle(info), componentMask);
}


void ResourceMap::emitDefineConstant(
          ir::Builder& builder,
          RegisterType registerType,
          uint32_t index,
    const Operand& imm) {
  auto info = m_converter.getShaderInfo();

  switch (registerType) {
    case RegisterType::eConst:
    case RegisterType::eConst2:
    case RegisterType::eConst3:
    case RegisterType::eConst4: {
      Vec4<float> values = {
        imm.getImmediate<float>(0u), imm.getImmediate<float>(1u),
        imm.getImmediate<float>(2u), imm.getImmediate<float>(3u)
      };

      auto def = builder.makeConstant(values.x, values.y, values.z, values.w);

      m_floatConstants.definedConstants.push_back({values, index, def});
      m_floatConstants.maxDefinedConstant = std::max(m_floatConstants.maxDefinedConstant, index);
    } break;

    case RegisterType::eConstInt: {
      Vec4<int32_t> values = {
        imm.getImmediate<int32_t>(0u), imm.getImmediate<int32_t>(1u),
        imm.getImmediate<int32_t>(2u), imm.getImmediate<int32_t>(3u)
      };

      auto def = builder.makeConstant(values.x, values.y, values.z, values.w);

      m_intConstants.definedConstants.push_back({values, index, def});
      m_intConstants.maxDefinedConstant = std::max(m_intConstants.maxDefinedConstant, index);
    } break;

    case RegisterType::eConstBool: {
      auto value = imm.getImmediate<uint32_t>(0u) != 0u;

      auto def = builder.makeConstant(value);

      m_boolConstants.definedConstants.push_back({value, index, def});
      m_boolConstants.maxDefinedConstant = std::max(m_boolConstants.maxDefinedConstant, index);
    } break;

    default:
      Logger::err("Register type ", UnambiguousRegisterType { registerType, info.getType(), info.getVersion().first }, " is not a constant register");
      dxbc_spv_unreachable();
  }
}

}
