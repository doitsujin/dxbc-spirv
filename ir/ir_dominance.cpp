#include "ir_dominance.h"

#include "../util/util_log.h"

namespace dxbc_spv::ir {

DominanceGraph::DominanceGraph(const Builder& builder)
: m_builder(builder) {
  SsaDef block = { };

  for (const auto& op : builder) {
    switch (op.getOpCode()) {
      case OpCode::eLabel: {
        block = op.getDef();

        if (!blockHasPredecessors(block))
          m_startNodes.push_back(block);

        auto& node = m_nodeInfos[block];
        node.blockDef = block;
        node.block.index = m_blocks.size();

        auto& info = m_blocks.emplace_back();
        info.block = block;
      } break;

      case OpCode::eReturn:
      case OpCode::eUnreachable: {
        m_exitNodes.push_back(block);
      } [[fallthrough]];

      case OpCode::eBranch:
      case OpCode::eBranchConditional:
      case OpCode::eSwitch: {
        auto& node = m_nodeInfos[op.getDef()];
        node.blockDef = block;

        auto& blockInfo = m_nodeInfos[block];
        blockInfo.block.terminator = op.getDef();
      } break;

      default: {
        auto& node = m_nodeInfos[op.getDef()];
        node.blockDef = block;
      } break;
    }
  }

  initLinks();
  initDominators();

  computeDominators();
  computePostDominators();

  for (const auto& n : m_blocks) {
    auto& node = m_nodeInfos[n.block];
    node.block.immDom = findImmediateDominator(n.block);
    node.block.immPostDom = findImmediatePostDominator(n.block);
  }
}


DominanceGraph::~DominanceGraph() {

}


bool DominanceGraph::dominates(SsaDef a, SsaDef b) const {
  auto aIndex = m_nodeInfos.at(a).block.index;
  auto bIndex = m_nodeInfos.at(b).block.index;

  return isDominatorBit(m_bitMaskDom.data(), bIndex, aIndex);
}


bool DominanceGraph::strictlyDominates(SsaDef a, SsaDef b) const {
  return a != b && dominates(a, b);
}


bool DominanceGraph::postDominates(SsaDef a, SsaDef b) const {
  auto aIndex = m_nodeInfos.at(a).block.index;
  auto bIndex = m_nodeInfos.at(b).block.index;

  return isDominatorBit(m_bitMaskPostDom.data(), bIndex, aIndex);
}


bool DominanceGraph::strictlyPostDominates(SsaDef a, SsaDef b) const {
  return a != b && postDominates(a, b);
}


SsaDef DominanceGraph::getImmediateDominator(SsaDef def) const {
  return m_nodeInfos.at(def).block.immDom;
}


SsaDef DominanceGraph::getImmediatePostDominator(SsaDef def) const {
  return m_nodeInfos.at(def).block.immPostDom;
}


bool DominanceGraph::defDominates(SsaDef a, SsaDef b) const {
  if (!a || m_builder.getOp(a).isDeclarative()) return true;
  if (!b || m_builder.getOp(b).isDeclarative()) return false;

  auto aBlock = m_nodeInfos.at(a).blockDef;
  auto bBlock = m_nodeInfos.at(b).blockDef;

  if (aBlock != bBlock)
    return dominates(aBlock, bBlock);

  while (b != a && b != bBlock)
    b = m_builder.getPrev(b);

  return b == a;
}


SsaDef DominanceGraph::getClosestCommonDominator(SsaDef a, SsaDef b) const {
  util::small_vector<SsaDef, 256u> blocks;
  blocks.push_back(a);
  blocks.push_back(b);

  for (size_t i = 0u; i < blocks.size(); i++) {
    auto dom = getImmediateDominator(blocks[i]);

    if (dom) {
      if (std::find(blocks.begin(), blocks.end(), dom) != blocks.end())
        return dom;

      blocks.push_back(dom);
    }
  }

  return SsaDef();
}


void DominanceGraph::initLinks() {
  /* Compute number of successors and predecessors for each block */
  for (const auto& n : m_blocks) {
    const auto& terminator = m_builder.getOp(getBlockTerminator(n.block));

    forEachBranchTarget(terminator, [&] (SsaDef target) {
      auto srcIndex = m_nodeInfos.at(n.block).block.index;
      auto dstIndex = m_nodeInfos.at(target).block.index;

      m_blocks.at(srcIndex).succCount++;
      m_blocks.at(dstIndex).predCount++;
    });
  }

  uint32_t linkCount = 0u;

  for (auto& n : m_blocks) {
    n.predIndex = linkCount;
    n.succIndex = linkCount + n.predCount;

    linkCount += n.predCount + n.succCount;
  }

  /* Write out successor and predecessor array for each node */
  m_links.resize(linkCount);

  for (const auto& n : m_blocks) {
    const auto& terminator = m_builder.getOp(getBlockTerminator(n.block));

    forEachBranchTarget(terminator, [&] (SsaDef target) {
      auto srcIndex = m_nodeInfos.at(n.block).block.index;
      auto dstIndex = m_nodeInfos.at(target).block.index;

      m_links.at(m_blocks.at(srcIndex).succIndex++) = dstIndex;
      m_links.at(m_blocks.at(dstIndex).predIndex++) = srcIndex;
    });
  }

  /* Fix up indices once again */
  for (auto& n : m_blocks) {
    n.predIndex -= n.predCount;
    n.succIndex -= n.succCount;
  }
}


void DominanceGraph::initDominators() {
  m_nodeCount = m_blocks.size();

  if (m_nodeCount)
    m_maskCount = computeMaskIndex(m_nodeCount - 1u).first + 1u;

  m_bitMaskDom.resize(m_maskCount * m_nodeCount, uint64_t(-1));
  m_bitMaskPostDom.resize(m_maskCount * m_nodeCount, uint64_t(-1));

  for (auto n : m_startNodes) {
    auto index = m_nodeInfos[n].block.index;
    initDominanceMask(m_bitMaskDom.data(), index);
  }

  for (auto n : m_exitNodes) {
    auto index = m_nodeInfos[n].block.index;
    initDominanceMask(m_bitMaskPostDom.data(), index);
  }
}


void DominanceGraph::computeDominators() {
  bool progress = false;

  do {
    for (size_t i = 0u; i < m_nodeCount; i++) {
      const auto& n = m_blocks[i];

      for (uint32_t j = 0u; j < n.predCount; j++)
        progress |= mergeDominanceMasks(m_bitMaskDom.data(), i, m_links[n.predIndex + j]);
    }
  } while (std::exchange(progress, false));
}


void DominanceGraph::computePostDominators() {
  bool progress = false;

  do {
    /* Scanning in reverse order drastically reduces the iteration count */
    for (size_t i = m_nodeCount; i; i--) {
      const auto& n = m_blocks[i - 1u];

      for (uint32_t j = 0u; j < n.succCount; j++)
        progress |= mergeDominanceMasks(m_bitMaskPostDom.data(), i - 1u, m_links[n.succIndex + j]);
    }
  } while (std::exchange(progress, false));
}


SsaDef DominanceGraph::findImmediateDominator(SsaDef node) {
  auto index = findImmediateDominatorBit(m_bitMaskDom.data(), m_nodeInfos[node].block.index);

  if (index < 0)
    return SsaDef();

  return m_blocks.at(index).block;
}


SsaDef DominanceGraph::findImmediatePostDominator(SsaDef node) {
  auto index = findImmediateDominatorBit(m_bitMaskPostDom.data(), m_nodeInfos[node].block.index);

  if (index < 0)
    return SsaDef();

  return m_blocks.at(index).block;
}


int32_t DominanceGraph::findImmediateDominatorBit(const uint64_t* masks, uint32_t node) const {
  auto base = computeBaseIndex(node);

  int32_t selected = -1;

  for (size_t i = 0u; i < m_maskCount; i++) {
    auto mask = masks[base + i];

    while (mask) {
      auto index = computeNodeIndex(i, mask);

      if (index != node && index < m_nodeCount) {
        /* Immediate dominator can't dominate any other dominators */
        if (selected < 0 || isDominatorBit(masks, index, uint32_t(selected)))
          selected = int32_t(index);
      }

      mask &= mask - 1u;
    }
  }

  return selected;
}


bool DominanceGraph::isDominatorBit(const uint64_t* masks, uint32_t base, uint32_t node) const {
  auto [index, bit] = computeMaskIndex(node);
  index += computeBaseIndex(base);

  return masks[index] & bit;
}


void DominanceGraph::initDominanceMask(uint64_t* masks, uint32_t node) {
  auto [index, bit] = computeMaskIndex(node);
  auto base = computeBaseIndex(node);

  for (size_t i = 0u; i < m_maskCount; i++)
    masks[base + i] = (i == index ? bit : uint64_t(0u));
}


bool DominanceGraph::mergeDominanceMasks(uint64_t* masks, uint32_t base, uint32_t node) {
  auto [dstIndex, dstBit] = computeMaskIndex(base);
  auto dstBase = computeBaseIndex(base);
  auto srcBase = computeBaseIndex(node);

  bool progress = false;

  for (size_t i = 0u; i < m_maskCount; i++) {
    auto dstMask = masks[dstBase + i];
    auto srcMask = masks[srcBase + i] & dstMask;

    if (i == dstIndex)
      srcMask |= dstBit;

    if (dstMask != srcMask) {
      masks[dstBase + i] = srcMask;
      progress = true;
    }
  }

  return progress;
}


std::pair<size_t, uint64_t> DominanceGraph::computeMaskIndex(uint32_t node) const {
  return std::make_pair(size_t(node / 64u), uint64_t(1u) << (node % 64u));
}


size_t DominanceGraph::computeBaseIndex(uint32_t node) const {
  return node * m_maskCount;
}


size_t DominanceGraph::computeNodeIndex(size_t maskIndex, uint64_t mask) const {
  return 64u * maskIndex + util::tzcnt(mask);
}


bool DominanceGraph::blockHasPredecessors(SsaDef block) const {
  auto [a, b] = m_builder.getUses(block);

  for (auto use = a; use != b; use++) {
    if (isBranchInstruction(use->getOpCode()))
      return true;
  }

  return false;
}

}
