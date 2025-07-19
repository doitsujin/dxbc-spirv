#include "../../dxbc/dxbc_types.h"

#include "../test_common.h"

namespace dxbc_spv::tests::dxbc {

using namespace dxbc_spv::dxbc;

void testDxbcTypeToIrType() {
  ok(resolveType(ComponentType::eVoid, MinPrecision::eNone) == ir::ScalarType::eVoid);
  ok(resolveType(ComponentType::eBool, MinPrecision::eNone) == ir::ScalarType::eBool);
  ok(resolveType(ComponentType::eUint, MinPrecision::eNone) == ir::ScalarType::eU32);
  ok(resolveType(ComponentType::eUint, MinPrecision::eMin16Uint) == ir::ScalarType::eMinU16);
  ok(resolveType(ComponentType::eSint, MinPrecision::eNone) == ir::ScalarType::eI32);
  ok(resolveType(ComponentType::eSint, MinPrecision::eMin16Sint) == ir::ScalarType::eMinI16);
  ok(resolveType(ComponentType::eFloat, MinPrecision::eNone) == ir::ScalarType::eF32);
  ok(resolveType(ComponentType::eFloat, MinPrecision::eMin16Float) == ir::ScalarType::eMinF16);
  ok(resolveType(ComponentType::eFloat, MinPrecision::eMin10Float) == ir::ScalarType::eMinF10);
  ok(resolveType(ComponentType::eDouble, MinPrecision::eNone) == ir::ScalarType::eF64);
}


void testDxbcTypeFromIrType() {
  { auto [t, p] = determineComponentType(ir::ScalarType::eVoid);
    ok(t == ComponentType::eVoid);
    ok(p == MinPrecision::eNone);
  }

  { auto [t, p] = determineComponentType(ir::ScalarType::eBool);
    ok(t == ComponentType::eBool);
    ok(p == MinPrecision::eNone);
  }

  { auto [t, p] = determineComponentType(ir::ScalarType::eU32);
    ok(t == ComponentType::eUint);
    ok(p == MinPrecision::eNone);
  }

  { auto [t, p] = determineComponentType(ir::ScalarType::eMinU16);
    ok(t == ComponentType::eUint);
    ok(p == MinPrecision::eMin16Uint);
  }

  { auto [t, p] = determineComponentType(ir::ScalarType::eI32);
    ok(t == ComponentType::eSint);
    ok(p == MinPrecision::eNone);
  }

  { auto [t, p] = determineComponentType(ir::ScalarType::eMinI16);
    ok(t == ComponentType::eSint);
    ok(p == MinPrecision::eMin16Sint);
  }

  { auto [t, p] = determineComponentType(ir::ScalarType::eF32);
    ok(t == ComponentType::eFloat);
    ok(p == MinPrecision::eNone);
  }

  { auto [t, p] = determineComponentType(ir::ScalarType::eMinF16);
    ok(t == ComponentType::eFloat);
    ok(p == MinPrecision::eMin16Float);
  }

  { auto [t, p] = determineComponentType(ir::ScalarType::eMinF10);
    ok(t == ComponentType::eFloat);
    ok(p == MinPrecision::eMin10Float);
  }
}


void testDxbcSwizzle() {
  /* Test default swizzle */
  Swizzle sw = { };

  for (uint32_t i = 0u; i < 4u; i++) {
    ok(sw.get(i) == Component::eX);
    ok(sw.map(Component(i)) == Component::eX);
  }

  /* Test identity swizzle */
  sw = Swizzle::identity();

  for (uint32_t i = 0u; i < 4u; i++) {
    ok(sw.get(i) == Component(i));
    ok(sw.map(Component(i)) == Component(i));
  }

  /* Test raw construction */
  sw = Swizzle(0x63u);

  ok(sw.map(Component::eX) == Component::eW);
  ok(sw.map(Component::eY) == Component::eX);
  ok(sw.map(Component::eZ) == Component::eZ);
  ok(sw.map(Component::eW) == Component::eY);

  /* Test explicit construction */
  sw = Swizzle(Component::eY, Component::eW, Component::eX, Component::eZ);

  ok(sw.map(Component::eX) == Component::eY);
  ok(sw.map(Component::eY) == Component::eW);
  ok(sw.map(Component::eZ) == Component::eX);
  ok(sw.map(Component::eW) == Component::eZ);

  /* Test mask calculation */
  sw = Swizzle(Component::eX, Component::eY, Component::eZ, Component::eW);
  ok(sw.getReadMask(ComponentBit::eAll) == ComponentBit::eAll);
  ok(sw.getReadMask(ComponentBit::eX) == ComponentBit::eX);
  ok(sw.getReadMask(ComponentBit::eY) == ComponentBit::eY);
  ok(sw.getReadMask(ComponentBit::eZ) == ComponentBit::eZ);
  ok(sw.getReadMask(ComponentBit::eW) == ComponentBit::eW);
  ok(sw.getReadMask(ComponentBit::eY | ComponentBit::eW) == (ComponentBit::eY | ComponentBit::eW));

  sw = Swizzle(Component::eZ, Component::eZ, Component::eZ, Component::eZ);
  ok(sw.getReadMask(ComponentBit::eAll) == ComponentBit::eZ);
  ok(sw.getReadMask(ComponentBit::eX) == ComponentBit::eZ);
  ok(sw.getReadMask(ComponentBit::eY) == ComponentBit::eZ);
  ok(sw.getReadMask(ComponentBit::eZ) == ComponentBit::eZ);
  ok(sw.getReadMask(ComponentBit::eW) == ComponentBit::eZ);

  sw = Swizzle(Component::eX, Component::eX, Component::eY, Component::eX);
  ok(sw.getReadMask(ComponentBit::eAll) == (ComponentBit::eX | ComponentBit::eY));
  ok(sw.getReadMask(ComponentBit::eX | ComponentBit::eY | ComponentBit::eW) == ComponentBit::eX);
  ok(sw.getReadMask(ComponentBit::eZ) == ComponentBit::eY);

  /* Test compaction */
  sw = Swizzle::identity().compact(ComponentBit::eAll);
  ok(sw.map(Component::eX) == Component::eX);
  ok(sw.map(Component::eY) == Component::eY);
  ok(sw.map(Component::eZ) == Component::eZ);
  ok(sw.map(Component::eW) == Component::eW);

  sw = Swizzle::identity().compact(ComponentBit::eX);
  ok(sw.map(Component::eX) == Component::eX);

  sw = Swizzle::identity().compact(ComponentBit::eY);
  ok(sw.map(Component::eX) == Component::eY);

  sw = Swizzle::identity().compact(ComponentBit::eZ);
  ok(sw.map(Component::eX) == Component::eZ);

  sw = Swizzle::identity().compact(ComponentBit::eW);
  ok(sw.map(Component::eX) == Component::eW);

  sw = Swizzle::identity().compact(ComponentBit::eY | ComponentBit::eW);
  ok(sw.map(Component::eX) == Component::eY);
  ok(sw.map(Component::eY) == Component::eW);

  sw = Swizzle(Component::eW, Component::eY, Component::eX, Component::eZ).compact(ComponentBit::eZ | ComponentBit::eW);
  ok(sw.map(Component::eX) == Component::eX);
  ok(sw.map(Component::eY) == Component::eZ);
}

}
