#include "ir_pass_cfg_cleanup.h"

#include "../ir_utils.h"

namespace dxbc_spv::ir {

CleanupControlFlowPass::CleanupControlFlowPass(Builder& builder)
: m_builder(builder) {

}


CleanupControlFlowPass::~CleanupControlFlowPass() {

}


bool CleanupControlFlowPass::run() {
  auto iter = m_builder.getCode().first;

  bool progress = false;

  while (iter != m_builder.end()) {
    switch (iter->getOpCode()) {
      case OpCode::eFunction: {
        auto [status, next] = handleFunction(iter);
        progress |= status;
        iter = next;
      } break;

      case OpCode::eLabel: {
        auto [status, next] = handleLabel(iter);
        progress |= status;
        iter = next;
      } break;

      case OpCode::eBranchConditional: {
        auto [status, next] = handleBranchConditional(iter);
        progress |= status;
        iter = next;
      } break;

      case OpCode::eSwitch: {
        auto [status, next] = handleSwitch(iter);
        progress |= status;
        iter = next;
      } break;

      default:
        ++iter;
    }
  }

  /* Remove any blocks that have been additionally marked as unreachable
   * during processing. This may also cause more functions to go unused,
   * so do this first. */
  progress |= removeUnusedBlocks();

  /* Remove any functions that may have been removed */
  progress |= removeUnusedFunctions();
  return progress;
}


std::pair<bool, Builder::iterator> CleanupControlFlowPass::handleSwitch(Builder::iterator op) {
  const auto& construct = m_builder.getOp(findContainingBlock(m_builder, op->getDef()));
  dxbc_spv_assert(construct && Construct(construct.getOperand(construct.getFirstLiteralOperandIndex())) == Construct::eStructuredSelection);

  const auto& mergeBlock = m_builder.getOpForOperand(construct, 0u);

  /* Eliminate cases that only branch to another case and are not used in any
   * phi operands, and deduplicate cases that branch to the merge block.*/
  SsaDef mergeCase = { };

  for (uint32_t i = 1u; i < op->getOperandCount(); i += 2u) {
    const auto& caseBlock = m_builder.getOpForOperand(*op, i);
    dxbc_spv_assert(caseBlock.getOpCode() == OpCode::eLabel);

    if (isBlockUsedInPhi(caseBlock.getDef()) || caseBlock.getDef() == mergeBlock.getDef())
      continue;

    const auto& branch = m_builder.getOp(m_builder.getNext(caseBlock.getDef()));

    if (branch.getOpCode() != OpCode::eBranch)
      continue;

    const auto& target = m_builder.getOpForOperand(branch, 0u);

    if (target.getDef() == mergeBlock.getDef()) {
      if (!mergeCase) {
        mergeCase = caseBlock.getDef();
      } else if (mergeCase != caseBlock.getDef()) {
        m_builder.removeOp(branch);
        m_builder.rewriteDef(caseBlock.getDef(), mergeCase);
        return std::make_pair(true, op);
      }
    } else {
      for (uint32_t j = i + 2u; j < op->getOperandCount(); j += 2u) {
        const auto& nextCase = m_builder.getOpForOperand(*op, j);
        dxbc_spv_assert(nextCase.getOpCode() == OpCode::eLabel);

        if (target.getDef() == nextCase.getDef()) {
          m_builder.removeOp(branch);
          m_builder.rewriteDef(caseBlock.getDef(), nextCase.getDef());
          return std::make_pair(true, op);
        }
      }
    }
  }

  /* Eliminate case labels that point to the default block */
  auto defaultCase = m_builder.getOpForOperand(*op, 1u).getDef();

  Op switchOp(OpCode::eSwitch, Type());
  switchOp.setFlags(op->getFlags());
  switchOp.addOperands(SsaDef(op->getOperand(0u)));
  switchOp.addOperands(defaultCase);

  for (uint32_t i = 2u; i < op->getOperandCount(); i += 2u) {
    const auto& caseValue = m_builder.getOpForOperand(*op, i);
    const auto& caseBlock = m_builder.getOpForOperand(*op, i + 1u);

    if (caseBlock.getDef() != defaultCase) {
      switchOp.addOperand(caseValue.getDef());
      switchOp.addOperand(caseBlock.getDef());
    }
  }

  if (!switchOp.isEquivalent(*op)) {
    /* If the new switch block doesn't have any non-default blocks,
     * eliminate it and branch to the default block directly */
    bool hasNonDefaultLabel = switchOp.getOperandCount() > 2u;

    if (hasNonDefaultLabel) {
      m_builder.rewriteOp(op->getDef(), std::move(switchOp));
      return std::make_pair(true, op);
    } else {
      m_builder.rewriteOp(op->getDef(), Op::Branch(defaultCase));
      m_builder.rewriteOp(construct.getDef(), Op::Label());
      return std::make_pair(true, op);
    }
  }

  return std::make_pair(false, ++op);
}


void CleanupControlFlowPass::removeUnusedFunctions(uint32_t candidateCount, const SsaDef* candidates) {
  /* Remove unused functions in the list first so that the
   * instruction references do not get invalidated. */
  for (uint32_t i = 0u; i < candidateCount; i++) {
    if (!isFunctionUsed(candidates[i]))
      removeFunction(candidates[i]);
  }

  /* Remove any functions that have further gone unused */
  removeUnusedFunctions();
}


void CleanupControlFlowPass::resolveConditionalBranch(SsaDef branch) {
  dxbc_spv_assert(m_builder.getOp(branch).getOpCode() == OpCode::eBranchConditional);

  handleBranchConditional(m_builder.iter(branch));

  removeUnusedBlocks();
  removeUnusedFunctions();
}


bool CleanupControlFlowPass::runPass(Builder& builder) {
  return CleanupControlFlowPass(builder).run();
}


std::pair<bool, Builder::iterator> CleanupControlFlowPass::handleFunction(Builder::iterator op) {
  if (isFunctionUsed(op->getDef()))
    return std::make_pair(false, ++op);

  return std::make_pair(true, m_builder.iter(removeFunction(op->getDef())));
}


std::pair<bool, Builder::iterator> CleanupControlFlowPass::handleLabel(Builder::iterator op) {
  /* Remove unused blocks entirely */
  if (!isBlockUsed(op->getDef()))
    return std::make_pair(true, m_builder.iter(removeBlock(op->getDef())));

  /* If the block does not serve any special function w.r.t. structured control flow,
   * only has a single predecessor, and if that predecessor is not the header of any
   * construct and unconditionally branches to this block without any trivial phi,
   * we can merge the two blocks. This is a common occurence as part of other control
   * flow optimizations. */
  SsaDef parentBranch = { };

  auto [a, b] = m_builder.getUses(op->getDef());

  for (auto iter = a; iter != b; iter++) {
    switch (iter->getOpCode()) {
      case OpCode::eLabel:
      case OpCode::eBranchConditional:
      case OpCode::eSwitch:
        return std::make_pair(false, ++op);

      case OpCode::eBranch: {
        if (std::exchange(parentBranch, iter->getDef()))
          return std::make_pair(false, ++op);
      } break;

      case OpCode::ePhi:
        continue;

      default:
        dxbc_spv_assert(iter->isDeclarative());
    }
  }

  /* The blocks must be adjacent in code */
  if (m_builder.getPrev(op->getDef()) != parentBranch)
    return std::make_pair(false, ++op);

  /* The single predecessor can't be a construct header of any kind */
  auto& parentBlock = m_builder.getOp(findContainingBlock(m_builder, parentBranch));

  if (Construct(parentBlock.getOperand(parentBlock.getFirstLiteralOperandIndex())) != Construct::eNone)
    return std::make_pair(false, ++op);

  /* If the parent block is used in any phi, which must be a trivial phi
   * inside the block we want to eliminate, we can't merge yet */
  std::tie(a, b) = m_builder.getUses(parentBlock.getDef());

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::ePhi)
      return std::make_pair(false, ++op);
  }

  /* We can merge, simply replace all uses of the block with its predecessor */
  m_builder.rewriteOp(parentBlock.getDef(), *op);
  m_builder.remove(parentBranch);

  dxbc_spv_assert(!isBlockUsed(op->getDef()));

  auto next = m_builder.rewriteDef(op->getDef(), parentBlock.getDef());
  return std::make_pair(true, m_builder.iter(next));
}


