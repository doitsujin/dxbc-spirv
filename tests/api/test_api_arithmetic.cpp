#include "test_api_arithmetic.h"

namespace dxbc_spv::test_api {

struct ArithmeticTest {
  ir::OpCode opCode = ir::OpCode();
  uint32_t operandCount = 0u;
  ir::RoundMode roundMode = { };
};

Builder make_test_float_arithmetic(BasicType type, bool precise) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::SetCsWorkgroupSize(entryPoint, 1u, 1u, 1u));
  builder.add(Op::Label());

  auto floatFlags = precise ? OpFlags(OpFlag::ePrecise) : OpFlags();

  builder.add(Op::SetFpMode(entryPoint, ScalarType::eF32, floatFlags,
    RoundMode::eNearestEven, DenormMode::eFlush));

  if (type != ScalarType::eF32) {
    builder.add(Op::SetFpMode(entryPoint, type.getBaseType(), floatFlags,
      RoundMode::eNearestEven, DenormMode::ePreserve));
  }

  auto bufType = Type(ScalarType::eF32).addArrayDimension(type.getVectorSize()).addArrayDimension(0u);
  auto srv = builder.add(Op::DclSrv(bufType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured));
  auto uav = builder.add(Op::DclUav(bufType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured, UavFlag::eWriteOnly));

  auto accessType = BasicType(ScalarType::eF32, type.getVectorSize());

  auto srvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eSrv, srv, builder.makeConstant(0u)));
  auto uavDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, uav, builder.makeConstant(0u)));

  static const std::vector<ArithmeticTest> tests = {{
    { ir::OpCode::eFRound, 1u, ir::RoundMode::eZero },
    { ir::OpCode::eFAbs, 1u },
    { ir::OpCode::eFNeg, 1u },
    { ir::OpCode::eFAdd, 2u },
    { ir::OpCode::eFRound, 1u, ir::RoundMode::eNearestEven },
    { ir::OpCode::eFSub, 2u },
    { ir::OpCode::eFRound, 1u, ir::RoundMode::eNegativeInf },
    { ir::OpCode::eFMul, 2u },
    { ir::OpCode::eFRound, 1u, ir::RoundMode::ePositiveInf },
    { ir::OpCode::eFMad, 3u },
    { ir::OpCode::eFDiv, 2u },
    { ir::OpCode::eFRcp, 1u },
    { ir::OpCode::eFFract, 1u },
    { ir::OpCode::eFMin, 2u },
    { ir::OpCode::eFMax, 2u },
    { ir::OpCode::eFClamp, 3u },
  }};

  uint32_t srvOffset = 0u;

  /* Result of last instruction */
  auto resultDef = SsaDef();

  for (const auto& e : tests) {
    if (type.getBaseType() == ScalarType::eF64) {
      if (e.opCode == ir::OpCode::eFRound ||
          e.opCode == ir::OpCode::eFFract)
        continue;
    }

    Op op(e.opCode, type);

    for (uint32_t i = 0; i < e.operandCount; i++) {
      /* Use last result as first operand */
      SsaDef operand = i ? SsaDef() : resultDef;

      if (!operand) {
        /* Load input as F32 and convert to destination type */
        util::small_vector<SsaDef, 4u> components;

        for (uint32_t j = 0u; j < type.getVectorSize(); j++) {
          auto& component = components.emplace_back();

          auto indexDef = builder.makeConstant(srvOffset, j);
          component = builder.add(Op::BufferLoad(accessType.getBaseType(), srvDescriptor, indexDef, accessType.byteSize()));

          if (type.getBaseType() != ScalarType::eF32)
            component = builder.add(Op::ConvertFtoF(type.getBaseType(), component));
        }

        srvOffset++;

        /* Silence a GCC warning */
        operand = components.empty() ? SsaDef() : components.front();

        if (type.isVector()) {
          Op buildVectorOp(OpCode::eCompositeConstruct, type);

          for (auto def : components)
            buildVectorOp.addOperand(Operand(def));

          operand = builder.add(std::move(buildVectorOp));
        }
      }

      op.addOperand(Operand(operand));
    }

    if (e.opCode == ir::OpCode::eFRound)
      op.addOperand(Operand(e.roundMode));

    resultDef = builder.add(std::move(op));
  }

  /* Convert final result back to F32 and store */
  for (uint32_t i = 0u; i < type.getVectorSize(); i++) {
    auto component = resultDef;

    if (type.isVector())
      component = builder.add(Op::CompositeExtract(type.getBaseType(), resultDef, builder.makeConstant(i)));

    if (type.getBaseType() != ScalarType::eF32)
      component = builder.add(Op::ConvertFtoF(ScalarType::eF32, component));

    auto indexDef = builder.makeConstant(0u, i);
    builder.add(Op::BufferStore(uavDescriptor, indexDef, component, accessType.byteSize()));
  }

  builder.add(Op::Return());
  return builder;
}


