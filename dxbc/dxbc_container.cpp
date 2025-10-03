#include "dxbc_container.h"
#include "dxbc_parser.h"
#include "dxbc_signature.h"

#include "../util/util_log.h"
#include "../util/util_md5.h"
#include "../util/util_small_vector.h"

namespace dxbc_spv::dxbc {

util::md5::Digest hashDxbcBinary(const void* data, size_t size) {
  constexpr size_t BlockSize = 64u;

  /* Skip initial part of the header including the hash digest */
  size_t offset = offsetof(FileHeader, version);

  if (size < offset)
    return util::md5::Digest();

  auto bytes = reinterpret_cast<const unsigned char*>(data) + offset;
  size -= offset;

  /* Compute byte representations of the bit count and a derived
   * number that will be appended to the stream */
  const uint32_t aNum = uint32_t(size) * 8u;
  const uint32_t bNum = (aNum >> 2u) | 1u;

  std::array<uint8_t, sizeof(uint32_t)> a = { };
  std::array<uint8_t, sizeof(uint32_t)> b = { };

  for (uint32_t i = 0u; i < sizeof(uint32_t); i++) {
    a[i] = util::bextract(aNum, 8u * i, 8u);
    b[i] = util::bextract(bNum, 8u * i, 8u);
  }

  /* Hash remaining header and all chunk data */
  size_t remainder = size % BlockSize;
  size_t paddingSize = BlockSize - remainder;

  util::md5::Hasher hasher = { };
  hasher.update(bytes, size - remainder);

  /* DXBC hashing does not finalize the last block properly, instead
   * padding behaviour depends on the size of the byte stream */
  static const std::array<uint8_t, BlockSize> s_padding = { 0x80u };

  if (remainder >= 56u) {
    /* Append last block and pad to multiple of 64 bytes */
    hasher.update(&bytes[size - remainder], remainder);
    hasher.update(s_padding.data(), paddingSize);

    /* Pad with null block and custom finalizer */
    hasher.update(a.data(), a.size());
    hasher.update(s_padding.data() + a.size(), s_padding.size() - a.size() - b.size());
    hasher.update(b.data(), b.size());
  } else {
    /* Append bit count */
    hasher.update(a.data(), a.size());

    /* Append last block */
    if (remainder)
      hasher.update(&bytes[size - remainder], remainder);

    /* Append regular padding sequence */
    hasher.update(s_padding.data(), paddingSize - a.size() - b.size());

    /* Append final magic number */
    hasher.update(b.data(), b.size());
  }

  return hasher.getDigest();
}


ChunkHeader::ChunkHeader(util::ByteReader& reader) {
  if (!reader.read(tag) || !reader.read(size)) {
    tag = util::FourCC();
    size = 0u;
  }
}


bool ChunkHeader::write(util::ByteWriter& writer) const {
  return writer.write(tag) && writer.write(size);
}


Container::Container(util::ByteReader reader)
: m_reader(reader) {
  /* Reader must cover the entire file, not more, not less */
  dxbc_spv_assert(reader.getSize() == reader.getRemaining());

  if (!parseHeader())
    resetOnError();
}


util::ByteReader Container::findChunk(util::FourCC tag) const {
  for (const auto& chunk : m_chunks) {
    if (chunk.tag == tag)
      return m_reader.getRange(chunk.offset, chunk.size);
  }

  return util::ByteReader();
}


util::ByteReader Container::getInputSignatureChunk() const {
  auto chunk = findChunk(util::FourCC("ISG1"));

  if (!chunk)
    chunk = findChunk(util::FourCC("ISGN"));

  return chunk;
}


util::ByteReader Container::getOutputSignatureChunk() const {
  auto chunk = findChunk(util::FourCC("OSG1"));

  if (!chunk)
    chunk = findChunk(util::FourCC("OSG5"));

  if (!chunk)
    chunk = findChunk(util::FourCC("OSGN"));

  return chunk;
}


util::ByteReader Container::getPatchConstantSignatureChunk() const {
  auto chunk = findChunk(util::FourCC("PSG1"));

  if (!chunk)
    chunk = findChunk(util::FourCC("PCSG"));

  return chunk;
}


util::ByteReader Container::getCodeChunk() const {
  auto chunk = findChunk(util::FourCC("SHEX"));

  if (!chunk)
    chunk = findChunk(util::FourCC("SHDR"));

  return chunk;
}


util::ByteReader Container::getInterfaceChunk() const {
  return findChunk(util::FourCC("IFCE"));
}


bool Container::validateHash() const {
  auto digest = hashDxbcBinary(m_reader.getData(0u), m_reader.getSize());
  return m_header.hash == digest;
}


bool Container::checkFourCC(util::ByteReader reader) {
  util::FourCC magic;
  return reader.read(magic) && magic == util::FourCC("DXBC");
}


bool Container::parseHeader() {
  /* Read and validate file header */
  size_t fileSize = m_reader.getSize();

  if (!m_reader.read(m_header.magic) ||
      !m_reader.read(m_header.hash.data.size(), m_header.hash.data.data()) ||
      !m_reader.read(m_header.version) ||
      !m_reader.read(m_header.fileSize) ||
      !m_reader.read(m_header.chunkCount)) {
    Logger::err("Failed to read DXBC header.");
    return false;
  }

  if (m_header.magic != util::FourCC("DXBC")) {
    Logger::err("Invalid DXBC magic: '", m_header.magic, "'");
    return false;
  }

  if (m_header.fileSize > fileSize) {
    Logger::err("DXBC file size invalid: Got ", m_header.fileSize, ", expected ", fileSize, ".");
    return false;
  }

  /* Read chunk offsets and headers. */
  for (uint32_t i = 0u; i < m_header.chunkCount; i++) {
    uint32_t chunkOffset = 0u;

    if (!m_reader.read(chunkOffset)) {
      Logger::err("Failed to read DXBC chunk offset.");
      return false;
    }

    /* Read chunk header and ensure the range is valid */
    auto chunkReader = m_reader.getRange(chunkOffset, sizeof(ChunkHeader));

    if (!chunkReader) {
      Logger::err("Chunk offset ", chunkOffset, " out of range, file size is ", m_reader.getSize());
      return false;
    }

    ChunkHeader chunkHeader(chunkReader);

    if (!chunkReader) {
      Logger::err("Failed to parse chunk header at offset ", chunkOffset, " .");
      return false;
    }

    if (!m_reader.getRange(chunkOffset, chunkHeader.size + sizeof(chunkHeader))) {
      Logger::err("Chunk at offset ", chunkOffset, ", size ", chunkHeader.size, " out of range.");
      return false;
    }

    /* Write back chunk entry */
    auto& entry = m_chunks.emplace_back();
    entry.tag = chunkHeader.tag;
    entry.offset = chunkOffset;
    entry.size = chunkHeader.size + sizeof(chunkHeader);
  }

  return true;
}


void Container::resetOnError() {
  m_reader = util::ByteReader();
  m_header = { };
  m_chunks.clear();
}


static bool emitHeader(util::ByteWriter& writer, const FileHeader& header, const uint32_t* offsets) {
  bool result = writer.write(header.magic) &&
                writer.write(header.hash.data.size(), header.hash.data.data()) &&
                writer.write(header.version) &&
                writer.write(header.fileSize) &&
                writer.write(header.chunkCount);

  /* Append offsets for each chunk for now. */
  for (uint32_t i = 0u; i < header.chunkCount; i++) {
    uint32_t offset = offsets ? offsets[i] : 0u;
    result = result && writer.write(offset);
  }

  return result;
}


bool buildContainer(util::ByteWriter& writer, const ContainerInfo& info) {
  bool result = true;

  auto headerOffset = writer.moveToEnd();

  /* Count chunks, but leave header at its defaults for
   * now. We're going to have to fix it up later. */
  FileHeader header = { };

  if (info.inputSignature)
    header.chunkCount += 1u;
  if (info.patchSignature)
    header.chunkCount += 1u;
  if (info.outputSignature)
    header.chunkCount += 1u;
  if (info.code)
    header.chunkCount += 1u;

  /* Emit dummy header */
  if (!emitHeader(writer, header, nullptr))
    return false;

  /* Emit chunks and write back chunk header offsets */
  util::small_vector<uint32_t, 32u> chunkOffsets;

  if (info.inputSignature) {
    chunkOffsets.push_back(writer.moveToEnd() - headerOffset);
    result = result && info.inputSignature->write(writer);
  }

  if (info.patchSignature) {
    chunkOffsets.push_back(writer.moveToEnd() - headerOffset);
    result = result && info.patchSignature->write(writer);
  }

  if (info.outputSignature) {
    chunkOffsets.push_back(writer.moveToEnd() - headerOffset);
    result = result && info.outputSignature->write(writer);
  }

  if (info.code) {
    chunkOffsets.push_back(writer.moveToEnd() - headerOffset);
    result = result && info.code->write(writer);
  }

  if (!result)
    return false;

  /* Re-emit header again, this time with valid chunk
   * offsets as well as the final file size in bytes */
  header.fileSize = writer.moveToEnd() - headerOffset;
  writer.moveTo(headerOffset);

  result = result && emitHeader(writer, header, chunkOffsets.data());

  if (!result)
    return false;

  /* Compute MD5 hash on plain binary */
  auto data = std::move(writer).extract();
  dxbc_spv_assert(data.size() == headerOffset + header.fileSize);

  header.hash = util::md5::Hasher::compute(&data.at(headerOffset), header.fileSize);

  /* Re-emit header again, this time with the hash */
  writer = util::ByteWriter(std::move(data));
  writer.moveTo(headerOffset);

  return emitHeader(writer, header, chunkOffsets.data());
}


}
