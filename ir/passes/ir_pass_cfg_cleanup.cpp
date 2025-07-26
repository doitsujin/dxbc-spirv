#include "ir_pass_cfg_cleanup.h"

#include "../ir_utils.h"

namespace dxbc_spv::ir {

CleanupControlFlowPass::CleanupControlFlowPass(Builder& builder)
: m_builder(builder) {

}


CleanupControlFlowPass::~CleanupControlFlowPass() {

}


void CleanupControlFlowPass::run() {
  auto iter = m_builder.getCode().first;

  while (iter != m_builder.end()) {
    switch (iter->getOpCode()) {
      case OpCode::eFunction:
        iter = handleFunction(iter);
        break;

      case OpCode::eLabel:
        iter = handleLabel(iter);
        break;

      case OpCode::eBranchConditional:
        iter = handleBranchConditional(iter);
        break;

      default:
        ++iter;
    }
  }

  /* Remove any blocks that have been additionally marked as unreachable
   * during processing. This may also cause more functions to go unused,
   * so do this first. */
  removeUnusedBlocks();

  /* Remove any functions that may have been removed */
  removeUnusedFunctions();
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


void CleanupControlFlowPass::runPass(Builder& builder) {
  CleanupControlFlowPass pass(builder);
  pass.run();
}


Builder::iterator CleanupControlFlowPass::handleFunction(Builder::iterator op) {
  if (isFunctionUsed(op->getDef()))
    return ++op;

  return m_builder.iter(removeFunction(op->getDef()));
}


Builder::iterator CleanupControlFlowPass::handleLabel(Builder::iterator op) {
  if (isBlockUsed(op->getDef()))
    return ++op;

  return m_builder.iter(removeBlock(op->getDef()));
}


Builder::iterator CleanupControlFlowPass::handleBranchConditional(Builder::iterator op) {
  const auto& condOp = m_builder.getOp(SsaDef(op->getOperand(0u)));

  if (!condOp.isConstant())
    return ++op;

  bool cond = bool(condOp.getOperand(0u));

  auto targetTrue  = SsaDef(op->getOperand(1u));
  auto targetFalse = SsaDef(op->getOperand(2u));

  auto branchTarget = cond ? targetTrue : targetFalse;
  auto removeTarget = cond ? targetFalse : targetTrue;

  m_builder.rewriteOp(op->getDef(), Op::Branch(branchTarget));

  removeBlock(removeTarget);
  return ++op;
}


void CleanupControlFlowPass::removeUnusedFunctions() {
  while (!m_unusedFunctions.empty())
    removeFunction(m_unusedFunctions.back());
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


void CleanupControlFlowPass::removeUnusedBlocks() {
  while (!m_unusedBlocks.empty())
    removeBlock(m_unusedBlocks.back());
}


SsaDef CleanupControlFlowPass::removeBlock(SsaDef block) {
  auto iter = m_builder.iter(block);
  removeBlockFromPhiUses(block);

  while (!isBlockTerminator(iter->getOpCode())) {
    if (iter->getOpCode() == OpCode::eFunctionCall)
      iter = m_builder.iter(removeFunctionCall(iter->getDef()));
    else
      iter = m_builder.iter(m_builder.removeOp(*iter));
  }

  /* Remove actual block terminator */
  return removeBlockTerminator(iter->getDef());
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


void CleanupControlFlowPass::removeBlockFromPhiUses(SsaDef block) {
  dxbc_spv_assert(m_builder.getOp(block).getOpCode() == OpCode::eLabel);

  util::small_vector<SsaDef, 64u> uses;
  m_builder.getUses(block, uses);

  for (auto useDef : uses) {
    const auto& use = m_builder.getOp(useDef);

    if (use.getOpCode() == OpCode::ePhi) {
      uint32_t blockCount = use.getOperandCount() / 2u;

      if (blockCount <= 2u) {
        /* Find the value that does not correspond to the block
         * being removed and remove the phi entirely. */
        for (uint32_t i = 0u; i < use.getOperandCount(); i += 2u) {
          auto phiBlock = SsaDef(use.getOperand(i));

          if (phiBlock == block) {
            auto phiValue = SsaDef(use.getOperand(i + 1u));
            m_builder.rewriteDef(use.getDef(), phiValue);
            break;
          }
        }
      } else {
        /* Replace phi op with another phi that is missing this block */
        Op phi(OpCode::ePhi, use.getType());

        for (uint32_t i = 0u; i < use.getOperandCount(); i += 2u) {
          auto phiBlock = SsaDef(use.getOperand(i));
          auto phiValue = SsaDef(use.getOperand(i + 1u));

          if (phiBlock != block) {
            phi.addOperand(phiBlock);
            phi.addOperand(phiValue);
          }
        }

        m_builder.rewriteOp(use.getDef(), std::move(phi));
      }
    }
  }
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

      auto containingBlock = getContainingBlock(use->getDef());
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
  /* If the block itself is a continue block, we need to keep it intact,
   * otherwise only consider blocks that are directly reachable. */
  return isBlockReachable(block) || isContinueBlock(block);
}


SsaDef CleanupControlFlowPass::getContainingBlock(SsaDef block) const {
  while (block && m_builder.getOp(block).getOpCode() != OpCode::eLabel)
    block = m_builder.getPrev(block);

  return block;
}


Construct CleanupControlFlowPass::getConstructForBlock(SsaDef block) const {
  dxbc_spv_assert(m_builder.getOp(block).getOpCode() == OpCode::eLabel);

  const auto& op = m_builder.getOp(block);
  return Construct(op.getOperand(op.getFirstLiteralOperandIndex()));
}

}
