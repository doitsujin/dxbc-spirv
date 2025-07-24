#include <unordered_map>

#include "../../ir/ir_builder.h"
#include "../../ir/ir_serialize.h"

#include "../test_common.h"

namespace dxbc_spv::tests::ir {

using namespace dxbc_spv::ir;

void testIrSerializeBuilder(const Builder& srcBuilder) {
  Serializer serializer(srcBuilder);

  std::vector<uint8_t> data(serializer.computeSerializedSize());
  ok(serializer.serialize(data.data(), data.size()));

  Builder newBuilder;

  Deserializer deserializer(data.data(), data.size());
  ok(deserializer.deserialize(newBuilder));
  ok(deserializer.atEnd());

  /* Verify that instructions are in the same order and operands are the same. */
  std::unordered_map<SsaDef, SsaDef> defMap;

  auto a = srcBuilder.begin();
  auto b = newBuilder.begin();

  while (a != srcBuilder.end() && b != newBuilder.end()) {
    ok(*a && a->getDef());
    ok(*b && b->getDef());

    defMap.insert({ (b++)->getDef(), (a++)->getDef() });
  }

  ok(a == srcBuilder.end());
  ok(b == newBuilder.end());

  a = srcBuilder.begin();
  b = newBuilder.begin();

  while (a != srcBuilder.end() && b != newBuilder.end()) {
    ok(a->getOpCode() == b->getOpCode());
    ok(a->getType() == b->getType());
    ok(a->getFlags() == b->getFlags());
    ok(a->getOperandCount() == b->getOperandCount());
    ok(a->getFirstLiteralOperandIndex() == b->getFirstLiteralOperandIndex());

    for (uint32_t i = 0u; i < a->getFirstLiteralOperandIndex(); i++) {
      auto aDef = SsaDef(a->getOperand(i));
      auto bDef = SsaDef(b->getOperand(i));

      if (aDef && bDef) {
        auto e = defMap.find(bDef);
        ok(e != defMap.end());

        if (e != defMap.end())
          ok(e->second == aDef);
      } else {
        ok(!aDef && !bDef);
      }
    }

    for (uint32_t i = a->getFirstLiteralOperandIndex(); i < a->getOperandCount(); i++)
      ok(uint64_t(a->getOperand(i)) == uint64_t(b->getOperand(i)));

    a++;
    b++;
  }
}


void testIrSerialize() {
  testIrSerializeBuilder(Builder());

  Builder builder;

  auto funcDef = builder.add(Op::Function(Type()));
  builder.add(Op::DebugName(funcDef, "main"));

  auto entryPointDef = builder.add(
    Op::EntryPoint(funcDef, ShaderStage::eVertex));
  builder.add(Op::DebugName(entryPointDef, "vertex_shader_123"));

  auto vertexIdDef = builder.add(
    Op::DclInputBuiltIn(ScalarType::eU32, entryPointDef, BuiltIn::eVertexId));
  builder.add(Op::DebugName(vertexIdDef, "v0"));
  builder.add(Op::Semantic(vertexIdDef, 0, "SV_VERTEXID"));

  auto positionDef = builder.add(
    Op::DclOutputBuiltIn(BasicType(ScalarType::eF32, 4), entryPointDef, BuiltIn::ePosition));
  builder.add(Op::DebugName(positionDef, "o0"));
  builder.add(Op::Semantic(positionDef, 0, "SV_POSITION"));

  builder.add(Op::Label());

  auto vidDef = builder.add(Op::InputLoad(ScalarType::eU32, SsaDef(), vertexIdDef));
  auto xDef = builder.add(Op::Select(ScalarType::eF32,
    builder.add(Op::INe(ScalarType::eBool, builder.makeConstant(0u),
      builder.add(Op::IAnd(ScalarType::eU32, vidDef, builder.makeConstant(1u))))),
    builder.makeConstant(3.0f),
    builder.makeConstant(-1.0f)));
  auto yDef = builder.add(Op::Select(ScalarType::eF32,
    builder.add(Op::INe(ScalarType::eBool, builder.makeConstant(0u),
      builder.add(Op::IAnd(ScalarType::eU32, vidDef, builder.makeConstant(2u)).setFlags(OpFlag::ePrecise)))),
    builder.makeConstant(3.0f),
    builder.makeConstant(-1.0f)));
  auto zDef = builder.makeConstant(0.0f);
  auto wDef = builder.makeConstant(1.0f);

  auto vecDef = builder.add(Op::CompositeConstruct(BasicType(ScalarType::eF32, 4), xDef, yDef, zDef, wDef));
  builder.add(Op::OutputStore(positionDef, SsaDef(), vecDef));
  builder.add(Op::Return());
  builder.add(Op::FunctionEnd());

  testIrSerializeBuilder(builder);
}

}
