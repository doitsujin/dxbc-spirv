#pragma once

#include "dxbc_container.h"
#include "dxbc_control_flow.h"
#include "dxbc_io_map.h"
#include "dxbc_parser.h"
#include "dxbc_registers.h"
#include "dxbc_resources.h"
#include "dxbc_signature.h"
#include "dxbc_types.h"

#include "../ir/ir.h"
#include "../ir/ir_builder.h"

#include "../util/util_log.h"

namespace dxbc_spv::dxbc {

/** Shader converter from DXBC to custom IR.
 *
 * The generated IR will contain temporaries rather than pure SSA form,
 * scoped control rather than structured control flow, min-precision or
 * unknown types, and instructions that cannot be lowered directly. As
 * such, the IR will require further processing. */
class Converter {
  friend IoMap;
  friend RegisterFile;
  friend ResourceMap;

  /* Mask to apply to shift and bit count operands */
  static constexpr uint32_t BitCountMask = 0x1f;
public:

  struct Options {
    /** Shader name. If non-null, this will be set as the entry point
     *  name, which is interpreted as the overall name of the shader. */
    const char* name = nullptr;
    /** Whether to emit any debg names besides the shader name. This
     *  includes resources, scratch and shared variables, as well as
     *  semantic names for I/O variables. */
    bool includeDebugNames = false;
    /** Whether to ensure that locations for patch constants and geometry
     *  streams are unique and do not conflict with control point locations
     *  or other streams, respectively. Required for SPIR-V lowering. */
    bool uniqueIoLocations = true;
    /** Maximum tessellation factor override. Shaders may specify a
     *  different value, but any higher values will be ignored. */
    float maxTessFactor = 0.0f;
  };

  Converter(Container container, const Options& options);

  ~Converter();

  Converter             (const Converter&) = delete;
  Converter& operator = (const Converter&) = delete;

  /** Creates internal IR from DXBC shader. If an error occurs, this function
   *  will return false and log messages to the thread-local logger. */
  bool convertShader(ir::Builder& builder);

private:

  Container     m_dxbc;
  Options       m_options;

  RegisterFile  m_regFile;
  IoMap         m_ioMap;
  ResourceMap   m_resources;
  ControlFlow   m_controlFlow;
  Parser        m_parser;

  uint32_t      m_instructionCount = 0u;

  /* Entry point definition and function definitions. The main function
   * will point to the control point function for hull shaders. */
  struct {
    ir::SsaDef def;

    ir::SsaDef mainFunc;
    ir::SsaDef patchConstantFunc;
  } m_entryPoint;


  /* Default float control mode, can be overwritten by dcl_global_flags.
   * The presence of fp16 and fp64 types is also initially determined by
   * global flags, subsequent passes may remove any unused mode setting
   * instructions again. */
  struct {
    ir::OpFlags defaultFlags = ir::OpFlag::ePrecise;

    bool hasFp16 = false;
    bool hasFp64 = false;
  } m_fpMode;


  /* Hull shader state */
  struct {
    HullShaderPhase phase = HullShaderPhase::eNone;
    uint32_t phaseIndex = 0u;

    float maxTessFactor = 64.0f;

    uint32_t controlPointsIn = 0u;
    uint32_t controlPointsOut = 0u;

    TessDomain       domain        = TessDomain::eUndefined;
    TessOutput       primitiveType = TessOutput::eUndefined;
    TessPartitioning partitioning  = TessPartitioning::eUndefined;

    ir::SsaDef phaseInstanceId = { };
    ir::SsaDef phaseFunction = { };

    bool hasControlPointPhase = false;

    ir::SsaDef patchConstantCursor = { };

    util::small_vector<std::pair<ir::SsaDef, uint32_t>, 8u> phaseInstanceCounts;
  } m_hs;


  /* Geometry shader state */
  struct {
    uint32_t instanceCount = 1u;

    PrimitiveType     inputPrimitive = PrimitiveType::eUndefined;
    PrimitiveTopology outputTopology = PrimitiveTopology::eUndefined;
    uint32_t          outputVertices = 0u;

