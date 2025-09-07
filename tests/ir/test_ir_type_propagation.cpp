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

  info.processLocalLayout(false, true, true);

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

  info.processLocalLayout(false, true, true);

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
  PropagateResourceTypeRewriteInfo base = { };
  base.oldType = Type(ScalarType::eUnknown).addArrayDimension(10u).addArrayDimension(32u);
  base.oldOuterArrayDims = 1u;

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

  static const std::array<std::tuple<ScalarType, int16_t, int16_t, int16_t>, 10u> s_expected = {{
    { ScalarType::eVoid,    -1, -1, -1 },
    { ScalarType::eVoid,    -1, -1, -1 },
    { ScalarType::eF32,      0,  0,  0 },
    { ScalarType::eF32,      1,  0,  1 },
    { ScalarType::eF32,      2,  0,  2 },
    { ScalarType::eF32,      3,  0,  3 },
    { ScalarType::eU32,     -1,  1,  4 },
    { ScalarType::eVoid,    -1, -1, -1 },
    { ScalarType::eI16,      0,  2,  5 },
    { ScalarType::eI16,      1,  2,  6 },
  }};

  for (const auto& pair : s_entries) {
    auto& e = base.elements.emplace_back();
    e.resolvedType = pair.first;
    e.accessSize = pair.second;
  }

  PropagateResourceTypeRewriteInfo info = base;
  info.processLocalLayout(false, true, true);

  for (size_t i = 0u; i < s_expected.size(); i++) {
    auto [type, component, member, index] = s_expected.at(i);

    const auto& e = info.elements.at(i);
    ok(e.resolvedType == type);
    ok(e.componentIndex == component);
    ok(e.memberIndex == member);
  }

  auto expectedType = Type()
    .addStructMember(ScalarType::eF32, 4u)
    .addStructMember(ScalarType::eU32)
    .addStructMember(ScalarType::eI16, 2u)
    .addArrayDimension(32u);

  ok(!info.isFlattened);
  ok(info.newOuterArrayDims == 1u);
  ok(info.newType == expectedType);

  /* Test again but flatten to scalar array this time */
  info = base;
  info.processLocalLayout(true, true, true);

  ok(info.isFlattened && info.flattenedScalarCount == 7u);
  ok(info.newOuterArrayDims == 1u);

  for (size_t i = 0u; i < s_expected.size(); i++) {
    auto [type, component, member, index] = s_expected.at(i);

    auto expectedType = type;

    if (index >= 0)
      expectedType = ScalarType::eU32;

    const auto& e = info.elements.at(i);
    ok(e.resolvedType == expectedType);
    ok(e.componentIndex < 0);
    ok(e.memberIndex == index);
  }

  expectedType = Type()
    .addStructMember(ScalarType::eU32)
    .addArrayDimension(224u);

  ok(info.newType == expectedType);
}


void testIrPropagateStructuredLdsDynamic() {
  /* 6-component LDS where the first two are accessed as F16, but some elements
   * are accessed dynamically as F32. Must be mapped to a 6-wide F32 array. */
  PropagateResourceTypeRewriteInfo base = { };
  base.oldType = Type(ScalarType::eUnknown).addArrayDimension(6u).addArrayDimension(32u);
  base.oldOuterArrayDims = 1u;
  base.isDynamicallyIndexed = true;
  base.fallbackType = ScalarType::eF32;

  static const std::array<std::pair<ScalarType, uint8_t>, 6u> s_entries = {{
    { ScalarType::eF16,  2 },
    { ScalarType::eVoid, 0 },
    { ScalarType::eVoid, 0 },
    { ScalarType::eVoid, 0 },
    { ScalarType::eVoid, 0 },
    { ScalarType::eVoid, 0 },
  }};

  for (const auto& pair : s_entries) {
    auto& e = base.elements.emplace_back();
    e.resolvedType = pair.first;
    e.accessSize = pair.second;
  }

  PropagateResourceTypeRewriteInfo info = base;
  info.processLocalLayout(false, true, true);

  for (size_t i = 0u; i < info.elements.size(); i++) {
    const auto& e = info.elements.at(i);
    ok(e.resolvedType == ScalarType::eF32);
    ok(e.memberIndex == int16_t(i));
    ok(e.componentIndex < 0);
  }

  auto expectedType = Type(ScalarType::eF32).addArrayDimension(6u).addArrayDimension(32u);
  ok(info.newType == expectedType);

  /* Flatten */
  info = base;
  info.processLocalLayout(true, true, true);

  ok(info.isFlattened && info.flattenedScalarCount == 6u);
  ok(info.newOuterArrayDims == 1u);

  expectedType = Type()
    .addStructMember(ScalarType::eF32)
    .addArrayDimension(192u);

  ok(info.newType == expectedType);
}


