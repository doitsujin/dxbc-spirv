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

  auto in2Def = builder.add(Op::DclInput(BasicType(ScalarType::eF32, 3u), entryPoint, 2u, 0u, InterpolationMode::eSample));
  builder.add(Op::Semantic(in2Def, 1u, "IN_VECTOR"));

  auto out0Def = builder.add(Op::DclOutput(ScalarType::eF32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(out0Def, 0u, "SV_TARGET"));

  auto out1Def = builder.add(Op::DclOutput(BasicType(ScalarType::eF32, 3u), entryPoint, 1u, 0u));
  builder.add(Op::Semantic(out1Def, 1u, "SV_TARGET"));

  auto out2Def = builder.add(Op::DclOutput(ScalarType::eF32, entryPoint, 2u, 0u));
  builder.add(Op::Semantic(out2Def, 2u, "SV_TARGET"));

  builder.add(Op::OutputStore(out0Def, SsaDef(),
    builder.add(Op::InterpolateAtCentroid(ScalarType::eF32, in0Def, SsaDef()))));

  builder.add(Op::OutputStore(out1Def, SsaDef(),
    builder.add(Op::InterpolateAtCentroid(BasicType(ScalarType::eF32, 3u), in1Def, SsaDef()))));

  builder.add(Op::OutputStore(out2Def, SsaDef(),
    builder.add(Op::InterpolateAtCentroid(ScalarType::eF32, in2Def, builder.makeConstant(1u)))));

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

  auto in2Def = builder.add(Op::DclInput(BasicType(ScalarType::eF32, 3u), entryPoint, 2u, 0u, InterpolationMode::eCentroid));
  builder.add(Op::Semantic(in2Def, 1u, "IN_VECTOR"));

  auto out0Def = builder.add(Op::DclOutput(ScalarType::eF32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(out0Def, 0u, "SV_TARGET"));

  auto out1Def = builder.add(Op::DclOutput(BasicType(ScalarType::eF32, 3u), entryPoint, 1u, 0u));
  builder.add(Op::Semantic(out1Def, 1u, "SV_TARGET"));

  auto out2Def = builder.add(Op::DclOutput(ScalarType::eF32, entryPoint, 2u, 0u));
  builder.add(Op::Semantic(out2Def, 2u, "SV_TARGET"));

  builder.add(Op::OutputStore(out0Def, SsaDef(),
    builder.add(Op::InterpolateAtSample(ScalarType::eF32, in0Def, SsaDef(),
      builder.add(Op::InputLoad(ScalarType::eU32, sampleIdDef, SsaDef()))))));

  builder.add(Op::OutputStore(out1Def, SsaDef(),
    builder.add(Op::InterpolateAtSample(BasicType(ScalarType::eF32, 3u), in1Def, SsaDef(),
      builder.add(Op::InputLoad(ScalarType::eU32, sampleIdDef, SsaDef()))))));

  builder.add(Op::OutputStore(out2Def, SsaDef(),
    builder.add(Op::InterpolateAtSample(ScalarType::eF32, in2Def, builder.makeConstant(1u),
      builder.add(Op::InputLoad(ScalarType::eU32, sampleIdDef, SsaDef()))))));

  builder.add(Op::Return());
  return builder;
}

Builder test_io_ps_interpolate_offset() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());

  auto offsetInputDef = builder.add(Op::DclInput(BasicType(ScalarType::eF32, 2u), entryPoint, 3u, 0u, InterpolationModes()));
  builder.add(Op::Semantic(offsetInputDef, 0u, "OFFSET"));

  auto in0Def = builder.add(Op::DclInput(ScalarType::eF32, entryPoint, 0u, 0u, InterpolationMode::eNoPerspective));
  builder.add(Op::Semantic(in0Def, 0u, "IN_SCALAR"));

  auto in1Def = builder.add(Op::DclInput(BasicType(ScalarType::eF32, 3u), entryPoint, 1u, 0u, InterpolationModes()));
  builder.add(Op::Semantic(in1Def, 0u, "IN_VECTOR"));

  auto in2Def = builder.add(Op::DclInput(BasicType(ScalarType::eF32, 3u), entryPoint, 2u, 0u, InterpolationMode::eCentroid));
  builder.add(Op::Semantic(in2Def, 1u, "IN_VECTOR"));

  auto out0Def = builder.add(Op::DclOutput(ScalarType::eF32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(out0Def, 0u, "SV_TARGET"));

  auto out1Def = builder.add(Op::DclOutput(BasicType(ScalarType::eF32, 3u), entryPoint, 1u, 0u));
  builder.add(Op::Semantic(out1Def, 1u, "SV_TARGET"));

  auto out2Def = builder.add(Op::DclOutput(ScalarType::eF32, entryPoint, 2u, 0u));
  builder.add(Op::Semantic(out2Def, 2u, "SV_TARGET"));

  auto offsetDef = builder.add(Op::InputLoad(BasicType(ScalarType::eF32, 2u), offsetInputDef, SsaDef()));

  builder.add(Op::OutputStore(out0Def, SsaDef(),
    builder.add(Op::InterpolateAtOffset(ScalarType::eF32, in0Def, SsaDef(), offsetDef))));

  builder.add(Op::OutputStore(out1Def, SsaDef(),
    builder.add(Op::InterpolateAtOffset(BasicType(ScalarType::eF32, 3u), in1Def, SsaDef(), offsetDef))));

  builder.add(Op::OutputStore(out2Def, SsaDef(),
    builder.add(Op::InterpolateAtOffset(ScalarType::eF32, in2Def, builder.makeConstant(1u), offsetDef))));

  builder.add(Op::Return());
  return builder;
}

