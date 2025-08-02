#include <utility>

#include "../../ir/ir_builder.h"

#include "../../ir/passes/ir_pass_propagate_resource_types.h"

#include "../test_common.h"

namespace dxbc_spv::tests::ir {

using namespace dxbc_spv::ir;

void testIrPropagateStructuredLdsToScalar() {
  /* Single two-level array scalar to F32 */
  PropagateResourceTypeRewriteInfo info = { };
  info.oldType = Type(ScalarType::eUnknown).addArrayDimension(1u).addArrayDimension(32u);
  info.oldOuterArrayDims = 1u;

  auto& e = info.elements.emplace_back();
  e.resolvedType = ScalarType::eF32;
  e.accessSize = 1u;

  info.processLdsLayout(false, true);

  ok(e.componentIndex < 0);
  ok(e.memberIndex < 0);
  ok(e.resolvedType == ScalarType::eF32);
  ok(!e.isAtomicallyAccessed);

  ok(!info.isAtomicallyAccessed);
  ok(!info.isDynamicallyIndexed);
  ok(!info.isFlattened);

  ok(info.newType == Type(ScalarType::eF32).addArrayDimension(32u));
}


void testIrPropagateRawLdsType() {
  /* Flat array to U32 via Unknown */
  PropagateResourceTypeRewriteInfo info = { };
  info.oldType = Type(ScalarType::eUnknown).addArrayDimension(256u);
  info.oldOuterArrayDims = 1u;

  auto& e = info.elements.emplace_back();
  e.resolvedType = ScalarType::eUnknown;
  e.accessSize = 1u;

  info.processLdsLayout(false, true);

  ok(e.componentIndex < 0);
  ok(e.memberIndex < 0);
  ok(e.resolvedType == ScalarType::eU32);
  ok(!e.isAtomicallyAccessed);

  ok(!info.isAtomicallyAccessed);
  ok(!info.isDynamicallyIndexed);
  ok(!info.isFlattened);

  ok(info.newType == Type(ScalarType::eU32).addArrayDimension(256u));
}


void testIrPropagateStructuredLdsComplex() {
  /* 10-component LDS with {unused x2, vec4<f32>, unknown, unused, vec2<i16>} */
  PropagateResourceTypeRewriteInfo info = { };
  info.oldType = Type(ScalarType::eUnknown).addArrayDimension(10u).addArrayDimension(32u);
  info.oldOuterArrayDims = 1u;

  static const std::array<std::pair<ScalarType, uint8_t>, 10u> s_entries = {{
    { ScalarType::eVoid,    0 },
    { ScalarType::eVoid,    0 },
    { ScalarType::eF32,     4 },
    { ScalarType::eVoid,    0 },
    { ScalarType::eVoid,    0 },
    { ScalarType::eVoid,    0 },
    { ScalarType::eUnknown, 1 },
    { ScalarType::eVoid,    0 },
    { ScalarType::eI16,     2 },
    { ScalarType::eVoid,    0 },
  }};

  static const std::array<std::tuple<ScalarType, int16_t, int16_t>, 10u> s_expected = {{
    { ScalarType::eVoid,    -1, -1 },
    { ScalarType::eVoid,    -1, -1 },
    { ScalarType::eF32,      0,  0 },
    { ScalarType::eF32,      1,  0 },
    { ScalarType::eF32,      2,  0 },
    { ScalarType::eF32,      3,  0 },
    { ScalarType::eU32,     -1,  1 },
    { ScalarType::eVoid,    -1, -1 },
    { ScalarType::eI16,      0,  2 },
    { ScalarType::eI16,      1,  2 },
  }};

  for (const auto& pair : s_entries) {
    auto& e = info.elements.emplace_back();
    e.resolvedType = pair.first;
    e.accessSize = pair.second;
  }

  info.processLdsLayout(false, true);

  for (size_t i = 0u; i < s_expected.size(); i++) {
    auto [type, component, member] = s_expected.at(i);

    const auto& e = info.elements.at(i);
    ok(e.resolvedType == type);
    ok(e.componentIndex == component);
    ok(e.memberIndex == member);
  }

  auto expectedType = Type()
    .addStructMember(ScalarType::eF32, 4u)
    .addStructMember(ScalarType::eU32)
    .addStructMember(ScalarType::eI16, 2u)
    .addArrayDimension(32);

  ok(info.newType == expectedType);
}


void testIrTypePropagation() {
  RUN_TEST(testIrPropagateStructuredLdsToScalar);
  RUN_TEST(testIrPropagateRawLdsType);
  RUN_TEST(testIrPropagateStructuredLdsComplex);
}

}
