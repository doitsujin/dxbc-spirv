#pragma once

#include <unordered_set>

#include "../ir.h"
#include "../ir_builder.h"
#include "../ir_dominance.h"

namespace dxbc_spv::ir {

/** Common subexpression elimination pass. */
class CsePass {

public:

  CsePass(Builder& builder);

  ~CsePass();

  CsePass             (const CsePass&) = delete;
  CsePass& operator = (const CsePass&) = delete;

  /** Runs pass. */
  bool run();

  /** Initializes and runs pass on the given builder. */
  static bool runPass(Builder& builder);

private:

  Builder& m_builder;

  DominanceGraph m_dom;

  struct OpHash {
    size_t operator () (const Op& op) const;
  };

  struct OpEq {
    size_t operator () (const Op& a, const Op& b) const {
      return a.isEquivalent(b);
    }
  };

  std::unordered_multiset<Op, OpHash, OpEq> m_defs;

  bool canDeduplicateOp(const Op& op) const;

};

}
