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

  struct IndexedLoad {
    SsaDef    selectOp      = {};
    SsaDef    compareVar    = {};
    uint32_t  compareValue  = {};
    SsaDef    loadOp        = {};
    SsaDef    indexBase     = {};
    uint32_t  indexOffset   = {};

    bool isNextOf(const IndexedLoad& other) const;

    static bool compare(const IndexedLoad& a, const IndexedLoad& b);
  };

  Builder&  m_builder;
  Options   m_options;

  bool optimizeBuffer(SsaDef descriptor);

  bool rewriteLoadAsIndexed(const IndexedLoad& load, uint32_t stride);

  small_vector<SsaDef, 256> getBuffers();

  small_vector<IndexedLoad, 256> getIndexedLoads(SsaDef descriptor);

};

}
