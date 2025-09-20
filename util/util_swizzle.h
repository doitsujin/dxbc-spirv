#pragma once

#include <cstddef>
#include <cstdint>
#include <iostream>

#include "util_bit.h"
#include "util_debug.h"
#include "util_flags.h"

namespace dxbc_spv::util {

/** Vector component bit used in write masks */
enum class ComponentBit : uint8_t {
  eX = (1u << 0),
  eY = (1u << 1),
  eZ = (1u << 2),
  eW = (1u << 3),

  eAll = 0xfu,

  eFlagEnum = 0u
};

using WriteMask = util::Flags<ComponentBit>;


/** Helper function that extracts the first consecutive block of
 *  components from the write mask. All set bits in the returned
 *  mask will occur back to back with no gaps in between. */
WriteMask extractConsecutiveComponents(WriteMask mask);


/** Creates write mask with the lowest n components set. */
inline WriteMask makeWriteMaskForComponents(uint32_t n) {
  dxbc_spv_assert(n <= 4u);
  return WriteMask(uint8_t((1u << n) - 1u));
}


/** Vector component indices used in swizzles */
enum class Component : uint8_t {
  eX = 0u,
  eY = 1u,
  eZ = 2u,
  eW = 3u,
};


/** Convenience method to get component bit from component index */
inline ComponentBit componentBit(Component which) {
  return ComponentBit(1u << uint8_t(which));
}


/** Convenience method to get component index from component bit */
inline Component componentFromBit(ComponentBit bit) {
  return Component(util::tzcnt(uint32_t(bit)));
}


/** Vector swizzle */
class Swizzle {

public:

  Swizzle() = default;

  /** Creates vector swizzle from raw byte encoding */
  explicit Swizzle(uint8_t raw)
  : m_raw(raw) { }

  /** Creates swizzle from given component indices */
  Swizzle(Component x, Component y, Component z, Component w)
  : m_raw(uint8_t(x) | (uint8_t(y) << 2u) | (uint8_t(z) << 4u) | (uint8_t(w) << 6u)) { }

  /** Swizzle that replicates a single component */
  explicit Swizzle(Component c)
  : Swizzle(c, c, c, c) { }

  /** Retrieves named components. */
  Component x() const { return Component(util::bextract(m_raw, 0u, 2u)); }
  Component y() const { return Component(util::bextract(m_raw, 2u, 2u)); }
  Component z() const { return Component(util::bextract(m_raw, 4u, 2u)); }
  Component w() const { return Component(util::bextract(m_raw, 6u, 2u)); }

  /** Retrieves component mapping for a given component index. */
  Component get(uint32_t index) const {
    return Component(util::bextract(m_raw, 2u * index, 2u));
  }

  /** Retieves swizzled component for a source component. */
  Component map(Component which) const {
    return get(uint8_t(which));
  }

  /** Retieves swizzled component for a component bit. */
  Component map(ComponentBit which) const {
    return map(componentFromBit(which));
  }

  /** Computes mask of read components, given a write mask of the instruction
   *  using the swizzled operands. For example, if only the swizzled X component
   *  is accessed and is mapped to Z, then the resulting mask will only have the
   *  Z component set. Useful to determine which parts of an operand to load. */
  WriteMask getReadMask(WriteMask accessMask) const;

  /** Compacts swizzle according to the given access mask. As an example, if the
   *  access mask is binary 1ß10, then the y and w mappings will be moved to the
   *  x and y mappings, respectively. Higher mappings are undefined. */
  Swizzle compact(WriteMask accessMask) const;

  /** Checks whether a given component is included in the swizzle */
  bool contains(Component which, WriteMask accessMask) const {
    return bool(getReadMask(accessMask) & componentBit(which));
  }

  /** Retrieves raw byte encoding */
  explicit operator uint8_t () const {
    return m_raw;
  }

  /** Compares two swizzles */
  bool operator == (const Swizzle& other) const { return m_raw == other.m_raw; }
  bool operator != (const Swizzle& other) const { return m_raw != other.m_raw; }

  /** Creates identity swizzle */
  static Swizzle identity() {
    return Swizzle(Component::eX, Component::eY, Component::eZ, Component::eW);
  }

private:

  uint8_t m_raw = 0u;

};

std::ostream& operator << (std::ostream& os, Component component);
std::ostream& operator << (std::ostream& os, ComponentBit component);
std::ostream& operator << (std::ostream& os, WriteMask mask);
std::ostream& operator << (std::ostream& os, Swizzle swizzle);

}
