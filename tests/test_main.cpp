#include "./util/test_util.h"

namespace dxbc_spv::tests {

TestState g_testState;

void runTests() {
  util::runTests();

  std::cerr << "Tests run: " << g_testState.testsRun
    << ", failed: " << g_testState.testsFailed << std::endl;
}

}

int main(int, char**) {
  dxbc_spv::tests::runTests();
  return 0u;
}
