#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

namespace dxbc_spv::util {

/* Debug exception */
class DbgAssert {

public:

  explicit DbgAssert(const char* msg) {
    std::strncpy(m_message.data(), msg, m_message.size() - 1u);
    std::cerr << msg << std::endl;
  }

  const char* what() const {
    return m_message.data();
  }

private:

  std::array<char, 256> m_message = { };

};

#define dxbc_spv_macro_stringify_impl(x) #x
#define dxbc_spv_macro_stringify(x) dxbc_spv_macro_stringify_impl(x)

#ifdef NDEBUG
#define dxbc_spv_assert(cond) do { } while (0)
#define dxbc_spv_unreachable(cond) do { } while (0)
#else
/* assert macro, throws exception if false */
#define dxbc_spv_assert(cond)               \
  do {                                      \
    if (!(cond)) {                          \
      throw util::DbgAssert(__FILE__ ":"    \
        dxbc_spv_macro_stringify(__LINE__)  \
        ": Assertion failed:\n" #cond);     \
    }                                       \
  } while (0)

/* unreachable macro, throws exception if hit */
#define dxbc_spv_unreachable(cond)          \
  do {                                      \
    throw util::DbgAssert(__FILE__ ":"      \
      dxbc_spv_macro_stringify(__LINE__)    \
      ": Hit unreachable path");            \
  } while (0)

#endif

}