Builder make_test_io_ps_export_depth(OpCode constraint) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  if (constraint != OpCode::eUnknown)
    builder.add(Op(constraint, Type()).addOperand(entryPoint));

  builder.add(Op::Label());

  auto posDef = builder.add(Op::DclInputBuiltIn(Type(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition, InterpolationModes()));
  builder.add(Op::Semantic(posDef, 0u, "SV_POSITION"));

  auto inputDef = builder.add(Op::DclInput(ScalarType::eF32, entryPoint, 1u, 0u, InterpolationModes()));
  builder.add(Op::Semantic(inputDef, 0u, "DELTA"));

  auto depthDef = builder.add(Op::DclOutputBuiltIn(ScalarType::eF32, entryPoint, BuiltIn::eDepth));
  builder.add(Op::Semantic(depthDef, 0u, "SV_DEPTH"));

  builder.add(Op::OutputStore(depthDef, SsaDef(),
    builder.add(Op::FAdd(ScalarType::eF32,
      builder.add(Op::InputLoad(ScalarType::eF32, inputDef, SsaDef())),
      builder.add(Op::InputLoad(ScalarType::eF32, posDef, builder.makeConstant(2u)))))));

  builder.add(Op::Return());
  return builder;
}

Builder test_io_ps_export_depth() {
  return make_test_io_ps_export_depth(OpCode::eUnknown);
}

Builder test_io_ps_export_depth_less() {
  return make_test_io_ps_export_depth(OpCode::eSetPsDepthLessEqual);
}

Builder test_io_ps_export_depth_greater() {
  return make_test_io_ps_export_depth(OpCode::eSetPsDepthGreaterEqual);
}

Builder test_io_ps_export_stencil() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());

  auto inputDef = builder.add(Op::DclInput(ScalarType::eU32, entryPoint, 0u, 0u, InterpolationMode::eFlat));
  builder.add(Op::Semantic(inputDef, 0u, "STENCIL_REF"));

  auto stencilDef = builder.add(Op::DclOutputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eStencilRef));
  builder.add(Op::Semantic(stencilDef, 0u, "SV_STENCILREF"));

  builder.add(Op::OutputStore(stencilDef, SsaDef(),
    builder.add(Op::InputLoad(ScalarType::eU32, inputDef, SsaDef()))));

  builder.add(Op::Return());
  return builder;
}


Builder test_io_ps_builtins() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());

  auto frontFaceDef = builder.add(Op::DclInputBuiltIn(ScalarType::eBool, entryPoint, BuiltIn::eIsFrontFace, InterpolationMode::eFlat));
  builder.add(Op::Semantic(frontFaceDef, 0u, "SV_ISFRONTFACE"));

  auto sampleMaskInDef = builder.add(Op::DclInputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eSampleMask, InterpolationMode::eFlat));
  builder.add(Op::Semantic(sampleMaskInDef, 0u, "SV_COVERAGE"));

  auto sampleIdInDef = builder.add(Op::DclInputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eSampleId, InterpolationMode::eFlat));
  builder.add(Op::Semantic(sampleIdInDef, 0u, "SV_SAMPLEINDEX"));

  auto sampleMaskOutDef = builder.add(Op::DclOutputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eSampleMask));
  builder.add(Op::Semantic(sampleMaskOutDef, 0u, "SV_COVERAGE"));

  auto sampleMask = builder.add(Op::InputLoad(ScalarType::eU32, sampleMaskInDef, SsaDef()));
  auto sampleId = builder.add(Op::InputLoad(ScalarType::eU32, sampleIdInDef, SsaDef()));
  sampleMask = builder.add(Op::UBitExtract(ScalarType::eU32, sampleMask, builder.makeConstant(0u), sampleId));

  auto cond = builder.add(Op::InputLoad(ScalarType::eBool, frontFaceDef, SsaDef()));
  sampleMask = builder.add(Op::Select(ScalarType::eU32, cond, sampleMask, builder.makeConstant(0u)));

  builder.add(Op::OutputStore(sampleMaskOutDef, SsaDef(), sampleMask));
  builder.add(Op::Return());
  return builder;
}


Builder test_io_ps_fully_covered() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());

  auto fullyCoveredDef = builder.add(Op::DclInputBuiltIn(ScalarType::eBool, entryPoint, BuiltIn::eIsFullyCovered, InterpolationMode::eFlat));
  builder.add(Op::Semantic(fullyCoveredDef, 0u, "SV_INNERCOVERAGE"));

  auto outputDef = builder.add(Op::DclOutput(ScalarType::eF32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outputDef, 0u, "SV_TARGET"));

  builder.add(Op::OutputStore(outputDef, SsaDef(), builder.add(Op::Select(ScalarType::eF32,
    builder.add(Op::InputLoad(ScalarType::eBool, fullyCoveredDef, SsaDef())),
    builder.makeConstant(1.0f), builder.makeConstant(0.0f)))));

  builder.add(Op::Return());
  return builder;
}


