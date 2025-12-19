#include "sm3_resources.h"

#include "sm3_converter.h"

#include "../ir/ir_utils.h"

#include "../util/util_log.h"

namespace dxbc_spv::sm3 {

ResourceMap::ResourceMap(Converter& converter)
: m_converter (converter) {

}


ResourceMap::~ResourceMap() {

}


void ResourceMap::initialize(ir::Builder& builder) {
  ShaderType shaderType = m_converter.getShaderInfo().getType();
  uint32_t hwvpFloatConstantsCount = shaderType == ShaderType::ePixel ? MaxFloatConstantsPS : MaxFloatConstantsVS;
  uint32_t hwvpConstantsArraySize = hwvpFloatConstantsCount + MaxOtherConstants;

  bool isSwvp = m_converter.getOptions().isSWVP;

  if (isSwvp) {
    /* SWVP allows using a lot of constants so we put each type in its own constant buffer to make optimal use of
     * robustness2 to keep them small. */
    auto floatVec4Type = ir::Type(ir::ScalarType::eF32, 4u);
    auto floatArrayType = ir::Type(floatVec4Type).addArrayDimension(MaxFloatConstantsSoftware);
    m_constants[uint32_t(ConstantType::eFloat4)].bufferDef = builder.add(ir::Op::DclCbv(floatArrayType, m_converter.getEntryPoint(), 0u, SWVPFloatCbvRegIdx, 1u));
    builder.add(ir::Op::DebugName(m_constants[uint32_t(ConstantType::eFloat4)].bufferDef, "cF"));

    auto intVec4Type = ir::Type(ir::ScalarType::eI32, 4u);
    auto intArrayType = ir::Type(intVec4Type).addArrayDimension(MaxOtherConstantsSoftware);
    m_constants[uint32_t(ConstantType::eInt4)].bufferDef = builder.add(ir::Op::DclCbv(intArrayType, m_converter.getEntryPoint(), 0u, SWVPIntCbvRegIdx, 1u));
    builder.add(ir::Op::DebugName(m_constants[uint32_t(ConstantType::eInt4)].bufferDef, "cI"));

    auto boolBitMasksArrayType = ir::Type(ir::ScalarType::eU32).addArrayDimension((MaxOtherConstantsSoftware + 31u) / 32u);
    m_constants[uint32_t(ConstantType::eBool)].bufferDef = builder.add(ir::Op::DclCbv(boolBitMasksArrayType, m_converter.getEntryPoint(), 0u, SWVPBoolCbvRegIdx, 1u));
    builder.add(ir::Op::DebugName(m_constants[uint32_t(ConstantType::eBool)].bufferDef, "cB"));
  } else {
    /* HWVP allows using a lot of float constants but only few int and bool constants.
     * Bool constants get turned into specialization constants.
     * Int and float constants get put into the same constant buffer with int constants at the beginning so
     * float constants that weren't defined by the application can use robustness2 and the buffer stays small. */
    auto floatBufferMemberType = ir::Type(ir::ScalarType::eUnknown, 4u);
    auto constantsArrayType = ir::Type(floatBufferMemberType).addArrayDimension(hwvpConstantsArraySize);
    auto constantsBufferDef = builder.add(ir::Op::DclCbv(constantsArrayType, m_converter.getEntryPoint(), 0u, FloatIntCbvRegIdx, 1u));
    m_constants[uint32_t(ConstantType::eFloat4)].bufferDef = constantsBufferDef;
    m_constants[uint32_t(ConstantType::eInt4)].bufferDef = constantsBufferDef;

    if (m_converter.getOptions().includeDebugNames) {
      builder.add(ir::Op::DebugName(m_constants[uint32_t(ConstantType::eFloat4)].bufferDef, "cI_F"));
    }
  }

  auto& floatRange = m_constants[uint32_t(ConstantType::eFloat4)].constantRanges.emplace_back();
  floatRange.startIndex = 0u;
  floatRange.count = isSwvp ? MaxFloatConstantsSoftware : hwvpFloatConstantsCount;
  floatRange.namedBufferDef = m_constants[uint32_t(ConstantType::eFloat4)].bufferDef;

  auto& intRange = m_constants[uint32_t(ConstantType::eInt4)].constantRanges.emplace_back();
  intRange.startIndex = 0u;
  intRange.count = isSwvp ? MaxOtherConstantsSoftware : MaxOtherConstants;
  intRange.namedBufferDef = m_constants[uint32_t(ConstantType::eInt4)].bufferDef;

  if (isSwvp) {
    auto& boolRange = m_constants[uint32_t(ConstantType::eBool)].constantRanges.emplace_back();
    boolRange.startIndex = 0u;
    boolRange.count = (MaxOtherConstantsSoftware + 31u) / 32u;
    boolRange.namedBufferDef = m_constants[uint32_t(ConstantType::eBool)].bufferDef;
  }
}


void ResourceMap::emitNamedConstantRanges(ir::Builder& builder, const ConstantTable& ctab) {
  ShaderType shaderType = m_converter.getShaderInfo().getType();
  uint32_t hwvpFloatConstantsCount = (shaderType == ShaderType::ePixel ? MaxFloatConstantsPS : MaxFloatConstantsVS);
  uint32_t hwvpConstantsArraySize = hwvpFloatConstantsCount + MaxOtherConstants;

  bool isSwvp = m_converter.getOptions().isSWVP;

  /* If debug names are enabled, generate one buffer per named constant. They will all have the same regIdx. */

  for (const auto& entry : ctab.entries()) {
    auto& range = m_constants[uint32_t(entry.registerSet)].constantRanges.emplace_back();
    range.startIndex = entry.index;
    range.count = entry.count;

    uint32_t regIdx;
    ir::Type type;
    switch (entry.registerSet) {
      case ConstantType::eFloat4:
        regIdx = isSwvp ? SWVPFloatCbvRegIdx : FloatIntCbvRegIdx;
        type = ir::Type(isSwvp ? ir::ScalarType::eF32 : ir::ScalarType::eUnknown, 4u)
          .addArrayDimension(isSwvp ? MaxFloatConstantsSoftware : hwvpConstantsArraySize);
        break;

      case ConstantType::eInt4:
        regIdx = isSwvp ? SWVPIntCbvRegIdx : FloatIntCbvRegIdx;
        type = ir::Type(isSwvp ? ir::ScalarType::eI32 : ir::ScalarType::eUnknown, 4u)
          .addArrayDimension(isSwvp ? MaxOtherConstantsSoftware : hwvpConstantsArraySize);
        break;

      case ConstantType::eBool:
        if (!isSwvp)
          continue;

        regIdx = SWVPBoolCbvRegIdx;
        type = ir::Type(ir::ScalarType::eU32).addArrayDimension(MaxOtherConstantsSoftware);
        break;

      default:
        dxbc_spv_unreachable();
        continue;
    }

    range.namedBufferDef = builder.add(ir::Op::DclCbv(type, m_converter.getEntryPoint(), 0u, regIdx, 1u));
    builder.add(ir::Op::DebugName(range.namedBufferDef, entry.name.c_str()));
  }
}


ir::SsaDef ResourceMap::emitConstantLoad(
          ir::Builder&            builder,
    const Instruction&            op,
    const Operand&                operand,
          WriteMask               componentMask,
          ir::ScalarType          scalarType) {

  ShaderInfo info = m_converter.getShaderInfo();
  bool isSwvp = m_converter.getOptions().isSWVP;

  uint32_t registerIndex = operand.getIndex();
  RegisterType registerType = operand.getRegisterType();

  /* Relative addressing is only supported for float constants */
  dxbc_spv_assert(registerType == RegisterType::eConst
    || registerType == RegisterType::eConst2
    || registerType == RegisterType::eConst3
    || registerType == RegisterType::eConst4
    || !operand.hasRelativeAddressing());

  ConstantType constantType = constantTypeFromRegisterType(registerType);
  dxbc_spv_assert(constantType != ConstantType::eSampler);
  Constants& constants = m_constants[uint32_t(constantType)];

  /* Inline immediate constants that were defined in the shader. */
  if (!operand.hasRelativeAddressing()) {
    for (const auto& c : constants.immediateConstants) {
      if (c.index != registerIndex)
        continue;

      ir::ScalarType immediateConstantType = builder
      .getOp(c.def)
      .getType()
      .getBaseType(0u)
      .getBaseType();

      if (scalarType != immediateConstantType) {
        std::array<ir::SsaDef, 4u> components;

        for (uint32_t i = 0u; i < 4u; i++) {
          auto component = extractFromVector(builder, c.def, i);
          components[i] = builder.add(ir::Op::ConsumeAs(scalarType, component));
        }

        return composite(builder, makeVectorType(scalarType, componentMask), components.data(), operand.getSwizzle(info), componentMask);
      } else {
        return swizzleVector(builder, c.def, operand.getSwizzle(info), componentMask);
      }
    }
  }

  /* Bool constants are implemented using specialization constants on HWVP VS or PS */
  if (registerType == RegisterType::eConstBool && !isSwvp) {
    dxbc_spv_assert(scalarType == ir::ScalarType::eBool);

    auto bit = m_converter.m_specConstants.get(builder,
      info.getType() == ShaderType::eVertex ? SpecConstantId::eSpecVertexShaderBools : SpecConstantId::eSpecPixelShaderBools,
      builder.makeConstant(registerIndex), builder.makeConstant(1u));

    auto boolVal = builder.add(ir::Op::INe(ir::ScalarType::eBool, bit, builder.makeConstant(0u)));

    if (util::popcnt(uint8_t(componentMask)) == 1u) {
      return boolVal;
    } else {
      return broadcastScalar(builder, boolVal, componentMask);
    }
  }

  /* Find the smallest range that contains the requested constants. */
  const ConstantRange* bestRange = nullptr;
  for (const auto& range : constants.constantRanges) {
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

  /* In HWVP float constants and int constants are packed into the same buffer and the int constants come first. */
  uint32_t arrayIndex = constantType == ConstantType::eFloat4 && !isSwvp
    ? MaxOtherConstants
    : 0u;
  /* Bools are packed into 32bit integers. */
  arrayIndex += constantType == ConstantType::eBool
    ? (registerIndex / 32u)
    : registerIndex;

  ir::SsaDef offset;
  if (!operand.hasRelativeAddressing()) {
    offset = builder.makeConstant(arrayIndex);
  } else {
    dxbc_spv_assert(registerType == RegisterType::eConst
      || registerType == RegisterType::eConst2
      || registerType == RegisterType::eConst3
      || registerType == RegisterType::eConst4);
    offset = m_converter.calculateAddress(builder,
      operand.getRelativeAddressingRegisterType(),
      operand.getRelativeAddressingSwizzle(),
      arrayIndex,
      ir::ScalarType::eU32);
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
      builder.makeConstant(registerIndex % 32u),
      builder.makeConstant(1u)));

    auto boolVal = builder.add(ir::Op::INe(ir::ScalarType::eBool, bit, builder.makeConstant(0u)));

    if (util::popcnt(uint8_t(componentMask)) == 1u) {
      return boolVal;
    } else {
      return broadcastScalar(builder, boolVal, componentMask);
    }
  }