void testIrPropagateStructuredLdsAtomic() {
  PropagateResourceTypeRewriteInfo base = { };
  base.oldType = Type(ScalarType::eUnknown).addArrayDimension(2u).addArrayDimension(8u);
  base.oldOuterArrayDims = 1u;
  base.isAtomicallyAccessed = true;
  base.fallbackType = ScalarType::eI32;

  static const std::array<std::pair<ScalarType, uint8_t>, 2u> s_entries = {{
    { ScalarType::eF32, 2 },
    { ScalarType::eI32, 1 },
  }};

  for (const auto& pair : s_entries) {
    auto& e = base.elements.emplace_back();
    e.resolvedType = pair.first;
    e.accessSize = pair.second;
  }

  base.elements.at(1u).isAtomicallyAccessed = true;

  PropagateResourceTypeRewriteInfo info = base;
  info.processLocalLayout(false, true, true);

  for (size_t i = 0u; i < info.elements.size(); i++) {
    const auto& e = info.elements.at(i);
    ok(e.resolvedType == ScalarType::eI32);
    ok(e.memberIndex == int16_t(i));
    ok(e.componentIndex < 0);
  }

  auto expectedType = Type()
    .addStructMember(ScalarType::eI32)
    .addArrayDimension(2u)
    .addArrayDimension(8u);

  ok(info.newType == expectedType);
}


void testIrPropagateCbvTypeSingleVector() {
  PropagateResourceTypeRewriteInfo base = { };
  base.oldType = Type(ScalarType::eUnknown, 4u).addArrayDimension(4u);
  base.oldOuterArrayDims = 0u;

  for (uint32_t i = 0u; i < 16u; i++) {
    auto& e = base.elements.emplace_back();
    e.resolvedType = i ? ScalarType::eVoid : ScalarType::eF32;
    e.accessSize = i ? 0u : 1u;
  }

  /* Structured CBV should get rid of the array altogether */
  PropagateResourceTypeRewriteInfo info = base;
  info.processConstantBufferLayout(true);

  ok(!info.newOuterArrayDims);
  ok(info.newType == Type(ScalarType::eF32, 4u));

  for (uint32_t i = 0u; i < 4u; i++) {
    ok(info.elements.at(i).memberIndex < 0);
    ok(info.elements.at(i).componentIndex == int16_t(i));
  }

  /* For unstructured, we want to retain the array */
  info = base;
  info.processConstantBufferLayout(false);

  ok(!info.newOuterArrayDims);
  ok(info.newType == Type(ScalarType::eF32, 4u).addArrayDimension(1u));

  for (uint32_t i = 0u; i < 4u; i++) {
    ok(info.elements.at(i).memberIndex == 0);
    ok(info.elements.at(i).componentIndex == int16_t(i));
  }
}


