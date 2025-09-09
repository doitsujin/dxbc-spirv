#include "test_api_misc.h"

#include "../../ir/passes/ir_pass_function.h"
#include "../../ir/passes/ir_pass_lower_consume.h"
#include "../../ir/passes/ir_pass_remove_unused.h"
#include "../../ir/passes/ir_pass_ssa.h"

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

  auto tidCond = builder.add(Op::ULt(ir::ScalarType::eBool, tid, loopCounterPhiDef));
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
  auto cond = builder.add(Op::INe(ScalarType::eBool, loopCounterIterDef, builder.makeConstant(0u)));
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
  builder.add(Op::LdsAtomic(AtomicOp::eAdd, ScalarType::eVoid, ldsDef, builder.makeConstant(0u), dataDef));
  auto resultDef = builder.add(Op::LdsAtomic(AtomicOp::eXor, ScalarType::eI32, ldsDef, builder.makeConstant(0u), dataDef));

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

Builder test_misc_ps_demote() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);
  auto baseLabel = builder.add(Op::Label());

  auto coordInDef = builder.add(Op::DclInput(Type(ScalarType::eF32, 2u), entryPoint, 0u, 0u, InterpolationModes()));
  builder.add(Op::Semantic(coordInDef, 0u, "TEXCOORD"));
  builder.add(Op::DebugName(coordInDef, "v0"));

  auto colorOutDef = builder.add(Op::DclOutput(Type(ScalarType::eF32, 4u), entryPoint, 0u, 0u));
  builder.add(Op::Semantic(colorOutDef, 0u, "SV_TARGET"));
  builder.add(Op::DebugName(colorOutDef, "o0"));

  auto textureDef = builder.add(Op::DclSrv(ScalarType::eF32, entryPoint, 0u, 0u, 1u, ResourceKind::eImage2D));
  builder.add(Op::DebugName(textureDef, "t0"));

  auto samplerDef = builder.add(Op::DclSampler(entryPoint, 0u, 0u, 1u));
  builder.add(Op::DebugName(samplerDef, "s0"));

  auto textureDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eSrv, textureDef, builder.makeConstant(0u)));
  auto samplerDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eSampler, samplerDef, builder.makeConstant(0u)));

  auto coord = builder.add(Op::InputLoad(Type(ScalarType::eF32, 2u), coordInDef, SsaDef()));

  auto color = builder.add(Op::ImageSample(Type(ScalarType::eF32, 4u),
    textureDescriptor, samplerDescriptor, SsaDef(), coord, SsaDef(), SsaDef(), SsaDef(), SsaDef(), SsaDef(), SsaDef(), SsaDef()));

  auto alpha = builder.add(Op::CompositeExtract(ScalarType::eF32, color, builder.makeConstant(3u)));
  auto alphaTest = builder.add(Op::FLt(ScalarType::eBool, alpha, builder.makeConstant(0.005f)));

  auto labelDiscard = builder.add(Op::Label());
  builder.add(Op::Demote());

  auto labelMerge(builder.add(Op::Label()));

  builder.addBefore(labelDiscard, Op::BranchConditional(alphaTest, labelDiscard, labelMerge));
  builder.addBefore(labelMerge, Op::Branch(labelMerge));

  builder.rewriteOp(baseLabel, Op::LabelSelection(labelMerge));

  builder.add(Op::OutputStore(colorOutDef, SsaDef(), color));
  builder.add(Op::Return());
  return builder;
}

Builder test_misc_ps_early_z() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);

  builder.add(Op::SetPsEarlyFragmentTest(entryPoint));
  builder.add(Op::Label());

  auto indexInDef = builder.add(Op::DclInput(ScalarType::eU32, entryPoint, 0u, 0u, InterpolationMode::eFlat));
  builder.add(Op::Semantic(indexInDef, 0u, "INDEX"));
  builder.add(Op::DebugName(indexInDef, "v0"));

  auto uavDef = builder.add(Op::DclUav(Type(ScalarType::eU32).addArrayDimension(0u),
    entryPoint, 0u, 0u, 1u, ResourceKind::eBufferRaw, UavFlags()));
  builder.add(Op::DebugName(uavDef, "u0"));

  auto uavDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, uavDef, builder.makeConstant(0u)));
  auto index = builder.add(Op::InputLoad(ScalarType::eU32, indexInDef, SsaDef()));

  builder.add(Op::BufferAtomic(AtomicOp::eOr, Type(),
    uavDescriptor, index, builder.makeConstant(1u)));

  builder.add(Op::Return());
  return builder;
}

