#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "../util/util_debug.h"
#include "../util/util_small_vector.h"

namespace dxbc_spv::ir {

/** Pointer-stable, indexable container purpose-built to store
 *  data for SSA defs inside or outside of the builder. */
template<typename T>
class Container {
  static constexpr uint32_t LayerBits = 12u;
  static constexpr uint32_t LayerMask = (1u << LayerBits) - 1u;

  static constexpr size_t LayerSize = size_t(1u) << LayerBits;

  using Array = std::array<T, LayerSize>;
public:

  Container() {
    /* Ensure the null def is valid */
    m_layers.emplace_back(std::make_shared<Array>());
  }

  /* Queries maximum valid def that this container has data for. */
  SsaDef getMaxValidDef() const {
    return m_lastDef;
  }

  /* Allocates SSA def as well as storage for it. */
  SsaDef allocSsaDef() {
    SsaDef def(m_lastDef.getId() + 1u);
    ensure(def);
    return def;
  }

  /* Ensures that there is enough space for the given definition. */
  void ensure(SsaDef def) {
    auto layer = computeAddress(def).second;

    while (layer >= m_layers.size())
      m_layers.emplace_back(std::make_shared<Array>());

    m_lastDef = def;
  }

  /* Retrieves writable reference to existing object */
  T& at(SsaDef def) {
    return get(def);
  }

  /* Retrieves read-only reference to existing object */
  const T& at(SsaDef def) const {
    return get(def);
  }

  /* Retrieves reference to object. If the given SSA def does not
   * have data in the container, the container will be resized. */
  T& operator [] (SsaDef def) {
    if (def > getMaxValidDef())
      ensure(def);

    return get(def);
  }

  /* Retrieves read-only reference to existing object. Returns the
   * reference to the null definition in case of a const context. */
  const T& operator [] (SsaDef def) const {
    if (def > getMaxValidDef())
      return get(SsaDef());

    return get(def);
  }

private:

  SsaDef m_lastDef = SsaDef();

  util::small_vector<std::shared_ptr<Array>, 16u> m_layers;

  const T& get(SsaDef def) const {
    auto [index, layer] = computeAddress(def);
    return m_layers.at(layer)->at(index);
  }

  T& get(SsaDef def) {
    auto [index, layer] = computeAddress(def);
    return m_layers.at(layer)->at(index);
  }

  static std::pair<size_t, size_t> computeAddress(SsaDef def) {
    return std::make_pair(def.getId() & LayerMask, def.getId() >> LayerBits);
  }

};

}
