#pragma once

#include <optional>

#include "../ir.h"
#include "../ir_builder.h"
#include "../ir_dominance.h"

namespace dxbc_spv::ir {

class CleanupScratchPass {

public:

  struct Options {
    /* Whether to bound-check scratch reads and writes. If there are any
     * potential out-of-bounds accesses, this will increase the size of
     * such arrays by 1 in each dimension and clamp the index, as well
     * as ensure that the last element will always contain a 0 value. */
    bool enableBoundChecking = true;
    /* Whether to optimize constant buffer to scratch array copies. */
    bool resolveCbvCopy = true;
    /* Whether to unpack arrays that are only accessed via constant
     * indices. These arrays can be trivially converted to SSA form. */
    bool unpackConstantIndexedArrays = true;
    /* Whether to unpack small arrays with dynamic indexing. The
     * size thresholds are defined below. */
    bool unpackSmallArrays = true;
    /* Maximum array size for arrays using dynamically indexed loads or
     * stores, respectively. If an array has both, then the store limit
     * takes precedence and should be the smaller of the two limits. */
    uint32_t maxUnpackedDynamicLoadArraySize = 8u;
    uint32_t maxUnpackedDynamicStoreArraySize = 2u;
  };

  CleanupScratchPass(Builder& builder, const Options& options);

  ~CleanupScratchPass();

  CleanupScratchPass             (const CleanupScratchPass&) = delete;
  CleanupScratchPass& operator = (const CleanupScratchPass&) = delete;

  /* Runs pass to optimize away constant buffer to scratch arrray copies
   * in such a way that the constant buffer is accessed directly. This
   * pass must be run before consume instructions are lowered. */
  void propagateCbvScratchCopies();

  static void runResolveCbvToScratchCopyPass(Builder& builder, const Options& options);

  /* Optimizes small scalar or vector arrays with constant store indices to
   * use temporaries and if-ladders for loads. May invoke SSA construction. */
  void unpackArrays();

  static void runUnpackArrayPass(Builder& builder, const Options& options);

  /* Runs pass to enable bound-checking on scratch variables. Should be
   * run last in order to not replace constant indices with non-constants. */
  void enableBoundChecking();

  static void runBoundCheckingPass(Builder& builder, const Options& options);

  /* Runs all passes in a sensible order */
  static void runPass(Builder& builder, const Options& options);

private:

  Builder&  m_builder;
  Options   m_options = { };

  Container<SsaDef> m_functionsForDef;

  struct CbvInfo {
    SsaDef cbv = { };
    SsaDef cbvIndex = { };
    SsaDef cbvOffset = { };
    uint32_t cbvComponent = { };
  };

  struct IndexInfo {
    SsaDef indexDef = { };
    uint32_t index = 0u;
    uint32_t component = 0u;
  };

  bool promoteScratchCbvCopy(SsaDef def);

  CbvInfo getCbvCopyMapping(const Op& op);

  SsaDef emitScratchCbvFunction(SsaDef def, const CbvInfo& baseStore, uint32_t baseIndex, uint32_t storeMask);

  SsaDef extractCbvArrayIndex(const Op& op);

  SsaDef extractCbvComponent(const Op& op);

  bool hasConstantIndexOffset(SsaDef baseIndex, SsaDef index, uint32_t offset) const;

  bool tryUnpackArray(SsaDef def);

  bool unpackArray(SsaDef def);

  void rewriteScratchLoadToTemp(const Op& loadOp, size_t tempCount, const SsaDef* temps);

  void rewriteScratchStoreToTemp(const Op& storeOp, size_t tempCount, const SsaDef* temps);

  void determineFunctionForDefs();

  bool boundCheckScratchArray(SsaDef def);

  std::pair<SsaDef, uint64_t> extractBaseAndOffset(const Op& op) const;

  std::string getScratchCbvFunctionName(SsaDef def) const;

  bool allStoreIndicesConstant(SsaDef def) const;

  bool allLoadIndicesConstant(SsaDef def) const;

  bool isConstantIndex(const Op& op) const;

  IndexInfo extractConstantIndex(const Op& op) const;

};

}
