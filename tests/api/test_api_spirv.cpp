#include "test_api_misc.h"

namespace dxbc_spv::test_api {

Builder test_spirv_spec_constant() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);
  builder.add(Op::Label());

  auto selector = builder.add(Op::DclSpecConstant(ScalarType::eBool, entryPoint, 0u, false));
  auto a = builder.add(Op::DclSpecConstant(ScalarType::eU32, entryPoint, 1u, 5u));
  auto b = builder.add(Op::DclSpecConstant(ScalarType::eU32, entryPoint, 2u, 6u));

  builder.add(Op::DebugName(selector, "sel"));
  builder.add(Op::DebugName(a, "a"));
  builder.add(Op::DebugName(b, "b"));

  auto outputDef = builder.add(Op::DclOutput(ScalarType::eU32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outputDef, 0u, "SV_TARGET"));

  builder.add(Op::OutputStore(outputDef, SsaDef(),
    builder.add(Op::Select(ScalarType::eU32, selector, a, b))));

  builder.add(Op::Return());
  return builder;
}

Builder test_spirv_push_data() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);
  builder.add(Op::Label());

  auto selector = builder.add(Op::DclPushData(ScalarType::eU32, entryPoint, 0u, ShaderStage::ePixel));
  auto dataType = Type()
    .addStructMember(ScalarType::eF32, 3u)
    .addStructMember(ScalarType::eI32)
    .addStructMember(ScalarType::eF32, 3u)
    .addStructMember(ScalarType::eI32);
  auto data = builder.add(Op::DclPushData(dataType, entryPoint, 4u, ShaderStage::ePixel));

  builder.add(Op::DebugName(selector, "sel"));

  auto colorDef = builder.add(Op::DclOutput(BasicType(ScalarType::eF32, 3u), entryPoint, 0u, 0u));
  builder.add(Op::Semantic(colorDef, 0u, "SV_TARGET"));
  auto indexDef = builder.add(Op::DclOutput(BasicType(ScalarType::eF32, 1u), entryPoint, 1u, 0u));
  builder.add(Op::Semantic(indexDef, 1u, "SV_TARGET"));

  auto sel = builder.add(Op::PushDataLoad(ScalarType::eU32, selector, SsaDef()));
  auto cond = builder.add(Op::INe(sel, builder.makeConstant(0u)));

  auto a = builder.add(Op::PushDataLoad(ScalarType::eI32, data, builder.makeConstant(1u)));
  auto b = builder.add(Op::PushDataLoad(ScalarType::eI32, data, builder.makeConstant(3u)));
  auto result = builder.add(Op::ConvertItoF(ScalarType::eF32,
    builder.add(Op::Select(ScalarType::eI32, cond, a, b))));
  builder.add(Op::OutputStore(indexDef, SsaDef(), result));

  for (uint32_t i = 0u; i < 3u; i++) {
    auto a = builder.add(Op::PushDataLoad(ScalarType::eF32, data, builder.makeConstant(0u, i)));
    auto b = builder.add(Op::PushDataLoad(ScalarType::eF32, data, builder.makeConstant(2u, i)));
    auto result = builder.add(Op::Select(ScalarType::eF32, cond, a, b));
    builder.add(Op::OutputStore(colorDef, builder.makeConstant(i), result));
  }

  builder.add(Op::Return());
  return builder;
}

Builder test_spirv_raw_pointer() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);
  builder.add(Op::Label());

  auto indexDef = builder.add(Op::DclInput(ScalarType::eU32, entryPoint, 1u, 0u, InterpolationMode::eFlat));
  builder.add(Op::Semantic(indexDef, 0u, "INDEX"));

  auto outputDef = builder.add(Op::DclOutput(BasicType(ScalarType::eF32, 4u), entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outputDef, 0u, "SV_TARGET"));

  /* Push constant to load pointer from */
  auto addressDef = builder.add(Op::DclPushData(ScalarType::eU64, entryPoint, 0u, ShaderStage::ePixel));
  auto address = builder.add(Op::PushDataLoad(ScalarType::eU64, addressDef, SsaDef()));

  /* Unbounded read-only vec4 array */
  auto typeA = Type(ScalarType::eF32, 4u).addArrayDimension(0u);
  auto pointerA = builder.add(Op::Pointer(typeA, address, UavFlag::eReadOnly));

  address = builder.add(Op::IAdd(ScalarType::eU64, address, builder.makeConstant(uint64_t(16u))));

  auto index = builder.add(Op::InputLoad(ScalarType::eU32, indexDef, SsaDef()));

  builder.add(Op::OutputStore(outputDef, SsaDef(),
    builder.add(Op::MemoryLoad(BasicType(ScalarType::eF32, 4u), pointerA, index, 16u))));

  /* Struct to do an atomic, load and store on */
  auto typeB = Type()
    .addStructMember(ScalarType::eF32)
    .addStructMember(ScalarType::eU32)
    .addStructMember(ScalarType::eI32, 2u);

  auto pointerB = builder.add(Op::Pointer(typeB, address, UavFlag::eCoherent));

  auto loadDef = builder.add(Op::ConvertFtoI(ScalarType::eU32,
    builder.add(Op::MemoryLoad(ScalarType::eF32, pointerB, builder.makeConstant(0u), 4u))));
  auto atomicDef = builder.add(Op::MemoryAtomic(AtomicOp::eAdd, ScalarType::eU32, pointerB, builder.makeConstant(1u), loadDef));

  address = builder.add(Op::IAdd(ScalarType::eU64, address, builder.makeConstant(uint64_t(16u))));

  auto loadi32Def = builder.add(Op::MemoryLoad(ScalarType::eI32, pointerB, builder.makeConstant(2u, 0u), 4u));
  builder.add(Op::MemoryStore(pointerB, builder.makeConstant(2u, 1u), loadi32Def, 4u));

  /* Single scalar to store to */
  auto typeC = Type(ScalarType::eU32);
  auto pointerC = builder.add(Op::Pointer(typeC, address, UavFlag::eWriteOnly));

  builder.add(Op::MemoryStore(pointerC, SsaDef(), atomicDef, 4u));
  builder.add(Op::Return());
  return builder;
}

}
