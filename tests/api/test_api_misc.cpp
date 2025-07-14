#include "test_api_misc.h"

namespace dxbc_spv::test_api {

Builder test_misc_scratch() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::SetCsWorkgroupSize(entryPoint, 1u, 1u, 1u));
  builder.add(Op::Label());

  auto threadIdDef = builder.add(Op::DclInputBuiltIn(BasicType(ScalarType::eU32, 3u), entryPoint, BuiltIn::eGlobalThreadId));
  builder.add(Op::Semantic(threadIdDef, 0u, "SV_DispatchThreadID"));

  auto threadId = builder.add(Op::InputLoad(ScalarType::eU32, threadIdDef, builder.makeConstant(0u)));

  auto srvDataType = Type(ScalarType::eF32).addArrayDimension(4u).addArrayDimension(0u);
  auto srvDataDef = builder.add(Op::DclSrv(srvDataType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured));
  auto srvDataDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eSrv, srvDataDef, builder.makeConstant(0u)));
  builder.add(Op::DebugName(srvDataDef, "t0"));

  auto srvIndexType = Type(ScalarType::eU32).addArrayDimension(64u).addArrayDimension(0u);
  auto srvIndexDef = builder.add(Op::DclSrv(srvIndexType, entryPoint, 0u, 1u, 1u, ResourceKind::eBufferStructured));
  auto srvIndexDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eSrv, srvIndexDef, builder.makeConstant(0u)));
  builder.add(Op::DebugName(srvIndexDef, "t1"));

  auto uavDataType = Type(ScalarType::eF32).addArrayDimension(4u).addArrayDimension(0u);
  auto uavDataDef = builder.add(Op::DclUav(uavDataType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured, UavFlag::eWriteOnly));
  auto uavDataDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, uavDataDef, builder.makeConstant(0u)));
  builder.add(Op::DebugName(uavDataDef, "u0"));

  auto scratchVecType = Type(BasicType(ScalarType::eF32, 4u)).addArrayDimension(16u);
  auto scratchSumType = Type(BasicType(ScalarType::eF32, 4u)).addArrayDimension(4u);
  auto scratchIntType = Type(BasicType(ScalarType::eU32)).addArrayDimension(16u);

  auto scratchVecDef = builder.add(Op::DclScratch(scratchVecType, entryPoint));
  auto scratchSumDef = builder.add(Op::DclScratch(scratchSumType, entryPoint));
  auto scratchIntDef = builder.add(Op::DclScratch(scratchIntType, entryPoint));

  builder.add(Op::DebugName(scratchVecDef, "x0"));
  builder.add(Op::DebugName(scratchSumDef, "x1"));
  builder.add(Op::DebugName(scratchIntDef, "x2"));

  auto vec4Type = BasicType(ScalarType::eF32, 4u);

  for (uint32_t i = 0u; i < 4u; i++) {
    builder.add(Op::ScratchStore(scratchSumDef, builder.makeConstant(i),
      builder.makeConstant(1.0f, 2.0f, 3.0f, 4.0f)));
    builder.add(Op::ScratchStore(scratchIntDef, builder.makeConstant(i),
      builder.makeConstant(-1u)));
  }

  for (uint32_t i = 0u; i < 16u; i++) {
    auto dataDef = builder.add(Op::BufferLoad(vec4Type, srvDataDescriptor, builder.makeConstant(i, 0u), 16u));
    builder.add(Op::ScratchStore(scratchVecDef, builder.makeConstant(i), dataDef));
  }

  for (uint32_t i = 0u; i < 16u; i++) {
    for (uint32_t j = 0u; j < 4u; j++) {
      auto srvIndexDef = builder.add(Op::CompositeConstruct(
        BasicType(ScalarType::eU32, 2u), threadId, builder.makeConstant(4u * i + j)));
      auto srcIndexDef = builder.add(Op::BufferLoad(ScalarType::eU32, srvIndexDescriptor, srvIndexDef, 4u));
      auto sumIndexDef = builder.add(Op::IAnd(ScalarType::eU32, srcIndexDef, builder.makeConstant(0x3u)));

      auto sumDef = builder.add(Op::ScratchLoad(ScalarType::eF32, scratchSumDef,
        builder.add(Op::CompositeConstruct(BasicType(ScalarType::eU32, 2u), sumIndexDef, builder.makeConstant(i % 4u)))));
      auto srcDef = builder.add(Op::ScratchLoad(ScalarType::eF32, scratchVecDef,
        builder.add(Op::CompositeConstruct(BasicType(ScalarType::eU32, 2u), srcIndexDef, builder.makeConstant(j)))));

      sumDef = builder.add(Op::FAdd(ScalarType::eF32, sumDef, srcDef));
      builder.add(Op::ScratchStore(scratchSumDef,
        builder.add(Op::CompositeConstruct(BasicType(ScalarType::eU32, 2u), sumIndexDef, builder.makeConstant(i % 4u))), sumDef));

      auto intDef = builder.add(Op::ScratchLoad(ScalarType::eU32, scratchIntDef, srcIndexDef));
      intDef = builder.add(Op::IAdd(ScalarType::eU32, intDef, builder.makeConstant(1u)));
      builder.add(Op::ScratchStore(scratchIntDef, srcIndexDef, intDef));
    }
  }

  auto storeIndexDef = builder.add(Op::ScratchLoad(ScalarType::eU32, scratchIntDef, builder.makeConstant(15u)));
  storeIndexDef = builder.add(Op::IAdd(ScalarType::eU32, threadId, storeIndexDef));

  auto storeDataDef = builder.add(Op::ScratchLoad(vec4Type, scratchSumDef, builder.makeConstant(3u)));
  builder.add(Op::BufferStore(uavDataDescriptor,
    builder.add(Op::CompositeConstruct(BasicType(ScalarType::eU32, 2u), storeIndexDef, builder.makeConstant(0u))),
    storeDataDef, 4u));

  builder.add(Op::Return());
  return builder;
}

}
