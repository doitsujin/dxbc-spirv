#pragma once

#include <vector>

#include "ir_builder.h"

namespace dxbc_spv::ir {

/** Shader serializer. */
class Serializer {

public:

  Serializer(const Builder& builder);

  /** Computes serialized size, in bytes. */
  size_t computeSerializedSize() const;

  /** Serializes shader into pre-allocated byte array. */
  bool serialize(uint8_t* data, size_t size) const;

private:

  const Builder& m_builder;
  uint32_t m_opCount = 0u;

  std::vector<uint32_t> m_defMap = { };

  size_t computeEncodedTypeSize(const Type& type) const;

  size_t computeEncodedOpTokenSize(const Op& op) const;

  size_t computeEncodedOpSize(const Op& op) const;

  int64_t computeRelativeDef(uint32_t opIndex, SsaDef argDef) const;

  bool encodeType(const Type& type, uint8_t* dstData, size_t dstSize, size_t& dstOffset) const;

  bool encodeOp(const Op& op, uint8_t* dstData, size_t dstSize, size_t& dstOffset) const;

  uint64_t getOpToken(const Op& op) const;

  uint64_t getTypeHeaderToken(const Type& type) const;

  template<typename T>
  static bool writeBytes(uint8_t* dstData, size_t dstSize, size_t& dstOffset, T data);

  static bool writeVle(uint8_t* dstData, size_t dstSize, size_t& dstOffset, uint64_t sym);

};


/** Deserializer. Note that this is stateful and can only be
 *  used to deserialize a shader binary once. */
class Deserializer {

public:

  Deserializer(const uint8_t* data, size_t size);

  /** Deserializes total op count. This is the first token in the
   *  serialized binary and must be read before any actual ops.
   *  Any SSA IDs produced by the deserializer are guaranteed to
   *  be less than or equal to the op count. */
  bool deserializeOpCount(uint32_t& count);

  /** Deserializes next instruction. Assumes that binary data has
   *  already been processed successfully. The provided SSA def is
   *  the expected ID for the instruction and affects decoding of
   *  operands. */
  bool deserializeOp(Op& op, SsaDef def);

  /** Deserializes remaining binary into builder. */
  bool deserialize(Builder& builder);

  /** Checks whether the end has been reached */
  bool atEnd() const {
    return m_offset == m_size;
  }

private:

  const uint8_t* m_data = nullptr;
  size_t m_size   = 0u;
  size_t m_offset = 0u;

  bool deserializeType(Type& type);

  template<typename T>
  bool readBytes(T& data);

  bool readVle(uint64_t& sym);

};

}
