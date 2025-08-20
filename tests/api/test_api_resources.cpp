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

  auto data = builder.add(Op::BufferLoad(vec4Type, cbvDescriptor, builder.makeConstant(2u), 16u));

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
    builder.add(Op::InputLoad(ScalarType::eU32, inputDef, SsaDef())), 16u));

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
      builder.makeConstant(0u, 1u), 4u))));;

  auto dataCbvDef = builder.add(Op::DclCbv(Type(vec4Type).addArrayDimension(8u), entryPoint, 0u, 0u, 256u));
  auto dataCbvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eCbv, dataCbvDef, index));

  auto data = builder.add(Op::BufferLoad(vec4Type, dataCbvDescriptor, builder.makeConstant(2u), 16u));

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

  auto data = builder.add(Op::BufferLoad(vec4Type, cbvDescriptor, builder.makeConstant(2u), 16u));

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

SsaDef emit_buffer_load_store_address(Builder& builder, SsaDef entryPoint, ResourceKind kind, bool dynamic) {
  auto inputDef = SsaDef();

  if (dynamic) {
    inputDef = builder.add(Op::DclInput(BasicType(ScalarType::eU32, 2u), entryPoint, 0u, 0u, InterpolationMode::eFlat));
    builder.add(Op::Semantic(inputDef, 0u, "BUFFER_ADDRESS"));
  }

  auto index = dynamic
    ? builder.add(Op::InputLoad(ScalarType::eU32, inputDef, builder.makeConstant(0u)))
    : builder.makeConstant(7u);

  if (kind == ResourceKind::eBufferStructured) {
    auto subIndex = dynamic
      ? builder.add(Op::InputLoad(ScalarType::eU32, inputDef, builder.makeConstant(1u)))
      : builder.makeConstant(3u);

    index = builder.add(Op::CompositeConstruct(BasicType(ScalarType::eU32, 2u), index, subIndex));
  } else if (kind == ResourceKind::eBufferRaw && dynamic) {
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
  auto index0 = emit_buffer_load_store_address(builder, entryPoint, kind, true);
  auto index1 = emit_buffer_load_store_address(builder, entryPoint, kind, false);

  Type type = kind == ResourceKind::eBufferTyped
    ? BasicType(ScalarType::eF32, 4u)
    : BasicType(ScalarType::eU32, 2u);

  auto data0 = builder.add(Op::BufferLoad(type, descriptor, index0,
    kind == ResourceKind::eBufferTyped ? 0u : 4u));
  auto data1 = builder.add(Op::BufferLoad(type, descriptor, index1,
    kind == ResourceKind::eBufferTyped ? 0u : 4u));

  auto output0Def = builder.add(Op::DclOutput(type, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(output0Def, 0u, "SV_TARGET"));
  builder.add(Op::OutputStore(output0Def, SsaDef(), data0));

  auto output1Def = builder.add(Op::DclOutput(type, entryPoint, 1u, 0u));
  builder.add(Op::Semantic(output1Def, 1u, "SV_TARGET"));
  builder.add(Op::OutputStore(output1Def, SsaDef(), data1));

  if (kind == ResourceKind::eBufferStructured) {
    auto type = BasicType(ScalarType::eU32, 4u);

    auto index2 = builder.makeConstant(16u, 11u);
    auto data2 = builder.add(Op::BufferLoad(type, descriptor, index2, 4u));

    auto output2Def = builder.add(Op::DclOutput(type, entryPoint, 2u, 0u));
    builder.add(Op::Semantic(output2Def, 2u, "SV_TARGET"));
    builder.add(Op::OutputStore(output2Def, SsaDef(), data2));
  }

  if (kind != ResourceKind::eBufferTyped) {
    auto type = BasicType(ScalarType::eU32, 1u);
    auto data3 = builder.add(Op::BufferLoad(type, descriptor, index0, 4u));

    auto output3Def = builder.add(Op::DclOutput(type, entryPoint, 3u, 0u));
    builder.add(Op::Semantic(output3Def, 3u, "SV_TARGET"));
    builder.add(Op::OutputStore(output3Def, SsaDef(), data3));
  }

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
  auto index0 = emit_buffer_load_store_address(builder, entryPoint, kind, true);
  auto index1 = emit_buffer_load_store_address(builder, entryPoint, kind, false);

  Type type = kind == ResourceKind::eBufferTyped
    ? BasicType(ScalarType::eF32, 4u)
    : BasicType(ScalarType::eU32, 3u);

  SsaDef value = kind == ResourceKind::eBufferTyped
    ? builder.add(Op::CompositeConstruct(type,
        builder.makeConstant(1.0f), builder.makeConstant(2.0f),
        builder.makeConstant(3.0f), builder.makeConstant(4.0f)))
    : builder.add(Op::CompositeConstruct(type,
        builder.makeConstant(1u), builder.makeConstant(2u),
        builder.makeConstant(3u)));

  builder.add(Op::BufferStore(descriptor, index0, value,
    kind == ResourceKind::eBufferTyped ? 0u : 4u));
  builder.add(Op::BufferStore(descriptor, index1, value,
    kind == ResourceKind::eBufferTyped ? 0u : 4u));

  if (kind != ResourceKind::eBufferTyped)
    builder.add(Op::BufferStore(descriptor, index1, builder.makeConstant(6u), 4u));

  builder.add(Op::Return());
  return builder;
}

Builder make_test_buffer_atomic(ResourceKind kind, bool indexed) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());
  auto descriptor = emit_buffer_descriptor(builder, entryPoint, kind, true, indexed, true);
  auto index = emit_buffer_load_store_address(builder, entryPoint, kind, true);

  auto def = builder.add(Op::BufferAtomic(AtomicOp::eLoad, ScalarType::eU32, descriptor, index, SsaDef()));
  def = builder.add(Op::BufferAtomic(AtomicOp::eExchange, ScalarType::eU32, descriptor, index,
    builder.add(Op::IAdd(ScalarType::eU32, def, builder.makeConstant(10u)))));
  def = builder.add(Op::BufferAtomic(AtomicOp::eCompareExchange, ScalarType::eU32, descriptor, index,
    builder.add(Op::CompositeConstruct(BasicType(ScalarType::eU32, 2u), builder.makeConstant(10u), def))));
  def = builder.add(Op::BufferAtomic(AtomicOp::eAdd, ScalarType::eU32, descriptor, index, def));
  def = builder.add(Op::BufferAtomic(AtomicOp::eSub, ScalarType::eU32, descriptor, index, def));
  def = builder.add(Op::BufferAtomic(AtomicOp::eSMin, ScalarType::eU32, descriptor, index, def));
  def = builder.add(Op::BufferAtomic(AtomicOp::eSMax, ScalarType::eU32, descriptor, index, def));
  def = builder.add(Op::BufferAtomic(AtomicOp::eUMin, ScalarType::eU32, descriptor, index, def));
  def = builder.add(Op::BufferAtomic(AtomicOp::eUMax, ScalarType::eU32, descriptor, index, def));
  def = builder.add(Op::BufferAtomic(AtomicOp::eAnd, ScalarType::eU32, descriptor, index, def));
  def = builder.add(Op::BufferAtomic(AtomicOp::eOr, ScalarType::eU32, descriptor, index, def));
  def = builder.add(Op::BufferAtomic(AtomicOp::eXor, ScalarType::eU32, descriptor, index, def));
  def = builder.add(Op::BufferAtomic(AtomicOp::eInc, ScalarType::eU32, descriptor, index, SsaDef()));
  def = builder.add(Op::BufferAtomic(AtomicOp::eDec, ScalarType::eU32, descriptor, index, SsaDef()));
  builder.add(Op::BufferAtomic(AtomicOp::eStore, Type(), descriptor, index, def));

  builder.add(Op::BufferAtomic(AtomicOp::eAdd, ScalarType::eVoid, descriptor, index, builder.makeConstant(1u)));
  builder.add(Op::BufferAtomic(AtomicOp::eSub, ScalarType::eVoid, descriptor, index, builder.makeConstant(2u)));

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


SsaDef emit_uav_counter_descriptor(Builder& builder, SsaDef entryPoint, bool indexed) {
  SsaDef dcl = emit_buffer_declaration(builder, entryPoint, ResourceKind::eBufferStructured, true, indexed, false);
  SsaDef ctr = builder.add(Op::DclUavCounter(entryPoint, dcl));

  SsaDef index;

  if (indexed) {
    auto inputDef = builder.add(Op::DclInputBuiltIn(BasicType(ScalarType::eU32, 3u), entryPoint, BuiltIn::eWorkgroupId));
    builder.add(Op::Semantic(inputDef, 0u, "SV_GROUPID"));
    builder.add(Op::DebugName(inputDef, "vGroup"));

    index = builder.add(Op::InputLoad(ScalarType::eU32, inputDef, builder.makeConstant(0u)));
  } else {
    index = builder.makeConstant(0u);
  }

  auto op = Op::DescriptorLoad(ScalarType::eUavCounter, ctr, index);

  if (indexed)
    op.setFlags(OpFlag::eNonUniform);

  return builder.add(op);
}

Builder make_test_uav_counter(bool indexed) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::SetCsWorkgroupSize(entryPoint, 32u, 1u, 1u));

  builder.add(Op::Label());
  auto descriptor = emit_uav_counter_descriptor(builder, entryPoint, indexed);

  builder.add(Op::CounterAtomic(AtomicOp::eInc, ScalarType::eU32, descriptor));
  builder.add(Op::Return());
  return builder;
}