Builder make_test_float_compare(ScalarType type) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::SetCsWorkgroupSize(entryPoint, 1u, 1u, 1u));
  builder.add(Op::Label());

  auto srvType = Type(ScalarType::eF32).addArrayDimension(1u).addArrayDimension(0u);
  auto uavType = Type(ScalarType::eU32).addArrayDimension(1u).addArrayDimension(0u);

  auto srv = builder.add(Op::DclSrv(srvType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured));
  auto uav = builder.add(Op::DclUav(uavType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured, UavFlag::eWriteOnly));

  auto srvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eSrv, srv, builder.makeConstant(0u)));
  auto uavDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, uav, builder.makeConstant(0u)));

  static const std::vector<std::pair<ir::OpCode, uint32_t>> tests = {
    { ir::OpCode::eFEq, 2u },
    { ir::OpCode::eFNe, 2u },
    { ir::OpCode::eFLt, 2u },
    { ir::OpCode::eFLe, 2u },
    { ir::OpCode::eFGt, 2u },
    { ir::OpCode::eFGe, 2u },
    { ir::OpCode::eFIsNan, 1u },
  };

  /* Reuse the same operands for all ops */
  std::array<SsaDef, 2u> operands;

  for (uint32_t i = 0u; i < operands.size(); i++) {
    auto indexDef = builder.makeConstant(i, 0u);
    operands[i] = builder.add(Op::BufferLoad(ScalarType::eF32, srvDescriptor, indexDef, 4u));

    if (type != ScalarType::eF32)
      operands[i] = builder.add(Op::ConvertFtoF(type, operands[i]));
  }

  uint32_t uavIndex = 0u;

  for (const auto& e : tests) {
    Op op(e.first, ScalarType::eBool);

    for (uint32_t i = 0u; i < e.second; i++)
      op.addOperand(Operand(operands.at(i)));

    auto resultDef = builder.add(std::move(op));
    resultDef = builder.add(Op::Select(ScalarType::eU32, resultDef,
      builder.makeConstant(1u), builder.makeConstant(0u)));

    auto indexDef = builder.makeConstant(uavIndex++, 0u);
    builder.add(Op::BufferStore(uavDescriptor, indexDef, resultDef, 4u));
  }

  builder.add(Op::Return());
  return builder;
}


Builder test_arithmetic_fp32() {
  return make_test_float_arithmetic(ScalarType::eF32, false);
}

Builder test_arithmetic_fp32_precise() {
  return make_test_float_arithmetic(ScalarType::eF32, true);
}

Builder test_arithmetic_fp32_special() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::SetCsWorkgroupSize(entryPoint, 1u, 1u, 1u));
  builder.add(Op::Label());

  auto bufType = Type(ScalarType::eF32).addArrayDimension(1u).addArrayDimension(0u);

  auto srv = builder.add(Op::DclSrv(bufType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured));
  auto uav = builder.add(Op::DclUav(bufType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured, UavFlag::eWriteOnly));

  static const std::vector<std::pair<ir::OpCode, ir::OpFlags>> tests = {
    { ir::OpCode::eFLog2, ir::OpFlag::eNoSz   },
    { ir::OpCode::eFSin,  ir::OpFlag::eNoInf  },
    { ir::OpCode::eFSqrt, ir::OpFlags()       },
    { ir::OpCode::eFExp2, ir::OpFlag::eNoSz   },
    { ir::OpCode::eFRsq,  ir::OpFlag::eNoSz   },
    { ir::OpCode::eFCos,  ir::OpFlag::eNoInf  },
  };

  auto srvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eSrv, srv, builder.makeConstant(0u)));
  auto uavDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, uav, builder.makeConstant(0u)));

  auto indexDef = builder.makeConstant(0u, 0u);
  auto component = builder.add(Op::BufferLoad(ScalarType::eF32, srvDescriptor, indexDef, 4u));

  for (auto e : tests) {
    component = builder.add(Op(e.first, ScalarType::eF32)
      .setFlags(e.second).addOperand(Operand(component)));
  }

  component = builder.add(Op::FPow(ScalarType::eF32, component, builder.makeConstant(2.4f)));

  builder.add(Op::BufferStore(uavDescriptor, indexDef, component, 4u));
  builder.add(Op::Return());
  return builder;
}

