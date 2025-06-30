#include "../../ir/ir.h"

#include "../test_common.h"

namespace dxbc_spv::tests::ir {

using namespace dxbc_spv::ir;

void testIrTypeBasicProperties() {
  BasicType tVoid(ScalarType::eVoid);

  ok(tVoid.isVoidType());
  ok(tVoid.isScalar());
  ok(!tVoid.isNumericType());
  ok(!tVoid.isMinPrecisionType());
  ok(!tVoid.isDescriptorType());
  ok(!tVoid.isVector());
  ok(!tVoid.byteSize());

  ok(tVoid == BasicType());

  BasicType tBoolVec(ScalarType::eBool, 3);
  ok(!tBoolVec.isVoidType());
  ok(!tBoolVec.isNumericType());
  ok(!tBoolVec.isMinPrecisionType());
  ok(!tBoolVec.isDescriptorType());
  ok(!tBoolVec.isUnknownType());
  ok(tBoolVec.isBoolType());
  ok(!tBoolVec.isScalar());
  ok(tBoolVec.isVector());

  BasicType tUnknown(ScalarType::eUnknown);
  ok(!tUnknown.isVoidType());
  ok(tUnknown.isNumericType());
  ok(!tUnknown.isMinPrecisionType());
  ok(!tUnknown.isDescriptorType());
  ok(tUnknown.isUnknownType());
  ok(!tUnknown.isBoolType());
  ok(tUnknown.isScalar());
  ok(!tUnknown.isVector());

  Type tDefault;
  ok(tDefault.isVoidType());
  ok(!tDefault.getStructMemberCount());
  ok(tDefault.getBaseType(0) == BasicType());
  ok(tDefault.isScalarType());
  ok(!tDefault.isVectorType());
  ok(!tDefault.isStructType());
  ok(!tDefault.isArrayType());

  static const std::array<std::pair<ScalarType, uint32_t>, 11> s_numericTypes = {{
    { ScalarType::eI8,  1u },
    { ScalarType::eI16, 2u },
    { ScalarType::eI32, 4u },
    { ScalarType::eI64, 8u },
    { ScalarType::eU8,  1u },
    { ScalarType::eU16, 2u },
    { ScalarType::eU32, 4u },
    { ScalarType::eU64, 8u },
    { ScalarType::eF16, 2u },
    { ScalarType::eF32, 4u },
    { ScalarType::eF64, 8u },
  }};

  for (uint32_t i = 1u; i <= 4u; i++) {
    for (auto& e : s_numericTypes) {
      BasicType t(e.first, i);

      ok(t.isNumericType());
      ok(t.isScalar() == (i == 1u));
      ok(t.isVector() == (i != 1u));
      ok(t.byteSize() == e.second * i);
      ok(t.byteAlignment() == e.second);

      Type type(t);
      ok(type.getStructMemberCount() == 1u);
      ok(!type.isVoidType());
      ok(type.isBasicType());
      ok(type.isScalarType() == t.isScalar());
      ok(type.isVectorType() == t.isVector());
      ok(type.byteSize() == t.byteSize());
      ok(type.byteAlignment() == t.byteAlignment());
      ok(!type.isStructType());
      ok(!type.isArrayType());
      ok(type.getBaseType(0u) == t);
      ok(type.getSubType(0) == (i > 1u ? Type(e.first) : Type()));

      for (uint32_t j = 0u; j < i; j++)
        ok(type.resolveFlattenedType(j) == e.first);
    }
  }
}


void testIrTypeBasicFrom() {
  for (uint32_t i = 1u; i <= 4u; i++) {
    ok(BasicType::from(int8_t(0), i) == BasicType(ScalarType::eI8, i));
    ok(BasicType::from(int16_t(0), i) == BasicType(ScalarType::eI16, i));
    ok(BasicType::from(-1, i) == BasicType(ScalarType::eI32, i));
    ok(BasicType::from(int64_t(0ll), i) == BasicType(ScalarType::eI64, i));

    ok(BasicType::from(uint8_t(0), i) == BasicType(ScalarType::eU8, i));
    ok(BasicType::from(uint16_t(0), i) == BasicType(ScalarType::eU16, i));
    ok(BasicType::from(0u, i) == BasicType(ScalarType::eU32, i));
    ok(BasicType::from(uint64_t(0), i) == BasicType(ScalarType::eU64, i));

    ok(BasicType::from(float16_t(6.0f), i) == BasicType(ScalarType::eF16, i));
    ok(BasicType::from(1.0f, i) == BasicType(ScalarType::eF32, i));
    ok(BasicType::from(1.0, i) == BasicType(ScalarType::eF64, i));
  }
}


void testIrTypeStruct() {
  Type tStruct = Type()
    .addStructMember(BasicType(ScalarType::eF32, 4))
    .addStructMember(ScalarType::eU16)
    .addStructMember(ScalarType::eU64);

  ok(!tStruct.isVoidType());
  ok(!tStruct.isBasicType());
  ok(tStruct.isStructType());
  ok(!tStruct.isArrayType());

  ok(tStruct.getStructMemberCount() == 3u);

  ok(tStruct.byteSize() == 32u);
  ok(tStruct.byteAlignment() == 8u);

  ok(tStruct.resolveFlattenedType(0u) == ScalarType::eF32);
  ok(tStruct.resolveFlattenedType(1u) == ScalarType::eF32);
  ok(tStruct.resolveFlattenedType(2u) == ScalarType::eF32);
  ok(tStruct.resolveFlattenedType(3u) == ScalarType::eF32);
  ok(tStruct.resolveFlattenedType(4u) == ScalarType::eU16);
  ok(tStruct.resolveFlattenedType(5u) == ScalarType::eU64);

  ok(tStruct.getSubType(0u) == BasicType(ScalarType::eF32, 4));
  ok(tStruct.getSubType(1u) == ScalarType::eU16);
  ok(tStruct.getSubType(2u) == ScalarType::eU64);

  ok(tStruct.byteOffset(0u) == 0u);
  ok(tStruct.byteOffset(1u) == 16u);
  ok(tStruct.byteOffset(2u) == 24u);
}


void testIrTypeArray() {
  Type tArray = Type(BasicType(ScalarType::eF32, 4))
    .addArrayDimension(24u)
    .addArrayDimension(0u);

  ok(!tArray.isVoidType());
  ok(!tArray.isBasicType());
  ok(!tArray.isStructType());
  ok(tArray.isArrayType());
  ok(tArray.isUnboundedArray());
  ok(!tArray.isSizedArray());
  ok(tArray.getArrayDimensions() == 2u);
  ok(tArray.getStructMemberCount() == 1u);
  ok(tArray.getArraySize(0) == 24u);
  ok(tArray.getArraySize(1) == 0u);
  ok(tArray.byteSize() == 384u);
  ok(tArray.byteAlignment() == 4u);

  for (uint32_t i = 0u; i < 24 * 4u; i++)
    ok(tArray.resolveFlattenedType(i) == ScalarType::eF32);

  tArray = tArray.getSubType(-1);

  ok(!tArray.isBasicType());
  ok(!tArray.isStructType());
  ok(tArray.isArrayType());
  ok(tArray.getArrayDimensions() == 1u);
  ok(!tArray.isUnboundedArray());
  ok(tArray.isSizedArray());
  ok(tArray.byteSize() == 384u);
  ok(tArray.byteAlignment() == 4u);

  tArray = tArray.getSubType(-1);

  ok(tArray == BasicType(ScalarType::eF32, 4u));

  ok(tArray.isBasicType());
  ok(!tArray.isStructType());
  ok(!tArray.isArrayType());
  ok(tArray.getArrayDimensions() == 0u);
  ok(!tArray.isUnboundedArray());
  ok(!tArray.isSizedArray());
  ok(tArray.byteSize() == 16u);
  ok(tArray.byteAlignment() == 4u);

  Type tStruct = Type()
    .addStructMember(ScalarType::eU32)
    .addStructMember(ScalarType::eBool);

  tArray = Type(tStruct).addArrayDimension(4u);

  ok(!tArray.isBasicType());
  ok(!tArray.isStructType());
  ok(tArray.isArrayType());
  ok(tArray.getArrayDimensions() == 1u);
  ok(!tArray.isUnboundedArray());
  ok(tArray.isSizedArray());

  for (uint32_t i = 0u; i < 4u; i++) {
    ok(tArray.resolveFlattenedType(2u * i + 0u) == ScalarType::eU32);
    ok(tArray.resolveFlattenedType(2u * i + 1u) == ScalarType::eBool);
  }

  ok(tArray.getSubType(0) == tStruct);
}


void testIrType() {
  RUN_TEST(testIrTypeBasicProperties);
  RUN_TEST(testIrTypeBasicFrom);
  RUN_TEST(testIrTypeStruct);
  RUN_TEST(testIrTypeArray);

}

}
