#pragma once

#include "ir_builder.h"
#include "ir_dominance.h"

namespace dxbc_spv::ir {

class DivergenceAnalysis {

public:

  explicit DivergenceAnalysis(const Builder& builder, const DominanceGraph& dominance);

  ~DivergenceAnalysis();

  /** Queries largest scope at which a given definition is still uniform */
  Scope getUniformScopeForDef(SsaDef def) const {
    return def ? m_nodeScopes.at(def).uniformScope : Scope::eGlobal;
  }

  /** Checks whether a function returns the same result for the same
   *  inputs. information is trivially available for this pass. */
  bool functionIsPure(SsaDef function) const {
    return !m_nodeScopes.at(function).tainted;
  }

private:

  const Builder&        m_builder;
  const DominanceGraph& m_dominance;

  struct NodeInfo {
    Scope uniformScope = Scope::eGlobal;
    Scope callScope = Scope::eGlobal;
    bool tainted = false;
  };

  Container<NodeInfo> m_nodeScopes;

  SsaDef m_currentFunction = { };
  SsaDef m_currentBlock = { };

  SsaDef m_patchConstantFunction = { };

  ShaderStage m_stage = ShaderStage::eFlagEnum;

  uint32_t m_workgroupSizeX = 0u;
  uint32_t m_workgroupSizeY = 0u;
  uint32_t m_workgroupSizeZ = 0u;

  bool m_hasNonUniformBarrier = false;

  bool runAnalysisPass();

  Scope determineScope(const Op& op);

  Scope determineScopeForArgs(const Op& op);

  Scope determineScopeForArgsAndBlock(const Op& op);

  Scope determineScopeForInputLoad(const Op& op);

  Scope determineScopeForInput(const Op& op);

  Scope determineScopeForBuiltIn(const Op& op);

  Scope determineScopeForBlock(const Op& op);

  Scope getLoopExitScope(const Op& loopHeader);

  void taintFunction(SsaDef function);

  void gatherMetadata();

  bool adjustScopeForDef(SsaDef def, Scope scope) {
    auto& node = m_nodeScopes.at(def);

    if (node.uniformScope <= scope)
      return false;

    node.uniformScope = scope;
    return true;
  }

};

}
