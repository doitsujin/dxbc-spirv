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


ir::SsaDef ResourceMap::emitSample(
            ir::Builder&   builder,
            uint32_t       samplerIndex,
            ir::SsaDef     texCoord,
            ir::SsaDef     lod,
            ir::SsaDef     lodBias,
            ir::SsaDef     dx,
            ir::SsaDef     dy,
            ir::ScalarType scalarType) {
  auto& samplerInfo = m_samplers.at(samplerIndex);

  if (!samplerInfo.samplerDef) {
    dxbc_spv_assert(m_converter.getShaderInfo().getVersion().first < 2u);
    dclSamplerAndAllTextureTypes(builder, samplerIndex);
  }

  dxbc_spv_assert(!!dx == !!dy);

  /* Load sampling function based on how we're sampling the texture. */

  SamplingConfig samplingConfig = { };

  if (lod)
    samplingConfig |= SamplingConfigBit::eExplicitLod;

  if (lodBias)
    samplingConfig |= SamplingConfigBit::eLodBias;

  if (dx || dy)
    samplingConfig |= SamplingConfigBit::eExplicitDerivatives;

  auto& samplingFunction = samplerInfo.samplingFunctions.at(uint8_t(samplingConfig));

  if (!samplingFunction)
    samplingFunction = emitSampleImageFunction(builder, samplerIndex, samplingConfig);

  /* Cast texCoord to F32 if necessary, sampling functions always expect Vec4<F32>. */
  auto texCoordType = builder.getOp(texCoord).getType().getBaseType(0u);

  if (texCoordType.getBaseType() != ir::ScalarType::eF32)
    texCoord = builder.add(ir::Op::ConsumeAs(ir::BasicType(ir::ScalarType::eF32, texCoordType.getVectorSize()), texCoord));

  /* Prepare function call with required arguments based on how we're sampling the texture. */
  auto funcCall = ir::Op::FunctionCall(ir::BasicType(ir::ScalarType::eF32, 4u), samplingFunction)
    .addParam(texCoord);

  if (lod) {
    auto lodType = builder.getOp(lod).getType().getBaseType(0u);

    if (lodType.getBaseType() != ir::ScalarType::eF32) {
      lod = builder.add(ir::Op::ConsumeAs(ir::BasicType(ir::ScalarType::eF32, lodType.getVectorSize()), lod));
    }

    funcCall.addParam(lod);
  }

  if (lodBias) {
    auto lodBiasType = builder.getOp(lodBias).getType().getBaseType(0u);

    if (lodBiasType.getBaseType() != ir::ScalarType::eF32) {
      lodBias = builder.add(ir::Op::ConsumeAs(ir::BasicType(ir::ScalarType::eF32, lodBiasType.getVectorSize()), lodBias));
    }

    funcCall.addParam(lodBias);
  }

  if (dx || dy) {
    auto dxType = builder.getOp(dx).getType().getBaseType(0u);
    auto dyType = builder.getOp(dy).getType().getBaseType(0u);

    if (dxType.getBaseType() != ir::ScalarType::eF32) {
      dx = builder.add(ir::Op::ConsumeAs(ir::BasicType(ir::ScalarType::eF32, dxType.getVectorSize()), dx));
    }

    if (dyType.getBaseType() != ir::ScalarType::eF32) {
      dy = builder.add(ir::Op::ConsumeAs(ir::BasicType(ir::ScalarType::eF32, dyType.getVectorSize()), dy));
    }

    funcCall.addParam(dx);
    funcCall.addParam(dy);
  }

  auto result = builder.add(std::move(funcCall));

  /* The sampling functions always return Vec4<F32>, cast if the caller expects something else. */

  if (scalarType != ir::ScalarType::eF32)
    result = builder.add(ir::Op::ConsumeAs(ir::BasicType(scalarType, 4u), result));

  return result;
}


