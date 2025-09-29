#pragma once

#include <iostream>

#include "../ir.h"
#include "../ir_builder.h"

#include "../../util/util_small_vector.h"

namespace dxbc_spv::ir {

/** Pass to remove unreachable blocks and do small cfg transforms.
 *
 * Specifically, this pass will:
 * - Remove all unreachable functions.
 * - Remove all unreachable blocks, except for continue blocks of
 *   loops that themseves can be reached.
 * - Replace conditional branches with a constant conditions with
 *   an unconditional branch, which allows removing more code. */
class CleanupControlFlowPass {

public:

  CleanupControlFlowPass(Builder& builder);

  ~CleanupControlFlowPass();

  CleanupControlFlowPass             (const CleanupControlFlowPass&) = delete;
  CleanupControlFlowPass& operator = (const CleanupControlFlowPass&) = delete;

  /** Runs full clean-up pass on the provided builder.
   *
   * If the incoming IR was produced by the control flow conversion
   * pass, the resulting IR from this pass will be legal. */
  bool run();

  /** Scans the given list of functions and recursively removes any
   *  functions that are unused. This does not otherwise touch control
   *  flow, and may be run on code containing scoped control flow. */
  void removeUnusedFunctions(uint32_t candidateCount, const SsaDef* candidates);

  /** Checks whether the given conditional branch has a constant
   *  condition and removes the not-taken part of the branch. */
  void resolveConditionalBranch(SsaDef branch);

  /** Initializes and runs pass on the given builder. */
  static bool runPass(Builder& builder);

private:

  Builder& m_builder;

  /* List of unused functions and blocks */
  util::small_vector<SsaDef,  16u> m_unusedFunctions;
  util::small_vector<SsaDef, 256u> m_unusedBlocks;

  std::pair<bool, Builder::iterator> handleFunction(Builder::iterator op);

  std::pair<bool, Builder::iterator> handleLabel(Builder::iterator op);

  std::pair<bool, Builder::iterator> handleBranchConditional(Builder::iterator op);

  std::pair<bool, Builder::iterator> handleSwitch(Builder::iterator op);

  bool removeUnusedFunctions();

  SsaDef removeFunction(SsaDef function);

  SsaDef removeFunctionCall(SsaDef call);

  bool isFunctionUsed(SsaDef function) const;

  bool removeUnusedBlocks();

  void removeBlockFromUnusedList(SsaDef block);

  SsaDef removeBlock(SsaDef block);

  SsaDef removeBlockTerminator(SsaDef block);

  bool isMergeBlock(SsaDef block) const;

  bool isContinueBlock(SsaDef block) const;

  bool isBlockReachable(SsaDef block) const;

  bool isBlockUsed(SsaDef block) const;

  bool isBlockUsedInPhi(SsaDef def) const;

  Construct getConstructForBlock(SsaDef block) const;

};

}
