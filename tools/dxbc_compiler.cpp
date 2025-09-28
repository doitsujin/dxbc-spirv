#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#include "../ir/ir.h"
#include "../ir/ir_builder.h"
#include "../ir/ir_disasm.h"
#include "../ir/ir_legalize.h"
#include "../ir/ir_serialize.h"

#include "../ir/passes/ir_pass_lower_io.h"

#include "../dxbc/dxbc_api.h"
#include "../dxbc/dxbc_container.h"
#include "../dxbc/dxbc_converter.h"
#include "../dxbc/dxbc_disasm.h"
#include "../dxbc/dxbc_parser.h"
#include "../dxbc/dxbc_signature.h"

#include "../spirv/spirv_builder.h"
#include "../spirv/spirv_mapping.h"

using namespace dxbc_spv;

struct Options {
  std::string input;
  std::string spvTarget;
  std::string irBinTarget;

  bool irInput = false;
  bool printIrAsm = false;
  bool convertOnly = false;
  bool gsPassthrough = false;
  bool noDebug = false;
  bool noColors = false;
  bool benchmark = false;
  bool ldsBarriers = false;
  bool uavBarriers = false;
  bool debugLog = false;
  bool compactIds = false;
};


class LocalLogger : public util::Logger {

public:

  LocalLogger(const Options& options)
  : m_fileName(options.input), m_debugLog(options.debugLog) { }

  ~LocalLogger() { }

  LogLevel getMinimumSeverity() override {
    return m_debugLog ? LogLevel::eDebug : LogLevel::eInfo;
  }

  void message(LogLevel severity, const char* message) override {
    std::cerr << severity << m_fileName << ": " << message << std::endl;
  }

private:

  std::string m_fileName;
  bool        m_debugLog = false;

};


struct Timers {
  std::chrono::high_resolution_clock::time_point tConvertBegin;
  std::chrono::high_resolution_clock::time_point tConvertEnd;
  std::chrono::high_resolution_clock::time_point tAfterPasses;
  std::chrono::high_resolution_clock::time_point tSerializeBegin;
  std::chrono::high_resolution_clock::time_point tSerializeEnd;
  std::chrono::high_resolution_clock::time_point tLowerSpirvBegin;
  std::chrono::high_resolution_clock::time_point tLowerSpirvEnd;
};

void printDuration(const char* type, std::chrono::high_resolution_clock::duration dur) {
  std::cout << type << ": " << std::setprecision(3) <<
    double(std::chrono::duration_cast<std::chrono::microseconds>(dur).count()) / 1000.0 << " ms" << std::endl;
}

void printTimers(const Timers& timers) {
  auto defaultTime = std::chrono::high_resolution_clock::time_point();

  auto totalDuration = timers.tConvertEnd - timers.tConvertBegin;
  printDuration("conversion", totalDuration);

  if (timers.tAfterPasses != defaultTime) {
    totalDuration = timers.tAfterPasses - timers.tConvertBegin;
    printDuration("passes", timers.tAfterPasses - timers.tConvertEnd);
  }

  if (timers.tSerializeBegin != defaultTime) {
    auto serializeDuration = timers.tSerializeEnd - timers.tSerializeBegin;
    printDuration("serialize", serializeDuration);
    totalDuration += serializeDuration;
  }

  if (timers.tLowerSpirvBegin != defaultTime) {
    auto spirvDuration = timers.tLowerSpirvEnd - timers.tLowerSpirvBegin;
    printDuration("spirv", spirvDuration);
    totalDuration += spirvDuration;
  }

  printDuration("total", totalDuration);
}


bool printIrAssembly(ir::Builder& builder, const Options& options) {
  if (!options.convertOnly)
    ir::SsaConstructionPass::runInsertExitPhiPass(builder);

  if (options.compactIds) {
    /* Do a serialization round-trip to clean up IDs */
    ir::Serializer serializer(builder);

    std::vector<uint8_t> serializedData(serializer.computeSerializedSize());
    serializer.serialize(serializedData.data(), serializedData.size());

    ir::Deserializer deserializer(serializedData.data(), serializedData.size());

    builder = ir::Builder();
    deserializer.deserialize(builder);
  }

  ir::Disassembler::Options disasmOptions;
  disasmOptions.resolveConstants = true;
  disasmOptions.showConstants = false;
  disasmOptions.coloredOutput = !options.noColors;
  disasmOptions.showDivergence = !options.convertOnly;

  ir::Disassembler disassembler(builder, disasmOptions);
  disassembler.disassemble(std::cout);

  return true;
}