Builder test_arithmetic_fp32_compare() {
  return make_test_float_compare(ScalarType::eF32);
}

Builder test_arithmetic_fp64() {
  return make_test_float_arithmetic(ScalarType::eF64, false);
}

Builder test_arithmetic_fp64_compare() {
  return make_test_float_compare(ScalarType::eF64);
}

Builder test_arithmetic_fp64_packing() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::SetCsWorkgroupSize(entryPoint, 1u, 1u, 1u));
  builder.add(Op::Label());

  auto bufType = Type(ScalarType::eU32).addArrayDimension(2u).addArrayDimension(0u);

  auto srv = builder.add(Op::DclSrv(bufType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured));
  auto uav = builder.add(Op::DclUav(bufType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured, UavFlag::eWriteOnly));

  auto srvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eSrv, srv, builder.makeConstant(0u)));
  auto uavDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, uav, builder.makeConstant(0u)));

  auto vec2Type = BasicType(ScalarType::eU32, 2u);

  auto load0Def = builder.add(Op::BufferLoad(vec2Type, srvDescriptor, builder.makeConstant(0u, 0u), 4u));
  auto load1Def = builder.add(Op::BufferLoad(vec2Type, srvDescriptor, builder.makeConstant(1u, 0u), 4u));

  auto vec0Def = builder.add(Op::Cast(ScalarType::eF64, load0Def));
  auto vec1Def = builder.add(Op::Cast(ScalarType::eF64, load1Def));

  auto resultDef = builder.add(Op::FAdd(ScalarType::eF64, vec0Def, vec1Def));
  resultDef = builder.add(Op::Cast(vec2Type, resultDef));

  builder.add(Op::BufferStore(uavDescriptor, builder.makeConstant(0u, 0u), resultDef, 4u));
  builder.add(Op::Return());
  return builder;
}

Builder test_arithmetic_fp16_scalar() {
  return make_test_float_arithmetic(ScalarType::eF16, false);
}

Builder test_arithmetic_fp16_vector() {
  return make_test_float_arithmetic(BasicType(ScalarType::eF16, 2u), false);
}

Builder test_arithmetic_fp16_compare() {
  return make_test_float_compare(ScalarType::eF16);
}

Builder test_arithmetic_fp16_packing() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::SetCsWorkgroupSize(entryPoint, 1u, 1u, 1u));
  builder.add(Op::Label());

  auto bufType = Type(ScalarType::eU32).addArrayDimension(1u).addArrayDimension(0u);

  auto srv = builder.add(Op::DclSrv(bufType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured));
  auto uav = builder.add(Op::DclUav(bufType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured, UavFlag::eWriteOnly));

  auto srvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eSrv, srv, builder.makeConstant(0u)));
  auto uavDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, uav, builder.makeConstant(0u)));

  auto load0Def = builder.add(Op::BufferLoad(ScalarType::eU32, srvDescriptor, builder.makeConstant(0u, 0u), 4u));
  auto load1Def = builder.add(Op::BufferLoad(ScalarType::eU32, srvDescriptor, builder.makeConstant(1u, 0u), 4u));

  auto vec2Type = BasicType(ScalarType::eF16, 2u);

  auto vec0Def = builder.add(Op::Cast(vec2Type, load0Def));
  auto vec1Def = builder.add(Op::Cast(vec2Type, load1Def));

  auto resultDef = builder.add(Op::FAdd(vec2Type, vec0Def, vec1Def));
  resultDef = builder.add(Op::Cast(ScalarType::eU32, resultDef));

  builder.add(Op::BufferStore(uavDescriptor, builder.makeConstant(0u, 0u), resultDef, 4u));
  builder.add(Op::Return());
  return builder;
}


