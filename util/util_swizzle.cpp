#include "util_swizzle.h"

namespace dxbc_spv::util {

WriteMask extractConsecutiveComponents(WriteMask mask) {
  if (!mask)
    return WriteMask();

  auto raw = uint8_t(mask);

  /* Isolate lowest set bit in the mask */
  auto first = raw & -raw;

  /* Add lowest set bit to the raw mask, the resulting number will have
   * the first 1 bit past the end of the first consecutive block */
  auto block = (raw + first);

  /* Isolate that new end-of-block bit and subtract 1 to create
   * a mask of bits ending at the desired block */
  block &= -block;
  block -= 1u;

  /* Mask out the lowest bits not set in the original mask */
  return WriteMask(block & raw);
}




WriteMask Swizzle::getReadMask(WriteMask accessMask) const {
  WriteMask readMask = 0u;

  for (uint32_t i = 0u; i < 4u; i++) {
    auto component = Component(i);

    if (accessMask & componentBit(component))
      readMask |= componentBit(map(component));
  }

  return readMask;
}


Swizzle Swizzle::compact(WriteMask accessMask) const {
  uint8_t swizzle = 0u;
  uint8_t shift = 0u;

  for (uint32_t i = 0u; i < 4u; i++) {
    auto component = Component(i);

    if (accessMask & componentBit(component)) {
      swizzle |= util::bextract(m_raw, 2u * i, 2u) << shift;
      shift += 2u;
    }
  }

  return Swizzle(swizzle);
}


std::ostream& operator << (std::ostream& os, Component component) {
  switch (component) {
    case Component::eX: return os << 'x';
    case Component::eY: return os << 'y';
    case Component::eZ: return os << 'z';
    case Component::eW: return os << 'w';
  }

  return os << "Component(" << uint32_t(uint8_t(component)) << ")";
}

std::ostream& operator << (std::ostream& os, ComponentBit component) {
  switch (component) {
    case ComponentBit::eX: return os << 'x';
    case ComponentBit::eY: return os << 'y';
    case ComponentBit::eZ: return os << 'z';
    case ComponentBit::eW: return os << 'w';

    case ComponentBit::eAll:
    case ComponentBit::eFlagEnum: break;
  }

  return os << "ComponentBit(" << uint32_t(uint8_t(component)) << ")";
}

std::ostream& operator << (std::ostream& os, WriteMask mask) {
  for (uint32_t i = 0u; i < 4u; i++) {
    if (mask & componentBit(Component(i)))
      os << Component(i);
  }

  return os;
}

std::ostream& operator << (std::ostream& os, Swizzle swizzle) {
  for (uint32_t i = 0u; i < 4u; i++)
    os << swizzle.get(i);

  return os;
}

}
