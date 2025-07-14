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


Builder test_misc_lds() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::SetCsWorkgroupSize(entryPoint, 32u, 1u, 1u));
  auto baseBlock = builder.add(Op::Label());

  auto gidInputDef = builder.add(Op::DclInputBuiltIn(BasicType(ScalarType::eU32, 3u), entryPoint, BuiltIn::eGlobalThreadId));
  builder.add(Op::Semantic(gidInputDef, 0u, "SV_DispatchThreadID"));

  auto tidInputDef = builder.add(Op::DclInputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eLocalThreadIndex));
  builder.add(Op::Semantic(tidInputDef, 0u, "SV_GroupIndex"));

  auto gid = builder.add(Op::InputLoad(ScalarType::eU32, gidInputDef, builder.makeConstant(0u)));
  auto tid = builder.add(Op::InputLoad(ScalarType::eU32, tidInputDef, SsaDef()));

  auto bufferType = Type(ScalarType::eF32).addArrayDimension(1u).addArrayDimension(0u);

  auto srvDef = builder.add(Op::DclSrv(bufferType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured));
  auto srvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eSrv, srvDef, builder.makeConstant(0u)));
  builder.add(Op::DebugName(srvDef, "t0"));

  auto uavDef = builder.add(Op::DclUav(bufferType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured, UavFlag::eWriteOnly));
  auto uavDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, uavDef, builder.makeConstant(0u)));
  builder.add(Op::DebugName(uavDef, "u0"));

  auto ldsType = Type(ScalarType::eF32).addArrayDimension(32u);
  auto ldsDef = builder.add(Op::DclLds(ldsType, entryPoint));
  builder.add(Op::DebugName(ldsDef, "g0"));

  auto dataDef = builder.add(Op::BufferLoad(ScalarType::eF32, srvDescriptor,
    builder.add(Op::CompositeConstruct(BasicType(ScalarType::eU32, 2u), gid, builder.makeConstant(0u))), 4u));
  builder.add(Op::LdsStore(ldsDef, tid, dataDef));

  auto loopCounterInitDef = builder.makeConstant(16u);
  auto reductionInit = builder.makeUndef(ScalarType::eF32);

  /* Reserve loop labels */
  auto labelLoopBody = builder.add(Op::Label());
  auto labelLoopContinue = builder.add(Op::Label());
  auto labelLoopMerge = builder.add(Op::Label());

  /* Loop header with phis etc */
  auto labelLoopHeader = builder.addBefore(labelLoopBody,
    Op::LabelLoop(labelLoopMerge, labelLoopBody));
  builder.addBefore(labelLoopHeader, Op::Branch(labelLoopHeader));

  builder.setCursor(labelLoopHeader);

  auto loopCounterPhi = Op::Phi(ScalarType::eU32);
  auto loopCounterPhiDef = builder.add(loopCounterPhi);

  builder.add(Op::Branch(labelLoopBody));

  /* Loop body */
  builder.setCursor(labelLoopBody);

  auto labelReductionRead = builder.addAfter(labelLoopBody, Op::Label());
  auto labelReductionMerge = builder.addAfter(labelReductionRead, Op::Label());
  builder.rewriteOp(labelLoopBody, Op::LabelSelection(labelReductionMerge));

  builder.add(Op::Barrier(Scope::eWorkgroup, Scope::eWorkgroup, MemoryType::eLds));

  auto tidCond = builder.add(Op::ULt(tid, loopCounterPhiDef));
  builder.add(Op::BranchConditional(tidCond, labelReductionRead, labelReductionMerge));

  builder.setCursor(labelReductionRead);

  auto reductionValue = builder.add(Op::FAdd(ScalarType::eF32,
    builder.add(Op::LdsLoad(ScalarType::eF32, ldsDef, tid)),
    builder.add(Op::LdsLoad(ScalarType::eF32, ldsDef, builder.add(Op::IAdd(ScalarType::eU32, tid, loopCounterPhiDef))))));

  builder.add(Op::Branch(labelReductionMerge));

  builder.setCursor(labelReductionMerge);

  auto labelReductionWrite = builder.addAfter(labelReductionMerge, Op::Label());
  builder.rewriteOp(labelReductionMerge, Op::LabelSelection(labelLoopContinue));

  auto reductionPhi = builder.add(Op::Phi(ScalarType::eF32)
    .addPhi(labelLoopBody, reductionInit)
    .addPhi(labelReductionRead, reductionValue));

  builder.add(Op::Barrier(Scope::eWorkgroup, Scope::eWorkgroup, MemoryType::eLds));
  builder.add(Op::BranchConditional(tidCond, labelReductionWrite, labelLoopContinue));

  builder.setCursor(labelReductionWrite);
  builder.add(Op::LdsStore(ldsDef, tid, reductionPhi));
  builder.add(Op::Branch(labelLoopContinue));

  /* Loop continue block */
  builder.setCursor(labelLoopContinue);

  /* Adjust loop counter and properly emit phi */
  auto loopCounterIterDef = builder.add(Op::UShr(ScalarType::eU32, loopCounterPhiDef, builder.makeConstant(1u)));

  loopCounterPhi.addPhi(baseBlock, loopCounterInitDef);
  loopCounterPhi.addPhi(labelLoopContinue, loopCounterIterDef);

  builder.rewriteOp(loopCounterPhiDef, std::move(loopCounterPhi));

  /* Check loop counter value and branch */
  auto cond = builder.add(Op::INe(loopCounterIterDef, builder.makeConstant(0u)));
  builder.add(Op::BranchConditional(cond, labelLoopHeader, labelLoopMerge));

  builder.setCursor(labelLoopMerge);
  builder.add(Op::Barrier(Scope::eWorkgroup, Scope::eWorkgroup, MemoryType::eLds));

  /* Write back */
  builder.add(Op::BufferStore(uavDescriptor,
    builder.add(Op::CompositeConstruct(BasicType(ScalarType::eU32, 2u), gid, builder.makeConstant(0u))),
    builder.add(Op::LdsLoad(ScalarType::eF32, ldsDef, builder.makeConstant(0u))), 4u));

  builder.add(Op::Return());
  return builder;
}

