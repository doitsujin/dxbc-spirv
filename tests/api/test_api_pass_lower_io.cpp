#include "test_api_pass_scalarize.h"

#include "../../ir/passes/ir_pass_lower_io.h"
#include "../../ir/passes/ir_pass_remove_unused.h"
#include "../../ir/passes/ir_pass_scalarize.h"

namespace dxbc_spv::test_api {

static Builder& run_xfb_pass(Builder& b, size_t entryCount, const IoXfbInfo* entries, int32_t rasterizedStream) {
  LowerIoPass pass(b);
  pass.resolveXfbOutputs(entryCount, entries, rasterizedStream);

  RemoveUnusedPass::runPass(b);
  return b;
}


static std::pair<Builder, SsaDef> setup_xfb_test(uint32_t streamMask) {
  Builder builder;

  auto entryPoint = setupTestFunction(builder, ShaderStage::eGeometry);
  builder.add(Op::SetGsInstances(entryPoint, 1u));
  builder.add(Op::SetGsInputPrimitive(entryPoint, PrimitiveType::eTriangles));
  builder.add(Op::SetGsOutputVertices(entryPoint, 16u));
  builder.add(Op::SetGsOutputPrimitive(entryPoint, PrimitiveType::ePoints, streamMask));

  auto label = builder.add(Op::Label());
  builder.addAfter(label, Op::Return());
  return std::make_pair(builder, entryPoint);
}


Builder test_pass_lower_io_xfb_simple() {
  auto [b, entry] = setup_xfb_test(0x1u);

  util::small_vector<IoXfbInfo, 32u> xfb;
  xfb.push_back(IoXfbInfo { "sv_position", 0u, 0xfu, 0u, 0u, 0u, 16u });
  xfb.push_back(IoXfbInfo { "normal", 0u, 0x7u, 0u, 1u, 0u, 20u });
  xfb.push_back(IoXfbInfo { "texcoord", 0u, 0x3u, 0u, 1u, 12u, 20u });
  xfb.push_back(IoXfbInfo { "drawid", 0u, 0x1u, 0u, 2u, 0u, 4u });

  auto inPosition = b.add(Op::DclInputBuiltIn(Type(ScalarType::eF32, 4u).addArrayDimension(3u), entry, BuiltIn::ePosition));
  auto inNormal = b.add(Op::DclInput(Type(ScalarType::eF32, 3u).addArrayDimension(3u), entry, 1u, 0u));
  auto inTexCoord = b.add(Op::DclInput(Type(ScalarType::eF32, 2u).addArrayDimension(3u), entry, 2u, 0u));
  auto inDrawId = b.add(Op::DclInput(Type(ScalarType::eU32, 1u).addArrayDimension(3u), entry, 2u, 2u));

  b.add(Op::Semantic(inPosition, 0, "SV_POSITION"));
  b.add(Op::Semantic(inNormal, 0, "NORMAL"));
  b.add(Op::Semantic(inTexCoord, 0, "TEXCOORD"));
  b.add(Op::Semantic(inDrawId, 0, "DRAWID"));

  auto outPosition = b.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32, 4u), entry, BuiltIn::ePosition, 0u));
  auto outNormal = b.add(Op::DclOutput(Type(ScalarType::eF32, 3u), entry, 1u, 0u, 0u));
  auto outTexCoord = b.add(Op::DclOutput(Type(ScalarType::eF32, 2u), entry, 2u, 0u, 0u));
  auto outDrawId = b.add(Op::DclOutput(Type(ScalarType::eU32, 1u), entry, 2u, 2u, 0u));

  b.add(Op::Semantic(outPosition, 0, "SV_POSITION"));
  b.add(Op::Semantic(outNormal, 0, "NORMAL"));
  b.add(Op::Semantic(outTexCoord, 0, "TEXCOORD"));
  b.add(Op::Semantic(outDrawId, 0, "DRAWID"));

  for (uint32_t i = 0u; i < 3u; i++) {
    b.add(Op::OutputStore(outPosition, SsaDef(), b.add(
      Op::InputLoad(Type(ScalarType::eF32, 4u), inPosition, b.makeConstant(i)))));

    b.add(Op::OutputStore(outNormal, SsaDef(), b.add(
      Op::InputLoad(Type(ScalarType::eF32, 3u), inNormal, b.makeConstant(i)))));

    b.add(Op::OutputStore(outTexCoord, SsaDef(), b.add(
      Op::InputLoad(Type(ScalarType::eF32, 2u), inTexCoord, b.makeConstant(i)))));

    b.add(Op::OutputStore(outDrawId, SsaDef(), b.add(
      Op::InputLoad(Type(ScalarType::eU32), inDrawId, b.makeConstant(i)))));

    b.add(Op::EmitVertex(0u));
  }

  return run_xfb_pass(b, xfb.size(), xfb.data(), -1);
}


Builder test_pass_lower_io_xfb_partial() {
  auto [b, entry] = setup_xfb_test(0x1u);

  util::small_vector<IoXfbInfo, 32u> xfb;
  xfb.push_back(IoXfbInfo { "sv_position", 0u, 0xau, 0u, 0u, 0u, 8u });
  xfb.push_back(IoXfbInfo { "NORMAL", 0u, 0x1u, 0u, 1u, 0u, 8u });
  xfb.push_back(IoXfbInfo { "TEXCOORD", 0u, 0x2u, 0u, 1u, 4u, 8u });

  auto inPosition = b.add(Op::DclInputBuiltIn(Type(ScalarType::eF32, 4u).addArrayDimension(3u), entry, BuiltIn::ePosition));
  auto inNormal = b.add(Op::DclInput(Type(ScalarType::eF32, 3u).addArrayDimension(3u), entry, 1u, 0u));
  auto inTexCoord = b.add(Op::DclInput(Type(ScalarType::eF32, 2u).addArrayDimension(3u), entry, 2u, 0u));
  auto inDrawId = b.add(Op::DclInput(Type(ScalarType::eU32, 1u).addArrayDimension(3u), entry, 2u, 2u));

  b.add(Op::Semantic(inPosition, 0, "SV_POSITION"));
  b.add(Op::Semantic(inNormal, 0, "NORMAL"));
  b.add(Op::Semantic(inTexCoord, 0, "TEXCOORD"));
  b.add(Op::Semantic(inDrawId, 0, "DRAWID"));

  auto outPosition = b.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32, 4u), entry, BuiltIn::ePosition, 0u));
  auto outNormal = b.add(Op::DclOutput(Type(ScalarType::eF32, 3u), entry, 1u, 0u, 0u));
  auto outTexCoord = b.add(Op::DclOutput(Type(ScalarType::eF32, 2u), entry, 2u, 0u, 0u));
  auto outDrawId = b.add(Op::DclOutput(Type(ScalarType::eU32, 1u), entry, 2u, 2u, 0u));

  b.add(Op::Semantic(outPosition, 0, "SV_POSITION"));
  b.add(Op::Semantic(outNormal, 0, "NORMAL"));
  b.add(Op::Semantic(outTexCoord, 0, "TEXCOORD"));
  b.add(Op::Semantic(outDrawId, 0, "DRAWID"));

  for (uint32_t i = 0u; i < 3u; i++) {
    b.add(Op::OutputStore(outPosition, SsaDef(), b.add(
      Op::InputLoad(Type(ScalarType::eF32, 4u), inPosition, b.makeConstant(i)))));

    b.add(Op::OutputStore(outNormal, SsaDef(), b.add(
      Op::InputLoad(Type(ScalarType::eF32, 3u), inNormal, b.makeConstant(i)))));

    b.add(Op::OutputStore(outTexCoord, SsaDef(), b.add(
      Op::InputLoad(Type(ScalarType::eF32, 2u), inTexCoord, b.makeConstant(i)))));

    b.add(Op::OutputStore(outDrawId, SsaDef(), b.add(
      Op::InputLoad(Type(ScalarType::eU32), inDrawId, b.makeConstant(i)))));

    b.add(Op::EmitVertex(0u));
  }

  return run_xfb_pass(b, xfb.size(), xfb.data(), -1);
}