Builder test_arithmetic_fp16_packing_legacy() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::SetCsWorkgroupSize(entryPoint, 1u, 1u, 1u));
  builder.add(Op::Label());

  auto bufType = Type(ScalarType::eU32).addArrayDimension(1u).addArrayDimension(0u);

  auto srv = builder.add(Op::DclSrv(bufType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured));
  auto uav = builder.add(Op::DclUav(bufType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured, UavFlag::eWriteOnly));

  auto srvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eSrv, srv, builder.makeConstant(0u)));
  auto uavDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, uav, builder.makeConstant(0u)));

  auto load0Def = builder.add(Op::BufferLoad(ScalarType::eU32, srvDescriptor, builder.makeConstant(0u, 0u), 4u));
  auto load1Def = builder.add(Op::BufferLoad(ScalarType::eU32, srvDescriptor, builder.makeConstant(1u, 0u), 4u));

  auto vec2Type = BasicType(ScalarType::eF32, 2u);

  auto vec0Def = builder.add(Op::ConvertPackedF16toF32(BasicType(ScalarType::eF32, 2u), load0Def));
  auto vec1Def = builder.add(Op::ConvertPackedF16toF32(BasicType(ScalarType::eF32, 2u), load1Def));

  std::array<SsaDef, 2u> scalars = { };

  for (uint32_t i = 0u; i < 2u; i++) {
    scalars.at(i) = builder.add(Op::FAdd(ScalarType::eF32,
      builder.add(Op::CompositeExtract(ScalarType::eF32, vec0Def, builder.makeConstant(i))),
      builder.add(Op::CompositeExtract(ScalarType::eF32, vec1Def, builder.makeConstant(i)))));
  }

  auto resultDef = builder.add(Op::CompositeConstruct(vec2Type, scalars.at(0), scalars.at(1u)));
  resultDef = builder.add(Op::ConvertF32toPackedF16(ScalarType::eU32, resultDef));

  builder.add(Op::BufferStore(uavDescriptor, builder.makeConstant(0u, 0u), resultDef, 4u));
  builder.add(Op::Return());
  return builder;
}


struct ArithmeticIntTest {
  ir::OpCode opCode = ir::OpCode();
  uint32_t operandCount = 0u;
  ScalarType requiredType = ScalarType::eUnknown;
};

