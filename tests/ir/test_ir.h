#pragma once

#include "../test_common.h"

namespace dxbc_spv::tests::ir {

void testIrBuilder();
void testIrOp();
void testIrType();

void runTests() {
  RUN_TEST(testIrType);
  RUN_TEST(testIrOp);
  RUN_TEST(testIrBuilder);
}

}
