#include "sm3_spec_constants.h"

#include <sstream>

#include "sm3_converter.h"

namespace dxbc_spv::sm3 {

ir::SsaDef SpecializationConstantsMap::getSpecConstDword(ir::Builder& builder, uint32_t idx) {
  if (!m_specConstantIds[idx]) {
    m_specConstantIds[idx] = builder.add(ir::Op::DclSpecConstant(ir::ScalarType::eU32, m_converter.getEntryPoint(), idx, 0u));

    if (m_converter.getOptions().includeDebugNames) {
      std::stringstream namestream;
      namestream << "SpecConst";
      namestream << idx;
      std::string name = namestream.str();
      builder.add(ir::Op::DebugName(m_specConstantIds[idx], name.c_str()));
    }
  }

  return m_specConstantIds[idx];
}


ir::SsaDef SpecializationConstantsMap::getSpecUBODword(ir::Builder& builder, uint32_t idx) const {
  auto member = builder.makeConstant(idx);
  auto descriptor = builder.add(ir::Op::DescriptorLoad(ir::ScalarType::eCbv, m_bufferDef, builder.makeConstant(0u)));
  auto dword = builder.add(ir::Op::BufferLoad(ir::ScalarType::eU32, descriptor, member, 4u));

  return dword;
}


ir::SsaDef SpecializationConstantsMap::getOptimizedBool(ir::Builder& builder) {
  /* The spec constant at MaxNumSpecConstants is set to True
   * when this is an optimized pipeline. */
  auto optimized = getSpecConstDword(builder, m_layout.getOptimizedDwordOffset());
  optimized = builder.add(ir::Op::INe(ir::ScalarType::eBool, optimized, builder.makeConstant(0u)));

  return optimized;
}


ir::SsaDef SpecializationConstantsMap::get(ir::Builder& builder, SpecConstantId id) {
  return get(builder, id, builder.makeConstant(0u), builder.makeConstant(32u));
}


ir::SsaDef SpecializationConstantsMap::get(ir::Builder& builder, SpecConstantId id, ir::SsaDef bitOffset, ir::SsaDef bitCount, ir::SsaDef uboOverride) {
  const auto& layout = m_layout.getSpecConstantLayout(id);

  if (m_converter.getOptions().includeDebugNames) {
    /* Read the spec constant using a named function that's declared lazily. */

    ir::SsaDef& function = m_specConstFunctions[uint32_t(id)];

    if (!function) {
      auto bitOffsetParam = builder.add(ir::Op::DclParam(ir::ScalarType::eU32));
      auto bitCountParam = builder.add(ir::Op::DclParam(ir::ScalarType::eU32));
      builder.add(ir::Op::DebugName(bitOffsetParam, "bitOffset"));
      builder.add(ir::Op::DebugName(bitCountParam, "bitCount"));

      function = builder.addBefore(builder.getCode().first->getDef(),
          ir::Op::Function(ir::ScalarType::eU32)
          .addOperand(bitOffsetParam)
          .addOperand(bitCountParam)
      );

      auto cursor = builder.setCursor(function);

      auto bitOffsetArg = builder.add(ir::Op::ParamLoad(ir::ScalarType::eU32, function, bitOffsetParam));
      auto bitCountArg = builder.add(ir::Op::ParamLoad(ir::ScalarType::eU32, function, bitCountParam));

      auto optimized = getOptimizedBool(builder);

      auto quickValue     = uboOverride ? uboOverride : getSpecUBODword(builder, layout.dwordOffset);
      auto optimizedValue = getSpecConstDword(builder, layout.dwordOffset);

      auto val = builder.add(ir::Op::Select(ir::ScalarType::eU32, optimized, optimizedValue, quickValue));

      auto offset = builder.add(ir::Op::IAdd(ir::ScalarType::eU32, builder.makeConstant(layout.bitOffset), bitOffsetArg));
      auto count = builder.add(ir::Op::UMin(ir::ScalarType::eU32,
        builder.add(ir::Op::ISub(ir::ScalarType::eU32, builder.makeConstant(layout.sizeInBits), bitOffsetArg)),
        bitCountArg
      ));

      auto extractedVal = builder.add(ir::Op::UBitExtract(ir::ScalarType::eU32, val,
        offset, count));

      builder.add(ir::Op::Return(ir::ScalarType::eU32, extractedVal));
      builder.add(ir::Op::FunctionEnd());

      std::stringstream namestream;
      namestream << id;
      std::string name = namestream.str();
      builder.add(ir::Op::DebugName(function, name.c_str()));

      builder.setCursor(cursor);
    }

    return builder.add(ir::Op::FunctionCall(ir::ScalarType::eU32, function)
      .addOperand(bitOffset)
      .addOperand(bitCount));
  } else {
    /* Read the spec constant directly */

    auto optimized = getOptimizedBool(builder);

    auto quickValue     = uboOverride ? uboOverride : getSpecUBODword(builder, layout.dwordOffset);
    auto optimizedValue = getSpecConstDword(builder, layout.dwordOffset);

    auto val = builder.add(ir::Op::Select(ir::ScalarType::eU32, optimized, optimizedValue, quickValue));

    auto offset = builder.add(ir::Op::IAdd(ir::ScalarType::eU32, builder.makeConstant(layout.bitOffset), bitOffset));
    auto count = builder.add(ir::Op::UMin(ir::ScalarType::eU32,
      builder.add(ir::Op::ISub(ir::ScalarType::eU32, builder.makeConstant(layout.sizeInBits), bitOffset)),
      bitCount
    ));

    auto extractedVal = builder.add(ir::Op::UBitExtract(ir::ScalarType::eU32, val,
      offset, count));

    return extractedVal;
  }
}


uint32_t SpecializationConstantsMap::getSamplerSpecConstIndex(ShaderType shaderType, uint32_t perShaderSamplerIndex) {
  return m_layout.getSamplerSpecConstIndex(shaderType, perShaderSamplerIndex);
}


void SpecializationConstantsMap::initialize(ir::Builder& builder) {
  auto arrayType = ir::Type(ir::ScalarType::eU32).addArrayDimension(m_specConstantIds.size());
  m_bufferDef = builder.add(ir::Op::DclCbv(arrayType, m_converter.getEntryPoint(), 0u, FastSpecConstCbvRegIdx, 1u));
  builder.add(ir::Op::DebugName(m_bufferDef, "FastSpecConsts"));
}


std::ostream& operator << (std::ostream& os, SpecConstantId id) {
  switch (id) {
    case SpecConstantId::eSpecSamplerType: return os << "SamplerType";
    case SpecConstantId::eSpecSamplerDepthMode: return os << "SamplerDepthMode";
    case SpecConstantId::eSpecAlphaCompareOp: return os << "AlphaCompareOp";
    case SpecConstantId::eSpecSamplerProjected: return os << "SamplerProjected";
    case SpecConstantId::eSpecSamplerNull: return os << "SamplerNull";
    case SpecConstantId::eSpecAlphaPrecisionBits: return os << "AlphaPrecisionBits";
    case SpecConstantId::eSpecFogEnabled: return os << "FogEnabled";
    case SpecConstantId::eSpecVertexFogMode: return os << "VertexFogMode";
    case SpecConstantId::eSpecPixelFogMode: return os << "PixelFogMode";
    case SpecConstantId::eSpecVertexShaderBools: return os << "VertexShaderBools";
    case SpecConstantId::eSpecPixelShaderBools: return os << "PixelShaderBools";
    case SpecConstantId::eSpecSamplerFetch4: return os << "SamplerFetch4";
    case SpecConstantId::eSpecSamplerDrefClamp: return os << "SamplerDrefClamp";
    case SpecConstantId::eSpecClipPlaneCount: return os << "ClipPlaneCount";
    case SpecConstantId::eSpecPointMode: return os << "PointMode";
    case SpecConstantId::eSpecDrefScaling: return os << "DrefScaling";
  }

  return os << "SpecConstantId(" << uint32_t(id) << ")";
}

}
