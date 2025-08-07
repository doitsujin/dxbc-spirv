#pragma once

#include <cstddef>
#include <cstdint>

#include "util_bit.h"

namespace dxbc_spv::util {

uint16_t f32tof16(float f32);
float f16tof32(uint16_t f16);

/** 16-bit float type.
 *
 * Only provides storage as well as conversion
 * methods to and from 32-bit floats.
 */
struct float16_t {
  float16_t() = default;

  explicit float16_t(float f)
  : data(f32tof16(f)) { }

  bool operator == (const float16_t& other) const { return data == other.data; }
  bool operator != (const float16_t& other) const { return data != other.data; }

  explicit operator float () const {
    return f16tof32(data);
  }

  explicit operator double () const {
    return double(f16tof32(data));
  }

  static float16_t fromRaw(uint16_t data) {
    float16_t result = { };
    result.data = data;
    return result;
  }

  static float16_t minValue() { return fromRaw(0xfbffu); }
  static float16_t maxValue() { return fromRaw(0x7bffu); }

  uint16_t data;
};

inline float16_t operator ""_f16(long double d) {
  return float16_t(d);
}

}
