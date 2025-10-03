#include <iomanip>

#include "dxbc_container.h"
#include "dxbc_interface.h"

#include "../util/util_log.h"

namespace dxbc_spv::dxbc {

InterfaceChunk::InterfaceChunk(util::ByteReader reader) {
  ChunkHeader header(reader);
  dxbc_spv_assert(header);

  /* The data layout of this chunk type was inferred from SlimShader code by Tim Jones:
   * https://github.com/tgjones/slimshader/blob/master/src/SlimShader/Chunks/Ifce/InterfacesChunk.cs */
  util::ByteReader readerBase = reader;

  uint32_t classInstanceCount = 0u;

  uint32_t classTypeCount = 0u;
  uint32_t classTypeOffset = 0u;

  uint32_t ifaceSlotInfoCount = 0u;
  uint32_t ifaceSlotCount = 0u;
  uint32_t ifaceSlotOffset = 0u;

  uint32_t unknown = 0u;

  if (!reader.read(classInstanceCount) ||
      !reader.read(classTypeCount) ||
      !reader.read(ifaceSlotInfoCount) ||
      !reader.read(ifaceSlotCount) ||
      !reader.read(unknown) ||
      !reader.read(classTypeOffset) ||
      !reader.read(ifaceSlotOffset)) {
    resetOnError();
    return;
  }

  /* Read class type records */
  auto classReader = readerBase;
  classReader.skip(classTypeOffset);

  m_classTypes.resize(classTypeCount);

  for (uint32_t i = 0u; i < classTypeCount; i++) {
    auto classType = parseClassType(classReader, readerBase);

    if (!classType) {
      resetOnError();
      return;
    }

    m_classTypes.at(i) = *classType;
    m_classTypes.at(i).id = i;
  }

  /* Read class instance records, these immediately follow class types */
  m_classInstances.resize(classInstanceCount);

  for (uint32_t i = 0u; i < classInstanceCount; i++) {
    auto classInstance = parseClassInstance(classReader, readerBase);

    if (!classInstance) {
      resetOnError();
      return;
    }

    m_classInstances.at(i) = *classInstance;
  }

  auto ifaceReader = readerBase;
  ifaceReader.skip(ifaceSlotOffset);

  m_interfaceSlots.resize(ifaceSlotInfoCount);

  uint32_t slotIndex = 0u;

  for (uint32_t i = 0u; i < ifaceSlotInfoCount; i++) {
    auto interfaceSlot = parseInterfaceSlot(ifaceReader, readerBase);

    if (!interfaceSlot) {
      resetOnError();
      return;
    }

    m_interfaceSlots.at(i) = *interfaceSlot;
    m_interfaceSlots.at(i).index = slotIndex;

    slotIndex += m_interfaceSlots.at(i).count;
  }
}


InterfaceChunk::~InterfaceChunk() {

}


void InterfaceChunk::resetOnError() {
  m_error = true;
}


std::optional<ClassType> InterfaceChunk::parseClassType(util::ByteReader& reader, util::ByteReader base) {
  uint32_t nameOffset = 0u;

  if (!reader.read(nameOffset))
    return std::nullopt;

  auto name = parseName(base, nameOffset);

  if (!name)
    return std::nullopt;

  std::optional<ClassType> result;

  auto& typeInfo = result.emplace();
  typeInfo.name = std::move(*name);

  if (!reader.read(typeInfo.id) ||
      !reader.read(typeInfo.cbSize) ||
      !reader.read(typeInfo.srvCount) ||
      !reader.read(typeInfo.samplerCount))
    return std::nullopt;

  return result;
}


std::optional<ClassInstance> InterfaceChunk::parseClassInstance(util::ByteReader& reader, util::ByteReader base) {
  uint32_t nameOffset = 0u;

  if (!reader.read(nameOffset))
    return std::nullopt;

  auto name = parseName(base, nameOffset);

  if (!name)
    return std::nullopt;

  std::optional<ClassInstance> result;

  auto& instanceInfo = result.emplace();
  instanceInfo.name = std::move(*name);

  uint16_t unknown = 0u;

  if (!reader.read(instanceInfo.typeId) ||
      !reader.read(unknown) ||
      !reader.read(instanceInfo.cbvIndex) ||
      !reader.read(instanceInfo.cbvOffset) ||
      !reader.read(instanceInfo.srvIndex) ||
      !reader.read(instanceInfo.samplerIndex))
    return std::nullopt;

  return result;
}


std::optional<InterfaceSlot> InterfaceChunk::parseInterfaceSlot(util::ByteReader& reader, util::ByteReader base) {
  std::optional<InterfaceSlot> result;
  auto& ifaceInfo = result.emplace();

  if (!reader.read(ifaceInfo.count))
    return std::nullopt;

  uint32_t entryCount = 0u;

  uint32_t typeIdOffset = 0u;
  uint32_t tableIdOffset = 0u;

  if (!reader.read(entryCount) ||
      !reader.read(typeIdOffset) ||
      !reader.read(tableIdOffset))
    return std::nullopt;

  ifaceInfo.entries.resize(entryCount);

  auto typeIdReader = base;
  typeIdReader.skip(typeIdOffset);

  auto tableIdReader = base;
  tableIdReader.skip(tableIdOffset);

  for (uint32_t i = 0u; i < entryCount; i++) {
    auto& entry = ifaceInfo.entries.at(i);

    if (!typeIdReader.read(entry.typeId) ||
        !tableIdReader.read(entry.tableId))
      return std::nullopt;
  }

  return result;
}


std::optional<std::string> InterfaceChunk::parseName(util::ByteReader base, uint32_t offset) {
  base.skip(offset);

  std::string name;

  if (!base.readString(name))
    return std::nullopt;

  return name;
}


std::ostream& operator << (std::ostream& os, const ClassType& type) {
  os << "| " << std::dec << std::setw(2u) << std::setfill(' ') << type.id << " | ";
  os << type.name;

  for (size_t i = type.name.size(); i < 24u; i++)
    os << ' ';

  os << " | " << std::dec << std::setw(4u) << std::setfill(' ') << type.cbSize;
  os << " | " << std::dec << std::setw(3u) << std::setfill(' ') << type.srvCount;
  os << " | " << std::dec << std::setw(7u) << std::setfill(' ') << type.samplerCount << " |";
  return os;
}


std::ostream& operator << (std::ostream& os, const ClassInstance& instance) {
  os << "| " << instance.name;

  for (size_t i = instance.name.size(); i < 24u; i++)
    os << ' ';

  os << " | " << std::dec << std::setw(4u) << std::setfill(' ') << instance.typeId;
  os << " | " << std::dec << std::setw(3u) << std::setfill(' ') << instance.cbvIndex;
  os << " | " << std::dec << std::setw(6u) << std::setfill(' ') << instance.cbvOffset;
  os << " | " << std::dec << std::setw(3u) << std::setfill(' ') << instance.srvIndex;
  os << " | " << std::dec << std::setw(7u) << std::setfill(' ') << instance.samplerIndex << " |";
  return os;
}


std::ostream& operator << (std::ostream& os, const InterfaceSlot& slot) {
  os << "| " << std::dec << std::setw(2u) << std::setfill(' ') << slot.index;

  if (slot.count > 1u)
    os << " - " << std::dec << std::setw(2u) << std::setfill(' ') << (slot.index + slot.count - 1u);
  else
    os << "     ";

  os << " | ";

  uint32_t len = 0u;

  for (const auto& e : slot.entries) {
    os << e.typeId << ':' << e.tableId << ' ';

    len += e.typeId >= 100u ? 3u : (e.typeId >= 10u ? 2u : 1u);
    len += e.tableId >= 100u ? 3u : (e.tableId >= 10u ? 2u : 1u);
    len += 2u;
  }

  while (++len <= 48u)
    os << ' ';

  os << " |";
  return os;
}


std::ostream& operator << (std::ostream& os, const InterfaceChunk& chunk) {
  os << "IFCE:" << std::endl << std::endl;

  os << "Class types:" << std::endl;
  os << "| ID | Name                     | Data | SRV | Sampler |" << std::endl;
  os << "|----|--------------------------|------|-----|---------|" << std::endl;

  for (auto iter = chunk.getClassTypes().first; iter != chunk.getClassTypes().second; iter++)
    os << *iter << std::endl;

  os << std::endl << "Class instance:" << std::endl;
  os << "| Name                     | Type | CBV | Offset | SRV | Sampler |" << std::endl;
  os << "|--------------------------|------|-----|--------|-----|---------|" << std::endl;

  for (auto iter = chunk.getClassInstances().first; iter != chunk.getClassInstances().second; iter++)
    os << *iter << std::endl;

  os << std::endl << "Interface slots:" << std::endl;
  os << "| Range   | Entries                                          |" << std::endl;
  os << "|---------|--------------------------------------------------|" << std::endl;

  for (auto iter = chunk.getInterfaceSlots().first; iter != chunk.getInterfaceSlots().second; iter++)
    os << *iter << std::endl;

  return os;
}

}