bool writeIrBinary(const ir::Builder& builder, const Options& options, Timers& timers) {
  timers.tSerializeBegin = std::chrono::high_resolution_clock::now();

  ir::Serializer serializer(builder);

  std::vector<uint8_t> data(serializer.computeSerializedSize());

  if (!serializer.serialize(data.data(), data.size())) {
    std::cerr << "Error: Failed to serialize IR." << std::endl;
    return false;
  }

  timers.tSerializeEnd = std::chrono::high_resolution_clock::now();

  std::ofstream file(options.irBinTarget, std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);

  if (!file.is_open()) {
    std::cerr << "Error: Failed to open file " << options.irBinTarget << std::endl;
    return false;
  }

  file.write(reinterpret_cast<const char*>(data.data()), data.size());
  return bool(file);
}


bool writeSpirvBinary(ir::Builder builder, const Options& options, Timers& timers) {
  timers.tLowerSpirvBegin = std::chrono::high_resolution_clock::now();

  { ir::LowerIoPass pass(builder);

    pass.lowerSampleCountToSpecConstant(0u);
  }

  spirv::BasicResourceMapping mapping = { };

  spirv::SpirvBuilder::Options spirvOptions = { };
  spirvOptions.includeDebugNames = !options.noDebug;
  spirvOptions.floatControls2 = false;

  spirvOptions.supportedRoundModesF16 =
  spirvOptions.supportedRoundModesF32 =
  spirvOptions.supportedRoundModesF64 = ir::RoundMode::eNearestEven | ir::RoundMode::eZero;

  spirvOptions.supportedDenormModesF16 =
  spirvOptions.supportedDenormModesF32 =
  spirvOptions.supportedDenormModesF64 = ir::DenormMode::eFlush | ir::DenormMode::ePreserve;

  /* Generate actual SPIR-V binary */
  spirv::SpirvBuilder spirvBuilder(builder, mapping, spirvOptions);
  spirvBuilder.buildSpirvBinary();

  size_t size = 0u;
  spirvBuilder.getSpirvBinary(size, nullptr);

  std::vector<char> data(size);
  spirvBuilder.getSpirvBinary(size, data.data());

  timers.tLowerSpirvEnd = std::chrono::high_resolution_clock::now();

  /* Write SPIR-V binary to file */
  std::ofstream file(options.spvTarget, std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);

  if (!file.is_open()) {
    std::cerr << "Error: Failed to open file " << options.spvTarget << std::endl;
    return false;
  }

  file.write(reinterpret_cast<const char*>(data.data()), data.size());
  return bool(file);
}


bool compileShader(util::ByteReader reader, const Options& options) {
  Timers timers = { };
  timers.tConvertBegin = std::chrono::high_resolution_clock::now();

  ir::Builder builder;

  if (!options.irInput) {
    /* Parse file header */
    dxbc::Container container(reader);

    if (!container) {
      std::cerr << "Error: " << options.input << " is not a valid dxbc file." << std::endl;
      return false;
    }

    /* Work out shader name based on the file hash */
    auto name = [&] {
      std::stringstream stream;
      stream << container.getHash();
      return stream.str();
    } ();

    /* Set up conversion options */
    dxbc::Converter::Options dxbcOptions = { };
    dxbcOptions.includeDebugNames = !options.noDebug;
    dxbcOptions.name = name.c_str();

    dxbc::Converter converter(std::move(container), dxbcOptions);

    bool status = false;

    if (options.gsPassthrough)
      status = converter.createPassthroughGs(builder);
    else
      status = converter.convertShader(builder);

    if (!status) {
      std::cerr << "Error: Failed to convert shader." << std::endl;
      return false;
    }
  } else {
    ir::Deserializer deserializer(reinterpret_cast<const uint8_t*>(reader.getData(0u)), reader.getSize());

    if (!deserializer.deserialize(builder)) {
      std::cerr << "Error: Failed to deserialize shader." << std::endl;
      return false;
    }
  }

  timers.tConvertEnd = std::chrono::high_resolution_clock::now();

  if (!(options.convertOnly || options.irInput)) {
    ir::CompileOptions compileOptions = { };

    compileOptions.syncOptions.insertLdsBarriers = options.ldsBarriers;
    compileOptions.syncOptions.insertUavBarriers = options.uavBarriers;

    compileOptions.cseOptions.relocateDescriptorLoad = true;

    compileOptions.descriptorIndexing.optimizeDescriptorIndexing = true;

    ir::legalizeIr(builder, compileOptions);
    timers.tAfterPasses = std::chrono::high_resolution_clock::now();
  }

  /* Output results */
  if (options.printIrAsm)
    printIrAssembly(builder, options);

  if (!options.irBinTarget.empty())
    writeIrBinary(builder, options, timers);

  if (!options.spvTarget.empty())
    writeSpirvBinary(builder, options, timers);

  if (options.benchmark)
    printTimers(timers);

  return true;
}


