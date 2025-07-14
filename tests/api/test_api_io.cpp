#include "test_api_io.h"

namespace dxbc_spv::test_api {

Builder test_io_vs() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eVertex);

  builder.add(Op::Label());

  auto posInDef = builder.add(Op::DclInput(BasicType(ScalarType::eF32, 3u), entryPoint, 0u, 0u));
  builder.add(Op::Semantic(posInDef, 0u, "POSITION"));
  builder.add(Op::DebugName(posInDef, "v0"));

  auto normalInDef = builder.add(Op::DclInput(BasicType(ScalarType::eF32, 3u), entryPoint, 1u, 0u));
  builder.add(Op::Semantic(normalInDef, 0u, "NORMAL"));
  builder.add(Op::DebugName(normalInDef, "v1"));

  auto tangent0InDef = builder.add(Op::DclInput(BasicType(ScalarType::eF32, 3u), entryPoint, 2u, 0u));
  builder.add(Op::Semantic(tangent0InDef, 0u, "TANGENT"));
  builder.add(Op::DebugName(tangent0InDef, "v2"));

  auto tangent1InDef = builder.add(Op::DclInput(BasicType(ScalarType::eF32, 3u), entryPoint, 3u, 0u));
  builder.add(Op::Semantic(tangent1InDef, 1u, "TANGENT"));
  builder.add(Op::DebugName(tangent1InDef, "v3"));

  auto colorInDef = builder.add(Op::DclInput(BasicType(ScalarType::eU32, 1u), entryPoint, 4u, 0u));
  builder.add(Op::Semantic(colorInDef, 1u, "COLOR"));
  builder.add(Op::DebugName(colorInDef, "v4"));

  auto posOutDef = builder.add(Op::DclOutputBuiltIn(BasicType(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition));
  builder.add(Op::Semantic(posOutDef, 0u, "SV_POSITION"));
  builder.add(Op::DebugName(posOutDef, "o0"));

  auto normalOutDef = builder.add(Op::DclOutput(BasicType(ScalarType::eF32, 3u), entryPoint, 1u, 0u));
  builder.add(Op::Semantic(normalOutDef, 0u, "NORMAL"));
  builder.add(Op::DebugName(normalOutDef, "o1_xyz"));

  auto colorOutDef = builder.add(Op::DclOutput(ScalarType::eU32, entryPoint, 1u, 3u));
  builder.add(Op::Semantic(colorOutDef, 0u, "COLOR"));
  builder.add(Op::DebugName(colorOutDef, "o1_w"));

  auto tangent0OutDef = builder.add(Op::DclOutput(BasicType(ScalarType::eF32, 3u), entryPoint, 2u, 0u));
  builder.add(Op::Semantic(tangent0OutDef, 0u, "TANGENT"));
  builder.add(Op::DebugName(tangent0OutDef, "o2"));

  auto tangent1OutDef = builder.add(Op::DclOutput(BasicType(ScalarType::eF32, 3u), entryPoint, 3u, 0u));
  builder.add(Op::Semantic(tangent1OutDef, 1u, "TANGENT"));
  builder.add(Op::DebugName(tangent1OutDef, "o3"));

  builder.add(Op::OutputStore(posOutDef, SsaDef(),
    builder.add(Op::CompositeConstruct(BasicType(ScalarType::eF32, 4u),
      builder.add(Op::InputLoad(ScalarType::eF32, posInDef, builder.makeConstant(0u))),
      builder.add(Op::InputLoad(ScalarType::eF32, posInDef, builder.makeConstant(1u))),
      builder.add(Op::InputLoad(ScalarType::eF32, posInDef, builder.makeConstant(2u))),
      builder.makeConstant(1.0f)))));

  builder.add(Op::OutputStore(normalOutDef, SsaDef(),
    builder.add(Op::InputLoad(BasicType(ScalarType::eF32, 3u), normalInDef, SsaDef()))));

  builder.add(Op::OutputStore(colorOutDef, SsaDef(),
    builder.add(Op::InputLoad(ScalarType::eU32, colorInDef, SsaDef()))));

  auto tangent0 = builder.add(Op::InputLoad(BasicType(ScalarType::eF32, 3u), tangent0InDef, SsaDef()));

  for (uint32_t i = 0u; i < 3u; i++) {
    builder.add(Op::OutputStore(tangent0OutDef, builder.makeConstant(i),
      builder.add(Op::CompositeExtract(ScalarType::eF32, tangent0, builder.makeConstant(i)))));
  }

  for (uint32_t i = 0u; i < 3u; i++) {
    builder.add(Op::OutputStore(tangent1OutDef, builder.makeConstant(i),
      builder.add(Op::InputLoad(ScalarType::eF32, tangent1InDef, builder.makeConstant(i)))));
  }

  builder.add(Op::Return());

  return builder;
}

