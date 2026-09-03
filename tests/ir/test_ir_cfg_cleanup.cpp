#include "../../ir/ir_builder.h"

#include "../../ir/passes/ir_pass_cfg_cleanup.h"

#include "../test_common.h"

namespace dxbc_spv::tests::ir {

using namespace dxbc_spv::ir;

/* Builds the control flow graph below, reproducing the shape found in the wild
 * in an ENBSeries pixel shader (see tests/data/enb_dead_loop_fs.md):
 * a selection whose condition is a constant, where the taken path returns and
 * the not-taken path leads to a loop.
 *
 *   entry:        LabelSelection(merge = selMerge)
 *                 BranchConditional(<constant>, thenBlock, selMerge)
 *   thenBlock:    Label
 *                 Return
 *   selMerge:     Label
 *                 Branch loopHeader
 *   loopHeader:   LabelLoop(merge = loopMerge, continue = loopContinue)
 *                 Branch loopContinue
 *   loopContinue: Label
 *                 Branch loopHeader        <- back-edge
 *   loopMerge:    Label
 *                 Return
 *
 * Constructed with the condition supplied by the caller so both folding
 * directions can be exercised. */
static Builder buildConstantSelectionAroundLoop(bool condition) {
  Builder builder;

  auto funcEnd = builder.add(Op::FunctionEnd());
  auto func = builder.addBefore(funcEnd, Op::Function(ScalarType::eVoid));

  /* Allocate the blocks up front */
  auto entry        = builder.addBefore(funcEnd, Op::Label());
  auto thenBlock    = builder.addBefore(funcEnd, Op::Label());
  auto selMerge     = builder.addBefore(funcEnd, Op::Label());
  auto loopHeader   = builder.addBefore(funcEnd, Op::Label());
  auto loopContinue = builder.addBefore(funcEnd, Op::Label());
  auto loopMerge    = builder.addBefore(funcEnd, Op::Label());

  builder.rewriteOp(entry, Op::LabelSelection(selMerge));
  builder.rewriteOp(loopHeader, Op::LabelLoop(loopMerge, loopContinue));

  /* And fill 'em */
  auto cond = builder.makeConstant(condition);

  builder.addBefore(thenBlock,    Op::BranchConditional(cond, thenBlock, selMerge));
  builder.addBefore(selMerge,     Op::Return());
  builder.addBefore(loopHeader,   Op::Branch(loopHeader));
  builder.addBefore(loopContinue, Op::Branch(loopContinue));
  builder.addBefore(loopMerge,    Op::Branch(loopHeader));
  builder.addBefore(funcEnd,      Op::Return());

  builder.add(Op::EntryPoint(func, ShaderStage::ePixel));
  return builder;
}


/* Walks every op in the builder and checks that no operand names a def that has
 * been removed.
 *
 * Only the first getFirstLiteralOperandIndex() operands are SSA defs. */
static bool hasDanglingOperands(const Builder& builder) {
  bool dangling = false;

  for (auto iter = builder.begin(); iter != builder.end(); ++iter) {
    for (uint32_t i = 0u; i < iter->getFirstLiteralOperandIndex(); i++) {
      auto operand = SsaDef(iter->getOperand(i));

      if (operand && !builder.getOp(operand)) {
        std::cerr << "  dangling operand " << i << " of op "
          << iter->getDef().getId() << " -> removed def "
          << operand.getId() << std::endl;
        dangling = true;
      }
    }
  }

  return dangling;
}


/* Every block reached by a branch must still be a label.
 * This is a reimplementation of the invariant getConstructForBlock asserts
 * on. */
static bool hasNonLabelBranchTarget(const Builder& builder) {
  bool bad = false;

  for (auto iter = builder.begin(); iter != builder.end(); ++iter) {
    util::small_vector<SsaDef, 16u> targets;

    switch (iter->getOpCode()) {
      case OpCode::eBranch:
        targets.push_back(SsaDef(iter->getOperand(0u)));
        break;

      case OpCode::eBranchConditional:
        targets.push_back(SsaDef(iter->getOperand(1u)));
        targets.push_back(SsaDef(iter->getOperand(2u)));
        break;

      case OpCode::eSwitch:
        for (uint32_t i = 1u; i < iter->getOperandCount(); i += 2u)
          targets.push_back(SsaDef(iter->getOperand(i)));
        break;

      default:
        continue;
    }

    for (auto t : targets) {
      if (builder.getOp(t).getOpCode() != OpCode::eLabel) {
        std::cerr << "  branch op " << iter->getDef().getId()
          << " targets non-label def " << t.getId() << std::endl;
        bad = true;
      }
    }
  }

  return bad;
}


/* The actual meat of the bug.
 *
 * With a constant-true condition the selection folds to the returning side, so
 * everything below it becomes unreachable and gets cleaned up. The order in
 * which that happens is what matters:
 *
 *   1. selMerge is no longer branched to, so handleLabel removes it. Its
 *      terminator branched to loopHeader, so loopHeader goes on the work list.
 *   2. loopHeader is a loop header whose only remaining predecessor is the loop's
 *      own continue block. Then isBlockReachable deliberately ignores that back-
 *      edge (ir_pass_cfg_cleanup.cpp:681), correctly deciding the loop is dead,
 *      and the header is removed.
 *   3. removeBlock rewrites phi uses of the removed block and scrubs the work
 *      list, but does not rewrite branch operands. loopContinue's back-edge is
 *      now a branch to a def that no longer exists.
 *   4. loopContinue is no longer a continue block either, because isContinueBlock
 *      looks for a LabelLoop, and that label is gone now, so it is removed too.
 *   5. removeBlockTerminator collects its terminator's targets and calls
 *      isBlockUsed on the stale loopHeader (:612), which reaches
 *      getConstructForBlock and trips its eLabel assert.
 *
 * After the fix it would be optimal if pass removed the whole dead region and
 * left no branch pointing at a removed block. */
void testIrCfgCleanupDeadLoopBackEdge() {
  for (bool condition : { true, false }) {
    std::cerr << "-- constant condition = " << (condition ? "true" : "false") << std::endl;

    auto builder = buildConstantSelectionAroundLoop(condition);

    CleanupControlFlowPass::runPass(builder);

    ok(!hasDanglingOperands(builder));
    ok(!hasNonLabelBranchTarget(builder));

    uint32_t loopHeadersCount = 0u;

    for (auto iter = builder.begin(); iter != builder.end(); ++iter) {
      if (iter->getOpCode() == OpCode::eLabel &&
          Construct(iter->getOperand(iter->getFirstLiteralOperandIndex())) == Construct::eStructuredLoop)
        loopHeadersCount++;
    }

    /* Actually assert the loop is removed when condition==true and
     * preserved when condition==false. */
    ok(loopHeadersCount == (condition ? 0u : 1u));
  }
}


void testIrCfgCleanup() {
  testIrCfgCleanupDeadLoopBackEdge();
}

}
