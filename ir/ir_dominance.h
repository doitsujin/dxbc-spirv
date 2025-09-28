#pragma once

#include "ir_builder.h"
#include "ir_container.h"
#include "ir_utils.h"

namespace dxbc_spv::ir {

/** Dominance graph. */
class DominanceGraph {

public:

  explicit DominanceGraph(const Builder& builder);

  ~DominanceGraph();

  /* Checks whether a block dominates another. */
  bool dominates(SsaDef a, SsaDef b) const;

  /* Checks whether a block dominates another and the two
   * blocks are not the same. */
  bool strictlyDominates(SsaDef a, SsaDef b) const;

  /* Checks whether a block post-dominates another. */
  bool postDominates(SsaDef a, SsaDef b) const;

  /* Checks whether a block post-dominates another
   * and the two blocks are not the same. */
  bool strictlyPostDominates(SsaDef a, SsaDef b) const;

  /* Queries immediate dominator for the given block. Can be
   * used to traverse the dominance graph upwards. */
  SsaDef getImmediateDominator(SsaDef def) const;

  /* Queries immediate post-dominator for the given block. Can be used
   * to traverse the dominance graph downwards. May be a null node if
   * a path returns instead of merging. */
  SsaDef getImmediatePostDominator(SsaDef def) const;

  /* Checks whether a definition dominates another. This does not
   * require operands to be a label. If both instructions are
   * contained inside the same block, this will check that a is
   * defined before b. */
  bool defDominates(SsaDef a, SsaDef b) const;

  /* Finds closest block that dominates both blocks. */
  SsaDef getClosestCommonDominator(SsaDef a, SsaDef b) const;

  /* Queries containing block for a definition. */
  SsaDef getBlockForDef(SsaDef def) const {
    return m_nodeInfos[def].blockDef;
  }

  void setBlockForDef(SsaDef def, SsaDef block) {
    m_nodeInfos[def].blockDef = block;
  }

  /* Queries terminator instruction for a given block. */
  SsaDef getBlockTerminator(SsaDef def) const {
    dxbc_spv_assert(m_builder.getOp(def).getOpCode() == OpCode::eLabel);
    return m_nodeInfos[def].block.terminator;
  }

private:

  struct NodeInfo {
    /* Block that any given instruction belongs to */
    SsaDef blockDef = { };

    struct {
      /* Termination instruction */
      SsaDef terminator = { };
      /* Immediate dominator. Null if unreachable
       * or entry node of the function. */
      SsaDef immDom = { };
      /* Immediate post-dominator. Null if a path
       * returns before merging. */
      SsaDef immPostDom = { };
      /* Block index for dominance masks */
      uint32_t index = 0u;
    } block;
  };

  struct BlockInfo {
    SsaDef block = { };
    uint32_t predIndex = 0u;
    uint32_t predCount = 0u;
    uint32_t succIndex = 0u;
    uint32_t succCount = 0u;
  };

  const Builder& m_builder;

  std::vector<BlockInfo> m_blocks;

  std::vector<SsaDef> m_startNodes;
  std::vector<SsaDef> m_exitNodes;

  std::vector<uint64_t> m_bitMaskDom;
  std::vector<uint64_t> m_bitMaskPostDom;

  std::vector<uint32_t> m_links;

  size_t m_nodeCount = 0u;
  size_t m_maskCount = 0u;

  /* Node properties. For labels, this stores the immediate dominator.
   * For all other instructions, crucially including block terminators,
   * this stores the block that the instruction belongs to. */
  Container<NodeInfo> m_nodeInfos;

  void initLinks();

  void initDominators();

  void computeDominators();

  void computePostDominators();

  SsaDef findImmediateDominator(SsaDef node);

  SsaDef findImmediatePostDominator(SsaDef node);

  int32_t findImmediateDominatorBit(const uint64_t* masks, uint32_t node) const;

  bool isDominatorBit(const uint64_t* masks, uint32_t base, uint32_t node) const;

  void initDominanceMask(uint64_t* masks, uint32_t node);

  bool mergeDominanceMasks(uint64_t* masks, uint32_t base, uint32_t node);

  std::pair<size_t, uint64_t> computeMaskIndex(uint32_t node) const;

  size_t computeBaseIndex(uint32_t node) const;

  size_t computeNodeIndex(size_t maskIndex, uint64_t mask) const;

  bool blockHasPredecessors(SsaDef block) const;

};

}
