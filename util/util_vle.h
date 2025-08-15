#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "util_bit.h"

namespace dxbc_spv::util::vle {

/** Computes byte size of variable-encoded number. */
inline size_t encodedSize(uint64_t sym) {
  if (sym >> 63u) sym = -sym;
  if (sym <= 0x000000000000003full) return 1u;
  if (sym <= 0x0000000000001fffull) return 2u;
  if (sym <= 0x00000000000fffffull) return 3u;
  if (sym <= 0x0000000007ffffffull) return 4u;
  if (sym <= 0x00000003ffffffffull) return 5u;
  if (sym <= 0x000001ffffffffffull) return 6u;
  if (sym <= 0x0000ffffffffffffull) return 7u;
  if (sym <= 0x007fffffffffffffull) return 8u;
  return 9u;
}


/** Encodes a 64-bit symbol with a variable length. Note that the symbol
 *  is treated as a signed integer, and decoding must sign-extend the
 *  number if the most significant bit is set. */
inline size_t encode(uint64_t sym, uint8_t* data, size_t maxSize) {
  size_t len = encodedSize(sym) - 1u;

  if (maxSize <= len)
    return 0u;

  if (!len) {
    data[0u] = sym & 0x7fu;
    return 1u;
  }

  /* Header token; the number of leading '1' bits is equivalent to the
   * number of bytes that follow. */
  data[0u] = uint8_t(0xff00u >> len) | ((sym >> (8u * len)) & (0x7fu >> len));

  for (size_t i = 1u; i <= len; i++)
    data[i] = uint8_t(sym >> (8u * (len - i)));

  return len + 1u;
}


/** Decodes a variable-length symbol in a byte stream. */
inline size_t decode(uint64_t& sym, const uint8_t* data, size_t maxSize) {
  if (!maxSize)
    return 0u;

  uint8_t header = data[0];

  if (header <= 0x7fu) {
    /* Sign-extend header token as necessary. */
    sym = header | -uint64_t(header & 0x40u);
    return 1u;
  }

  /* Number of bytes that follow */
  uint32_t len = lzcnt8(~header);

  if (maxSize < len + 1u)
    return 0u;

  /* Sign-extend header token right away, we'll
   * shift any excess bits out anyway. */
  if (len < 7u) {
    uint64_t sign = 0x40u >> len;
    sym = -uint64_t(sign & header) | (header & (sign - 1u));
  } else if (len == 7u) {
    sym = -uint64_t(data[1u] >> 7u);
  } else {
    sym = 0u;
  }

  for (uint32_t i = 1u; i <= len; i++) {
    sym <<= 8u;
    sym |= data[i];
  }

  return len + 1u;
}

}