ir::SsaDef ResourceMap::projectTexCoord(ir::Builder& builder, uint32_t samplerIndex, ir::SsaDef texCoord, bool controlWithSpecConst) {
  auto texCoordType = builder.getOp(texCoord).getType().getBaseType(0u);

  auto texCoordW = builder.add(ir::Op::CompositeExtract(texCoordType.getBaseType(), texCoord, builder.makeConstant(3u)));
  auto projectedTexCoord = builder.add(ir::Op::FDiv(
    texCoordType,
    texCoord,
    broadcastScalar(builder, texCoordW, ComponentBit::eAll)
  ));

  if (controlWithSpecConst) {
    uint32_t specConstIdx = m_converter.m_specConstants.getSamplerSpecConstIndex(
      m_converter.getShaderInfo().getType(), samplerIndex);

    auto isProjectedSpecConst = m_converter.m_specConstants.get(
      builder,
      SpecConstantId::eSpecSamplerProjected,
      builder.makeConstant(specConstIdx),
      builder.makeConstant(1u)
    );
    auto isProjectedBool = builder.add(ir::Op::INe(ir::ScalarType::eBool, isProjectedSpecConst, builder.makeConstant(0u)));

    return builder.add(ir::Op::Select(texCoordType, broadcastScalar(builder, isProjectedBool, WriteMask(ComponentBit::eAll)),
      projectedTexCoord, texCoord));
  }

  return projectedTexCoord;
}


bool ResourceMap::handleDclSampler(ir::Builder& builder, const Instruction& op) {
  auto dcl = op.getDcl();
  auto dst = op.getDst();
  uint32_t samplerIndex = dst.getIndex();

  dxbc_spv_assert(dst.getRegisterType() == RegisterType::eSampler);

  SpecConstTextureType textureType = specConstTextureTypeFromTextureType(dcl.getTextureType());

  auto sampler = dclSampler(builder, samplerIndex);
  auto texture = dclTexture(builder, textureType, samplerIndex);

  auto& resourceInfo = m_samplers.at(samplerIndex);
  resourceInfo.regIndex = samplerIndex;
  resourceInfo.samplerDef = sampler;
  resourceInfo.textureDefs[uint32_t(textureType)] = texture;
  resourceInfo.textureType = std::optional(textureType);
  return true;
}


bool ResourceMap::dclSamplerAndAllTextureTypes(ir::Builder& builder, uint32_t samplerIndex) {
  auto sampler = dclSampler(builder, samplerIndex);

  std::array<ir::SsaDef, uint32_t(SpecConstTextureType::eTexture3D) + 1u> textures;

  for (uint32_t i = 0; i < textures.size(); i++) {
    SpecConstTextureType textureType = SpecConstTextureType(i);
    textures[i] = dclTexture(builder, textureType, samplerIndex);
  }

  auto& resourceInfo = m_samplers.at(samplerIndex);
  resourceInfo.regIndex = samplerIndex;
  resourceInfo.samplerDef = sampler;
  resourceInfo.textureDefs = textures;
  return true;
}


ir::SsaDef ResourceMap::dclSampler(ir::Builder& builder, uint32_t samplerIndex) {
  auto samplerDef = builder.add(ir::Op::DclSampler(m_converter.getEntryPoint(), SamplerBindingsRegSpace, samplerIndex, 1u));

  if (m_converter.m_options.includeDebugNames) {
    const ConstantInfo* ctabEntry = nullptr;

    for (const auto& entry : m_converter.m_ctab.entries()[uint32_t(ConstantType::eSampler)]) {
      if (entry.index <= samplerIndex && entry.index + entry.count > samplerIndex) {
        ctabEntry = &entry;
        break;
      }
    }

    std::stringstream nameStream;
    nameStream << "s_";
    nameStream << samplerIndex;

    if (ctabEntry) {
      nameStream << "_";
      nameStream << ctabEntry->name;
    }

    std::string name = nameStream.str();
    builder.add(ir::Op::DebugName(samplerDef, name.c_str()));
  }

  return samplerDef;
}


