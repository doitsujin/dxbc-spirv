#include "util_md5.h"
#include "util_bit.h"
#include "util_debug.h"

namespace dxbc_spv::util::md5 {

void Hasher::update(const void* data, size_t size) {
  auto src = reinterpret_cast<const unsigned char*>(data);

  /* If we have an incomplete block, fill it first */
  size_t blockOffset = m_size % BlockSize;
  m_size += size;

  if (blockOffset) {
    size_t n = std::min(size, BlockSize - blockOffset);
    std::memcpy(&m_block[blockOffset], src, n);

    size -= n;
    src += n;

    if (blockOffset + n >= BlockSize)
      processBlock(m_block.data());
  }

  /* Process full 64-byte blocks directly */
  while (size >= BlockSize) {
    processBlock(src);

    src += BlockSize;
    size -= BlockSize;
  }

  /* If there is any data left, start a new block */
  if (size)
    std::memcpy(m_block.data(), src, size);
}


Digest Hasher::finalize() {
  padBlock();

  auto digest = getDigest();

  reset();
  return digest;
}


Digest Hasher::getDigest() {
  Digest digest = { };

  for (uint32_t i = 0u; i < m_state.size(); i++) {
    auto dw = m_state.at(i);

    for (uint32_t j = 0u; j < sizeof(dw); j++)
      digest.data.at(sizeof(dw) * i + j) = util::bextract(dw, 8u * j, 8u);
  }

  return digest;
}


Digest Hasher::compute(const void* data, size_t size) {
  Hasher state = { };
  state.update(data, size);
  return state.finalize();
}


void Hasher::processBlock(const unsigned char* data) {
  static const std::array<uint8_t, 64u> s_shifts = {
    7u, 12u, 17u, 22u,  7u, 12u, 17u, 22u,  7u, 12u, 17u, 22u,  7u, 12u, 17u, 22u,
    5u,  9u, 14u, 20u,  5u,  9u, 14u, 20u,  5u,  9u, 14u, 20u,  5u,  9u, 14u, 20u,
    4u, 11u, 16u, 23u,  4u, 11u, 16u, 23u,  4u, 11u, 16u, 23u,  4u, 11u, 16u, 23u,
    6u, 10u, 15u, 21u,  6u, 10u, 15u, 21u,  6u, 10u, 15u, 21u,  6u, 10u, 15u, 21u,
  };

  static const std::array<uint32_t, 64u> s_constants = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
    0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
    0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
    0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
    0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
    0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
    0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
    0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
    0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u,
  };

  /* Basic implementation based on Wikipedia pseudo-code. This does not need
   * to be fast since we will only use it to generate hashes for custom DXBC
   * binaries. */
  std::array<uint32_t, 4u> state = m_state;

  auto finalizeIteration = [&state, data,
    s = s_shifts.data(),
    k = s_constants.data()
  ] (uint32_t i, uint32_t f, uint32_t g) {
    f = f + state[0u] + k[i] + readDword(&data[4u * g]);
    state[0u] = state[3u];
    state[3u] = state[2u];
    state[2u] = state[1u];
    state[1u] += (f << s[i]) + (f >> (32u - s[i]));
  };

  for (uint32_t i = 0u; i < 16u; i++) {
    uint32_t f = (state[1u] & state[2u]) | (~state[1u] & state[3u]);
    uint32_t g = i;
    finalizeIteration(i, f, g);
  }

  for (uint32_t i = 16u; i < 32u; i++) {
    uint32_t f = (state[3u] & state[1u]) | (~state[3u] & state[2u]);
    uint32_t g = (5u * i + 1u) % 16u;
    finalizeIteration(i, f, g);
  }

  for (uint32_t i = 32u; i < 48u; i++) {
    uint32_t f = state[1u] ^ state[2u] ^ state[3u];
    uint32_t g = (3u * i + 5u) % 16u;
    finalizeIteration(i, f, g);
  }

  for (uint32_t i = 48u; i < 64u; i++) {
    uint32_t f = state[2u] ^ (state[1u] | ~state[3u]);
    uint32_t g = (7u * i) % 16u;
    finalizeIteration(i, f, g);
  }

  for (uint32_t i = 0u; i < state.size(); i++)
    m_state.at(i) += state.at(i);
}


void Hasher::padBlock() {
  static const std::array<uint8_t, BlockSize> s_padding = { 0x80u };

  /* Bit count of the original message */
  uint64_t bitCount = 8u * m_size;

  /* Compute number of bytes to pad with */
  size_t currentLength = m_size % BlockSize;
  size_t desiredLength = currentLength >= 56u ? 120u : 56u;

  update(s_padding.data(), desiredLength - currentLength);

  /* Append bit count as little endian */
  std::array<uint8_t, sizeof(bitCount)> finalPadding = { };

  for (uint32_t i = 0u; i < finalPadding.size(); i++)
    finalPadding.at(i) = uint8_t(util::bextract(bitCount, 8u * i, 8u));

  update(finalPadding.data(), finalPadding.size());

  dxbc_spv_assert(!(m_size % BlockSize));
}


void Hasher::reset() {
  *this = Hasher();
}


uint32_t Hasher::readDword(const unsigned char* src) {
  return (uint32_t(src[0u]) <<  0u) | (uint32_t(src[1u]) <<  8u) |
         (uint32_t(src[2u]) << 16u) | (uint32_t(src[3u]) << 24u);
}


std::ostream& operator << (std::ostream& os, const Digest& hash) {
  static const std::array<char, 16u> s_nibbles = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
  };

  for (auto b : hash.data) {
    os << s_nibbles[util::bextract(b, 4u, 4u)]
       << s_nibbles[util::bextract(b, 0u, 4u)];
  }

  return os;
}

}
