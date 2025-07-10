#pragma once

#include <iostream>
#include <utility>

namespace dxbc_spv::util {

class ConsoleState {

public:

  ConsoleState() = default;

  ConsoleState(std::ostream& stream, uint32_t fg, uint32_t effect = 0)
  : m_stream(&stream) {
    (*m_stream) << "\033[" << fg;

    if (effect)
      (*m_stream) << ";" << effect;

    (*m_stream) << "m";
  }

  ConsoleState(ConsoleState&& other)
  : m_stream(std::exchange(other.m_stream, nullptr)) { }

  ConsoleState& operator = (ConsoleState&& other) {
    m_stream = std::exchange(other.m_stream, nullptr);
    return *this;
  }

  ConsoleState             (const ConsoleState&) = delete;
  ConsoleState& operator = (const ConsoleState&) = delete;

  ~ConsoleState() {
    if (m_stream)
      (*m_stream) << "\033[0m";
  }

  static constexpr uint32_t FgBlack = 30;
  static constexpr uint32_t FgRed = 31;
  static constexpr uint32_t FgGreen = 32;
  static constexpr uint32_t FgYellow = 33;
  static constexpr uint32_t FgBlue = 34;
  static constexpr uint32_t FgMagenta = 35;
  static constexpr uint32_t FgCyan = 36;
  static constexpr uint32_t FgWhite = 37;
  static constexpr uint32_t FgDefault = 39;

  static constexpr uint32_t EffectBold = 1;
  static constexpr uint32_t EffectDim = 1;

private:

  std::ostream* m_stream = nullptr;

};

}
