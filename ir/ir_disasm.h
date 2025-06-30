#pragma once

#include <string>
#include <sstream>
#include <unordered_map>

#include "ir_builder.h"

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
  };

  Disassembler(const Builder& builder, const Options& options)
  : m_builder(builder), m_options(options) { }

  /** Returns disassembled shader as an UTF-8 string. */
  void disassemble();

  /** Retrieves disassembled string. */
  std::string getString() const {
    return m_str.str();
  }

private:

  const Builder&    m_builder;
  Options           m_options;

  std::stringstream m_str;

  std::unordered_map<SsaDef, std::string> m_debugNames;

  void disassembleInstruction(const Op& op);

  void disassembleBinaryData();

  void resolveDebugNames();

  void disassembleDef(SsaDef def);

  void disassembleOperandDef(const Op& op, uint32_t index);

  void disassembleOperandLiteral(const Op& op, uint32_t index);

};

}
