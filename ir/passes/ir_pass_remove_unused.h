#pragma once

#include <iostream>

#include "../ir.h"
#include "../ir_builder.h"

#include "../../util/util_small_vector.h"

namespace dxbc_spv::ir {

/** Pass to remove unused instructions.
 *
 * This will thoroughly clean up any instructions whose result is unused
 * and that do not have any observable side effects. This includes unused
 * resource declarations, but not I/O variables.
 *
 * This pass will also remove any debug instructions associated instructions
 * that get removed. */
class RemoveUnusedPass {

public:

  RemoveUnusedPass(Builder& builder);

  ~RemoveUnusedPass();

  RemoveUnusedPass             (const RemoveUnusedPass&) = delete;
  RemoveUnusedPass& operator = (const RemoveUnusedPass&) = delete;

  /** Runs pass. */
  void run();

  /** Runs pass to remove unused floating point mode declarations. */
  void removeUnusedFloatModes();

  /** Initializes and runs pass on the given builder. */
  static void runPass(Builder& builder);

  /** Runs pass to remove unused floating point modes. */
  static void runRemoveUnusedFloatModePass(Builder& builder);

private:

  Builder& m_builder;

  bool canRemoveOp(const Op& op) const;

  void removeOp(SsaDef def);

  static bool hasSideEffect(OpCode opCode);

};

}
