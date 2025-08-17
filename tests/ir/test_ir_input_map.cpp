#include "../test_common.h"

#include "../../ir/passes/ir_pass_lower_io.h"

namespace dxbc_spv::tests::ir {

using namespace dxbc_spv::ir;

void testIrInputMapIoLocation() {
  IoLocation a(BuiltIn::eClipDistance, 0xfu);
  ok(a.getType() == IoEntryType::eBuiltIn);
  ok(a.getBuiltIn() == BuiltIn::eClipDistance);
  ok(a.getComponentMask() == 0xfu);
  ok(a.getFirstComponentBit() == 0x1u);
  ok(a.getFirstComponentIndex() == 0u);
  ok(a.computeComponentCount() == 4u);

  IoLocation b(IoEntryType::ePerVertex, 2u, 0x6u);
  ok(b.getType() == IoEntryType::ePerVertex);
  ok(b.getLocationIndex() == 2u);
  ok(b.getComponentMask() == 0x6u);
  ok(b.getFirstComponentBit() == 0x2u);
  ok(b.getFirstComponentIndex() == 1u);
  ok(b.computeComponentCount() == 2u);
  ok(b.isOrderedBefore(a));

  IoLocation c(IoEntryType::ePerPatch, 2u, 0x6u);
  ok(c.getType() == IoEntryType::ePerPatch);
  ok(b.isOrderedBefore(c));

  IoLocation d(IoEntryType::ePerPatch, 2u, 0xeu);
  ok(d.getType() == IoEntryType::ePerPatch);
  ok(d.getLocationIndex() == 2u);
  ok(d.getComponentMask() == 0xeu);
  ok(d.getFirstComponentBit() == 0x2u);
  ok(d.getFirstComponentIndex() == 1u);
  ok(d.computeComponentCount() == 3u);

  ok(!c.isOrderedBefore(d));
  ok(!d.isOrderedBefore(c));

  ok(!a.overlaps(b));
  ok(!b.overlaps(c));

  ok(c.overlaps(d));
  ok(d.overlaps(c));

  ok(!c.covers(d));
  ok(d.covers(c));
}


void testIrInputMapEntryFromOp() {
  SsaDef entryPoint(1u);

  auto e = IoMap::getEntryForOp(ShaderStage::eVertex,
    Op::DclInput(BasicType(ScalarType::eF32, 4u), entryPoint, 3u, 0u));
  ok(e.getType() == IoEntryType::ePerVertex);
  ok(e.getLocationIndex() == 3u);
  ok(e.getComponentMask() == 0xfu);

  e = IoMap::getEntryForOp(ShaderStage::ePixel,
    Op::DclOutput(BasicType(ScalarType::eF32, 3u), entryPoint, 2u, 0u));
  ok(e.getType() == IoEntryType::ePerVertex);
  ok(e.getLocationIndex() == 2u);
  ok(e.getComponentMask() == 0x7u);

  e = IoMap::getEntryForOp(ShaderStage::eGeometry,
    Op::DclInput(Type(ScalarType::eI32, 1u).addArrayDimension(3u), entryPoint, 1u, 3u));
  ok(e.getType() == IoEntryType::ePerVertex);
  ok(e.getLocationIndex() == 1u);
  ok(e.getComponentMask() == 0x8u);

  e = IoMap::getEntryForOp(ShaderStage::eHull,
    Op::DclInput(Type(ScalarType::eI32, 2u).addArrayDimension(32u), entryPoint, 4u, 1u));
  ok(e.getType() == IoEntryType::ePerVertex);
  ok(e.getLocationIndex() == 4u);
  ok(e.getComponentMask() == 0x6u);

  e = IoMap::getEntryForOp(ShaderStage::eHull,
    Op::DclOutput(Type(ScalarType::eF32, 1u).addArrayDimension(3u), entryPoint, 6u, 2u));
  ok(e.getType() == IoEntryType::ePerVertex);
  ok(e.getLocationIndex() == 6u);
  ok(e.getComponentMask() == 0x4u);

  e = IoMap::getEntryForOp(ShaderStage::eHull,
    Op::DclOutput(Type(ScalarType::eF32, 4u), entryPoint, 19u, 0u));
  ok(e.getType() == IoEntryType::ePerPatch);
  ok(e.getLocationIndex() == 19u);
  ok(e.getComponentMask() == 0xfu);

  e = IoMap::getEntryForOp(ShaderStage::eDomain,
    Op::DclInput(Type(ScalarType::eF32, 1u).addArrayDimension(3u), entryPoint, 6u, 2u));
  ok(e.getType() == IoEntryType::ePerVertex);
  ok(e.getLocationIndex() == 6u);
  ok(e.getComponentMask() == 0x4u);

  e = IoMap::getEntryForOp(ShaderStage::eDomain,
    Op::DclInput(Type(ScalarType::eF32, 4u), entryPoint, 19u, 0u));
  ok(e.getType() == IoEntryType::ePerPatch);
  ok(e.getLocationIndex() == 19u);
  ok(e.getComponentMask() == 0xfu);

  e = IoMap::getEntryForOp(ShaderStage::eDomain,
    Op::DclOutput(Type(ScalarType::eI32, 2u), entryPoint, 4u, 1u));
  ok(e.getType() == IoEntryType::ePerVertex);
  ok(e.getLocationIndex() == 4u);
  ok(e.getComponentMask() == 0x6u);

  e = IoMap::getEntryForOp(ShaderStage::eHull,
    Op::DclOutputBuiltIn(Type(ScalarType::eF32).addArrayDimension(2u), entryPoint, BuiltIn::eTessFactorInner));
  ok(e.getType() == IoEntryType::eBuiltIn);
  ok(e.getBuiltIn() == BuiltIn::eTessFactorInner);
  ok(e.getComponentMask() == 0x3u);

  e = IoMap::getEntryForOp(ShaderStage::eDomain,
    Op::DclInputBuiltIn(Type(ScalarType::eF32).addArrayDimension(2u), entryPoint, BuiltIn::eTessFactorInner));
  ok(e.getType() == IoEntryType::eBuiltIn);
  ok(e.getBuiltIn() == BuiltIn::eTessFactorInner);
  ok(e.getComponentMask() == 0x3u);

  e = IoMap::getEntryForOp(ShaderStage::ePixel,
    Op::DclOutputBuiltIn(Type(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition));
  ok(e.getType() == IoEntryType::eBuiltIn);
  ok(e.getBuiltIn() == BuiltIn::ePosition);
  ok(e.getComponentMask() == 0xfu);

  e = IoMap::getEntryForOp(ShaderStage::eGeometry,
    Op::DclOutputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::ePrimitiveId));
  ok(e.getType() == IoEntryType::eBuiltIn);
  ok(e.getBuiltIn() == BuiltIn::ePrimitiveId);
  ok(e.getComponentMask() == 0x1u);

  e = IoMap::getEntryForOp(ShaderStage::eVertex,
    Op::DclOutputBuiltIn(Type(ScalarType::eU32).addArrayDimensions(8u), entryPoint, BuiltIn::eClipDistance));
  ok(e.getType() == IoEntryType::eBuiltIn);
  ok(e.getBuiltIn() == BuiltIn::eClipDistance);
  ok(e.getComponentMask() == 0xffu);
}


void testIrInputMapEntryFromBuilder() {
  Builder b;

  auto function = b.add(Op::Function(ScalarType::eVoid));
  b.add(Op::Label());
  b.add(Op::Return());
  b.add(Op::FunctionEnd());

  auto entryPoint = b.add(Op::EntryPoint(function, ShaderStage::eDomain));
  b.add(Op::DclInput(Type(ScalarType::eF32, 2u).addArrayDimension(32u), entryPoint, 1u, 2u));
  b.add(Op::DclInputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::ePrimitiveId));
  b.add(Op::DclInput(Type(ScalarType::eU32, 2u).addArrayDimension(32u), entryPoint, 1u, 0u));
  b.add(Op::DclInputBuiltIn(Type(ScalarType::eF32, 3u), entryPoint, BuiltIn::eTessCoord));
  b.add(Op::DclInput(Type(ScalarType::eF32, 4u), entryPoint, 1u, 0u));
  b.add(Op::DclInputBuiltIn(Type(ScalarType::eF32).addArrayDimension(2u), entryPoint, BuiltIn::eTessFactorInner));
  b.add(Op::DclInput(Type(ScalarType::eF32, 3u).addArrayDimension(32u), entryPoint, 0u, 0u));
  b.add(Op::DclInput(ScalarType::eF32, entryPoint, 0u, 0u));
  b.add(Op::DclInputBuiltIn(Type(ScalarType::eF32).addArrayDimension(4u), entryPoint, BuiltIn::eTessFactorOuter));
  b.add(Op::DclInput(Type(ScalarType::eU32, 3u), entryPoint, 2u, 0u));
  b.add(Op::DclInput(ScalarType::eU32, entryPoint, 2u, 3u));

