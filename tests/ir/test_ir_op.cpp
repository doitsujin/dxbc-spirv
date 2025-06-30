#include "../../ir/ir.h"

#include "../test_common.h"

namespace dxbc_spv::tests::ir {

using namespace dxbc_spv::ir;

void testIrOpDefault() {
  Op op = { };

  ok(!op);
  ok(op.getOpCode() == OpCode::eUnknown);
  ok(op.getOperandCount() == 0u);
  ok(op.getType().isVoidType());
}


void testIrOpDebugName() {
  Op op = Op::DebugName(SsaDef(1u), "r0");

  ok(op);

  ok(op.getOpCode() == OpCode::eDebugName);
  ok(op.isDeclarative());
  ok(op.getType().isVoidType());
  ok(op.getFirstLiteralOperandIndex() == 1u);
  ok(op.getOperandCount() == 2u);

  ok(SsaDef(op.getOperand(0u)) == SsaDef(1u));
  ok(op.getLiteralString(1u) == "r0");

  op = Op::DebugName(SsaDef(), "deadbeef");
  ok(op.getOperandCount() == 2u);
  ok(op.getLiteralString(1u) == "deadbeef");

  op = Op::DebugName(SsaDef(), "VeryLongNullTerminatedDebugName");
  ok(op.getLiteralString(1u) == "VeryLongNullTerminatedDebugName");
}


void testIrOpConstant() {
  Op op = Op::Constant(false);

  ok(op);
  ok(op.isDeclarative());
  ok(op.getOperandCount() == 1u);
  ok(op.getOpCode() == OpCode::eConstant);
  ok(op.getType() == ScalarType::eBool);

  ok(Op::Constant(1).getType() == ScalarType::eI32);
  ok(Op::Constant(1.0f).getType() == ScalarType::eF32);
  ok(Op::Constant(1.0).getType() == ScalarType::eF64);

  op = Op::Constant(1.0f, 2.0f);
  ok(op.getType() == BasicType(ScalarType::eF32, 2));
  ok(op.getOperandCount() == 2u);
  ok(float(op.getOperand(0u)) == 1.0f);
  ok(float(op.getOperand(1u)) == 2.0f);
}


void testIrOpOperandTypes() {
  Operand opi8(int8_t(-1));
  Operand opi16(int16_t(-1));
  Operand opi32(int32_t(-1));
  Operand opi64(int64_t(-1));

  Operand opu8(uint8_t(-1));

  ok(int8_t(opi8) == -1);
  ok(int8_t(opi16) == -1);
  ok(int8_t(opi32) == -1);
  ok(int8_t(opi64) == -1);

  ok(int16_t(opi8) == -1);
  ok(int16_t(opi16) == -1);
  ok(int16_t(opi32) == -1);
  ok(int16_t(opi64) == -1);

  ok(int32_t(opi8) == -1);
  ok(int32_t(opi16) == -1);
  ok(int32_t(opi32) == -1);
  ok(int32_t(opi64) == -1);

  ok(int64_t(opi8) == -1);
  ok(int64_t(opi16) == -1);
  ok(int64_t(opi32) == -1);
  ok(int64_t(opi64) == -1);

  ok(int8_t(opu8) == -1);
  ok(int16_t(opu8) == 255);
  ok(int32_t(opu8) == 255);
  ok(int64_t(opu8) == 255);

  ok(uint8_t(opi8) == 0xffu);
  ok(uint16_t(opi8) == 0xffffu);
  ok(uint32_t(opi8) == -1u);
  ok(uint64_t(opi8) == -1ull);
}


void testIrOp() {
  RUN_TEST(testIrOpDefault);
  RUN_TEST(testIrOpDebugName);
  RUN_TEST(testIrOpConstant);
  RUN_TEST(testIrOpOperandTypes);
}

}