Builder test_misc_function() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);
  auto mainFunc = ir::SsaDef(builder.getOp(entryPoint).getOperand(0u));

  auto outDef = builder.add(Op::DclOutput(Type(ScalarType::eF32, 4u), entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outDef, 0u, "SV_TARGET"));

  /* Emit function */
  builder.setCursor(SsaDef());

  auto func = builder.add(Op::Function(Type()));
  builder.add(Op::Label());
  builder.add(Op::OutputStore(outDef, SsaDef(), builder.makeConstant(1.0f, 2.0f, 3.0f, 4.0f)));
  builder.add(Op::Return());
  auto funcEnd = builder.add(Op::FunctionEnd());

  /* Emit function call */
  builder.setCursor(mainFunc);
  builder.add(Op::Label());
  builder.add(Op::FunctionCall(Type(), func));
  builder.add(Op::Return());

  /* Move function to correct spot */
  builder.reorderBefore(mainFunc, func, funcEnd);
  return builder;
}

Builder test_misc_function_with_args() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);
  auto mainFunc = ir::SsaDef(builder.getOp(entryPoint).getOperand(0u));

  auto outDef = builder.add(Op::DclOutput(Type(ScalarType::eF32, 4u), entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outDef, 0u, "SV_TARGET"));

  /* Emit function */
  builder.setCursor(SsaDef());

  auto paramR = builder.add(Op::DclParam(ScalarType::eF32));
  auto paramG = builder.add(Op::DclParam(ScalarType::eF32));
  auto paramB = builder.add(Op::DclParam(ScalarType::eF32));
  auto paramA = builder.add(Op::DclParam(ScalarType::eF32));

  builder.add(Op::DebugName(paramR, "r"));
  builder.add(Op::DebugName(paramG, "g"));
  builder.add(Op::DebugName(paramB, "b"));
  builder.add(Op::DebugName(paramA, "a"));

  auto func = builder.add(Op::Function(Type())
    .addParam(paramR)
    .addParam(paramG)
    .addParam(paramB)
    .addParam(paramA));

  builder.add(Op::Label());
  builder.add(Op::OutputStore(outDef, builder.makeConstant(0u),
    builder.add(Op::ParamLoad(ScalarType::eF32, func, paramR))));
  builder.add(Op::OutputStore(outDef, builder.makeConstant(1u),
    builder.add(Op::ParamLoad(ScalarType::eF32, func, paramG))));
  builder.add(Op::OutputStore(outDef, builder.makeConstant(2u),
    builder.add(Op::ParamLoad(ScalarType::eF32, func, paramB))));
  builder.add(Op::OutputStore(outDef, builder.makeConstant(3u),
    builder.add(Op::ParamLoad(ScalarType::eF32, func, paramA))));
  builder.add(Op::Return());
  auto funcEnd = builder.add(Op::FunctionEnd());

  /* Emit function call */
  builder.setCursor(mainFunc);
  builder.add(Op::Label());
  builder.add(Op::FunctionCall(Type(), func)
    .addParam(builder.makeConstant(1.0f))
    .addParam(builder.makeConstant(2.0f))
    .addParam(builder.makeConstant(3.0f))
    .addParam(builder.makeConstant(4.0f)));
  builder.add(Op::Return());

  /* Move function to correct spot */
  builder.reorderBefore(mainFunc, func, funcEnd);
  return builder;
}

