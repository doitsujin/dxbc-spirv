#include "test_api_pass_scalarize.h"

#include "../../ir/passes/ir_pass_scalarize.h"

namespace dxbc_spv::test_api {

Builder test_scalarize(const ScalarizePass::Options& options, BasicType uintType) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::SetCsWorkgroupSize(entryPoint, 32u, 1u, 1u));
  auto labelStart = builder.add(Op::Label());

  /* Thread ID */
  auto gidDef = builder.add(Op::DclInputBuiltIn(Type(ScalarType::eU32, 3u), entryPoint, BuiltIn::eGlobalThreadId));
  builder.add(Op::Semantic(gidDef, 0u, "SV_DispatchThreadId"));

  /* Declare raw buffer */
  auto srv = builder.add(Op::DclSrv(Type(ScalarType::eU32).addArrayDimension(0u), entryPoint, 0u, 0u, 1u, ResourceKind::eBufferRaw));
  auto srvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eSrv, srv, builder.makeConstant(0u)));

  auto uav = builder.add(Op::DclUav(Type(ScalarType::eU32).addArrayDimension(0u), entryPoint, 0u, 0u, 1u, ResourceKind::eBufferRaw, UavFlag::eWriteOnly));
  auto uavDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, uav, builder.makeConstant(0u)));

  /* Figure out corresponding float and signed types */
  std::array<std::pair<ScalarType, std::pair<ScalarType, ScalarType>>, 4u> s_types = {{
    { ScalarType::eU8,  { ScalarType::eI8,  ScalarType::eVoid } },
    { ScalarType::eU16, { ScalarType::eI16, ScalarType::eF16  } },
    { ScalarType::eU32, { ScalarType::eI32, ScalarType::eF32  } },
    { ScalarType::eU64, { ScalarType::eI64, ScalarType::eF64  } },
  }};

  BasicType sintType = { };
  BasicType floatType = { };

  for (const auto& e : s_types) {
    if (e.first == uintType.getBaseType()) {
      sintType = BasicType(e.second.first, uintType.getVectorSize());
      floatType = BasicType(e.second.second, uintType.getVectorSize());
    }
  }

  dxbc_spv_assert(!sintType.isVoidType());

  /* Declare integer undef and constants */
  SsaDef uintVectorUndef = builder.makeUndef(uintType);
  SsaDef sintVectorUndef = builder.makeUndef(sintType);

  SsaDef uintScalarUndef = builder.makeUndef(uintType.getBaseType());
  SsaDef sintScalarUndef = builder.makeUndef(sintType.getBaseType());

  Op uintConstantOp(OpCode::eConstant, uintType);
  Op sintConstantOp(OpCode::eConstant, sintType);

  std::array<SsaDef, 4u> uintScalarConstants = { };
  std::array<SsaDef, 4u> sintScalarConstants = { };

  for (uint32_t i = 0u; i < uintType.getVectorSize(); i++) {
    uintConstantOp.addOperand(Operand(uint64_t(i + 1)));
    sintConstantOp.addOperand(Operand(int64_t(i) - 1));

    uintScalarConstants.at(i) = builder.add(Op(OpCode::eConstant, uintType.getBaseType()).addOperand(uint64_t(i + 16)));
    sintScalarConstants.at(i) = builder.add(Op(OpCode::eConstant, sintType.getBaseType()).addOperand(int64_t(16) - int64_t(i)));
  }

  SsaDef uintVectorConstant = builder.add(uintConstantOp);
  SsaDef sintVectorConstant = builder.add(sintConstantOp);

  /* Declare float undef and constants if applicable */
  std::array<SsaDef, 4u> floatScalarConstants = { };

  Op floatConstantOp(OpCode::eConstant, floatType);

  SsaDef floatVectorUndef = { };
  SsaDef floatScalarUndef = { };

  SsaDef floatVectorConstant = { };

  if (!floatType.isVoidType()) {
    floatVectorUndef = builder.makeUndef(floatType);
    floatScalarUndef = builder.makeUndef(floatType.getBaseType());

    for (uint32_t i = 0u; i < uintType.getVectorSize(); i++) {
      float f0 = float(i - 1.5f);
      float f1 = float(i - 0.3333f);

      Operand o0 = Operand(f0);
      Operand o1 = Operand(f1);

      if (floatType.getBaseType() == ScalarType::eF16) {
        o0 = Operand(float16_t(f0));
        o1 = Operand(float16_t(f1));
      } else if (floatType.getBaseType() == ScalarType::eF64) {
        o0 = Operand(double(f0));
        o1 = Operand(double(f1));
      }

      floatConstantOp.addOperand(o0);
      floatScalarConstants[i] = builder.add(Op(OpCode::eConstant, floatType.getBaseType()).addOperand(o1));
    }

    floatVectorConstant = builder.add(floatConstantOp);
  }

  /* Construct undef and constant vectors */
  SsaDef uintCompositeUndef = uintVectorUndef;
  SsaDef sintCompositeUndef = sintVectorUndef;
  SsaDef floatCompositeUndef = floatVectorUndef;

  SsaDef uintCompositeConstant = uintScalarConstants.at(0u);
  SsaDef sintCompositeConstant = sintScalarConstants.at(0u);
  SsaDef floatCompositeConstant = floatScalarConstants.at(0u);

  if (uintType.isVector()) {
    Op uintUndefOp(OpCode::eCompositeConstruct, uintType);
    Op sintUndefOp(OpCode::eCompositeConstruct, sintType);
    Op floatUndefOp(OpCode::eCompositeConstruct, floatType);

    Op uintConstantOp(OpCode::eCompositeConstruct, uintType);
    Op sintConstantOp(OpCode::eCompositeConstruct, sintType);
    Op floatConstantOp(OpCode::eCompositeConstruct, floatType);

    for (uint32_t i = 0u; i < uintType.getVectorSize(); i++) {
      uintUndefOp.addOperand(uintScalarUndef);
      sintUndefOp.addOperand(sintScalarUndef);
      floatUndefOp.addOperand(floatScalarUndef);

      uintConstantOp.addOperand(uintScalarConstants.at(i));
      sintConstantOp.addOperand(sintScalarConstants.at(i));
      floatConstantOp.addOperand(floatScalarConstants.at(i));
    }

    uintCompositeUndef = builder.add(uintUndefOp);
    sintCompositeUndef = builder.add(sintUndefOp);
    uintCompositeConstant = builder.add(uintConstantOp);
    sintCompositeConstant = builder.add(sintConstantOp);

    if (!floatType.isVoidType()) {
      floatCompositeUndef = builder.add(floatUndefOp);
      floatCompositeConstant = builder.add(floatConstantOp);
    }
  }

  /* Load thread ID */
  auto threadId = builder.add(Op::InputLoad(ScalarType::eU32, gidDef, builder.makeConstant(0u)));

  /* Load vector data from raw buffer */
  auto loadedValue = builder.add(Op::BufferLoad(BasicType(ScalarType::eU32, uintType.getVectorSize()),
    srvDescriptor, threadId, 4u));

  /* Cast or convert to respective types */
  SsaDef uintValue = loadedValue;
  SsaDef sintValue = loadedValue;
  SsaDef floatValue = { };

  if (uintType.getBaseType() != ScalarType::eU32)
    uintValue = builder.add(Op::ConvertItoI(uintType, loadedValue));

  sintValue = builder.add(Op::Cast(sintType, uintValue));

  if (!floatType.isVoidType())
    floatValue = builder.add(Op::ConvertItoF(floatType, sintValue));

  /* Add composite constant */
  uintValue = builder.add(Op::IAdd(uintType, uintValue, uintCompositeConstant));
  uintValue = builder.add(Op::UMin(uintType, uintValue, uintValue));
  uintValue = builder.add(Op::IAnd(uintType, uintValue, uintCompositeConstant));
  uintValue = builder.add(Op::IOr(uintType, uintValue, uintCompositeConstant));
  uintValue = builder.add(Op::IXor(uintType, uintValue, uintCompositeConstant));
  uintValue = builder.add(Op::UShr(uintType, uintValue, uintCompositeConstant));
  uintValue = builder.add(Op::UDiv(uintType, uintValue, uintCompositeConstant));
  uintValue = builder.add(Op::UMod(uintType, uintValue, uintCompositeConstant));

  sintValue = builder.add(Op::ISub(sintType, sintValue, sintCompositeConstant));
  sintValue = builder.add(Op::IMul(sintType, sintValue, sintCompositeConstant));
  sintValue = builder.add(Op::IShl(sintType, sintValue, sintCompositeConstant));
  sintValue = builder.add(Op::SShr(sintType, sintValue, sintCompositeConstant));
  sintValue = builder.add(Op::INeg(sintType, sintValue));
  sintValue = builder.add(Op::SMin(sintType, sintValue, sintCompositeConstant));
  sintValue = builder.add(Op::IAbs(sintType, sintValue));
  sintValue = builder.add(Op::SMax(sintType, sintValue, sintCompositeConstant));

  if (!floatType.isVoidType())
    floatValue = builder.add(Op::FAdd(floatType, floatValue, floatCompositeConstant));

  /* Compare to vector constants and spam some boolean ops
   * that must be scalarized regardless of the type. */
  auto boolType = BasicType(ScalarType::eBool, uintType.getVectorSize());

  auto ueq = builder.add(Op::IEq(boolType, uintValue, uintVectorConstant));
  auto uresult = builder.add(Op::BNot(boolType, ueq));
  auto une = builder.add(Op::INe(boolType, uintValue, uintVectorConstant));
  uresult = builder.add(Op::BOr(boolType, uresult, une));
  auto ult = builder.add(Op::ULt(boolType, uintValue, uintVectorConstant));
  uresult = builder.add(Op::BAnd(boolType, uresult, ult));
  auto ule = builder.add(Op::ULe(boolType, uintValue, uintVectorConstant));
  uresult = builder.add(Op::BNe(boolType, uresult, ule));
  auto ugt = builder.add(Op::UGt(boolType, uintValue, uintVectorConstant));
  uresult = builder.add(Op::BEq(boolType, uresult, ugt));
  auto uge = builder.add(Op::UGe(boolType, uintValue, uintVectorConstant));
  uresult = builder.add(Op::BAnd(boolType, uresult, uge));

  auto seq = builder.add(Op::IEq(boolType, sintValue, sintVectorConstant));
  auto sresult = builder.add(Op::BNot(boolType, seq));
  auto sne = builder.add(Op::INe(boolType, sintValue, sintVectorConstant));
  sresult = builder.add(Op::BOr(boolType, sresult, sne));
  auto slt = builder.add(Op::SLt(boolType, sintValue, sintVectorConstant));
  sresult = builder.add(Op::BAnd(boolType, sresult, slt));
  auto sle = builder.add(Op::SLe(boolType, sintValue, sintVectorConstant));
  sresult = builder.add(Op::BNe(boolType, sresult, sle));
  auto sgt = builder.add(Op::SGt(boolType, sintValue, sintVectorConstant));
  sresult = builder.add(Op::BEq(boolType, sresult, sgt));
  auto sge = builder.add(Op::SGe(boolType, sintValue, sintVectorConstant));
  sresult = builder.add(Op::BAnd(boolType, sresult, sge));

  auto combined = builder.add(Op::BNe(boolType, uresult, sresult));

  /* Merge everything into one bool and do a scalar load so we
   * can test scalarizing phis and mess with undefs */
  auto scalarBool = combined;

  if (boolType.isVector()) {
    scalarBool = builder.add(Op::CompositeExtract(ScalarType::eBool, combined, builder.makeConstant(0u)));

    for (uint32_t i = 1u; i < boolType.getVectorSize(); i++) {
      scalarBool = builder.add(Op::BOr(ScalarType::eBool, scalarBool,
        builder.add(Op::CompositeExtract(ScalarType::eBool, combined, builder.makeConstant(i)))));
    }
  }

  auto labelTrue = builder.add(Op::Label());
  auto labelMerge = builder.add(Op::Label());
  builder.addBefore(labelTrue, Op::BranchConditional(scalarBool, labelTrue, labelMerge));
  builder.addBefore(labelMerge, Op::Branch(labelMerge));
  builder.rewriteOp(labelStart, Op::LabelSelection(labelMerge));

  uintValue = builder.add(Op::Phi(uintType)
    .addPhi(labelStart, uintVectorUndef)
    .addPhi(labelTrue, uintValue));
  sintValue = builder.add(Op::Phi(sintType)
    .addPhi(labelStart, sintVectorConstant)
    .addPhi(labelTrue, sintValue));

  uintValue = builder.add(Op::IAdd(uintType, uintValue,
    builder.add(Op::Cast(uintType, sintValue))));

  if (uintType.getBaseType() != ScalarType::eU32)
    uintValue = builder.add(Op::ConvertItoI(BasicType(ScalarType::eU32, uintType.getVectorSize()), uintValue));

  builder.add(Op::BufferStore(uavDescriptor, threadId, uintValue, 4u));

  if (!floatType.isVoidType()) {
    floatValue = builder.add(Op::FAdd(floatType, floatValue, floatCompositeConstant));
    floatValue = builder.add(Op::FAbs(floatType, floatValue));
    floatValue = builder.add(Op::FNeg(floatType, floatValue));
    floatValue = builder.add(Op::FRcp(floatType, floatValue));
    floatValue = builder.add(Op::FDiv(floatType, floatCompositeConstant, floatValue));
    floatValue = builder.add(Op::FMad(floatType, floatValue, floatCompositeConstant, floatValue));

    /* Test vectorized select */
    auto cond = builder.add(Op::FIsNan(boolType, floatValue));
    floatValue = builder.add(Op::Select(floatType, cond, floatValue, floatVectorUndef));

    if (floatType.getBaseType() != ScalarType::eF32)
      floatValue = builder.add(Op::ConvertFtoF(BasicType(ScalarType::eF32, uintType.getVectorSize()), floatValue));

    floatValue = builder.add(Op::Cast(BasicType(ScalarType::eU32, uintType.getVectorSize()), floatValue));
    builder.add(Op::BufferStore(uavDescriptor, threadId, uintValue, 4u));
  }

  builder.add(Op::Return());

  /* Run scalarization pass */
  ScalarizePass::runPass(builder, options);

  return builder;
}


