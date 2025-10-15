#pragma once

#include "../ir.h"

#include "../../util/util_hash.h"

namespace dxbc_spv::ir {

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


/** Exit phi properties for a definition */
struct SsaExitPhiState {
  SsaDef loopHeader = { };
  SsaDef loopMerge  = { };
  SsaDef exitPhi    = { };
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