Builder test_misc_function_with_return() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);
  auto mainFunc = ir::SsaDef(builder.getOp(entryPoint).getOperand(0u));

  auto outDef = builder.add(Op::DclOutput(Type(ScalarType::eF32, 4u), entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outDef, 0u, "SV_TARGET"));

  /* Emit function */
  builder.setCursor(SsaDef());

  auto paramR = builder.add(Op::DclParam(ScalarType::eF32));
  auto paramG = builder.add(Op::DclParam(ScalarType::eF32));
  auto paramB = builder.add(Op::DclParam(ScalarType::eF32));
  auto paramA = builder.add(Op::DclParam(ScalarType::eF32));

  builder.add(Op::DebugName(paramR, "r"));
  builder.add(Op::DebugName(paramG, "g"));
  builder.add(Op::DebugName(paramB, "b"));
  builder.add(Op::DebugName(paramA, "a"));

  auto func = builder.add(Op::Function(Type(ScalarType::eF32, 4u))
    .addParam(paramR)
    .addParam(paramG)
    .addParam(paramB)
    .addParam(paramA));

  builder.add(Op::Label());
  auto a = builder.add(Op::ParamLoad(ScalarType::eF32, func, paramA));
  auto r = builder.add(Op::ParamLoad(ScalarType::eF32, func, paramR));
  r = builder.add(Op::FMul(ScalarType::eF32, r, a));
  auto g = builder.add(Op::ParamLoad(ScalarType::eF32, func, paramG));
  g = builder.add(Op::FMul(ScalarType::eF32, g, a));
  auto b = builder.add(Op::ParamLoad(ScalarType::eF32, func, paramB));
  b = builder.add(Op::FMul(ScalarType::eF32, b, a));
  builder.add(Op::Return(Type(ScalarType::eF32, 4u),
    builder.add(Op::CompositeConstruct(Type(ScalarType::eF32, 4u), r, g, b, a))));
  auto funcEnd = builder.add(Op::FunctionEnd());

  /* Emit function call */
  builder.setCursor(mainFunc);
  builder.add(Op::Label());
  auto color = builder.add(Op::FunctionCall(Type(ScalarType::eF32, 4u), func)
    .addParam(builder.makeConstant(0.2f))
    .addParam(builder.makeConstant(0.5f))
    .addParam(builder.makeConstant(1.0f))
    .addParam(builder.makeConstant(0.8f)));
  builder.add(Op::OutputStore(outDef, SsaDef(), color));
  builder.add(Op::Return());

  /* Move function to correct spot */
  builder.reorderBefore(mainFunc, func, funcEnd);
  return builder;
}

Builder test_misc_function_with_undef() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);
  auto mainFunc = ir::SsaDef(builder.getOp(entryPoint).getOperand(0u));

  auto inDef = builder.add(Op::DclInput(ScalarType::eF32, entryPoint, 0u, 0u, InterpolationModes()));
  builder.add(Op::Semantic(inDef, 0u, "INPUT"));

  auto outDef = builder.add(Op::DclOutput(ScalarType::eF32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outDef, 0u, "SV_TARGET"));

  /* Emit function */
  builder.setCursor(SsaDef());

  auto paramA = builder.add(Op::DclParam(ScalarType::eF32));
  builder.add(Op::DebugName(paramA, "a"));

  auto func = builder.add(Op::Function(ScalarType::eF32).addParam(paramA));

  auto mainBlock = builder.add(Op::Label());
  auto a = builder.add(Op::ParamLoad(ScalarType::eF32, func, paramA));
  auto cond = builder.add(Op::FGe(ScalarType::eBool, a, builder.makeConstant(0.0f)));

  auto ifBlock = builder.add(Op::Label());
  auto b = builder.add(Op::FSqrt(ScalarType::eF32, a));

  auto mergeBlock = builder.add(Op::Label());
  builder.addBefore(mergeBlock, Op::Branch(mergeBlock));
  builder.addBefore(ifBlock, Op::BranchConditional(cond, ifBlock, mergeBlock));
  builder.rewriteOp(mainBlock, Op::LabelSelection(mergeBlock));

  auto returnValue = builder.add(Op::Phi(ScalarType::eF32)
    .addPhi(mainBlock, builder.makeUndef(ScalarType::eF32))
    .addPhi(ifBlock, b));

  builder.add(Op::Return(ScalarType::eF32, returnValue));
  auto funcEnd = builder.add(Op::FunctionEnd());

  /* Emit function call */
  builder.setCursor(mainFunc);
  builder.add(Op::Label());

  auto v = builder.add(Op::InputLoad(ScalarType::eF32, inDef, SsaDef()));
  cond = builder.add(Op::FGe(ScalarType::eBool, v, builder.makeConstant(0.0f)));
  v = builder.add(Op::FunctionCall(ScalarType::eF32, func).addParam(v));
  v = builder.add(Op::Select(ScalarType::eF32, cond, v, builder.makeUndef(ScalarType::eF32)));
  builder.add(Op::OutputStore(outDef, SsaDef(), v));
  builder.add(Op::Return());

  /* Move function to correct spot */
  builder.reorderBefore(mainFunc, func, funcEnd);
  return builder;
}

