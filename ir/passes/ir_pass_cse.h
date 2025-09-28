#pragma once

#include <unordered_set>

#include "../ir.h"
#include "../ir_builder.h"
#include "../ir_dominance.h"

#include "../../util/util_flags.h"

namespace dxbc_spv::ir {

/* Op classification */
enum class CseOpFlag : uint32_t {
  eHasSideEffects = (1u << 0),
  eCanDeduplicate = (1u << 1),

  eFlagEnum = 0u
};

using CseOpFlags = util::Flags<CseOpFlag>;


/** Common subexpression elimination pass. */
class CsePass {

public:

  struct Options {
    /* Whether relocating descriptor loads is safe */
    bool relocateDescriptorLoad = false;
  };

  CsePass(Builder& builder, const Options& options);

  ~CsePass();

  CsePass             (const CsePass&) = delete;
  CsePass& operator = (const CsePass&) = delete;

  /** Runs pass. */
  bool run();

  /** Initializes and runs pass on the given builder. */
  static bool runPass(Builder& builder, const Options& options);

private:

  Builder&  m_builder;
  Options   m_options;

  DominanceGraph m_dom;

  std::unordered_set<SsaDef> m_pureFunctions;

  bool    m_functionIsPure = false;
  SsaDef  m_functionDef = { };

  struct OpHash {
    size_t operator () (const Op& op) const;
  };

  struct OpEq {
    size_t operator () (const Op& a, const Op& b) const {
      return a.isEquivalent(b);
    }
  };

  std::unordered_multiset<Op, OpHash, OpEq> m_defs;

  CseOpFlags classifyOp(const Op& op) const;

  bool isTrivialOp(const Op& op) const;

};

}