Builder test_io_vs_vertex_id() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eVertex);

  builder.add(Op::Label());

  auto inDef = builder.add(Op::DclInputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eVertexId));
  builder.add(Op::Semantic(inDef, 0u, "SV_VERTEXID"));
  builder.add(Op::DebugName(inDef, "v0"));

  auto outDef = builder.add(Op::DclOutput(ScalarType::eU32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outDef, 0u, "SHADER_OUT"));
  builder.add(Op::DebugName(outDef, "o0"));

  builder.add(Op::OutputStore(outDef, SsaDef(),
    builder.add(Op::InputLoad(ScalarType::eU32, inDef, SsaDef()))));

  builder.add(Op::Return());
  return builder;
}

Builder test_io_vs_instance_id() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eVertex);

  builder.add(Op::Label());

  auto inDef = builder.add(Op::DclInputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eInstanceId));
  builder.add(Op::Semantic(inDef, 0u, "SV_INSTANCEID"));
  builder.add(Op::DebugName(inDef, "v0"));

  auto outDef = builder.add(Op::DclOutput(ScalarType::eU32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outDef, 0u, "SHADER_OUT"));
  builder.add(Op::DebugName(outDef, "o0"));

  builder.add(Op::OutputStore(outDef, SsaDef(),
    builder.add(Op::InputLoad(ScalarType::eU32, inDef, SsaDef()))));

  builder.add(Op::Return());
  return builder;
}

Builder test_io_vs_clip_dist() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eVertex);

  builder.add(Op::Label());

  auto outDef = builder.add(Op::DclOutputBuiltIn(
    Type(ScalarType::eF32).addArrayDimension(6u), entryPoint, BuiltIn::eClipDistance));
  builder.add(Op::Semantic(outDef, 0u, "SV_CLIPDISTANCE"));
  builder.add(Op::DebugName(outDef, "oClip"));

  for (uint32_t i = 0u; i < 6u; i++) {
    builder.add(Op::OutputStore(outDef, builder.makeConstant(i),
      builder.makeConstant(float(i) - 2.5f)));
  }

  builder.add(Op::Return());
  return builder;
}

Builder test_io_vs_cull_dist() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eVertex);

  builder.add(Op::Label());

  auto outDef = builder.add(Op::DclOutputBuiltIn(
    Type(ScalarType::eF32).addArrayDimension(2u), entryPoint, BuiltIn::eCullDistance));
  builder.add(Op::Semantic(outDef, 0u, "SV_CULLDISTANCE"));
  builder.add(Op::DebugName(outDef, "oCull"));

  builder.add(Op::OutputStore(outDef, builder.makeConstant(0u), builder.makeConstant(0.7f)));
  builder.add(Op::OutputStore(outDef, builder.makeConstant(1u), builder.makeConstant(0.1f)));

  builder.add(Op::Return());
  return builder;
}

Builder test_io_vs_clip_cull_dist() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eVertex);

  builder.add(Op::Label());

  auto clipDef = builder.add(Op::DclOutputBuiltIn(
    Type(ScalarType::eF32).addArrayDimension(7u), entryPoint, BuiltIn::eClipDistance));
  builder.add(Op::Semantic(clipDef, 0u, "SV_CLIPDISTANCE"));
  builder.add(Op::DebugName(clipDef, "oClip"));

  auto cullDef = builder.add(Op::DclOutputBuiltIn(
    Type(ScalarType::eF32).addArrayDimension(1u), entryPoint, BuiltIn::eCullDistance));
  builder.add(Op::Semantic(cullDef, 0u, "SV_CULLDISTANCE"));
  builder.add(Op::DebugName(cullDef, "oCull"));

  for (uint32_t i = 0u; i < 7u; i++)
    builder.add(Op::OutputStore(clipDef, builder.makeConstant(i), builder.makeConstant(float(i) - 2.5f)));

  builder.add(Op::OutputStore(cullDef, builder.makeConstant(0u), builder.makeConstant(-2.0f)));

  builder.add(Op::Return());
  return builder;
}

Builder test_io_vs_layer() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eVertex);

  builder.add(Op::Label());

  auto inDef = builder.add(Op::DclInputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eInstanceId));
  builder.add(Op::Semantic(inDef, 0u, "SV_INSTANCEID"));
  builder.add(Op::DebugName(inDef, "v0"));

  auto outDef = builder.add(Op::DclOutputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eLayerIndex));
  builder.add(Op::Semantic(outDef, 0u, "SV_RenderTargetArrayIndex"));
  builder.add(Op::DebugName(outDef, "o0"));

  builder.add(Op::OutputStore(outDef, SsaDef(),
    builder.add(Op::InputLoad(ScalarType::eU32, inDef, SsaDef()))));

  builder.add(Op::Return());
  return builder;
}

Builder test_io_vs_viewport() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eVertex);

  builder.add(Op::Label());

  auto inDef = builder.add(Op::DclInputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eInstanceId));
  builder.add(Op::Semantic(inDef, 0u, "SV_INSTANCEID"));
  builder.add(Op::DebugName(inDef, "v0"));

  auto outDef = builder.add(Op::DclOutputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eViewportIndex));
  builder.add(Op::Semantic(outDef, 0u, "SV_ViewportArrayIndex"));
  builder.add(Op::DebugName(outDef, "o0"));

  builder.add(Op::OutputStore(outDef, SsaDef(),
    builder.add(Op::InputLoad(ScalarType::eU32, inDef, SsaDef()))));

  builder.add(Op::Return());
  return builder;
}

