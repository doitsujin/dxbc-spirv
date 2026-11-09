#pragma once

#include <optional>

#include "../ir.h"
#include "../ir_builder.h"

namespace dxbc_spv::ir {

/* Pass to optimize patterns where buffer access is unrolled but
 * conditionally accessed back into a dynamically indexed load. */
class BufferIndexingPass {

public:

  struct Options {
    /** Whether buffer indexing is safe and robust. */
    bool allowDynamicIndexing = false;
  };

  BufferIndexingPass(Builder& builder, const Options& options);

  ~BufferIndexingPass();

  BufferIndexingPass             (const BufferIndexingPass&) = delete;
  BufferIndexingPass& operator = (const BufferIndexingPass&) = delete;

  bool run();

  static bool runPass(Builder& builder, const Options& options);

private:

  struct LoadInfo {
    SsaDef    selectOp      = {};
    SsaDef    selectArg     = {};
    SsaDef    castOp        = {};
    SsaDef    extractOp     = {};
  };

  struct IndexedLoad {
    SsaDef    selectOp      = {};
    uint32_t  selectArg     = 0u;
    SsaDef    compareVar    = {};
    uint32_t  compareValue  = 0u;
    SsaDef    loadOp        = {};
    SsaDef    loadExtract   = {};
    SsaDef    indexBase     = {};
    uint32_t  indexOffset   = 0u;

    bool canMergeWithStride(const IndexedLoad& other, uint32_t stride) const;

    bool isMergeCandidate(const IndexedLoad& other) const;

    uint32_t computeStride(const IndexedLoad& other) const;

    static bool compare(const IndexedLoad& a, const IndexedLoad& b);
  };

  Builder&  m_builder;
  Options   m_options;

  uint32_t getOptimalStride(size_t count, const IndexedLoad* loads);

  bool optimizeBuffer(SsaDef descriptor);

  bool rewriteLoadAsIndexed(const IndexedLoad& load, uint32_t stride);

  small_vector<SsaDef, 256> getBuffers();

  void getLoadSelectInfos(SsaDef def, LoadInfo base, small_vector<LoadInfo, 1024>& loads) const;

  small_vector<IndexedLoad, 256> getIndexedLoads(SsaDef descriptor);

};

}
