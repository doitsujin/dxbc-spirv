#include "test_api_io.h"

namespace dxbc_spv::test_api {

Builder test_resources_cbv() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());

  auto vec4Type = BasicType(ScalarType::eF32, 4u);

  auto outputDef = builder.add(Op::DclOutput(vec4Type, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outputDef, 0u, "SV_TARGET"));
  builder.add(Op::DebugName(outputDef, "o0"));

  auto cbvDef = builder.add(Op::DclCbv(Type(vec4Type).addArrayDimension(8u), entryPoint, 0u, 0u, 1u));
  auto cbvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eCbv, cbvDef, builder.makeConstant(0u)));

  auto data = builder.add(Op::BufferLoad(vec4Type, cbvDescriptor, builder.makeConstant(2u)));

  builder.add(Op::OutputStore(outputDef, SsaDef(), data));

  builder.add(Op::Return());
  return builder;
}

Builder test_resources_cbv_dynamic() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());

  auto vec4Type = BasicType(ScalarType::eF32, 4u);

  auto outputDef = builder.add(Op::DclOutput(vec4Type, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outputDef, 0u, "SV_TARGET"));
  builder.add(Op::DebugName(outputDef, "o0"));

  auto inputDef = builder.add(Op::DclInput(ScalarType::eU32, entryPoint, 0u, 0u, InterpolationMode::eFlat));
  builder.add(Op::Semantic(inputDef, 0u, "INDEX"));
  builder.add(Op::DebugName(inputDef, "v0"));

  auto cbvDef = builder.add(Op::DclCbv(Type(vec4Type).addArrayDimension(4096u), entryPoint, 0u, 0u, 1u));
  auto cbvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eCbv, cbvDef, builder.makeConstant(0u)));

  auto data = builder.add(Op::BufferLoad(vec4Type, cbvDescriptor,
    builder.add(Op::InputLoad(ScalarType::eU32, inputDef, SsaDef()))));

  builder.add(Op::OutputStore(outputDef, SsaDef(), data));
  builder.add(Op::Return());
  return builder;
}

Builder test_resources_cbv_indexed() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());

  auto vec4Type = BasicType(ScalarType::eF32, 4u);

  auto outputDef = builder.add(Op::DclOutput(vec4Type, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outputDef, 0u, "SV_TARGET"));
  builder.add(Op::DebugName(outputDef, "o0"));

  auto indexCbvDef = builder.add(Op::DclCbv(Type(vec4Type).addArrayDimension(1u), entryPoint, 1u, 0u, 1u));
  auto indexCbvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eCbv, indexCbvDef, builder.makeConstant(0u)));

  auto index = builder.add(Op::Cast(ScalarType::eU32,
    builder.add(Op::BufferLoad(ScalarType::eF32, indexCbvDescriptor,
      builder.makeConstant(0u, 1u)))));;

  auto dataCbvDef = builder.add(Op::DclCbv(Type(vec4Type).addArrayDimension(8u), entryPoint, 0u, 0u, 256u));
  auto dataCbvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eCbv, dataCbvDef, index));

  auto data = builder.add(Op::BufferLoad(vec4Type, dataCbvDescriptor, builder.makeConstant(2u)));

  builder.add(Op::OutputStore(outputDef, SsaDef(), data));
  builder.add(Op::Return());
  return builder;
}

