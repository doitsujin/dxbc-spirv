#include <type_traits>

#include "ir_utils.h"

#include "../util/util_bit.h"

namespace dxbc_spv::ir {

void rewriteBlockInPhiUse(Builder& builder, SsaDef phi, SsaDef oldBlock, SsaDef newBlock) {
  const auto& oldPhi = builder.getOp(phi);

  dxbc_spv_assert(oldPhi.getOpCode() == OpCode::ePhi);
  dxbc_spv_assert(builder.getOp(oldBlock).getOpCode() == OpCode::eLabel);
  dxbc_spv_assert(!newBlock || builder.getOp(newBlock).getOpCode() == OpCode::eLabel);

  auto newPhi = Op(OpCode::ePhi, oldPhi.getType()).setFlags(oldPhi.getFlags());

  for (uint32_t i = 0u; i < oldPhi.getOperandCount(); i += 2u) {
    if (SsaDef(oldPhi.getOperand(i)) == oldBlock) {
      if (newBlock) {
        newPhi.addOperand(newBlock);
        newPhi.addOperand(oldPhi.getOperand(i + 1u));
      }
    } else {
      newPhi.addOperand(oldPhi.getOperand(i));
      newPhi.addOperand(oldPhi.getOperand(i + 1u));
    }
  }

  /* Resolve trivial phi right away */
  if (newPhi.getOperandCount() > 2u)
    builder.rewriteOp(phi, std::move(newPhi));
  else
    builder.rewriteDef(phi, SsaDef(newPhi.getOperand(1u)));
}


void rewriteBlockInPhiUses(Builder& builder, SsaDef oldBlock, SsaDef newBlock) {
  util::small_vector<SsaDef, 64u> uses;
  builder.getUses(oldBlock, uses);

  for (auto use : uses) {
    if (builder.getOp(use).getOpCode() == OpCode::ePhi)
      rewriteBlockInPhiUse(builder, use, oldBlock, newBlock);
  }
}


void rewriteBlockInPhiUsesInBlock(Builder& builder, SsaDef targetBlock, SsaDef oldBlock, SsaDef newBlock) {
  util::small_vector<SsaDef, 64u> uses;
  builder.getUses(oldBlock, uses);

  for (auto use : uses) {
    if (builder.getOp(use).getOpCode() == OpCode::ePhi &&
        findContainingBlock(builder, use) == targetBlock)
      rewriteBlockInPhiUse(builder, use, oldBlock, newBlock);
  }
}


template<typename T>
std::pair<uint64_t, double> normalizeConstantLiteral(Operand src) {
  if constexpr (std::is_integral_v<T>) {
    T i = T(src);
    return std::make_pair(uint64_t(i), double(i));
  } else {
    double d = double(T(src));
    return std::make_pair(uint64_t(int64_t(d)), d);
  }
}


ScalarType normalizeTypeForConsume(ScalarType type) {
  switch (type) {
    case ScalarType::eF16:
    case ScalarType::eF32:
    case ScalarType::eMinF16:
      return ScalarType::eF32;

    case ScalarType::eF64:
      return ScalarType::eF64;

    case ScalarType::eI8:
    case ScalarType::eI16:
    case ScalarType::eI32:
    case ScalarType::eMinI16:
      return ScalarType::eI32;

    case ScalarType::eI64:
      return ScalarType::eI64;

    case ScalarType::eUnknown:
    case ScalarType::eU8:
    case ScalarType::eU16:
    case ScalarType::eU32:
    case ScalarType::eMinU16:
      return ScalarType::eU32;

    case ScalarType::eU64:
      return ScalarType::eU64;

    default:
      dxbc_spv_unreachable();
      return ScalarType::eUnknown;
  }
}


BasicType normalizeTypeForConsume(BasicType type) {
  return BasicType(normalizeTypeForConsume(type.getBaseType()), type.getVectorSize());
}


Op convertConstant(const Op& op, BasicType dstType) {
  dxbc_spv_assert(op.getType().isBasicType());
  auto srcType = op.getType().getBaseType(0u);

  dxbc_spv_assert(srcType.getVectorSize() == dstType.getVectorSize());

  if (srcType == dstType)
    return op;

  Op result(OpCode::eConstant, dstType);

  for (uint32_t i = 0u; i < dstType.getVectorSize(); i++) {
    auto src = op.getOperand(i);

    auto [normalizedInt, normalizedFloat] = [src, srcType] {
      switch (srcType.getBaseType()) {
        case ScalarType::eBool: {
          bool v = uint64_t(src) != 0u;
          return std::make_pair(uint64_t(v ? 1u : 0u), double(v ? 1.0 : 0.0));
        }
        case ScalarType::eI8:
          return normalizeConstantLiteral<int8_t>(src);
        case ScalarType::eI16:
          return normalizeConstantLiteral<int16_t>(src);
        case ScalarType::eMinI16:
        case ScalarType::eI32:
          return normalizeConstantLiteral<int32_t>(src);
        case ScalarType::eI64:
          return normalizeConstantLiteral<int64_t>(src);
        case ScalarType::eU8:
          return normalizeConstantLiteral<uint8_t>(src);
        case ScalarType::eU16:
          return normalizeConstantLiteral<uint16_t>(src);
        case ScalarType::eMinU16:
        case ScalarType::eUnknown:
        case ScalarType::eU32:
          return normalizeConstantLiteral<uint32_t>(src);
        case ScalarType::eU64:
          return normalizeConstantLiteral<uint64_t>(src);
        case ScalarType::eF16:
          return normalizeConstantLiteral<float16_t>(src);
        case ScalarType::eMinF16:
        case ScalarType::eF32:
          return normalizeConstantLiteral<float>(src);
        case ScalarType::eF64:
          return normalizeConstantLiteral<double>(src);
        default:
          dxbc_spv_unreachable();
          return std::make_pair(uint64_t(0), double(0.0));
      }
    } ();

    auto operand = [dstType, normalizedInt, normalizedFloat] {
      switch (dstType.getBaseType()) {
        case ScalarType::eBool:
          return Operand(normalizedInt ? 1u : 0u);
        case ScalarType::eI8:
          return Operand(int8_t(normalizedInt));
        case ScalarType::eI16:
          return Operand(int16_t(normalizedInt));
        case ScalarType::eMinI16:
        case ScalarType::eI32:
          return Operand(int32_t(normalizedInt));
        case ScalarType::eI64:
          return Operand(int64_t(normalizedInt));
        case ScalarType::eU8:
          return Operand(uint8_t(normalizedInt));
        case ScalarType::eU16:
          return Operand(uint16_t(normalizedInt));
        case ScalarType::eUnknown:
        case ScalarType::eMinU16:
        case ScalarType::eU32:
          return Operand(uint32_t(normalizedInt));
        case ScalarType::eU64:
          return Operand(uint64_t(normalizedInt));
        case ScalarType::eF16:
          return Operand(float16_t(normalizedFloat));
        case ScalarType::eMinF16:
        case ScalarType::eF32:
          return Operand(float(normalizedFloat));
        case ScalarType::eF64:
          return Operand(double(normalizedFloat));
        default:
          dxbc_spv_unreachable();
          return Operand();
      }
    } ();

    result.addOperand(operand);
  }

  return result;
}


Op castConstant(const Op& op, BasicType dstType) {
  dxbc_spv_assert(op.getType().isBasicType());
  auto srcType = op.getType().getBaseType(0u);

  dxbc_spv_assert(dstType.byteSize() == srcType.byteSize());

  if (srcType == dstType)
    return op;

  Op result(OpCode::eConstant, dstType);

  if (srcType.getVectorSize() > dstType.getVectorSize()) {
    /* Construct scalars from multiple source operands */
    uint32_t factor = srcType.getVectorSize() / dstType.getVectorSize();
    uint32_t bitCount = 8u * byteSize(srcType.getBaseType());

    for (uint32_t i = 0u; i < dstType.getVectorSize(); i++) {
      uint64_t dstLiteral = 0u;

      for (uint32_t j = 0u; j < factor; j++) {
        uint64_t srcLiteral = util::bextract(uint64_t(op.getOperand(i * factor + j)), 0u, bitCount);
        dstLiteral |= srcLiteral << (bitCount * j);
      }

      result.addOperand(dstLiteral);
    }
  } else if (srcType.getVectorSize() < dstType.getVectorSize()) {
    /* Construct multiple scalars from one source operand */
    uint32_t factor = dstType.getVectorSize() / srcType.getVectorSize();
    uint32_t bitCount = 8u * byteSize(dstType.getBaseType());

    for (uint32_t i = 0u; i < srcType.getVectorSize(); i++) {
      uint64_t srcLiteral = uint64_t(op.getOperand(i));

      for (uint32_t j = 0u; j < factor; j++)
        result.addOperand(util::bextract(srcLiteral, bitCount * j, bitCount));
    }
  } else {
    /* Same vector size, copy operands as they are */
    for (uint32_t i = 0u; i < op.getOperandCount(); i++)
      result.addOperand(op.getOperand(i));
  }

  /* Mask or sign-extend operands as necessary */
  for (uint32_t i = 0u; i < result.getOperandCount(); i++) {
    auto scalarType = result.getType().getBaseType(0u);

    uint64_t sign = uint64_t(1u) << ((8u * scalarType.byteSize()) - 1u);
    uint64_t mask = sign | (sign - 1u);

    uint64_t value = uint64_t(result.getOperand(i));
    value &= mask;

    if (scalarType.isSignedIntType())
      value |= -(value & sign);

    result.setOperand(i, Operand(value));
  }

  return result;
}


Op consumeConstant(const Op& op, BasicType dstType) {
  dxbc_spv_assert(op.getType().isBasicType());
  auto srcType = op.getType().getBaseType(0u);

  dxbc_spv_assert(srcType.getVectorSize() == dstType.getVectorSize());

  auto srcNormalized = normalizeTypeForConsume(srcType);
  auto dstNormalized = normalizeTypeForConsume(dstType);

  Op result = convertConstant(op, srcNormalized);
  result = castConstant(result, dstNormalized);
  result = convertConstant(result, dstType);

  return result;
}

}