void printHelp(const char* appName) {
  std::cerr << "Usage: " << appName << " [options] input.dxbc" << std::endl
            << std::endl
            << "Options:" << std::endl
            << "    --spv file          Emit SPIR-V to given binary file. If not specified," << std::endl
            << "                        no SPIR-V lowering will be performed." << std::endl
            << "    --ir file           Emit internal IR as a serialized binary to tge given file." << std::endl
            << "    --ir-asm            Emit disassembled IR to standard output." << std::endl
            << std::endl
            << "    --convert-only      Only perform initial conversion to the initial IR and skip" << std::endl
            << "                        any further processing steps. Cannot be used with SPIR-V lowering." << std::endl
            << "    --gs-passthrough    Create a pass-through geometry shader instead of compiling code." << std::endl
            << "    --no-debug          Do not emit any debug info in the shader binary." << std::endl
            << "    --no-colors         Do not use colored output for disassembly." << std::endl
            << "    --benchmark         Time shader compilation." << std::endl;
}


int main(int argc, char** argv) {
  Options options;

  for (int i = 1u; i < argc; i++) {
    std::string arg = argv[i];

    if (arg == "--help") {
      printHelp(argv[0]);
      return 0;
    } else if (arg == "--spv" && i + 1 < argc) {
      options.spvTarget = argv[++i];
    } else if (arg == "--ir" && i + 1 < argc) {
      options.irBinTarget = argv[++i];
    } else if (arg == "--ir-input" && i + 1 < argc) {
      options.irInput = true;
    } else if (arg == "--ir-asm") {
      options.printIrAsm = true;
    } else if (arg == "--convert-only") {
      options.convertOnly = true;
    } else if (arg == "--gs-passthrough") {
      options.gsPassthrough = true;
    } else if (arg == "--no-debug") {
      options.noDebug = true;
    } else if (arg == "--no-colors") {
      options.noColors = true;
    } else if (arg == "--benchmark") {
      options.benchmark = true;
    } else if (arg == "--lds-barriers") {
      options.ldsBarriers = true;
    } else if (arg == "--uav-barriers") {
      options.uavBarriers = true;
    } else if (arg == "--compact-ids") {
      options.compactIds = true;
    } else if (arg == "--debug") {
      options.debugLog = true;
    } else {
      if (arg.size() >= 2u && arg[0] == '-' && arg[1] == '-') {
        std::cerr << "Invalid option: " << arg << std::endl;
        return 1;
      }

      options.input = std::move(arg);
    }
  }

  if (options.input.empty()) {
    std::cerr << "Error: No input file specified." << std::endl;
    return 1;
  }

  if (options.convertOnly && !options.spvTarget.empty()) {
    std::cerr << "Error: --convert-only and --spv cannot be set together." << std::endl;
    return 1;
  }

  /* Read input file to memory and hand it off */
  std::ifstream file(options.input, std::ios_base::in | std::ios_base::binary);

  if (!file.is_open()) {
    std::cerr << "Error: Failed to open input file: " << options.input << std::endl;
    return 1;
  }

  LocalLogger logger(options);

  file.seekg(0u, std::ios_base::end);
  std::vector<char> data(file.tellg());
  file.seekg(0u, std::ios_base::beg);
  file.read(data.data(), data.size());

  if (!compileShader(util::ByteReader(data.data(), data.size()), options))
    return 1;

  return 0;
}