Builder test_cfg_if() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);
  auto lStart = builder.add(Op::Label());

  auto outDef = builder.add(Op::DclOutput(ScalarType::eF32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outDef, 0u, "SV_TARGET"));

  auto posDef = builder.add(Op::DclInputBuiltIn(BasicType(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition, InterpolationModes()));
  builder.add(Op::Semantic(posDef, 0u, "SV_POSITION"));

  auto z = builder.add(Op::InputLoad(ScalarType::eF32, posDef, builder.makeConstant(2u)));
  auto w = builder.add(Op::InputLoad(ScalarType::eF32, posDef, builder.makeConstant(3u)));

  auto cond = builder.add(Op::FNe(ScalarType::eBool, w, builder.makeConstant(0.0f)));

  auto lTrue = builder.add(Op::Label());
  auto zDiv = builder.add(Op::FDiv(ScalarType::eF32, z, w));

  auto lMerge = builder.add(Op::Label());
  builder.addBefore(lTrue, Op::BranchConditional(cond, lTrue, lMerge));
  builder.addBefore(lMerge, Op::Branch(lMerge));
  builder.rewriteOp(lStart, Op::LabelSelection(lMerge));

  auto zPhi = builder.add(Op::Phi(ScalarType::eF32)
    .addPhi(lStart, z)
    .addPhi(lTrue, zDiv));

  builder.add(Op::OutputStore(outDef, SsaDef(), zPhi));
  builder.add(Op::Return());
  return builder;
}


Builder test_cfg_if_else() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);
  auto lStart = builder.add(Op::Label());

  auto outDef = builder.add(Op::DclOutput(ScalarType::eF32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outDef, 0u, "SV_TARGET"));

  auto posDef = builder.add(Op::DclInputBuiltIn(BasicType(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition, InterpolationModes()));
  builder.add(Op::Semantic(posDef, 0u, "SV_POSITION"));

  auto z = builder.add(Op::InputLoad(ScalarType::eF32, posDef, builder.makeConstant(2u)));
  auto w = builder.add(Op::InputLoad(ScalarType::eF32, posDef, builder.makeConstant(3u)));

  auto cond = builder.add(Op::FGt(ScalarType::eBool, z, w));

  auto lTrue = builder.add(Op::Label());
  auto zDivT = builder.add(Op::FDiv(ScalarType::eF32, z, w));

  auto lFalse = builder.add(Op::Label());
  auto zDivF = builder.add(Op::FDiv(ScalarType::eF32, w, z));

  auto lMerge = builder.add(Op::Label());
  builder.addBefore(lTrue, Op::BranchConditional(cond, lTrue, lFalse));
  builder.addBefore(lFalse, Op::Branch(lMerge));
  builder.addBefore(lMerge, Op::Branch(lMerge));
  builder.rewriteOp(lStart, Op::LabelSelection(lMerge));

  auto zPhi = builder.add(Op::Phi(ScalarType::eF32)
    .addPhi(lTrue, zDivT)
    .addPhi(lFalse, zDivF));

  builder.add(Op::OutputStore(outDef, SsaDef(), zPhi));
  builder.add(Op::Return());
  return builder;
}