Builder test_resource_uav_counter() {
  return make_test_uav_counter(false);
}

Builder test_resource_uav_counter_indexed() {
  return make_test_uav_counter(true);
}


SsaDef emit_image_declaration(Builder& builder, SsaDef entryPoint, ResourceKind kind, bool uav, bool indexed, bool atomic) {
  Type type = atomic ? ScalarType::eU32 : ScalarType::eF32;

  uint32_t arraySize = indexed ? 0u : 1u;

  Op op;
  UavFlags flags = 0u;

  if (atomic)
    flags |= UavFlag::eFixedFormat;

  if (uav)
    op = Op::DclUav(type, entryPoint, 0, 0, arraySize, kind, flags);
  else
    op = Op::DclSrv(type, entryPoint, 0, 0, arraySize, kind);

  return builder.add(op);
}

SsaDef emit_sampler_declaration(Builder& builder, SsaDef entryPoint, bool indexed) {
  uint32_t arraySize = indexed ? 0u : 1u;
  return builder.add(Op::DclSampler(entryPoint, 0, 0, arraySize));
}

SsaDef emit_image_descriptor_index(Builder& builder, SsaDef entryPoint, bool indexed) {
  SsaDef index;

  if (indexed) {
    auto cbvDef = builder.add(Op::DclCbv(Type(BasicType(ScalarType::eU32, 4u)).addArrayDimension(1u), entryPoint, 0u, 0u, 1u));
    auto cbvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eCbv, cbvDef, builder.makeConstant(0u)));

    index = builder.add(Op::BufferLoad(ScalarType::eU32, cbvDescriptor, builder.makeConstant(0u, 0u), 16u));
  } else {
    index = builder.makeConstant(0u);
  }

  return index;
}

SsaDef emit_load_image_descriptor(Builder& builder, SsaDef decl, SsaDef index, bool uav) {
  auto flags = builder.getOp(index).isConstant() ? OpFlags() : OpFlags(OpFlag::eNonUniform);
  return builder.add(Op::DescriptorLoad(uav ? ScalarType::eUav : ScalarType::eSrv, decl, index).setFlags(flags));
}

SsaDef emit_load_sampler_descriptor(Builder& builder, SsaDef decl, SsaDef index) {
  auto flags = builder.getOp(index).isConstant() ? OpFlags() : OpFlags(OpFlag::eNonUniform);
  return builder.add(Op::DescriptorLoad(ScalarType::eSampler, decl, index).setFlags(flags));
}

SsaDef emit_image_coord(Builder& builder, SsaDef entryPoint, ScalarType type, ResourceKind kind) {
  InterpolationModes interpolationMode = 0u;

  if (BasicType(type).isIntType())
    interpolationMode |= InterpolationMode::eFlat;

  auto input = builder.add(Op::DclInput(BasicType(type, 3u), entryPoint, 0u, 0u, interpolationMode));
  builder.add(Op::Semantic(input, 0, "TEXCOORD"));

  uint32_t count = resourceCoordComponentCount(kind);

  std::array<SsaDef, 3u> coord = { };

  for (uint32_t i = 0u; i < count; i++)
    coord.at(i) = builder.add(Op::InputLoad(type, input, builder.makeConstant(i)));

  SsaDef result = coord.at(0u);

  if (count > 1u) {
    Op op(OpCode::eCompositeConstruct, BasicType(type, count));

    for (uint32_t i = 0u; i < count; i++)
      op.addOperand(Operand(coord.at(i)));

    result = builder.add(std::move(op));
  }

  return result;
}

void emit_store_outptut(Builder& builder, SsaDef entryPoint, Type type, uint32_t index, SsaDef value) {
  auto output = builder.add(Op::DclOutput(type, entryPoint, index, 0u));
  builder.add(Op::Semantic(output, index, "SV_TARGET"));
  builder.add(Op::OutputStore(output, SsaDef(), value));
}

SsaDef emit_constant_offset(Builder& builder, ResourceKind kind) {
  uint32_t count = resourceCoordComponentCount(kind);

  Op op(OpCode::eConstant, BasicType(ScalarType::eI32, count));

  for (uint32_t i = 0u; i < count; i++)
    op.addOperand(Operand(int32_t(i) - 1));

  return builder.add(std::move(op));
}

SsaDef emit_programmable_offset(Builder& builder, SsaDef entryPoint, ResourceKind kind) {
  uint32_t count = resourceCoordComponentCount(kind);

  auto type = Type(BasicType(ScalarType::eF32, count));
  auto input = builder.add(Op::DclInput(type, entryPoint, 1u, 0u, InterpolationMode::eFlat));
  builder.add(Op::Semantic(input, 0, "OFFSET"));

  return builder.add(Op::InputLoad(type, input, SsaDef()));
}

Builder make_test_image_load(ResourceKind kind, bool uav, bool indexed) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());

  Type vec4Type = BasicType(ScalarType::eF32, 4u);

  auto dclImg = emit_image_declaration(builder, entryPoint, kind, uav, indexed, false);
  auto index = emit_image_descriptor_index(builder, entryPoint, indexed);
  auto descriptor = emit_load_image_descriptor(builder, dclImg, index, uav);
  auto coord = emit_image_coord(builder, entryPoint, ScalarType::eU32, kind);

  SsaDef mip, layer, sample, offset;

  if (!uav && !resourceIsMultisampled(kind))
    mip = builder.makeConstant(1u);

  if (resourceIsLayered(kind))
    layer = builder.makeConstant(2u);

  if (resourceIsMultisampled(kind)) {
    auto input = builder.add(Op::DclInputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eSampleId, InterpolationMode::eFlat));
    builder.add(Op::Semantic(input, 0u, "SV_SAMPLEINDEX"));

    sample = builder.add(Op::InputLoad(ScalarType::eU32, input, SsaDef()));
  }

  uint32_t outputId = 0u;
  emit_store_outptut(builder, entryPoint, vec4Type, outputId++, builder.add(
    Op::ImageLoad(vec4Type, descriptor, mip, layer, coord, sample, offset)));

  bool isCube = kind == ResourceKind::eImageCube ||
                kind == ResourceKind::eImageCubeArray;

  if (!uav && !isCube) {
    offset = emit_constant_offset(builder, kind);

    emit_store_outptut(builder, entryPoint, vec4Type, outputId++, builder.add(
      Op::ImageLoad(vec4Type, descriptor, mip, layer, coord, sample, offset)));
  }

  builder.add(Op::Return());
  return builder;
}

