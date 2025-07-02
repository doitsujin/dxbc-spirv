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

  builder.add(Op::Constant(-1));
  builder.add(Op::Constant(3.0));

  auto funcDef = builder.add(Op::Function(Type()));
  builder.add(Op::Label());
  builder.add(Op::Return());
  builder.add(Op::FunctionEnd());

  builder.add(Op::DebugName(funcDef, "entry_point"));

  testIrSerializeBuilder(builder);
}

}