Builder test_cfg_loop_once() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);
  builder.add(Op::Label());

  auto outDef = builder.add(Op::DclOutput(ScalarType::eF32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outDef, 0u, "SV_TARGET"));

  auto posDef = builder.add(Op::DclInputBuiltIn(BasicType(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition, InterpolationModes()));
  builder.add(Op::Semantic(posDef, 0u, "SV_POSITION"));

  auto x = builder.add(Op::InputLoad(ScalarType::eF32, posDef, builder.makeConstant(0u)));

  auto loopHeader = builder.add(Op::Label());
  builder.addBefore(loopHeader, Op::Branch(loopHeader));

  auto loopBody = builder.add(Op::Label());
  builder.addBefore(loopBody, Op::Branch(loopBody));

  auto xAdd = builder.add(Op::FAdd(ScalarType::eF32, x, builder.makeConstant(1.0f)));

  auto loopContinue = builder.add(Op::Label());
  builder.add(Op::Branch(loopHeader));

  auto loopMerge = builder.add(Op::Label());
  builder.addBefore(loopContinue, Op::Branch(loopMerge));
  builder.rewriteOp(loopHeader, Op::LabelLoop(loopMerge, loopContinue));

  builder.add(Op::OutputStore(outDef, SsaDef(), xAdd));
  builder.add(Op::Return());
  return builder;
}


Builder test_cfg_loop_infinite() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);
  auto labelStart = builder.add(Op::Label());

  auto outDef = builder.add(Op::DclOutput(ScalarType::eF32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outDef, 0u, "SV_TARGET"));

  auto posDef = builder.add(Op::DclInputBuiltIn(BasicType(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition, InterpolationModes()));
  builder.add(Op::Semantic(posDef, 0u, "SV_POSITION"));

  auto x = builder.add(Op::InputLoad(ScalarType::eF32, posDef, builder.makeConstant(0u)));

  auto loopHeader = builder.add(Op::Label());
  builder.addBefore(loopHeader, Op::Branch(loopHeader));

  auto xPhi = builder.add(Op::Phi(ScalarType::eF32));

  auto loopBody = builder.add(Op::Label());
  builder.addBefore(loopBody, Op::Branch(loopBody));

  auto xAdd = builder.add(Op::FAdd(ScalarType::eF32, xPhi, builder.makeConstant(1.0f)));
  auto xCond = builder.add(Op::FGe(ScalarType::eBool, xAdd, builder.makeConstant(10000.0f)));

  auto returnBlock = builder.add(Op::Label());
  builder.add(Op::OutputStore(outDef, SsaDef(), xAdd));
  builder.add(Op::Return());

  auto returnMerge = builder.add(Op::Label());
  builder.addBefore(returnBlock, Op::BranchConditional(xCond, returnBlock, returnMerge));
  builder.rewriteOp(loopBody, Op::LabelSelection(returnMerge));

  auto loopContinue = builder.add(Op::Label());
  builder.addBefore(loopContinue, Op::Branch(loopContinue));

  builder.rewriteOp(xPhi, Op::Phi(ScalarType::eF32)
    .addPhi(labelStart, x)
    .addPhi(loopContinue, xAdd));

  builder.add(Op::Branch(loopHeader));

  auto loopMerge = builder.add(Op::Label());
  builder.add(Op::Unreachable());
  builder.rewriteOp(loopHeader, Op::LabelLoop(loopMerge, loopContinue));
  return builder;
}


