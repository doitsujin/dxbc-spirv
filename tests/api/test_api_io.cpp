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

Builder make_test_io_gs_basic(ir::PrimitiveType inType, ir::PrimitiveType outType) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eGeometry);
  auto vertexCount = primitiveVertexCount(inType);

  builder.add(Op::SetGsInstances(entryPoint, 1u));
  builder.add(Op::SetGsInputPrimitive(entryPoint, inType));
  builder.add(Op::SetGsOutputPrimitive(entryPoint, outType, 0u));
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
  auto loopCond = builder.add(Op::ULt(loopCounterValue, builder.makeConstant(vertexCount)));

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
  builder.add(Op::SetGsOutputPrimitive(entryPoint, PrimitiveType::ePoints, 0u));
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
  builder.add(Op::SetGsOutputPrimitive(entryPoint, PrimitiveType::ePoints, 0u));
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
  builder.add(Op::SetGsOutputPrimitive(entryPoint, PrimitiveType::ePoints, 0u));
  builder.add(Op::SetGsOutputPrimitive(entryPoint, PrimitiveType::ePoints, 1u));
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
  builder.add(Op::SetGsOutputPrimitive(entryPoint, PrimitiveType::ePoints, 0u));
  builder.add(Op::SetGsOutputPrimitive(entryPoint, PrimitiveType::ePoints, 1u));
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

}