Builder test_pass_lower_io_xfb_multi_stream_with_stream_index(int32_t rasterizedStream, bool streamout) {
  auto [b, entry] = setup_xfb_test(0x7u);

  util::small_vector<IoXfbInfo, 32u> xfb;

  /* Stream 0 */
  xfb.push_back(IoXfbInfo { "SV_position", 0u, 0x7u, 0u, 0u, 0u, 16u });
  xfb.push_back(IoXfbInfo { "NORMAL", 0u, 0x7u, 0u, 1u, 0u, 20u });
  xfb.push_back(IoXfbInfo { "texcoord", 0u, 0x3u, 0u, 1u, 16u, 20u });

  auto inS0Position = b.add(Op::DclInputBuiltIn(Type(ScalarType::eF32, 4u).addArrayDimension(3u), entry, BuiltIn::ePosition));
  auto inS0Normal = b.add(Op::DclInput(Type(ScalarType::eF32, 3u).addArrayDimension(3u), entry, 1u, 0u));
  auto inS0TexCoord = b.add(Op::DclInput(Type(ScalarType::eF32, 2u).addArrayDimension(3u), entry, 2u, 0u));

  b.add(Op::Semantic(inS0Position, 0, "SV_POSITION"));
  b.add(Op::Semantic(inS0Normal, 0, "NORMAL"));
  b.add(Op::Semantic(inS0TexCoord, 0, "TEXCOORD"));

  auto outS0Position = b.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32, 4u), entry, BuiltIn::ePosition, 0u));
  auto outS0Normal = b.add(Op::DclOutput(Type(ScalarType::eF32, 3u), entry, 1u, 0u, 0u));
  auto outS0TexCoord = b.add(Op::DclOutput(Type(ScalarType::eF32, 2u), entry, 2u, 0u, 0u));

  b.add(Op::Semantic(outS0Position, 0, "SV_POSITION"));
  b.add(Op::Semantic(outS0Normal, 0, "NORMAL"));
  b.add(Op::Semantic(outS0TexCoord, 0, "TEXCOORD"));

  /* Stream 1 */
  xfb.push_back(IoXfbInfo { "SV_POSITION", 0u, 0x3u, 1u, 2u, 0u, 16u });

  auto inS1Position = b.add(Op::DclInput(Type(ScalarType::eF32, 4u).addArrayDimension(3u), entry, 4u, 0u));
  auto inS1Color = b.add(Op::DclInput(Type(ScalarType::eF32, 3u).addArrayDimension(3u), entry, 5u, 1u));

  b.add(Op::Semantic(inS1Position, 0, "SV_POSITION"));
  b.add(Op::Semantic(inS1Color, 0, "COLOR"));

  auto outS1Position = b.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32, 4u), entry, BuiltIn::ePosition, 1u));
  auto outS1Color = b.add(Op::DclOutput(Type(ScalarType::eF32, 3u), entry, 0u, 1u, 1u));

  b.add(Op::Semantic(outS1Position, 0, "SV_POSITION"));
  b.add(Op::Semantic(outS1Color, 0, "COLOR"));

  /* Stream 2 */
  xfb.push_back(IoXfbInfo { "COLOR", 0u, 0x7u, 2u, 3u, 0u, 12u });

  auto inS2Color = b.add(Op::DclInput(Type(ScalarType::eF32, 3u).addArrayDimension(3u), entry, 8u, 0u));
  b.add(Op::Semantic(inS2Color, 0, "COLOR"));

  auto outS2Color = b.add(Op::DclOutput(Type(ScalarType::eF32, 3u), entry, 8u, 0u, 2u));
  b.add(Op::Semantic(outS2Color, 0, "COLOR"));

  for (uint32_t i = 0u; i < 3u; i++) {
    b.add(Op::OutputStore(outS0Position, SsaDef(), b.add(
      Op::InputLoad(Type(ScalarType::eF32, 4u), inS0Position, b.makeConstant(i)))));

    b.add(Op::OutputStore(outS0Normal, SsaDef(), b.add(
      Op::InputLoad(Type(ScalarType::eF32, 3u), inS0Normal, b.makeConstant(i)))));

    b.add(Op::OutputStore(outS0TexCoord, SsaDef(), b.add(
      Op::InputLoad(Type(ScalarType::eF32, 2u), inS0TexCoord, b.makeConstant(i)))));

    b.add(Op::EmitVertex(0u));

    b.add(Op::OutputStore(outS1Position, SsaDef(), b.add(
      Op::InputLoad(Type(ScalarType::eF32, 4u), inS1Position, b.makeConstant(i)))));

    b.add(Op::OutputStore(outS1Color, SsaDef(), b.add(
      Op::InputLoad(Type(ScalarType::eF32, 3u), inS1Color, b.makeConstant(i)))));

    b.add(Op::EmitVertex(1u));

    b.add(Op::OutputStore(outS2Color, SsaDef(), b.add(
      Op::InputLoad(Type(ScalarType::eF32, 3u), inS2Color, b.makeConstant(i)))));

    b.add(Op::EmitVertex(2u));
  }

  return run_xfb_pass(b, streamout ? xfb.size() : 0u, xfb.data(), rasterizedStream);
}


Builder test_pass_lower_io_xfb_multi_stream() {
  return test_pass_lower_io_xfb_multi_stream_with_stream_index(-1, true);
}


Builder test_pass_lower_io_xfb_multi_stream_with_raster() {
  return test_pass_lower_io_xfb_multi_stream_with_stream_index(1, true);
}


Builder test_pass_lower_io_xfb_multi_stream_raster_only() {
  return test_pass_lower_io_xfb_multi_stream_with_stream_index(0, false);
}