ir::SsaDef ResourceMap::dclTexture(ir::Builder& builder, SpecConstTextureType textureType, uint32_t samplerIndex) {
  auto textureDef = builder.add(ir::Op::DclSrv(ir::ScalarType::eF32, m_converter.getEntryPoint(), TextureBindingsRegSpace,
    samplerIndex, 1u, resourceKindFromTextureType(textureTypeFromSpecConstTextureType(textureType))));

  if (m_converter.m_options.includeDebugNames) {
    const ConstantInfo* ctabEntry = nullptr;

    for (const auto& entry : m_converter.m_ctab.entries()[uint32_t(ConstantType::eSampler)]) {
      if (entry.index <= samplerIndex && entry.index + entry.count > samplerIndex) {
        ctabEntry = &entry;
        break;
      }
    }

    std::stringstream nameStream;
    nameStream << "s_";
    nameStream << samplerIndex;

    if (ctabEntry) {
      nameStream << "_";
      nameStream << ctabEntry->name;
    }

    nameStream << "_";
    nameStream << textureTypeFromSpecConstTextureType(textureType);

    std::string name = nameStream.str();
    builder.add(ir::Op::DebugName(textureDef, name.c_str()));
  }

  return textureDef;
}


ir::SsaDef ResourceMap::emitSampleImageFunction(
  ir::Builder &builder,
  uint32_t samplerIndex,
  SamplingConfig config
) {
  uint32_t specConstIdx = m_converter.m_specConstants.getSamplerSpecConstIndex(
    m_converter.getShaderInfo().getType(), samplerIndex);

  auto vec4FType = ir::BasicType(ir::ScalarType::eF32, 4u);
  auto functionOp = ir::Op::Function(vec4FType);

  auto cursor = builder.setCursor(m_functionInsertPoint);

  /* TexCoord */
  auto texCoordParam = builder.add(ir::Op::DclParam(vec4FType));
  functionOp.addOperand(texCoordParam);

  if (m_converter.getOptions().includeDebugNames)
    builder.add(ir::Op::DebugName(texCoordParam, "texCoord"));

  ir::SsaDef lodParam = { };
  ir::SsaDef lodBiasParam = { };
  ir::SsaDef dxParam = { };
  ir::SsaDef dyParam = { };

  if (config & SamplingConfigBit::eExplicitLod) {
    /* Lod */
    lodParam = builder.add(ir::Op::DclParam(ir::ScalarType::eF32));
    functionOp.addOperand(lodParam);

    if (m_converter.getOptions().includeDebugNames) {
      builder.add(ir::Op::DebugName(lodParam, "lod"));
    }
  }

  if (config & SamplingConfigBit::eLodBias) {
    /* LodBias */
    lodBiasParam = builder.add(ir::Op::DclParam(ir::ScalarType::eF32));
    functionOp.addOperand(lodBiasParam);

    if (m_converter.getOptions().includeDebugNames) {
      builder.add(ir::Op::DebugName(lodBiasParam, "lodBias"));
    }
  }

  if (config & SamplingConfigBit::eExplicitDerivatives) {
    dxParam = builder.add(ir::Op::DclParam(ir::BasicType(ir::ScalarType::eF32, 4u))); /* Dx */
    dyParam = builder.add(ir::Op::DclParam(ir::BasicType(ir::ScalarType::eF32, 4u))); /* Dy */

    functionOp.addOperand(dxParam);
    functionOp.addOperand(dyParam);

    if (m_converter.getOptions().includeDebugNames) {
      builder.add(ir::Op::DebugName(dxParam, "dxParam"));
      builder.add(ir::Op::DebugName(dyParam, "dyParam"));
    }
  }

  auto function = builder.add(std::move(functionOp));

  auto texCoord = builder.add(ir::Op::ParamLoad(vec4FType, function, texCoordParam));
  auto lod = lodParam ? builder.add(ir::Op::ParamLoad(ir::ScalarType::eF32, function, lodParam)) : ir::SsaDef();
  auto lodBias = lodBiasParam ? builder.add(ir::Op::ParamLoad(ir::ScalarType::eF32, function, lodBiasParam)) : ir::SsaDef();
  auto dx = dxParam ? builder.add(ir::Op::ParamLoad(ir::BasicType(ir::ScalarType::eF32, 4u), function, dxParam)) : ir::SsaDef();
  auto dy = dyParam ? builder.add(ir::Op::ParamLoad(ir::BasicType(ir::ScalarType::eF32, 4u), function, dyParam)) : ir::SsaDef();

  const auto& samplerInfo = m_samplers.at(samplerIndex);
  dxbc_spv_assert(samplerInfo.regIndex == samplerIndex);
  auto sampler = builder.add(ir::Op::DescriptorLoad(ir::ScalarType::eSampler, samplerInfo.samplerDef, ir::SsaDef()));

  if (m_converter.getShaderInfo().getVersion().first >= 2) {
    /* Shader model 2+ requires declaring samplers/textures with a DCL instruction first. */
    dxbc_spv_assert(samplerInfo.textureType.has_value());

    auto specConstTextureType = samplerInfo.textureType.value();

    auto descriptor = builder.add(ir::Op::DescriptorLoad(ir::ScalarType::eSrv,
      samplerInfo.textureDefs[uint32_t(specConstTextureType)],
      ir::SsaDef()));

    builder.add(ir::Op::Return(ir::BasicType(ir::ScalarType::eF32, 4u), emitSampleColorOrDref(builder, texCoord, specConstTextureType, samplerIndex, descriptor, sampler, lod, lodBias, dx, dy)));
  } else {
    /* Shader model 1 does not require declaring samplers/textures with a DCL instruction.
     * We emit a switch() block with one case for each texture type. Decide based on a spec constant. */
    auto resultTmp = builder.add(ir::Op::DclTmp(ir::BasicType(ir::ScalarType::eF32, 4u), m_converter.getEntryPoint()));
    builder.add(ir::Op::TmpStore(resultTmp, ir::broadcastScalar(builder, builder.makeConstant(0.0f), ComponentBit::eAll)));

    /* Load spec constant. */
    auto samplerTypeSpecConst = m_converter.m_specConstants.get(
      builder,
      SpecConstantId::eSpecSamplerType,
      builder.makeConstant(2u * specConstIdx),
      builder.makeConstant(2u)
    );

    auto textureTypeSwitch = builder.add(ir::Op::ScopedSwitch(ir::SsaDef(), samplerTypeSpecConst));

    /* Emit a switch case for each texture type. */
    for (uint32_t i = 0; i <= uint32_t(SpecConstTextureType::eTexture3D); i++) {
      builder.add(ir::Op::ScopedSwitchCase(textureTypeSwitch, i));

      auto descriptor = builder.add(ir::Op::DescriptorLoad(ir::ScalarType::eSrv,
        samplerInfo.textureDefs[i],
        ir::SsaDef()));

      auto typeResult = emitSampleColorOrDref(builder, texCoord, SpecConstTextureType(i), samplerIndex, descriptor, sampler, lod, lodBias, dx, dy);

      builder.add(ir::Op::TmpStore(resultTmp, typeResult));
      builder.add(ir::Op::ScopedSwitchBreak(textureTypeSwitch));
    }

    auto textureTypeSwitchEnd = builder.add(ir::Op::ScopedEndSwitch(textureTypeSwitch));
    builder.rewriteOp(textureTypeSwitch, ir::Op(builder.getOp(textureTypeSwitch)).setOperand(0u, textureTypeSwitchEnd));

    /* Return whatever value we loaded in the texture type switch. */
    auto returnVal = builder.add(ir::Op::TmpLoad(ir::BasicType(ir::ScalarType::eF32, 4u), resultTmp));
    builder.add(ir::Op::Return(ir::BasicType(ir::ScalarType::eF32, 4u), returnVal));
  }

  builder.add(ir::Op::FunctionEnd());

  if (m_converter.m_options.includeDebugNames) {
    const ConstantInfo* ctabEntry = nullptr;

    for (const auto& entry : m_converter.m_ctab.entries()[uint32_t(ConstantType::eSampler)]) {
      if (entry.index <= samplerIndex && entry.index + entry.count > samplerIndex) {
        ctabEntry = &entry;
        break;
      }
    }

    std::stringstream nameStream;
    nameStream << "sampleTexture_";
    nameStream << samplerIndex;

    if (ctabEntry) {
      nameStream << "_";
      nameStream << ctabEntry->name;
    }

    if (config & SamplingConfigBit::eExplicitLod)
      nameStream << "_explicit";

    if (config & SamplingConfigBit::eLodBias)
      nameStream << "_bias";

    if (config & SamplingConfigBit::eExplicitDerivatives)
      nameStream << "_grad";


    std::string name = nameStream.str();
    builder.add(ir::Op::DebugName(function, name.c_str()));
  }

  builder.setCursor(cursor);
  return function;
}


