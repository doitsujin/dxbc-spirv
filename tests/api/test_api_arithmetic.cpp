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

        operand = components.front();

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

    if (precise)
      op.setFlags(OpFlag::ePrecise);

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

  static const std::vector<ir::OpCode> tests = {
    ir::OpCode::eFLog2,
    ir::OpCode::eFSin,
    ir::OpCode::eFSqrt,
    ir::OpCode::eFExp2,
    ir::OpCode::eFRsq,
    ir::OpCode::eFCos,
  };

  auto srvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eSrv, srv, builder.makeConstant(0u)));
  auto uavDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, uav, builder.makeConstant(0u)));

  auto indexDef = builder.makeConstant(0u, 0u);
  auto component = builder.add(Op::BufferLoad(ScalarType::eF32, srvDescriptor, indexDef, 4u));

  for (auto opCode : tests)
    component = builder.add(Op(opCode, ScalarType::eF32).addOperand(Operand(component)));

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

  auto vec0Def = builder.add(Op::ConvertPackedF16toF32(load0Def));
  auto vec1Def = builder.add(Op::ConvertPackedF16toF32(load1Def));

  std::array<SsaDef, 2u> scalars = { };

  for (uint32_t i = 0u; i < 2u; i++) {
    scalars.at(i) = builder.add(Op::FAdd(ScalarType::eF32,
      builder.add(Op::CompositeExtract(ScalarType::eF32, vec0Def, builder.makeConstant(i))),
      builder.add(Op::CompositeExtract(ScalarType::eF32, vec1Def, builder.makeConstant(i)))));
  }

  auto resultDef = builder.add(Op::CompositeConstruct(vec2Type, scalars.at(0), scalars.at(1u)));
  resultDef = builder.add(Op::ConvertF32toPackedF16(resultDef));

  builder.add(Op::BufferStore(uavDescriptor, builder.makeConstant(0u, 0u), resultDef, 4u));
  builder.add(Op::Return());
  return builder;
}

}
