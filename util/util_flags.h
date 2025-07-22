#pragma once

#include <atomic>
#include <cstdint>
#include <iterator>
#include <ostream>
#include <type_traits>
#include <utility>

namespace dxbc_spv::util {

template<typename T>
class Flags {
  static_assert(std::is_enum_v<T>);
public:

  using IntType = std::underlying_type_t<T>;
  static_assert(!IntType(T::eFlagEnum));

  class iterator {

  public:

    using iterator_category = std::input_iterator_tag;
    using difference_type = uint32_t;
    using value_type = T;
    using reference = T;
    using pointer = const T*;

    explicit iterator(IntType flags)
    : m_flags(flags) { }

    iterator& operator ++ () {
      m_flags &= m_flags - 1;
      return *this;
    }

    iterator operator ++ (int) {
      iterator retval = *this;
      m_flags &= m_flags - 1;
      return retval;
    }

    T operator * () const {
      return T(m_flags & -m_flags);
    }

    bool operator == (iterator other) const { return m_flags == other.m_flags; }
    bool operator != (iterator other) const { return m_flags != other.m_flags; }

  private:

    IntType m_flags;

  };

  Flags() = default;
  Flags(IntType raw)
  : m_raw(raw) { }
  Flags(T flag)
  : m_raw(IntType(flag)) { }

  iterator begin() const { return iterator(m_raw); }
  iterator end() const { return iterator(0); }

  Flags operator | (Flags f) const { return Flags(m_raw | f.m_raw); }
  Flags operator & (Flags f) const { return Flags(m_raw & f.m_raw); }
  Flags operator ^ (Flags f) const { return Flags(m_raw ^ f.m_raw); }
  Flags operator - (Flags f) const { return Flags(m_raw &~f.m_raw); }

  Flags& operator |= (Flags f) { m_raw |= f.m_raw; return *this; }
  Flags& operator &= (Flags f) { m_raw &= f.m_raw; return *this; }
  Flags& operator ^= (Flags f) { m_raw ^= f.m_raw; return *this; }
  Flags& operator -= (Flags f) { m_raw &=~f.m_raw; return *this; }

  bool all(Flags f) {
    return (m_raw & f.m_raw) == f.m_raw;
  }

  T first() const {
    return T(m_raw & -m_raw);
  }

  void set(Flags f, bool cond) {
    m_raw = cond
      ? m_raw |  f.m_raw
      : m_raw & ~f.m_raw;
  }

  bool operator == (Flags f) const { return m_raw == f.m_raw; }
  bool operator != (Flags f) const { return m_raw != f.m_raw; }

  explicit operator bool () const {
    return m_raw != 0;
  }

  explicit operator T () const {
    return T(m_raw);
  }

  explicit operator IntType () const {
    return m_raw;
  }

private:

  IntType m_raw;

};


template<typename T>
class AtomicFlags {
  static_assert(std::is_enum_v<T>);
public:

  using FlagType = Flags<T>;
  using IntType = typename FlagType::IntType;

  AtomicFlags() = default;

  AtomicFlags(FlagType flags)
  : m_raw(IntType(flags)) { }

  AtomicFlags(const AtomicFlags& other)
  : m_raw(other.m_raw.load(std::memory_order_relaxed)) { }

  AtomicFlags& operator = (const AtomicFlags& other) {
    m_raw.store(other.m_raw.load(std::memory_order_relaxed), std::memory_order_relaxed);
    return *this;
  }

  AtomicFlags& operator = (FlagType flags) {
    m_raw = IntType(flags);
    return *this;
  }

  FlagType load(std::memory_order memoryOrder = std::memory_order_seq_cst) const {
    return FlagType(m_raw.load(memoryOrder));
  }

  void store(FlagType flags,
          std::memory_order memoryOrder = std::memory_order_seq_cst) {
    m_raw.store(IntType(flags), memoryOrder);
  }

  FlagType set(FlagType flags,
          std::memory_order memoryOrder = std::memory_order_seq_cst) {
    return FlagType(m_raw.fetch_or(IntType(flags), memoryOrder));
  }

  FlagType clr(FlagType flags,
          std::memory_order memoryOrder = std::memory_order_seq_cst) {
    return FlagType(m_raw.fetch_and(~IntType(flags), memoryOrder));
  }

  FlagType exchange(FlagType flags,
          std::memory_order memoryOrder = std::memory_order_seq_cst) {
    return FlagType(m_raw.exchange(IntType(flags), memoryOrder));
  }

private:

  std::atomic<IntType> m_raw;

};

template<typename T>
std::ostream& operator << (std::ostream& os, Flags<T> flags) {
  if (!flags) {
    os << "None";
  } else {
    os << flags.first();
    flags -= flags.first();

    while (flags) {
      os << "|" << flags.first();
      flags -= flags.first();
    }
  }

  return os;
}

}

template<typename T, T v = T::eFlagEnum>
auto operator | (T a, T b) { return dxbc_spv::util::Flags<T>(a) | b; }
template<typename T, T v = T::eFlagEnum>
auto operator & (T a, T b) { return dxbc_spv::util::Flags<T>(a) & b; }
template<typename T, T v = T::eFlagEnum>
auto operator ^ (T a, T b) { return dxbc_spv::util::Flags<T>(a) ^ b; }
template<typename T, T v = T::eFlagEnum>
auto operator - (T a, T b) { return dxbc_spv::util::Flags<T>(a) - b; }
template<typename T, T v = T::eFlagEnum>
auto operator == (T a, T b) { return dxbc_spv::util::Flags<T>(a) == b; }
template<typename T, T v = T::eFlagEnum>
auto operator != (T a, T b) { return dxbc_spv::util::Flags<T>(a) != b; }

template<typename T, T v = T::eFlagEnum>
auto operator | (T a, dxbc_spv::util::Flags<T> b) { return dxbc_spv::util::Flags<T>(a) | b; }
template<typename T, T v = T::eFlagEnum>
auto operator & (T a, dxbc_spv::util::Flags<T> b) { return dxbc_spv::util::Flags<T>(a) & b; }
template<typename T, T v = T::eFlagEnum>
auto operator ^ (T a, dxbc_spv::util::Flags<T> b) { return dxbc_spv::util::Flags<T>(a) ^ b; }
template<typename T, T v = T::eFlagEnum>
auto operator - (T a, dxbc_spv::util::Flags<T> b) { return dxbc_spv::util::Flags<T>(a) - b; }
template<typename T, T v = T::eFlagEnum>
auto operator == (T a, dxbc_spv::util::Flags<T> b) { return dxbc_spv::util::Flags<T>(a) == b; }
template<typename T, T v = T::eFlagEnum>
auto operator != (T a, dxbc_spv::util::Flags<T> b) { return dxbc_spv::util::Flags<T>(a) != b; }

template<typename T, T v = T::eFlagEnum>
std::ostream& operator << (std::ostream& os, dxbc_spv::util::Flags<T> flags) {
  if (!flags) {
    os << "None";
  } else {
    os << flags.first();
    flags -= flags.first();

    while (flags) {
      os << "|" << flags.first();
      flags -= flags.first();
    }
  }

  return os;
}