Builder make_test_image_query(ResourceKind kind, bool uav, bool indexed) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());

  auto dclImg = emit_image_declaration(builder, entryPoint, kind, uav, indexed, false);
  auto index = emit_image_descriptor_index(builder, entryPoint, indexed);
  auto descriptor = emit_load_image_descriptor(builder, dclImg, index, uav);

  uint32_t coordCount = resourceDimensions(kind);

  auto coordType = BasicType(ScalarType::eU32, coordCount);
  auto queryType = Type().addStructMember(coordType).addStructMember(ScalarType::eU32);

  auto outSize = builder.add(Op::DclOutput(coordType, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outSize, 0, "SV_TARGET"));

  auto outLayers = builder.add(Op::DclOutput(ScalarType::eU32, entryPoint, 1u, 0u));
  builder.add(Op::Semantic(outLayers, 1, "SV_TARGET"));

  SsaDef mip;

  if (!uav && !resourceIsMultisampled(kind))
    mip = builder.makeConstant(0u);

  auto sizeAndLayers = builder.add(Op::ImageQuerySize(queryType, descriptor, mip));

  for (uint32_t i = 0u; i < coordCount; i++) {
    auto index = builder.makeConstant(0u);

    if (coordType.isVector())
      index = builder.makeConstant(0u, i);

    builder.add(Op::OutputStore(outSize, coordType.isVector() ? builder.makeConstant(i) : ir::SsaDef(),
      builder.add(Op::CompositeExtract(ScalarType::eU32, sizeAndLayers, index))));
  }

  builder.add(Op::OutputStore(outLayers, SsaDef(),
    builder.add(Op::CompositeExtract(ScalarType::eU32, sizeAndLayers, builder.makeConstant(1u)))));

  if (!uav && !resourceIsMultisampled(kind)) {
    auto outMips = builder.add(Op::DclOutput(ScalarType::eU32, entryPoint, 2u, 0u));
    builder.add(Op::Semantic(outMips, 2, "SV_TARGET"));

    builder.add(Op::OutputStore(outMips, SsaDef(),
      builder.add(Op::ImageQueryMips(ScalarType::eU32, descriptor))));
  }

  if (resourceIsMultisampled(kind)) {
    auto outSamples = builder.add(Op::DclOutput(ScalarType::eU32, entryPoint, 3u, 0u));
    builder.add(Op::Semantic(outSamples, 3, "SV_TARGET"));

    builder.add(Op::OutputStore(outSamples, SsaDef(),
      builder.add(Op::ImageQuerySamples(ScalarType::eU32, descriptor))));
  }

  builder.add(Op::Return());
  return builder;
}

Builder make_test_image_sample(ResourceKind kind, bool indexed, bool depthCompare) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());

  Type sampledType = BasicType(ScalarType::eF32, depthCompare ? 1u : 4u);

  auto dclImg = emit_image_declaration(builder, entryPoint, kind, false, indexed, false);
  auto dclSampler = emit_sampler_declaration(builder, entryPoint, indexed);
  auto index = emit_image_descriptor_index(builder, entryPoint, indexed);
  auto image = emit_load_image_descriptor(builder, dclImg, index, false);
  auto sampler = emit_load_sampler_descriptor(builder, dclSampler, index);
  auto coord = emit_image_coord(builder, entryPoint, ScalarType::eF32, kind);

  auto depthRef = builder.add(Op::DclInput(ScalarType::eF32, entryPoint, 1u, 0u, InterpolationModes()));
  builder.add(Op::Semantic(depthRef, 0u, "DEPTH_REF"));

  auto lodBias = builder.add(Op::DclInput(ScalarType::eF32, entryPoint, 1u, 1u, InterpolationModes()));
  builder.add(Op::Semantic(lodBias, 0u, "LOD_BIAS"));

  auto lodClamp = builder.add(Op::DclInput(ScalarType::eF32, entryPoint, 1u, 2u, InterpolationModes()));
  builder.add(Op::Semantic(lodClamp, 0u, "LOD_CLAMP"));

  auto layer = builder.add(Op::DclInput(ScalarType::eF32, entryPoint, 1u, 3u, InterpolationModes()));
  builder.add(Op::Semantic(layer, 0u, "LAYER"));

  uint32_t coordCount = resourceCoordComponentCount(kind);
  auto coordType = BasicType(ScalarType::eF32, coordCount);

  auto derivCoord = builder.add(Op::DclInput(coordType, entryPoint, 2u, 0u, InterpolationModes()));
  builder.add(Op::Semantic(derivCoord, 2u, "TEXCOORD"));

  SsaDef drefValue, layerValue;

  if (depthCompare)
    drefValue = builder.add(Op::InputLoad(ScalarType::eF32, depthRef, SsaDef()));

  if (resourceIsLayered(kind))
    layerValue = builder.add(Op::InputLoad(ScalarType::eF32, layer, SsaDef()));

  SsaDef lodIndexValue;

  if (depthCompare) {
    lodIndexValue = builder.makeConstant(0.0f);
  } else {
    auto lodType = BasicType(ScalarType::eF32, 2u);
    auto lodPair = builder.add(Op::ImageComputeLod(lodType, image, sampler, coord));
    lodIndexValue = builder.add(Op::CompositeExtract(ScalarType::eF32, lodPair, builder.makeConstant(0u)));
  }

  auto lodBiasValue = builder.add(Op::InputLoad(ScalarType::eF32, lodBias, SsaDef()));
  auto lodClampValue = builder.add(Op::InputLoad(ScalarType::eF32, lodClamp, SsaDef()));

  bool isCube = kind == ResourceKind::eImageCube ||
                kind == ResourceKind::eImageCubeArray;

  uint32_t outputId = 0u;
  emit_store_outptut(builder, entryPoint, sampledType, outputId++, builder.add(
    Op::ImageSample(sampledType, image, sampler, layerValue, coord,
    SsaDef(), SsaDef(), SsaDef(), SsaDef(), SsaDef(), SsaDef(), drefValue)));

  if (!isCube) {
    emit_store_outptut(builder, entryPoint, sampledType, outputId++, builder.add(
      Op::ImageSample(sampledType, image, sampler, layerValue, coord,
      emit_constant_offset(builder, kind), SsaDef(), SsaDef(), SsaDef(), SsaDef(), SsaDef(), drefValue)));
  }

  emit_store_outptut(builder, entryPoint, sampledType, outputId++, builder.add(
    Op::ImageSample(sampledType, image, sampler, layerValue, coord,
    SsaDef(), lodIndexValue, SsaDef(), SsaDef(), SsaDef(), SsaDef(), drefValue)));

  if (!depthCompare) {
    emit_store_outptut(builder, entryPoint, sampledType, outputId++, builder.add(
      Op::ImageSample(sampledType, image, sampler, layerValue, coord,
      SsaDef(), SsaDef(), lodBiasValue, SsaDef(), SsaDef(), SsaDef(), drefValue)));

    emit_store_outptut(builder, entryPoint, sampledType, outputId++, builder.add(
      Op::ImageSample(sampledType, image, sampler, layerValue, coord,
      SsaDef(), SsaDef(), SsaDef(), lodClampValue, SsaDef(), SsaDef(), drefValue)));

    if (!isCube) {
      emit_store_outptut(builder, entryPoint, sampledType, outputId++, builder.add(
        Op::ImageSample(sampledType, image, sampler, layerValue, coord,
        emit_constant_offset(builder, kind), SsaDef(), lodBiasValue, lodClampValue, SsaDef(), SsaDef(), drefValue)));
    }

    SsaDef dx, dy;

    Op dxOp(OpCode::eCompositeConstruct, coordType);
    Op dyOp(OpCode::eCompositeConstruct, coordType);

    for (uint32_t i = 0u; i < coordCount; i++) {
      auto derivCoordIn = builder.add(Op::InputLoad(ScalarType::eF32, derivCoord,
        coordCount > 1u ? builder.makeConstant(i) : SsaDef()));

      dx = builder.add(Op::DerivX(ScalarType::eF32, derivCoordIn, DerivativeMode::eDefault));
      dy = builder.add(Op::DerivY(ScalarType::eF32, derivCoordIn, DerivativeMode::eDefault));

      if (coordCount > 1u) {
        dxOp.addOperand(Operand(dx));
        dyOp.addOperand(Operand(dy));
      }
    }

    if (coordCount > 1u) {
      dx = builder.add(dxOp);
      dy = builder.add(dyOp);
    }

    emit_store_outptut(builder, entryPoint, sampledType, outputId++, builder.add(
      Op::ImageSample(sampledType, image, sampler, layerValue, coord,
      SsaDef(), SsaDef(), SsaDef(), SsaDef(), dx, dy, drefValue)));

    if (!isCube) {
      emit_store_outptut(builder, entryPoint, sampledType, outputId++, builder.add(
        Op::ImageSample(sampledType, image, sampler, layerValue, coord,
        emit_constant_offset(builder, kind), SsaDef(), SsaDef(), SsaDef(), dx, dy, drefValue)));
    }
  }

  builder.add(Op::Return());
  return builder;
}