Builder test_io_ps_interpolate_centroid() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());

  auto in0Def = builder.add(Op::DclInput(ScalarType::eF32, entryPoint, 0u, 0u, InterpolationMode::eNoPerspective));
  builder.add(Op::Semantic(in0Def, 0u, "IN_SCALAR"));

  auto in1Def = builder.add(Op::DclInput(BasicType(ScalarType::eF32, 3u), entryPoint, 1u, 0u, InterpolationModes()));
  builder.add(Op::Semantic(in1Def, 0u, "IN_VECTOR"));

  auto out0Def = builder.add(Op::DclOutput(ScalarType::eF32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(out0Def, 0u, "SV_TARGET"));

  auto out1Def = builder.add(Op::DclOutput(BasicType(ScalarType::eF32, 3u), entryPoint, 1u, 0u));
  builder.add(Op::Semantic(out1Def, 1u, "SV_TARGET"));

  builder.add(Op::OutputStore(out0Def, SsaDef(),
    builder.add(Op::InterpolateAtCentroid(ScalarType::eF32, in0Def))));

  builder.add(Op::OutputStore(out1Def, SsaDef(),
    builder.add(Op::InterpolateAtCentroid(BasicType(ScalarType::eF32, 3u), in1Def))));

  builder.add(Op::Return());
  return builder;
}

Builder test_io_ps_interpolate_sample() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());

  auto sampleIdDef = builder.add(Op::DclInputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eSampleId, InterpolationMode::eFlat));
  builder.add(Op::Semantic(sampleIdDef, 0u, "SV_SAMPLEINDEX"));

  auto in0Def = builder.add(Op::DclInput(ScalarType::eF32, entryPoint, 0u, 0u, InterpolationMode::eNoPerspective));
  builder.add(Op::Semantic(in0Def, 0u, "IN_SCALAR"));

  auto in1Def = builder.add(Op::DclInput(BasicType(ScalarType::eF32, 3u), entryPoint, 1u, 0u, InterpolationModes()));
  builder.add(Op::Semantic(in1Def, 0u, "IN_VECTOR"));

  auto out0Def = builder.add(Op::DclOutput(ScalarType::eF32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(out0Def, 0u, "SV_TARGET"));

  auto out1Def = builder.add(Op::DclOutput(BasicType(ScalarType::eF32, 3u), entryPoint, 1u, 0u));
  builder.add(Op::Semantic(out1Def, 1u, "SV_TARGET"));

  builder.add(Op::OutputStore(out0Def, SsaDef(),
    builder.add(Op::InterpolateAtSample(ScalarType::eF32, in0Def,
      builder.add(Op::InputLoad(ScalarType::eU32, sampleIdDef, SsaDef()))))));

  builder.add(Op::OutputStore(out1Def, SsaDef(),
    builder.add(Op::InterpolateAtSample(BasicType(ScalarType::eF32, 3u), in1Def,
      builder.add(Op::InputLoad(ScalarType::eU32, sampleIdDef, SsaDef()))))));

  builder.add(Op::Return());
  return builder;
}

Builder test_io_ps_interpolate_offset() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());

  auto offsetInputDef = builder.add(Op::DclInput(BasicType(ScalarType::eF32, 2u), entryPoint, 2u, 0u, InterpolationModes()));
  builder.add(Op::Semantic(offsetInputDef, 0u, "OFFSET"));

  auto in0Def = builder.add(Op::DclInput(ScalarType::eF32, entryPoint, 0u, 0u, InterpolationMode::eNoPerspective));
  builder.add(Op::Semantic(in0Def, 0u, "IN_SCALAR"));

  auto in1Def = builder.add(Op::DclInput(BasicType(ScalarType::eF32, 3u), entryPoint, 1u, 0u, InterpolationModes()));
  builder.add(Op::Semantic(in1Def, 0u, "IN_VECTOR"));

  auto out0Def = builder.add(Op::DclOutput(ScalarType::eF32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(out0Def, 0u, "SV_TARGET"));

  auto out1Def = builder.add(Op::DclOutput(BasicType(ScalarType::eF32, 3u), entryPoint, 1u, 0u));
  builder.add(Op::Semantic(out1Def, 1u, "SV_TARGET"));

  auto offsetDef = builder.add(Op::InputLoad(BasicType(ScalarType::eF32, 2u), offsetInputDef, SsaDef()));

  builder.add(Op::OutputStore(out0Def, SsaDef(),
    builder.add(Op::InterpolateAtOffset(ScalarType::eF32, in0Def, offsetDef))));

  builder.add(Op::OutputStore(out1Def, SsaDef(),
    builder.add(Op::InterpolateAtOffset(BasicType(ScalarType::eF32, 3u), in1Def, offsetDef))));

  builder.add(Op::Return());
  return builder;
}

}