  b.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32).addArrayDimension(5u), entryPoint, BuiltIn::eClipDistance));
  b.add(Op::DclOutput(Type(ScalarType::eF32, 1u), entryPoint, 2u, 0u));
  b.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32).addArrayDimension(3u), entryPoint, BuiltIn::eCullDistance));
  b.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition));
  b.add(Op::DclOutput(Type(ScalarType::eU32, 2u), entryPoint, 1u, 0u));
  b.add(Op::DclOutput(Type(ScalarType::eF32, 3u), entryPoint, 0u, 0u));
  b.add(Op::DclOutput(Type(ScalarType::eF32, 3u), entryPoint, 2u, 1u));

  IoMap inputMap = IoMap::forInputs(b);
  ok(inputMap.getCount() == 11u);

  for (uint32_t i = 1u; i < inputMap.getCount(); i++)
    ok(inputMap.get(i - 1u).isOrderedBefore(inputMap.get(i)));

  ok(inputMap.get(0u).getType() == IoEntryType::ePerVertex);
  ok(inputMap.get(0u).getLocationIndex() == 0u);
  ok(inputMap.get(0u).getComponentMask() == 0x7u);

  ok(inputMap.get(1u).getType() == IoEntryType::ePerVertex);
  ok(inputMap.get(1u).getLocationIndex() == 1u);
  ok(inputMap.get(1u).getComponentMask() == 0x3u);

  ok(inputMap.get(2u).getType() == IoEntryType::ePerVertex);
  ok(inputMap.get(2u).getLocationIndex() == 1u);
  ok(inputMap.get(2u).getComponentMask() == 0xcu);

  ok(inputMap.get(3u).getType() == IoEntryType::ePerPatch);
  ok(inputMap.get(3u).getLocationIndex() == 0u);
  ok(inputMap.get(3u).getComponentMask() == 0x1u);

  ok(inputMap.get(4u).getType() == IoEntryType::ePerPatch);
  ok(inputMap.get(4u).getLocationIndex() == 1u);
  ok(inputMap.get(4u).getComponentMask() == 0xfu);

  ok(inputMap.get(5u).getType() == IoEntryType::ePerPatch);
  ok(inputMap.get(5u).getLocationIndex() == 2u);
  ok(inputMap.get(5u).getComponentMask() == 0x7u);

  ok(inputMap.get(6u).getType() == IoEntryType::ePerPatch);
  ok(inputMap.get(6u).getLocationIndex() == 2u);
  ok(inputMap.get(6u).getComponentMask() == 0x8u);

  ok(inputMap.get(7u).getType() == IoEntryType::eBuiltIn);
  ok(inputMap.get(7u).getBuiltIn() == BuiltIn::ePrimitiveId);
  ok(inputMap.get(7u).getComponentMask() == 0x1u);

  ok(inputMap.get(8u).getType() == IoEntryType::eBuiltIn);
  ok(inputMap.get(8u).getBuiltIn() == BuiltIn::eTessCoord);
  ok(inputMap.get(8u).getComponentMask() == 0x7u);

  ok(inputMap.get(9u).getType() == IoEntryType::eBuiltIn);
  ok(inputMap.get(9u).getBuiltIn() == BuiltIn::eTessFactorInner);
  ok(inputMap.get(9u).getComponentMask() == 0x3u);

  ok(inputMap.get(10u).getType() == IoEntryType::eBuiltIn);
  ok(inputMap.get(10u).getBuiltIn() == BuiltIn::eTessFactorOuter);
  ok(inputMap.get(10u).getComponentMask() == 0xfu);

  IoMap outputMap = IoMap::forOutputs(b, 0u);
  ok(outputMap.getCount() == 7u);

  for (uint32_t i = 1u; i < outputMap.getCount(); i++)
    ok(outputMap.get(i - 1u).isOrderedBefore(outputMap.get(i)));

  ok(outputMap.get(0u).getType() == IoEntryType::ePerVertex);
  ok(outputMap.get(0u).getLocationIndex() == 0u);
  ok(outputMap.get(0u).getComponentMask() == 0x7u);

  ok(outputMap.get(1u).getType() == IoEntryType::ePerVertex);
  ok(outputMap.get(1u).getLocationIndex() == 1u);
  ok(outputMap.get(1u).getComponentMask() == 0x3u);

  ok(outputMap.get(2u).getType() == IoEntryType::ePerVertex);
  ok(outputMap.get(2u).getLocationIndex() == 2u);
  ok(outputMap.get(2u).getComponentMask() == 0x1u);

  ok(outputMap.get(3u).getType() == IoEntryType::ePerVertex);
  ok(outputMap.get(3u).getLocationIndex() == 2u);
  ok(outputMap.get(3u).getComponentMask() == 0xeu);

  ok(outputMap.get(4u).getType() == IoEntryType::eBuiltIn);
  ok(outputMap.get(4u).getBuiltIn() == BuiltIn::ePosition);
  ok(outputMap.get(4u).getComponentMask() == 0xfu);

  ok(outputMap.get(5u).getType() == IoEntryType::eBuiltIn);
  ok(outputMap.get(5u).getBuiltIn() == BuiltIn::eClipDistance);
  ok(outputMap.get(5u).getComponentMask() == 0x1fu);

  ok(outputMap.get(6u).getType() == IoEntryType::eBuiltIn);
  ok(outputMap.get(6u).getBuiltIn() == BuiltIn::eCullDistance);
  ok(outputMap.get(6u).getComponentMask() == 0x7u);
}