Builder make_test_io_gs_basic(ir::PrimitiveType inType, ir::PrimitiveType outType) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eGeometry);
  auto vertexCount = primitiveVertexCount(inType);

  builder.add(Op::SetGsInstances(entryPoint, 1u));
  builder.add(Op::SetGsInputPrimitive(entryPoint, inType));
  builder.add(Op::SetGsOutputPrimitive(entryPoint, outType, 0x1u));
  builder.add(Op::SetGsOutputVertices(entryPoint, vertexCount));

  auto baseBlock = builder.add(Op::Label());

  auto posInDef = builder.add(Op::DclInputBuiltIn(
    Type(ScalarType::eF32, 4u).addArrayDimension(vertexCount),
    entryPoint, BuiltIn::ePosition));
  builder.add(Op::Semantic(posInDef, 0u, "SV_POSITION"));

  auto posOutDef = builder.add(Op::DclOutputBuiltIn(
    Type(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition, 0u));
  builder.add(Op::Semantic(posOutDef, 0u, "SV_POSITION"));

  auto clipInDef = builder.add(Op::DclInputBuiltIn(
    Type(ScalarType::eF32).addArrayDimensions(2u, vertexCount),
    entryPoint, BuiltIn::eClipDistance));
  builder.add(Op::Semantic(clipInDef, 0u, "SV_CLIPDISTANCE"));

  auto clipOutDef = builder.add(Op::DclOutputBuiltIn(
    Type(ScalarType::eF32).addArrayDimension(2u), entryPoint, BuiltIn::eClipDistance, 0u));
  builder.add(Op::Semantic(clipOutDef, 0u, "SV_CLIPDISTANCE"));

  auto coordInDef = builder.add(Op::DclInput(
    Type(ScalarType::eF32, 2u).addArrayDimension(vertexCount), entryPoint, 2u, 0u));
    builder.add(Op::Semantic(coordInDef, 0u, "TEXCOORD"));

  auto coordOutDef = builder.add(Op::DclOutput(Type(ScalarType::eF32, 2u), entryPoint, 2u, 0u, 0u));
  builder.add(Op::Semantic(coordOutDef, 0u, "TEXCOORD"));

  auto normalInDef = builder.add(Op::DclInput(
    Type(ScalarType::eF32, 3u).addArrayDimension(vertexCount), entryPoint, 3u, 0u));
  builder.add(Op::Semantic(normalInDef, 0u, "NORMAL"));

  auto normalOutDef = builder.add(Op::DclOutput(Type(ScalarType::eF32, 3u), entryPoint, 3u, 0u, 0u));
  builder.add(Op::Semantic(normalOutDef, 0u, "NORMAL"));

  auto loopCounterInit = builder.makeConstant(0u);

  auto loopHeaderLabel = builder.add(Op::Label());
  builder.addBefore(loopHeaderLabel, Op::Branch(loopHeaderLabel));

  auto loopCounterPhi = builder.add(Op::Phi(ScalarType::eU32));

  auto loopBodyLabel = builder.add(Op::Label());
  builder.addBefore(loopBodyLabel, Op::Branch(loopBodyLabel));

  builder.add(Op::OutputStore(posOutDef, SsaDef(),
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 4u), posInDef, loopCounterPhi))));
  builder.add(Op::OutputStore(normalOutDef, SsaDef(),
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 3u), normalInDef, loopCounterPhi))));

  for (uint32_t i = 0u; i < 2u; i++) {
    builder.add(Op::OutputStore(clipOutDef, builder.makeConstant(i),
      builder.add(Op::InputLoad(Type(ScalarType::eF32), clipInDef,
        builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 2u), loopCounterPhi, builder.makeConstant(i)))))));
  }

  for (uint32_t i = 0u; i < 2u; i++) {
    builder.add(Op::OutputStore(coordOutDef, builder.makeConstant(i),
      builder.add(Op::InputLoad(Type(ScalarType::eF32), coordInDef,
        builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 2u), loopCounterPhi, builder.makeConstant(i)))))));
  }

  builder.add(Op::EmitVertex(0u));

  auto loopContinueLabel = builder.add(Op::Label());
  builder.addBefore(loopContinueLabel, Op::Branch(loopContinueLabel));

  auto loopCounterValue = builder.add(Op::IAdd(ScalarType::eU32, loopCounterPhi, builder.makeConstant(1u)));
  auto loopCond = builder.add(Op::ULt(ScalarType::eBool, loopCounterValue, builder.makeConstant(vertexCount)));

  auto loopEndLabel = builder.add(Op::Label());
  builder.addBefore(loopEndLabel, Op::BranchConditional(loopCond, loopHeaderLabel, loopEndLabel));
  builder.rewriteOp(loopHeaderLabel, Op::LabelLoop(loopEndLabel, loopContinueLabel));
  builder.rewriteOp(loopCounterPhi, Op::Phi(ScalarType::eU32)
    .addPhi(baseBlock, loopCounterInit)
    .addPhi(loopContinueLabel, loopCounterValue));

  builder.add(Op::EmitPrimitive(0u));
  builder.add(Op::Return());
  return builder;
}

Builder test_io_gs_basic_point() {
  return make_test_io_gs_basic(ir::PrimitiveType::ePoints, ir::PrimitiveType::ePoints);
}

Builder test_io_gs_basic_line() {
  return make_test_io_gs_basic(ir::PrimitiveType::eLines, ir::PrimitiveType::eLines);
}

Builder test_io_gs_basic_line_adj() {
  return make_test_io_gs_basic(ir::PrimitiveType::eLinesAdj, ir::PrimitiveType::eLines);
}

Builder test_io_gs_basic_triangle() {
  return make_test_io_gs_basic(ir::PrimitiveType::eTriangles, ir::PrimitiveType::eTriangles);
}

Builder test_io_gs_basic_triangle_adj() {
  return make_test_io_gs_basic(ir::PrimitiveType::eTrianglesAdj, ir::PrimitiveType::eTriangles);
}