ir::SsaDef ResourceMap::emitSampleColorOrDref(
  ir::Builder& builder,
  ir::SsaDef texCoord,
  SpecConstTextureType textureType,
  uint32_t samplerIndex,
  ir::SsaDef descriptor,
  ir::SsaDef sampler,
  ir::SsaDef lod,
  ir::SsaDef lodBias,
  ir::SsaDef dx,
  ir::SsaDef dy
) {
  uint32_t specConstIdx = m_converter.m_specConstants.getSamplerSpecConstIndex(
    m_converter.getShaderInfo().getType(), samplerIndex);

  if (textureType != SpecConstTextureType::eTexture3D) {
    auto resultTmp = builder.add(ir::Op::DclTmp(ir::BasicType(ir::ScalarType::eF32, 4u), m_converter.getEntryPoint()));

    auto isDepth = m_converter.m_specConstants.get(
      builder,
      SpecConstantId::eSpecSamplerDepthMode,
      builder.makeConstant(specConstIdx),
      builder.makeConstant(1u)
    );
    auto isDepthCondition = builder.add(ir::Op::INe(ir::ScalarType::eBool, isDepth, builder.makeConstant(0u)));

    /* if (SpecSamplerDepthMode & (1u << samplerIndex)) */
    auto isDepthIf = builder.add(ir::Op::ScopedIf(ir::SsaDef(), isDepthCondition));
    auto depthResult = emitSampleDref(builder, texCoord, textureType, descriptor, sampler, lod, lodBias, dx, dy);
    builder.add(ir::Op::TmpStore(resultTmp, depthResult));

    /* else */
    builder.add(ir::Op::ScopedElse(isDepthIf));
    auto colorResult = emitSampleColorImageType(builder, texCoord, textureType, samplerIndex, descriptor, sampler, lod, lodBias, dx, dy);
    builder.add(ir::Op::TmpStore(resultTmp, colorResult));

    /* endif */
    auto isDepthEnd = builder.add(ir::Op::ScopedEndIf(isDepthIf));
    builder.rewriteOp(isDepthIf, ir::Op(builder.getOp(isDepthIf)).setOperand(0u, isDepthEnd));

    return builder.add(ir::Op::TmpLoad(ir::BasicType(ir::ScalarType::eF32, 4u), resultTmp));
  } else {
    return emitSampleColorImageType(builder, texCoord, textureType, samplerIndex, descriptor, sampler, lod, lodBias, dx, dy);
  }
}


