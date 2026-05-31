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

  bool isSwvp = m_converter.getOptions().isSWVP;

  uint32_t constCountFloat = isSwvp
    ? MaxFloatConstantsSoftware
    : (shaderType == ShaderType::ePixel ? MaxFloatConstantsPS : MaxFloatConstantsVS);

  uint32_t constCountOther = isSwvp
    ? MaxOtherConstantsSoftware
    : MaxOtherConstants;

  m_constants[uint32_t(ConstantType::eFloat4)].inputDef = builder.add(ir::Op::DclInputBuiltIn(
    ir::Type(ir::ScalarType::eF32, 4u).addArrayDimension(constCountFloat), m_converter.getEntryPoint(),
    ir::BuiltIn::eLegacyConstFloat));
  builder.add(ir::Op::DebugName(m_constants[uint32_t(ConstantType::eFloat4)].inputDef, "cF"));

  m_constants[uint32_t(ConstantType::eInt4)].inputDef = builder.add(ir::Op::DclInputBuiltIn(
    ir::Type(ir::ScalarType::eI32, 4u).addArrayDimension(constCountOther), m_converter.getEntryPoint(),
    ir::BuiltIn::eLegacyConstInt));
  builder.add(ir::Op::DebugName(m_constants[uint32_t(ConstantType::eInt4)].inputDef, "cI"));

  m_constants[uint32_t(ConstantType::eBool)].inputDef = builder.add(ir::Op::DclInputBuiltIn(
    ir::Type(ir::ScalarType::eBool).addArrayDimension(constCountOther), m_converter.getEntryPoint(),
    ir::BuiltIn::eLegacyConstBool));
  builder.add(ir::Op::DebugName(m_constants[uint32_t(ConstantType::eBool)].inputDef, "cB"));

  m_samplerState = emitSamplerState(builder);
}