Builder test_io_gs_instanced() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eGeometry);

  builder.add(Op::SetGsInstances(entryPoint, 12u));
  builder.add(Op::SetGsInputPrimitive(entryPoint, PrimitiveType::ePoints));
  builder.add(Op::SetGsOutputPrimitive(entryPoint, PrimitiveType::ePoints, 0x1u));
  builder.add(Op::SetGsOutputVertices(entryPoint, 1u));

  builder.add(Op::Label());

  auto posOutDef = builder.add(Op::DclOutputBuiltIn(
    Type(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition, 0u));
  builder.add(Op::Semantic(posOutDef, 0u, "SV_POSITION"));

  auto layerOutDef = builder.add(Op::DclOutputBuiltIn(
    ScalarType::eU32, entryPoint, BuiltIn::eLayerIndex, 0u));
  auto viewportOutDef = builder.add(Op::DclOutputBuiltIn(
    ScalarType::eU32, entryPoint, BuiltIn::eViewportIndex, 0u));

  auto primitiveIdInDef = builder.add(Op::DclInputBuiltIn(
    ScalarType::eU32, entryPoint, BuiltIn::ePrimitiveId));
  auto primitiveIdOutDef = builder.add(Op::DclOutputBuiltIn(
    ScalarType::eU32, entryPoint, BuiltIn::ePrimitiveId, 0u));

  auto instanceIdDef = builder.add(Op::DclInputBuiltIn(
    ScalarType::eU32, entryPoint, BuiltIn::eGsInstanceId));

  builder.add(Op::Semantic(layerOutDef, 0u, "SV_RenderTargetArrayIndex"));
  builder.add(Op::Semantic(viewportOutDef, 0u, "SV_ViewportArrayIndex"));
  builder.add(Op::Semantic(primitiveIdInDef, 0u, "SV_PrimitiveId"));
  builder.add(Op::Semantic(primitiveIdOutDef, 0u, "SV_PrimitiveId"));
  builder.add(Op::Semantic(instanceIdDef, 0u, "SV_GSInstanceID"));

  auto instanceId = builder.add(Op::InputLoad(ScalarType::eU32, instanceIdDef, SsaDef()));
  auto primitiveId = builder.add(Op::InputLoad(ScalarType::eU32, primitiveIdInDef, SsaDef()));

  primitiveId = builder.add(Op::IAdd(ScalarType::eU32, instanceId,
    builder.add(Op::IMul(ScalarType::eU32, primitiveId, builder.makeConstant(12u)))));
  builder.add(Op::OutputStore(primitiveIdOutDef, SsaDef(), primitiveId));

  builder.add(Op::OutputStore(layerOutDef, SsaDef(),
    builder.add(Op::UShr(ScalarType::eU32, instanceId, builder.makeConstant(1u)))));
  builder.add(Op::OutputStore(viewportOutDef, SsaDef(),
    builder.add(Op::IAnd(ScalarType::eU32, instanceId, builder.makeConstant(1u)))));

  builder.add(Op::OutputStore(posOutDef, SsaDef(), builder.makeConstant(1.0f, 1.0f, 1.0f, 1.0f)));

  builder.add(Op::EmitVertex(0u));
  builder.add(Op::EmitPrimitive(0u));
  builder.add(Op::Return());
  return builder;
}

Builder test_io_gs_xfb() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eGeometry);

  builder.add(Op::SetGsInstances(entryPoint, 1u));
  builder.add(Op::SetGsInputPrimitive(entryPoint, PrimitiveType::ePoints));
  builder.add(Op::SetGsOutputPrimitive(entryPoint, PrimitiveType::ePoints, 0x1u));
  builder.add(Op::SetGsOutputVertices(entryPoint, 1u));

  builder.add(Op::Label());

  auto bufAattr0def = builder.add(Op::DclOutput(Type(ScalarType::eF32, 3u), entryPoint, 0u, 0u, 0u));
  builder.add(Op::Semantic(bufAattr0def, 0u, "BUFFER_A_ATTR"));
  builder.add(Op::DclXfb(bufAattr0def, 0u, 16u, 0u));

  auto bufAattr1def = builder.add(Op::DclOutput(Type(ScalarType::eU32, 1u), entryPoint, 0u, 3u, 0u));
  builder.add(Op::Semantic(bufAattr1def, 1u, "BUFFER_A_ATTR"));
  builder.add(Op::DclXfb(bufAattr1def, 0u, 16u, 12u));

  auto bufBattr0def = builder.add(Op::DclOutput(Type(ScalarType::eI32, 2u), entryPoint, 1u, 2u, 0u));
  builder.add(Op::Semantic(bufBattr0def, 0u, "BUFFER_B_ATTR"));
  builder.add(Op::DclXfb(bufBattr0def, 1u, 8u, 0u));

  builder.add(Op::OutputStore(bufAattr0def, SsaDef(), builder.makeConstant(1.0f, 2.0f, 3.0f)));
  builder.add(Op::OutputStore(bufAattr1def, SsaDef(), builder.makeConstant(4u)));

  builder.add(Op::EmitVertex(0u));
  builder.add(Op::EmitPrimitive(0u));

  builder.add(Op::OutputStore(bufBattr0def, SsaDef(), builder.makeConstant(5, 6)));
  builder.add(Op::EmitVertex(1u));
  builder.add(Op::EmitPrimitive(1u));

  builder.add(Op::Return());
  return builder;
}

Builder test_io_gs_multi_stream_xfb_raster_0() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eGeometry);

  builder.add(Op::SetGsInstances(entryPoint, 1u));
  builder.add(Op::SetGsInputPrimitive(entryPoint, PrimitiveType::ePoints));
  builder.add(Op::SetGsOutputPrimitive(entryPoint, PrimitiveType::ePoints, 0x3u));
  builder.add(Op::SetGsOutputVertices(entryPoint, 1u));

  builder.add(Op::Label());

  auto bufAattr0def = builder.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition, 0u));
  builder.add(Op::Semantic(bufAattr0def, 0u, "SV_POSITION"));
  builder.add(Op::DclXfb(bufAattr0def, 0u, 16u, 0u));

  auto bufAattr1def = builder.add(Op::DclOutput(Type(ScalarType::eU32, 1u), entryPoint, 1u, 0u, 0u));
  builder.add(Op::Semantic(bufAattr1def, 1u, "BUFFER_A_ATTR"));

  auto bufBattr0def = builder.add(Op::DclOutput(Type(ScalarType::eI32, 2u), entryPoint, 2u, 0u, 1u));
  builder.add(Op::Semantic(bufBattr0def, 0u, "BUFFER_B_ATTR"));
  builder.add(Op::DclXfb(bufBattr0def, 1u, 8u, 0u));

  builder.add(Op::OutputStore(bufAattr0def, SsaDef(), builder.makeConstant(1.0f, 2.0f, 3.0f, 4.0f)));
  builder.add(Op::OutputStore(bufAattr1def, SsaDef(), builder.makeConstant(4u)));

  builder.add(Op::EmitVertex(0u));
  builder.add(Op::EmitPrimitive(0u));

  builder.add(Op::OutputStore(bufBattr0def, SsaDef(), builder.makeConstant(5, 6)));
  builder.add(Op::EmitVertex(1u));
  builder.add(Op::EmitPrimitive(1u));

  builder.add(Op::Return());
  return builder;
}

