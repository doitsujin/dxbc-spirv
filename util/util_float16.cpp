#include "util_float16.h"

#include <cstring>

namespace dxbc_spv::util {

/** Converts 32-bit float to 16-bit float. */
uint16_t f32tof16(float f32) {
  uint32_t u32;
  std::memcpy(&u32, &f32, 4);

  uint32_t exp32 = (u32 & 0x7F800000) >> 23;
  uint32_t frc32 = (u32 & 0x007FFFFF);

  uint32_t sgn16 = (u32 & 0x80000000) >> 16;
  uint32_t exp16, frc16;

  if (exp32 > 142) {
    if (exp32 == 0xFF) {
      /* Infinity or NaN, preserve. */
      exp16 = 0x1F;
      frc16 = frc32 >> 13;

      if (frc32)
        frc16 |= 0x200;
    } else {
      /* Regular number that is larger what we can represent
       * with f16, return maximum representable number. */
      exp16 = 0x1E;
      frc16 = 0x3FF;
    }
  } else if (exp32 < 113) {
    if (exp32 >= 103) {
      /* Number can be represented as denorm */
      exp16 = 0;
      frc16 = (0x0400 | (frc32 >> 13)) >> (113 - exp32);
    } else {
      /* Number too small to be represented */
      exp16 = 0;
      frc16 = 0;
    }
  } else {
    /* Regular number */
    exp16 = exp32 - 112;
    frc16 = frc32 >> 13;
  }

  return uint16_t(sgn16 | (exp16 << 10) | frc16);
}


/** Converts 16-bit float to 32-bit float */
float f16tof32(uint16_t f16) {
  uint32_t exp16 = uint32_t(f16 & 0x7C00) >> 10;
  uint32_t frc16 = uint32_t(f16 & 0x03FF);

  uint32_t sgn32 = uint32_t(f16 & 0x8000) << 16;
  uint32_t exp32, frc32;

  if (!exp16) {
    if (!frc16) {
      exp32 = 0;
      frc32 = 0;
    } else {
      /* Denorm in 16-bit, but we can represent these
       * natively in 32-bit by adjusting the exponent. */
      int32_t bit = findmsb(frc16);
      exp32 = 127 - 24 + bit;
      frc32 = (frc16 << (23 - bit)) & 0x007FFFFF;
    }
  } else if (exp16 == 0x1F) {
    /* Infinity or NaN, preserve semantic meaning. */
    exp32 = 0xFF;
    frc32 = frc16 << 13;

    if (frc16)
      frc32 |= 0x400000;
  } else {
    /* Regular finite number, adjust the exponent as
     * necessary and shift the fractional part. */
    exp32 = exp16 + 112;
    frc32 = frc16 << 13;
  }

  float f32;
  uint32_t u32 = sgn32 | (exp32 << 23) | frc32;
  std::memcpy(&f32, &u32, sizeof(f32));
  return f32;
}

}