Builder test_cfg_switch_simple() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);
  auto labelStart = builder.add(Op::Label());

  auto outDef = builder.add(Op::DclOutput(ScalarType::eF32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outDef, 0u, "SV_TARGET"));

  auto posDef = builder.add(Op::DclInputBuiltIn(BasicType(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition, InterpolationModes()));
  builder.add(Op::Semantic(posDef, 0u, "SV_POSITION"));

  auto selDef = builder.add(Op::DclInput(ScalarType::eI32, entryPoint, 1u, 0u, InterpolationMode::eFlat));
  builder.add(Op::Semantic(selDef, 0u, "SEL"));

  auto sel = builder.add(Op::InputLoad(ScalarType::eI32, selDef, SsaDef()));

  auto case3 = builder.add(Op::Label());
  auto x = builder.add(Op::InputLoad(ScalarType::eF32, posDef, builder.makeConstant(0u)));
  builder.add(Op::OutputStore(outDef, SsaDef(), x));

  auto case7 = builder.add(Op::Label());
  auto y = builder.add(Op::InputLoad(ScalarType::eF32, posDef, builder.makeConstant(1u)));
  builder.add(Op::OutputStore(outDef, SsaDef(), y));

  auto case9 = builder.add(Op::Label());
  auto z = builder.add(Op::InputLoad(ScalarType::eF32, posDef, builder.makeConstant(2u)));
  builder.add(Op::OutputStore(outDef, SsaDef(), z));

  auto caseDefault = builder.add(Op::Label());
  auto w = builder.add(Op::InputLoad(ScalarType::eF32, posDef, builder.makeConstant(3u)));
  builder.add(Op::OutputStore(outDef, SsaDef(), w));

  auto switchMerge = builder.add(Op::Label());
  builder.rewriteOp(labelStart, Op::LabelSelection(switchMerge));

  builder.addBefore(switchMerge, Op::Branch(switchMerge));
  builder.addBefore(caseDefault, Op::Branch(switchMerge));
  builder.addBefore(case9, Op::Branch(switchMerge));
  builder.addBefore(case7, Op::Branch(switchMerge));

  builder.addBefore(case3, Op::Switch(sel, caseDefault)
    .addCase(builder.makeConstant(3), case3)
    .addCase(builder.makeConstant(6), case7)
    .addCase(builder.makeConstant(7), case7)
    .addCase(builder.makeConstant(9), case9));

  builder.add(Op::Return());
  return builder;
}


Builder test_cfg_switch_complex() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);
  auto labelStart = builder.add(Op::Label());

  auto outDef = builder.add(Op::DclOutput(ScalarType::eF32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outDef, 0u, "SV_TARGET"));

  auto posDef = builder.add(Op::DclInputBuiltIn(BasicType(ScalarType::eF32, 4u), entryPoint, BuiltIn::ePosition, InterpolationModes()));
  builder.add(Op::Semantic(posDef, 0u, "SV_POSITION"));

  auto selDef = builder.add(Op::DclInput(ScalarType::eI32, entryPoint, 1u, 0u, InterpolationMode::eFlat));
  builder.add(Op::Semantic(selDef, 0u, "SEL"));

  auto sel = builder.add(Op::InputLoad(ScalarType::eI32, selDef, SsaDef()));

  auto case3 = builder.add(Op::Label());
  auto x = builder.add(Op::InputLoad(ScalarType::eF32, posDef, builder.makeConstant(0u)));

  auto case7 = builder.add(Op::Label());
  auto y = builder.add(Op::InputLoad(ScalarType::eF32, posDef, builder.makeConstant(1u)));

  auto case9 = builder.add(Op::Label());
  auto a = builder.add(Op::Phi(ScalarType::eF32)
    .addPhi(labelStart, builder.makeConstant(-1.0f))
    .addPhi(case7, y));
  auto z = builder.add(Op::InputLoad(ScalarType::eF32, posDef, builder.makeConstant(2u)));

  z = builder.add(Op::FAdd(ScalarType::eF32, z, a));

  auto case17 = builder.add(Op::Label());
  auto w = builder.add(Op::InputLoad(ScalarType::eF32, posDef, builder.makeConstant(3u)));

  auto switchMerge = builder.add(Op::Label());
  builder.rewriteOp(labelStart, Op::LabelSelection(switchMerge));

  builder.addBefore(switchMerge, Op::Branch(switchMerge));
  builder.addBefore(case7, Op::Branch(switchMerge));
  builder.addBefore(case9, Op::Branch(case9));
  builder.addBefore(case17, Op::Branch(switchMerge));

  builder.addBefore(case3, Op::Switch(sel, switchMerge)
    .addCase(builder.makeConstant(3), case3)
    .addCase(builder.makeConstant(7), case7)
    .addCase(builder.makeConstant(9), case9)
    .addCase(builder.makeConstant(17), case17));

  auto phi = builder.add(Op::Phi(ScalarType::eF32)
    .addPhi(labelStart, builder.makeConstant(0.0f))
    .addPhi(case3, x)
    .addPhi(case9, z)
    .addPhi(case17, w));

  builder.add(Op::OutputStore(outDef, SsaDef(), phi));
  builder.add(Op::Return());
  return builder;
}