void testIrPropagateCbvTypeStructuredComplex() {
  PropagateResourceTypeRewriteInfo base = { };
  base.oldType = Type(ScalarType::eUnknown, 4u).addArrayDimension(4u);
  base.oldOuterArrayDims = 0u;

  static const std::array<std::pair<ScalarType, uint8_t>, 16u> s_entries = {{
    /* Unused, but needs to be retained as a vec4 */
    { ScalarType::eVoid, 0 },
    { ScalarType::eVoid, 0 },
    { ScalarType::eVoid, 0 },
    { ScalarType::eVoid, 0 },

    /* Unused into vec2, should be split */
    { ScalarType::eVoid, 0 },
    { ScalarType::eVoid, 0 },
    { ScalarType::eU32,  2 },
    { ScalarType::eVoid, 0 },

    /* Vector overlap, single vec4 */
    { ScalarType::eF32,  3 },
    { ScalarType::eF32,  1 },
    { ScalarType::eF32,  2 },
    { ScalarType::eVoid, 0 },

    /* Vec2 into unused into scalar, the scalar and unused
     * element should be merged and u16 must be promoted to
     * u32 to maintain layout compatibility. */
    { ScalarType::eU16,  2 },
    { ScalarType::eVoid, 0 },
    { ScalarType::eVoid, 0 },
    { ScalarType::eI32,  1 },
  }};

  static const std::array<std::tuple<ScalarType, int16_t, int16_t>, 16u> s_expected = {{
    { ScalarType::eU32, 0, 0 },
    { ScalarType::eU32, 0, 1 },
    { ScalarType::eU32, 0, 2 },
    { ScalarType::eU32, 0, 3 },

    { ScalarType::eU32, 1, 0 },
    { ScalarType::eU32, 1, 1 },
    { ScalarType::eU32, 2, 0 },
    { ScalarType::eU32, 2, 1 },

    { ScalarType::eF32, 3, 0 },
    { ScalarType::eF32, 3, 1 },
    { ScalarType::eF32, 3, 2 },
    { ScalarType::eF32, 3, 3 },

    { ScalarType::eU32, 4, 0 },
    { ScalarType::eU32, 4, 1 },
    { ScalarType::eI32, 5, 0 },
    { ScalarType::eI32, 5, 1 },
  }};

  for (const auto& pair : s_entries) {
    auto& e = base.elements.emplace_back();
    e.resolvedType = pair.first;
    e.accessSize = pair.second;
  }

  /* Test fully structured */
  PropagateResourceTypeRewriteInfo info = base;
  info.processConstantBufferLayout(true);

  auto expectedType = Type()
    .addStructMember(ScalarType::eU32, 4)
    .addStructMember(ScalarType::eU32, 2)
    .addStructMember(ScalarType::eU32, 2)
    .addStructMember(ScalarType::eF32, 4)
    .addStructMember(ScalarType::eU32, 2)
    .addStructMember(ScalarType::eI32, 2);

  ok(info.newType == expectedType);

  for (uint32_t i = 0u; i < 16u; i++) {
    auto [type, member, component] = s_expected.at(i);
    ok(info.elements.at(i).resolvedType == type);
    ok(info.elements.at(i).memberIndex == member);
    ok(info.elements.at(i).componentIndex == component);
  }

  /* Test unstructured */
  info = base;
  info.processConstantBufferLayout(false);

  ok(!info.newOuterArrayDims);
  ok(info.newType == Type(ScalarType::eU32, 4u).addArrayDimension(4u));

  for (uint32_t i = 0u; i < 16u; i++) {
    ok(info.elements.at(i).memberIndex == int16_t(i / 4u));
    ok(info.elements.at(i).componentIndex == int16_t(i % 4u));
  }
}


void testIrPropagateCbvTypeDynamicallyIndexed() {
  PropagateResourceTypeRewriteInfo base = { };
  base.oldType = Type(ScalarType::eUnknown, 4u).addArrayDimension(4096u);
  base.oldOuterArrayDims = 0u;
  base.isDynamicallyIndexed = true;
  base.fallbackType = ScalarType::eF16;
  base.elements.resize(16384u);

  /* Test fully structured */
  PropagateResourceTypeRewriteInfo info = base;
  info.processConstantBufferLayout(true);
  ok(info.newType == Type(ScalarType::eF32, 4u).addArrayDimension(4096u));

  /* Test unstructured, should have the same result */
  info = base;
  info.processConstantBufferLayout(false);

  ok(!info.newOuterArrayDims);
  ok(info.newType == Type(ScalarType::eF32, 4u).addArrayDimension(4096u));
}


void testIrPropagateStructuredSrvSimpleScalar() {
  PropagateResourceTypeRewriteInfo base = { };
  base.oldType = Type(ScalarType::eUnknown, 1u).addArrayDimension(1u).addArrayDimension(0u);
  base.oldOuterArrayDims = 1u;

  auto& e = base.elements.emplace_back();
  e.resolvedType = ScalarType::eI32;
  e.accessSize = 1u;

  PropagateResourceTypeRewriteInfo info = base;
  info.processResourceBufferLayout(true);

  ok(info.newOuterArrayDims == 1u);
  ok(info.newType == Type(ScalarType::eI32).addArrayDimension(0u));
  ok(info.elements.front().resolvedType == ScalarType::eI32);
  ok(info.elements.front().memberIndex < 0);
  ok(info.elements.front().componentIndex < 0);

  info = base;
  info.processResourceBufferLayout(false);

  ok(info.newOuterArrayDims == 1u);
  ok(info.newType == Type(ScalarType::eI32).addArrayDimension(1u).addArrayDimension(0u));
  ok(info.elements.front().resolvedType == ScalarType::eI32);
  ok(info.elements.front().memberIndex == 0);
  ok(info.elements.front().componentIndex < 0);
}