Builder make_test_int_arithmetic(BasicType type) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::SetCsWorkgroupSize(entryPoint, 1u, 1u, 1u));
  builder.add(Op::Label());

  auto bufType = Type(ScalarType::eU32).addArrayDimension(1u).addArrayDimension(0u);

  auto srv = builder.add(Op::DclSrv(bufType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured));
  auto uav = builder.add(Op::DclUav(bufType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured, UavFlag::eWriteOnly));

  auto srvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eSrv, srv, builder.makeConstant(0u)));
  auto uavDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, uav, builder.makeConstant(0u)));

  static const std::vector<ArithmeticIntTest> tests = {
    { ir::OpCode::eIAnd, 2 },
    { ir::OpCode::eIOr, 2 },
    { ir::OpCode::eIXor, 2 },
    { ir::OpCode::eINot, 1 },
    { ir::OpCode::eIAdd, 2 },
    { ir::OpCode::eISub, 2 },
    { ir::OpCode::eINeg, 1 },
    { ir::OpCode::eIAbs, 1 },
    { ir::OpCode::eIMul, 2 },
    { ir::OpCode::eIShl, 2 },
    { ir::OpCode::eSShr, 2 },
    { ir::OpCode::eUShr, 2 },
    /* Unsigned-only ops */
    { ir::OpCode::eUDiv, 2, ScalarType::eU32 },
    { ir::OpCode::eUDiv, 2, ScalarType::eU16 },
    { ir::OpCode::eUMod, 2, ScalarType::eU32 },
    { ir::OpCode::eUMod, 2, ScalarType::eU16 },
    { ir::OpCode::eUMin, 2, ScalarType::eU32 },
    { ir::OpCode::eUMin, 2, ScalarType::eU16 },
    { ir::OpCode::eUMax, 2, ScalarType::eU32 },
    { ir::OpCode::eUMax, 2, ScalarType::eU16 },
    { ir::OpCode::eUClamp, 3, ScalarType::eU32 },
    { ir::OpCode::eUClamp, 3, ScalarType::eU16 },
    /* Signed-only ops */
    { ir::OpCode::eSMin, 2, ScalarType::eI32 },
    { ir::OpCode::eSMin, 2, ScalarType::eI16 },
    { ir::OpCode::eSMax, 2, ScalarType::eI32 },
    { ir::OpCode::eSMax, 2, ScalarType::eI16 },
    { ir::OpCode::eSClamp, 3, ScalarType::eI32 },
    { ir::OpCode::eSClamp, 3, ScalarType::eI16 },
    /* Ops only supported on 32-bit types */
    { ir::OpCode::eIBitInsert, 4, ScalarType::eU32 },
    { ir::OpCode::eIBitInsert, 4, ScalarType::eI32 },
    { ir::OpCode::eSBitExtract, 3, ScalarType::eI32 },
    { ir::OpCode::eUBitExtract, 3, ScalarType::eU32 },
    { ir::OpCode::eIBitCount, 1, ScalarType::eI32 },
    { ir::OpCode::eIBitCount, 1, ScalarType::eU32 },
    { ir::OpCode::eIBitReverse, 1, ScalarType::eI32 },
    { ir::OpCode::eIBitReverse, 1, ScalarType::eU32 },
    { ir::OpCode::eIFindLsb, 1, ScalarType::eI32 },
    { ir::OpCode::eIFindLsb, 1, ScalarType::eU32 },
    { ir::OpCode::eSFindMsb, 1, ScalarType::eI32 },
    { ir::OpCode::eUFindMsb, 1, ScalarType::eU32 },
  };

  uint32_t srvOffset = 0u;

  /* Result of last instruction */
  auto resultDef = SsaDef();

  for (const auto& e : tests) {
    if (e.requiredType != ScalarType::eUnknown &&
        e.requiredType != type.getBaseType())
      continue;

    Op op(e.opCode, type);

    uint32_t n = 0u;

    if (resultDef) {
      op.addOperand(Operand(resultDef));
      n++;
    }

    while (n < e.operandCount) {
      auto operandDef = builder.add(Op::BufferLoad(ScalarType::eU32,
        srvDescriptor, builder.makeConstant(srvOffset++, 0u), 4u));

      if (type != ScalarType::eU32) {
        operandDef = type.byteSize() != sizeof(uint32_t)
          ? builder.add(Op::ConvertItoI(type, operandDef))
          : builder.add(Op::Cast(type, operandDef));
      }

      op.addOperand(Operand(operandDef));
      n++;
    }

    resultDef = builder.add(std::move(op));
  }

  /* Convert final result back to F32 and store */
  if (type != ScalarType::eU32) {
    resultDef = type.byteSize() != sizeof(uint32_t)
      ? builder.add(Op::ConvertItoI(ScalarType::eU32, resultDef))
      : builder.add(Op::Cast(ScalarType::eU32, resultDef));
  }

  builder.add(Op::BufferStore(uavDescriptor,
    builder.makeConstant(0u, 0u), resultDef, 4u));

  builder.add(Op::Return());
  return builder;
}


Builder test_arithmetic_sint32() {
  return make_test_int_arithmetic(ScalarType::eI32);
}

Builder test_arithmetic_uint32() {
  return make_test_int_arithmetic(ScalarType::eU32);
}

Builder test_arithmetic_sint16_scalar() {
  return make_test_int_arithmetic(ScalarType::eI16);
}

Builder test_arithmetic_sint16_vector() {
  return make_test_int_arithmetic(BasicType(ScalarType::eI16, 2u));
}

Builder test_arithmetic_uint16_scalar() {
  return make_test_int_arithmetic(ScalarType::eU16);
}

Builder test_arithmetic_uint16_vector() {
  return make_test_int_arithmetic(BasicType(ScalarType::eU16, 2u));
}


