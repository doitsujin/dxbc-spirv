#pragma once

#include "dxbc_container.h"
#include "dxbc_parser.h"
#include "dxbc_signature.h"
#include "dxbc_types.h"

#include "../ir/ir.h"
#include "../ir/ir_builder.h"

namespace dxbc_spv::dxbc {

/** Shader converter from DXBC to custom IR.
 *
 * The generated IR will contain temporaries rather than pure SSA form,
 * scoped control rather than structured control flow, min-precision or
 * unknown types, and instructions that cannot be lowered directly. As
 * such, the IR will require further processing. */
class Converter {

public:

  struct Options {
    /** Shader name. If non-null, this will be set as the entry point
     *  name, which is interpreted as the overall name of the shader. */
    const char* name = nullptr;
    /** Whether to emit any debg names besides the shader name. This
     *  includes resources, scratch and shared variables, as well as
     *  semantic names for I/O variables. */
    bool includeDebugNames = false;
  };

  Converter(Container container, const Options& options);

  ~Converter();

  Converter             (const Converter&) = delete;
  Converter& operator = (const Converter&) = delete;

  /** Creates internal IR from DXBC shader. If an error occurs, this function
   *  will return false and log messages to the thread-local logger. */
  bool convertShader(ir::Builder& builder);

private:

  Container m_dxbc;
  Options   m_options;

  Signature m_isgn = { };
  Signature m_osgn = { };
  Signature m_psgn = { };

  Parser m_parser;

  bool convertInstruction(ir::Builder& builder, const Instruction& op);

  bool initSignature(Signature& sig, util::ByteReader reader);

  bool initParser(Parser& parser, util::ByteReader reader);

};

}