static Builder setup_patch_constant_test_hs() {
  Builder builder;

  auto entryPoint = setupTestFunction(builder, ShaderStage::eHull);

  builder.add(Op::SetTessPrimitive(entryPoint, PrimitiveType::eTriangles, TessWindingOrder::eCcw, TessPartitioning::eFractOdd));
  builder.add(Op::SetTessDomain(entryPoint, PrimitiveType::eQuads));
  builder.add(Op::SetTessControlPoints(entryPoint, 4u, 4u));

  auto cpIn = builder.add(Op::DclInputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eTessControlPointId));

  /* Control point inputs */
  auto posIn = builder.add(Op::DclInputBuiltIn(Type(ScalarType::eF32, 4u).addArrayDimension(4u), entryPoint, BuiltIn::ePosition));
  auto colorIn = builder.add(Op::DclInput(Type(ScalarType::eF32, 3u).addArrayDimension(4u), entryPoint, 1u, 0u));
  auto normalIn = builder.add(Op::DclInput(Type(ScalarType::eF32, 3u).addArrayDimension(4u), entryPoint, 2u, 0u));
  auto drawIdIn = builder.add(Op::DclInput(Type(ScalarType::eU32, 1u).addArrayDimension(4u), entryPoint, 3u, 0u));
  auto instanceIdIn = builder.add(Op::DclInput(Type(ScalarType::eU32, 1u).addArrayDimension(4u), entryPoint, 3u, 1u));
  auto transformInA = builder.add(Op::DclInput(Type(ScalarType::eF32, 4u).addArrayDimension(4u), entryPoint, 4u, 0u));
  auto transformInB = builder.add(Op::DclInput(Type(ScalarType::eF32, 4u).addArrayDimension(4u), entryPoint, 5u, 0u));

  builder.add(Op::Semantic(posIn, 0u, "SV_POSITION"));
  builder.add(Op::Semantic(colorIn, 0u, "COLOR"));
  builder.add(Op::Semantic(normalIn, 0u, "NORMAL"));
  builder.add(Op::Semantic(drawIdIn, 0u, "DRAWID"));
  builder.add(Op::Semantic(instanceIdIn, 0u, "SV_INSTANCEID"));
  builder.add(Op::Semantic(transformInA, 0u, "TRANSFORM"));
  builder.add(Op::Semantic(transformInB, 1u, "TRANSFORM"));

  /* Control point outputs */
  auto posOut = builder.add(Op::DclOutput(Type(ScalarType::eF32, 4u).addArrayDimension(32u), entryPoint, 0u, 0u));
  auto colorOut = builder.add(Op::DclOutput(Type(ScalarType::eF32, 3u).addArrayDimension(32u), entryPoint, 2u, 0u));
  auto normalOut = builder.add(Op::DclOutput(Type(ScalarType::eF32, 3u).addArrayDimension(32u), entryPoint, 3u, 0u));

  builder.add(Op::Semantic(posOut, 0u, "SV_POSITION"));
  builder.add(Op::Semantic(colorOut, 0u, "COLOR"));
  builder.add(Op::Semantic(normalOut, 0u, "NORMAL"));

  /* Patch constant outputs */
  auto drawIdOut = builder.add(Op::DclOutput(Type(ScalarType::eU32, 1u), entryPoint, 0u, 0u));
  auto instanceIdOut = builder.add(Op::DclOutput(Type(ScalarType::eU32, 1u), entryPoint, 0u, 1u));
  auto transformOutA = builder.add(Op::DclOutput(Type(ScalarType::eF32, 4u), entryPoint, 1u, 0u));
  auto transformOutB = builder.add(Op::DclOutput(Type(ScalarType::eF32, 4u), entryPoint, 8u, 0u));

  builder.add(Op::Semantic(drawIdOut, 0u, "DRAWID"));
  builder.add(Op::Semantic(instanceIdOut, 0u, "SV_INSTANCEID"));
  builder.add(Op::Semantic(transformOutA, 0u, "TRANSFORM"));
  builder.add(Op::Semantic(transformOutB, 1u, "TRANSFORM"));

  /* Tess factor outputs */
  auto tessFactorOuter = builder.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32).addArrayDimension(4u), entryPoint, BuiltIn::eTessFactorOuter));
  auto tessFactorInner = builder.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32).addArrayDimension(2u), entryPoint, BuiltIn::eTessFactorInner));

  builder.add(Op::Semantic(tessFactorOuter, 0u, "SV_TESSFACTOR"));
  builder.add(Op::Semantic(tessFactorInner, 0u, "SV_INSIDETESSFACTOR"));

  /* Control point function */
  builder.setCursor(SsaDef(builder.getOp(entryPoint).getOperand(0u)));

  builder.add(Op::Label());

  auto cpId = builder.add(Op::InputLoad(ScalarType::eU32, cpIn, SsaDef()));

  builder.add(Op::OutputStore(posOut, cpId,
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 4u), posIn, cpId))));

  builder.add(Op::OutputStore(colorOut, cpId,
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 3u), colorIn, cpId))));

  builder.add(Op::OutputStore(normalOut, cpId,
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 3u), normalIn, cpId))));

  builder.add(Op::Return());

  /* Patch constant function */
  builder.setCursor(SsaDef(builder.getOp(entryPoint).getOperand(1u)));

  builder.add(Op::Label());

  builder.add(Op::OutputStore(drawIdOut, SsaDef(),
    builder.add(Op::InputLoad(Type(ScalarType::eU32, 1u), drawIdIn, builder.makeConstant(0u)))));

  builder.add(Op::OutputStore(instanceIdOut, SsaDef(),
    builder.add(Op::InputLoad(Type(ScalarType::eU32, 1u), instanceIdIn, builder.makeConstant(0u)))));

  builder.add(Op::OutputStore(transformOutA, SsaDef(),
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 4u), transformInA, builder.makeConstant(0u)))));

  builder.add(Op::OutputStore(transformOutB, SsaDef(),
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 4u), transformInB, builder.makeConstant(0u)))));

  for (uint32_t i = 0u; i < 4u; i++)
    builder.add(Op::OutputStore(tessFactorOuter, builder.makeConstant(i), builder.makeConstant(4.0f)));

  for (uint32_t i = 0u; i < 2u; i++)
    builder.add(Op::OutputStore(tessFactorInner, builder.makeConstant(i), builder.makeConstant(4.0f)));

  builder.add(Op::Return());
  return builder;
}


static Builder setup_patch_constant_test_ds() {
  Builder ds;

  auto entryPoint = setupTestFunction(ds, ShaderStage::eDomain);

  /* Control point inputs */
  auto colorIn = ds.add(Op::DclInput(Type(ScalarType::eF32, 3u).addArrayDimension(32u), entryPoint, 2u, 0u));
  auto posIn = ds.add(Op::DclInput(Type(ScalarType::eF32, 4u).addArrayDimension(32u), entryPoint, 0u, 0u));

  ds.add(Op::Semantic(posIn, 0u, "SV_POSITION"));
  ds.add(Op::Semantic(colorIn, 0u, "COLOR"));

  /* Patch constant inputs */
  auto transformInA = ds.add(Op::DclInput(Type(ScalarType::eF32, 4u), entryPoint, 1u, 0u));
  auto instanceIdIn = ds.add(Op::DclInput(Type(ScalarType::eU32, 1u), entryPoint, 0u, 1u));
  auto transformInB = ds.add(Op::DclInput(Type(ScalarType::eF32, 4u), entryPoint, 8u, 0u));
  auto drawIdIn = ds.add(Op::DclInput(Type(ScalarType::eU32, 1u), entryPoint, 0u, 0u));

  ds.add(Op::Semantic(drawIdIn, 0u, "DRAWID"));
  ds.add(Op::Semantic(instanceIdIn, 0u, "SV_INSTANCEID"));
  ds.add(Op::Semantic(transformInA, 0u, "TRANSFORM"));
  ds.add(Op::Semantic(transformInB, 1u, "TRANSFORM"));

  /* Outputs */
  auto posOut = ds.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition));
  auto colorOut = ds.add(Op::DclOutput(Type(ScalarType::eF32, 3u), entryPoint, 0u, 0u));
  auto drawIdOut = ds.add(Op::DclOutput(Type(ScalarType::eU32, 1u), entryPoint, 2u, 0u));
  auto instanceIdOut = ds.add(Op::DclOutput(Type(ScalarType::eU32, 1u), entryPoint, 2u, 1u));

  ds.add(Op::Semantic(posOut, 0u, "SV_POSITION"));
  ds.add(Op::Semantic(drawIdOut, 0u, "DRAWID"));
  ds.add(Op::Semantic(colorOut, 0u, "COLOR"));
  ds.add(Op::Semantic(instanceIdOut, 0u, "SV_INSTANCEID"));

  /* Tess coord */
  auto tessCoord = ds.add(Op::DclInputBuiltIn(Type(ScalarType::eF32, 3u), entryPoint, BuiltIn::eTessCoord));
  ds.add(Op::Semantic(tessCoord, 0u, "SV_DOMAINLOCATION"));

  ds.add(Op::Label());

  ds.add(Op::OutputStore(drawIdOut, SsaDef(),
    ds.add(Op::InputLoad(ScalarType::eU32, drawIdIn, SsaDef()))));

  ds.add(Op::OutputStore(instanceIdOut, SsaDef(),
    ds.add(Op::InputLoad(ScalarType::eU32, instanceIdIn, SsaDef()))));

  ds.add(Op::OutputStore(colorOut, SsaDef(),
    ds.add(Op::InputLoad(Type(ScalarType::eF32, 3u), colorIn, ds.makeConstant(0u)))));

  auto tcX = ds.add(Op::InputLoad(ScalarType::eF32, tessCoord, ds.makeConstant(0u)));
  auto tcY = ds.add(Op::InputLoad(ScalarType::eF32, tessCoord, ds.makeConstant(1u)));

  auto tcXn = ds.add(Op::FSub(ScalarType::eF32, ds.makeConstant(1.0f), tcX));
  auto tcYn = ds.add(Op::FSub(ScalarType::eF32, ds.makeConstant(1.0f), tcY));

  for (uint32_t i = 0u; i < 4u; i++) {
    auto posIn00 = ds.add(Op::InputLoad(ScalarType::eF32, posIn, ds.makeConstant(0u, i)));
    auto posIn10 = ds.add(Op::InputLoad(ScalarType::eF32, posIn, ds.makeConstant(1u, i)));
    auto posIn01 = ds.add(Op::InputLoad(ScalarType::eF32, posIn, ds.makeConstant(2u, i)));
    auto posIn11 = ds.add(Op::InputLoad(ScalarType::eF32, posIn, ds.makeConstant(3u, i)));

    posIn00 = ds.add(Op::FMul(ScalarType::eF32, posIn00, tcXn));
    posIn10 = ds.add(Op::FMad(ScalarType::eF32, posIn10, tcX, posIn00));
    posIn01 = ds.add(Op::FMul(ScalarType::eF32, posIn01, tcXn));
    posIn11 = ds.add(Op::FMad(ScalarType::eF32, posIn11, tcX, posIn01));
    posIn10 = ds.add(Op::FMul(ScalarType::eF32, posIn10, tcYn));
    auto pos = ds.add(Op::FMad(ScalarType::eF32, posIn11, tcY, posIn10));

    pos = ds.add(Op::FMad(ScalarType::eF32, pos,
      ds.add(Op::InputLoad(ScalarType::eF32, transformInA, ds.makeConstant(i))),
      ds.add(Op::InputLoad(ScalarType::eF32, transformInB, ds.makeConstant(i)))));

    ds.add(Op::OutputStore(posOut, ds.makeConstant(i), pos));
  }

  ds.add(Op::Return());
  return ds;
}