Builder make_test_image_gather(ResourceKind kind, bool indexed, bool depthCompare) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());

  Type resultType = BasicType(ScalarType::eF32, 4u);

  auto dclImg = emit_image_declaration(builder, entryPoint, kind, false, indexed, false);
  auto dclSampler = emit_sampler_declaration(builder, entryPoint, indexed);
  auto index = emit_image_descriptor_index(builder, entryPoint, indexed);
  auto image = emit_load_image_descriptor(builder, dclImg, index, false);
  auto sampler = emit_load_sampler_descriptor(builder, dclSampler, index);
  auto coord = emit_image_coord(builder, entryPoint, ScalarType::eF32, kind);

  auto depthRef = builder.add(Op::DclInput(ScalarType::eF32, entryPoint, 1u, 0u, InterpolationModes()));
  builder.add(Op::Semantic(depthRef, 0u, "DEPTH_REF"));

  auto layer = builder.add(Op::DclInput(ScalarType::eF32, entryPoint, 1u, 1u, InterpolationModes()));
  builder.add(Op::Semantic(layer, 0u, "LAYER"));

  uint32_t coordCount = resourceCoordComponentCount(kind);
  auto offsetType = BasicType(ScalarType::eI32, coordCount);

  auto programmableOffset = builder.add(Op::DclInput(offsetType, entryPoint, 2u, 0u, InterpolationMode::eFlat));
  builder.add(Op::Semantic(programmableOffset, 0u, "OFFSET"));

  SsaDef drefValue, layerValue, poValue;

  if (depthCompare)
    drefValue = builder.add(Op::InputLoad(ScalarType::eF32, depthRef, SsaDef()));

  if (resourceIsLayered(kind))
    layerValue = builder.add(Op::InputLoad(ScalarType::eF32, layer, SsaDef()));

  if (kind == ResourceKind::eImage2D || kind == ResourceKind::eImage2DArray)
    poValue = builder.add(Op::InputLoad(offsetType, programmableOffset, SsaDef()));

  uint32_t outputId = 0u;
  emit_store_outptut(builder, entryPoint, resultType, outputId++, builder.add(
    Op::ImageGather(resultType, image, sampler, layerValue, coord, SsaDef(), drefValue, 0u)));

  if (kind == ResourceKind::eImage2D || kind == ResourceKind::eImage2DArray) {
    emit_store_outptut(builder, entryPoint, resultType, outputId++, builder.add(
      Op::ImageGather(resultType, image, sampler, layerValue, coord,
        emit_constant_offset(builder, kind), drefValue, 0u)));
  }

  if (!depthCompare) {
    for (uint32_t i = 1u; i < 4u; i++) {
      emit_store_outptut(builder, entryPoint, resultType, outputId++, builder.add(
        Op::ImageGather(resultType, image, sampler, layerValue, coord, SsaDef(), drefValue, i)));
    }
  }

  if (poValue) {
    emit_store_outptut(builder, entryPoint, resultType, outputId++, builder.add(
      Op::ImageGather(resultType, image, sampler, layerValue, coord, poValue, drefValue, 0u)));
  }

  builder.add(Op::Return());
  return builder;
}

Builder make_test_image_store(ResourceKind kind, bool indexed) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());

  Type vec4Type = BasicType(ScalarType::eF32, 4u);

  auto dclImg = emit_image_declaration(builder, entryPoint, kind, true, indexed, false);
  auto index = emit_image_descriptor_index(builder, entryPoint, indexed);
  auto descriptor = emit_load_image_descriptor(builder, dclImg, index, true);
  auto coord = emit_image_coord(builder, entryPoint, ScalarType::eU32, kind);

  SsaDef layer, sample, offset;

  if (resourceIsLayered(kind))
    layer = builder.makeConstant(2u);

  auto color = builder.add(Op::DclInput(vec4Type, entryPoint, 2u, 0u, InterpolationMode::eNoPerspective));
  builder.add(Op::Semantic(color, 0, "COLOR"));

  builder.add(Op::ImageStore(descriptor, layer, coord,
    builder.add(Op::InputLoad(vec4Type, color, SsaDef()))));

  builder.add(Op::Return());
  return builder;
}

Builder make_test_image_atomic(ResourceKind kind, bool indexed) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());

  auto dclImg = emit_image_declaration(builder, entryPoint, kind, true, indexed, true);
  auto index = emit_image_descriptor_index(builder, entryPoint, indexed);
  auto descriptor = emit_load_image_descriptor(builder, dclImg, index, true);
  auto coord = emit_image_coord(builder, entryPoint, ScalarType::eU32, kind);

  SsaDef layer, sample, offset;

  if (resourceIsLayered(kind))
    layer = builder.makeConstant(2u);

  auto value = builder.add(Op::DclInput(ScalarType::eU32, entryPoint, 2u, 0u, InterpolationMode::eFlat));
  builder.add(Op::Semantic(value, 0, "VALUE"));

  auto result = builder.add(Op::ImageAtomic(AtomicOp::eAdd, ScalarType::eU32, descriptor,
    layer, coord, builder.add(Op::InputLoad(ScalarType::eU32, value, SsaDef()))));

  emit_store_outptut(builder, entryPoint, ScalarType::eU32, 0u, result);

  builder.add(Op::Return());
  return builder;
}


Builder test_resource_srv_image_1d_load() {
  return make_test_image_load(ResourceKind::eImage1D, false, false);
}

Builder test_resource_srv_image_1d_query() {
  return make_test_image_query(ResourceKind::eImage1D, false, false);
}

Builder test_resource_srv_image_1d_sample() {
  return make_test_image_sample(ResourceKind::eImage1D, false, false);
}

Builder test_resource_srv_image_1d_array_load() {
  return make_test_image_load(ResourceKind::eImage1DArray, false, false);
}

Builder test_resource_srv_image_1d_array_query() {
  return make_test_image_query(ResourceKind::eImage1DArray, false, false);
}

Builder test_resource_srv_image_1d_array_sample() {
  return make_test_image_sample(ResourceKind::eImage1DArray, false, false);
}

Builder test_resource_srv_image_2d_load() {
  return make_test_image_load(ResourceKind::eImage2D, false, false);
}

Builder test_resource_srv_image_2d_query() {
  return make_test_image_query(ResourceKind::eImage2D, false, false);
}

Builder test_resource_srv_image_2d_sample() {
  return make_test_image_sample(ResourceKind::eImage2D, false, false);
}

Builder test_resource_srv_image_2d_sample_depth() {
  return make_test_image_sample(ResourceKind::eImage2D, false, true);
}

Builder test_resource_srv_image_2d_gather() {
  return make_test_image_gather(ResourceKind::eImage2D, false, false);
}

Builder test_resource_srv_image_2d_gather_depth() {
  return make_test_image_gather(ResourceKind::eImage2D, false, true);
}

Builder test_resource_srv_image_2d_array_load() {
  return make_test_image_load(ResourceKind::eImage2DArray, false, false);
}

