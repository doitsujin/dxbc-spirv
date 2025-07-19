#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <utility>

namespace dxbc_spv::util {

struct FourCC {
  FourCC() = default;

  FourCC(char c0, char c1, char c2, char c3)
  : c { c0, c1, c2, c3 } { }

  explicit FourCC(const std::string& str) {
    for (size_t i = 0u; i < c.size() && i < str.size(); i++)
      c[i] = i < str.size() ? str[i] : ' ';
  }

  explicit FourCC(const char* str) {
    size_t n = 0u;

    while (str[n] && n < c.size()) {
      c[n] = str[n];
      n++;
    }

    while (n < c.size())
      c[n++] = ' ';
  }

  bool operator == (const FourCC& other) const {
    bool eq = true;

    for (size_t i = 0u; i < c.size() && eq; i++)
      eq = c[i] == other.c[i];

    return eq;
  }

  bool operator != (const FourCC& other) const {
    return !(operator == (other));
  }

  std::string toString() const {
    std::string result(4u, '\0');
    for (size_t i = 0u; i < 4u; i++)
      result[i] = c[i];
    return result;
  }

  std::array<char, 4u> c = { };
};


/* Prints FourCC as string */
inline std::ostream& operator << (std::ostream& stream, const FourCC& fourcc) {
  for (size_t i = 0u; i < fourcc.c.size(); i++)
    stream << fourcc.c[i];
  return stream;
}

}

namespace std {

template<>
struct hash<dxbc_spv::util::FourCC> {
  size_t operator () (const dxbc_spv::util::FourCC& k) const {
    return uint32_t(uint8_t(k.c[0])) << 0u
         | uint32_t(uint8_t(k.c[1])) << 8u
         | uint32_t(uint8_t(k.c[2])) << 16u
         | uint32_t(uint8_t(k.c[3])) << 24u;
  }
};

}