Builder test_io_gs_multi_stream_xfb_raster_1() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eGeometry);

  builder.add(Op::SetGsInstances(entryPoint, 1u));
  builder.add(Op::SetGsInputPrimitive(entryPoint, PrimitiveType::ePoints));
  builder.add(Op::SetGsOutputPrimitive(entryPoint, PrimitiveType::ePoints, 0x3u));
  builder.add(Op::SetGsOutputVertices(entryPoint, 1u));

  builder.add(Op::Label());

  auto bufAattr0def = builder.add(Op::DclOutput(Type(ScalarType::eI32, 2u), entryPoint, 0u, 0u, 0u));
  builder.add(Op::Semantic(bufAattr0def, 0u, "BUFFER_A_ATTR"));
  builder.add(Op::DclXfb(bufAattr0def, 0u, 8u, 0u));

  auto bufAattr1def = builder.add(Op::DclOutput(Type(ScalarType::eU32, 1u), entryPoint, 1u, 0u, 0u));
  builder.add(Op::Semantic(bufAattr1def, 1u, "BUFFER_A_ATTR"));
  builder.add(Op::DclXfb(bufAattr1def, 0u, 8u, 0u));

  auto bufBattr0def = builder.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition, 1u));
  builder.add(Op::Semantic(bufBattr0def, 0u, "SV_POSITION"));
  builder.add(Op::DclXfb(bufBattr0def, 1u, 16u, 0u));

  builder.add(Op::OutputStore(bufAattr0def, SsaDef(), builder.makeConstant(1, 2)));
  builder.add(Op::OutputStore(bufAattr1def, SsaDef(), builder.makeConstant(3u)));

  builder.add(Op::EmitVertex(0u));
  builder.add(Op::EmitPrimitive(0u));

  builder.add(Op::OutputStore(bufBattr0def, SsaDef(), builder.makeConstant(4.0f, 5.0f, 6.0f, 7.0f)));
  builder.add(Op::EmitVertex(1u));
  builder.add(Op::EmitPrimitive(1u));

  builder.add(Op::Return());
  return builder;
}

