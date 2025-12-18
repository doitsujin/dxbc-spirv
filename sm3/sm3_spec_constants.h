#pragma once

#include <cstdint>

#include "../ir/ir.h"
#include "../ir/ir_builder.h"

#include "sm3_spec_constants_layout.h"

namespace dxbc_spv::sm3 {

class Converter;

class SpecializationConstantsMap {

public:

  explicit SpecializationConstantsMap(Converter& converter, SpecializationConstantLayout& layout)
    : m_converter(converter), m_layout(layout) { }

  ir::SsaDef get(ir::Builder& builder, SpecConstantId id);

  ir::SsaDef get(ir::Builder& builder, SpecConstantId id, ir::SsaDef bitOffset, ir::SsaDef bitCount, ir::SsaDef uboOverride = { });

  uint32_t getSamplerSpecConstIndex(ShaderType shaderType, uint32_t perShaderSamplerIndex);

  void initialize(ir::Builder& builder);

  void setInsertCursor(ir::SsaDef cursor) {
    m_functionInsertPoint = cursor;
  }

private:

  ir::SsaDef getSpecConstDword(ir::Builder& builder, uint32_t idx);

  ir::SsaDef getSpecUBODword(ir::Builder& builder, uint32_t idx) const;

  ir::SsaDef getOptimizedBool(ir::Builder& builder);

  Converter& m_converter;

  std::array<ir::SsaDef, 32u> m_specConstantIds = { };

  std::array<ir::SsaDef, uint32_t(SpecConstantId::eSpecDrefScaling) + 1u> m_specConstFunctions = { };

  ir::SsaDef m_functionInsertPoint = { };

  ir::SsaDef m_bufferDef = { };

  SpecializationConstantLayout& m_layout;

};

}