Builder test_pass_scalarize_32_vec1() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = true;

  return test_scalarize(opt, BasicType(ScalarType::eU32, 1u));
}


Builder test_pass_scalarize_32_vec2() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = true;

  return test_scalarize(opt, BasicType(ScalarType::eU32, 2u));
}


Builder test_pass_scalarize_32_vec3() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = true;

  return test_scalarize(opt, BasicType(ScalarType::eU32, 3u));
}


Builder test_pass_scalarize_32_vec4() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = true;

  return test_scalarize(opt, BasicType(ScalarType::eU32, 4u));
}



Builder test_pass_scalarize_64_vec1() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = true;

  return test_scalarize(opt, BasicType(ScalarType::eU64, 1u));
}


Builder test_pass_scalarize_64_vec2() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = true;

  return test_scalarize(opt, BasicType(ScalarType::eU64, 2u));
}



Builder test_pass_scalarize_16_vec1() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = false;

  return test_scalarize(opt, BasicType(ScalarType::eU16, 1u));
}


Builder test_pass_scalarize_16_vec2() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = false;

  return test_scalarize(opt, BasicType(ScalarType::eU16, 2u));
}


Builder test_pass_scalarize_16_vec3() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = false;

  return test_scalarize(opt, BasicType(ScalarType::eU16, 3u));
}