ir::SsaDef ResourceMap::emitSampleColorImageType(
  ir::Builder& builder,
  ir::SsaDef texCoord,
  SpecConstTextureType textureType,
  uint32_t samplerIndex,
  ir::SsaDef descriptor,
  ir::SsaDef sampler,
  ir::SsaDef lod,
  ir::SsaDef lodBias,
  ir::SsaDef dx,
  ir::SsaDef dy
) {
  uint32_t specConstIdx = m_converter.m_specConstants.getSamplerSpecConstIndex(
    m_converter.getShaderInfo().getType(), samplerIndex);

  uint32_t texCoordComponentCount = textureType == SpecConstTextureType::eTexture2D ? 2u : 3u;
  std::array<ir::SsaDef, 4u> texCoordComponents;

  for (uint32_t i = 0u; i < texCoordComponentCount; i++) {
    texCoordComponents[i] = builder.add(ir::Op::CompositeExtract(ir::ScalarType::eF32, texCoord, builder.makeConstant(i)));
  }

  auto sizedTexCoord = buildVector(builder, ir::ScalarType::eF32, texCoordComponentCount, texCoordComponents.data());

  auto sizedDx = ir::SsaDef();
  auto sizedDy = ir::SsaDef();

  if (dx && dy) {
    /* Load derivatives for textureGrad sampling function. */
    std::array<ir::SsaDef, 4u> dxComponents;
    std::array<ir::SsaDef, 4u> dyComponents;

    for (uint32_t i = 0u; i < texCoordComponentCount; i++) {
      dxComponents[i] = builder.add(ir::Op::CompositeExtract(ir::ScalarType::eF32, dx, builder.makeConstant(i)));
      dyComponents[i] = builder.add(ir::Op::CompositeExtract(ir::ScalarType::eF32, dy, builder.makeConstant(i)));
    }

    sizedDx = buildVector(builder, ir::ScalarType::eF32, texCoordComponentCount, dxComponents.data());
    sizedDy = buildVector(builder, ir::ScalarType::eF32, texCoordComponentCount, dyComponents.data());
  }

  auto color = builder.add(ir::Op::ImageSample(
    ir::BasicType(ir::ScalarType::eF32, 4u),
    descriptor,
    sampler,
    ir::SsaDef(),
    sizedTexCoord,
    ir::SsaDef(),
    lod,
    lodBias,
    ir::SsaDef(),
    sizedDx,
    sizedDy,
    ir::SsaDef()
  ));

  /* Fetch4 */
  if (m_converter.getShaderInfo().getType() == ShaderType::ePixel && textureType != SpecConstTextureType::eTexture3D) {
    /* Doesn't really work for cubes...
     * D3D9 does support gather on 3D but we cannot :<
     * Nothing probably relies on that though.
     * If we come back to this ever, make sure to handle cube/3d differences. */

    /* Load the spec constant that tells us if fetch4 (gather) is enabled for the sampler. */

    auto fetch4EnabledSpecConst = m_converter.m_specConstants.get(
      builder,
      SpecConstantId::eSpecSamplerFetch4,
      builder.makeConstant(specConstIdx),
      builder.makeConstant(1u)
    );
    auto fetch4Enabled = builder.add(ir::Op::INe(ir::ScalarType::eBool, fetch4EnabledSpecConst, builder.makeConstant(0u)));

    if (textureType == SpecConstTextureType::eTexture2D) {
      /* Account for half texel offset...
       * texcoord += (1.0f - 1.0f / 256.0f) / float(2 * textureSize(sampler, 0)) */

      /* scaledTextureSizeF = float(2 * textureSize(sampler, 0)) */
      auto coordDims = ir::resourceDimensions(resourceKindFromTextureType(textureTypeFromSpecConstTextureType(textureType)));
      auto sizeType = ir::Type()
        .addStructMember(ir::ScalarType::eU32, coordDims)    /* size   */
        .addStructMember(ir::ScalarType::eU32);           /* layers */
      auto textureSizeTypeI = ir::BasicType(ir::ScalarType::eU32, coordDims);
      auto textureSizeType = ir::BasicType(ir::ScalarType::eF32, coordDims);

      auto textureSizeStruct = builder.add(ir::Op::ImageQuerySize(sizeType, descriptor, builder.makeConstant(0u)));
      auto textureSizeI = builder.add(ir::Op::CompositeExtract(textureSizeTypeI, textureSizeStruct, builder.makeConstant(0u)));
      auto const2vec = broadcastScalar(builder, builder.makeConstant(2u), util::makeWriteMaskForComponents(coordDims));
      auto scaledTextureSizeI = builder.add(ir::Op::IMul(textureSizeTypeI, textureSizeI, const2vec));
      auto scaledTextureSizeF = builder.add(ir::Op::ConvertItoF(textureSizeType, scaledTextureSizeI));

      /* invTextureSize = (1.0f - 1.0f / 256.0f) / scaledTextureSizeF */
      float numerator = 1.0f;
      /* HACK: Bias fetch4 half-texel offset to avoid a "grid" effect.
       * Technically we should only do that for non-powers of two
       * as only then does the imprecision need to be biased
       * towards infinity -- but that's not really worth doing... */
      numerator -= 1.0f / 256.0f;

      auto numeratorVec = broadcastScalar(builder, builder.makeConstant(numerator), util::makeWriteMaskForComponents(coordDims));
      auto invTextureSize = builder.add(ir::Op::FDiv(textureSizeType, numeratorVec, scaledTextureSizeF));

      /* texcoord += invTextureSize */
      sizedTexCoord = builder.add(ir::Op::FAdd(textureSizeType, sizedTexCoord, invTextureSize));
    }

    auto fetch4Val = builder.add(ir::Op::ImageGather(
      ir::BasicType(ir::ScalarType::eF32, 4u),
      descriptor,
      sampler,
      ir::SsaDef(),
      sizedTexCoord,
      ir::SsaDef(),
      ir::SsaDef(),
      0u
    ));

    /* Shuffle the vector to match the funny D3D9 order: B R G A */
    fetch4Val = swizzleVector(builder, fetch4Val, Swizzle(Component::eY, Component::eX, Component::eZ, Component::eW), WriteMask(ComponentBit::eAll));

    /* Use Fetch4 value if spec constant bit is set and regular sampled color if not. */
    color = builder.add(ir::Op::Select(ir::BasicType(ir::ScalarType::eF32, 4u),
    broadcastScalar(builder, fetch4Enabled, WriteMask(ComponentBit::eAll)),
    fetch4Val, color));
  }

  /* Load the spec constant that tells us if the texture is unbound. */

  auto isNullSpecConst = m_converter.m_specConstants.get(
    builder,
    SpecConstantId::eSpecSamplerNull,
    builder.makeConstant(specConstIdx),
    builder.makeConstant(1u)
  );
  auto isNull = builder.add(ir::Op::INe(ir::ScalarType::eBool, isNullSpecConst, builder.makeConstant(0u)));

  return builder.add(ir::Op::Select(
    ir::BasicType(ir::ScalarType::eF32, 4u),
    broadcastScalar(builder, isNull, WriteMask(ComponentBit::eAll)),
    builder.makeConstant(0.0f, 0.0f, 0.0f, 1.0f),
    color));
}


