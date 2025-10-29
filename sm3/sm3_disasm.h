#pragma once

#include "sm3_parser.h"

namespace dxbc_spv::sm3 {

/** Shader code disassembler. */
class Disassembler {

public:

  struct Options {
    /** Whether to indent control flow blocks */
    bool indent = false;
    /** Whether to print line numbers before each instruction.
     *  Unlike reference d3dcompiler, this makes no distinction
     *  between declarations and regular instructions. */
    bool lineNumbers = false;
  };

  explicit Disassembler(const Options& options, ShaderInfo info)
  : m_options(options), m_info(info) { }

  /** Disassemble instruction to the given stream. */
  void disassembleOp(std::ostream& stream, const Instruction& op, const ConstantTable& ctab);

  /** Disassemble instruction into a string. */
  std::string disassembleOp(const Instruction& op, const ConstantTable& ctab);

private:

  Options m_options = { };
  ShaderInfo m_info = { };

  uint32_t    m_lineNumber = 0u;
  uint32_t    m_indentDepth = 0u;

  void disassembleOpcodeToken(std::ostream& stream, const Instruction& op) const;

  void disassembleDeclaration(std::ostream& stream, const Instruction& op, const Operand& operand) const;

  void disassembleImmediate(std::ostream& stream, const Instruction& op, const Operand& arg) const;

  void disassembleOperand(std::ostream& stream, const Instruction& op, const Operand& arg, const ConstantTable& ctab) const;

  void disassembleRegisterAddressing(std::ostream& stream, const Operand& arg, const ConstantTable& ctab) const;

  void disassembleSwizzleWriteMask(std::ostream& stream, const Instruction& op, const Operand& arg) const;

  void emitLineNumber(std::ostream& stream);

  void emitIndentation(std::ostream& stream) const;

  void incrementIndentation();

  void decrementIndentation();

  static bool opBeginsNestedBlock(const Instruction& op);

  static bool opEndsNestedBlock(const Instruction& op);

};

}