Builder test_resources_cbv_indexed_nonuniform() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());

  auto vec4Type = BasicType(ScalarType::eF32, 4u);

  auto outputDef = builder.add(Op::DclOutput(vec4Type, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outputDef, 0u, "SV_TARGET"));
  builder.add(Op::DebugName(outputDef, "o0"));

  auto inputDef = builder.add(Op::DclInput(ScalarType::eU32, entryPoint, 0u, 0u, InterpolationMode::eFlat));
  builder.add(Op::Semantic(inputDef, 0u, "INDEX"));
  builder.add(Op::DebugName(inputDef, "v0"));

  auto cbvDef = builder.add(Op::DclCbv(Type(vec4Type).addArrayDimension(8u), entryPoint, 0u, 0u, 0u));
  auto cbvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eCbv, cbvDef,
    builder.add(Op::InputLoad(ScalarType::eU32, inputDef, SsaDef()))).setFlags(OpFlag::eNonUniform));

  auto data = builder.add(Op::BufferLoad(vec4Type, cbvDescriptor, builder.makeConstant(2u)));

  builder.add(Op::OutputStore(outputDef, SsaDef(), data));

  builder.add(Op::Return());
  return builder;
}


SsaDef emit_buffer_declaration(Builder& builder, SsaDef entryPoint, ResourceKind kind, bool uav, bool indexed, bool atomic) {
  Type type = { };

  switch (kind) {
    case ResourceKind::eBufferTyped: {
      type = atomic ? ScalarType::eU32 : ScalarType::eF32;
    } break;

    case ResourceKind::eBufferRaw: {
      type = Type(ScalarType::eU32).addArrayDimension(0u);
    } break;

    case ResourceKind::eBufferStructured: {
      type = Type(ScalarType::eU32)
        .addArrayDimension(20u)
        .addArrayDimension(0u);
    } break;

    default:
      return SsaDef();
  }

  uint32_t arraySize = indexed ? 0u : 1u;

  Op op;
  UavFlags flags = 0u;

  if (atomic && kind == ResourceKind::eBufferTyped)
    flags |= UavFlag::eFixedFormat;

  if (uav)
    op = Op::DclUav(type, entryPoint, 0, 0, arraySize, kind, flags);
  else
    op = Op::DclSrv(type, entryPoint, 0, 0, arraySize, kind);

  return builder.add(op);
}

SsaDef emit_buffer_descriptor(Builder& builder, SsaDef entryPoint, ResourceKind kind, bool uav, bool indexed, bool atomic) {
  SsaDef dcl = emit_buffer_declaration(builder, entryPoint, kind, uav, indexed, atomic);

  SsaDef index;

  if (indexed) {
    auto inputDef = builder.add(Op::DclInput(ScalarType::eU32, entryPoint, 0u, 2u, InterpolationMode::eFlat));
    builder.add(Op::Semantic(inputDef, 0u, "BUFFER_INDEX"));

    index = builder.add(Op::InputLoad(ScalarType::eU32, inputDef, SsaDef()));
  } else {
    index = builder.makeConstant(0u);
  }

  auto op = Op::DescriptorLoad(uav ? ScalarType::eUav : ScalarType::eSrv, dcl, index);

  if (indexed)
    op.setFlags(OpFlag::eNonUniform);

  return builder.add(op);
}

SsaDef emit_buffer_load_store_address(Builder& builder, SsaDef entryPoint, ResourceKind kind) {
  auto inputDef = builder.add(Op::DclInput(BasicType(ScalarType::eU32, 2u), entryPoint, 0u, 0u, InterpolationMode::eFlat));
  builder.add(Op::Semantic(inputDef, 0u, "BUFFER_ADDRESS"));

  auto index = builder.add(Op::InputLoad(ScalarType::eU32, inputDef, builder.makeConstant(0u)));

  if (kind == ResourceKind::eBufferStructured) {
    auto subIndex = builder.add(Op::InputLoad(ScalarType::eU32, inputDef, builder.makeConstant(1u)));
    index = builder.add(Op::CompositeConstruct(BasicType(ScalarType::eU32, 2u), index, subIndex));
  } else if (kind == ResourceKind::eBufferRaw) {
    index = builder.add(Op::IAdd(ScalarType::eU32, builder.add(Op::IMul(
      ScalarType::eU32, index, builder.makeConstant(4u))), builder.makeConstant(2u)));
  }

  return index;
}

