#pragma once

#include "../ir.h"

namespace dxbc_spv::ir {

/** Block properties */
struct SsaPassBlockState {
  /* Block is considered 'filled' and can provide
   * variable definitions for its successors */
  bool isFilled = false;
  /* Block is considered 'sealed' and can provide variable
   * definitions for its predecessors. This is the case when
   * all its predecessors are 'filled'. */
  bool isSealed = false;
};


/** Key to look up temporary definitions per block. */
struct SsaPassTempKey {
  SsaPassTempKey() = default;
  SsaPassTempKey(SsaDef b, SsaDef v)
  : block(b), var(v) { }

  SsaDef block  = { };
  SsaDef var    = { };

  bool operator == (const SsaPassTempKey& other) const { return block == other.block && var == other.var; }
  bool operator != (const SsaPassTempKey& other) const { return block != other.block || var != other.var; }
};

}

namespace std {

template<>
struct hash<dxbc_spv::ir::SsaPassTempKey> {
  size_t operator () (const dxbc_spv::ir::SsaPassTempKey& k) const {
    return dxbc_spv::util::hash_combine(
      std::hash<dxbc_spv::ir::SsaDef>()(k.block),
      std::hash<dxbc_spv::ir::SsaDef>()(k.var));
  }
};

}