Builder test_misc_lds_atomic() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::SetCsWorkgroupSize(entryPoint, 1u, 1u, 1u));
  builder.add(Op::Label());

  auto gidInputDef = builder.add(Op::DclInputBuiltIn(BasicType(ScalarType::eU32, 3u), entryPoint, BuiltIn::eGlobalThreadId));
  auto gid = builder.add(Op::InputLoad(ScalarType::eU32, gidInputDef, builder.makeConstant(0u)));

  builder.add(Op::Semantic(gidInputDef, 0u, "SV_DispatchThreadID"));

  auto bufferType = Type(ScalarType::eI32).addArrayDimension(1u).addArrayDimension(0u);

  auto srvDef = builder.add(Op::DclSrv(bufferType, entryPoint, 0u, 1u, 1u, ResourceKind::eBufferStructured));
  auto srvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eSrv, srvDef, builder.makeConstant(0u)));

  auto uavDef = builder.add(Op::DclUav(bufferType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured, UavFlag::eWriteOnly));
  auto uavDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, uavDef, builder.makeConstant(0u)));

  auto ldsType = Type(ScalarType::eI32).addArrayDimension(1u);
  auto ldsDef = builder.add(Op::DclLds(ldsType, entryPoint));
  builder.add(Op::DebugName(ldsDef, "g0"));

  auto dataDef = builder.add(Op::BufferLoad(ScalarType::eI32, srvDescriptor,
    builder.add(Op::CompositeConstruct(BasicType(ScalarType::eU32, 2u), gid, builder.makeConstant(0u))), 4u));
  auto resultDef = builder.add(Op::LdsAtomic(AtomicOp::eAdd, ScalarType::eI32, ldsDef, builder.makeConstant(0u), dataDef));

  builder.add(Op::BufferStore(uavDescriptor,
    builder.add(Op::CompositeConstruct(BasicType(ScalarType::eU32, 2u), gid, builder.makeConstant(0u))),
    resultDef, 4u));

  builder.add(Op::Return());
  return builder;
}

Builder test_misc_constant_load() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eVertex);
  builder.add(Op::Label());

  auto inDef = builder.add(Op::DclInputBuiltIn(ScalarType::eU32, entryPoint, BuiltIn::eVertexId));
  builder.add(Op::Semantic(inDef, 0u, "SV_VERTEXID"));

  auto posOutDef = builder.add(Op::DclOutputBuiltIn(BasicType(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition));
  builder.add(Op::Semantic(posOutDef, 0u, "SV_POSITION"));

  auto constantDef = builder.add(Op(OpCode::eConstant, Type(BasicType(ScalarType::eF32, 4u)).addArrayDimension(5u))
    .addOperands(Operand(0.0f), Operand(0.0f), Operand(0.0f), Operand(0.0f))
    .addOperands(Operand(0.0f), Operand(1.0f), Operand(0.0f), Operand(0.0f))
    .addOperands(Operand(1.0f), Operand(0.0f), Operand(0.0f), Operand(0.0f))
    .addOperands(Operand(1.0f), Operand(1.0f), Operand(0.0f), Operand(0.0f))
    .addOperands(Operand(0.0f), Operand(0.0f), Operand(0.0f), Operand(0.0f)));

  auto vertexId = builder.add(Op::InputLoad(ScalarType::eU32, inDef, SsaDef()));
  auto indexDef = builder.add(Op::UMin(ScalarType::eU32, vertexId, builder.makeConstant(4u)));

  auto data = builder.add(Op::ConstantLoad(
    BasicType(ScalarType::eF32, 4), constantDef, indexDef));

  builder.add(Op::OutputStore(posOutDef, SsaDef(), data));
  builder.add(Op::Return());
  return builder;
}

}
