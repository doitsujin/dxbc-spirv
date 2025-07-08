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

}