    uint32_t streamIndex = 0u;
    uint32_t streamMask = 0u;
  } m_gs;


  /* Compute shader state */
  struct {
    uint32_t workgroupSizeX = 0u;
    uint32_t workgroupSizeY = 0u;
    uint32_t workgroupSizeZ = 0u;
  } m_cs;


  bool convertInstruction(ir::Builder& builder, const Instruction& op);

  bool initialize(ir::Builder& builder);

  bool finalize(ir::Builder& builder);

  void emitFloatModes(ir::Builder& builder);

  bool emitHsStateSetup(ir::Builder& builder);

  void emitHsPatchConstantFunction(ir::Builder& builder);

  bool emitGsStateSetup(ir::Builder& builder);

  bool handleDclGlobalFlags(ir::Builder& builder, const Instruction& op);

  bool handleHsPhase(ir::Builder& builder, const Instruction& op);

  bool handleHsPhaseInstanceCount(const Instruction& op);

  bool handleHsControlPointCount(const Instruction& op);

  bool handleHsMaxTessFactor(const Instruction& op);

  bool handleTessDomain(const Instruction& op);

  bool handleTessPartitioning(const Instruction& op);

  bool handleTessOutput(const Instruction& op);

  bool handleStream(const Instruction& op);

  bool handleGsInstanceCount(const Instruction& op);

  bool handleGsInputPrimitive(const Instruction& op);

  bool handleGsOutputPrimitive(const Instruction& op);

  bool handleGsOutputVertexCount(const Instruction& op);

  bool handleCsWorkgroupSize(ir::Builder& builder, const Instruction& op);

  bool handleMov(ir::Builder& builder, const Instruction& op);

  bool handleMovc(ir::Builder& builder, const Instruction& op);

  bool handleFloatArithmetic(ir::Builder& builder, const Instruction& op);

  bool handleFloatMad(ir::Builder& builder, const Instruction& op);

  bool handleFloatDot(ir::Builder& builder, const Instruction& op);

  bool handleFloatCompare(ir::Builder& builder, const Instruction& op);

  bool handleFloatConvert(ir::Builder& builder, const Instruction& op);

  bool handleIntArithmetic(ir::Builder& builder, const Instruction& op);

  bool handleIntMultiply(ir::Builder& builder, const Instruction& op);

  bool handleIntShift(ir::Builder& builder, const Instruction& op);

  bool handleIntCompare(ir::Builder& builder, const Instruction& op);

  bool handleBitInsert(ir::Builder& builder, const Instruction& op);

  bool handleLdStructured(ir::Builder& builder, const Instruction& op);

  bool handleStoreStructured(ir::Builder& builder, const Instruction& op);

  bool handleIf(ir::Builder& builder, const Instruction& op);

  bool handleElse(ir::Builder& builder, const Instruction& op);

  bool handleEndIf(ir::Builder& builder, const Instruction& op);

  bool handleLoop(ir::Builder& builder);

  bool handleEndLoop(ir::Builder& builder, const Instruction& op);

  bool handleSwitch(ir::Builder& builder, const Instruction& op);

  bool handleCase(ir::Builder& builder, const Instruction& op);

  bool handleDefault(ir::Builder& builder, const Instruction& op);

  bool handleEndSwitch(ir::Builder& builder, const Instruction& op);

  bool handleBreak(ir::Builder& builder, const Instruction& op);

  bool handleContinue(ir::Builder& builder, const Instruction& op);

  bool handleRet(ir::Builder& builder, const Instruction& op);

  bool handleGsEmitCut(ir::Builder& builder, const Instruction& op);

  bool handleSync(ir::Builder& builder, const Instruction& op);

  void applyNonUniform(ir::Builder& builder, ir::SsaDef def);

  ir::SsaDef applySrcModifiers(ir::Builder& builder, ir::SsaDef def, const Instruction& instruction, const Operand& operand);

