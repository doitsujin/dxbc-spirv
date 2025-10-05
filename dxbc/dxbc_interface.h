#pragma once

#include <optional>

#include "../util/util_bit.h"
#include "../util/util_byte_stream.h"
#include "../util/util_small_vector.h"

namespace dxbc_spv::dxbc {

/** Data layout of 'this' entries */
struct InstanceData {
  constexpr static uint32_t CbvIndexShift = 0u;
  constexpr static uint32_t CbvIndexCount = 4u;
  constexpr static uint32_t CbvOffsetShift = 16u;
  constexpr static uint32_t CbvOffsetCount = 16u;
  constexpr static uint32_t SrvIndexShift = 8u;
  constexpr static uint32_t SrvIndexCount = 8u;
  constexpr static uint32_t SamplerIndexShift = 4u;
  constexpr static uint32_t SamplerIndexCount = 4u;
  constexpr static uint32_t DefaultFunctionTable = -1u;

  InstanceData() = default;

  InstanceData(uint32_t cbv, uint32_t offset, uint32_t srv, uint32_t sampler, uint32_t ft)
  : data((cbv << CbvIndexShift) | (offset  << CbvOffsetShift) | (srv << SrvIndexShift) | (sampler << SamplerIndexShift))
  , functionTable(ft) { }

  uint32_t data = 0u;
  uint32_t functionTable = DefaultFunctionTable;
};


/** Class metadata */
struct ClassType {
  /* Class name */
  std::string name;
  /* Unique type ID */
  uint16_t id = 0u;
  /* Constant data size per instance */
  uint16_t cbSize = 0u;
  /* Number of resource views per instance */
  uint16_t srvCount = 0u;
  /* Number of samplers per instance */
  uint16_t samplerCount = 0u;
};


/** Instance metadata */
struct ClassInstance {
  /* Instance name */
  std::string name;
  /* Type ID */
  uint16_t typeId = 0u;
  /* Constant buffer index for instance data */
  int16_t cbvIndex = 0u;
  /* Constant data offset for instance data */
  uint16_t cbvOffset = 0u;
  /* Base shader resource binding */
  int16_t srvIndex = 0u;
  /* Base sampler binding */
  int16_t samplerIndex = 0u;
};


/** Interface type info. Lists types and valid
 *  function tables to use for each valid type. */
struct InterfaceType {
  uint16_t typeId = 0u;
  uint32_t tableId = 0u;
};


/** Interface slot infos. Each slot can receive
 *  a class instance at runtime. */
struct InterfaceSlot {
  uint32_t index = 0u;
  uint32_t count = 0u;
  util::small_vector<InterfaceType, 12u> entries;
};


/** Interface chunk */
class InterfaceChunk {

public:

  explicit InterfaceChunk(util::ByteReader reader);

  ~InterfaceChunk();

  /** Enumerates unique class types */
  auto getClassTypes() const {
    return std::make_pair(m_classTypes.begin(), m_classTypes.end());
  }

  /** Enumerates shader-defined class instances */
  auto getClassInstances() const {
    return std::make_pair(m_classInstances.begin(), m_classInstances.end());
  }

  /** Enumerates interface slots */
  auto getInterfaceSlots() const {
    return std::make_pair(m_interfaceSlots.begin(), m_interfaceSlots.end());
  }

  /** Checks whether there are any interface slots */
  bool hasInterfaces() const {
    return !m_interfaceSlots.empty();
  }

  /** Checks whether parsing was successful */
  explicit operator bool () const {
    return !m_error;
  }

private:

  bool m_error = false;

  std::vector<ClassType>      m_classTypes;
  std::vector<ClassInstance>  m_classInstances;
  std::vector<InterfaceSlot>  m_interfaceSlots;

  void resetOnError();

  static std::optional<ClassType> parseClassType(util::ByteReader& reader, util::ByteReader base);

  static std::optional<ClassInstance> parseClassInstance(util::ByteReader& reader, util::ByteReader base);

  static std::optional<InterfaceSlot> parseInterfaceSlot(util::ByteReader& reader, util::ByteReader base);

  static std::optional<std::string> parseName(util::ByteReader base, uint32_t offset);

};

std::ostream& operator << (std::ostream& os, const ClassType& type);
std::ostream& operator << (std::ostream& os, const ClassInstance& instance);
std::ostream& operator << (std::ostream& os, const InterfaceSlot& instance);
std::ostream& operator << (std::ostream& os, const InterfaceChunk& chunk);

}