static Builder& run_patch_constant_pass(Builder& b, const IoMap& hsOut) {
  LowerIoPass pass(b);
  pass.resolvePatchConstantLocations(hsOut);
  return b;
}


Builder test_pass_lower_io_patch_constant_locations_hs() {
  auto hs = setup_patch_constant_test_hs();
  auto hsOut = IoMap::forOutputs(hs, 0u);

  return run_patch_constant_pass(hs, hsOut);
}


Builder test_pass_lower_io_patch_constant_locations_ds() {
  auto ds = setup_patch_constant_test_ds();
  auto hs = setup_patch_constant_test_hs();
  auto hsOut = IoMap::forOutputs(hs, 0u);

  return run_patch_constant_pass(ds, hsOut);
}



Builder test_pass_lower_io_rewrite_gs_primitive_type() {
  Builder builder;

  auto entryPoint = setupTestFunction(builder, ShaderStage::eGeometry);
  builder.add(Op::SetGsInstances(entryPoint, 1u));
  builder.add(Op::SetGsInputPrimitive(entryPoint, PrimitiveType::eTriangles));
  builder.add(Op::SetGsOutputVertices(entryPoint, 4u));
  builder.add(Op::SetGsOutputPrimitive(entryPoint, PrimitiveType::eTriangles, 0x1u));

  auto vertexCountIn = builder.add(Op::DclInputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eGsVertexCountIn));

  auto posIn = builder.add(Op::DclInputBuiltIn(Type(ScalarType::eF32, 4u).addArrayDimension(3u), entryPoint, BuiltIn::ePosition));
  auto colorIn = builder.add(Op::DclInput(Type(ScalarType::eF32, 3u).addArrayDimension(3u), entryPoint, 1u, 0u));

  builder.add(Op::Semantic(posIn, 0u, "SV_POSITION"));
  builder.add(Op::Semantic(colorIn, 0u, "COLOR"));

  auto posOut = builder.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition, 0u));
  auto colorOut = builder.add(Op::DclOutput(Type(ScalarType::eF32, 3u), entryPoint, 1u, 0u, 0u));

  builder.add(Op::Semantic(posOut, 0u, "SV_POSITION"));
  builder.add(Op::Semantic(colorOut, 0u, "COLOR"));

  builder.add(Op::Label());

  auto count = builder.add(Op::InputLoad(ScalarType::eU32, vertexCountIn, SsaDef()));

  for (uint32_t i = 0u; i < 3u; i++) {
    auto cond = builder.add(Op::ULt(ScalarType::eBool, builder.makeConstant(i), count));

    builder.add(Op::OutputStore(posOut, SsaDef(),
      builder.add(Op::Select(Type(ScalarType::eF32, 4u), cond,
        builder.add(Op::InputLoad(Type(ScalarType::eF32, 4u), posIn, builder.makeConstant(i))),
        builder.makeUndef(Type(ScalarType::eF32, 4u))))));

    builder.add(Op::OutputStore(colorOut, SsaDef(),
      builder.add(Op::Select(Type(ScalarType::eF32, 3u), cond,
        builder.add(Op::InputLoad(Type(ScalarType::eF32, 3u), colorIn, builder.makeConstant(i))),
        builder.makeUndef(Type(ScalarType::eF32, 3u))))));

    builder.add(Op::EmitVertex(0u));
  }

  builder.add(Op::Return());

  LowerIoPass pass(builder);
  pass.changeGsInputPrimitiveType(PrimitiveType::ePoints);
  return builder;
}


static Builder& run_io_mismatch_passes(Builder& b, ShaderStage prevStage, const IoMap& map) {
  LowerIoPass(b).resolveMismatchedIo(prevStage, map);
  ScalarizePass::runResolveRedundantCompositesPass(b);
  RemoveUnusedPass::runPass(b);
  return b;
}


static Builder test_io_mismatch_vs(const IoMap& map) {
  Builder builder;

  auto entryPoint = setupTestFunction(builder, ShaderStage::eVertex);

  auto posIn = builder.add(Op::DclInput(Type(ScalarType::eF32, 4u), entryPoint, 0u, 0u));
  auto normalIn = builder.add(Op::DclInput(Type(ScalarType::eF32, 3u), entryPoint, 1u, 0u));
  auto colorIn = builder.add(Op::DclInput(Type(ScalarType::eU32, 1u), entryPoint, 2u, 0u));
  auto vidIn = builder.add(Op::DclInputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eVertexId));

  builder.add(Op::Semantic(posIn, 0u, "POSITION"));
  builder.add(Op::Semantic(normalIn, 0u, "NORMAL"));
  builder.add(Op::Semantic(colorIn, 0u, "COLOR"));
  builder.add(Op::Semantic(vidIn, 0u, "SV_VERTEXID"));

  auto posOut = builder.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition));
  auto normalOut = builder.add(Op::DclOutput(Type(ScalarType::eF32, 3u), entryPoint, 1u, 0u));
  auto colorOut = builder.add(Op::DclOutput(Type(ScalarType::eF32, 4u), entryPoint, 2u, 0u));
  auto vidOut = builder.add(Op::DclOutput(ScalarType::eU32, entryPoint, 3u, 0u));

  builder.add(Op::Semantic(posOut, 0u, "SV_POSITION"));
  builder.add(Op::Semantic(normalOut, 0u, "NORMAL"));
  builder.add(Op::Semantic(colorOut, 0u, "COLOR"));
  builder.add(Op::Semantic(vidOut, 0u, "VERTEXID"));

  builder.add(Op::Label());

  builder.add(Op::OutputStore(posOut, SsaDef(),
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 4u), posIn, SsaDef()))));

  builder.add(Op::OutputStore(vidOut, SsaDef(),
    builder.add(Op::InputLoad(ScalarType::eU32, vidIn, SsaDef()))));

  for (uint32_t i = 0u; i < 3u; i++) {
    auto n = builder.add(Op::InputLoad(ScalarType::eF32, normalIn, builder.makeConstant(i)));
    n = builder.add(Op::FMad(ScalarType::eF32, n, builder.makeConstant(2.0f), builder.makeConstant(-1.0f)));
    builder.add(Op::OutputStore(normalOut, builder.makeConstant(i), n));
  }

  for (uint32_t i = 0u; i < 4u; i++) {
    auto c = builder.add(Op::InputLoad(ScalarType::eU32, colorIn, SsaDef()));
    c = builder.add(Op::UBitExtract(ScalarType::eU32, c, builder.makeConstant(8u * i), builder.makeConstant(8u)));
    c = builder.add(Op::ConvertItoF(ScalarType::eF32, c));
    c = builder.add(Op::FMul(ScalarType::eF32, c, builder.makeConstant(1.0f / 255.0f)));
    builder.add(Op::OutputStore(colorOut, builder.makeConstant(i), c));
  }

  builder.add(Op::Return());

  return run_io_mismatch_passes(builder, ShaderStage::eFlagEnum, map);
}