void testIrPropagateStructuredSrvSimpleStructure() {
  PropagateResourceTypeRewriteInfo base = { };
  base.oldType = Type(ScalarType::eUnknown).addArrayDimension(12u).addArrayDimension(0u);
  base.oldOuterArrayDims = 1u;

  static const std::array<std::pair<ScalarType, uint8_t>, 12u> s_entries = {{
    /* Unused, retain as u32 */
    { ScalarType::eVoid, 0u },

    /* Simple vec3 with scalar loads */
    { ScalarType::eF32,   3u },
    { ScalarType::eVoid,  0u },
    { ScalarType::eF32,   1u },

    /* Single used scalar in vec4 */
    { ScalarType::eVoid,  0u },
    { ScalarType::eVoid,  0u },
    { ScalarType::eI32,   1u },
    { ScalarType::eVoid,  0u },

    /* Int vector that must be promoted to u32 */
    { ScalarType::eU16,   2u },
    { ScalarType::eVoid,  0u },

    /* Unused */
    { ScalarType::eVoid,  0u },
    { ScalarType::eVoid,  0u },
  }};

  static const std::array<std::tuple<ScalarType, int16_t, int16_t>, 12u> s_expected = {{
    { ScalarType::eU32, 0, -1 },
    { ScalarType::eF32, 1, 0 },
    { ScalarType::eF32, 1, 1 },
    { ScalarType::eF32, 1, 2 },

    { ScalarType::eI32, 2, 0 },
    { ScalarType::eI32, 2, 1 },
    { ScalarType::eI32, 2, 2 },
    { ScalarType::eI32, 2, 3 },

    { ScalarType::eU32, 3, 0 },
    { ScalarType::eU32, 3, 1 },
    { ScalarType::eU32, 4, 0 },
    { ScalarType::eU32, 4, 1 },
  }};

  for (const auto& pair : s_entries) {
    auto& e = base.elements.emplace_back();
    e.resolvedType = pair.first;
    e.accessSize = pair.second;
  }

  /* Test structured */
  PropagateResourceTypeRewriteInfo info = base;
  info.processResourceBufferLayout(true);

  for (uint32_t i = 0u; i < 12u; i++) {
    auto [type, member, component] = s_expected.at(i);
    ok(info.elements.at(i).resolvedType == type);
    ok(info.elements.at(i).memberIndex == member);
    ok(info.elements.at(i).componentIndex == component);
  }

  auto expectedType = Type()
    .addStructMember(ScalarType::eU32)
    .addStructMember(ScalarType::eF32, 3)
    .addStructMember(ScalarType::eI32, 4)
    .addStructMember(ScalarType::eU32, 2)
    .addStructMember(ScalarType::eU32, 2)
    .addArrayDimension(0u);

  ok(info.newOuterArrayDims == 1u);
  ok(info.newType == expectedType);

  /* Test unstructured */
  info = base;
  info.processResourceBufferLayout(false);

  for (uint32_t i = 0u; i < 12u; i++) {
    ok(info.elements.at(i).resolvedType == ScalarType::eU32);
    ok(info.elements.at(i).memberIndex == int16_t(i));
    ok(info.elements.at(i).componentIndex < 0);
  }

  expectedType = Type(ScalarType::eU32).addArrayDimension(12u).addArrayDimension(0u);

  ok(info.newOuterArrayDims == 1u);
  ok(info.newType == expectedType);
}


