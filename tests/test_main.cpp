#include <iostream>

#include "./dxbc/test_dxbc.h"

#include "./ir/test_ir.h"

#include "./util/test_util.h"

namespace dxbc_spv::tests {

TestState g_testState;

void runTests() {
  util::runTests();
  ir::runTests();
  dxbc::runTests();

  std::cerr << "Tests run: " << g_testState.testsRun
    << ", failed: " << g_testState.testsFailed << std::endl;
}

}

int main(int, char**) {
  dxbc_spv::tests::runTests();
  return 0u;
}