void testIrInputMapCompatibility() {
  IoMap outMap;
  outMap.add(IoLocation(BuiltIn::eClipDistance, 0x3fu));
  outMap.add(IoLocation(BuiltIn::ePosition, 0xfu));
  outMap.add(IoLocation(IoEntryType::ePerVertex, 0u, 0x7u));
  outMap.add(IoLocation(IoEntryType::ePerVertex, 0u, 0x8u));
  outMap.add(IoLocation(IoEntryType::ePerVertex, 1u, 0xfu));
  outMap.add(IoLocation(IoEntryType::ePerVertex, 2u, 0x1u));
  outMap.add(IoLocation(IoEntryType::ePerVertex, 2u, 0x2u));
  outMap.add(IoLocation(IoEntryType::ePerVertex, 2u, 0x4u));

  IoMap inMap;
  ok(IoMap::checkCompatibility(ShaderStage::eVertex, outMap, ShaderStage::ePixel, inMap));

  /* Basic match */
  inMap.add(IoLocation(BuiltIn::ePosition, 0xfu));
  ok(IoMap::checkCompatibility(ShaderStage::eVertex, outMap, ShaderStage::ePixel, inMap));

  /* Always defined when consuming vertex shader */
  inMap.add(IoLocation(BuiltIn::ePrimitiveId, 0xfu));
  ok(IoMap::checkCompatibility(ShaderStage::eVertex, outMap, ShaderStage::ePixel, inMap));

  /* GS must explicitly write it */
  inMap.add(IoLocation(BuiltIn::ePrimitiveId, 0xfu));
  ok(!IoMap::checkCompatibility(ShaderStage::eGeometry, outMap, ShaderStage::ePixel, inMap));

  /* Full and partial matches for regular I/O locations */
  inMap.add(IoLocation(IoEntryType::ePerVertex, 0u, 0x8u));
  ok(IoMap::checkCompatibility(ShaderStage::eVertex, outMap, ShaderStage::ePixel, inMap));

  inMap.add(IoLocation(IoEntryType::ePerVertex, 0u, 0x3u));
  ok(IoMap::checkCompatibility(ShaderStage::eVertex, outMap, ShaderStage::ePixel, inMap));

  inMap.add(IoLocation(IoEntryType::ePerVertex, 2u, 0x1u));
  ok(IoMap::checkCompatibility(ShaderStage::eVertex, outMap, ShaderStage::ePixel, inMap));

  /* Straddling multiple outputs */
  inMap = IoMap();
  inMap.add(IoLocation(IoEntryType::ePerVertex, 2u, 0x6u));
  ok(!IoMap::checkCompatibility(ShaderStage::eVertex, outMap, ShaderStage::ePixel, inMap));

  /* Cull distance is not written at all */
  inMap = IoMap();
  inMap.add(IoLocation(BuiltIn::eCullDistance, 0x3u));
  ok(!IoMap::checkCompatibility(ShaderStage::eVertex, outMap, ShaderStage::ePixel, inMap));

  /* Clip distance mask must match exactly */
  inMap = IoMap();
  inMap.add(IoLocation(BuiltIn::eClipDistance, 0x3fu));
  ok(IoMap::checkCompatibility(ShaderStage::eVertex, outMap, ShaderStage::ePixel, inMap));

  inMap = IoMap();
  inMap.add(IoLocation(BuiltIn::eClipDistance, 0xfu));
  ok(!IoMap::checkCompatibility(ShaderStage::eVertex, outMap, ShaderStage::ePixel, inMap));

  inMap = IoMap();
  inMap.add(IoLocation(BuiltIn::eClipDistance, 0xffu));
  ok(!IoMap::checkCompatibility(ShaderStage::eVertex, outMap, ShaderStage::ePixel, inMap));

  /* Different starting components */
  inMap = IoMap();
  inMap.add(IoLocation(IoEntryType::ePerVertex, 0u, 0x6u));
  ok(!IoMap::checkCompatibility(ShaderStage::eVertex, outMap, ShaderStage::ePixel, inMap));

  /* Unwritten location */
  inMap = IoMap();
  inMap.add(IoLocation(IoEntryType::ePerVertex, 3u, 0x1u));
  ok(!IoMap::checkCompatibility(ShaderStage::eVertex, outMap, ShaderStage::ePixel, inMap));
}


