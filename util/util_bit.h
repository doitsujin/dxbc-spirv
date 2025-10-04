#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>

namespace dxbc_spv::util {

/** Aligns integer value to next power of two. */
template<typename T>
constexpr T align(T value, T alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}


/* Population count. */
template<typename T>
T popcnt(T n) {
  n -= ((n >> 1u) & T(0x5555555555555555ull));
  n = (n & T(0x3333333333333333ull)) + ((n >> 2u) & T(0x3333333333333333ull));
  n = (n + (n >> 4u)) & T(0x0f0f0f0f0f0f0f0full);
  n *= T(0x0101010101010101ull);
  return n >> (8u * (sizeof(T) - 1u));
}


/** Trailing zero count (8-bit) */
inline uint32_t tzcnt(uint8_t n) {
  uint32_t r = 7;
  n &= -n;
  r -= (n & 0x0fu) ? 4 : 0;
  r -= (n & 0x33u) ? 2 : 0;
  r -= (n & 0x55u) ? 1 : 0;
  return n != 0 ? r : 8;
}


/** Trailing zero count (16-bit) */
inline uint32_t tzcnt(uint16_t n) {
  uint32_t r = 15;
  n &= -n;
  r -= (n & 0x00ffu) ? 8 : 0;
  r -= (n & 0x0f0fu) ? 4 : 0;
  r -= (n & 0x3333u) ? 2 : 0;
  r -= (n & 0x5555u) ? 1 : 0;
  return n != 0 ? r : 16;
}


/** Trailing zero count (32-bit) */
inline uint32_t tzcnt(uint32_t n) {
  uint32_t r = 31;
  n &= -n;
  r -= (n & 0x0000ffffu) ? 16 : 0;
  r -= (n & 0x00ff00ffu) ?  8 : 0;
  r -= (n & 0x0f0f0f0fu) ?  4 : 0;
  r -= (n & 0x33333333u) ?  2 : 0;
  r -= (n & 0x55555555u) ?  1 : 0;
  return n != 0 ? r : 32;
}


/** Trailing zero count (64-bit) */
inline uint32_t tzcnt(uint64_t n) {
  uint64_t r = 63;
  n &= -n;
  r -= (n & 0x00000000ffffffffull) ? 32 : 0;
  r -= (n & 0x0000ffff0000ffffull) ? 16 : 0;
  r -= (n & 0x00ff00ff00ff00ffull) ?  8 : 0;
  r -= (n & 0x0f0f0f0f0f0f0f0full) ?  4 : 0;
  r -= (n & 0x3333333333333333ull) ?  2 : 0;
  r -= (n & 0x5555555555555555ull) ?  1 : 0;
  return n != 0 ? r : 64;
}


/** Leading zero count (8-bit) */
inline uint32_t lzcnt8(uint8_t n) {
  uint32_t r = 0;
  r += ((n << r) & 0xf0u) ? 0 : 4;
  r += ((n << r) & 0xc0u) ? 0 : 2;
  r += ((n << r) & 0x80u) ? 0 : 1;
  return n != 0 ? r : 8;
}


/** Leading zero count (16-bit) */
inline uint32_t lzcnt16(uint16_t n) {
  uint32_t r = 0;
  r += ((n << r) & 0xff00u) ? 0 : 8;
  r += ((n << r) & 0xf000u) ? 0 : 4;
  r += ((n << r) & 0xc000u) ? 0 : 2;
  r += ((n << r) & 0x8000u) ? 0 : 1;
  return n != 0 ? r : 16;
}


/** Leading zero count (32-bit) */
inline uint32_t lzcnt32(uint32_t n) {
  uint32_t r = 0;
  r += ((n << r) & 0xffff0000u) ? 0 : 16;
  r += ((n << r) & 0xff000000u) ? 0 : 8;
  r += ((n << r) & 0xf0000000u) ? 0 : 4;
  r += ((n << r) & 0xc0000000u) ? 0 : 2;
  r += ((n << r) & 0x80000000u) ? 0 : 1;
  return n != 0 ? r : 32;
}


/** Leading zero count (64-bit) */
inline uint32_t lzcnt64(uint64_t n) {
  uint32_t r = 0;
  r += ((n << r) & 0xffffffff00000000ull) ? 0 : 32;
  r += ((n << r) & 0xffff000000000000ull) ? 0 : 16;
  r += ((n << r) & 0xff00000000000000ull) ? 0 : 8;
  r += ((n << r) & 0xf000000000000000ull) ? 0 : 4;
  r += ((n << r) & 0xc000000000000000ull) ? 0 : 2;
  r += ((n << r) & 0x8000000000000000ull) ? 0 : 1;
  return n != 0 ? r : 64;
}


/** Finds index of least significant bit */
template<typename T>
inline int32_t findlsb(uint32_t number) {
  return number ? int32_t(tzcnt(number)) : -1;
}


/** Finds index of most significant bit (32-bit) */
inline int32_t findmsb(uint32_t number) {
  return 31 - int32_t(lzcnt32(number));
}


/** Finds index of most significant bit (64-bit) */
inline int32_t findmsb(uint64_t number) {
  return 63 - int32_t(lzcnt64(number));
}


/** Extracts bit range from bit field */
template<typename T>
T bextract(T op, uint32_t first, uint32_t count) {
  if (!count)
    return 0;

  T mask = (T(2) << (count - 1)) - T(1);
  return (op >> first) & mask;
}

/** Extracts bit range from bit field as a signed integer */
template<typename T, typename T2>
T bextractSigned(T2 op, uint32_t first, uint32_t count) {
  if (!count)
    return 0;

  T mask = (T(1) << (count - 1)) - T(1);
  T signMask = T(1) << (count - 1);
  return (T(op >> first) & mask) - (T(op >> first) & signMask);
}

/** Inserts bits into bit field */
template<typename T>
T binsert(T op, T v, uint32_t first, uint32_t count) {
  if (!count)
    return op;

  T mask = ((T(2) << (count - 1)) - T(1)) << first;
  return (op & ~mask) | ((v << first) & mask);
}


/** Convert unsigned integer to float using round-to-zero semantics */
template<typename T, uint32_t ExpBits, uint32_t MantissaBits>
T convertUintToFloatRtz(uint64_t v) {
  if (!v)
    return T(0u);

  T msb = findmsb(v);

  /* Compute exponent based on msb */
  T expBias = (T(1u) << (ExpBits - 1u)) - 1u;
  T expValue = expBias + msb;

  /* Extract mantissa and discard leading one */
  T mantissa = T((v << (64u - msb)) >> (64u - MantissaBits));

  return T(mantissa | (expValue << MantissaBits));
}


/** Convert signed integer to float using round-to-zero semantics */
template<typename T, uint32_t ExpBits, uint32_t MantissaBits>
T convertSintToFloatRtz(int64_t v) {
  T result = convertUintToFloatRtz<T, ExpBits, MantissaBits>(std::abs(v));

  if (v < 0)
    result |= T(1u) << (ExpBits + MantissaBits);

  return result;
}


/** Compares ASCII characters in a case-insensitive way */
inline bool compareCharsCaseInsensitive(char a, char b) {
  if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
  if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
  return a == b;
}

}
