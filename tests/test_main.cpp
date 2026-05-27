#include <iostream>

#ifdef DXBC_SPV_ENABLE_SM5
#include "./dxbc/test_dxbc.h"
#endif

#include "./ir/test_ir.h"

#include "./util/test_util.h"

namespace dxbc_spv::tests {

TestState g_testState;

void runTests() {
  util::runTests();
  ir::runTests();

#ifdef DXBC_SPV_ENABLE_SM5
  dxbc::runTests();
#endif

  std::cerr << "Tests run: " << g_testState.testsRun
    << ", failed: " << g_testState.testsFailed << std::endl;
}

}

int main(int, char**) {
  dxbc_spv::tests::runTests();
  return 0u;
}