  ir::SsaDef applyDstModifiers(ir::Builder& builder, ir::SsaDef def, const Instruction& instruction, const Operand& operand);

  ir::SsaDef loadImmediate(ir::Builder& builder, const Operand& operand, WriteMask mask, ir::ScalarType type);

  ir::SsaDef loadPhaseInstanceId(ir::Builder& builder, WriteMask mask, ir::ScalarType type);

  ir::SsaDef loadSrc(ir::Builder& builder, const Instruction& op, const Operand& operand, WriteMask mask, ir::ScalarType type);

  ir::SsaDef loadSrcModified(ir::Builder& builder, const Instruction& op, const Operand& operand, WriteMask mask, ir::ScalarType type);

  ir::SsaDef loadSrcConditional(ir::Builder& builder, const Instruction& op, const Operand& operand);

  ir::SsaDef loadSrcBitCount(ir::Builder& builder, const Instruction& op, const Operand& operand, WriteMask mask);

  ir::SsaDef loadOperandIndex(ir::Builder& builder, const Instruction& op, const Operand& operand, uint32_t dim);

  bool storeDst(ir::Builder& builder, const Instruction& op, const Operand& operand, ir::SsaDef value);

  bool storeDstModified(ir::Builder& builder, const Instruction& op, const Operand& operand, ir::SsaDef value);

  ir::SsaDef computeRawAddress(ir::Builder& builder, ir::SsaDef byteAddress, WriteMask componentMask);

  ir::SsaDef computeStructuredAddress(ir::Builder& builder, ir::SsaDef elementIndex, ir::SsaDef elementOffset, WriteMask componentMask);

  ir::SsaDef boolToInt(ir::Builder& builder, ir::SsaDef def);

  ir::SsaDef intToBool(ir::Builder& builder, ir::SsaDef def);

  ir::SsaDef broadcastScalar(ir::Builder& builder, ir::SsaDef def, WriteMask mask);

  ir::ScalarType determineOperandType(
    const Operand&                operand,
          ir::ScalarType          fallback          = ir::ScalarType::eUnknown,
          bool                    allowMinPrecision = true) const;

  ir::SsaDef composite(ir::Builder& builder, ir::BasicType type,
    const ir::SsaDef* components, Swizzle swizzle, WriteMask mask);

  ir::SsaDef buildVector(ir::Builder& builder, ir::ScalarType scalarType, size_t count, const ir::SsaDef* scalars);

  ir::SsaDef extractFromVector(ir::Builder& builder, ir::SsaDef def, uint32_t component);

  template<typename T>
  ir::SsaDef makeTypedConstant(ir::Builder& builder, ir::BasicType type, T value);

  std::string makeRegisterDebugName(RegisterType type, uint32_t index, WriteMask mask) const;

  bool isSm51() const;

  bool initParser(Parser& parser, util::ByteReader reader);

  ir::SsaDef getEntryPoint() const {
    return m_entryPoint.def;
  }

  void logOp(LogLevel severity, const Instruction& op) const;

  template<typename... Args>
  bool logOpMessage(LogLevel severity, const Instruction& op, const Args&... args) const {
    logOp(severity, op);
    Logger::log(severity, args...);
    return false;
  }

  template<typename... Args>
  bool logOpError(const Instruction& op, const Args&... args) const {
    return logOpMessage(LogLevel::eError, op, args...);
  }

  static ir::BasicType makeVectorType(ir::ScalarType type, WriteMask mask) {
    uint8_t shift = is64BitType(type) ? 1u : 0u;
    return ir::BasicType(type, util::popcnt(uint8_t(mask)) >> shift);
  }

  static WriteMask convertMaskTo32Bit(WriteMask mask);

  static WriteMask convertMaskTo64Bit(WriteMask mask);

  static bool isValid64BitMask(WriteMask mask);

  static bool isValidControlPointCount(uint32_t n);

  static bool isValidTessFactor(float f);

  static bool is64BitType(ir::BasicType type);

  static bool hasAbsNegModifiers(const Operand& operand);

};

}
