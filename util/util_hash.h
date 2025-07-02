#pragma once

#include <cstddef>
#include <cstdint>

namespace dxbc_spv::util {

/* Helper function to combine two hash values */
inline size_t hash_combine(size_t a, size_t b) {
  return a ^ (b + 0x9e3779b9u + (a << 6) + (a >> 2));
}

}
