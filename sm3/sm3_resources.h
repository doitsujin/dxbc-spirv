#pragma once

#include <cstdint>

#include "sm3_parser.h"
#include "sm3_types.h"

#include "../ir/ir.h"
#include "../ir/ir_builder.h"

namespace dxbc_spv::sm3 {

class Converter;

enum class SamplerStateType : uint32_t {
  eTexture2D   = 0u,
  eTexture3D   = 1u,
  eTextureCube = 2u,

  eCount
};

enum class SamplingConfigBit : uint8_t {
  eExplicitLod         = 1u << 0u,
  eLodBias             = 1u << 1u,
  eExplicitDerivatives = 1u << 2u,

  eFlagEnum            = 0u,
};

using SamplingConfig = util::Flags<SamplingConfigBit>;

struct SamplerRegister {
  /** Register index */
  uint32_t regIndex = 0u;

  /** Declaration of the texture
   * One for each texture type on SM1 and only one on SM2+ */
  std::array<ir::SsaDef, 3u> textureDefs = { };

  /** The type of the texture. This is only set on SM2+ as there are no dcl_samplerType instructions
   * on SM1. This texture type represents the index of the one valid `textureDef` on SM2. */
  std::optional<SamplerStateType> textureType = std::nullopt;

  /** Declaration of the sampler */
  ir::SsaDef samplerDef = { };

  /** Sampling functions. Will be populated lazily.
   * A SamplingFunctionConfigBit bitmask makes up the index into this array.
   * Each function takes in an F32 vec4 for the texCoords and some
   * will take additional arguments for LODs and/or derivatives depending
   * on the flags. */
  std::array<ir::SsaDef, 8u> samplingFunctions = { };
};

struct ImmediateConstDef {
  uint32_t index;
  ir::SsaDef def;
};

struct Constants {
  /** All statically defined constants of this constant type. */
  util::small_vector<ImmediateConstDef, 16u> immediateConstants;

  ir::SsaDef inputDef = { };

  ConstantType type;
};


/** Resource variable map. Handles both texture declaration and access.
 *  Also takes care of textures getting accessed without getting declared first
 *  in SM1. On top of that it handles constant registers. */
class ResourceMap {

public:

  explicit ResourceMap(Converter& converter);

  ~ResourceMap();

  void initialize(ir::Builder& builder);

  /** Handles Dcl instructions on SM 2+ with Sampler as the register type. */
  bool handleDclSampler(ir::Builder& builder, const Instruction& op);

  bool dclSamplerAndAllTextureTypes(ir::Builder& builder, uint32_t samplerIndex);

  ir::SsaDef projectTexCoord(ir::Builder& builder, uint32_t samplerIndex, ir::SsaDef texCoord, bool controlWithSpecConst);

  /** Loads a resource or sampler descriptor and retrieves basic
   *  properties required to perform any operations on typed resources. */
  ir::SsaDef emitSample(
          ir::Builder&   builder,
          uint32_t       samplerIndex,
          ir::SsaDef     texCoord,
          ir::SsaDef     lod,
          ir::SsaDef     lodBias,
          ir::SsaDef     dx,
          ir::SsaDef     dy,
          ir::ScalarType scalarType);

  /** Loads data from a constant buffer using one or more BufferLoad
   *  instruction. If possible this will emit a vectorized load. */
  ir::SsaDef emitConstantLoad(
          ir::Builder&            builder,
    const Operand&                operand,
          WriteMask               componentMask,
          ir::ScalarType          scalarType);

  void emitImmediateConstant(
          ir::Builder& builder,
          RegisterType registerType,
          uint32_t index,
    const Operand& imm);

  /** Emits debug names for all known constants */
  void emitConstantNames(
          ir::Builder&            builder,
    const ConstantTable&          ctab);

private:

  Converter& m_converter;
  ir::SsaDef m_samplerState = {};

  std::array<SamplerRegister, 32> m_samplers;

  std::array<Constants, uint32_t(ConstantType::eSampler)> m_constants;

  ir::SsaDef dclSampler(ir::Builder& builder, uint32_t samplerIndex);

  ir::SsaDef dclTexture(ir::Builder& builder, SamplerStateType textureType, uint32_t samplerIndex);

  ir::SsaDef emitSampleImageFunction(
          ir::Builder&      builder,
          uint32_t          samplerIndex,
          SamplingConfig    config);

  void emitSampleColorOrDref(
          ir::Builder&      builder,
          ir::SsaDef        texCoord,
          SamplerStateType  textureType,
          uint32_t          samplerIndex,
          ir::SsaDef        descriptor,
          ir::SsaDef        sampler,
          ir::SsaDef        lod,
          ir::SsaDef        lodBias,
          ir::SsaDef        dx,
          ir::SsaDef        dy);

  void emitSampleColorImageType(
          ir::Builder&      builder,
          ir::SsaDef        texCoord,
          SamplerStateType  textureType,
          uint32_t          samplerIndex,
          ir::SsaDef        descriptor,
          ir::SsaDef        sampler,
          ir::SsaDef        lod,
          ir::SsaDef        lodBias,
          ir::SsaDef        dx,
          ir::SsaDef        dy);

  void emitSampleDref(
          ir::Builder&      builder,
          ir::SsaDef        texCoord,
          SamplerStateType  textureType,
          uint32_t          samplerIndex,
          ir::SsaDef        descriptor,
          ir::SsaDef        sampler,
          ir::SsaDef        lod,
          ir::SsaDef        lodBias,
          ir::SsaDef        dx,
          ir::SsaDef        dy);

  ir::SsaDef emitSamplerState(ir::Builder& builder);

  ir::SsaDef loadSamplerState(
          ir::Builder&                  builder,
          uint32_t                      sampler,
          ir::LegacySamplerStateLayout  member);

};


inline ir::ResourceKind resourceKindFromTextureType(TextureType textureType) {
  switch (textureType) {
    case TextureType::eTexture2D:
      return ir::ResourceKind::eImage2D;
    case TextureType::eTextureCube:
      return ir::ResourceKind::eImageCube;
    case TextureType::eTexture3D:
      return ir::ResourceKind::eImage3D;
  }
  return ir::ResourceKind::eBufferRaw;
}

inline SamplerStateType samplerStateTypeFromTextureType(TextureType textureType) {
  switch (textureType) {
    default:
    case TextureType::eTexture2D: return SamplerStateType::eTexture2D;
    case TextureType::eTexture3D: return SamplerStateType::eTexture3D;
    case TextureType::eTextureCube: return SamplerStateType::eTextureCube;
  }
}

inline TextureType textureTypeFromSamplerStateType(SamplerStateType SamplerStateType) {
  switch (SamplerStateType) {
    default:
    case SamplerStateType::eTexture2D: return TextureType::eTexture2D;
    case SamplerStateType::eTexture3D: return TextureType::eTexture3D;
    case SamplerStateType::eTextureCube: return TextureType::eTextureCube;
  }
}

inline SamplerStateType samplerStateTypeFromResourceKind(ir::ResourceKind resourceKind) {
  switch (resourceKind) {
    case ir::ResourceKind::eImage2D:
      return SamplerStateType::eTexture2D;
    case ir::ResourceKind::eImage3D:
      return SamplerStateType::eTexture3D;
    case ir::ResourceKind::eImageCube:
      return SamplerStateType::eTextureCube;
    default:
      dxbc_spv_unreachable();
      return SamplerStateType::eTexture2D;
  }
}

}
