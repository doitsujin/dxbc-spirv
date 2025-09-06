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

  bool promoteScratchCbvCopy(SsaDef def);

  CbvInfo getCbvCopyMapping(const Op& op);

  SsaDef emitScratchCbvFunction(SsaDef def, const CbvInfo& baseStore, uint32_t baseIndex, uint32_t storeMask);

  SsaDef extractCbvArrayIndex(const Op& op);

  SsaDef extractCbvComponent(const Op& op);

  bool hasConstantIndexOffset(SsaDef baseIndex, SsaDef index, uint32_t offset) const;

  std::pair<SsaDef, uint64_t> extractBaseAndOffset(const Op& op) const;

  std::string getScratchCbvFunctionName(SsaDef def) const;

};

}
