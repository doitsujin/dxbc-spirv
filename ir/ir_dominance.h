#pragma once

#include <unordered_map>

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

  /* Queries containing block for a definition. */
  SsaDef getBlockForDef(SsaDef def) const {
    return m_nodeInfos.at(def).blockDef;
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
      /* Points to a loop header if alls paths
       * lead back to it */
      SsaDef continueLoop = { };
    } block;

  };

  const Builder& m_builder;

  /* Node properties. For labels, this stores the immediate dominator.
   * For all other instructions, crucially including block terminators,
   * this stores the block that the instruction belongs to. */
  Container<NodeInfo> m_nodeInfos;

  SsaDef computeImmediateDominator(SsaDef block) const;

  std::pair<SsaDef, SsaDef> computeImmediatePostDominator(SsaDef block) const;

  SsaDef getContinueLoop(SsaDef block, SsaDef target) const;

  SsaDef findContainingLoop(SsaDef block) const;

};

}
