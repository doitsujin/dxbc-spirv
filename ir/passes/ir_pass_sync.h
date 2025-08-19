#pragma once

#include "../ir.h"
#include "../ir_builder.h"

namespace dxbc_spv::ir {

/** Pass to optimize or fix up synchronization for resources
 *  as well as workgroup memory in compute shaders. */
class SyncPass {

public:

  struct Options {
    /** Whether to insert scoped locks for ROVs. Has no effect
     *  if no rasterizer-ordered resources are present. */
    bool insertRovLocks = true;
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

  void processAtomicsAndBarriers();

  Builder::iterator handleAtomic(Builder::iterator op);

  Builder::iterator handleBarrier(Builder::iterator op, Scope uavScope);

  void insertRovLocks();

  bool hasSharedVariables() const;

  bool hasRovResources() const;

  Scope getUavMemoryScope() const;

};

}