static Builder test_io_mismatch_hs(const IoMap& map) {
  Builder hs = setup_patch_constant_test_hs();
  return run_io_mismatch_passes(hs, ShaderStage::eVertex, map);
}


static Builder test_io_mismatch_ds(const IoMap& map) {
  Builder ds = setup_patch_constant_test_ds();
  return run_io_mismatch_passes(ds, ShaderStage::eHull, map);
}


static Builder test_io_mismatch_gs(ShaderStage prevStage, const IoMap& map) {
  Builder builder;

  auto entryPoint = setupTestFunction(builder, ShaderStage::eGeometry);

  builder.add(Op::SetGsInstances(entryPoint, 6u));
  builder.add(Op::SetGsInputPrimitive(entryPoint, PrimitiveType::eTrianglesAdj));
  builder.add(Op::SetGsOutputVertices(entryPoint, 1u));
  builder.add(Op::SetGsOutputPrimitive(entryPoint, PrimitiveType::ePoints, 0x1u));

  auto instanceIdIn = builder.add(Op::DclInputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eGsInstanceId));
  builder.add(Op::Semantic(instanceIdIn, 0u, "SV_GSINSTANCEID"));

  auto posIn = builder.add(Op::DclInputBuiltIn(Type(ScalarType::eF32, 4u).addArrayDimension(6u), entryPoint, BuiltIn::ePosition));
  auto clipIn = builder.add(Op::DclInputBuiltIn(Type(ScalarType::eF32).addArrayDimension(2u).addArrayDimension(6u), entryPoint, BuiltIn::eClipDistance));
  auto coord0In = builder.add(Op::DclInput(Type(ScalarType::eF32, 2u).addArrayDimension(6u), entryPoint, 1u, 0u));
  auto coord1In = builder.add(Op::DclInput(Type(ScalarType::eF32, 2u).addArrayDimension(6u), entryPoint, 1u, 2u));
  auto colorIn = builder.add(Op::DclInput(Type(ScalarType::eF32, 4u).addArrayDimension(6u), entryPoint, 2u, 0u));

  builder.add(Op::Semantic(posIn, 0u, "SV_POSITION"));
  builder.add(Op::Semantic(clipIn, 0u, "SV_CLIPDISTANCE"));
  builder.add(Op::Semantic(coord0In, 0u, "TEXCOORD"));
  builder.add(Op::Semantic(coord1In, 1u, "TEXCOORD"));
  builder.add(Op::Semantic(colorIn, 0u, "COLOR"));

  auto posOut = builder.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition, 0u));
  auto clipOut = builder.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32).addArrayDimension(2u), entryPoint, BuiltIn::eClipDistance, 0u));
  auto coord0Out = builder.add(Op::DclOutput(Type(ScalarType::eF32, 2u), entryPoint, 1u, 0u, 0u));
  auto coord1Out = builder.add(Op::DclOutput(Type(ScalarType::eF32, 2u), entryPoint, 1u, 2u, 0u));
  auto colorOut = builder.add(Op::DclOutput(Type(ScalarType::eF32, 4u), entryPoint, 2u, 0u, 0u));

  builder.add(Op::Semantic(posOut, 0u, "SV_POSITION"));
  builder.add(Op::Semantic(clipOut, 0u, "SV_CLIPDISTANCE"));
  builder.add(Op::Semantic(coord0Out, 0u, "TEXCOORD"));
  builder.add(Op::Semantic(coord1Out, 1u, "TEXCOORD"));
  builder.add(Op::Semantic(colorOut, 0u, "COLOR"));

  builder.add(Op::Label());

  auto iid = builder.add(Op::InputLoad(ScalarType::eU32, instanceIdIn, SsaDef()));

  builder.add(Op::OutputStore(posOut, SsaDef(),
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 4u), posIn, iid))));

  builder.add(Op::OutputStore(coord0Out, SsaDef(),
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 2u), coord0In, iid))));

  builder.add(Op::OutputStore(coord1Out, SsaDef(),
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 2u), coord1In, iid))));

  builder.add(Op::OutputStore(colorOut, SsaDef(),
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 4u), colorIn, iid))));

  for (uint32_t i = 0u; i < 2u; i++) {
    auto address = builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 2u), iid, builder.makeConstant(i)));
    auto clip = builder.add(Op::InputLoad(ScalarType::eF32, clipIn, address));
    builder.add(Op::OutputStore(clipOut, builder.makeConstant(i), clip));
  }

  builder.add(Op::EmitVertex(0u));
  builder.add(Op::Return());

  return run_io_mismatch_passes(builder, prevStage, map);
}