void testIrInputMapSemanticInfo() {
  Builder b;

  auto function = b.add(Op::Function(ScalarType::eVoid));
  b.add(Op::Label());
  b.add(Op::Return());
  b.add(Op::FunctionEnd());

  auto entryPoint = b.add(Op::EntryPoint(function, ShaderStage::eHull));

  b.add(Op::Semantic(b.add(Op::DclInputBuiltIn(Type(ScalarType::eF32, 4u).addArrayDimension(32u), entryPoint, BuiltIn::ePosition)), 0u, "SV_POSITION"));
  b.add(Op::Semantic(b.add(Op::DclInput(Type(ScalarType::eF32, 2u).addArrayDimension(32u), entryPoint, 0u, 0u)), 0u, "TEXCOORD"));
  b.add(Op::Semantic(b.add(Op::DclInput(Type(ScalarType::eF32, 2u).addArrayDimension(32u), entryPoint, 0u, 2u)), 1u, "TexCoord"));
  b.add(Op::Semantic(b.add(Op::DclInput(Type(ScalarType::eF32, 2u).addArrayDimension(32u), entryPoint, 1u, 0u)), 2u, "texcoord"));
  b.add(Op::Semantic(b.add(Op::DclInput(Type(ScalarType::eU32, 1u).addArrayDimension(32u), entryPoint, 1u, 2u)), 0u, "DrawId"));

  b.add(Op::Semantic(b.add(Op::DclOutput(Type(ScalarType::eF32, 4u).addArrayDimension(32u), entryPoint, 0u, 0u)), 0u, "SV_POSITION"));
  b.add(Op::Semantic(b.add(Op::DclOutput(Type(ScalarType::eF32, 2u).addArrayDimension(32u), entryPoint, 1u, 0u)), 0u, "TexCoord"));
  b.add(Op::Semantic(b.add(Op::DclOutput(Type(ScalarType::eF32, 3u).addArrayDimension(32u), entryPoint, 2u, 0u)), 0u, "Normal"));

  b.add(Op::Semantic(b.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32).addArrayDimension(4u), entryPoint, BuiltIn::eTessFactorOuter)), 0u, "SV_TESSFACTOR"));
  b.add(Op::Semantic(b.add(Op::DclOutputBuiltIn(Type(ScalarType::eF32).addArrayDimension(2u), entryPoint, BuiltIn::eTessFactorInner)), 0u, "SV_InsideTessFactor"));
  b.add(Op::Semantic(b.add(Op::DclOutput(Type(ScalarType::eF32, 4u), entryPoint, 6u, 0u)), 0u, "Color"));

  LowerIoPass pass(b);

  auto e = pass.getSemanticInfo("sv_position", 0u, IoSemanticType::eInput, 0u);
  ok(e && e->getType() == IoEntryType::eBuiltIn);
  ok(e && e->getBuiltIn() == BuiltIn::ePosition);
  ok(e && e->getComponentMask() == 0xfu);

  e = pass.getSemanticInfo("sv_position", 1u, IoSemanticType::eInput, 0u);
  ok(!e);

  e = pass.getSemanticInfo("SV_TESSFACTOR", 0u, IoSemanticType::eInput, 0u);
  ok(!e);

  e = pass.getSemanticInfo("TEXCOORD", 0u, IoSemanticType::eInput, 0u);
  ok(e && e->getType() == IoEntryType::ePerVertex);
  ok(e && e->getLocationIndex() == 0u);
  ok(e && e->getComponentMask() == 0x3u);

  e = pass.getSemanticInfo("TEXCOORD", 1u, IoSemanticType::eInput, 0u);
  ok(e && e->getType() == IoEntryType::ePerVertex);
  ok(e && e->getLocationIndex() == 0u);
  ok(e && e->getComponentMask() == 0xcu);

  e = pass.getSemanticInfo("tExCoOrD", 2u, IoSemanticType::eInput, 0u);
  ok(e && e->getType() == IoEntryType::ePerVertex);
  ok(e && e->getLocationIndex() == 1u);
  ok(e && e->getComponentMask() == 0x3u);

  e = pass.getSemanticInfo("TEXCOORD", 3u, IoSemanticType::eInput, 0u);
  ok(!e);

  e = pass.getSemanticInfo("drawid", 0u, IoSemanticType::eInput, 0u);
  ok(e && e->getType() == IoEntryType::ePerVertex);
  ok(e && e->getLocationIndex() == 1u);
  ok(e && e->getComponentMask() == 0x4u);

  e = pass.getSemanticInfo("SV_POSITION", 0u, IoSemanticType::eOutput, 0u);
  ok(e && e->getType() == IoEntryType::ePerVertex);
  ok(e && e->getLocationIndex() == 0u);
  ok(e && e->getComponentMask() == 0xfu);

  e = pass.getSemanticInfo("texcoord", 0u, IoSemanticType::eOutput, 0u);
  ok(e && e->getType() == IoEntryType::ePerVertex);
  ok(e && e->getLocationIndex() == 1u);
  ok(e && e->getComponentMask() == 0x3u);

  e = pass.getSemanticInfo("normal", 0u, IoSemanticType::eOutput, 0u);
  ok(e && e->getType() == IoEntryType::ePerVertex);
  ok(e && e->getLocationIndex() == 2u);
  ok(e && e->getComponentMask() == 0x7u);

  e = pass.getSemanticInfo("normal", 1u, IoSemanticType::eOutput, 0u);
  ok(!e);

  e = pass.getSemanticInfo("sv_tessfactor", 0u, IoSemanticType::eOutput, 0u);
  ok(e && e->getType() == IoEntryType::eBuiltIn);
  ok(e && e->getBuiltIn() == BuiltIn::eTessFactorOuter);
  ok(e && e->getComponentMask() == 0xfu);

  e = pass.getSemanticInfo("sv_insidetessfactor", 0u, IoSemanticType::eOutput, 0u);
  ok(e && e->getType() == IoEntryType::eBuiltIn);
  ok(e && e->getBuiltIn() == BuiltIn::eTessFactorInner);
  ok(e && e->getComponentMask() == 0x3u);

  e = pass.getSemanticInfo("color", 0u, IoSemanticType::eOutput, 0u);
  ok(e && e->getType() == IoEntryType::ePerPatch);
  ok(e && e->getLocationIndex() == 6u);
  ok(e && e->getComponentMask() == 0xfu);
}


void testIrInputMap() {
  RUN_TEST(testIrInputMapIoLocation);
  RUN_TEST(testIrInputMapEntryFromOp);
  RUN_TEST(testIrInputMapEntryFromBuilder);
  RUN_TEST(testIrInputMapCompatibility);
  RUN_TEST(testIrInputMapSemanticInfo);
}

}