void testIrPropagateStructuredSrvVectorOverlap() {
  PropagateResourceTypeRewriteInfo base = { };
  base.oldType = Type(ScalarType::eUnknown).addArrayDimension(4u).addArrayDimension(0u);
  base.oldOuterArrayDims = 1u;

  static const std::array<std::pair<ScalarType, uint8_t>, 4u> s_entries = {{
    /* vec3 where z component is part of another vec2 */
    { ScalarType::eF32,   3u },
    { ScalarType::eVoid,  0u },
    { ScalarType::eF32,   2u },
    { ScalarType::eVoid,  0u },
  }};

  for (const auto& pair : s_entries) {
    auto& e = base.elements.emplace_back();
    e.resolvedType = pair.first;
    e.accessSize = pair.second;
  }

  /* Test structured */
  PropagateResourceTypeRewriteInfo info = base;
  info.processResourceBufferLayout(true);

  auto expectedType = Type(ScalarType::eF32).addArrayDimension(4u).addArrayDimension(0u);

  ok(info.newType == expectedType);

  for (uint32_t i = 0u; i < 4u; i++) {
    ok(info.elements.at(i).resolvedType == ScalarType::eF32);
    ok(info.elements.at(i).memberIndex == int16_t(i));
    ok(info.elements.at(i).componentIndex < 0);
  }

  /* Test unstructured, same result */
  info = base;
  info.processResourceBufferLayout(false);

  ok(info.newType == expectedType);

  for (uint32_t i = 0u; i < 4u; i++) {
    ok(info.elements.at(i).resolvedType == ScalarType::eF32);
    ok(info.elements.at(i).memberIndex == int16_t(i));
    ok(info.elements.at(i).componentIndex < 0);
  }
}


void testIrPropagateStructuredSrvDynamic() {
  PropagateResourceTypeRewriteInfo base = { };
  base.oldType = Type(ScalarType::eUnknown).addArrayDimension(12u).addArrayDimension(0u);
  base.oldOuterArrayDims = 1u;
  base.isDynamicallyIndexed = true;
  base.fallbackType = ScalarType::eF32;

  static const std::array<std::pair<ScalarType, uint8_t>, 12u> s_entries = {{
    /* Unused, retain as u32 */
    { ScalarType::eVoid,  0u },

    /* Simple vec3 with scalar loads */
    { ScalarType::eF32,   3u },
    { ScalarType::eVoid,  0u },
    { ScalarType::eF32,   1u },

    /* Single used scalar in vec4 */
    { ScalarType::eVoid,  0u },
    { ScalarType::eVoid,  0u },
    { ScalarType::eI32,   1u },
    { ScalarType::eVoid,  0u },

    /* Int vector that must be promoted to u32 */
    { ScalarType::eU16,   2u },
    { ScalarType::eVoid,  0u },

    /* Unused */
    { ScalarType::eVoid,  0u },
    { ScalarType::eVoid,  0u },
  }};

  for (const auto& pair : s_entries) {
    auto& e = base.elements.emplace_back();
    e.resolvedType = pair.first;
    e.accessSize = pair.second;
  }

  /* Test structured */
  PropagateResourceTypeRewriteInfo info = base;
  info.processResourceBufferLayout(true);

  auto expectedType = Type(ScalarType::eU32).addArrayDimension(12u).addArrayDimension(0u);

  ok(info.newType == expectedType);

  for (uint32_t i = 0u; i < 12u; i++) {
    ok(info.elements.at(i).resolvedType == ScalarType::eU32);
    ok(info.elements.at(i).memberIndex == int16_t(i));
    ok(info.elements.at(i).componentIndex < 0);
  }

  /* Test unstructured */
  info = base;
  info.processResourceBufferLayout(false);

  ok(info.newType == expectedType);

  for (uint32_t i = 0u; i < 12u; i++) {
    ok(info.elements.at(i).resolvedType == ScalarType::eU32);
    ok(info.elements.at(i).memberIndex == int16_t(i));
    ok(info.elements.at(i).componentIndex < 0);
  }
}