Builder test_resource_srv_image_2d_array_query() {
  return make_test_image_query(ResourceKind::eImage2DArray, false, false);
}

Builder test_resource_srv_image_2d_array_sample() {
  return make_test_image_sample(ResourceKind::eImage2DArray, false, false);
}

Builder test_resource_srv_image_2d_array_sample_depth() {
  return make_test_image_sample(ResourceKind::eImage2DArray, false, true);
}

Builder test_resource_srv_image_2d_array_gather() {
  return make_test_image_gather(ResourceKind::eImage2DArray, false, false);
}

Builder test_resource_srv_image_2d_array_gather_depth() {
  return make_test_image_gather(ResourceKind::eImage2DArray, false, true);
}

Builder test_resource_srv_image_2d_ms_load() {
  return make_test_image_load(ResourceKind::eImage2DMS, false, false);
}

Builder test_resource_srv_image_2d_ms_query() {
  return make_test_image_query(ResourceKind::eImage2DMS, false, false);
}

Builder test_resource_srv_image_2d_ms_array_load() {
  return make_test_image_load(ResourceKind::eImage2DMSArray, false, false);
}

Builder test_resource_srv_image_2d_ms_array_query() {
  return make_test_image_query(ResourceKind::eImage2DMSArray, false, false);
}

Builder test_resource_srv_image_cube_query() {
  return make_test_image_query(ResourceKind::eImageCube, false, false);
}

Builder test_resource_srv_image_cube_sample() {
  return make_test_image_sample(ResourceKind::eImageCube, false, false);
}

Builder test_resource_srv_image_cube_sample_depth() {
  return make_test_image_sample(ResourceKind::eImageCube, false, true);
}

Builder test_resource_srv_image_cube_gather() {
  return make_test_image_gather(ResourceKind::eImageCube, false, false);
}

Builder test_resource_srv_image_cube_gather_depth() {
  return make_test_image_gather(ResourceKind::eImageCube, false, true);
}

Builder test_resource_srv_image_cube_array_query() {
  return make_test_image_query(ResourceKind::eImageCubeArray, false, false);
}

Builder test_resource_srv_image_cube_array_sample() {
  return make_test_image_sample(ResourceKind::eImageCubeArray, false, false);
}

Builder test_resource_srv_image_cube_array_sample_depth() {
  return make_test_image_sample(ResourceKind::eImageCubeArray, false, true);
}

Builder test_resource_srv_image_cube_array_gather() {
  return make_test_image_gather(ResourceKind::eImageCubeArray, false, false);
}

Builder test_resource_srv_image_cube_array_gather_depth() {
  return make_test_image_gather(ResourceKind::eImageCubeArray, false, true);
}

Builder test_resource_srv_image_3d_load() {
  return make_test_image_load(ResourceKind::eImage3D, false, false);
}

Builder test_resource_srv_image_3d_query() {
  return make_test_image_query(ResourceKind::eImage3D, false, false);
}

Builder test_resource_srv_image_3d_sample() {
  return make_test_image_sample(ResourceKind::eImage3D, false, false);
}


Builder test_resource_srv_indexed_image_1d_load() {
  return make_test_image_load(ResourceKind::eImage1D, false, true);
}

Builder test_resource_srv_indexed_image_1d_query() {
  return make_test_image_query(ResourceKind::eImage1D, false, true);
}

Builder test_resource_srv_indexed_image_1d_sample() {
  return make_test_image_sample(ResourceKind::eImage1D, true, false);
}

Builder test_resource_srv_indexed_image_1d_array_load() {
  return make_test_image_load(ResourceKind::eImage1DArray, false, true);
}

Builder test_resource_srv_indexed_image_1d_array_query() {
  return make_test_image_query(ResourceKind::eImage1DArray, false, true);
}

Builder test_resource_srv_indexed_image_1d_array_sample() {
  return make_test_image_sample(ResourceKind::eImage1DArray, true, false);
}

Builder test_resource_srv_indexed_image_2d_load() {
  return make_test_image_load(ResourceKind::eImage2D, false, true);
}

Builder test_resource_srv_indexed_image_2d_query() {
  return make_test_image_query(ResourceKind::eImage2D, false, true);
}

Builder test_resource_srv_indexed_image_2d_sample() {
  return make_test_image_sample(ResourceKind::eImage2D, true, false);
}

Builder test_resource_srv_indexed_image_2d_sample_depth() {
  return make_test_image_sample(ResourceKind::eImage2D, true, true);
}

Builder test_resource_srv_indexed_image_2d_gather() {
  return make_test_image_gather(ResourceKind::eImage2D, true, false);
}

Builder test_resource_srv_indexed_image_2d_gather_depth() {
  return make_test_image_gather(ResourceKind::eImage2D, true, true);
}

Builder test_resource_srv_indexed_image_2d_array_load() {
  return make_test_image_load(ResourceKind::eImage2DArray, false, true);
}

Builder test_resource_srv_indexed_image_2d_array_query() {
  return make_test_image_query(ResourceKind::eImage2DArray, false, true);
}

Builder test_resource_srv_indexed_image_2d_array_sample() {
  return make_test_image_sample(ResourceKind::eImage2DArray, true, false);
}

Builder test_resource_srv_indexed_image_2d_array_sample_depth() {
  return make_test_image_sample(ResourceKind::eImage2DArray, true, true);
}

Builder test_resource_srv_indexed_image_2d_array_gather() {
  return make_test_image_gather(ResourceKind::eImage2DArray, true, false);
}

Builder test_resource_srv_indexed_image_2d_array_gather_depth() {
  return make_test_image_gather(ResourceKind::eImage2DArray, true, true);
}

Builder test_resource_srv_indexed_image_2d_ms_load() {
  return make_test_image_load(ResourceKind::eImage2DMS, false, true);
}

Builder test_resource_srv_indexed_image_2d_ms_query() {
  return make_test_image_query(ResourceKind::eImage2DMS, false, true);
}

Builder test_resource_srv_indexed_image_2d_ms_array_load() {
  return make_test_image_load(ResourceKind::eImage2DMSArray, false, true);
}

Builder test_resource_srv_indexed_image_2d_ms_array_query() {
  return make_test_image_query(ResourceKind::eImage2DMSArray, false, true);
}

Builder test_resource_srv_indexed_image_cube_query() {
  return make_test_image_query(ResourceKind::eImageCube, false, true);
}

Builder test_resource_srv_indexed_image_cube_sample() {
  return make_test_image_sample(ResourceKind::eImageCube, true, false);
}

Builder test_resource_srv_indexed_image_cube_sample_depth() {
  return make_test_image_sample(ResourceKind::eImageCube, true, true);
}

Builder test_resource_srv_indexed_image_cube_gather() {
  return make_test_image_gather(ResourceKind::eImageCube, true, false);
}

Builder test_resource_srv_indexed_image_cube_gather_depth() {
  return make_test_image_gather(ResourceKind::eImageCube, true, true);
}

Builder test_resource_srv_indexed_image_cube_array_query() {
  return make_test_image_query(ResourceKind::eImageCubeArray, false, true);
}

Builder test_resource_srv_indexed_image_cube_array_sample() {
  return make_test_image_sample(ResourceKind::eImageCubeArray, true, false);
}

Builder test_resource_srv_indexed_image_cube_array_sample_depth() {
  return make_test_image_sample(ResourceKind::eImageCubeArray, true, true);
}

Builder test_resource_srv_indexed_image_cube_array_gather() {
  return make_test_image_gather(ResourceKind::eImageCubeArray, true, false);
}

Builder test_resource_srv_indexed_image_cube_array_gather_depth() {
  return make_test_image_gather(ResourceKind::eImageCubeArray, true, true);
}

Builder test_resource_srv_indexed_image_3d_load() {
  return make_test_image_load(ResourceKind::eImage3D, false, true);
}

Builder test_resource_srv_indexed_image_3d_query() {
  return make_test_image_query(ResourceKind::eImage3D, false, true);
}

