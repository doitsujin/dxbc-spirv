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

Builder test_arithmetic_fp64() {
  return make_test_float_arithmetic(ScalarType::eF64, false);
}

Builder test_arithmetic_fp16_scalar() {
  return make_test_float_arithmetic(ScalarType::eF16, false);
}

Builder test_arithmetic_fp16_vector() {
  return make_test_float_arithmetic(BasicType(ScalarType::eF16, 2u), false);
}

}