void testIrPropagateStructuredSrvAtomic() {
  PropagateResourceTypeRewriteInfo base = { };
  base.oldType = Type(ScalarType::eUnknown).addArrayDimension(4u).addArrayDimension(0u);
  base.oldOuterArrayDims = 1u;
  base.isAtomicallyAccessed = true;
  base.fallbackType = ScalarType::eI32;

  static const std::array<std::pair<ScalarType, uint8_t>, 4u> s_entries = {{
    /* vec3 where z component is part of another vec2 */
    { ScalarType::eF32,   2u },
    { ScalarType::eVoid,  0u },
    { ScalarType::eI32,   1u },
    { ScalarType::eI32,   1u },
  }};

  for (const auto& pair : s_entries) {
    auto& e = base.elements.emplace_back();
    e.resolvedType = pair.first;
    e.accessSize = pair.second;
  }

  base.elements.at(2u).isAtomicallyAccessed = true;

  /* Test structured */
  PropagateResourceTypeRewriteInfo info = base;
  info.processResourceBufferLayout(true);

  auto expectedType = Type()
    .addStructMember(ScalarType::eF32, 2)
    .addStructMember(ScalarType::eI32, 2)
    .addArrayDimension(0u);

  ok(info.newType == expectedType);

  /* Test unstructured */
  info = base;
  info.processResourceBufferLayout(false);

  expectedType = Type(ScalarType::eI32).addArrayDimension(4u).addArrayDimension(0u);

  ok(info.newType == expectedType);
}


void testIrPropagateStructuredSrvAtomicWithOverlap() {
  PropagateResourceTypeRewriteInfo base = { };
  base.oldType = Type(ScalarType::eUnknown).addArrayDimension(4u).addArrayDimension(0u);
  base.oldOuterArrayDims = 1u;
  base.isAtomicallyAccessed = true;
  base.fallbackType = ScalarType::eI32;

  static const std::array<std::pair<ScalarType, uint8_t>, 4u> s_entries = {{
    /* vec3 where z component is part of another vec2 */
    { ScalarType::eI32,   4u },
    { ScalarType::eVoid,  0u },
    { ScalarType::eI32,   1u },
    { ScalarType::eI32,   1u },
  }};

  for (const auto& pair : s_entries) {
    auto& e = base.elements.emplace_back();
    e.resolvedType = pair.first;
    e.accessSize = pair.second;
  }

  base.elements.at(2u).isAtomicallyAccessed = true;

  /* Test structured */
  PropagateResourceTypeRewriteInfo info = base;
  info.processResourceBufferLayout(true);

  auto expectedType = Type(ScalarType::eI32).addArrayDimension(4u).addArrayDimension(0u);

  ok(info.newType == expectedType);

  /* Test unstructured */
  info = base;
  info.processResourceBufferLayout(false);

  expectedType = Type(ScalarType::eI32).addArrayDimension(4u).addArrayDimension(0u);

  ok(info.newType == expectedType);
}


void testIrPropagateRawSrvType() {
  PropagateResourceTypeRewriteInfo base = { };
  base.oldType = Type(ScalarType::eUnknown, 1u).addArrayDimension(0u);
  base.oldOuterArrayDims = 1u;

  auto& e = base.elements.emplace_back();
  e.resolvedType = ScalarType::eF32;
  e.accessSize = 4u;

  PropagateResourceTypeRewriteInfo info = base;
  info.processResourceBufferLayout(true);
  ok(info.newOuterArrayDims == 1u);
  ok(info.newType == Type(ScalarType::eF32).addArrayDimension(0u));

  info = base;
  info.processResourceBufferLayout(false);
  ok(info.newOuterArrayDims == 1u);
  ok(info.newType == Type(ScalarType::eF32).addArrayDimension(0u));
}


void testIrTypePropagation() {
  RUN_TEST(testIrPropagateStructuredLdsToScalar);
  RUN_TEST(testIrPropagateRawLdsType);
  RUN_TEST(testIrPropagateStructuredLdsComplex);
  RUN_TEST(testIrPropagateStructuredLdsDynamic);
  RUN_TEST(testIrPropagateStructuredLdsAtomic);

  RUN_TEST(testIrPropagateCbvTypeSingleVector);
  RUN_TEST(testIrPropagateCbvTypeStructuredComplex);
  RUN_TEST(testIrPropagateCbvTypeDynamicallyIndexed);

  RUN_TEST(testIrPropagateStructuredSrvSimpleScalar);
  RUN_TEST(testIrPropagateStructuredSrvSimpleStructure);
  RUN_TEST(testIrPropagateStructuredSrvVectorOverlap);
  RUN_TEST(testIrPropagateStructuredSrvDynamic);
  RUN_TEST(testIrPropagateStructuredSrvAtomic);
  RUN_TEST(testIrPropagateStructuredSrvAtomicWithOverlap);
  RUN_TEST(testIrPropagateRawSrvType);
}

}