Builder test_resource_srv_indexed_image_3d_sample() {
  return make_test_image_sample(ResourceKind::eImage3D, true, false);
}


Builder test_resource_uav_image_1d_load() {
  return make_test_image_load(ResourceKind::eImage1D, true, false);
}

Builder test_resource_uav_image_1d_query() {
  return make_test_image_query(ResourceKind::eImage1D, true, false);
}

Builder test_resource_uav_image_1d_store() {
  return make_test_image_store(ResourceKind::eImage1D, false);
}

Builder test_resource_uav_image_1d_atomic() {
  return make_test_image_atomic(ResourceKind::eImage1D, false);
}

Builder test_resource_uav_image_1d_array_load() {
  return make_test_image_load(ResourceKind::eImage1DArray, true, false);
}

Builder test_resource_uav_image_1d_array_query() {
  return make_test_image_query(ResourceKind::eImage1DArray, true, false);
}

Builder test_resource_uav_image_1d_array_store() {
  return make_test_image_store(ResourceKind::eImage1DArray, false);
}

Builder test_resource_uav_image_1d_array_atomic() {
  return make_test_image_atomic(ResourceKind::eImage1DArray, false);
}

Builder test_resource_uav_image_2d_load() {
  return make_test_image_load(ResourceKind::eImage2D, true, false);
}

Builder test_resource_uav_image_2d_query() {
  return make_test_image_query(ResourceKind::eImage2D, true, false);
}

Builder test_resource_uav_image_2d_store() {
  return make_test_image_store(ResourceKind::eImage2D, false);
}

Builder test_resource_uav_image_2d_atomic() {
  return make_test_image_atomic(ResourceKind::eImage2D, false);
}

Builder test_resource_uav_image_2d_array_load() {
  return make_test_image_load(ResourceKind::eImage2DArray, true, false);
}

Builder test_resource_uav_image_2d_array_query() {
  return make_test_image_query(ResourceKind::eImage2DArray, true, false);
}

Builder test_resource_uav_image_2d_array_store() {
  return make_test_image_store(ResourceKind::eImage2DArray, false);
}

Builder test_resource_uav_image_2d_array_atomic() {
  return make_test_image_atomic(ResourceKind::eImage2DArray, false);
}

Builder test_resource_uav_image_3d_load() {
  return make_test_image_load(ResourceKind::eImage3D, true, false);
}

Builder test_resource_uav_image_3d_query() {
  return make_test_image_query(ResourceKind::eImage3D, true, false);
}

Builder test_resource_uav_image_3d_store() {
  return make_test_image_store(ResourceKind::eImage3D, false);
}

Builder test_resource_uav_image_3d_atomic() {
  return make_test_image_atomic(ResourceKind::eImage3D, false);
}


Builder test_resource_uav_indexed_image_1d_load() {
  return make_test_image_load(ResourceKind::eImage1D, true, true);
}

Builder test_resource_uav_indexed_image_1d_query() {
  return make_test_image_query(ResourceKind::eImage1D, true, true);
}

Builder test_resource_uav_indexed_image_1d_store() {
  return make_test_image_store(ResourceKind::eImage1D, true);
}

Builder test_resource_uav_indexed_image_1d_atomic() {
  return make_test_image_atomic(ResourceKind::eImage1D, true);
}

Builder test_resource_uav_indexed_image_1d_array_load() {
  return make_test_image_load(ResourceKind::eImage1DArray, true, true);
}

Builder test_resource_uav_indexed_image_1d_array_query() {
  return make_test_image_query(ResourceKind::eImage1DArray, true, true);
}

Builder test_resource_uav_indexed_image_1d_array_store() {
  return make_test_image_store(ResourceKind::eImage1DArray, true);
}

Builder test_resource_uav_indexed_image_1d_array_atomic() {
  return make_test_image_atomic(ResourceKind::eImage1DArray, true);
}

Builder test_resource_uav_indexed_image_2d_load() {
  return make_test_image_load(ResourceKind::eImage2D, true, true);
}

Builder test_resource_uav_indexed_image_2d_query() {
  return make_test_image_query(ResourceKind::eImage2D, true, true);
}

Builder test_resource_uav_indexed_image_2d_store() {
  return make_test_image_store(ResourceKind::eImage2D, true);
}

Builder test_resource_uav_indexed_image_2d_atomic() {
  return make_test_image_atomic(ResourceKind::eImage2D, true);
}

Builder test_resource_uav_indexed_image_2d_array_load() {
  return make_test_image_load(ResourceKind::eImage2DArray, true, true);
}

Builder test_resource_uav_indexed_image_2d_array_query() {
  return make_test_image_query(ResourceKind::eImage2DArray, true, true);
}

Builder test_resource_uav_indexed_image_2d_array_store() {
  return make_test_image_store(ResourceKind::eImage2DArray, true);
}

Builder test_resource_uav_indexed_image_2d_array_atomic() {
  return make_test_image_atomic(ResourceKind::eImage2DArray, true);
}

Builder test_resource_uav_indexed_image_3d_load() {
  return make_test_image_load(ResourceKind::eImage3D, true, true);
}

Builder test_resource_uav_indexed_image_3d_query() {
  return make_test_image_query(ResourceKind::eImage3D, true, true);
}

Builder test_resource_uav_indexed_image_3d_store() {
  return make_test_image_store(ResourceKind::eImage3D, true);
}

Builder test_resource_uav_indexed_image_3d_atomic() {
  return make_test_image_atomic(ResourceKind::eImage3D, true);
}


Builder make_test_buffer_load_sparse_feedback(bool uav) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());
  auto descriptor = emit_buffer_descriptor(builder, entryPoint,
    ResourceKind::eBufferTyped, uav, false, false);
  auto index = builder.makeConstant(12345u);

  auto texelType = BasicType(ScalarType::eF32, 4u);
  auto resultType = Type()
    .addStructMember(ScalarType::eU32)
    .addStructMember(texelType);

  auto load = builder.add(Op::BufferLoad(resultType, descriptor, index, 0u).setFlags(OpFlag::eSparseFeedback));

  auto output0Def = builder.add(Op::DclOutput(texelType, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(output0Def, 0u, "SV_TARGET"));

  auto output1Def = builder.add(Op::DclOutput(ScalarType::eF32, entryPoint, 1u, 0u));
  builder.add(Op::Semantic(output1Def, 1u, "SV_TARGET"));

  builder.add(Op::OutputStore(output0Def, SsaDef(),
    builder.add(Op::CompositeExtract(texelType, load, builder.makeConstant(1u)))));

  auto feedback = builder.add(Op::CompositeExtract(ScalarType::eU32, load, builder.makeConstant(0u)));

  builder.add(Op::OutputStore(output1Def, SsaDef(),
    builder.add(Op::Select(ScalarType::eF32, builder.add(Op::CheckSparseAccess(feedback)),
      builder.makeConstant(1.0f), builder.makeConstant(0.0f)))));

  builder.add(Op::Return());
  return builder;
}

