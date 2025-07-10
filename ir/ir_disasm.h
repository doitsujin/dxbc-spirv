#pragma once

#include <string>
#include <sstream>
#include <unordered_map>

#include "ir_builder.h"

#include "../util/util_console.h"

namespace dxbc_spv::ir {

/** Disassembler pass. Useful for debugging purposes.
 *  Assumes that the given shader is valid. */
class Disassembler {

public:

  struct Options {
    /** Whether to resolve debug names in references.
     *  Otherwise, displays raw SSA IDs only. */
    bool useDebugNames = true;
    /** Whether to resolve enum names where appropriate.
     *  Otherwise, displays raw SSA IDs only */
    bool useEnumNames = true;
    /** Whether to resolve constant references. */
    bool resolveConstants = true;
    /** Whether to enable colored output */
    bool coloredOutput = false;
  };

  Disassembler(const Builder& builder, const Options& options);

  ~Disassembler();

  /** Disassembles shader module into the given stream. */
  void disassemble(std::ostream& stream) const;

  /** Disassembles single instruction into the given stream. */
  void disassembleOp(std::ostream& stream, const Op& op) const;

  /** Disassembles shader module into a string. */
  std::string disassemble() const;

  /** Disassembles single instruction into a string. */
  std::string disassembleOp(const Op& op) const;

private:

  const Builder& m_builder;
  Options m_options;

  std::unordered_map<SsaDef, std::string> m_debugNames;

  void resolveDebugNames();

  void disassembleDef(std::ostream& stream, SsaDef def) const;

  void disassembleOperandDef(std::ostream& stream, const Op& op, uint32_t index) const;

  void disassembleOperandLiteral(std::ostream& stream, const Op& op, uint32_t index) const;

  util::ConsoleState scopedColor(std::ostream& stream, uint32_t fg, uint32_t effect = 0u) const;

  static size_t countChars(const std::string& str);

};

}
