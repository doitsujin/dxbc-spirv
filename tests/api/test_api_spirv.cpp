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
  builder.add(Op::DebugMemberName(data, 0u, "aVec"));
  builder.add(Op::DebugMemberName(data, 1u, "aInt"));
  builder.add(Op::DebugMemberName(data, 2u, "bVec"));
  builder.add(Op::DebugMemberName(data, 3u, "bInt"));

  auto colorDef = builder.add(Op::DclOutput(BasicType(ScalarType::eF32, 3u), entryPoint, 0u, 0u));
  builder.add(Op::Semantic(colorDef, 0u, "SV_TARGET"));
  auto indexDef = builder.add(Op::DclOutput(BasicType(ScalarType::eF32, 1u), entryPoint, 1u, 0u));
  builder.add(Op::Semantic(indexDef, 1u, "SV_TARGET"));

  auto sel = builder.add(Op::PushDataLoad(ScalarType::eU32, selector, SsaDef()));
  auto cond = builder.add(Op::INe(ScalarType::eBool, sel, builder.makeConstant(0u)));

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

Builder test_spirv_cbv_srv_uav_structs() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eVertex);

  builder.add(Op::SetFpMode(entryPoint, ScalarType::eF32, OpFlags(), RoundMode::eNearestEven, DenormMode::eFlush));
  builder.add(Op::SetFpMode(entryPoint, ScalarType::eF16, OpFlags(), RoundMode::eNearestEven, DenormMode::ePreserve));

  builder.add(Op::Label());

  auto drawInfoType = Type()
    .addStructMember(ScalarType::eU32)
    .addStructMember(ScalarType::eF16)
    .addStructMember(ScalarType::eF16)
    .addStructMember(ScalarType::eF16, 2u)
    .addStructMember(ScalarType::eU8, 4u);

  auto glyphType = Type()
    .addStructMember(ScalarType::eF16, 2u)
    .addStructMember(ScalarType::eF16, 2u)
    .addStructMember(ScalarType::eF16, 2u)
    .addArrayDimension(0u);

  auto textType = Type(ScalarType::eU8)
    .addArrayDimension(0u);

  auto feedbackType = Type()
    .addStructMember(ScalarType::eF16, 2u)
    .addStructMember(ScalarType::eU8, 4u)
    .addArrayDimension(0u);

  auto drawInfoCbv = builder.add(Op::DclCbv(drawInfoType, entryPoint, 0u, 0u, 1u));
  builder.add(Op::DebugName(drawInfoCbv, "draw_info"));
  builder.add(Op::DebugMemberName(drawInfoCbv, 0u, "text_offset"));
  builder.add(Op::DebugMemberName(drawInfoCbv, 1u, "text_size"));
  builder.add(Op::DebugMemberName(drawInfoCbv, 2u, "text_advance"));
  builder.add(Op::DebugMemberName(drawInfoCbv, 3u, "text_location"));
  builder.add(Op::DebugMemberName(drawInfoCbv, 4u, "color"));
  auto drawInfoDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eCbv, drawInfoCbv, builder.makeConstant(0u)));

  auto glyphInfoSrv = builder.add(Op::DclSrv(glyphType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured));
  builder.add(Op::DebugName(glyphInfoSrv, "glyph_infos"));
  builder.add(Op::DebugMemberName(glyphInfoSrv, 0u, "offset"));
  builder.add(Op::DebugMemberName(glyphInfoSrv, 1u, "size"));
  builder.add(Op::DebugMemberName(glyphInfoSrv, 2u, "origin"));
  auto glyphInfoDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eSrv, glyphInfoSrv, builder.makeConstant(0u)));

  auto textSrv = builder.add(Op::DclSrv(textType, entryPoint, 0u, 1u, 1u, ResourceKind::eBufferStructured));
  builder.add(Op::DebugName(textSrv, "text"));
  auto textDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eSrv, textSrv, builder.makeConstant(0u)));

  auto feedbackUav = builder.add(Op::DclUav(feedbackType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured, UavFlags()));
  builder.add(Op::DebugName(feedbackUav, "feedback"));
  builder.add(Op::DebugMemberName(feedbackUav, 0u, "location"));
  builder.add(Op::DebugMemberName(feedbackUav, 1u, "color"));
  auto feedbackDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, feedbackUav, builder.makeConstant(0u)));

  auto vertexIdDef = builder.add(Op::DclInputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eVertexId));
  builder.add(Op::Semantic(vertexIdDef, 0u, "SV_VERTEXID"));

  auto vertexId = builder.add(Op::InputLoad(ScalarType::eU32, vertexIdDef, SsaDef()));

  auto charIndex = builder.add(Op::BufferLoad(ScalarType::eU32, drawInfoDescriptor, builder.makeConstant(0u), 4u));
  charIndex = builder.add(Op::IAdd(ScalarType::eU32, charIndex, vertexId));

  auto glyphIndex = builder.add(Op::ConvertItoI(ScalarType::eU32,
    builder.add(Op::BufferLoad(ScalarType::eU8, textDescriptor, charIndex, 1u))));

  for (uint32_t i = 0u; i < 4u; i++) {
    auto color = builder.add(Op::BufferLoad(ScalarType::eU8,
      drawInfoDescriptor, builder.makeConstant(4u, i), 1u));

    builder.add(Op::BufferStore(feedbackDescriptor,
      builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 3u),
        vertexId, builder.makeConstant(1u), builder.makeConstant(i))), color, 1u));
  }

  auto location = builder.add(Op::BufferLoad(Type(ScalarType::eF16, 2u),
    drawInfoDescriptor, builder.makeConstant(3u), 4u));

  auto size = builder.add(Op::BufferLoad(ScalarType::eF16,
    drawInfoDescriptor, builder.makeConstant(1u), 2u));

  auto advance = builder.add(Op::BufferLoad(ScalarType::eF16,
    drawInfoDescriptor, builder.makeConstant(2u), 2u));
  advance = builder.add(Op::FMul(ScalarType::eF16, advance, size));

  auto locationX = builder.add(Op::CompositeExtract(ScalarType::eF16, location, builder.makeConstant(0u)));
  auto locationY = builder.add(Op::CompositeExtract(ScalarType::eF16, location, builder.makeConstant(1u)));
  locationX = builder.add(Op::FMad(ScalarType::eF16, advance,
    builder.add(Op::ConvertItoF(ScalarType::eF16, vertexId)), locationX));

  location = builder.add(Op::CompositeConstruct(Type(ScalarType::eF16, 2u), locationX, locationY));

  auto origin = builder.add(Op::BufferLoad(Type(ScalarType::eF16, 2u), glyphInfoDescriptor,
    builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 2u), glyphIndex, builder.makeConstant(2u))), 4u));

  location = builder.add(Op::FMad(Type(ScalarType::eF16, 2u),
    builder.add(Op::CompositeConstruct(Type(ScalarType::eF16, 2u), size, size)),
    origin, location));

  builder.add(Op::BufferStore(feedbackDescriptor,
    builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 2u),
      vertexId, builder.makeConstant(0u))), location, 4u));

  builder.add(Op::Return());
  return builder;
}

Builder test_spirv_point_size() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eVertex);
  builder.add(Op::Label());

  auto pointSize = builder.add(Op::DclOutputBuiltIn(ScalarType::eF32, entryPoint, BuiltIn::ePointSize));
  builder.add(Op::Semantic(pointSize, 0u, "PSIZE"));
  builder.add(Op::OutputStore(pointSize, SsaDef(), builder.makeConstant(2.0f)));
  builder.add(Op::Return());
  return builder;
}

}
