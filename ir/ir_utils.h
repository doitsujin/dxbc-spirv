#pragma once

#include <vector>

#include "ir.h"
#include "ir_builder.h"

namespace dxbc_spv::ir {

/** Checks whether an instruction is a branch */
inline bool isBranchInstruction(OpCode opCode) {
  return opCode == OpCode::eBranch ||
         opCode == OpCode::eBranchConditional ||
         opCode == OpCode::eSwitch;
}

/** Checks whether an instruction terminates a block */
inline bool isBlockTerminator(OpCode opCode) {
  return isBranchInstruction(opCode) ||
         opCode == OpCode::eReturn ||
         opCode == OpCode::eUnreachable;
}


/** Helper class for per-def look-up tables. Initializes a local
 *  array with the total def count of the given builder, and will
 *  dynamically add more entries as necessary. */
template<typename T>
class DefMetadata {

public:

  DefMetadata() = default;
  DefMetadata(const Builder& builder, T value = T())
  : m_data(builder.getDefCount(), value) { }

  T& operator [] (SsaDef def) {
    dxbc_spv_assert(def);

    if (def.getId() >= m_data.size())
      m_data.resize(def.getId() + 1u);

    return m_data[def.getId()];
  }

private:

  std::vector<T> m_data;

};

}
