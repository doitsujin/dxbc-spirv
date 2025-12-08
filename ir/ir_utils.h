#pragma once

#include <vector>

#include "ir.h"
#include "ir_builder.h"

#include "../util/util_swizzle.h"

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


/** Finds block terminator for given instruction */
inline SsaDef findBlockTerminator(const Builder& builder, SsaDef op) {
  while (op && !isBlockTerminator(builder.getOp(op).getOpCode()))
    op = builder.getNext(op);

  return op;
}


/** Removes instruction if it goes unused. If the instruction gets
 *  removed, this will return (true, next), where next is the next
 *  instruction in line. Otherwise, the return value is (false, def). */
inline std::pair<bool, SsaDef> removeIfUnused(Builder& builder, SsaDef def) {
  if (!builder.getUseCount(def))
    return std::make_pair(true, builder.remove(def));

  return std::make_pair(false, def);
}


/** Checks whether a given instruction is the only non-debug user of
 *  an instruction. */
inline bool isOnlyUse(const Builder& builder, SsaDef def, SsaDef use) {
  auto [a, b] = builder.getUses(def);

  for (auto i = a; i != b; i++) {
    if (i->getDef() != use && !i->isDeclarative())
      return false;
  }

  return true;
}


/** Rewrites a block reference in all phis to a different block */
void rewriteBlockInPhiUses(Builder& builder, SsaDef oldBlock, SsaDef newBlock);

/** Rewrites a block reference in a specific block to a different block */
void rewriteBlockInPhiUsesInBlock(Builder& builder, SsaDef targetBlock, SsaDef oldBlock, SsaDef newBlock);


/** Normalizes sub-dword basic scalar or vector types to 32-bit for
 *  use in consume operations. */
ScalarType normalizeTypeForConsume(ScalarType type);
BasicType normalizeTypeForConsume(BasicType type);


/** Converts constant op using Convert* semantics */
Op convertConstant(const Op& op, BasicType dstType);

/** Converts constant op using Cast semantics */
Op castConstant(const Op& op, BasicType dstType);

/** Converts constant op using ConsumeAs semantics */
Op consumeConstant(const Op& op, BasicType dstType);

SsaDef broadcastScalar(Builder& builder, SsaDef def, util::WriteMask mask);

SsaDef swizzleVector(Builder& builder, SsaDef value, util::Swizzle swizzle, util::WriteMask writeMask);

SsaDef composite(Builder& builder, BasicType type,
  const SsaDef* components, util::Swizzle swizzle, util::WriteMask mask);

SsaDef buildVector(Builder& builder, ScalarType scalarType, size_t count, const SsaDef* scalars);

SsaDef extractFromVector(Builder& builder, SsaDef def, uint32_t component);

bool is64BitType(BasicType type);

inline BasicType makeVectorType(ScalarType type, util::WriteMask mask) {
  uint8_t shift = is64BitType(type) ? 1u : 0u;
  return BasicType(type, util::popcnt(uint8_t(mask)) >> shift);
}

template<typename T>
SsaDef makeTypedConstant(Builder& builder, BasicType type, T value) {
  Op op(OpCode::eConstant, type);

  Operand scalar = [type, value] {
    switch (type.getBaseType()) {
      case ScalarType::eBool: return Operand(bool(value));
      case ScalarType::eU8:   return Operand(uint8_t(value));
      case ScalarType::eU16:  return Operand(uint16_t(value));
      case ScalarType::eMinU16:
      case ScalarType::eU32:  return Operand(uint32_t(value));
      case ScalarType::eU64:  return Operand(uint64_t(value));
      case ScalarType::eI8:   return Operand(int8_t(value));
      case ScalarType::eI16:  return Operand(int16_t(value));
      case ScalarType::eMinI16:
      case ScalarType::eI32:  return Operand(int32_t(value));
      case ScalarType::eI64:  return Operand(int64_t(value));
      case ScalarType::eF16:  return Operand(util::float16_t(value));
      case ScalarType::eMinF16:
      case ScalarType::eF32:  return Operand(float(value));
      case ScalarType::eF64:  return Operand(double(value));
      default: break;
    }

    dxbc_spv_unreachable();
    return Operand();
  } ();

  for (uint32_t i = 0u; i < type.getVectorSize(); i++)
    op.addOperand(scalar);

  return builder.add(std::move(op));
}

}