Builder test_pass_function_shared_temps() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);
  auto mainFunc = builder.getOpForOperand(entryPoint, 0u).getDef();

  /* Helper function that writes texture coordinates to a two temporaries */
  auto coordX = builder.add(Op::DclTmp(ScalarType::eF32, entryPoint));
  auto coordY = builder.add(Op::DclTmp(ScalarType::eF32, entryPoint));

  auto coordIn = builder.add(Op::DclInput(BasicType(ScalarType::eF32, 2u), entryPoint, 0u, 0u, InterpolationModes()));
  builder.add(Op::Semantic(coordIn, 0u, "TEXCOORD"));

  auto texCoordFunc = builder.addBefore(mainFunc, Op::Function(ScalarType::eVoid));
  builder.setCursor(texCoordFunc);
  builder.add(Op::DebugName(texCoordFunc, "get_texcoord"));

  builder.add(Op::Label());
  builder.add(Op::TmpStore(coordX, builder.add(Op::InputLoad(ScalarType::eF32, coordIn, builder.makeConstant(0u)))));
  builder.add(Op::TmpStore(coordY, builder.add(Op::InputLoad(ScalarType::eF32, coordIn, builder.makeConstant(1u)))));
  builder.add(Op::Return());
  builder.add(Op::FunctionEnd());

  /* Helper function that computes derivatives using temporaries */
  auto derivX = builder.add(Op::DclTmp(BasicType(ScalarType::eF32, 2u), entryPoint));
  auto derivY = builder.add(Op::DclTmp(BasicType(ScalarType::eF32, 2u), entryPoint));

  auto derivativeFunc = builder.addBefore(mainFunc, Op::Function(ScalarType::eVoid));
  builder.setCursor(derivativeFunc);
  builder.add(Op::DebugName(derivativeFunc, "get_derivatives"));

  builder.add(Op::Label());
  auto x = builder.add(Op::TmpLoad(ScalarType::eF32, coordX));
  auto y = builder.add(Op::TmpLoad(ScalarType::eF32, coordY));

  auto dxx = builder.add(Op::DerivX(ScalarType::eF32, x, DerivativeMode::eCoarse));
  auto dxy = builder.add(Op::DerivY(ScalarType::eF32, x, DerivativeMode::eCoarse));
  auto dyx = builder.add(Op::DerivX(ScalarType::eF32, y, DerivativeMode::eCoarse));
  auto dyy = builder.add(Op::DerivY(ScalarType::eF32, y, DerivativeMode::eCoarse));

  builder.add(Op::TmpStore(derivX, builder.add(Op::CompositeConstruct(BasicType(ScalarType::eF32, 2u), dxx, dxy))));
  builder.add(Op::TmpStore(derivY, builder.add(Op::CompositeConstruct(BasicType(ScalarType::eF32, 2u), dyx, dyy))));

  builder.add(Op::Return());
  builder.add(Op::FunctionEnd());

  /* Helper function that samples a texture using derivatives */
  auto color = builder.add(Op::DclTmp(BasicType(ScalarType::eF32, 4u), entryPoint));

  auto sampler = builder.add(Op::DclSampler(entryPoint, 0u, 0u, 1u));
  auto texture = builder.add(Op::DclSrv(ScalarType::eF32, entryPoint, 0u, 0u, 1u, ResourceKind::eImage2D));

  auto sampleFunc = builder.addBefore(mainFunc, Op::Function(ScalarType::eVoid));
  builder.setCursor(sampleFunc);
  builder.add(Op::DebugName(sampleFunc, "sample_texture"));

  builder.add(Op::Label());
  builder.add(Op::FunctionCall(ScalarType::eVoid, texCoordFunc));
  builder.add(Op::FunctionCall(ScalarType::eVoid, derivativeFunc));

  auto value = builder.add(Op::ImageSample(BasicType(ScalarType::eF32, 4u),
    builder.add(Op::DescriptorLoad(ScalarType::eSrv, texture, builder.makeConstant(0u))),
    builder.add(Op::DescriptorLoad(ScalarType::eSampler, sampler, builder.makeConstant(0u))),
    SsaDef(),
    builder.add(Op::CompositeConstruct(BasicType(ScalarType::eF32, 2u),
      builder.add(Op::TmpLoad(ScalarType::eF32, coordX)),
      builder.add(Op::TmpLoad(ScalarType::eF32, coordY)))),
    SsaDef(),
    SsaDef(),
    SsaDef(),
    SsaDef(),
    builder.add(Op::TmpLoad(BasicType(ScalarType::eF32, 2u), derivX)),
    builder.add(Op::TmpLoad(BasicType(ScalarType::eF32, 2u), derivY)),
    SsaDef()));

  builder.add(Op::TmpStore(color, value));
  builder.add(Op::Return());
  builder.add(Op::FunctionEnd());

  /* Helper function to return a constant scaling factor for derivatives */
  auto scale = builder.add(Op::DclTmp(ScalarType::eF32, entryPoint));

  auto scaleFunc = builder.addBefore(mainFunc, Op::Function(ScalarType::eVoid));
  builder.setCursor(scaleFunc);
  builder.add(Op::DebugName(scaleFunc, "get_scale"));
  builder.add(Op::Label());
  builder.add(Op::TmpStore(scale, builder.makeConstant(2.0f)));
  builder.add(Op::Return());
  builder.add(Op::FunctionEnd());

  /* Main function */
  auto colorOut = builder.add(Op::DclOutput(BasicType(ScalarType::eF32, 4u), entryPoint, 0u, 0u));
  auto derivXOut = builder.add(Op::DclOutput(BasicType(ScalarType::eF32, 2u), entryPoint, 1u, 0u));
  auto derivYOut = builder.add(Op::DclOutput(BasicType(ScalarType::eF32, 2u), entryPoint, 2u, 0u));

  builder.add(Op::Semantic(colorOut, 0u, "SV_TARGET"));
  builder.add(Op::Semantic(derivXOut, 1u, "SV_TARGET"));
  builder.add(Op::Semantic(derivYOut, 2u, "SV_TARGET"));

  builder.setCursor(mainFunc);
  builder.add(Op::Label());

  builder.add(Op::FunctionCall(ScalarType::eVoid, sampleFunc));
  builder.add(Op::FunctionCall(ScalarType::eVoid, scaleFunc));

  auto factor = builder.add(Op::TmpLoad(ScalarType::eF32, scale));

  builder.add(Op::TmpStore(coordX, builder.add(Op::FMul(ScalarType::eF32, builder.add(Op::TmpLoad(ScalarType::eF32, coordX)), factor))));
  builder.add(Op::TmpStore(coordY, builder.add(Op::FMul(ScalarType::eF32, builder.add(Op::TmpLoad(ScalarType::eF32, coordY)), factor))));

  builder.add(Op::FunctionCall(ScalarType::eVoid, derivativeFunc));

  builder.add(Op::OutputStore(colorOut, SsaDef(), builder.add(Op::TmpLoad(BasicType(ScalarType::eF32, 4u), color))));
  builder.add(Op::OutputStore(derivXOut, SsaDef(), builder.add(Op::TmpLoad(BasicType(ScalarType::eF32, 2u), derivX))));
  builder.add(Op::OutputStore(derivYOut, SsaDef(), builder.add(Op::TmpLoad(BasicType(ScalarType::eF32, 2u), derivY))));

  builder.add(Op::Return());

  ir::FunctionCleanupPass::runResolveSharedTempPass(builder);
  ir::SsaConstructionPass::runPass(builder);
  ir::LowerConsumePass::runResolveCastChainsPass(builder);
  ir::LowerConsumePass::runLowerConsumePass(builder);
  ir::RemoveUnusedPass::runPass(builder);
  ir::FunctionCleanupPass::runRemoveParameterPass(builder);
  ir::RemoveUnusedPass::runPass(builder);

  return builder;
}

}