std::pair<bool, Builder::iterator> CleanupControlFlowPass::handleBranchConditional(Builder::iterator op) {
  const auto& condOp = m_builder.getOpForOperand(*op, 0u);

  const auto& targetTrue  = m_builder.getOpForOperand(*op, 1u);
  const auto& targetFalse = m_builder.getOpForOperand(*op, 2u);

  dxbc_spv_assert(targetTrue.getDef() != targetFalse.getDef());

  const auto& block = m_builder.getOp(findContainingBlock(m_builder, op->getDef()));
  dxbc_spv_assert(block && Construct(block.getOperand(block.getFirstLiteralOperandIndex())) == Construct::eStructuredSelection);

  const auto& mergeBlock = m_builder.getOpForOperand(block, 0u);
  dxbc_spv_assert(targetTrue.getDef() != mergeBlock.getDef());

  if (condOp.isConstant()) {
    /* Replace conditionl branch with constant condition with a
     * plain branch, and remove the unreachable block */
    bool cond = bool(condOp.getOperand(0u));

    auto branchTarget = cond ? targetTrue.getDef() : targetFalse.getDef();
    auto removeTarget = cond ? targetFalse.getDef() : targetTrue.getDef();

    m_builder.rewriteOp(op->getDef(), Op::Branch(branchTarget));

    const auto& mergeBlock = m_builder.getOpForOperand(block, 0u);
    m_builder.rewriteOp(block.getDef(), Op::Label());

    if (removeTarget == mergeBlock.getDef()) {
      /* If the non-taken branch targets the merge block, we obviously cannot
       * remove that block, but we do need to alter all phis in the block to
       * no longer check for the block containing the conditional branch. */
      rewriteBlockInPhiUsesInBlock(m_builder, removeTarget, block.getDef(), SsaDef());
    } else {
      /* Otherwise, the block targeted by the non-taken branch will be unreachable. */
      dxbc_spv_assert(!isBlockUsed(removeTarget));
      removeBlock(removeTarget);
    }

    return std::make_pair(true, op);
  }

  /* If the 'false' branch targets an empty block that only branches to the merge
   * block of the selection, simply target the merge block itself instead. */
  if (targetFalse.getDef() != mergeBlock.getDef()) {
    const auto& branch = m_builder.getOp(m_builder.getNext(targetFalse.getDef()));

    if (branch.getOpCode() == OpCode::eBranch && SsaDef(branch.getOperand(0u)) == mergeBlock.getDef()) {
      m_builder.rewriteOp(op->getDef(), Op::BranchConditional(condOp.getDef(), targetTrue.getDef(), mergeBlock.getDef()));
      rewriteBlockInPhiUses(m_builder, targetFalse.getDef(), block.getDef());

      dxbc_spv_assert(!isBlockUsed(targetFalse.getDef()));
      removeBlock(targetFalse.getDef());

      return std::make_pair(true, op);
    }
  }

  /* If the 'true' branch is empty, there are two scenarios: Either the 'false' branch
   * targets a dedicated block, in which case we can simply invert the branch condition
   * and let the empty 'false' branch elimination get rid of the empty block.
   * Alternatively, if the 'false' branch targets the merge block, rewrite all phis for
   * this selection as plain selects based on the branch condition and eliminate the
   * conditional branch altogether. This is a non-aggressive way of flattening control
   * flow, as it will eliminate blocks that only perform assignments, but will preserve
   * any control flow with non-trivial code inside it. */
  if (targetTrue.getDef() != mergeBlock.getDef()) {
    const auto& branch = m_builder.getOp(m_builder.getNext(targetTrue.getDef()));

    if (branch.getOpCode() == OpCode::eBranch && SsaDef(branch.getOperand(0u)) == mergeBlock.getDef()) {
      if (targetFalse.getDef() != mergeBlock.getDef()) {
        auto notCond = m_builder.addBefore(op->getDef(), Op::BNot(condOp.getType(), condOp.getDef()));
        m_builder.rewriteOp(op->getDef(), Op::BranchConditional(notCond, targetFalse.getDef(), targetTrue.getDef()));
        return std::make_pair(true, op);
      } else {
        auto next = m_builder.getNext(mergeBlock.getDef());

        while (m_builder.getOp(next).getOpCode() == OpCode::ePhi) {
          const auto& phi = m_builder.getOp(next);

          SsaDef trueValue = { };
          SsaDef falseValue = { };

          forEachPhiOperand(phi, [&] (SsaDef from, SsaDef value) {
            if (from == targetTrue.getDef())
              trueValue = value;
            else if (from == block.getDef())
              falseValue = value;
            else
              dxbc_spv_unreachable();
          });

          m_builder.rewriteOp(next, Op::Select(phi.getType(), condOp.getDef(), trueValue, falseValue));
          next = m_builder.getNext(phi.getDef());
        }

        m_builder.rewriteOp(op->getDef(), Op::Branch(mergeBlock.getDef()));
        m_builder.rewriteOp(block.getDef(), Op::Label());

        dxbc_spv_assert(!isBlockUsed(targetTrue.getDef()));
        removeBlock(targetTrue.getDef());

        return std::make_pair(true, op);
      }
    }
  }

  /* If the block consists of nothing but a conditional branch, is the merge
   * block of a selection, and that selection uses a conditional branch with
   * the same condition and the merge block as its false branch, we can fold
   * the block into the parent selection and get rid of the redundant branch. */
  if (block.getDef() == m_builder.getPrev(op->getDef())) {
    SsaDef parentSelection = { };
    SsaDef parentBranchIf = { };
    SsaDef parentBranchEnd = { };

    auto [a, b] = m_builder.getUses(block.getDef());

    for (auto use = a; use != b; use++) {
      switch (use->getOpCode()) {
        case OpCode::eLabel: {
          if (Construct(use->getOperand(use->getFirstLiteralOperandIndex())) != Construct::eStructuredSelection)
            return std::make_pair(false, ++op);

          dxbc_spv_assert(SsaDef(use->getOperand(0u)) == block.getDef());

          if (std::exchange(parentSelection, use->getDef()))
            return std::make_pair(false, ++op);
        } break;

        case OpCode::eBranch: {
          if (std::exchange(parentBranchEnd, use->getDef()))
            return std::make_pair(false, ++op);
        } break;

        case OpCode::eBranchConditional: {
          if (std::exchange(parentBranchIf, use->getDef()))
            return std::make_pair(false, ++op);

          /* Ensure that conditions match and the 'false' target targets our block */
          if (m_builder.getOpForOperand(*use, 0u).getDef() != condOp.getDef() ||
              m_builder.getOpForOperand(*use, 1u).getDef() == block.getDef() ||
              m_builder.getOpForOperand(*use, 2u).getDef() != block.getDef())
            return std::make_pair(false, ++op);
        } break;

        default:
          break;
      }
    }

    if (!parentSelection || !parentBranchIf || !parentBranchEnd)
      return std::make_pair(false, ++op);

    /* We ensured that phis for this block only exist inside the merge block.
     * Rewrite those phis to use the original selection instead. */
    rewriteBlockInPhiUsesInBlock(m_builder,
      mergeBlock.getDef(), block.getDef(), parentSelection);

    /* Rewrite unconditional branch inside the parent's if block to target
     * the true branch of the current selection instead */
    const auto& tBranch = m_builder.getOpForOperand(*op, 1u);
    const auto& fBranch = m_builder.getOpForOperand(*op, 2u);

    m_builder.rewriteOp(parentBranchEnd, Op::Branch(tBranch.getDef()));

    /* Rewrite conditional branch in parent selection to target the false
     * branch of the current selection rather the merge block */
    const auto& parentBranch = m_builder.getOp(parentBranchIf);

    m_builder.rewriteOp(parentBranchIf, Op::BranchConditional(
      m_builder.getOpForOperand(parentBranch, 0u).getDef(),
      m_builder.getOpForOperand(parentBranch, 1u).getDef(),
      fBranch.getDef()));

    /* Rewrite the parent selection to target the new merge block */
    m_builder.rewriteOp(parentSelection, Op::LabelSelection(mergeBlock.getDef()));

    /* Remove the old merge block, which is now unreachable */
    auto next = removeBlock(block.getDef());
    return std::make_pair(true, m_builder.iter(next));
  }

  return std::make_pair(false, ++op);
}


