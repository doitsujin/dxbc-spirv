#pragma once

#include <utility>

#include "../ir.h"
#include "../ir_builder.h"
#include "../ir_utils.h"

namespace dxbc_spv::ir {

/** Pass to resolve ambiguous types (i.e. Unknown and AnyI*).
 *
 * Unknown is treated as an arbitrary 32-bit type, and can generally only be
 * encountered in bit-pattern preserving instructions such as *Load, *Store,
 * Phi and Select, as well as the corresponding declarations. A two-component
 * vector of Unknown may also represent a 64-bit data type.
 *
 * In some cases, e.g. when used as scratch or shared memory, Unknown may also
 * be resolved to a lower precision type if the data is only used as such.
 *
 * If an unknown type cannot be resolved at all, e.g. because the shader only
 * uses it to move data around in memory, it will be mapped to u32. Similarly,
 * if Any* types cannot be resolved, they will be mapped to unsigned variants.
 *
 * Structured resources, such as LDS or untyped buffers, may be mapped to
 * struct types if the respective access patterns provide sufficient type
 * information for each field based on their offset.
 */
class PropagateTypesPass {

public:

  PropagateTypesPass(Builder& builder);

  ~PropagateTypesPass();

  PropagateTypesPass             (const PropagateTypesPass&) = delete;
  PropagateTypesPass& operator = (const PropagateTypesPass&) = delete;

  /** Runs type propagation pass. Assumes that consume chains have been
   *  eliminated already, i.e. there are no ConsumeAs instructions that
   *  have another ConsumeAs as an operand. */
  void run();

  /** Initializes and runs pass on the given builder. */
  static void runPass(Builder& builder);

  /** Helper function to determine the type for an instruction. */
  static BasicType resolveTypeForUnknownOp(BasicType opType, BasicType operandType);

private:

  Builder& m_builder;

  std::vector<SsaDef> m_opsToResolve;

  std::pair<bool, BasicType> resolveUnknownPhiSelect(const Op& op);

  SsaDef rewriteResolvedOp(const Op& op, BasicType type);

  SsaDef consumeAs(SsaDef def, BasicType type);

  BasicType inferOpType(SsaDef def);

  static ScalarType makeIntTypeSigned(ScalarType t);

  static BasicType makeIntTypeSigned(BasicType t);

};

}