Builder make_test_buffer_load(ResourceKind kind, bool uav, bool indexed) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());
  auto descriptor = emit_buffer_descriptor(builder, entryPoint, kind, uav, indexed, false);
  auto index = emit_buffer_load_store_address(builder, entryPoint, kind);

  Type type = kind == ResourceKind::eBufferTyped
    ? BasicType(ScalarType::eF32, 4u)
    : BasicType(ScalarType::eU32, 2u);

  auto data = builder.add(Op::BufferLoad(type, descriptor, index));

  auto outputDef = builder.add(Op::DclOutput(type, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outputDef, 0u, "SV_TARGET"));
  builder.add(Op::OutputStore(outputDef, SsaDef(), data));

  builder.add(Op::Return());
  return builder;
}

Builder make_test_buffer_query(ResourceKind kind, bool uav, bool indexed) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());
  auto descriptor = emit_buffer_descriptor(builder, entryPoint, kind, uav, indexed, false);

  auto size = builder.add(Op::BufferQuerySize(descriptor));

  auto outputDef = builder.add(Op::DclOutput(ScalarType::eU32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outputDef, 0u, "SV_TARGET"));
  builder.add(Op::OutputStore(outputDef, SsaDef(), size));

  builder.add(Op::Return());
  return builder;
}

Builder make_test_buffer_store(ResourceKind kind, bool indexed) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());
  auto descriptor = emit_buffer_descriptor(builder, entryPoint, kind, true, indexed, false);
  auto index = emit_buffer_load_store_address(builder, entryPoint, kind);

  Type type = kind == ResourceKind::eBufferTyped
    ? BasicType(ScalarType::eF32, 4u)
    : BasicType(ScalarType::eU32, 2u);

  SsaDef value = kind == ResourceKind::eBufferTyped
    ? builder.add(Op::CompositeConstruct(type,
        builder.makeConstant(1.0f), builder.makeConstant(2.0f),
        builder.makeConstant(3.0f), builder.makeConstant(4.0f)))
    : builder.add(Op::CompositeConstruct(type,
        builder.makeConstant(1u), builder.makeConstant(2u)));

  builder.add(Op::BufferStore(descriptor, index, value));

  builder.add(Op::Return());
  return builder;
}

Builder make_test_buffer_atomic(ResourceKind kind, bool indexed) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());
  auto descriptor = emit_buffer_descriptor(builder, entryPoint, kind, true, indexed, true);
  auto index = emit_buffer_load_store_address(builder, entryPoint, kind);

  builder.add(Op::BufferAtomic(AtomicOp::eAdd, Type(),
    descriptor, index, builder.makeConstant(16u)));

  builder.add(Op::Return());
  return builder;
}


Builder test_resources_srv_buffer_typed_load() {
  return make_test_buffer_load(ResourceKind::eBufferTyped, false, false);
}

Builder test_resources_srv_buffer_typed_query() {
  return make_test_buffer_query(ResourceKind::eBufferTyped, false, false);
}

Builder test_resources_srv_buffer_raw_load() {
  return make_test_buffer_load(ResourceKind::eBufferRaw, false, false);
}

Builder test_resources_srv_buffer_raw_query() {
  return make_test_buffer_query(ResourceKind::eBufferRaw, false, false);
}

Builder test_resources_srv_buffer_structured_load() {
  return make_test_buffer_load(ResourceKind::eBufferStructured, false, false);
}

Builder test_resources_srv_buffer_structured_query() {
  return make_test_buffer_query(ResourceKind::eBufferStructured, false, false);
}


Builder test_resources_srv_indexed_buffer_typed_load() {
  return make_test_buffer_load(ResourceKind::eBufferTyped, false, true);
}

Builder test_resources_srv_indexed_buffer_typed_query() {
  return make_test_buffer_query(ResourceKind::eBufferTyped, false, true);
}

Builder test_resources_srv_indexed_buffer_raw_load() {
  return make_test_buffer_load(ResourceKind::eBufferRaw, false, true);
}