bool CleanupControlFlowPass::removeUnusedFunctions() {
  bool progress = false;

  while (!m_unusedFunctions.empty()) {
    auto function = m_unusedFunctions.back();
    m_unusedFunctions.pop_back();

    if (m_builder.getOp(function)) {
      removeFunction(function);
      progress = true;
    }
  }

  return progress;
}


SsaDef CleanupControlFlowPass::removeFunction(SsaDef function) {
  dxbc_spv_assert(m_builder.getOp(function).getOpCode() == OpCode::eFunction);

  /* Remove any debug instructions targeting this function as well */
  auto uses = m_builder.getUses(function);

  while (uses.first != uses.second) {
    m_builder.removeOp(*uses.first);
    uses = m_builder.getUses(function);
  }

  /* Remove the function itself. If we run into any function call
   * instructions, add the called function to be re-checked. */
  auto iter = m_builder.iter(function);

  while (iter->getOpCode() != OpCode::eFunctionEnd) {
    if (iter->getOpCode() == OpCode::eFunctionCall)
      iter = m_builder.iter(removeFunctionCall(iter->getDef()));
    else
      iter = m_builder.iter(m_builder.removeOp(*iter));
  }

  /* Remove function end as well and return next instruction */
  return m_builder.removeOp(*iter);
}