Builder make_test_int_compare(ScalarType type) {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::SetCsWorkgroupSize(entryPoint, 1u, 1u, 1u));
  builder.add(Op::Label());

  bool isSigned = BasicType(type).isSignedIntType();
  auto plainType = isSigned ? ScalarType::eI32 : ScalarType::eU32;

  auto srvType = Type(plainType).addArrayDimension(1u).addArrayDimension(0u);
  auto uavType = Type(ScalarType::eU32).addArrayDimension(1u).addArrayDimension(0u);

  auto srv = builder.add(Op::DclSrv(srvType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured));
  auto uav = builder.add(Op::DclUav(uavType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured, UavFlag::eWriteOnly));

  auto srvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eSrv, srv, builder.makeConstant(0u)));
  auto uavDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, uav, builder.makeConstant(0u)));

  static const std::vector<ir::OpCode> tests = {
    ir::OpCode::eIEq,
    ir::OpCode::eINe,
    ir::OpCode::eSLt,
    ir::OpCode::eSLe,
    ir::OpCode::eSGt,
    ir::OpCode::eSGe,
    ir::OpCode::eULt,
    ir::OpCode::eULe,
    ir::OpCode::eUGt,
    ir::OpCode::eUGe,
  };

  /* Reuse the same operands for all ops */
  std::array<SsaDef, 2u> operands;

  for (uint32_t i = 0u; i < operands.size(); i++) {
    auto indexDef = builder.makeConstant(i, 0u);
    operands[i] = builder.add(Op::BufferLoad(plainType, srvDescriptor, indexDef, 4u));

    if (type != plainType)
      operands[i] = builder.add(Op::ConvertItoI(type, operands[i]));
  }

  uint32_t uavIndex = 0u;

  for (const auto& e : tests) {
    auto op = Op(e, ScalarType::eBool)
      .addOperand(Operand(operands.at(0u)))
      .addOperand(Operand(operands.at(1u)));

    auto resultDef = builder.add(std::move(op));
    resultDef = builder.add(Op::Select(ScalarType::eU32, resultDef,
      builder.makeConstant(1u), builder.makeConstant(0u)));

    auto indexDef = builder.makeConstant(uavIndex++, 0u);
    builder.add(Op::BufferStore(uavDescriptor, indexDef, resultDef, 4u));
  }

  builder.add(Op::Return());
  return builder;
}


Builder test_arithmetic_sint32_compare() {
  return make_test_int_compare(ScalarType::eI32);
}

Builder test_arithmetic_uint32_compare() {
  return make_test_int_compare(ScalarType::eU32);
}

Builder test_arithmetic_sint16_compare() {
  return make_test_int_compare(ScalarType::eI16);
}

Builder test_arithmetic_uint16_compare() {
  return make_test_int_compare(ScalarType::eU16);
}


Builder test_arithmetic_int_extended() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::SetCsWorkgroupSize(entryPoint, 1u, 1u, 1u));
  builder.add(Op::Label());

  auto uvec2Type = BasicType(ScalarType::eU32, 2u);

  auto srvType = Type(ScalarType::eU32).addArrayDimension(2u).addArrayDimension(0u);
  auto uavType = Type(ScalarType::eU32).addArrayDimension(2u).addArrayDimension(0u);

  auto srv = builder.add(Op::DclSrv(srvType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured));
  auto uav = builder.add(Op::DclUav(uavType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured, UavFlag::eWriteOnly));

  auto srvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eSrv, srv, builder.makeConstant(0u)));
  auto uavDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, uav, builder.makeConstant(0u)));

  static const std::vector<std::pair<ir::OpCode, ScalarType>> tests = {
    { ir::OpCode::eIAddCarry, ScalarType::eU32 },
    { ir::OpCode::eISubBorrow, ScalarType::eU32 },
    { ir::OpCode::eSMulExtended, ScalarType::eI32 },
    { ir::OpCode::eUMulExtended, ScalarType::eU32 },
  };

  uint32_t srvIndex = 0u;
  uint32_t uavIndex = 0u;

  for (const auto& test : tests) {
    auto loadDef = builder.add(Op::BufferLoad(uvec2Type, srvDescriptor,
      builder.makeConstant(srvIndex++, 0u), 8u));

    auto a = builder.add(Op::CompositeExtract(ScalarType::eU32, loadDef, builder.makeConstant(0u)));
    auto b = builder.add(Op::CompositeExtract(ScalarType::eU32, loadDef, builder.makeConstant(1u)));

    if (test.second != ScalarType::eU32) {
      a = builder.add(Op::Cast(test.second, a));
      b = builder.add(Op::Cast(test.second, b));
    }

    auto resultDef = builder.add(Op(test.first, BasicType(test.second, 2u))
      .addOperand(Operand(a))
      .addOperand(Operand(b)));

    if (test.second != ScalarType::eU32) {
      a = builder.add(Op::CompositeExtract(test.second, resultDef, builder.makeConstant(0u)));
      b = builder.add(Op::CompositeExtract(test.second, resultDef, builder.makeConstant(1u)));

      a = builder.add(Op::Cast(ScalarType::eU32, a));
      b = builder.add(Op::Cast(ScalarType::eU32, b));

      resultDef = builder.add(Op::CompositeConstruct(uvec2Type, a, b));
    }

    builder.add(Op::BufferStore(uavDescriptor, builder.makeConstant(uavIndex++, 0u), resultDef, 8u));
  }

  builder.add(Op::Return());
  return builder;
}


