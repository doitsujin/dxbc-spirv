#pragma once

#include <optional>

#include "../ir_builder.h"
#include "../ir_dominance.h"
#include "../ir_divergence.h"

namespace dxbc_spv::ir {

/** Pass to deduplicate and optimize indexed descriptors. */
class DescriptorIndexingPass {

public:

  struct Options {
    /** Whether to optimize conditional access to discrete SRV bindings to
     *  a descriptor array. This works if resource access instructions are
     *  identical except for the accessed resource or sampler. */
    bool optimizeDescriptorIndexing = false;
  };

  DescriptorIndexingPass(Builder& builder, const Options& options);

  ~DescriptorIndexingPass();

  bool run();

  bool deduplicateBindings();

  static bool runPass(Builder& builder, const Options& options);

  static bool runDeduplicateBindingPass(Builder& builder, const Options& options);

private:

  struct ResourceAccessInfo {
    SsaDef    baseResource = { };
    SsaDef    baseBlock = { };
    Op        accessOp = { };
    SsaDef    indexVar = { };
    int64_t   indexLo = 0u;
    int64_t   indexHi = 0u;
    uint32_t  regSpace = 0u;
    uint32_t  regIndex = 0u;
    std::array<SsaDef, 4u> phi = { };
    std::array<SsaDef, 4u> fallback = { };
  };

  Builder&  m_builder;
  Options   m_options;

  std::optional<DominanceGraph> m_dominance;
  std::optional<DivergenceAnalysis> m_divergence;

  std::vector<ResourceAccessInfo> m_indexableInfo;

  void rewriteIndexedResource(const ResourceAccessInfo& info);

  bool mergeBindings(const Op& a, const Op& b);

  bool rewriteDescriptorLoads(const Op& oldBinding, const Op& newBinding, uint32_t descriptorIndex);

  bool validateAccessEntry(const ResourceAccessInfo& info);

  void gatherIndexableResourceInfos();

  void gatherIndexableInfoForPhi(const Op& op);

  void gatherIndexableInfoForIf(const Op& phi, const Op& branch, SsaDef block);

  void gatherIndexableInfoForSwitch(const Op& phi, const Op& branch, SsaDef block);

  bool isEligibleAccessOp(const Op& op) const;

  bool isEquivalentAccessOp(const Op& a, const Op& b) const;

  bool isEquivalentResource(const Op& a, const Op& b) const;

  std::pair<SsaDef, uint32_t> extractAccessOp(const Op& op) const;

  SsaDef getResourceForAccessOp(const Op& op) const;

  void addIndexableInfo(const ResourceAccessInfo& info);

  void ensureDominanceInfo();

  void ensureDivergenceInfo();

  std::string getDebugName(SsaDef def);

  static SsaDef findPhiValue(const Op& op, SsaDef block);

};

}