bool CleanupControlFlowPass::isFunctionUsed(SsaDef function) const {
  dxbc_spv_assert(m_builder.getOp(function).getOpCode() == OpCode::eFunction);

  auto [a, b] = m_builder.getUses(function);

  for (auto use = a; use != b; use++) {
    /* Recursion isn't possible, so ignore that case */
    if (use->getOpCode() == OpCode::eEntryPoint ||
        use->getOpCode() == OpCode::eFunctionCall)
      return true;
  }

  return false;
}


SsaDef CleanupControlFlowPass::removeFunctionCall(SsaDef call) {
  auto callTarget = SsaDef(m_builder.getOp(call).getOperand(0u));
  auto next = m_builder.remove(call);

  if (!isFunctionUsed(callTarget))
    m_unusedFunctions.push_back(callTarget);

  return next;
}


bool CleanupControlFlowPass::removeUnusedBlocks() {
  if (m_unusedBlocks.empty())
    return false;

  while (!m_unusedBlocks.empty()) {
    auto block = m_unusedBlocks.back();
    m_unusedBlocks.pop_back();

    removeBlock(block);
  }

  return true;
}


SsaDef CleanupControlFlowPass::removeBlock(SsaDef block) {
  dxbc_spv_assert(m_builder.getOp(block).getOpCode() == OpCode::eLabel);

  removeBlockFromUnusedList(block);

  auto iter = m_builder.iter(block);
  rewriteBlockInPhiUses(m_builder, block, SsaDef());

  while (!isBlockTerminator(iter->getOpCode())) {
    if (iter->getOpCode() == OpCode::eFunctionCall)
      iter = m_builder.iter(removeFunctionCall(iter->getDef()));
    else if (iter->getType().isVoidType())
      iter = m_builder.iter(m_builder.removeOp(*iter));
    else
      iter = m_builder.iter(m_builder.rewriteDef(iter->getDef(), m_builder.makeUndef(iter->getType())));
  }

  /* Remove actual block terminator */
  return removeBlockTerminator(iter->getDef());
}


