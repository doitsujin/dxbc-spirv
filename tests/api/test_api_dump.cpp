#include <iostream>

#include "test_api.h"

#include "../../ir/ir_disasm.h"

int main(int argc, char** argv) {
  const char* filter = nullptr;

  if (argc > 1)
    filter = argv[1u];

  auto tests = dxbc_spv::test_api::enumerateTests(filter);

  for (const auto& test : tests) {
    std::cout << test.name << ":" << std::endl;

    dxbc_spv::ir::Disassembler disasm(test.builder, dxbc_spv::ir::Disassembler::Options());
    disasm.disassemble(std::cout);

    std::cout << std::endl;
  }
}
