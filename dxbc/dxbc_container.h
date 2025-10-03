#pragma once

#include <array>

#include "../util/util_byte_stream.h"
#include "../util/util_fourcc.h"
#include "../util/util_md5.h"
#include "../util/util_small_vector.h"

namespace dxbc_spv::dxbc {

class Builder;
class Signature;

/** Header of the contrainer file */
struct FileHeader {
  /* Always 'DXBC' */
  util::FourCC magic = util::FourCC("DXBC");
  /* MD5 digest */
  util::md5::Digest hash = { };
  /* Always 1 */
  uint32_t version = 1u;
  /* Total file size, in bytes */
  uint32_t fileSize = 0u;
  /* Number of chunks */
  uint32_t chunkCount = 0u;
};


/** Header of each chunk */
struct ChunkHeader {
  ChunkHeader() = default;

  explicit ChunkHeader(util::ByteReader& reader);

  /* Chunk identifier */
  util::FourCC tag = { };
  /* Chunk size, in bytes */
  uint32_t size = 0u;

  /** Writes chunk header to byte array */
  bool write(util::ByteWriter& writer) const;

  /** Checks whether chunk header is valid */
  explicit operator bool () const {
    return tag != util::FourCC();
  }
};


/** Chunk entry */
struct ChunkEntry {
  /* Chunk identifier */
  util::FourCC tag = { };
  /* Chunk offset and size, in bytes */
  uint32_t offset = 0u;
  uint32_t size = 0u;
};


/** Parser for the container format */
class Container {

public:

  explicit Container(const void* data, size_t size)
  : Container(util::ByteReader(data, size)) { }

  explicit Container(util::ByteReader reader);

  /** Looks up chunk by its four-character code and returns a
   *  byte reader covering the chunk's memory region, if any. */
  util::ByteReader findChunk(util::FourCC tag) const;

  /** Finds input signature chunk */
  util::ByteReader getInputSignatureChunk() const;

  /** Finds output signature chunk */
  util::ByteReader getOutputSignatureChunk() const;

  /** Finds patch constant signature chunk */
  util::ByteReader getPatchConstantSignatureChunk() const;

  /** Finds shader code chunk */
  util::ByteReader getCodeChunk() const;

  /** Finds interface chunk */
  util::ByteReader getInterfaceChunk() const;

  /** Checks whether the binary hash is valid */
  bool validateHash() const;

  /** Retrieves MD5 hash of the shader. */
  util::md5::Digest getHash() const {
    return m_header.hash;
  }

  static bool checkFourCC(util::ByteReader reader);

  /** Checks whether parser successfully parsed header and
   *  chunk metadata */
  explicit operator bool () const {
    return bool(m_reader);
  }

private:

  util::ByteReader  m_reader;
  FileHeader        m_header = { };

  util::small_vector<ChunkEntry, 16u> m_chunks;

  bool parseHeader();

  void resetOnError();

};


/** Info for building DXBC containers with all
 *  the chunk types currently implemented. */
struct ContainerInfo {
  /** Input signature, not optional */
  const Signature* inputSignature = nullptr;
  /** Patch constant signature for tessellation shaders */
  const Signature* patchSignature = nullptr;
  /** Output signature, not optional */
  const Signature* outputSignature = nullptr;
  /** Code chunk, not optional */
  const Builder* code = nullptr;
};


/** Function to build a DXBC container. This will insert a header with a
 *  valid hash, and chain the given non-null chunks into the binary. */
bool buildContainer(util::ByteWriter& writer, const ContainerInfo& info);

}
