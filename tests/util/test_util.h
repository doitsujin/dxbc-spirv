#pragma once

#include "../test_common.h"

namespace dxbc_spv::tests::util {

void testSmallVector();

void runTests() {
  RUN_TEST(testSmallVector);
}

}
