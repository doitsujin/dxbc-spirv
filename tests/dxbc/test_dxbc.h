#pragma once

#include "../test_common.h"

namespace dxbc_spv::tests::dxbc {

void testDxbcTypeToIrType();
void testDxbcTypeFromIrType();
void testDxbcSwizzle();

void runTests() {
  RUN_TEST(testDxbcTypeToIrType);
  RUN_TEST(testDxbcTypeFromIrType);
  RUN_TEST(testDxbcSwizzle);
}

}
