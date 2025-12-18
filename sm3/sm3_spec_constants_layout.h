#pragma once

#include "sm3_types.h"

namespace dxbc_spv::sm3 {

enum class SpecConstantId : uint32_t {
  eSpecSamplerType              = 0u,
  eSpecSamplerDepthMode         = 1u,
  eSpecAlphaCompareOp           = 2u,
  eSpecSamplerProjected         = 3u,
  eSpecSamplerNull              = 4u,
  eSpecAlphaPrecisionBits       = 5u,
  eSpecFogEnabled               = 6u,
  eSpecVertexFogMode            = 7u,
  eSpecPixelFogMode             = 8u,
  eSpecVertexShaderBools        = 9u,
  eSpecPixelShaderBools         = 10u,
  eSpecSamplerFetch4            = 11u,
  eSpecSamplerDrefClamp         = 13u,
  eSpecClipPlaneCount           = 14u,
  eSpecPointMode                = 15u,
  eSpecDrefScaling              = 16u,
  /* 12 and 17 - 46 are spec constants used for fixed function shaders */
};

std::ostream& operator << (std::ostream& os, SpecConstantId id);

struct SpecializationConstantBits {
  uint32_t dwordOffset;
  uint32_t bitOffset;
  uint32_t sizeInBits;
};

class SpecializationConstantLayout {

public:

  virtual SpecializationConstantBits getSpecConstantLayout(SpecConstantId id) const = 0;

  virtual uint32_t getOptimizedDwordOffset() const = 0;

  virtual uint32_t getSamplerSpecConstIndex(ShaderType shaderType, uint32_t perShaderSamplerIndex) = 0;

};

}
