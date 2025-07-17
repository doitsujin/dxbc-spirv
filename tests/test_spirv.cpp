#include <fstream>
#include <iostream>

#include "./api/test_api.h"

#include "../spirv/spirv_builder.h"

namespace dxbc_spv::tests {

using namespace dxbc_spv::test_api;
using namespace dxbc_spv::spirv;

struct Options {
  std::string basePath;
  std::string testFilter;
  SpirvBuilder::Options spirv = { };
  bool overrideRef = false;
  bool generateNew = false;
};

std::vector<char> generateSpirv(const ir::Builder& builder, const SpirvBuilder::Options& options) {
  BasicResourceMapping mapping;

  SpirvBuilder spvBuilder(builder, mapping, options);
  spvBuilder.buildSpirvBinary();

  size_t size = 0u;
  spvBuilder.getSpirvBinary(size, nullptr);

  std::vector<char> data(size);
  spvBuilder.getSpirvBinary(size, data.data());

  return data;
}


bool runTest(const Options& options, const NamedTest& test) {
  const std::string refFile = options.basePath + "/" + test.name + ".ref.spv";
  const std::string newFile = options.basePath + "/" + test.name + ".new.spv";

  std::ifstream refStream;
  std::ofstream dstStream;

  /* Load reference binary from file if present */
  std::vector<char> refBinary;

  if (options.overrideRef) {
    dstStream = std::ofstream(refFile,
      std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);

    if (!dstStream.is_open()) {
      std::cerr << "Failed to open " << refFile << " for writing." << std::endl;
      return false;
    }
  } else {
    refStream = std::ifstream(refFile,
      std::ios_base::in | std::ios_base::binary);

    if (refStream.is_open()) {
      refStream.seekg(0, std::ios_base::end);
      refBinary.resize(refStream.tellg());
      refStream.seekg(0, std::ios_base::beg);
      refStream.read(refBinary.data(), refBinary.size());

      if (!refStream) {
        std::cerr << "Failed to read " << refFile << "." << std::endl;
        return false;
      }
    } else if (options.generateNew) {
      dstStream = std::ofstream(refFile,
        std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);

      if (!dstStream.is_open()) {
        std::cerr << "Failed to open " << refFile << " for writing." << std::endl;
        return false;
      }
    } else {
      std::cerr << "Failed to open " << refFile << " for reading." << std::endl;
      return true;
    }
  }

  /* Generate SPIR-V binary for lowering test */
  std::vector<char> newBinary = generateSpirv(test.builder, options.spirv);

  /* Compare to reference and write as necessary */
  bool writeDstFile = true;

  if (!refBinary.empty()) {
    bool isSame = false;

    if (newBinary.size() == refBinary.size())
      isSame = !std::memcmp(refBinary.data(), newBinary.data(), newBinary.size());

    if (isSame)
      writeDstFile = false;
    else
      std::cout << refFile << " has changed" << std::endl;
  }

  if (writeDstFile) {
    if (!dstStream.is_open()) {
      dstStream = std::ofstream(newFile,
        std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);

      if (!dstStream.is_open()) {
        std::cerr << "Failed to open " << newFile << " for writing." << std::endl;
        return false;
      }
    }

    dstStream.write(newBinary.data(), newBinary.size());
  }

  return true;
}


void runTests(const Options& options, const std::vector<NamedTest>& tests) {
  for (const auto& test : tests)
    runTest(options, test);
}


void run(const Options& options) {
  const char* filter = !options.testFilter.empty() ? options.testFilter.c_str() : nullptr;
  runTests(options, test_api::enumerateTests(filter));
  runTests(options, test_api::enumerateSpirvTests(filter));
}

}


void printHelp(const char* name) {
  std::cerr << "Usage: " << name << " file_path [--filter name] [--regen] [--gen-new] [--enable-nv-access-chains]" << std::endl;
}


int main(int argc, char** argv) {
  if (argc <= 1 || std::string(argv[1u]) == "--help") {
    printHelp(argv[0]);
    return 0;
  }

  dxbc_spv::tests::Options options;
  options.basePath = argv[1u];
  options.spirv.includeDebugNames = true;

  for (int i = 2u; i < argc; i++) {
    std::string arg = argv[i];

    if (arg == "--regen") {
      options.overrideRef = true;
    } else if (arg == "--gen-new") {
      options.generateNew = true;
    } else if (arg == "--enable-nv-raw-access-chains") {
      options.spirv.nvRawAccessChains = true;
    } else if (arg == "--filter" && i + 1 < argc) {
      options.testFilter = argv[++i];
    } else {
      std::cerr << "Unrecognized option: " << arg << std::endl;
      return 1;
    }
  }

  dxbc_spv::tests::run(options);
  return 0;
}