Builder test_resources_srv_indexed_buffer_raw_query() {
  return make_test_buffer_query(ResourceKind::eBufferRaw, false, true);
}

Builder test_resources_srv_indexed_buffer_structured_load() {
  return make_test_buffer_load(ResourceKind::eBufferStructured, false, true);
}

Builder test_resources_srv_indexed_buffer_structured_query() {
  return make_test_buffer_query(ResourceKind::eBufferStructured, false, true);
}


Builder test_resources_uav_buffer_typed_load() {
  return make_test_buffer_load(ResourceKind::eBufferTyped, true, false);
}

Builder test_resources_uav_buffer_typed_query() {
  return make_test_buffer_query(ResourceKind::eBufferTyped, true, false);
}

Builder test_resources_uav_buffer_typed_store() {
  return make_test_buffer_store(ResourceKind::eBufferTyped, false);
}

Builder test_resources_uav_buffer_typed_atomic() {
  return make_test_buffer_atomic(ResourceKind::eBufferTyped, false);
}

Builder test_resources_uav_buffer_raw_load() {
  return make_test_buffer_load(ResourceKind::eBufferRaw, true, false);
}

Builder test_resources_uav_buffer_raw_query() {
  return make_test_buffer_query(ResourceKind::eBufferRaw, true, false);
}

Builder test_resources_uav_buffer_raw_store() {
  return make_test_buffer_store(ResourceKind::eBufferRaw, false);
}

Builder test_resources_uav_buffer_raw_atomic() {
  return make_test_buffer_atomic(ResourceKind::eBufferRaw, false);
}

Builder test_resources_uav_buffer_structured_load() {
  return make_test_buffer_load(ResourceKind::eBufferStructured, true, false);
}

Builder test_resources_uav_buffer_structured_query() {
  return make_test_buffer_query(ResourceKind::eBufferStructured, true, false);
}

Builder test_resources_uav_buffer_structured_store() {
  return make_test_buffer_store(ResourceKind::eBufferStructured, false);
}

Builder test_resources_uav_buffer_structured_atomic() {
  return make_test_buffer_atomic(ResourceKind::eBufferStructured, false);
}


Builder test_resources_uav_indexed_buffer_typed_load() {
  return make_test_buffer_load(ResourceKind::eBufferTyped, true, true);
}

Builder test_resources_uav_indexed_buffer_typed_query() {
  return make_test_buffer_query(ResourceKind::eBufferTyped, true, true);
}

Builder test_resources_uav_indexed_buffer_typed_store() {
  return make_test_buffer_store(ResourceKind::eBufferTyped, true);
}

Builder test_resources_uav_indexed_buffer_typed_atomic() {
  return make_test_buffer_atomic(ResourceKind::eBufferTyped, true);
}

Builder test_resources_uav_indexed_buffer_raw_load() {
  return make_test_buffer_load(ResourceKind::eBufferRaw, true, true);
}

Builder test_resources_uav_indexed_buffer_raw_query() {
  return make_test_buffer_query(ResourceKind::eBufferRaw, true, true);
}

Builder test_resources_uav_indexed_buffer_raw_store() {
  return make_test_buffer_store(ResourceKind::eBufferRaw, true);
}

Builder test_resources_uav_indexed_buffer_raw_atomic() {
  return make_test_buffer_atomic(ResourceKind::eBufferRaw, true);
}

Builder test_resources_uav_indexed_buffer_structured_load() {
  return make_test_buffer_load(ResourceKind::eBufferStructured, true, true);
}

Builder test_resources_uav_indexed_buffer_structured_query() {
  return make_test_buffer_query(ResourceKind::eBufferStructured, true, true);
}

Builder test_resources_uav_indexed_buffer_structured_store() {
  return make_test_buffer_store(ResourceKind::eBufferStructured, true);
}

Builder test_resources_uav_indexed_buffer_structured_atomic() {
  return make_test_buffer_atomic(ResourceKind::eBufferStructured, true);
}

}
