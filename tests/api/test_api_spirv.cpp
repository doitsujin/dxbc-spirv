#include "test_api_misc.h"

namespace dxbc_spv::test_api {

Builder test_spirv_spec_constant() {
  Builder builder;
  auto entryPoint = setupTestFunction(builder, ShaderStage::ePixel);
  builder.add(Op::Label());

  auto selector = builder.add(Op::DclSpecConstant(ScalarType::eBool, entryPoint, 0u, false));
  auto a = builder.add(Op::DclSpecConstant(ScalarType::eU32, entryPoint, 1u, 5u));
  auto b = builder.add(Op::DclSpecConstant(ScalarType::eU32, entryPoint, 2u, 6u));

  builder.add(Op::DebugName(selector, "sel"));
  builder.add(Op::DebugName(a, "a"));
  builder.add(Op::DebugName(b, "b"));

  auto outputDef = builder.add(Op::DclOutput(ScalarType::eU32, entryPoint, 0u, 0u));
  builder.add(Op::Semantic(outputDef, 0u, "SV_TARGET"));

  builder.add(Op::OutputStore(outputDef, SsaDef(),
    builder.add(Op::Select(ScalarType::eU32, selector, a, b))));

  builder.add(Op::Return());
  return builder;
}

}
