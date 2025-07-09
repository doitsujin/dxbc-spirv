#pragma once

#include <string>
#include <sstream>

#include "ir_builder.h"
#include "ir_disasm.h"

namespace dxbc_spv::ir {

/** IR validator. Provides various passes that can be executed
 *  individually to verify correctness of shader code. */
class Validator {

public:

  Validator(const Builder& builder);

  ~Validator();

  /** Validates basic module structure:
   * - Checks that declarative instructions occur first in the module.
   * - Checks that all referenced SSA defs are defined. */
  bool validateStructure(std::ostream& str) const;

  /** Validates shader I/O declarations. */
  bool validateShaderIo(std::ostream& str) const;

  /** Validates resource declarations. */
  bool validateResources(std::ostream& str) const;

  /** Validates load/store ops:
   * - Checks that the address vector is valid.
   * - Checks that return / value types are valid for the given declaration. */
  bool validateLoadStoreOps(std::ostream& str) const;

  /** Validates image ops:
   * - Checks whether parameter combinations make sense.
   * - Checks return and value types match the resource declaration.
   * - Chekcs that coordinate vector sizes are valid. */
  bool validateImageOps(std::ostream& str) const;

  /** Validates structured control flow instructions. */
  bool validateStructuredCfg(std::ostream& str) const;

  /** Executes all validation passes that are useful on the final
   *  shader module right before lowering to SPIR-V or another IR. */
  bool validateFinalIr(std::ostream& str) const;

private:

  const Builder& m_builder;

  Disassembler m_disasm;

  PrimitiveType findGsInputPrimitive(SsaDef entryPoint) const;

};

}
