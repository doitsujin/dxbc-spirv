#pragma once

#include <optional>
#include <unordered_map>

#include "../ir.h"
#include "../ir_builder.h"
#include "../ir_dominance.h"
#include "../ir_divergence.h"

namespace dxbc_spv::ir {

/** Pass to optimize or fix up synchronization for resources
 *  as well as workgroup memory in compute shaders. */
class SyncPass {

public:

  struct Options {
    /** Whether to insert scoped locks for ROVs. Has no effect
     *  if no rasterizer-ordered resources are present. */
    bool insertRovLocks = true;
    /** Whether to insert barriers for potentially unsynchronized
     *  LDS accesses. May negatively impact performance. */
    bool insertLdsBarriers = false;
    /** Whether to insert barriers for potentially unsynchronized
     *  UAV accesses. May negatively impact performance. */
    bool insertUavBarriers = false;
  };

  SyncPass(Builder& builder, const Options& options);

  ~SyncPass();

  SyncPass             (const SyncPass&) = delete;
  SyncPass& operator = (const SyncPass&) = delete;

  void run();

  static void runPass(Builder& builder, const Options& options);

private:

  Builder&  m_builder;
  Options   m_options = { };

  SsaDef      m_entryPoint = { };
  ShaderStage m_stage = { };

  std::optional<DominanceGraph> m_dominance;
  std::optional<DivergenceAnalysis> m_divergence;

  std::unordered_map<SsaDef, MemoryTypeFlags> m_blockBarriers;

  uint32_t m_insertedBarrierCount = 0u;

  void processAtomicsAndBarriers();

  Builder::iterator handleAtomic(Builder::iterator op);

  Builder::iterator handleBarrier(Builder::iterator op, Scope uavScope);

  void insertRovLocks();

  void insertBarriers();

  void insertLdsBarriers(SsaDef variable);

  void insertUavBarriers(SsaDef variable);

  void resolveHazard(const Op& srcAccess, const Op& dstAccess, MemoryType type);

  MemoryTypeFlags scanBlockBarriers(SsaDef block);

  void addBlockBarrier(SsaDef block, MemoryTypeFlags types);

  void insertBarrierBefore(SsaDef ref);

  bool hasSharedVariables() const;

  bool hasRovResources() const;

  Scope getUavMemoryScope() const;

  static bool barrierSynchronizes(const Op& barrier, MemoryType type);

};

}