Builder make_test_io_hs(PrimitiveType primType, TessWindingOrder winding, TessPartitioning partitioning) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eHull);

  auto domain = primType == PrimitiveType::eLines
    ? PrimitiveType::eLines
    : PrimitiveType::eTriangles;

  builder.add(Op::SetTessPrimitive(entryPoint, primType, winding, partitioning));
  builder.add(Op::SetTessControlPoints(entryPoint, 4u, 4u));
  builder.add(Op::SetTessDomain(entryPoint, domain));

  auto cpDef = ir::SsaDef(builder.getOp(entryPoint).getOperand(0u));
  auto pcDef = ir::SsaDef(builder.getOp(entryPoint).getOperand(1u));

  /* Input defs */
  auto positionInDef = builder.add(Op::DclInputBuiltIn(Type(ScalarType::eF32, 4u).addArrayDimension(32u),
    entryPoint, BuiltIn::ePosition));
  builder.add(Op::Semantic(positionInDef, 0u, "SV_POSITION"));

  auto normalInDef = builder.add(Op::DclInput(Type(ScalarType::eF32, 3u).addArrayDimension(32u),
    entryPoint, 1u, 0u));
  builder.add(Op::Semantic(normalInDef, 0u, "NORMAL"));

  auto factorInDef = builder.add(Op::DclInput(Type(ScalarType::eF32).addArrayDimension(32u),
    entryPoint, 1u, 3u));
  builder.add(Op::Semantic(factorInDef, 0u, "FACTOR"));

  auto controlPointDef = builder.add(Op::DclInputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eTessControlPointId));
  builder.add(Op::Semantic(controlPointDef, 0u, "SV_OUTPUTCONTROLPOINTID"));

  auto primitiveIdDef = builder.add(Op::DclInputBuiltIn(Type(ScalarType::eU32).addArrayDimension(32u), entryPoint, BuiltIn::ePrimitiveId));
  builder.add(Op::Semantic(primitiveIdDef, 0u, "PRIMITIVE_ID"));

  /* Output defs */
  auto positionOutDef = builder.add(Op::DclOutput(Type(ScalarType::eF32, 4u).addArrayDimension(4u), entryPoint, 0u, 0u));
  builder.add(Op::Semantic(positionOutDef, 0u, "SV_POSITION"));

  auto normalOutDef = builder.add(Op::DclOutput(Type(ScalarType::eF32, 3u).addArrayDimension(4u), entryPoint, 1u, 0u));
  builder.add(Op::Semantic(normalOutDef, 0u, "NORMAL"));

  /* Patch constant output defs */
  auto instanceOutDef = builder.add(Op::DclOutput(Type(ScalarType::eU32), entryPoint, 2u, 0u));
  builder.add(Op::Semantic(instanceOutDef, 0u, "INSTANCE_ID"));

  auto tangentAOutDef = builder.add(Op::DclOutput(Type(ScalarType::eF32, 3u), entryPoint, 3u, 0u));
  builder.add(Op::Semantic(tangentAOutDef, 0u, "TANGENT"));

  auto tangentBOutDef = builder.add(Op::DclOutput(Type(ScalarType::eF32, 3u), entryPoint, 4u, 0u));
  builder.add(Op::Semantic(tangentBOutDef, 1u, "TANGENT"));

  /* Tess factors */
  auto tessFactorOuterDef = builder.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32).addArrayDimension(4u), entryPoint, BuiltIn::eTessFactorOuter));
  builder.add(Op::Semantic(tessFactorOuterDef, 0u, "SV_TESSFACTOR"));

  auto tessFactorInnerDef = builder.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32).addArrayDimension(2u), entryPoint, BuiltIn::eTessFactorInner));
  builder.add(Op::Semantic(tessFactorInnerDef, 0u, "SV_INSIDETESSFACTOR"));

  /* Control point function */
  builder.setCursor(cpDef);
  builder.add(Op::Label());

  auto controlPointId = builder.add(Op::InputLoad(ScalarType::eU32, controlPointDef, SsaDef()));

  builder.add(Op::OutputStore(positionOutDef, controlPointId,
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 4u), positionInDef, controlPointId))));
  builder.add(Op::OutputStore(normalOutDef, controlPointId,
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 3u), normalInDef, controlPointId))));

  builder.add(Op::Return());

  /* Patch constant function */
  builder.setCursor(pcDef);
  builder.add(Op::Label());

  builder.add(Op::OutputStore(instanceOutDef, SsaDef(),
    builder.add(Op::InputLoad(ScalarType::eU32, primitiveIdDef, builder.makeConstant(0u)))));

  auto factor = builder.add(Op::FMin(ScalarType::eF32,
    builder.add(Op::InputLoad(ScalarType::eF32, factorInDef, builder.makeConstant(0u))),
    builder.makeConstant(64.0f)));

  for (uint32_t i = 0u; i < 4u; i++)
    builder.add(Op::OutputStore(tessFactorOuterDef, builder.makeConstant(i), factor));

  for (uint32_t i = 0u; i < 2u; i++)
    builder.add(Op::OutputStore(tessFactorInnerDef, builder.makeConstant(i), factor));

  for (uint32_t i = 0u; i < 3u; i++) {
    auto pos0 = builder.add(Op::OutputLoad(ScalarType::eF32, positionOutDef, builder.makeConstant(0u, i)));
    auto pos1 = builder.add(Op::OutputLoad(ScalarType::eF32, positionOutDef, builder.makeConstant(1u, i)));
    auto pos2 = builder.add(Op::OutputLoad(ScalarType::eF32, positionOutDef, builder.makeConstant(2u, i)));

    builder.add(Op::OutputStore(tangentAOutDef, builder.makeConstant(i), builder.add(Op::FSub(ScalarType::eF32, pos1, pos0))));
    builder.add(Op::OutputStore(tangentBOutDef, builder.makeConstant(i), builder.add(Op::FSub(ScalarType::eF32, pos2, pos0))));
  }

  builder.add(Op::Return());

  return builder;
}

Builder test_io_hs_point() {
  return make_test_io_hs(PrimitiveType::ePoints, TessWindingOrder::eCw, TessPartitioning::eInteger);
}

Builder test_io_hs_line() {
  return make_test_io_hs(PrimitiveType::eLines, TessWindingOrder::eCw, TessPartitioning::eInteger);
}

Builder test_io_hs_triangle_cw() {
  return make_test_io_hs(PrimitiveType::eTriangles, TessWindingOrder::eCw, TessPartitioning::eFractEven);
}

Builder test_io_hs_triangle_ccw() {
  return make_test_io_hs(PrimitiveType::eTriangles, TessWindingOrder::eCcw, TessPartitioning::eFractOdd);
}


