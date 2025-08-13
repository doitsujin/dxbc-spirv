#include "test_api_pass_buffer_kind.h"

#include "../../ir/passes/ir_pass_arithmetic.h"
#include "../../ir/passes/ir_pass_buffer_kind.h"
#include "../../ir/passes/ir_pass_lower_consume.h"
#include "../../ir/passes/ir_pass_remove_unused.h"
#include "../../ir/passes/ir_pass_scalarize.h"

namespace dxbc_spv::test_api {

static Builder& run_passes(Builder& b) {
  ir::ArithmeticPass::Options arithmeticOptions = { };

  ir::ConvertBufferKindPass::Options bufferOptions = { };
  bufferOptions.useTypedForRaw = true;
  bufferOptions.useTypedForStructured = true;
  bufferOptions.useTypedForSparseFeedback = true;
  bufferOptions.useRawForTypedAtomic = true;

  ir::ConvertBufferKindPass::runPass(b, bufferOptions);
  ir::LowerConsumePass::runLowerConsumePass(b);

  while (true) {
    bool progress = false;

    progress |= ir::ArithmeticPass::runPass(b, arithmeticOptions);
    progress |= ir::LowerConsumePass::runResolveCastChainsPass(b);
    progress |= ir::ScalarizePass::runResolveRedundantCompositesPass(b);

    if (!progress)
      break;

    ir::RemoveUnusedPass::runPass(b);
  }

  ir::ArithmeticPass::runLateLoweringPasses(b, arithmeticOptions);
  return b;
}


static std::tuple<Builder, SsaDef, SsaDef, SsaDef> setup_builder() {
  Builder builder;

  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);
  builder.add(Op::Label());

  auto address = builder.add(Op::DclInput(ScalarType::eU32, entryPoint, 0u, 0u, InterpolationMode::eFlat));
  builder.add(Op::Semantic(address, 0u, "ADDRESS"));

  auto output = builder.add(Op::DclOutput(BasicType(ScalarType::eF32, 4u), entryPoint, 0u, 0u));
  builder.add(Op::Semantic(output, 0u, "SV_TARGET"));

  address = builder.add(Op::InputLoad(ScalarType::eU32, address, SsaDef()));

  builder.addAfter(address, Op::Return());
  return std::tuple(builder, entryPoint, address, output);
}


Builder test_pass_buffer_kind_typed_uav_to_raw() {
  auto [builder, entry, address, output] = setup_builder();

  auto uav = builder.add(Op::DclUav(ScalarType::eI32, entry, 0u, 0u, 1u, ResourceKind::eBufferTyped, UavFlag::eFixedFormat));
  auto uavDesc = builder.add(Op::DescriptorLoad(ScalarType::eUav, uav, builder.makeConstant(0u)));

  /* Query buffer size */
  auto size = builder.add(Op::BufferQuerySize(uavDesc));
  auto offset = builder.add(Op::ISub(ScalarType::eU32, size, builder.makeConstant(1u)));

  /* Load value to feed into atomic and emit atomic */
  auto increment = builder.add(Op::BufferLoad(Type(ScalarType::eI32, 4u), uavDesc, address, 0u));
  auto result = builder.add(Op::Cast(Type(ScalarType::eF32, 4u), increment));

  increment = builder.add(Op::CompositeExtract(ScalarType::eI32, increment, builder.makeConstant(0u)));

  auto atomic = builder.add(Op::BufferAtomic(AtomicOp::eAdd, ScalarType::eI32, uavDesc, offset, increment));
  atomic = builder.add(Op::CompositeConstruct(Type(ScalarType::eI32, 4u), atomic, atomic, atomic, atomic));

  builder.add(Op::BufferStore(uavDesc, address, atomic, 0u));
  builder.add(Op::OutputStore(output, SsaDef(), result));
  return run_passes(builder);
}