static Builder test_io_mismatch_ps(ShaderStage prevStage, const IoMap& map) {
  Builder builder;

  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  auto posIn = builder.add(Op::DclInputBuiltIn(Type(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition, InterpolationModes()));
  auto clipIn = builder.add(Op::DclInputBuiltIn(Type(ScalarType::eF32).addArrayDimension(5u), entryPoint, BuiltIn::eClipDistance, InterpolationModes()));
  auto cullIn = builder.add(Op::DclInputBuiltIn(Type(ScalarType::eF32).addArrayDimension(2u), entryPoint, BuiltIn::eCullDistance, InterpolationModes()));
  auto primIn = builder.add(Op::DclInputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::ePrimitiveId, InterpolationMode::eFlat));

  auto normalIn = builder.add(Op::DclInput(Type(ScalarType::eF32, 3u), entryPoint, 3u, 0u, InterpolationModes()));
  auto depthIn = builder.add(Op::DclInput(Type(ScalarType::eF32, 1u), entryPoint, 3u, 3u, InterpolationModes()));

  auto clipIndexIn = builder.add(Op::DclInput(ScalarType::eU32, entryPoint, 4u, 0u, InterpolationMode::eFlat));

  auto colorIn = builder.add(Op::DclInput(Type(ScalarType::eF32, 4u), entryPoint, 5u, 0u, InterpolationMode::eCentroid));
  auto coordIn = builder.add(Op::DclInput(Type(ScalarType::eF32, 2u), entryPoint, 6u, 2u, InterpolationMode::eNoPerspective));

  builder.add(Op::Semantic(posIn, 0u, "SV_POSITION"));
  builder.add(Op::Semantic(clipIn, 0u, "SV_CLIPDISTANCE"));
  builder.add(Op::Semantic(cullIn, 0u, "SV_CULLDISTANCE"));
  builder.add(Op::Semantic(primIn, 0u, "SV_PRIMITIVEID"));
  builder.add(Op::Semantic(normalIn, 0u, "NORMAL"));
  builder.add(Op::Semantic(depthIn, 0u, "DEPTH"));
  builder.add(Op::Semantic(clipIndexIn, 0u, "CLIPINDEX"));
  builder.add(Op::Semantic(colorIn, 0u, "COLOR"));
  builder.add(Op::Semantic(coordIn, 1u, "TEXCOORD"));

  auto o0 = builder.add(Op::DclOutput(Type(ScalarType::eF32, 4u), entryPoint, 0u, 0u));
  auto o1 = builder.add(Op::DclOutput(Type(ScalarType::eF32, 1u), entryPoint, 1u, 0u));
  auto o2 = builder.add(Op::DclOutput(Type(ScalarType::eF32, 2u), entryPoint, 2u, 0u));
  auto o3 = builder.add(Op::DclOutput(Type(ScalarType::eU32, 1u), entryPoint, 3u, 0u));
  auto o4 = builder.add(Op::DclOutput(Type(ScalarType::eF32, 4u), entryPoint, 4u, 0u));
  auto o5 = builder.add(Op::DclOutput(Type(ScalarType::eF32, 2u), entryPoint, 5u, 0u));
  auto o6 = builder.add(Op::DclOutput(Type(ScalarType::eF32, 3u), entryPoint, 6u, 0u));
  auto oDepth = builder.add(Op::DclOutputBuiltIn(ScalarType::eF32, entryPoint, BuiltIn::eDepth));

  builder.add(Op::Semantic(o0, 0u, "SV_TARGET"));
  builder.add(Op::Semantic(o1, 1u, "SV_TARGET"));
  builder.add(Op::Semantic(o2, 2u, "SV_TARGET"));
  builder.add(Op::Semantic(o3, 3u, "SV_TARGET"));
  builder.add(Op::Semantic(o4, 4u, "SV_TARGET"));
  builder.add(Op::Semantic(o5, 5u, "SV_TARGET"));
  builder.add(Op::Semantic(o6, 6u, "SV_TARGET"));
  builder.add(Op::Semantic(oDepth, 5u, "SV_DEPTH"));

  builder.add(Op::Label());

  builder.add(Op::OutputStore(o0, SsaDef(),
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 4u), posIn, SsaDef()))));

  auto clipIndex = builder.add(Op::InputLoad(ScalarType::eU32, clipIndexIn, SsaDef()));
  auto primIndex = builder.add(Op::InputLoad(ScalarType::eU32, primIn, SsaDef()));

  clipIndex = builder.add(Op::IXor(ScalarType::eU32, clipIndex, primIndex));

  builder.add(Op::OutputStore(o1, SsaDef(),
    builder.add(Op::InputLoad(ScalarType::eF32, clipIn, clipIndex))));

  for (uint32_t i = 0u; i < 2u; i++) {
    builder.add(Op::OutputStore(o2, builder.makeConstant(i),
      builder.add(Op::InputLoad(Type(ScalarType::eF32, 1u), cullIn, builder.makeConstant(i)))));
  }

  builder.add(Op::OutputStore(o3, SsaDef(), primIndex));

  for (uint32_t i = 0u; i < 4u; i++) {
    builder.add(Op::OutputStore(o4, builder.makeConstant(i),
      builder.add(Op::InputLoad(ScalarType::eF32, colorIn, builder.makeConstant(i)))));
  }

  builder.add(Op::OutputStore(o5, SsaDef(),
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 2u), coordIn, SsaDef()))));

  builder.add(Op::OutputStore(o6, SsaDef(),
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 3u), normalIn, SsaDef()))));

  builder.add(Op::OutputStore(oDepth, SsaDef(),
    builder.add(Op::InputLoad(ScalarType::eF32, depthIn, SsaDef()))));
  builder.add(Op::Return());

  return run_io_mismatch_passes(builder, prevStage, map);
}


Builder test_pass_lower_io_mismatch_vs_basic() {
  IoMap inputs = { };

  /* regular inputs */
  inputs.add(IoLocation(IoEntryType::ePerVertex, 0u, 0xfu));
  inputs.add(IoLocation(IoEntryType::ePerVertex, 1u, 0xfu));
  inputs.add(IoLocation(IoEntryType::ePerVertex, 2u, 0xfu));

  /* unused input */
  inputs.add(IoLocation(IoEntryType::ePerVertex, 3u, 0xfu));

  return test_io_mismatch_vs(inputs);
}


Builder test_pass_lower_io_mismatch_vs_missing_input() {
  IoMap inputs = { };

  /* Omit input 1 */
  inputs.add(IoLocation(IoEntryType::ePerVertex, 0u, 0xfu));
  inputs.add(IoLocation(IoEntryType::ePerVertex, 2u, 0xfu));

  /* unused input */
  inputs.add(IoLocation(IoEntryType::ePerVertex, 3u, 0xfu));

  return test_io_mismatch_vs(inputs);
}



Builder test_pass_lower_io_mismatch_hs_basic() {
  IoMap prevOut = { };

  /* Exact match for everything */
  prevOut.add(IoLocation(BuiltIn::ePosition, 0xfu));

  prevOut.add(IoLocation(IoEntryType::ePerVertex, 1u, 0x7u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 2u, 0x7u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 3u, 0x1u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 3u, 0x2u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 5u, 0xfu));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 6u, 0xfu));

  return test_io_mismatch_hs(prevOut);
}


Builder test_pass_lower_io_mismatch_hs_missing_input() {
  IoMap prevOut = { };

  /* Omit position, add unused builtin instead */
  prevOut.add(IoLocation(BuiltIn::eClipDistance, 0xfu));

  /* Split up vec3 into two vec2s */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 1u, 0x3u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 1u, 0xcu));

  /* Scalarize and omit one component */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 2u, 0x1u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 2u, 0x4u));

  /* Omit first component, enlarge second */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 3u, 0x6u));

  /* Use partial component masks for these */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 5u, 0x3u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 6u, 0xcu));

  return test_io_mismatch_hs(prevOut);
}


Builder test_pass_lower_io_mismatch_ds_basic() {
  IoMap prevOut = { };

  /* Irrelevant, kind of */
  prevOut.add(IoLocation(BuiltIn::eTessFactorOuter, 0xfu));
  prevOut.add(IoLocation(BuiltIn::eTessFactorInner, 0x3u));

  /* Exact match for everything */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 0u, 0xfu));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 2u, 0x7u));

  prevOut.add(IoLocation(IoEntryType::ePerPatch, 0u, 0x1u));
  prevOut.add(IoLocation(IoEntryType::ePerPatch, 0u, 0x2u));
  prevOut.add(IoLocation(IoEntryType::ePerPatch, 1u, 0xfu));
  prevOut.add(IoLocation(IoEntryType::ePerPatch, 8u, 0xfu));

  return test_io_mismatch_ds(prevOut);
}


Builder test_pass_lower_io_mismatch_ds_missing_input() {
  IoMap prevOut = { };

  /* Irrelevant, kind of */
  prevOut.add(IoLocation(BuiltIn::eTessFactorOuter, 0xfu));
  prevOut.add(IoLocation(BuiltIn::eTessFactorInner, 0x3u));

  /* Omit position components and color input */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 0u, 0x3u));

  /* Missing second component */
  prevOut.add(IoLocation(IoEntryType::ePerPatch, 0u, 0x1u));

  /* Partially defined components */
  prevOut.add(IoLocation(IoEntryType::ePerPatch, 1u, 0x6u));

  /* Wrong class of input */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 8u, 0xfu));

  return test_io_mismatch_ds(prevOut);
}


