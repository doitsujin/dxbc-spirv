#pragma once

#include "sm3_parser.h"
#include "sm3_io_map.h"
#include "sm3_registers.h"
#include "sm3_spec_constants.h"
#include "sm3_resources.h"

#include "../ir/ir_builder.h"

#include "../util/util_byte_stream.h"
#include "../util/util_log.h"

namespace dxbc_spv::sm3 {

/** Shader converter from SM3 DXBC to custom IR.
 *
 * The generated IR will contain temporaries rather than pure SSA form,
 * scoped control rather than structured control flow, min-precision or
 * unknown types, and instructions that cannot be lowered directly. As
 * such, the IR will require further processing. */
class Converter {

  friend IoMap;
  friend RegisterFile;
  friend ResourceMap;
  friend SpecializationConstantsMap;

public:

  struct Options {
    /** Shader name. If non-null, this will be set as the entry point
     *  name, which is interpreted as the overall name of the shader. */
    const char* name = nullptr;
    /** Whether to emit any debug names besides the shader name. This
     *  includes resources, scratch and shared variables, as well as
     *  semantic names for I/O variables. */
    bool includeDebugNames = false;

    /** Whether the shader uses the software vertex processing
     * limits. Only applies to vertex shaders. */
    bool isSWVP = false;

    /** Whether D3D9 fmulz floats are emulated by strategically clamping in the right spots. */
    bool fastFloatEmulation = false;
  };

  Converter(util::ByteReader code, SpecializationConstantLayout& specConstantsLayout, const Options& options);

  ~Converter();

  /** Creates internal IR from SM3 DXBC shader. If an error occurs, this function
   *  will return false and log messages to the thread-local logger. */
  bool convertShader(ir::Builder& builder);

private:

  util::ByteReader m_code;
  Options          m_options;

  ConstantTable    m_ctab = { };

  Parser           m_parser;

  IoMap            m_ioMap;
  RegisterFile     m_regFile;
  ResourceMap      m_resources;

  SpecializationConstantsMap m_specConstants;

  ir::SsaDef       m_psSharedData;

  uint32_t m_instructionCount = 0u;

  /* Entry point definition and function definitions. */
  struct {
    ir::SsaDef def;

    ir::SsaDef mainFunc;
  } m_entryPoint;

  bool convertInstruction(ir::Builder& builder, const Instruction& op);

  bool initialize(ir::Builder& builder, ShaderType shaderType);

  bool finalize(ir::Builder& builder, ShaderType shaderType);

  bool initParser(Parser& parser, util::ByteReader reader);

  ir::SsaDef getEntryPoint() const {
    return m_entryPoint.def;
  }

  ShaderInfo getShaderInfo() const {
    return m_parser.getShaderInfo();
  }

  const Options& getOptions() const {
    return m_options;
  }

  ir::Op OpFMul(ir::Type type, ir::SsaDef a, ir::SsaDef b) const {
    return !m_options.fastFloatEmulation
      ? ir::Op::FMulLegacy(type, a, b)
      : ir::Op::FMul(type, a, b);
  }
  ir::Op OpFMad(ir::Type type, ir::SsaDef a, ir::SsaDef b, ir::SsaDef c) const {
    return !m_options.fastFloatEmulation
      ? ir::Op::FMadLegacy(type, a, b, c)
      : ir::Op::FMad(type, a, b, c);
  }
  ir::Op OpFDot(ir::Type type, ir::SsaDef a, ir::SsaDef b) const {
    return !m_options.fastFloatEmulation
      ? ir::Op::FDotLegacy(type, a, b)
      : ir::Op::FDot(type, a, b);
  }
  ir::Op OpFPow(ir::Type type, ir::SsaDef base, ir::SsaDef exp) const {
    return !m_options.fastFloatEmulation
      ? ir::Op::FPowLegacy(type, base, exp)
      : ir::Op::FPow(type, base, exp);
  }

  ir::SsaDef emitSharedConstants(ir::Builder& builder);

  ir::SsaDef applyBumpMapping(ir::Builder& builder, uint32_t stageIdx, ir::SsaDef src0, ir::SsaDef src1);

  ir::SsaDef normalizeVector(ir::Builder& builder, ir::SsaDef def);

  bool handleComment(ir::Builder& builder, const Instruction& op);

  bool handleDef(ir::Builder& builder, const Instruction& op);

  bool handleDcl(ir::Builder& builder, const Instruction& op);

  bool handleMov(ir::Builder& builder, const Instruction& op);

  bool handleArithmetic(ir::Builder& builder, const Instruction& op);

  bool handleDot(ir::Builder& builder, const Instruction& op);

  bool handleCompare(ir::Builder& builder, const Instruction& op);

  bool handleLit(ir::Builder& builder, const Instruction& op);

  bool handleMatrixArithmetic(ir::Builder& builder, const Instruction& op);

  bool handleBem(ir::Builder& builder, const Instruction& op);

  bool handleTexCoord(ir::Builder& builder, const Instruction& op);

  bool handleTextureSample(ir::Builder& builder, const Instruction& op);

  bool handleTexKill(ir::Builder& builder, const Instruction& op);

  bool handleTexDepth(ir::Builder& builder, const Instruction& op);

  bool handleSelect(ir::Builder& builder, const Instruction& op);

  bool handleNrm(ir::Builder& builder, const Instruction& op);

  bool handleSinCos(ir::Builder& builder, const Instruction& op);

  bool handlePow(ir::Builder& builder, const Instruction& op);

  bool handleDst(ir::Builder& builder, const Instruction& op);

  ir::SsaDef loadSrc(ir::Builder& builder, const Instruction& op, const Operand& operand, WriteMask mask, Swizzle swizzle, ir::ScalarType type);

  ir::SsaDef applySrcModifiers(ir::Builder& builder, ir::SsaDef def, const Instruction& instruction, const Operand& operand, WriteMask mask);

  ir::SsaDef loadSrcModified(ir::Builder& builder, const Instruction& op, const Operand& operand, WriteMask mask, ir::ScalarType type);

  bool storeDst(ir::Builder& builder, const Instruction& op, const Operand& operand, ir::SsaDef predicateVec, ir::SsaDef value);

  ir::SsaDef applyDstModifiers(ir::Builder& builder, ir::SsaDef def, const Instruction& instruction, const Operand& operand);

  bool storeDstModifiedPredicated(ir::Builder& builder, const Instruction& op, const Operand& operand, ir::SsaDef value);

  WriteMask fixupWriteMask(ir::Builder& builder, WriteMask writeMask, ir::SsaDef value);

  ir::SsaDef loadAddress(ir::Builder& builder, RegisterType registerType, Swizzle swizzle);

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

  std::string makeRegisterDebugName(RegisterType type, uint32_t index, WriteMask mask) const;

};

}
