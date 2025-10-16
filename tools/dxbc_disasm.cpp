#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#include "../dxbc/dxbc_container.h"
#include "../dxbc/dxbc_disasm.h"
#include "../dxbc/dxbc_interface.h"
#include "../dxbc/dxbc_parser.h"
#include "../dxbc/dxbc_signature.h"

#include "../sm3/sm3_disasm.h"
#include "../sm3/sm3_parser.h"

using namespace dxbc_spv;

bool printSignature(util::ByteReader reader) {
  if (!reader)
    return true;

  dxbc::Signature sig(reader);

  if (!sig) {
    std::cerr << "Failed to parse signature" << std::endl;
    return false;
  }

  std::cout << sig << std::endl;
  return true;
}


bool printInterface(util::ByteReader reader) {
  if (!reader)
    return true;

  dxbc::InterfaceChunk iface(reader);

  if (!iface) {
    std::cerr << "Failed to parse input signature chunk" << std::endl;
    return false;
  }

  std::cout << iface << std::endl;
  return true;
}


bool printCode(util::ByteReader reader) {
  if (!reader) {
    std::cout << "(no code)" << std::endl;
    return true;
  }

  dxbc::Parser parser(reader);

  dxbc::Disassembler::Options options = { };
  options.lineNumbers = true;
  options.indent = true;

  dxbc::Disassembler disasm(options, parser.getShaderInfo());

  std::cout << parser.getShaderInfo() << ":" << std::endl;

  while (parser) {
    auto op = parser.parseInstruction();

    if (!op) {
      std::cerr << "Failed to parse instruction" << std::endl;
      return false;
    }

    disasm.disassembleOp(std::cout, op);
    std::cout << std::endl;
  }

  return true;
}


bool printCodeSM3(util::ByteReader reader, bool useDebugNames) {
  if (!reader) {
    std::cout << "(no code)" << std::endl;
    return true;
  }

  sm3::Parser parser(reader);

  sm3::Disassembler::Options options = { };
  options.lineNumbers = true;
  options.indent = true;

  sm3::ConstantTable ctab;

  sm3::Disassembler disasm(options, parser.getShaderInfo());

  std::cout << parser.getShaderInfo() << ":" << std::endl;

  while (parser) {
    auto op = parser.parseInstruction();
    if (useDebugNames && op.getOpCode() == sm3::OpCode::eComment && !ctab) {
      auto ctabReader = util::ByteReader(op.getCommentData(), op.getCommentDataSize());
      ctab = sm3::ConstantTable(ctabReader);
    }

    if (!op) {
      std::cerr << "Failed to parse instruction" << std::endl;
      return false;
    }

    disasm.disassembleOp(std::cout, op, ctab);
    std::cout << std::endl;
  }

  return true;
}


bool disassembleShader(util::ByteReader reader, bool useDebugNames) {
  if (!dxbc::Container::checkFourCC(reader)) {
    return printCodeSM3(reader, useDebugNames);
  }

  dxbc::Container container(reader);

  return printSignature(container.getInputSignatureChunk()) &&
         printSignature(container.getOutputSignatureChunk()) &&
         printSignature(container.getPatchConstantSignatureChunk()) &&
         printInterface(container.getInterfaceChunk()) &&
         printCode(container.getCodeChunk());
}


void printHelp(const char* appName) {
  std::cerr << "Usage: " << appName << " file.dxbc" << std::endl;
}


int main(int argc, char** argv) {
  if (argc < 2) {
    printHelp(argv[0u]);
    return 1;
  }

  bool useDebugNames = false;
  const char* fileName = nullptr;

  for (int32_t i = 1; i < argc; i++) {
    const char* arg = argv[i];

    if (strlen(arg) < 2u
      || arg[0u] != '-'
      || arg[1u] != '-') {
      fileName = arg;
      continue;
    }

    if (strcmp(arg, "--debug-names") == 0) {
      useDebugNames = true;
    } else {
      std::cerr << "Unknown option: " << arg << std::endl;
      return 1;
    }
  }

  if (fileName == nullptr) {
    std::cerr << "No file path provided" << std::endl;
    return 1;
  }

  std::ifstream file(fileName, std::ios_base::in | std::ios_base::binary);

  if (!file.is_open()) {
    std::cerr << "Failed to open file: " << fileName << std::endl;
    return 1;
  }

  file.seekg(0u, std::ios_base::end);
  std::vector<char> data(file.tellg());
  file.seekg(0u, std::ios_base::beg);
  file.read(data.data(), data.size());

  if (!disassembleShader(util::ByteReader(data.data(), data.size()), useDebugNames))
    return 1;

  return 0;
}