Builder test_pass_lower_io_mismatch_ds_straddle_input() {
  IoMap prevOut = { };

  /* Irrelevant, kind of */
  prevOut.add(IoLocation(BuiltIn::eTessFactorOuter, 0xfu));
  prevOut.add(IoLocation(BuiltIn::eTessFactorInner, 0x3u));

  /* Split up input */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 0u, 0x1u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 0u, 0x4u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 0u, 0x8u));

  /* Full component mismatch */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 2u, 0xeu));

  /* Merged into one vector */
  prevOut.add(IoLocation(IoEntryType::ePerPatch, 0u, 0xfu));

  /* Split up patch constant */
  prevOut.add(IoLocation(IoEntryType::ePerPatch, 1u, 0x3u));
  prevOut.add(IoLocation(IoEntryType::ePerPatch, 1u, 0x8u));

  /* Another partial input test */
  prevOut.add(IoLocation(IoEntryType::ePerPatch, 8u, 0xeu));

  return test_io_mismatch_ds(prevOut);
}



Builder test_pass_lower_io_mismatch_gs_basic() {
  IoMap prevOut = { };

  /* Exact match for outputs */
  prevOut.add(IoLocation(BuiltIn::ePosition, 0xfu));
  prevOut.add(IoLocation(BuiltIn::eClipDistance, 0x3u));

  /* Unused built-in */
  prevOut.add(IoLocation(BuiltIn::eLayerIndex, 0x1u));

  /* Exact match */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 1u, 0x3u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 1u, 0xcu));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 2u, 0xfu));

  /* Unused location */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 3u, 0xfu));

  return test_io_mismatch_gs(ShaderStage::eVertex, prevOut);
}


Builder test_pass_lower_io_mismatch_gs_missing_input() {
  IoMap prevOut = { };

  /* Omit position input */
  prevOut.add(IoLocation(BuiltIn::eClipDistance, 0x3u));

  /* Omit one of the regular inputs */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 1u, 0x3u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 2u, 0xfu));

  return test_io_mismatch_gs(ShaderStage::eVertex, prevOut);
}


Builder test_pass_lower_io_mismatch_gs_partial_input() {
  IoMap prevOut = { };

  /* Make clip distance smol */
  prevOut.add(IoLocation(BuiltIn::ePosition, 0xfu));
  prevOut.add(IoLocation(BuiltIn::eClipDistance, 0x1u));

  /* Omit components for each input */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 1u, 0x2u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 1u, 0x4u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 2u, 0x6u));

  return test_io_mismatch_gs(ShaderStage::eVertex, prevOut);
}


Builder test_pass_lower_io_mismatch_gs_straddle() {
  IoMap prevOut = { };

  /* Make clip distance larger */
  prevOut.add(IoLocation(BuiltIn::ePosition, 0xfu));
  prevOut.add(IoLocation(BuiltIn::eClipDistance, 0xfu));

  /* Rewrite split input as scalar + vec3 */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 1u, 0x1u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 1u, 0xeu));

  /* Rewrite vec4 input as vec2 + two scalars */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 2u, 0x3u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 2u, 0x4u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 2u, 0x8u));

  return test_io_mismatch_gs(ShaderStage::eVertex, prevOut);
}



Builder test_pass_lower_io_mismatch_ps_basic() {
  IoMap prevOut = { };

  /* Irrelevant */
  prevOut.add(IoLocation(BuiltIn::ePosition, 0xfu));

  /* Exact matches for built-ins */
  prevOut.add(IoLocation(BuiltIn::eClipDistance, 0x1fu));
  prevOut.add(IoLocation(BuiltIn::eCullDistance, 0x3u));
  prevOut.add(IoLocation(BuiltIn::ePrimitiveId, 0x1u));

  /* Unused built-in */
  prevOut.add(IoLocation(BuiltIn::eLayerIndex, 0x1u));

  /* Unused location */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 2u, 0xfu));

  /* Exact match */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 3u, 0x7u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 3u, 0x8u));

  /* Oversized output, this is fine */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 4u, 0x7u));

  /* Exact match */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 5u, 0xfu));

  /* One unused coord, one used part */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 6u, 0x3u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 6u, 0xcu));

  return test_io_mismatch_ps(ShaderStage::eGeometry, prevOut);
}


Builder test_pass_lower_io_mismatch_ps_missing_input() {
  IoMap prevOut = { };

  /* Irrelevant */
  prevOut.add(IoLocation(BuiltIn::ePosition, 0xfu));

  /* Exact matches for built-ins */
  prevOut.add(IoLocation(BuiltIn::eClipDistance, 0x1fu));
  prevOut.add(IoLocation(BuiltIn::eCullDistance, 0x3u));

  /* Omit normal part */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 3u, 0x8u));

  /* Exact match */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 4u, 0x1u));

  /* Omit location 5 entirely */
  /* Unused coord, omit used part */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 6u, 0x3u));

  return test_io_mismatch_ps(ShaderStage::eVertex, prevOut);
}


Builder test_pass_lower_io_mismatch_ps_partial_input() {
  IoMap prevOut = { };

  /* Irrelevant */
  prevOut.add(IoLocation(BuiltIn::ePosition, 0xfu));

  /* Exact matches for built-ins */
  prevOut.add(IoLocation(BuiltIn::eClipDistance, 0x1fu));
  prevOut.add(IoLocation(BuiltIn::eCullDistance, 0x3u));

  /* Omit two components of normal part */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 3u, 0x1u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 3u, 0x8u));

  /* Oversized output, this is fine */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 4u, 0x7u));

  /* Omit first component of color input */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 5u, 0xeu));

  /* Mismatch and omit last component of coord */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 6u, 0x7u));

  return test_io_mismatch_ps(ShaderStage::eVertex, prevOut);
}


Builder test_pass_lower_io_mismatch_ps_missing_builtin() {
  IoMap prevOut = { };

  /* Irrelevant */
  prevOut.add(IoLocation(BuiltIn::ePosition, 0xfu));

  /* Omit clip distance and primitive ID */
  prevOut.add(IoLocation(BuiltIn::eCullDistance, 0x3u));

  /* Unused built-in */
  prevOut.add(IoLocation(BuiltIn::eLayerIndex, 0x1u));

  /* Exact match */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 3u, 0x7u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 3u, 0x8u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 4u, 0x1u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 5u, 0xfu));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 6u, 0xcu));

  return test_io_mismatch_ps(ShaderStage::eGeometry, prevOut);
}


Builder test_pass_lower_io_mismatch_ps_straddle() {
  IoMap prevOut = { };

  /* Irrelevant */
  prevOut.add(IoLocation(BuiltIn::ePosition, 0xfu));

  /* Exact matches for built-ins */
  prevOut.add(IoLocation(BuiltIn::eClipDistance, 0x1fu));
  prevOut.add(IoLocation(BuiltIn::eCullDistance, 0x3u));
  prevOut.add(IoLocation(BuiltIn::ePrimitiveId, 0x1u));

  /* Rewrite vec3 + scalar as scalar + vec3 */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 3u, 0x1u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 3u, 0xeu));

  /* Oversized output, this is fine */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 4u, 0xfu));

  /* Scalarize output and omit 3rd component */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 5u, 0x1u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 5u, 0x2u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 5u, 0x8u));

  /* Rewrite input as full vec4 */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 6u, 0xfu));

  return test_io_mismatch_ps(ShaderStage::eGeometry, prevOut);
}


