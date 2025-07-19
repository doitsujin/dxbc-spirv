#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

#include "util_fourcc.h"

namespace dxbc_spv::util {

/* Helper to find uint type for a given byte size */
template<size_t S> struct uint_type;
template<> struct uint_type<1u> { using type = uint8_t; };
template<> struct uint_type<2u> { using type = uint16_t; };
template<> struct uint_type<4u> { using type = uint32_t; };
template<> struct uint_type<8u> { using type = uint64_t; };

template<typename T> using uint_type_t = typename uint_type<sizeof(T)>::type;


/** Simple helper class to safely read data from a byte array. */
class ByteReader {

public:

  ByteReader() = default;
  ByteReader(const void* data, size_t size)
  : m_data(reinterpret_cast<const unsigned char*>(data))
  , m_size(size), m_offset(0u), m_eof(!m_data) { }

  /** Retrieves raw data pointer at the given offset */
  const void* getData(size_t offset) const {
    return &m_data[offset];
  }

  /** Queries total size of the byte array */
  size_t getSize() const {
    return m_size;
  }

  /** Queries remaining amount to read */
  size_t getRemaining() const {
    /* This is safe against overflows given the invariant
     * that the offset can never exceed size. */
    return m_size - m_offset;
  }

  /* Sets read location to given fixed offset.
   * Returns true if the offset is in bounds. */
  ByteReader getRange(size_t offset, size_t size) const {
    if (offset > m_size || size > m_size - offset)
      return ByteReader(nullptr, 0u);

    return ByteReader(&m_data[offset], size);
  }

  /** Retrieves range starting from current offset. */
  ByteReader getRangeRelative(size_t offset, size_t size) const {
    return getRange(m_offset + offset, size);
  }

  /** Jumps to given byte offset */
  void moveTo(size_t n) {
    m_offset = std::min(n, m_size);
  }

  /** Advances read offset by given number of bytes.
   *  Returns true if the resulting offset is in bounds. */
  bool skip(size_t n) {
    if (n > m_size - m_offset) {
      m_eof = true;
      return false;
    }

    m_offset += n;
    return true;
  }

  /** Reads a raw number from the byte stream as little endian. */
  template<typename T, std::enable_if_t<std::is_arithmetic_v<T>, bool> = true>
  bool read(T& dst) {
    if (sizeof(dst) > getRemaining()) {
      m_eof = true;
      return false;
    }

    uint_type_t<T> value;
    readUint(&m_data[m_offset], value);

    std::memcpy(&dst, &value, sizeof(dst));

    m_offset += sizeof(dst);
    return true;
  }

  /** Reads enum value */
  template<typename T, std::enable_if_t<std::is_enum_v<T>, bool> = true>
  bool read(T& dst) {
    std::underlying_type_t<T> data = 0;

    if (!read(data))
      return false;

    dst = T(data);
    return true;
  }

  /** Reads raw bytes from the byte stream */
  template<typename T, std::enable_if_t<(std::is_trivially_copyable_v<T> && sizeof(T) == 1u), bool> = true>
  bool read(size_t n, T* dst) {
    if (n > getRemaining()) {
      m_eof = true;
      return false;
    }

    std::memcpy(dst, &m_data[m_offset], n);

    m_offset += n;
    return true;
  }

  /** Reads four-character code */
  bool read(FourCC& cc) {
    return read(cc.c.size(), cc.c.data());
  }

  /** Reads null-terminated string into container */
  template<typename T>
  bool readString(T& container) {
    char ch = '\0';

    while (read(ch) && ch)
      container.push_back(ch);

    return operator bool ();
  }

  /** Checks whether reader can read more data */
  explicit operator bool () const {
    return !m_eof;
  }

private:

  const unsigned char*  m_data    = nullptr;
  size_t                m_size    = 0u;
  size_t                m_offset  = 0u;
  bool                  m_eof     = true;

  /* GCC doesn't understand doing this in a loop and will generate
   * terrible code, so type it out for each type */
  static void readUint(const unsigned char* data, uint8_t& dst) {
    dst = data[0u];
  }

  static void readUint(const unsigned char* data, uint16_t& dst) {
    dst = uint16_t(data[0u]) | (uint16_t(data[1u]) << 8);
  }

  static void readUint(const unsigned char* data, uint32_t& dst) {
    dst = (uint32_t(data[0u]) <<  0) | (uint32_t(data[1u]) <<  8) |
          (uint32_t(data[2u]) << 16) | (uint32_t(data[3u]) << 24);
  }