Builder test_pass_buffer_kind_raw_srv_to_typed() {
  auto [builder, entry, address, output] = setup_builder();

  auto srv = builder.add(Op::DclSrv(Type(ScalarType::eF32).addArrayDimension(0u), entry, 0u, 0u, 1u, ResourceKind::eBufferRaw));
  auto srvDesc = builder.add(Op::DescriptorLoad(ScalarType::eSrv, srv, builder.makeConstant(0u)));

  /* Query buffer size */
  auto size = builder.add(Op::BufferQuerySize(srvDesc));

  /* Do a scalar load */
  auto factor = builder.add(Op::BufferLoad(ScalarType::eF32, srvDesc, address, 4u));
  address = builder.add(Op::ISub(ScalarType::eU32, size, address));

  /* Do a vector load */
  auto vector = builder.add(Op::BufferLoad(BasicType(ScalarType::eF32, 4u), srvDesc, address, 4u));

  for (uint32_t i = 0u; i < 4u; i++) {
    auto value = builder.add(Op::CompositeExtract(ScalarType::eF32, vector, builder.makeConstant(i)));
    value = builder.add(Op::FMul(ScalarType::eF32, value, factor));
    builder.add(Op::OutputStore(output, builder.makeConstant(i), value));
  }

  return run_passes(builder);
}


Builder test_pass_buffer_kind_raw_srv_to_typed_sparse() {
  auto [builder, entry, address, output] = setup_builder();

  auto srv = builder.add(Op::DclSrv(Type(ScalarType::eF32).addArrayDimension(0u), entry, 0u, 0u, 1u, ResourceKind::eBufferRaw));
  auto srvDesc = builder.add(Op::DescriptorLoad(ScalarType::eSrv, srv, builder.makeConstant(0u)));

  /* Query buffer size */
  auto size = builder.add(Op::BufferQuerySize(srvDesc));

  /* Do a scalar load */
  auto factorType = Type().addStructMember(ScalarType::eU32).addStructMember(ScalarType::eF32);
  auto factor = builder.add(Op::BufferLoad(factorType, srvDesc, address, 4u).setFlags(OpFlag::eSparseFeedback));
  address = builder.add(Op::ISub(ScalarType::eU32, size, address));

  /* Do a vector load */
  auto vectorType = Type().addStructMember(ScalarType::eU32).addStructMember(BasicType(ScalarType::eF32, 4u));
  auto vector = builder.add(Op::BufferLoad(vectorType, srvDesc, address, 4u).setFlags(OpFlag::eSparseFeedback));

  /* Combine sparse feedbacks */
  auto factorFeedback = builder.add(Op::CheckSparseAccess(builder.add(Op::CompositeExtract(ScalarType::eU32, factor, builder.makeConstant(0u)))));
  auto vectorFeedback = builder.add(Op::CheckSparseAccess(builder.add(Op::CompositeExtract(ScalarType::eU32, vector, builder.makeConstant(0u)))));

  auto feedback = builder.add(Op::BAnd(ScalarType::eBool, factorFeedback, vectorFeedback));

  factor = builder.add(Op::CompositeExtract(ScalarType::eF32, factor, builder.makeConstant(1u)));

  for (uint32_t i = 0u; i < 4u; i++) {
    auto value = builder.add(Op::CompositeExtract(ScalarType::eF32, vector, builder.makeConstant(1u, i)));
    value = builder.add(Op::FMul(ScalarType::eF32, value, factor));
    value = builder.add(Op::Select(ScalarType::eF32, feedback, value, builder.makeConstant(-1.0f)));
    builder.add(Op::OutputStore(output, builder.makeConstant(i), value));
  }

  return run_passes(builder);
}


