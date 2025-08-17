#pragma once

#include "../test_common.h"

namespace dxbc_spv::tests::ir {

void testIrBuilder();
void testIrInputMap();
void testIrOp();
void testIrSerialize();
void testIrType();
void testIrTypePropagation();

void runTests() {
  RUN_TEST(testIrType);
  RUN_TEST(testIrOp);
  RUN_TEST(testIrBuilder);
  RUN_TEST(testIrSerialize);
  RUN_TEST(testIrTypePropagation);
  RUN_TEST(testIrInputMap);
}

}
