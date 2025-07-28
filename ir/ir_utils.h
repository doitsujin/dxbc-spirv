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


/** For a branch instruction, iterates over branch targets. */
template<typename Proc>
void forEachBranchTarget(const Op& op, const Proc& proc) {
  switch (op.getOpCode()) {
    case OpCode::eBranch: {
      proc(SsaDef(op.getOperand(0u)));
    } break;

    case OpCode::eBranchConditional: {
      proc(SsaDef(op.getOperand(1u)));
      proc(SsaDef(op.getOperand(2u)));
    } break;

    case OpCode::eSwitch: {
      for (uint32_t i = 1u; i < op.getOperandCount(); i += 2u)
        proc(SsaDef(op.getOperand(i)));
    } break;

    default:
      return;
  }
}


/** For a phi op, iterate over all block -> value pairs */
template<typename Proc>
void forEachPhiOperand(const Op& op, const Proc& proc) {
  dxbc_spv_assert(op.getOpCode() == OpCode::ePhi);

  for (uint32_t i = 0u; i < op.getOperandCount(); i += 2u) {
    auto block = SsaDef(op.getOperand(i + 0u));
    auto value = SsaDef(op.getOperand(i + 1u));

    proc(block, value);
  }
}


/** Finds block containing an instruction. */
inline SsaDef findContainingBlock(const Builder& builder, SsaDef op) {
  while ((op = builder.getPrev(op))) {
    auto opCode = builder.getOp(op).getOpCode();

    if (opCode == OpCode::eLabel)
      return op;

    if (opCode == OpCode::eFunction || isBlockTerminator(opCode))
      break;
  }

  return SsaDef();
}


/** Removes instruction if it goes unused. If the instruction gets
 *  removed, this will return (true, next), where next is the next
 *  instruction in line. Otherwise, the return value is (false, def). */
inline std::pair<bool, SsaDef> removeIfUnused(Builder& builder, SsaDef def) {
  if (!builder.getUseCount(def))
    return std::make_pair(true, builder.remove(def));

  return std::make_pair(false, def);
}

/** Converts constant op using Convert* semantics */
Op convertConstant(const Op& op, BasicType dstType);

/** Converts constant op using Cast semantics */
Op castConstant(const Op& op, BasicType dstType);

/** Converts constant op using ConsumeAs semantics */
Op consumeConstant(const Op& op, BasicType dstType);


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

  const T& operator [] (SsaDef def) const {
    dxbc_spv_assert(def && def.getId() < m_data.size());
    return m_data[def.getId()];
  }

private:

  std::vector<T> m_data;

};

}