Builder test_pass_buffer_kind_raw_uav_to_typed() {
  auto [builder, entry, address, output] = setup_builder();

  auto uav = builder.add(Op::DclUav(Type(ScalarType::eU32).addArrayDimension(0u), entry, 0u, 0u, 1u, ResourceKind::eBufferRaw, UavFlags()));
  auto uavDesc = builder.add(Op::DescriptorLoad(ScalarType::eUav, uav, builder.makeConstant(0u)));

  /* Query buffer size */
  auto size = builder.add(Op::BufferQuerySize(uavDesc));

  /* Do a scalar load */
  auto factor = builder.add(Op::BufferLoad(ScalarType::eU32, uavDesc, address, 4u));
  address = builder.add(Op::IAdd(ScalarType::eU32, size, builder.makeConstant(1u)));

  /* Do a scalar store */
  builder.add(Op::BufferStore(uavDesc, address, factor, 4u));

  /* Do an atomic */
  address = builder.add(Op::ISub(ScalarType::eU32, size, builder.makeConstant(1u)));
  auto atomic = builder.add(Op::BufferAtomic(AtomicOp::eAdd, ScalarType::eU32, uavDesc, address, builder.makeConstant(1u)));

  /* Do a vector load */
  auto vector = builder.add(Op::BufferLoad(BasicType(ScalarType::eU32, 4u), uavDesc, atomic, 4u));

  /* Do a vector store */
  atomic = builder.add(Op::IAdd(ScalarType::eU32, atomic, builder.makeConstant(4u)));
  builder.add(Op::BufferStore(uavDesc, atomic, vector, 4u));

  for (uint32_t i = 0u; i < 4u; i++) {
    auto value = builder.add(Op::CompositeExtract(ScalarType::eU32, vector, builder.makeConstant(i)));
    value = builder.add(Op::IMul(ScalarType::eU32, value, factor));
    value = builder.add(Op::ConvertItoF(ScalarType::eF32, value));
    builder.add(Op::OutputStore(output, builder.makeConstant(i), value));
  }

  return run_passes(builder);
}


Builder test_pass_buffer_kind_structured_srv_to_typed() {
  auto [builder, entry, address, output] = setup_builder();

  auto srvType = Type()
    .addStructMember(ScalarType::eU32)
    .addStructMember(ScalarType::eF32, 4u)
    .addStructMember(ScalarType::eF32)
    .addArrayDimension(0u);

  auto srv = builder.add(Op::DclSrv(srvType, entry, 0u, 0u, 1u, ResourceKind::eBufferStructured));
  auto srvDesc = builder.add(Op::DescriptorLoad(ScalarType::eSrv, srv, builder.makeConstant(0u)));

  /* Query buffer size */
  auto size = builder.add(Op::BufferQuerySize(srvDesc));
  address = builder.add(Op::UMin(ScalarType::eU32, address, size));

  /* Do a scalar load */
  auto factorAddress = builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 2u), address, builder.makeConstant(2u)));
  auto factor = builder.add(Op::BufferLoad(ScalarType::eF32, srvDesc, factorAddress, 4u));

  /* Do a vector load */
  auto vectorAddress = builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 2u), address, builder.makeConstant(1u)));
  auto vector = builder.add(Op::BufferLoad(BasicType(ScalarType::eF32, 4u), srvDesc, vectorAddress, 4u));

  for (uint32_t i = 0u; i < 4u; i++) {
    auto value = builder.add(Op::CompositeExtract(ScalarType::eF32, vector, builder.makeConstant(i)));
    value = builder.add(Op::FMul(ScalarType::eF32, value, factor));
    builder.add(Op::OutputStore(output, builder.makeConstant(i), value));
  }

  return run_passes(builder);
}