Builder make_test_io_ds(PrimitiveType domain) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eDomain);

  builder.add(Op::SetTessDomain(entryPoint, domain));
  builder.add(Op::Label());

  /* Inputs */
  auto tessCoordInDef = builder.add(Op::DclInputBuiltIn(Type(ScalarType::eF32, 3u), entryPoint, BuiltIn::eTessCoord));
  builder.add(Op::Semantic(tessCoordInDef, 0u, "SV_DOMAINLOCATION"));

  auto tessLevelInnerInDef = builder.add(Op::DclInputBuiltIn(Type(ScalarType::eF32).addArrayDimension(2u), entryPoint, BuiltIn::eTessFactorInner));
  builder.add(Op::Semantic(tessLevelInnerInDef, 0u, "SV_INSIDETESSFACTOR"));

  auto tessLevelOuterInDef = builder.add(Op::DclInputBuiltIn(Type(ScalarType::eF32).addArrayDimension(4u), entryPoint, BuiltIn::eTessFactorOuter));
  builder.add(Op::Semantic(tessLevelOuterInDef, 0u, "SV_TESSFACTOR"));

  auto primIdInDef = builder.add(Op::DclInputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::ePrimitiveId));
  builder.add(Op::Semantic(primIdInDef, 0u, "SV_PRIMITIVEID"));

  auto posInDef = builder.add(Op::DclInput(Type(ScalarType::eF32, 4u).addArrayDimension(4u), entryPoint, 0u, 0u));
  builder.add(Op::Semantic(posInDef, 0u, "SV_POSITION"));

  auto rColorInDef = builder.add(Op::DclInput(Type(ScalarType::eF32).addArrayDimension(4u), entryPoint, 1u, 0u));
  auto gColorInDef = builder.add(Op::DclInput(Type(ScalarType::eF32).addArrayDimension(4u), entryPoint, 1u, 1u));
  auto bColorInDef = builder.add(Op::DclInput(Type(ScalarType::eF32).addArrayDimension(4u), entryPoint, 1u, 2u));

  builder.add(Op::Semantic(rColorInDef, 0u, "R_COLOR"));
  builder.add(Op::Semantic(gColorInDef, 0u, "G_COLOR"));
  builder.add(Op::Semantic(bColorInDef, 0u, "B_COLOR"));

  auto coordInDef = builder.add(Op::DclInput(Type(ScalarType::eF32, 2u).addArrayDimension(4u), entryPoint, 2u, 0u));
  builder.add(Op::Semantic(coordInDef, 0u, "TEXCOORD"));

  auto layerInDef = builder.add(Op::DclInput(ScalarType::eU32, entryPoint, 3u, 0u));
  builder.add(Op::Semantic(layerInDef, 0u, "LAYER"));

  auto viewportInDef = builder.add(Op::DclInput(ScalarType::eU32, entryPoint, 3u, 1u));
  builder.add(Op::Semantic(viewportInDef, 0u, "VIEWPORT"));

  auto indexInDef = builder.add(Op::DclInput(ScalarType::eU32, entryPoint, 3u, 2u));
  builder.add(Op::Semantic(indexInDef, 0u, "INDEX"));

  /* Outputs */
  auto posOutDef = builder.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition));
  builder.add(Op::Semantic(posOutDef, 0u, "SV_POSITION"));

  auto clipOutDef = builder.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32).addArrayDimension(8u), entryPoint, BuiltIn::eClipDistance));
  builder.add(Op::Semantic(clipOutDef, 0u, "SV_CLIPDISTANCE"));

  auto layerIdOutDef = builder.add(Op::DclOutputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eLayerIndex));
  builder.add(Op::Semantic(layerIdOutDef, 0u, "SV_RENDERTARGETARRAYINDEX"));

  auto viewportIdOutDef = builder.add(Op::DclOutputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eViewportIndex));
  builder.add(Op::Semantic(viewportIdOutDef, 0u, "SV_VIEWPORTARRAYINDEX"));

  auto colorOutDef = builder.add(Op::DclOutput(Type(ScalarType::eF32, 3u), entryPoint, 6u, 0u));
  builder.add(Op::Semantic(colorOutDef, 0u, "COLOR"));

  auto primIdOutDef = builder.add(Op::DclOutput(ScalarType::eU32, entryPoint, 6u, 3u));
  builder.add(Op::Semantic(primIdOutDef, 0u, "PRIMID"));

  auto coordOutDef = builder.add(Op::DclOutput(Type(ScalarType::eF32, 2u), entryPoint, 7u, 0u));
  builder.add(Op::Semantic(coordOutDef, 0u, "TEXCOORD"));

  auto tessLevelInnerOutDef = builder.add(Op::DclOutput(Type(ScalarType::eF32, 2u), entryPoint, 7u, 2u));
  builder.add(Op::Semantic(tessLevelInnerOutDef, 0u, "TESS_INNER"));

  auto tessLevelOuterOutDef = builder.add(Op::DclOutput(Type(ScalarType::eF32, 4u), entryPoint, 8u, 0u));
  builder.add(Op::Semantic(tessLevelOuterOutDef, 0u, "TESS_OUTER"));

  /* Export tess factors */
  for (uint32_t i = 0u; i < 2u; i++) {
    builder.add(Op::OutputStore(tessLevelInnerOutDef, builder.makeConstant(i),
      builder.add(Op::InputLoad(ScalarType::eF32, tessLevelInnerInDef, builder.makeConstant(i)))));
  }

  for (uint32_t i = 0u; i < 4u; i++) {
    builder.add(Op::OutputStore(tessLevelOuterOutDef, builder.makeConstant(i),
      builder.add(Op::InputLoad(ScalarType::eF32, tessLevelOuterInDef, builder.makeConstant(i)))));
  }

  /* Export built-ins */
  builder.add(Op::OutputStore(layerIdOutDef, SsaDef(),
    builder.add(Op::InputLoad(ScalarType::eU32, layerInDef, SsaDef()))));

  builder.add(Op::OutputStore(viewportIdOutDef, SsaDef(),
    builder.add(Op::InputLoad(ScalarType::eU32, viewportInDef, SsaDef()))));

  builder.add(Op::OutputStore(primIdOutDef, SsaDef(),
    builder.add(Op::InputLoad(ScalarType::eU32, primIdInDef, SsaDef()))));

  /* Compute and export vertex attributes */
  auto indexDef = builder.add(Op::InputLoad(ScalarType::eU32, indexInDef, SsaDef()));

  std::array<SsaDef, 3u> tessCoordDef = { };
  std::array<SsaDef, 4u> posDef = { };

  if (domain == PrimitiveType::eTriangles) {
    for (uint32_t i = 0u; i < 3u; i++) {
      tessCoordDef[i] = builder.add(Op::InputLoad(ScalarType::eF32,
        tessCoordInDef, builder.makeConstant(i)));
    }

    for (uint32_t i = 0u; i < 4u; i++) {
      auto posCoord = builder.add(Op::InputLoad(ScalarType::eF32, posInDef,
        builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 2u), indexDef, builder.makeConstant(i)))));
      posDef[i] = builder.add(Op::FMul(ScalarType::eF32, posCoord, tessCoordDef[0u]));
    }

    for (uint32_t j = 1u; j < 3u; j++) {
      auto indexShiftDef = builder.add(Op::IXor(ScalarType::eU32, indexDef, builder.makeConstant(j)));

      for (uint32_t i = 0u; i < 4u; i++) {
        auto posCoord = builder.add(Op::InputLoad(ScalarType::eF32, posInDef,
          builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 2u), indexShiftDef, builder.makeConstant(i)))));
        posDef[i] = builder.add(Op::FMad(ScalarType::eF32, posCoord, tessCoordDef[j], posDef[i]));
      }
    }
  } else {
    for (uint32_t i = 0u; i < 2u; i++) {
      tessCoordDef[i] = builder.add(Op::InputLoad(ScalarType::eF32,
        tessCoordInDef, builder.makeConstant(i)));
    }

    for (uint32_t i = 0u; i < 4u; i++) {
      auto a = builder.add(Op::InputLoad(ScalarType::eF32, posInDef, builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 2u),
        indexDef, builder.makeConstant(i)))));
      auto b = builder.add(Op::InputLoad(ScalarType::eF32, posInDef, builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 2u),
        builder.add(Op::IXor(ScalarType::eU32, indexDef, builder.makeConstant(1u))), builder.makeConstant(i)))));
      auto c = builder.add(Op::InputLoad(ScalarType::eF32, posInDef, builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 2u),
        builder.add(Op::IXor(ScalarType::eU32, indexDef, builder.makeConstant(2u))), builder.makeConstant(i)))));
      auto d = builder.add(Op::InputLoad(ScalarType::eF32, posInDef, builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 2u),
        builder.add(Op::IXor(ScalarType::eU32, indexDef, builder.makeConstant(3u))), builder.makeConstant(i)))));

      b = builder.add(Op::FSub(ScalarType::eF32, b, a));
      d = builder.add(Op::FSub(ScalarType::eF32, d, c));

      a = builder.add(Op::FMad(ScalarType::eF32, tessCoordDef[0u], b, a));
      c = builder.add(Op::FMad(ScalarType::eF32, tessCoordDef[0u], d, c));

      c = builder.add(Op::FSub(ScalarType::eF32, c, a));
      a = builder.add(Op::FMad(ScalarType::eF32, tessCoordDef[1u], c, a));

      posDef[i] = a;
    }
  }

  for (uint32_t i = 0u; i < 4u; i++)
    builder.add(Op::OutputStore(posOutDef, builder.makeConstant(i), posDef[i]));

  builder.add(Op::OutputStore(colorOutDef, builder.makeConstant(0u),
    builder.add(Op::InputLoad(ScalarType::eF32, rColorInDef, builder.makeConstant(0u)))));
  builder.add(Op::OutputStore(colorOutDef, builder.makeConstant(1u),
    builder.add(Op::InputLoad(ScalarType::eF32, gColorInDef, builder.makeConstant(1u)))));
  builder.add(Op::OutputStore(colorOutDef, builder.makeConstant(2u),
    builder.add(Op::InputLoad(ScalarType::eF32, bColorInDef, builder.makeConstant(2u)))));

  builder.add(Op::OutputStore(coordOutDef, SsaDef(),
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 2u), coordInDef, indexDef))));

  builder.add(Op::Return());
  return builder;
}

