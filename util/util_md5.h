#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ostream>

namespace dxbc_spv::util::md5 {

/** 128-bit MD5 digest */
struct Digest {
  std::array<uint8_t, 16u> data = { };

  bool operator == (const Digest& other) const { return !std::memcmp(data.data(), other.data.data(), sizeof(data)); }
  bool operator != (const Digest& other) const { return  std::memcmp(data.data(), other.data.data(), sizeof(data)); }
};


/** MD5 hash state */
class Hasher {
  constexpr static size_t BlockSize = 64u;
public:

  Hasher() = default;

  /** Processes data to hash */
  void update(const void* data, size_t size);

  /** Finalizes hash and computes digest. The hasher
   *  will be returned to its default state after. */
  Digest finalize();

  /** Retrieves current digest without finalizing the
   *  stream properly. */
  Digest getDigest();

  /** Convenience method to compute a hash for a simple
   *  binary blob of data in memory. */
  static Digest compute(const void* data, size_t size);

private:

  std::array<uint8_t, BlockSize> m_block = { };
  uint64_t m_size = 0u;

  std::array<uint32_t, 4u> m_state = {
    0x67452301u, 0xefcdab89u,
    0x98badcfeu, 0x10325476u,
  };

  void processBlock(const unsigned char* data);

  void padBlock();

  void reset();

  static uint32_t readDword(const unsigned char* src);

};

/** Prints hash */
std::ostream& operator << (std::ostream& os, const Digest& hash);

}