void CleanupControlFlowPass::removeBlockFromUnusedList(SsaDef block) {
  for (size_t i = 0u; i < m_unusedBlocks.size(); ) {
    if (m_unusedBlocks[i] == block) {
      m_unusedBlocks[i] = m_unusedBlocks.back();
      m_unusedBlocks.pop_back();
    } else {
      i++;
    }
  }
}


SsaDef CleanupControlFlowPass::removeBlockTerminator(SsaDef block) {
  util::small_vector<SsaDef, 16u> branchTargets;

  const auto& op = m_builder.getOp(block);

  switch (op.getOpCode()) {
    case OpCode::eBranch: {
      branchTargets.push_back(SsaDef(op.getOperand(0u)));
    } break;

    case OpCode::eBranchConditional: {
      branchTargets.push_back(SsaDef(op.getOperand(1u)));
      branchTargets.push_back(SsaDef(op.getOperand(2u)));
    } break;

    case OpCode::eSwitch: {
      for (uint32_t i = 1u; i < op.getOperandCount(); i += 2u)
        branchTargets.push_back(SsaDef(op.getOperand(i)));
    } break;

    default:
      break;
  }

  /* Remove branch and add any now unused blocks to the list */
  auto next = m_builder.remove(block);

  for (auto t : branchTargets) {
    if (!isBlockUsed(t))
      m_unusedBlocks.push_back(t);
  }

  return next;
}