Builder test_io_ds_isoline() {
  return make_test_io_ds(PrimitiveType::eLines);
}

Builder test_io_ds_triangle() {
  return make_test_io_ds(PrimitiveType::eTriangles);
}

Builder test_io_ds_quad() {
  return make_test_io_ds(PrimitiveType::eQuads);
}

Builder test_io_cs_builtins() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eCompute);

  builder.add(Op::SetCsWorkgroupSize(entryPoint, 4u, 4u, 4u));
  builder.add(Op::Label());

  auto gidDef = builder.add(Op::DclInputBuiltIn(Type(ScalarType::eU32, 3u), entryPoint, BuiltIn::eGlobalThreadId));
  auto widDef = builder.add(Op::DclInputBuiltIn(Type(ScalarType::eU32, 3u), entryPoint, BuiltIn::eWorkgroupId));
  auto lidDef = builder.add(Op::DclInputBuiltIn(Type(ScalarType::eU32, 3u), entryPoint, BuiltIn::eLocalThreadId));
  auto tidDef = builder.add(Op::DclInputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eLocalThreadIndex));

  builder.add(Op::Semantic(gidDef, 0u, "SV_DispatchThreadId"));
  builder.add(Op::Semantic(widDef, 0u, "SV_GroupId"));
  builder.add(Op::Semantic(lidDef, 0u, "SV_GroupThreadId"));
  builder.add(Op::Semantic(tidDef, 0u, "SV_GroupIndex"));

  auto aUav = builder.add(Op::DclUav(ScalarType::eU32, entryPoint, 0u, 0u, 1u, ResourceKind::eImage3D, UavFlag::eWriteOnly));
  auto bUav = builder.add(Op::DclUav(ScalarType::eU32, entryPoint, 0u, 1u, 1u, ResourceKind::eImage3D, UavFlag::eWriteOnly));
  auto cUav = builder.add(Op::DclUav(ScalarType::eU32, entryPoint, 0u, 2u, 1u, ResourceKind::eImage3D, UavFlag::eWriteOnly));

  auto aDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, aUav, builder.makeConstant(0u)));
  auto bDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, bUav, builder.makeConstant(0u)));
  auto cDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, cUav, builder.makeConstant(0u)));

  auto tid = builder.add(Op::InputLoad(ScalarType::eU32, tidDef, SsaDef()));
  auto value = builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 4u), tid, tid, tid, tid));

  auto aCoord = builder.add(Op::InputLoad(Type(ScalarType::eU32, 3u), gidDef, SsaDef()));
  auto bCoord = builder.add(Op::InputLoad(Type(ScalarType::eU32, 3u), widDef, SsaDef()));
  auto cCoord = builder.add(Op::InputLoad(Type(ScalarType::eU32, 3u), lidDef, SsaDef()));

  builder.add(Op::ImageStore(aDescriptor, SsaDef(), aCoord, value));
  builder.add(Op::ImageStore(bDescriptor, SsaDef(), bCoord, value));
  builder.add(Op::ImageStore(cDescriptor, SsaDef(), cCoord, value));

  builder.add(Op::Return());
  return builder;
}

}