Builder test_pass_lower_io_mismatch_ps_clip_distance_small() {
  IoMap prevOut = { };

  /* Irrelevant */
  prevOut.add(IoLocation(BuiltIn::ePosition, 0xfu));

  /* Make clip distance smaller but cull distance larger */
  prevOut.add(IoLocation(BuiltIn::eClipDistance, 0x7u));
  prevOut.add(IoLocation(BuiltIn::eCullDistance, 0xfu));

  /* Exact match */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 3u, 0x7u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 3u, 0x8u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 4u, 0x1u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 5u, 0xfu));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 6u, 0xcu));

  return test_io_mismatch_ps(ShaderStage::eVertex, prevOut);
}


Builder test_pass_lower_io_mismatch_ps_clip_distance_large() {
  IoMap prevOut = { };

  /* Irrelevant */
  prevOut.add(IoLocation(BuiltIn::ePosition, 0xfu));

  /* Make clip distance larger but cull distance smaller */
  prevOut.add(IoLocation(BuiltIn::eClipDistance, 0x7fu));
  prevOut.add(IoLocation(BuiltIn::eCullDistance, 0x1u));

  /* Exact match */
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 3u, 0x7u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 3u, 0x8u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 4u, 0x1u));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 5u, 0xfu));
  prevOut.add(IoLocation(IoEntryType::ePerVertex, 6u, 0xcu));

  return test_io_mismatch_ps(ShaderStage::eVertex, prevOut);
}


Builder test_misc_ps() {
  Builder builder;

  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  auto pos = builder.add(Op::DclInputBuiltIn(Type(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition, InterpolationModes()));
  auto clip = builder.add(Op::DclInputBuiltIn(Type(ScalarType::eF32).addArrayDimension(4u), entryPoint, BuiltIn::eClipDistance, InterpolationModes()));
  auto color = builder.add(Op::DclInput(Type(ScalarType::eF32, 4u), entryPoint, 1u, 0u, InterpolationModes()));
  auto coord = builder.add(Op::DclInput(Type(ScalarType::eF32, 2u), entryPoint, 2u, 0u, InterpolationMode::eNoPerspective));
  auto depth = builder.add(Op::DclInput(Type(ScalarType::eF32, 1u), entryPoint, 3u, 0u, InterpolationMode::eNoPerspective));
  auto index = builder.add(Op::DclInput(Type(ScalarType::eU32, 1u), entryPoint, 4u, 0u, InterpolationMode::eFlat));
  auto base  = builder.add(Op::DclInput(Type(ScalarType::eI32, 3u), entryPoint, 4u, 1u, InterpolationMode::eFlat));
  auto centroid = builder.add(Op::DclInput(Type(ScalarType::eF32, 4u), entryPoint, 5u, 0u, InterpolationMode::eCentroid));

  builder.add(Op::Semantic(pos, 0u, "SV_POSITION"));
  builder.add(Op::Semantic(clip, 0u, "SV_CLIPDISTANCE"));
  builder.add(Op::Semantic(color, 0u, "COLOR"));
  builder.add(Op::Semantic(coord, 0u, "TEXCOORD"));
  builder.add(Op::Semantic(depth, 0u, "DEPTH"));
  builder.add(Op::Semantic(index, 0u, "INDEX"));
  builder.add(Op::Semantic(base, 0u, "BASE"));
  builder.add(Op::Semantic(centroid, 0u, "CENTROID"));

  auto o0 = builder.add(Op::DclOutput(Type(ScalarType::eF32, 4u), entryPoint, 0u, 0u));
  auto o1 = builder.add(Op::DclOutput(Type(ScalarType::eF32, 4u), entryPoint, 1u, 0u));
  auto o2 = builder.add(Op::DclOutput(Type(ScalarType::eF32, 4u), entryPoint, 2u, 0u));
  auto o3 = builder.add(Op::DclOutput(Type(ScalarType::eF32, 2u), entryPoint, 3u, 0u));
  auto o4 = builder.add(Op::DclOutput(Type(ScalarType::eU32, 1u), entryPoint, 4u, 0u));
  auto o5 = builder.add(Op::DclOutput(Type(ScalarType::eI32, 3u), entryPoint, 5u, 0u));
  auto o6 = builder.add(Op::DclOutput(Type(ScalarType::eF32, 4u), entryPoint, 6u, 0u));
  auto oDepth = builder.add(Op::DclOutputBuiltIn(ScalarType::eF32, entryPoint, BuiltIn::eDepth));

  builder.add(Op::Semantic(o0, 0u, "SV_TARGET"));
  builder.add(Op::Semantic(o1, 1u, "SV_TARGET"));
  builder.add(Op::Semantic(o2, 2u, "SV_TARGET"));
  builder.add(Op::Semantic(o3, 3u, "SV_TARGET"));
  builder.add(Op::Semantic(o4, 4u, "SV_TARGET"));
  builder.add(Op::Semantic(o5, 5u, "SV_TARGET"));
  builder.add(Op::Semantic(o6, 6u, "SV_TARGET"));
  builder.add(Op::Semantic(oDepth, 6u, "SV_DEPTH"));

  builder.add(Op::Label());

  builder.add(Op::OutputStore(o0, SsaDef(),
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 4u), pos, SsaDef()))));

  for (uint32_t i = 0u; i < 4u; i++) {
    builder.add(Op::OutputStore(o1, builder.makeConstant(i),
      builder.add(Op::InputLoad(ScalarType::eF32, pos, builder.makeConstant(i)))));
  }

  builder.add(Op::OutputStore(o2, SsaDef(),
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 4u), color, SsaDef()))));

  builder.add(Op::OutputStore(o3, SsaDef(),
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 2u), coord, SsaDef()))));

  builder.add(Op::OutputStore(o4, SsaDef(),
    builder.add(Op::InputLoad(ScalarType::eU32, index, SsaDef()))));

  builder.add(Op::OutputStore(o5, SsaDef(),
    builder.add(Op::InputLoad(Type(ScalarType::eI32, 3u), base, SsaDef()))));

  builder.add(Op::OutputStore(o6, SsaDef(),
    builder.add(Op::InputLoad(Type(ScalarType::eF32, 4u), centroid, SsaDef()))));

  builder.add(Op::OutputStore(oDepth, SsaDef(),
    builder.add(Op::InputLoad(ScalarType::eF32, depth, SsaDef()))));

  builder.add(Op::Return());
  return builder;
}


Builder test_pass_lower_io_enable_flat_shading() {
  auto ps = test_misc_ps();

  /* Flatten locations 0, 1, 2, 4, 5 */
  LowerIoPass(ps).enableFlatInterpolation(0x37u);
  return ps;
}


Builder test_pass_lower_io_enable_sample_shading() {
  auto ps = test_misc_ps();

  LowerIoPass(ps).enableSampleInterpolation();
  return ps;
}


Builder test_pass_lower_io_swizzle_rt() {
  auto ps = test_misc_ps();

  std::array<IoOutputSwizzle, 8u> swizzles = { };
  swizzles.at(1u) = { IoOutputComponent::eY, IoOutputComponent::eW, IoOutputComponent::eZ, IoOutputComponent::eX };
  swizzles.at(2u) = { IoOutputComponent::eOne, IoOutputComponent::eX, IoOutputComponent::eZero, IoOutputComponent::eY };
  swizzles.at(3u) = { IoOutputComponent::eY, IoOutputComponent::eZ, IoOutputComponent::eW, IoOutputComponent::eX };
  swizzles.at(4u) = { IoOutputComponent::eX, IoOutputComponent::eX, IoOutputComponent::eX, IoOutputComponent::eX };
  swizzles.at(5u) = { IoOutputComponent::eW, IoOutputComponent::eOne, IoOutputComponent::eY, IoOutputComponent::eZ };
  swizzles.at(6u) = { IoOutputComponent::eW, IoOutputComponent::eW, IoOutputComponent::eW, IoOutputComponent::eW };

  LowerIoPass(ps).swizzleOutputs(swizzles.size(), swizzles.data());
  return ps;
}

}
