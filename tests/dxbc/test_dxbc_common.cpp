#include "../../dxbc/dxbc_parser.h"
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


void testDxbcSampleControlToken() {
  SampleControlToken token;
  ok(!token);
  ok(!token.u());
  ok(!token.v());
  ok(!token.w());

  /* Test parsing the actual extended token */
  auto dword = uint32_t(1u) |
               uint32_t(0xeu <<  9u) | /* -2 */
               uint32_t(0x7u << 13u) | /*  7 */
               uint32_t(0x8u << 17u);  /* -8 */

  ok(extractExtendedOpcodeType(dword) == ExtendedOpcodeType::eSampleControls);

  token = SampleControlToken(extractExtendedOpcodePayload(dword));
  ok(token);
  ok(token.u() == -2);
  ok(token.v() ==  7);
  ok(token.w() == -8);

  /* Test encoding token */
  ok(token.asToken() == dword);

  /* Test initializing token */
  ok(token == SampleControlToken(-2, 7, -8));
}


void testDxbcResourceDimToken() {
  ResourceDimToken token;
  ok(!token);
  ok(token.getDim() == ResourceDim::eUnknown);
  ok(!token.getStructureStride());

  /* Test parsing the extended token */
  auto dword = uint32_t(2u) |
               uint32_t(12u << 6u) | /* structured buffer */
               uint32_t(2048u << 11u);

  ok(extractExtendedOpcodeType(dword) == ExtendedOpcodeType::eResourceDim);

  token = ResourceDimToken(extractExtendedOpcodePayload(dword));
  ok(token);
  ok(token.getDim() == ResourceDim::eStructuredBuffer);
  ok(token.getStructureStride() == 2048u);

  /* Test encoding token */
  ok(token.asToken() == dword);

  /* Test initializing token */
  ok(token == ResourceDimToken(ResourceDim::eStructuredBuffer, 2048u));
}


void testDxbcResourceTypeToken() {
  ResourceTypeToken token;
  ok(!token);

  /* Test parsing the extended token */
  auto dword = uint32_t(3u) |
               uint32_t(1u <<  6u) | /* unorm */
               uint32_t(5u << 10u) | /* float */
               uint32_t(4u << 14u) | /* uint */
               uint32_t(7u << 18u);  /* double */

  ok(extractExtendedOpcodeType(dword) == ExtendedOpcodeType::eResourceReturnType);

  token = ResourceTypeToken(extractExtendedOpcodePayload(dword));
  ok(token);
  ok(token.x() == SampledType::eUnorm);
  ok(token.y() == SampledType::eFloat);
  ok(token.z() == SampledType::eUint);
  ok(token.w() == SampledType::eDouble);

  /* Test encoding token */
  ok(token.asToken() == dword);
  ok(token.asImmediate() == dword >> 6u);

  /* Test initializing token */
  ok(token == ResourceTypeToken(SampledType::eUnorm, SampledType::eFloat,
                                SampledType::eUint,  SampledType::eDouble));
}


void testDxbcOpCodeToken() {
  /* Test extended tokens */
  auto sampleControls = SampleControlToken(-1, 0, 1);
  auto resourceType = ResourceTypeToken(SampledType::eMixed, SampledType::eMixed, SampledType::eMixed, SampledType::eMixed);
  auto resourceDim = ResourceDimToken(ResourceDim::eTexture2D, 0u);

  auto op = OpToken(OpCode::eLd).setLength(15u)
    .setSampleControlToken(sampleControls)
    .setResourceDimToken(resourceDim)
    .setResourceTypeToken(resourceType);

  ok(op && !op.isCustomData());
  ok(op.getLength() == 15u);
  ok(op.getOpCode() == OpCode::eLd);
  ok(op.getSampleControlToken() && op.getSampleControlToken() == sampleControls);
  ok(op.getResourceDimToken() && op.getResourceDimToken() == resourceDim);
  ok(op.getResourceTypeToken() && op.getResourceTypeToken() == resourceType);

  /* Test custom data token */
  op = OpToken(OpCode::eCustomData).setLength(65536u);
  ok(op && op.isCustomData());
  ok(op.getLength() == 65536u);
  ok(op.getOpCode() == OpCode::eCustomData);

  /* Test some basic instruction flags */
  op = OpToken(OpCode::eAdd).setLength(3u);

  ok(op && !op.isCustomData());
  ok(op.getLength() == 3u);
  ok(op.getOpCode() == OpCode::eAdd);
  ok(!op.isSaturated());
  ok(!op.getPreciseMask());

  op.setSaturated(true);

  ok(op.isSaturated());
  ok(!op.getPreciseMask());

  op.setPreciseMask(WriteMask(ComponentBit::eX | ComponentBit::eW));

  ok(op.isSaturated());
  ok(op.getPreciseMask());
  ok(op.getPreciseMask() == WriteMask(ComponentBit::eX | ComponentBit::eW));
}


void testDxbcOperandToken() {
  Operand operand;
  ok(!operand);

  /* Test immediates */
  operand = Operand({ OperandKind::eImm32, ir::ScalarType::eU32 }, RegisterType::eImm32, ComponentCount::e1Component).setImmediate(0u, 42u);

  ok(operand);
  ok(operand.getRegisterType() == RegisterType::eImm32);
  ok(!operand.getIndexDimensions());
  ok(operand.getImmediate<uint32_t>(0u) == 42u);
  ok(!operand.getModifiers());

  /* Test constant buffer array with 3D indexing */
  auto absNonUniformMod = OperandModifiers(false, true, MinPrecision::eNone, true);
  ok(!absNonUniformMod.isNegated());
  ok(absNonUniformMod.isAbsolute());
  ok(absNonUniformMod.isNonUniform());

  auto swizzle = Swizzle(Component::eY, Component::eW, Component::eX, Component::eZ);

  operand = Operand({ OperandKind::eSrcReg, ir::ScalarType::eF32 }, RegisterType::eCbv, ComponentCount::e4Component)
    .setSwizzle(swizzle)
    .addIndex(16u, 1u)
    .addIndex(46u, -1u)
    .addIndex(0u, 2u)
    .setModifiers(absNonUniformMod);

  ok(operand);
  ok(operand.getRegisterType() == RegisterType::eCbv);
  ok(operand.getIndexDimensions() == 3u);
  ok(operand.getIndexType(0u) == IndexType::eImm32PlusRelative);
  ok(operand.getIndexType(1u) == IndexType::eImm32);
  ok(operand.getIndexType(2u) == IndexType::eRelative);
  ok(operand.getIndex(0u) == 16u);
  ok(operand.getIndex(1u) == 46u);
  ok(operand.getIndex(2u) == 0u);
  ok(operand.getIndexOperand(0u) == 1u);
  ok(operand.getIndexOperand(1u) == -1u);
  ok(operand.getIndexOperand(2u) == 2u);
  ok(operand.getSwizzle() == swizzle);
  ok(operand.getModifiers() == absNonUniformMod);
}

}