Builder test_pass_scalarize_16_vec4() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = false;

  return test_scalarize(opt, BasicType(ScalarType::eU16, 4u));
}



Builder test_pass_scalarize_16_vec1_as_vec2() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = true;

  return test_scalarize(opt, BasicType(ScalarType::eU16, 1u));
}


Builder test_pass_scalarize_16_vec2_as_vec2() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = true;

  return test_scalarize(opt, BasicType(ScalarType::eU16, 2u));
}


Builder test_pass_scalarize_16_vec3_as_vec2() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = true;

  return test_scalarize(opt, BasicType(ScalarType::eU16, 3u));
}


Builder test_pass_scalarize_16_vec4_as_vec2() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = true;

  return test_scalarize(opt, BasicType(ScalarType::eU16, 4u));
}



Builder test_pass_scalarize_8_vec1() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = false;

  return test_scalarize(opt, BasicType(ScalarType::eU8, 1u));
}


Builder test_pass_scalarize_8_vec2() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = false;

  return test_scalarize(opt, BasicType(ScalarType::eU8, 2u));
}


Builder test_pass_scalarize_8_vec3() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = false;

  return test_scalarize(opt, BasicType(ScalarType::eU8, 3u));
}


Builder test_pass_scalarize_8_vec4() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = false;

  return test_scalarize(opt, BasicType(ScalarType::eU8, 4u));
}



Builder test_pass_scalarize_8_vec1_as_vec4() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = true;

  return test_scalarize(opt, BasicType(ScalarType::eU8, 1u));
}


Builder test_pass_scalarize_8_vec2_as_vec4() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = true;

  return test_scalarize(opt, BasicType(ScalarType::eU8, 2u));
}


Builder test_pass_scalarize_8_vec3_as_vec4() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = true;

  return test_scalarize(opt, BasicType(ScalarType::eU8, 3u));
}


Builder test_pass_scalarize_8_vec4_as_vec4() {
  ScalarizePass::Options opt = { };
  opt.subDwordVectors = true;

  return test_scalarize(opt, BasicType(ScalarType::eU8, 4u));
}

}
