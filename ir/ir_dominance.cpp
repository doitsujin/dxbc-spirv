#include <vector>

#include "ir_dominance.h"

#include "../util/util_log.h"

namespace dxbc_spv::ir {

DominanceGraph::DominanceGraph(const Builder& builder)
: m_builder(builder) {
  std::vector<SsaDef> postDomQueue;

  SsaDef function = { };
  SsaDef block = { };

  auto [a, b] = m_builder.getCode();

  for (auto iter = a; iter != b; iter++) {
    switch (iter->getOpCode()) {
      case OpCode::eLabel: {
        block = iter->getDef();

        auto& node = m_nodeInfos[block];
        node.blockDef = block;
        node.block.immDom = computeImmediateDominator(block);
      } break;

      case OpCode::eFunction: {
        function = iter->getDef();
      } break;

      case OpCode::eFunctionEnd: {
        function = SsaDef();
      } break;

      default: {
        auto& node = m_nodeInfos[iter->getDef()];
        node.blockDef = block;

        if (isBlockTerminator(iter->getOpCode())) {
          auto& blockNode = m_nodeInfos[block];
          blockNode.block.terminator = iter->getDef();
        }

        postDomQueue.push_back(block);
      } break;
    }
  }

  while (!postDomQueue.empty()) {
    auto block = postDomQueue.back();
    postDomQueue.pop_back();

    auto& node = m_nodeInfos[block];

    std::tie(node.block.immPostDom, node.block.isContinuePath) =
      computeImmediatePostDominator(block);
  }
}


DominanceGraph::~DominanceGraph() {

}


bool DominanceGraph::dominates(SsaDef a, SsaDef b) const {
  dxbc_spv_assert(m_nodeInfos[a].blockDef == a && m_nodeInfos[b].blockDef == b);

  auto node = b;

  while (node && node != a)
    node = getImmediateDominator(node);

  return node == a;
}


bool DominanceGraph::strictlyDominates(SsaDef a, SsaDef b) const {
  return a != b && dominates(a, b);
}


bool DominanceGraph::postDominates(SsaDef a, SsaDef b) const {
  return a == b || strictlyPostDominates(a, b);
}


bool DominanceGraph::strictlyPostDominates(SsaDef a, SsaDef b) const {
  dxbc_spv_assert(m_nodeInfos.at(a).blockDef == a && m_nodeInfos.at(b).blockDef == b);

  /* Post-dominance can be cyclic due to infinite loops */
  auto node = getImmediatePostDominator(b);

  while (node && node != a && node != b)
    node = getImmediatePostDominator(node);

  return node == a;
}


SsaDef DominanceGraph::getImmediateDominator(SsaDef def) const {
  return m_nodeInfos.at(def).block.immDom;
}


SsaDef DominanceGraph::getImmediatePostDominator(SsaDef def) const {
  return m_nodeInfos.at(def).block.immPostDom;
}


bool DominanceGraph::defDominates(SsaDef a, SsaDef b) const {
  if (m_builder.getOp(a).isDeclarative()) return true;
  if (m_builder.getOp(b).isDeclarative()) return false;

  auto aBlock = m_nodeInfos.at(a).blockDef;
  auto bBlock = m_nodeInfos.at(b).blockDef;

  if (aBlock != bBlock)
    return dominates(aBlock, bBlock);

  while (b != a && b != bBlock)
    b = m_builder.getPrev(b);

  return b == a;
}


SsaDef DominanceGraph::computeImmediateDominator(SsaDef block) const {
  /* The SPIR-V flavour of structured control flow that we implement gives us
   * the following guarantees that simplify dominance analysis:
   * - Back edges can only occur in continue blocks, and if reachable, continue
   *   blocks are dominated by the loop header. Consequently, continue blocks can
   *   never dominate the loop header and can therefore be ignored.
   * - Excluding back edges, successors of any given block must occur after the
   *   block itself in the program.
   * Together, this means that when processing any given block, we must have
   * already fully processed all its predecessors as well as all its dominators. */
  auto [a, b] = m_builder.getUses(block);

  util::small_vector<SsaDef, 16> pred;

  for (auto iter = a; iter != b; iter++) {
    if (isBranchInstruction(iter->getOpCode())) {
      /* will be null for back edges */
      auto predBlock = m_nodeInfos[iter->getDef()].blockDef;

      if (predBlock)
        pred.push_back(predBlock);
    }
  }

  /* Handle trivial cases where the block does not have multiple predecessors */
  auto predCount = uint32_t(pred.size());

  if (!predCount)
    return SsaDef();

  if (predCount == 1u)
    return pred.at(0u);

  /* Determine the immediate dominator of the node by performing a breadth-first
   * search over its n predecessors. The first node that we visit n times must
   * therefore be its immediate dominator. */
  util::small_vector<std::pair<SsaDef, uint32_t>, 256> visited;

  while (!pred.empty()) {
    for (auto iter = pred.begin(); iter != pred.end();) {
      auto& node = *iter;

      if (!node) {
        iter = pred.erase(iter);
        continue;
      }

      bool found = false;

      for (auto& v : visited) {
        if (v.first == node) {
          found = true;

          if (++v.second == predCount)
            return v.first;

          break;
        }
      }

      if (!found)
        visited.push_back(std::make_pair(node, 1u));

      node = getImmediateDominator(node);
      iter++;
    }
  }

  /* Invalid cfg? */
  dxbc_spv_unreachable();
  return SsaDef();
}


std::pair<SsaDef, bool> DominanceGraph::computeImmediatePostDominator(SsaDef block) const {
  const auto& terminator = m_builder.getOp(m_nodeInfos[block].block.terminator);

  /* If the block returns, it has no strict post-dominator */
  if (!isBranchInstruction(terminator.getOpCode()))
    return std::make_pair(SsaDef(), false);

  /* If the block has a single successor, that successor is its immediate post-dominator.
   * If additionally the block is the continue block of a loop, and only branches back to
   * the loop header it is the end of a continue path. */
  if (terminator.getOpCode() == OpCode::eBranch) {
    const auto& target = m_builder.getOpForOperand(terminator, 0u);
    return std::make_pair(target.getDef(), isContinuePath(block, target.getDef()));
  }

  /* Handling loops is tricky: If all or none of the block's successors are part of
   * a continue path, find the common post-dominator using a breadth-first search.
   * Otherwise, we need to ignore continue paths.
   *
   * Ordering guarantees from above still apply, and we process blocks in reverse
   * order for post-dominance analysis, so when processing any given block we will
   * already have processed all its successors that are not a back-edge. */
  util::small_vector<SsaDef, 16> succ;

  forEachBranchTarget(terminator, [&] (SsaDef target) {
    if (!isContinuePath(block, target))
      succ.push_back(target);
  });

  if (succ.empty()) {
    forEachBranchTarget(terminator, [&] (SsaDef target) {
      succ.push_back(target);
    });
  }

  dxbc_spv_assert(!succ.empty());

  /* Handle simple case where only one branch target is not a continue path */
  uint32_t succCount = uint32_t(succ.size());

  if (succCount == 1u) {
    auto result = succ.at(0u);
    return std::make_pair(result, isContinuePath(block, result));
  }

  /* Otherwise, do a breadth-first search over post-dominators until we find
   * a common node. If no common node exists, then some paths may return and
   * the node does not have an immediate post-dominator. */
  util::small_vector<std::pair<SsaDef, uint32_t>, 256> visited;

  while (!succ.empty()) {
    for (auto iter = succ.begin(); iter != succ.end(); ) {
      auto& node = *iter;

      if (!node) {
        iter = succ.erase(iter);
        continue;
      }

      bool found = false;

      for (auto& v : visited) {
        if (v.first == node) {
          found = true;

          if (++v.second == succCount)
            return std::make_pair(v.first, m_nodeInfos[v.first].block.isContinuePath);

          break;
        }
      }

      if (!found)
        visited.push_back(std::make_pair(node, 1u));

      node = getImmediatePostDominator(node);
      iter++;
    }
  }

  return std::make_pair(SsaDef(), false);
}


bool DominanceGraph::isContinuePath(SsaDef block, SsaDef target) const {
  /* If the edge leads to a continue path, it itself is
   * trivially part of a continue path */
  if (m_nodeInfos[target].block.isContinuePath)
    return true;

  /* Otherwise, check whether this is a back-edge */
  const auto& targetOp = m_builder.getOp(target);
  dxbc_spv_assert(targetOp.getOpCode() == OpCode::eLabel);

  auto construct = Construct(targetOp.getOperand(targetOp.getFirstLiteralOperandIndex()));

  SsaDef continueBlock = { };

  if (construct == Construct::eStructuredLoop)
    continueBlock = SsaDef(targetOp.getOperand(1u));

  return block == continueBlock;
}

}