  ir::ScalarType bufferElementScalarType = constantType == ConstantType::eFloat4
    ? ir::ScalarType::eF32
    : ir::ScalarType::eI32;

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

    // PS 1.x clamps float constants
    if (m_converter.getShaderInfo().getType() == ShaderType::ePixel
      && m_converter.getShaderInfo().getVersion().first == 1u
      && scalarType == ir::ScalarType::eF32)
      scalar = builder.add(ir::Op::FClamp(scalarType, scalar,
        builder.makeConstant(-1.0f), builder.makeConstant(1.0f)));
  }

  /* Build result vector */
  return composite(builder,
    makeVectorType(scalarType, componentMask),
    components.data(), operand.getSwizzle(info), componentMask);
}


void ResourceMap::emitImmediateConstant(
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
      std::array<float, 4u> values = {
        imm.getImmediate<float>(0u), imm.getImmediate<float>(1u),
        imm.getImmediate<float>(2u), imm.getImmediate<float>(3u)
      };

      // PS 1.x clamps float constants
      if (m_converter.getShaderInfo().getType() == ShaderType::ePixel
        && m_converter.getShaderInfo().getVersion().first == 1u) {
        for (float& value : values) {
          value = std::max(std::min(value, 1.0f), -1.0f);
        }
      }

      auto def = builder.makeConstant(values[0u], values[1u], values[2u], values[3u]);

      m_constants[uint32_t(ConstantType::eFloat4)].immediateConstants.push_back({index, def});
    } break;

    case RegisterType::eConstInt: {
      std::array<int32_t, 4u> values = {
        imm.getImmediate<int32_t>(0u), imm.getImmediate<int32_t>(1u),
        imm.getImmediate<int32_t>(2u), imm.getImmediate<int32_t>(3u)
      };

      auto def = builder.makeConstant(values[0u], values[1u], values[2u], values[3u]);

      m_constants[uint32_t(ConstantType::eInt4)].immediateConstants.push_back({index, def});
    } break;

    case RegisterType::eConstBool: {
      auto value = imm.getImmediate<uint32_t>(0u) != 0u;

      auto def = builder.makeConstant(value);

      m_constants[uint32_t(ConstantType::eBool)].immediateConstants.push_back({index, def});
    } break;

    default:
      Logger::err("Register type ", UnambiguousRegisterType { registerType, info.getType(), info.getVersion().first }, " is not a constant register");
      dxbc_spv_unreachable();
  }
}

}