Builder test_arithmetic_bool() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::SetCsWorkgroupSize(entryPoint, 1u, 1u, 1u));
  builder.add(Op::Label());

  auto srvType = Type(ScalarType::eU32).addArrayDimension(1u).addArrayDimension(0u);
  auto uavType = Type(ScalarType::eU32).addArrayDimension(1u).addArrayDimension(0u);

  auto srv = builder.add(Op::DclSrv(srvType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured));
  auto uav = builder.add(Op::DclUav(uavType, entryPoint, 0u, 0u, 1u, ResourceKind::eBufferStructured, UavFlag::eWriteOnly));

  auto srvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eSrv, srv, builder.makeConstant(0u)));
  auto uavDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, uav, builder.makeConstant(0u)));

  static const std::vector<std::pair<ir::OpCode, uint32_t>> tests = {
    { ir::OpCode::eBAnd, 2 },
    { ir::OpCode::eBOr, 2 },
    { ir::OpCode::eBEq, 2 },
    { ir::OpCode::eBNe, 2 },
    { ir::OpCode::eBNot, 1 },
  };

  uint32_t srvIndex = 0u;
  uint32_t uavIndex = 0u;

  for (const auto& e : tests) {
    auto op = Op(e.first, ScalarType::eBool);

    for (uint32_t i = 0u; i < e.second; i++) {
      auto indexDef = builder.makeConstant(srvIndex++, 0u);
      op.addOperand(Operand(builder.add(Op::INe(ScalarType::eBool, builder.makeConstant(0u),
        builder.add(Op::BufferLoad(ScalarType::eU32, srvDescriptor, indexDef, 4u))))));
    }

    auto resultDef = builder.add(std::move(op));
    resultDef = builder.add(Op::Select(ScalarType::eU32, resultDef,
      builder.makeConstant(1u), builder.makeConstant(0u)));

    auto indexDef = builder.makeConstant(uavIndex++, 0u);
    builder.add(Op::BufferStore(uavDescriptor, indexDef, resultDef, 4u));
  }

  builder.add(Op::Return());
  return builder;
}

Builder test_convert_f_to_f() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);
  builder.add(Op::Label());

  auto inputDef = builder.add(Op::DclInput(ScalarType::eF32, entryPoint, 0u, 0u, InterpolationModes()));
  builder.add(Op::Semantic(inputDef, 0u, "INPUT"));

  auto outputDef = builder.add(Op::DclOutput(ScalarType::eF32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outputDef, 0u, "SV_TARGET"));

  auto value = builder.add(Op::InputLoad(ScalarType::eF32, inputDef, SsaDef()));
  value = builder.add(Op::ConvertFtoF(ScalarType::eF16, value));
  value = builder.add(Op::ConvertFtoF(ScalarType::eF64, value));
  value = builder.add(Op::ConvertFtoF(ScalarType::eF32, value));
  value = builder.add(Op::ConvertFtoF(ScalarType::eF64, value));
  value = builder.add(Op::ConvertFtoF(ScalarType::eF16, value));
  value = builder.add(Op::ConvertFtoF(ScalarType::eF32, value));
  builder.add(Op::OutputStore(outputDef, SsaDef(), value));

  builder.add(Op::Return());
  return builder;
}

Builder test_convert_f_to_i() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);
  builder.add(Op::Label());

  auto inputDef = builder.add(Op::DclInput(ScalarType::eF32, entryPoint, 0u, 0u, InterpolationModes()));
  builder.add(Op::Semantic(inputDef, 0u, "INPUT"));

  auto outA = builder.add(Op::DclOutput(ScalarType::eU32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outA, 0u, "SV_TARGET"));
  auto outB = builder.add(Op::DclOutput(ScalarType::eI32, entryPoint, 1u, 0u));
  builder.add(Op::Semantic(outB, 1u, "SV_TARGET"));

  auto input = builder.add(Op::InputLoad(ScalarType::eF32, inputDef, SsaDef()));

  auto a = builder.add(Op::ConvertFtoI(ScalarType::eU32, input));
  auto b = builder.add(Op::ConvertFtoI(ScalarType::eI32, input));
  a = builder.add(Op::IAdd(ScalarType::eU32, a, builder.add(Op::ConvertFtoI(ScalarType::eU32, builder.add(Op::ConvertFtoF(ScalarType::eF64, input))))));
  b = builder.add(Op::IAdd(ScalarType::eI32, b, builder.add(Op::ConvertFtoI(ScalarType::eI32, builder.add(Op::ConvertFtoF(ScalarType::eF64, input))))));
  a = builder.add(Op::IAdd(ScalarType::eU32, a, builder.add(Op::ConvertFtoI(ScalarType::eU32, builder.add(Op::ConvertFtoF(ScalarType::eF16, input))))));
  b = builder.add(Op::IAdd(ScalarType::eI32, b, builder.add(Op::ConvertFtoI(ScalarType::eI32, builder.add(Op::ConvertFtoF(ScalarType::eF16, input))))));
  builder.add(Op::OutputStore(outA, SsaDef(), a));
  builder.add(Op::OutputStore(outB, SsaDef(), b));

  builder.add(Op::Return());
  return builder;
}