bool CleanupControlFlowPass::isMergeBlock(SsaDef block) const {
  dxbc_spv_assert(m_builder.getOp(block).getOpCode() == OpCode::eLabel);

  /* Find any construct where this block is declared as a merge block */
  auto [a, b] = m_builder.getUses(block);

  for (auto use = a; use != b; use++) {
    if (use->getOpCode() == OpCode::eLabel) {
      auto construct = Construct(use->getOperand(use->getFirstLiteralOperandIndex()));

      if (construct == Construct::eStructuredSelection || construct == Construct::eStructuredLoop) {
        auto mergeBlock = SsaDef(use->getOperand(0u));

        if (mergeBlock == block)
          return true;
      }
    }
  }

  return false;
}


bool CleanupControlFlowPass::isContinueBlock(SsaDef block) const {
  dxbc_spv_assert(m_builder.getOp(block).getOpCode() == OpCode::eLabel);

  /* Find loop construct where this block is declared as a continue block */
  auto [a, b] = m_builder.getUses(block);

  for (auto use = a; use != b; use++) {
    if (use->getOpCode() == OpCode::eLabel) {
      auto construct = Construct(use->getOperand(use->getFirstLiteralOperandIndex()));

      if (construct == Construct::eStructuredLoop) {
        auto continueBlock = SsaDef(use->getOperand(1u));

        if (continueBlock == block)
          return true;
      }
    }
  }

  return false;
}


bool CleanupControlFlowPass::isBlockReachable(SsaDef block) const {
  const auto& labelOp = m_builder.getOp(block);
  auto construct = getConstructForBlock(block);

  /* Check uses for branches targeting the block */
  auto [a, b] = m_builder.getUses(block);

  for (auto use = a; use != b; use++) {
    if (use->getOpCode() == OpCode::eBranch ||
        use->getOpCode() == OpCode::eBranchConditional ||
        use->getOpCode() == OpCode::eSwitch) {
      /* In general, if there is a branch to the block, we can consider it to be
       * reachable. For loops, we need to ignore the back-edge from the continue
       * block since the continue block itself cannot be reachable if the loop
       * header block isn't reached through other means. */
      if (construct != Construct::eStructuredLoop)
        return true;

      auto containingBlock = findContainingBlock(m_builder, use->getDef());
      auto continueBlock = SsaDef(labelOp.getOperand(1u));

      if (containingBlock != continueBlock)
        return true;
    }
  }

  /* If the block has no direct uses, it can only be reachable
   * if it is the first block inside a function. */
  const auto& prev = m_builder.getOp(m_builder.getPrev(block));
  return prev.getOpCode() == OpCode::eFunction;
}


bool CleanupControlFlowPass::isBlockUsed(SsaDef block) const {
  /* If the block itself is part of a construct, we need to keep it intact, otherwise
   * only consider blocks that are directly reachable. If the block is an unreachable
   * merge block, it will be removed if the construct itself is unreachable and gets
   * removed. */
  return isBlockReachable(block) || isContinueBlock(block) || isMergeBlock(block);
}


bool CleanupControlFlowPass::isBlockUsedInPhi(SsaDef def) const {
  auto [a, b] = m_builder.getUses(def);

  for (auto i = a; i != b; i++) {
    if (i->getOpCode() == OpCode::ePhi)
      return true;
  }

  return false;
}


Construct CleanupControlFlowPass::getConstructForBlock(SsaDef block) const {
  dxbc_spv_assert(m_builder.getOp(block).getOpCode() == OpCode::eLabel);

  const auto& op = m_builder.getOp(block);
  return Construct(op.getOperand(op.getFirstLiteralOperandIndex()));
}

}