Builder test_pass_buffer_kind_structured_srv_to_typed_sparse() {
  auto [builder, entry, address, output] = setup_builder();

  auto srvType = Type()
    .addStructMember(ScalarType::eU32)
    .addStructMember(ScalarType::eF32, 4u)
    .addStructMember(ScalarType::eF32)
    .addArrayDimension(0u);

  auto srv = builder.add(Op::DclSrv(srvType, entry, 0u, 0u, 1u, ResourceKind::eBufferStructured));
  auto srvDesc = builder.add(Op::DescriptorLoad(ScalarType::eSrv, srv, builder.makeConstant(0u)));

  /* Query buffer size */
  auto size = builder.add(Op::BufferQuerySize(srvDesc));
  address = builder.add(Op::UMin(ScalarType::eU32, address, size));

  /* Do a scalar load */
  auto factorAddress = builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 2u), address, builder.makeConstant(2u)));
  auto factorType = Type().addStructMember(ScalarType::eU32).addStructMember(ScalarType::eF32);
  auto factor = builder.add(Op::BufferLoad(factorType, srvDesc, factorAddress, 4u).setFlags(OpFlag::eSparseFeedback));

  /* Do a vector load */
  auto vectorAddress = builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 2u), address, builder.makeConstant(1u)));
  auto vectorType = Type().addStructMember(ScalarType::eU32).addStructMember(BasicType(ScalarType::eF32, 4u));
  auto vector = builder.add(Op::BufferLoad(vectorType, srvDesc, vectorAddress, 4u).setFlags(OpFlag::eSparseFeedback));

  /* Combine sparse feedbacks */
  auto factorFeedback = builder.add(Op::CheckSparseAccess(builder.add(Op::CompositeExtract(ScalarType::eU32, factor, builder.makeConstant(0u)))));
  auto vectorFeedback = builder.add(Op::CheckSparseAccess(builder.add(Op::CompositeExtract(ScalarType::eU32, vector, builder.makeConstant(0u)))));

  auto feedback = builder.add(Op::BAnd(ScalarType::eBool, factorFeedback, vectorFeedback));

  factor = builder.add(Op::CompositeExtract(ScalarType::eF32, factor, builder.makeConstant(1u)));

  for (uint32_t i = 0u; i < 4u; i++) {
    auto value = builder.add(Op::CompositeExtract(ScalarType::eF32, vector, builder.makeConstant(1u, i)));
    value = builder.add(Op::FMul(ScalarType::eF32, value, factor));

    value = builder.add(Op::Select(ScalarType::eF32, feedback, value, builder.makeConstant(-1.0f)));
    builder.add(Op::OutputStore(output, builder.makeConstant(i), value));
  }

  return run_passes(builder);
}


Builder test_pass_buffer_kind_structured_uav_to_typed() {
  auto [builder, entry, address, output] = setup_builder();

  auto srvType = Type()
    .addStructMember(ScalarType::eU32)
    .addStructMember(ScalarType::eF32, 4u)
    .addStructMember(ScalarType::eF32)
    .addArrayDimension(0u);

  auto uav = builder.add(Op::DclUav(srvType, entry, 0u, 0u, 1u, ResourceKind::eBufferStructured, UavFlags()));
  auto uavDesc = builder.add(Op::DescriptorLoad(ScalarType::eUav, uav, builder.makeConstant(0u)));

  /* Query buffer size */
  auto size = builder.add(Op::BufferQuerySize(uavDesc));
  address = builder.add(Op::UMin(ScalarType::eU32, address, size));

  /* Do a scalar load */
  auto factorAddress = builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 2u), address, builder.makeConstant(2u)));
  auto factor = builder.add(Op::BufferLoad(ScalarType::eF32, uavDesc, factorAddress, 4u));

  /* Do a vector load */
  auto vectorAddress = builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 2u), address, builder.makeConstant(1u)));
  auto vector = builder.add(Op::BufferLoad(BasicType(ScalarType::eF32, 4u), uavDesc, vectorAddress, 8u));

  /* Do an atomic */
  auto atomicAddress = builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 2u), address, builder.makeConstant(0u)));
  auto atomic = builder.add(Op::BufferAtomic(AtomicOp::eAdd, ScalarType::eU32, uavDesc, atomicAddress, builder.makeConstant(1u)));

  /* Do a scalar store */
  factorAddress = builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 2u), atomic, builder.makeConstant(2u)));
  builder.add(Op::BufferStore(uavDesc, factorAddress, factor, 4u));

  /* Do a vector store */
  vectorAddress = builder.add(Op::CompositeConstruct(Type(ScalarType::eU32, 2u), atomic, builder.makeConstant(1u)));
  builder.add(Op::BufferStore(uavDesc, vectorAddress, vector, 8u));

  for (uint32_t i = 0u; i < 4u; i++) {
    auto value = builder.add(Op::CompositeExtract(ScalarType::eF32, vector, builder.makeConstant(i)));
    value = builder.add(Op::FMul(ScalarType::eF32, value, factor));
    builder.add(Op::OutputStore(output, builder.makeConstant(i), value));
  }

  return run_passes(builder);
}

}