ir::SsaDef ResourceMap::emitSampleDref(
  ir::Builder &builder,
  ir::SsaDef texCoord,
  SpecConstTextureType textureType,
  ir::SsaDef descriptor,
  ir::SsaDef sampler,
  ir::SsaDef lod,
  ir::SsaDef lodBias,
  ir::SsaDef dx,
  ir::SsaDef dy
) {
  /* We don't check for NULL here because if there's no texture bound, we always end up in the color path. */

  uint32_t texCoordComponentCount = textureType == SpecConstTextureType::eTexture2D ? 2u : 3u;
  auto referenceComponentIdx = builder.makeConstant(texCoordComponentCount);
  auto reference = builder.add(ir::Op::CompositeExtract(ir::ScalarType::eF32, texCoord, referenceComponentIdx));

  /* [D3D8] Scale Dref from [0..(2^N - 1)] for D24S8 and D16 if Dref scaling is enabled */
  auto drefScaleShift = m_converter.m_specConstants.get(
    builder,
    SpecConstantId::eSpecDrefScaling
  );

  auto drefScale = builder.add(ir::Op::IShl(ir::ScalarType::eU32, builder.makeConstant(1u), drefScaleShift));
  drefScale      = builder.add(ir::Op::ConvertItoF(ir::ScalarType::eF32, drefScale));
  drefScale      = builder.add(ir::Op::FSub(ir::ScalarType::eF32, drefScale, builder.makeConstant(1.0f)));
  drefScale      = builder.add(ir::Op::FDiv(ir::ScalarType::eF32, builder.makeConstant(1.0f), drefScale));

  reference      = builder.add(ir::Op::Select(ir::ScalarType::eF32,
    builder.add(ir::Op::INe(ir::ScalarType::eBool, drefScaleShift, builder.makeConstant(0u))),
    builder.add(ir::Op::FMul(ir::ScalarType::eF32, reference, drefScale)),
    reference
  ));

  /* Clamp Dref to [0..1] for D32F emulating UNORM textures */
  auto clampDref = m_converter.m_specConstants.get(
    builder,
    SpecConstantId::eSpecSamplerDrefClamp
  );
  clampDref = builder.add(ir::Op::INe(ir::ScalarType::eBool, clampDref, builder.makeConstant(0u)));

  auto clampedDref = builder.add(ir::Op::FClamp(ir::ScalarType::eF32, reference, builder.makeConstant(0.0f), builder.makeConstant(1.0f)));
  reference = builder.add(ir::Op::Select(ir::ScalarType::eF32, clampDref, clampedDref, reference));

  std::array<ir::SsaDef, 4u> texCoordComponents;
  for (uint32_t i = 0u; i < texCoordComponentCount; i++) {
    texCoordComponents[i] = builder.add(ir::Op::CompositeExtract(ir::ScalarType::eF32, texCoord, builder.makeConstant(i)));
  }

  auto sizedTexCoord = buildVector(builder, ir::ScalarType::eF32, texCoordComponentCount, texCoordComponents.data());

  auto sizedDx = ir::SsaDef();
  auto sizedDy = ir::SsaDef();

  if (dx && dy) {
    /* Load derivatives for textureGrad sampling function. */
    std::array<ir::SsaDef, 4u> dxComponents;
    std::array<ir::SsaDef, 4u> dyComponents;

    for (uint32_t i = 0u; i < texCoordComponentCount; i++) {
      dxComponents[i] = builder.add(ir::Op::CompositeExtract(ir::ScalarType::eF32, dx, builder.makeConstant(i)));
      dyComponents[i] = builder.add(ir::Op::CompositeExtract(ir::ScalarType::eF32, dy, builder.makeConstant(i)));
    }

    sizedDx = buildVector(builder, ir::ScalarType::eF32, texCoordComponentCount, dxComponents.data());
    sizedDy = buildVector(builder, ir::ScalarType::eF32, texCoordComponentCount, dyComponents.data());
  }

  auto drefResult = builder.add(ir::Op::ImageSample(
    ir::ScalarType::eF32,
    descriptor,
    sampler,
    ir::SsaDef(),
    sizedTexCoord,
    ir::SsaDef(),
    lod,
    lodBias,
    ir::SsaDef(),
    sizedDx,
    sizedDy,
    reference
  ));

  return broadcastScalar(builder, drefResult, WriteMask(ComponentBit::eAll));
}

}