ir::SsaDef ResourceMap::emitConstantLoad(
          ir::Builder&            builder,
    const Operand&                operand,
          WriteMask               componentMask,
          ir::ScalarType          scalarType) {
  ShaderInfo info = m_converter.getShaderInfo();

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
  ir::BasicType type = builder.getOp(constants.inputDef).getType().getBaseType(0u);

  /* Inline immediate constants that were defined in the shader. */
  ir::SsaDef value;

  if (!operand.hasRelativeAddressing()) {
    for (const auto& c : constants.immediateConstants) {
      if (c.index == registerIndex) {
        value = c.def;
        break;
      }
    }
  }

  if (!value) {
    /* Read entire constant vector in one go. */
    ir::SsaDef constantIndex = builder.makeConstant(registerIndex);

    if (operand.hasRelativeAddressing()) {
      dxbc_spv_assert(registerType == RegisterType::eConst
        || registerType == RegisterType::eConst2
        || registerType == RegisterType::eConst3
        || registerType == RegisterType::eConst4);

      constantIndex = m_converter.calculateAddress(builder,
        operand.getRelativeAddressingRegisterType(),
        operand.getRelativeAddressingSwizzle(),
        registerIndex, ir::ScalarType::eU32);
    }

    value = builder.add(ir::Op::InputLoad(type, constants.inputDef, constantIndex));
  }

  std::array<ir::SsaDef, 4u> components = { };

  for (uint32_t i = 0u; i < components.size(); i++) {
    ir::SsaDef& scalar = components[i];
    scalar = value;

    if (!type.isScalar()) {
      scalar = builder.add(ir::Op::CompositeExtract(
        type.getBaseType(), scalar, builder.makeConstant(i)));
    }

    // PS 1.x clamps float constants
    if (m_converter.getShaderInfo().getType() == ShaderType::ePixel
     && m_converter.getShaderInfo().getVersion().first == 1u
     && constantType == ConstantType::eFloat4) {
      scalar = builder.add(ir::Op::FClamp(ir::ScalarType::eF32, scalar,
        builder.makeConstant(-1.0f), builder.makeConstant(1.0f)));
    }

    /* Convert scalars to the requested type */
    if (type != ir::ScalarType::eBool)
      scalar = builder.add(ir::Op::ConsumeAs(scalarType, scalar));
  }

  /* Build result vector */
  return composite(builder, makeVectorType(scalarType, componentMask),
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


void ResourceMap::emitConstantNames(
        ir::Builder&            builder,
  const ConstantTable&          ctab) {
  for (const auto& e : ctab.entries()) {
    auto setIndex = uint32_t(e.registerSet);

    /* Shouldn't happen, not fatal */
    if (setIndex >= m_constants.size() || !e.count || e.name.empty())
      continue;

    /* Don't evaluate debug names for out-of-bounds constants */
    const auto& input = builder.getOp(m_constants.at(setIndex).inputDef);

    if (e.index + e.count > input.getType().getArraySize(0u))
      continue;

    if (e.count == 1u) {
      /* Trivial case, use name as-is and avoid a temporary copy */
      builder.add(ir::Op::DebugMemberName(input.getDef(), e.index, e.name.c_str()));
    } else {
      /* Otherwise, append the index to the name to
       * distinguish matrix rows or array elements. */
      for (uint32_t i = 0u; i < e.count; i++) {
        auto name = e.name + std::to_string(i);
        builder.add(ir::Op::DebugMemberName(input.getDef(), e.index + i, name.c_str()));
      }
    }
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

  if (texCoordType != ir::BasicType(ir::ScalarType::eF32, 4u)) {
    std::array<ir::SsaDef, 4u> texCoordComponents;
    for (uint32_t i = 0; i < 4u; i++) {
      if (i < texCoordType.getVectorSize()) {
        texCoordComponents[i] = extractFromVector(builder, texCoord, i);
        if (texCoordType.getBaseType() != ir::ScalarType::eF32) {
          texCoordComponents[i] = builder.add(ir::Op::ConsumeAs(ir::ScalarType::eF32, texCoordComponents[i]));
        }
      } else {
        texCoordComponents[i] = builder.add(ir::Op::Constant(0.0f));
      }
    }
    texCoord = ir::buildVector(builder, ir::ScalarType::eF32, texCoordComponents.size(), texCoordComponents.data());
  }

  /* Prepare function call with required arguments based on how we're sampling the texture. */
  auto funcCall = ir::Op::FunctionCall(ir::BasicType(ir::ScalarType::eF32, 4u), samplingFunction)
    .addParam(texCoord);

  if (lod) {
    auto lodType = builder.getOp(lod).getType().getBaseType(0u);
    lod = builder.add(ir::Op::ConsumeAs(ir::BasicType(ir::ScalarType::eF32, lodType.getVectorSize()), lod));
    funcCall.addParam(lod);
  }

  if (lodBias) {
    auto lodBiasType = builder.getOp(lodBias).getType().getBaseType(0u);
    lodBias = builder.add(ir::Op::ConsumeAs(ir::BasicType(ir::ScalarType::eF32, lodBiasType.getVectorSize()), lodBias));
    funcCall.addParam(lodBias);
  }

  if (dx || dy) {
    auto dxType = builder.getOp(dx).getType().getBaseType(0u);
    dx = builder.add(ir::Op::ConsumeAs(ir::BasicType(ir::ScalarType::eF32, dxType.getVectorSize()), dx));
    funcCall.addParam(dx);

    auto dyType = builder.getOp(dy).getType().getBaseType(0u);
    dy = builder.add(ir::Op::ConsumeAs(ir::BasicType(ir::ScalarType::eF32, dyType.getVectorSize()), dy));
    funcCall.addParam(dy);
  }

  auto result = builder.add(std::move(funcCall));

  /* The sampling functions always return Vec4<F32>, cast if the caller expects something else. */
  result = builder.add(ir::Op::ConsumeAs(ir::BasicType(scalarType, 4u), result));

  return result;
}


ir::SsaDef ResourceMap::projectTexCoord(ir::Builder& builder, uint32_t samplerIndex, ir::SsaDef texCoord, bool controlWithSpecConst) {
  auto texCoordType = builder.getOp(texCoord).getType().getBaseType(0u);
  auto texCoordW = ir::extractFromVector(builder, texCoord, 3u);

  auto projectedTexCoord = builder.add(ir::Op::FDiv(texCoordType,
    texCoord, broadcastScalar(builder, texCoordW, ComponentBit::eAll)));

  if (controlWithSpecConst) {
    auto isProjected = loadSamplerState(builder, samplerIndex, ir::LegacySamplerStateLayout::eUseProjection);

    projectedTexCoord = builder.add(ir::Op::Select(texCoordType,
      broadcastScalar(builder, isProjected, WriteMask(ComponentBit::eAll)),
      projectedTexCoord, texCoord));
  }

  return projectedTexCoord;
}


bool ResourceMap::handleDclSampler(ir::Builder& builder, const Instruction& op) {
  auto dcl = op.getDcl();
  auto dst = op.getDst();
  uint32_t samplerIndex = dst.getIndex();

  dxbc_spv_assert(dst.getRegisterType() == RegisterType::eSampler);

  SamplerStateType textureType = samplerStateTypeFromTextureType(dcl.getTextureType());

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

  std::array<ir::SsaDef, uint32_t(SamplerStateType::eCount)> textures;

  for (uint32_t i = 0; i < textures.size(); i++) {
    SamplerStateType textureType = SamplerStateType(i);
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

    for (const auto& entry : m_converter.m_ctab.entries()) {
      if (entry.registerSet != ConstantType::eSampler)
        continue;

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


ir::SsaDef ResourceMap::dclTexture(ir::Builder& builder, SamplerStateType textureType, uint32_t samplerIndex) {
  auto textureDef = builder.add(ir::Op::DclSrv(ir::ScalarType::eF32, m_converter.getEntryPoint(), TextureBindingsRegSpace,
    samplerIndex, 1u, resourceKindFromTextureType(textureTypeFromSamplerStateType(textureType))));

  if (m_converter.m_options.includeDebugNames) {
    const ConstantInfo* ctabEntry = nullptr;

    for (const auto& entry : m_converter.m_ctab.entries()) {
      if (entry.registerSet != ConstantType::eSampler)
        continue;

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
    nameStream << textureTypeFromSamplerStateType(textureType);

    std::string name = nameStream.str();
    builder.add(ir::Op::DebugName(textureDef, name.c_str()));
  }

  return textureDef;
}


ir::SsaDef ResourceMap::emitSampleImageFunction(
  ir::Builder &builder,
  uint32_t samplerIndex,
  SamplingConfig config) {
  auto vec4FType = ir::BasicType(ir::ScalarType::eF32, 4u);
  auto functionOp = ir::Op::Function(vec4FType);

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

    if (m_converter.getOptions().includeDebugNames)
      builder.add(ir::Op::DebugName(lodParam, "lod"));
  }

  if (config & SamplingConfigBit::eLodBias) {
    /* LodBias */
    lodBiasParam = builder.add(ir::Op::DclParam(ir::ScalarType::eF32));
    functionOp.addOperand(lodBiasParam);

    if (m_converter.getOptions().includeDebugNames)
      builder.add(ir::Op::DebugName(lodBiasParam, "lodBias"));
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

  auto function = builder.addBefore(builder.getCode().first->getDef(), std::move(functionOp));
  auto cursor = builder.setCursor(function);

  auto texCoord = builder.add(ir::Op::ParamLoad(vec4FType, function, texCoordParam));
  auto lod = lodParam ? builder.add(ir::Op::ParamLoad(ir::ScalarType::eF32, function, lodParam)) : ir::SsaDef();
  auto lodBias = lodBiasParam ? builder.add(ir::Op::ParamLoad(ir::ScalarType::eF32, function, lodBiasParam)) : ir::SsaDef();
  auto dx = dxParam ? builder.add(ir::Op::ParamLoad(ir::BasicType(ir::ScalarType::eF32, 4u), function, dxParam)) : ir::SsaDef();
  auto dy = dyParam ? builder.add(ir::Op::ParamLoad(ir::BasicType(ir::ScalarType::eF32, 4u), function, dyParam)) : ir::SsaDef();

  const auto& samplerInfo = m_samplers.at(samplerIndex);
  dxbc_spv_assert(samplerInfo.regIndex == samplerIndex);
  auto sampler = builder.add(ir::Op::DescriptorLoad(ir::ScalarType::eSampler, samplerInfo.samplerDef, builder.makeConstant(0u)));

  if (m_converter.getShaderInfo().getVersion().first >= 2) {
    /* Shader model 2+ requires declaring samplers/textures with a DCL instruction first. */
    dxbc_spv_assert(samplerInfo.textureType.has_value());

    auto textureType = samplerInfo.textureType.value();

    auto descriptor = builder.add(ir::Op::DescriptorLoad(ir::ScalarType::eSrv,
      samplerInfo.textureDefs[uint32_t(textureType)], builder.makeConstant(0u)));

    builder.add(ir::Op::Return(ir::BasicType(ir::ScalarType::eF32, 4u),
      emitSampleColorOrDref(builder, texCoord, textureType, samplerIndex, descriptor, sampler, lod, lodBias, dx, dy)));
  } else {
    /* Shader model 1 does not require declaring samplers/textures with a DCL instruction.
     * We emit a switch() block with one case for each texture type. Decide based on sampler state. */
    auto resultTmp = builder.add(ir::Op::DclTmp(ir::BasicType(ir::ScalarType::eF32, 4u), m_converter.getEntryPoint()));
    builder.add(ir::Op::TmpStore(resultTmp, ir::broadcastScalar(builder, builder.makeConstant(0.0f), ComponentBit::eAll)));

    /* Load spec constant. */
    auto samplerType = loadSamplerState(builder,
      samplerIndex, ir::LegacySamplerStateLayout::eTextureType);

    auto textureTypeSwitch = builder.add(ir::Op::ScopedSwitch(ir::SsaDef(), samplerType));

    /* Emit a switch case for each texture type. */
    for (uint32_t i = 0; i < uint32_t(SamplerStateType::eCount); i++) {
      builder.add(ir::Op::ScopedSwitchCase(textureTypeSwitch, i));

      auto descriptor = builder.add(ir::Op::DescriptorLoad(ir::ScalarType::eSrv,
        samplerInfo.textureDefs[i],
        builder.makeConstant(0u)));

      auto typeResult = emitSampleColorOrDref(builder, texCoord, SamplerStateType(i),
        samplerIndex, descriptor, sampler, lod, lodBias, dx, dy);

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

    for (const auto& entry : m_converter.m_ctab.entries()) {
      if (entry.registerSet != ConstantType::eSampler)
        continue;

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
  SamplerStateType textureType,
  uint32_t samplerIndex,
  ir::SsaDef descriptor,
  ir::SsaDef sampler,
  ir::SsaDef lod,
  ir::SsaDef lodBias,
  ir::SsaDef dx,
  ir::SsaDef dy) {
  if (textureType != SamplerStateType::eTexture3D) {
    auto resultTmp = builder.add(ir::Op::DclTmp(ir::BasicType(ir::ScalarType::eF32, 4u), m_converter.getEntryPoint()));

    auto isDepth = loadSamplerState(builder, samplerIndex,
      ir::LegacySamplerStateLayout::eUseDepthCompare);

    /* if (samplerState[idx].useDepthCompare) */
    auto isDepthIf = builder.add(ir::Op::ScopedIf(ir::SsaDef(), isDepth));
    auto depthResult = emitSampleDref(builder, texCoord, textureType, samplerIndex, descriptor, sampler, lod, lodBias, dx, dy);
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
  SamplerStateType textureType,
  uint32_t samplerIndex,
  ir::SsaDef descriptor,
  ir::SsaDef sampler,
  ir::SsaDef lod,
  ir::SsaDef lodBias,
  ir::SsaDef dx,
  ir::SsaDef dy) {
  uint32_t texCoordComponentCount = textureType == SamplerStateType::eTexture2D ? 2u : 3u;
  std::array<ir::SsaDef, 4u> texCoordComponents;

  for (uint32_t i = 0u; i < texCoordComponentCount; i++) {
    texCoordComponents[i] = ir::extractFromVector(builder, texCoord, i);
  }

  auto sizedTexCoord = buildVector(builder, ir::ScalarType::eF32, texCoordComponentCount, texCoordComponents.data());

  auto sizedDx = ir::SsaDef();
  auto sizedDy = ir::SsaDef();

  if (dx && dy) {
    /* Load derivatives for textureGrad sampling function. */
    std::array<ir::SsaDef, 4u> dxComponents;
    std::array<ir::SsaDef, 4u> dyComponents;

    for (uint32_t i = 0u; i < texCoordComponentCount; i++) {
      dxComponents[i] = ir::extractFromVector(builder, dx, i);
      dyComponents[i] = ir::extractFromVector(builder, dy, i);
    }

    sizedDx = buildVector(builder, ir::ScalarType::eF32, texCoordComponentCount, dxComponents.data());
    sizedDy = buildVector(builder, ir::ScalarType::eF32, texCoordComponentCount, dyComponents.data());
  }

  auto color = builder.add(ir::Op::ImageSample(
    ir::BasicType(ir::ScalarType::eF32, 4u),
    descriptor, sampler, ir::SsaDef(), sizedTexCoord,
    ir::SsaDef(), lod, lodBias, ir::SsaDef(),
    sizedDx, sizedDy, ir::SsaDef() ));

  /* Fetch4
   * D3D9 does support gather on 3D but we cannot :< */
  if (m_converter.getShaderInfo().getType() == ShaderType::ePixel && textureType != SamplerStateType::eTexture3D) {
    /* Load the spec constant that tells us if fetch4 (gather) is enabled for the sampler. */
    auto fetch4Enabled = loadSamplerState(builder, samplerIndex,
      ir::LegacySamplerStateLayout::eUseGather);

    /* Account for half texel offset */
    if (textureType == SamplerStateType::eTexture2D) {
      /* Doesn't really work for cubes...
       * Nothing probably relies on that though.
       * If we come back to this ever, make sure to handle cube/3d differences.
       *   texcoord += (1.0f - 1.0f / 256.0f) / float(2 * textureSize(sampler, 0))
       * = texcoord += (256.0f / 512.0f) / textureSize(sampler, 0) */
      auto coordDims = ir::resourceDimensions(resourceKindFromTextureType(textureTypeFromSamplerStateType(textureType)));

      auto sizeType = ir::Type()
        .addStructMember(ir::ScalarType::eU32, coordDims)   /* size   */
        .addStructMember(ir::ScalarType::eU32);             /* layers */

      /* HACK: Bias fetch4 half-texel offset to avoid a "grid" effect.
       * Technically we should only do that for non-powers of two
       * as only then does the imprecision need to be biased
       * towards infinity -- but that's not really worth doing... */
      float numerator = 256.0f / 512.0f;

      auto textureSizeStruct = builder.add(ir::Op::ImageQuerySize(sizeType, descriptor, builder.makeConstant(0u)));
      auto textureSizeI = ir::extractFromVector(builder, textureSizeStruct, 0u);
      auto textureSizeType = ir::BasicType(ir::ScalarType::eF32, coordDims);
      auto textureSizeF = builder.add(ir::Op::ConvertItoF(textureSizeType, textureSizeI));

      auto numeratorVec = broadcastScalar(builder, builder.makeConstant(numerator), util::makeWriteMaskForComponents(coordDims));
      auto invTextureSize = builder.add(ir::Op::FDiv(textureSizeType, numeratorVec, textureSizeF));

      /* texcoord += invTextureSize */
      sizedTexCoord = builder.add(ir::Op::FAdd(textureSizeType, sizedTexCoord, invTextureSize));
    }

    auto fetch4Val = builder.add(ir::Op::ImageGather(
      ir::BasicType(ir::ScalarType::eF32, 4u),
      descriptor, sampler, ir::SsaDef(), sizedTexCoord,
      ir::SsaDef(), ir::SsaDef(), 0u ));

    /* Shuffle the vector to match the funny D3D9 order: B R G A */
    fetch4Val = swizzleVector(builder, fetch4Val, Swizzle(Component::eZ, Component::eX, Component::eY, Component::eW), WriteMask(ComponentBit::eAll));

    /* Use Fetch4 value if spec constant bit is set and regular sampled color if not. */
    color = builder.add(ir::Op::Select(ir::BasicType(ir::ScalarType::eF32, 4u),
    broadcastScalar(builder, fetch4Enabled, WriteMask(ComponentBit::eAll)),
    fetch4Val, color));
  }

  /* Load the spec constant that tells us if the texture is unbound. */
  auto isNull = loadSamplerState(builder, samplerIndex,
    ir::LegacySamplerStateLayout::eIsNull);

  return builder.add(ir::Op::Select(
    ir::BasicType(ir::ScalarType::eF32, 4u),
    broadcastScalar(builder, isNull, WriteMask(ComponentBit::eAll)),
    builder.makeConstant(0.0f, 0.0f, 0.0f, 1.0f),
    color));
}


ir::SsaDef ResourceMap::emitSampleDref(
  ir::Builder &builder,
  ir::SsaDef texCoord,
  SamplerStateType textureType,
  uint32_t samplerIndex,
  ir::SsaDef descriptor,
  ir::SsaDef sampler,
  ir::SsaDef lod,
  ir::SsaDef lodBias,
  ir::SsaDef dx,
  ir::SsaDef dy) {
  /* We don't check for NULL here because if there's no texture bound, we always end up in the color path. */
  uint32_t texCoordComponentCount = textureType == SamplerStateType::eTexture2D ? 2u : 3u;
  auto reference = ir::extractFromVector(builder, texCoord, texCoordComponentCount);

  /* [D3D8] Scale Dref from [0..(2^N - 1)] for D24S8 and D16 if Dref scaling is enabled */
  if (m_converter.getShaderInfo().getVersion().first == 1u) {
    reference = builder.add(ir::Op::FMul(ir::ScalarType::eF32, reference,
      loadSamplerState(builder, samplerIndex, ir::LegacySamplerStateLayout::eDrefScale)));
  }

  /* Clamp Dref to [0..1] for D32F emulating UNORM textures */
  auto clampDref = loadSamplerState(builder, samplerIndex,
    ir::LegacySamplerStateLayout::eDrefClamp);

  auto clampedDref = builder.add(ir::Op::FClamp(ir::ScalarType::eF32, reference, builder.makeConstant(0.0f), builder.makeConstant(1.0f)));
  reference = builder.add(ir::Op::Select(ir::ScalarType::eF32, clampDref, clampedDref, reference));

  std::array<ir::SsaDef, 4u> texCoordComponents;

  for (uint32_t i = 0u; i < texCoordComponentCount; i++)
    texCoordComponents[i] = ir::extractFromVector(builder, texCoord, i);

  auto sizedTexCoord = buildVector(builder, ir::ScalarType::eF32, texCoordComponentCount, texCoordComponents.data());

  auto sizedDx = ir::SsaDef();
  auto sizedDy = ir::SsaDef();

  if (dx && dy) {
    /* Load derivatives for textureGrad sampling function. */
    std::array<ir::SsaDef, 4u> dxComponents;
    std::array<ir::SsaDef, 4u> dyComponents;

    for (uint32_t i = 0u; i < texCoordComponentCount; i++) {
      dxComponents[i] = ir::extractFromVector(builder, dx, i);
      dyComponents[i] = ir::extractFromVector(builder, dy, i);
    }

    sizedDx = buildVector(builder, ir::ScalarType::eF32, texCoordComponentCount, dxComponents.data());
    sizedDy = buildVector(builder, ir::ScalarType::eF32, texCoordComponentCount, dyComponents.data());
  }

  auto drefResult = builder.add(ir::Op::ImageSample(
    ir::ScalarType::eF32, descriptor, sampler,
    ir::SsaDef(), sizedTexCoord, ir::SsaDef(),
    lod, lodBias, ir::SsaDef(),
    sizedDx, sizedDy, reference));

  return broadcastScalar(builder, drefResult, WriteMask(ComponentBit::eAll));
}


ir::SsaDef ResourceMap::emitSamplerState(ir::Builder& builder) {
  /* Declare sampler state with the maximum number of samplers that
   * the current shader stage can declare. This will be lowered to
   * some other kind of data structure anyway. */
  ShaderType shaderType = m_converter.getShaderInfo().getType();

  uint32_t maxSamplerCount = shaderType == ShaderType::ePixel
    ? MaxSamplerCountPs
    : MaxSamplerCountVs;

  auto samplerState = ir::Op::DclInputBuiltIn(
    ir::makeLegacySamplerStateType(maxSamplerCount),
    m_converter.getEntryPoint(), ir::BuiltIn::eLegacySamplerState);

  if (shaderType == ShaderType::ePixel)
    samplerState.addOperand(ir::InterpolationMode::eFlat);

  auto input = builder.add(std::move(samplerState));

  if (m_converter.getOptions().includeDebugNames) {
    static const std::array<std::pair<ir::LegacySamplerStateLayout, const char*>, 7u> s_names = {{
      { ir::LegacySamplerStateLayout::eTextureType,     "texture_type"  },
      { ir::LegacySamplerStateLayout::eUseDepthCompare, "use_dref"      },
      { ir::LegacySamplerStateLayout::eUseProjection,   "use_proj"      },
      { ir::LegacySamplerStateLayout::eIsNull,          "is_null"       },
      { ir::LegacySamplerStateLayout::eUseGather,       "use_gather"    },
      { ir::LegacySamplerStateLayout::eDrefClamp,       "dref_clamp"    },
      { ir::LegacySamplerStateLayout::eDrefScale,       "dref_scale"    },
    }};

    builder.add(ir::Op::DebugName(input, "sampler_state"));

    for (const auto& e : s_names)
      builder.add(ir::Op::DebugMemberName(input, uint32_t(e.first), e.second));
  }

  return input;
}


ir::SsaDef ResourceMap::loadSamplerState(
        ir::Builder&                  builder,
        uint32_t                      sampler,
        ir::LegacySamplerStateLayout  member) {
  auto memberIndex = uint32_t(member);
  auto memberType = builder.getOp(m_samplerState).getType().getBaseType(memberIndex);

  return builder.add(ir::Op::InputLoad(memberType, m_samplerState,
    builder.makeConstant(sampler, memberIndex)));
}

}
