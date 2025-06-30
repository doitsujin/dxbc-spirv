#pragma once

#include <cstddef>
#include <cstdint>
#include <iostream>

namespace dxbc_spv::tests {

class ScopedCounter {

public:

  ScopedCounter() = default;

  ScopedCounter(int32_t* ctr)
  : m_counter(ctr) {
    inc();
  }

  ScopedCounter(const ScopedCounter& other)
  : m_counter(other.m_counter) {
    inc();
  }

  ScopedCounter(ScopedCounter&& other)
  : m_counter(other.m_counter) {
    other.m_counter = nullptr;
  }

  ScopedCounter& operator = (const ScopedCounter& other) {
    dec();
    m_counter = other.m_counter;
    inc();
    return *this;
  }

  ScopedCounter& operator = (ScopedCounter&& other) {
    dec();
    m_counter = other.m_counter;
    other.m_counter = nullptr;
    return *this;
  }

  ~ScopedCounter() {
    dec();
  }

private:

  int32_t* m_counter = nullptr;

  void inc() {
    if (m_counter)
      (*m_counter)++;
  }

  void dec() {
    if (m_counter)
      (*m_counter)--;
  }

};

struct TestState {
  const char* testName = "";
  uint32_t testsRun = 0u;
  uint32_t testsFailed = 0u;
};

extern TestState g_testState;

}

#define RUN_TEST(fn, ...) do {                \
  g_testState.testName = #fn;                 \
  std::cerr << "=== " << #fn "(" #__VA_ARGS__ \
    ")" << std::endl;                         \
  fn(__VA_ARGS__);                            \
} while (0)


#define ok(cond) do {                         \
  g_testState.testsRun++;                     \
  if (!(cond)) {                              \
    g_testState.testsFailed++;                \
    std::cerr << __FILE__ << ":"              \
      << __LINE__ << " fail: "                \
      << #cond << ":" << std::endl;           \
  }                                           \
} while (0)