Builder test_convert_i_to_f() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);
  builder.add(Op::Label());

  auto inUintDef = builder.add(Op::DclInput(ScalarType::eU32, entryPoint, 0u, 0u, InterpolationMode::eFlat));
  builder.add(Op::Semantic(inUintDef, 0u, "UINPUT"));
  auto inSintDef = builder.add(Op::DclInput(ScalarType::eI32, entryPoint, 0u, 1u, InterpolationMode::eFlat));
  builder.add(Op::Semantic(inSintDef, 0u, "SINPUT"));

  auto outA = builder.add(Op::DclOutput(ScalarType::eF32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outA, 0u, "SV_TARGET"));
  auto outB = builder.add(Op::DclOutput(ScalarType::eF32, entryPoint, 1u, 0u));
  builder.add(Op::Semantic(outB, 1u, "SV_TARGET"));

  auto uintValue = builder.add(Op::InputLoad(ScalarType::eU32, inUintDef, SsaDef()));
  auto sintValue = builder.add(Op::InputLoad(ScalarType::eI32, inSintDef, SsaDef()));

  auto a = builder.add(Op::ConvertItoF(ScalarType::eF32, uintValue));
  auto b = builder.add(Op::ConvertItoF(ScalarType::eF32, sintValue));
  a = builder.add(Op::FAdd(ScalarType::eF32, a, builder.add(Op::ConvertFtoF(ScalarType::eF32, builder.add(Op::ConvertItoF(ScalarType::eF16, uintValue))))));
  b = builder.add(Op::FAdd(ScalarType::eF32, b, builder.add(Op::ConvertFtoF(ScalarType::eF32, builder.add(Op::ConvertItoF(ScalarType::eF16, sintValue))))));
  a = builder.add(Op::FAdd(ScalarType::eF32, a, builder.add(Op::ConvertFtoF(ScalarType::eF32, builder.add(Op::ConvertItoF(ScalarType::eF64, uintValue))))));
  b = builder.add(Op::FAdd(ScalarType::eF32, b, builder.add(Op::ConvertFtoF(ScalarType::eF32, builder.add(Op::ConvertItoF(ScalarType::eF64, sintValue))))));
  builder.add(Op::OutputStore(outA, SsaDef(), a));
  builder.add(Op::OutputStore(outB, SsaDef(), b));

  builder.add(Op::Return());
  return builder;
}

Builder test_convert_i_to_i() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);
  builder.add(Op::Label());

  auto inUintDef = builder.add(Op::DclInput(ScalarType::eU32, entryPoint, 0u, 0u, InterpolationMode::eFlat));
  builder.add(Op::Semantic(inUintDef, 0u, "INPUT"));

  auto outUintDef = builder.add(Op::DclOutput(ScalarType::eU32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outUintDef, 0u, "SV_TARGET"));

  auto value = builder.add(Op::InputLoad(ScalarType::eU32, inUintDef, SsaDef()));
  value = builder.add(Op::ConvertItoI(ScalarType::eU16, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eU64, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eU32, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eU64, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eU16, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eU32, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eI32, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eI64, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eI16, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eI64, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eI32, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eI16, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eI32, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eU64, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eI64, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eU32, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eI16, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eU16, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eI32, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eU16, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eI64, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eU16, value));
  value = builder.add(Op::ConvertItoI(ScalarType::eU32, value));
  builder.add(Op::OutputStore(outUintDef, SsaDef(), value));

  builder.add(Op::Return());
  return builder;
}

}