Builder make_test_image_load_sparse_feedback(bool uav) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());

  auto texelType = BasicType(ScalarType::eF32, 4u);
  auto resultType = Type()
    .addStructMember(ScalarType::eU32)
    .addStructMember(texelType);

  auto dclImg = emit_image_declaration(builder, entryPoint, ResourceKind::eImage2D, uav, false, false);
  auto index = emit_image_descriptor_index(builder, entryPoint, false);
  auto descriptor = emit_load_image_descriptor(builder, dclImg, index, uav);
  auto coord = emit_image_coord(builder, entryPoint, ScalarType::eU32, ResourceKind::eImage2D);

  SsaDef mip;

  if (!uav)
    mip = builder.makeConstant(1u);

  uint32_t outputId = 0u;

  auto load = builder.add(Op::ImageLoad(resultType, descriptor, mip,
    SsaDef(), coord, SsaDef(), SsaDef()).setFlags(OpFlag::eSparseFeedback));

  emit_store_outptut(builder, entryPoint, texelType, outputId++, builder.add(
    Op::CompositeExtract(texelType, load, builder.makeConstant(1u))));

  auto feedback = builder.add(Op::CheckSparseAccess(
    builder.add(Op::CompositeExtract(ScalarType::eU32, load, builder.makeConstant(0u)))));

  emit_store_outptut(builder, entryPoint, ScalarType::eF32, outputId++, builder.add(
    Op::Select(ScalarType::eF32, feedback, builder.makeConstant(1.0f), builder.makeConstant(0.0f))));

  if (!uav) {
    SsaDef offset = emit_constant_offset(builder, ResourceKind::eImage2D);

    load = builder.add(Op::ImageLoad(resultType, descriptor, mip,
      SsaDef(), coord, SsaDef(), offset).setFlags(OpFlag::eSparseFeedback));

    emit_store_outptut(builder, entryPoint, texelType, outputId++, builder.add(
      Op::CompositeExtract(texelType, load, builder.makeConstant(1u))));

    feedback = builder.add(Op::CheckSparseAccess(
      builder.add(Op::CompositeExtract(ScalarType::eU32, load, builder.makeConstant(0u)))));

    emit_store_outptut(builder, entryPoint, ScalarType::eF32, outputId++, builder.add(
      Op::Select(ScalarType::eF32, feedback, builder.makeConstant(1.0f), builder.makeConstant(0.0f))));
  }

  builder.add(Op::Return());
  return builder;

}


Builder test_resource_rov() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());

  auto uav = builder.add(Op::DclUav(ScalarType::eF32, entryPoint, 0u, 0u, 1u,
    ResourceKind::eImage2D, UavFlag::eCoherent | UavFlag::eRasterizerOrdered));
  auto uavDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, uav, builder.makeConstant(0u)));

  auto positionDef = builder.add(Op::DclInputBuiltIn(Type(ScalarType::eF32, 4u),
    entryPoint, BuiltIn::ePosition, InterpolationModes()));

  auto coord = builder.add(Op::CompositeConstruct(Type(ScalarType::eI32, 2u),
    builder.add(Op::ConvertFtoI(ScalarType::eI32,
      builder.add(Op::InputLoad(ScalarType::eF32, positionDef, builder.makeConstant(0u))))),
    builder.add(Op::ConvertFtoI(ScalarType::eI32,
      builder.add(Op::InputLoad(ScalarType::eF32, positionDef, builder.makeConstant(1u)))))));

  builder.add(Op::RovScopedLockBegin(MemoryType::eUav, RovScope::ePixel));

  auto color = builder.add(Op::ImageLoad(Type(ScalarType::eF32, 4u),
    uavDescriptor, SsaDef(), SsaDef(), coord, SsaDef(), SsaDef()));

  auto red = builder.add(Op::CompositeExtract(ScalarType::eF32, color, builder.makeConstant(0u)));
  red = builder.add(Op::FMul(ScalarType::eF32, red, builder.makeConstant(2.0f)));
  color = builder.add(Op::CompositeInsert(Type(ScalarType::eF32, 4u), color, builder.makeConstant(0u), red));

  builder.add(Op::ImageStore(uavDescriptor, SsaDef(), coord, color));

  builder.add(Op::RovScopedLockEnd(MemoryType::eUav));

  builder.add(Op::Return());
  return builder;
}


Builder make_test_image_sample_sparse_feedback(bool depthCompare) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());

  Type sampledType = BasicType(ScalarType::eF32, depthCompare ? 1u : 4u);
  Type resultType = Type().addStructMember(ScalarType::eU32).addStructMember(sampledType.getBaseType(0u));

  auto dclImg = emit_image_declaration(builder, entryPoint, ResourceKind::eImage2DArray, false, false, false);
  auto dclSampler = emit_sampler_declaration(builder, entryPoint, false);
  auto index = emit_image_descriptor_index(builder, entryPoint, false);
  auto image = emit_load_image_descriptor(builder, dclImg, index, false);
  auto sampler = emit_load_sampler_descriptor(builder, dclSampler, index);
  auto coord = emit_image_coord(builder, entryPoint, ScalarType::eF32, ResourceKind::eImage2DArray);

  auto depthRef = builder.add(Op::DclInput(ScalarType::eF32, entryPoint, 1u, 0u, InterpolationModes()));
  builder.add(Op::Semantic(depthRef, 0u, "DEPTH_REF"));

  auto lodBias = builder.add(Op::DclInput(ScalarType::eF32, entryPoint, 1u, 1u, InterpolationModes()));
  builder.add(Op::Semantic(lodBias, 0u, "LOD_BIAS"));

  auto lodClamp = builder.add(Op::DclInput(ScalarType::eF32, entryPoint, 1u, 2u, InterpolationModes()));
  builder.add(Op::Semantic(lodClamp, 0u, "LOD_CLAMP"));

  auto layer = builder.add(Op::DclInput(ScalarType::eF32, entryPoint, 1u, 3u, InterpolationModes()));
  builder.add(Op::Semantic(layer, 0u, "LAYER"));

  uint32_t coordCount = resourceCoordComponentCount(ResourceKind::eImage2DArray);
  auto coordType = BasicType(ScalarType::eF32, coordCount);

  auto derivCoord = builder.add(Op::DclInput(coordType, entryPoint, 2u, 0u, InterpolationModes()));
  builder.add(Op::Semantic(derivCoord, 2u, "TEXCOORD"));

  SsaDef drefValue;

  if (depthCompare)
    drefValue = builder.add(Op::InputLoad(ScalarType::eF32, depthRef, SsaDef()));

  SsaDef layerValue = builder.add(Op::InputLoad(ScalarType::eF32, layer, SsaDef()));

  SsaDef lodIndexValue;

  if (depthCompare) {
    lodIndexValue = builder.makeConstant(0.0f);
  } else {
    auto lodType = BasicType(ScalarType::eF32, 2u);
    auto lodPair = builder.add(Op::ImageComputeLod(lodType, image, sampler, coord));
    lodIndexValue = builder.add(Op::CompositeExtract(ScalarType::eF32, lodPair, builder.makeConstant(0u)));
  }

  auto lodBiasValue = builder.add(Op::InputLoad(ScalarType::eF32, lodBias, SsaDef()));
  auto lodClampValue = builder.add(Op::InputLoad(ScalarType::eF32, lodClamp, SsaDef()));

  uint32_t outputId = 0u;

  /* Plain sample with no additional parameters */
  auto load = builder.add(Op::ImageSample(resultType, image, sampler, layerValue, coord,
    SsaDef(), SsaDef(), SsaDef(), SsaDef(), SsaDef(), SsaDef(), drefValue).setFlags(OpFlag::eSparseFeedback));

  emit_store_outptut(builder, entryPoint, sampledType, outputId++, builder.add(
    Op::CompositeExtract(sampledType, load, builder.makeConstant(1u))));

  auto feedback = builder.add(Op::CheckSparseAccess(
    builder.add(Op::CompositeExtract(ScalarType::eU32, load, builder.makeConstant(0u)))));

  emit_store_outptut(builder, entryPoint, ScalarType::eF32, outputId++, builder.add(
    Op::Select(ScalarType::eF32, feedback, builder.makeConstant(1.0f), builder.makeConstant(0.0f))));

  /* Explicit LOD */
  load = builder.add(Op::ImageSample(resultType, image, sampler, layerValue, coord,
    SsaDef(), lodIndexValue, SsaDef(), SsaDef(), SsaDef(), SsaDef(), drefValue).setFlags(OpFlag::eSparseFeedback));

  emit_store_outptut(builder, entryPoint, sampledType, outputId++, builder.add(
    Op::CompositeExtract(sampledType, load, builder.makeConstant(1u))));

  feedback = builder.add(Op::CheckSparseAccess(
    builder.add(Op::CompositeExtract(ScalarType::eU32, load, builder.makeConstant(0u)))));

  emit_store_outptut(builder, entryPoint, ScalarType::eF32, outputId++, builder.add(
    Op::Select(ScalarType::eF32, feedback, builder.makeConstant(1.0f), builder.makeConstant(0.0f))));

  if (!depthCompare) {
    /* Test with LOD bias / clamp and all sorts of fun extras */
    load = builder.add(Op::ImageSample(resultType, image, sampler, layerValue, coord,
      emit_constant_offset(builder, ResourceKind::eImage2DArray), SsaDef(),
      lodBiasValue, lodClampValue, SsaDef(), SsaDef(), drefValue).setFlags(OpFlag::eSparseFeedback));

    emit_store_outptut(builder, entryPoint, sampledType, outputId++, builder.add(
      Op::CompositeExtract(sampledType, load, builder.makeConstant(1u))));

    feedback = builder.add(Op::CheckSparseAccess(
      builder.add(Op::CompositeExtract(ScalarType::eU32, load, builder.makeConstant(0u)))));

    emit_store_outptut(builder, entryPoint, ScalarType::eF32, outputId++, builder.add(
      Op::Select(ScalarType::eF32, feedback, builder.makeConstant(1.0f), builder.makeConstant(0.0f))));

    /* Test with explicit derivatives */
    SsaDef dx, dy;

    Op dxOp(OpCode::eCompositeConstruct, coordType);
    Op dyOp(OpCode::eCompositeConstruct, coordType);

    for (uint32_t i = 0u; i < coordCount; i++) {
      auto derivCoordIn = builder.add(Op::InputLoad(ScalarType::eF32, derivCoord,
        coordCount > 1u ? builder.makeConstant(i) : SsaDef()));

      dx = builder.add(Op::DerivX(ScalarType::eF32, derivCoordIn, DerivativeMode::eDefault));
      dy = builder.add(Op::DerivY(ScalarType::eF32, derivCoordIn, DerivativeMode::eDefault));

      if (coordCount > 1u) {
        dxOp.addOperand(Operand(dx));
        dyOp.addOperand(Operand(dy));
      }
    }

    if (coordCount > 1u) {
      dx = builder.add(dxOp);
      dy = builder.add(dyOp);
    }

    load = builder.add(Op::ImageSample(resultType, image, sampler, layerValue, coord,
      SsaDef(), SsaDef(), SsaDef(), SsaDef(), dx, dy, drefValue).setFlags(OpFlag::eSparseFeedback));

    emit_store_outptut(builder, entryPoint, sampledType, outputId++, builder.add(
      Op::CompositeExtract(sampledType, load, builder.makeConstant(1u))));

    feedback = builder.add(Op::CheckSparseAccess(
      builder.add(Op::CompositeExtract(ScalarType::eU32, load, builder.makeConstant(0u)))));

    emit_store_outptut(builder, entryPoint, ScalarType::eF32, outputId++, builder.add(
      Op::Select(ScalarType::eF32, feedback, builder.makeConstant(1.0f), builder.makeConstant(0.0f))));
  }

  builder.add(Op::Return());
  return builder;
}

