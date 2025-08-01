#pragma once

#include "../ir.h"

#include "../util/util_hash.h"

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

  bool operator == (const SsaPassTempKey& other) const { return var == other.var && block == other.block; }
  bool operator != (const SsaPassTempKey& other) const { return var != other.var && block != other.block; }
};

}

namespace std {

template<>
struct hash<dxbc_spv::ir::SsaPassTempKey> {
  size_t operator () (const dxbc_spv::ir::SsaPassTempKey& k) const {
    std::hash<dxbc_spv::ir::SsaDef> hash;
    return dxbc_spv::util::hash_combine(hash(k.block), hash(k.var));
  }
};

}
