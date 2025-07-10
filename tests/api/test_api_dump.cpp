#include <iostream>
#include <sstream>

#include "test_api.h"

#include "../../ir/ir_disasm.h"
#include "../../ir/ir_validation.h"

int main(int argc, char** argv) {
  const char* filter = nullptr;

  if (argc > 1)
    filter = argv[1u];

  auto tests = dxbc_spv::test_api::enumerateTests(filter);

  for (const auto& test : tests) {
    std::cout << test.name << ":" << std::endl;

    dxbc_spv::ir::Disassembler::Options disassembleOptions;
    disassembleOptions.resolveConstants = true;
    disassembleOptions.showConstants = false;
    disassembleOptions.showDebugNames = false;
    disassembleOptions.coloredOutput = true;

    dxbc_spv::ir::Disassembler disasm(test.builder, disassembleOptions);
    disasm.disassemble(std::cout);
    std::cout << std::endl;

    dxbc_spv::ir::Validator validator(test.builder);
    std::stringstream validationResult;

    if (!validator.validateFinalIr(validationResult)) {
      std::cerr << "Validation FAILED:" << std::endl;
      std::cerr << validationResult.str() << std::endl;
      return 1;
    }
  }

  return 0;
}