  static void readUint(const unsigned char* data, uint64_t& dst) {
    dst = (uint64_t(data[0u]) <<  0) | (uint64_t(data[1u]) <<  8) |
          (uint64_t(data[2u]) << 16) | (uint64_t(data[3u]) << 24) |
          (uint64_t(data[4u]) << 32) | (uint64_t(data[5u]) << 40) |
          (uint64_t(data[6u]) << 48) | (uint64_t(data[7u]) << 56);
  }

};


/** Helper class to safely write bytes to a byte array. */
class ByteWriter {

public:

  ByteWriter() = default;

  ByteWriter(std::vector<unsigned char>&& container)
  : m_data(std::move(container)) { }

  /** Queries current size of the byte array */
  size_t getSize() const {
    return m_data.size();
  }

  /** Queries current cursor position */
  size_t getCursor() const {
    return m_offset;
  }

  /** Sets cursor to given absolute offset. Any bytes starting
   *  at the given offset will be overwritten. */
  void moveTo(size_t n) {
    m_offset = n;
  }

  /** Sets cursor to the end of the byte array and returns
   *  cursor position. */
  size_t moveToEnd() {
    m_offset = getSize();
    return m_offset;
  }

  /** Tries to reserve given amount of memory at the end of
   *  the byte array. Will set eof state if the current write
   *  offset is invalid. */
  bool reserve(size_t n) {
    if (n && m_offset > m_data.size())
      return false;

    if (m_offset + n > getSize())
      m_data.resize(m_offset + n);
    return true;
  }

  /** Writes a raw number as little endian. */
  template<typename T, std::enable_if_t<std::is_arithmetic_v<T>, bool> = true>
  bool write(T data) {
    if (!reserve(sizeof(data)))
      return false;

    uint_type_t<T> value;
    std::memcpy(&value, &data, sizeof(value));

    writeUint(&m_data[m_offset], value);

    m_offset += sizeof(data);
    return true;
  }

  /** Writes an enum value. */
  template<typename T, std::enable_if_t<std::is_enum_v<T>, bool> = true>
  bool write(T data) {
    return write(std::underlying_type_t<T>(data));
  }

  /** Writes arbitrary byte array */
  template<typename T, std::enable_if_t<(std::is_trivially_copyable_v<T> && sizeof(T) == 1u), bool> = true>
  bool write(size_t size, const T* data) {
    if (!reserve(size))
      return false;

    std::memcpy(&m_data[m_offset], data, size);

    m_offset += size;
    return true;
  }

  /** Writes four-character code */
  bool write(const FourCC& cc) {
    return write(cc.c.size(), cc.c.data());
  }

  /** Writes null-terminated string */
  bool write(const char* str) {
    return write(std::strlen(str) + 1u, str);
  }

  /** Extracts byte array from object and leaves object in
   *  an undefined state, unless it was empty already. */
  std::vector<unsigned char> extract() && {
    m_eof = m_eof || !m_data.empty();
    return std::move(m_data);
  }

  /** Checks whether writer is in a valid state. This will
   *  only ever not be the case when attempting to write to
   *  an offset greater than the size. */
  explicit operator bool () const {
    return !m_eof;
  }

private:

  std::vector<unsigned char> m_data;
  size_t                     m_offset = 0u;
  bool                       m_eof = false;

  static void writeUint(unsigned char* data, uint8_t value) {
    data[0u] = value;
  }

  static void writeUint(unsigned char* data, uint16_t value) {
    data[0u] = uint8_t(value);
    data[1u] = uint8_t(value >> 8u);
  }

  static void writeUint(unsigned char* data, uint32_t value) {
    data[0u] = uint8_t(value);
    data[1u] = uint8_t(value >> 8u);
    data[2u] = uint8_t(value >> 16u);
    data[3u] = uint8_t(value >> 24u);
  }

  static void writeUint(unsigned char* data, uint64_t value) {
    data[0u] = uint8_t(value);
    data[1u] = uint8_t(value >> 8u);
    data[2u] = uint8_t(value >> 16u);
    data[3u] = uint8_t(value >> 24u);
    data[4u] = uint8_t(value >> 32u);
    data[5u] = uint8_t(value >> 40u);
    data[6u] = uint8_t(value >> 48u);
    data[7u] = uint8_t(value >> 56u);
  }

};

}