Builder make_test_image_gather_sparse_feedback(bool depthCompare) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::Label());

  auto sampledType = BasicType(ScalarType::eF32, 4u);
  auto resultType = Type().addStructMember(ScalarType::eU32).addStructMember(sampledType);

  auto dclImg = emit_image_declaration(builder, entryPoint, ResourceKind::eImage2D, false, false, false);
  auto dclSampler = emit_sampler_declaration(builder, entryPoint, false);
  auto index = emit_image_descriptor_index(builder, entryPoint, false);
  auto image = emit_load_image_descriptor(builder, dclImg, index, false);
  auto sampler = emit_load_sampler_descriptor(builder, dclSampler, index);
  auto coord = emit_image_coord(builder, entryPoint, ScalarType::eF32, ResourceKind::eImage2D);

  auto depthRef = builder.add(Op::DclInput(ScalarType::eF32, entryPoint, 1u, 0u, InterpolationModes()));
  builder.add(Op::Semantic(depthRef, 0u, "DEPTH_REF"));

  uint32_t coordCount = resourceCoordComponentCount(ResourceKind::eImage2D);
  auto offsetType = BasicType(ScalarType::eI32, coordCount);

  auto programmableOffset = builder.add(Op::DclInput(offsetType, entryPoint, 2u, 0u, InterpolationMode::eFlat));
  builder.add(Op::Semantic(programmableOffset, 0u, "OFFSET"));

  SsaDef drefValue;

  if (depthCompare)
    drefValue = builder.add(Op::InputLoad(ScalarType::eF32, depthRef, SsaDef()));

  SsaDef poValue = builder.add(Op::InputLoad(offsetType, programmableOffset, SsaDef()));

  uint32_t outputId = 0u;

  auto load = builder.add(Op::ImageGather(resultType, image, sampler,
    SsaDef(), coord, SsaDef(), drefValue, 0u).setFlags(OpFlag::eSparseFeedback));

  emit_store_outptut(builder, entryPoint, sampledType, outputId++, builder.add(
    Op::CompositeExtract(sampledType, load, builder.makeConstant(1u))));

  auto feedback = builder.add(Op::CheckSparseAccess(
    builder.add(Op::CompositeExtract(ScalarType::eU32, load, builder.makeConstant(0u)))));

  emit_store_outptut(builder, entryPoint, ScalarType::eF32, outputId++, builder.add(
    Op::Select(ScalarType::eF32, feedback, builder.makeConstant(1.0f), builder.makeConstant(0.0f))));

  /* Test constant offset */
  load = builder.add(Op::ImageGather(resultType, image, sampler, SsaDef(), coord,
    emit_constant_offset(builder, ResourceKind::eImage2D), drefValue, 0u).setFlags(OpFlag::eSparseFeedback));

  emit_store_outptut(builder, entryPoint, sampledType, outputId++, builder.add(
    Op::CompositeExtract(sampledType, load, builder.makeConstant(1u))));

  feedback = builder.add(Op::CheckSparseAccess(
    builder.add(Op::CompositeExtract(ScalarType::eU32, load, builder.makeConstant(0u)))));

  emit_store_outptut(builder, entryPoint, ScalarType::eF32, outputId++, builder.add(
    Op::Select(ScalarType::eF32, feedback, builder.makeConstant(1.0f), builder.makeConstant(0.0f))));

  /* Test programmable offset */
  load = builder.add(Op::ImageGather(resultType, image, sampler, SsaDef(), coord,
    poValue, drefValue, 0u).setFlags(OpFlag::eSparseFeedback));

  emit_store_outptut(builder, entryPoint, sampledType, outputId++, builder.add(
    Op::CompositeExtract(sampledType, load, builder.makeConstant(1u))));

  feedback = builder.add(Op::CheckSparseAccess(
    builder.add(Op::CompositeExtract(ScalarType::eU32, load, builder.makeConstant(0u)))));

  emit_store_outptut(builder, entryPoint, ScalarType::eF32, outputId++, builder.add(
    Op::Select(ScalarType::eF32, feedback, builder.makeConstant(1.0f), builder.makeConstant(0.0f))));

  builder.add(Op::Return());
  return builder;
}


Builder test_resource_srv_buffer_load_sparse_feedback() {
  return make_test_buffer_load_sparse_feedback(false);
}

Builder test_resource_srv_image_load_sparse_feedback() {
  return make_test_image_load_sparse_feedback(false);
}

Builder test_resource_srv_image_sample_sparse_feedback() {
  return make_test_image_sample_sparse_feedback(false);
}

Builder test_resource_srv_image_sample_depth_sparse_feedback() {
  return make_test_image_sample_sparse_feedback(true);
}

Builder test_resource_srv_image_gather_sparse_feedback() {
  return make_test_image_gather_sparse_feedback(false);
}

Builder test_resource_srv_image_gather_depth_sparse_feedback() {
  return make_test_image_gather_sparse_feedback(true);
}


Builder test_resource_uav_buffer_load_sparse_feedback() {
  return make_test_buffer_load_sparse_feedback(true);
}

Builder test_resource_uav_image_load_sparse_feedback() {
  return make_test_image_load_sparse_feedback(true);
}

}
