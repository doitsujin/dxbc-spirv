#include <sstream>

#include "dxbc_container.h"
#include "dxbc_signature.h"

#include "../util/util_bit.h"
#include "../util/util_log.h"

namespace dxbc_spv::dxbc {

static bool signatureHasStreamIndex(util::FourCC tag) {
  return tag == util::FourCC("ISG1") ||
         tag == util::FourCC("OSG1") ||
         tag == util::FourCC("PSG1") ||
         tag == util::FourCC("OSG5");
}


static bool signatureHasMinPrecision(util::FourCC tag) {
  return tag == util::FourCC("ISG1") ||
         tag == util::FourCC("OSG1") ||
         tag == util::FourCC("PSG1");
}


std::optional<Sysval> resolveSignatureSysval(SignatureSysval sv, uint32_t semanticIndex) {
  switch (sv) {
    case SignatureSysval::eNone:
      return Sysval::eNone;

    case SignatureSysval::ePosition:
      return Sysval::ePosition;

    case SignatureSysval::eClipDistance:
      return Sysval::eClipDistance;

    case SignatureSysval::eCullDistance:
      return Sysval::eCullDistance;

    case SignatureSysval::eRenderTargetArrayIndex:
      return Sysval::eRenderTargetId;

    case SignatureSysval::eViewportIndex:
      return Sysval::eViewportId;

    case SignatureSysval::eVertexId:
      return Sysval::eVertexId;

    case SignatureSysval::ePrimitiveId:
      return Sysval::ePrimitiveId;

    case SignatureSysval::eInstanceId:
      return Sysval::eInstanceId;

    case SignatureSysval::eIsFrontFace:
      return Sysval::eIsFrontFace;

    case SignatureSysval::eSampleIndex:
      return Sysval::eSampleIndex;

    case SignatureSysval::eQuadEdgeTessFactor:
      return Sysval(uint32_t(Sysval::eQuadU0EdgeTessFactor) + semanticIndex);

    case SignatureSysval::eQuadInsideTessFactor:
      return Sysval(uint32_t(Sysval::eQuadUInsideTessFactor) + semanticIndex);

    case SignatureSysval::eTriEdgeTessFactor:
      return Sysval(uint32_t(Sysval::eTriUEdgeTessFactor) + semanticIndex);

    case SignatureSysval::eTriInsideTessFactor:
      return Sysval::eTriInsideTessFactor;

    case SignatureSysval::eLineDetailTessFactor:
      return Sysval::eLineDetailTessFactor;

    case SignatureSysval::eLineDensityTessFactor:
      return Sysval::eLineDensityTessFactor;

    case SignatureSysval::eBarycentrics:
    case SignatureSysval::eShadingRate:
    case SignatureSysval::eCullPrimitive:
    case SignatureSysval::eTarget:
    case SignatureSysval::eDepth:
    case SignatureSysval::eCoverage:
    case SignatureSysval::eDepthGreaterEqual:
    case SignatureSysval::eDepthLessEqual:
    case SignatureSysval::eStencilRef:
    case SignatureSysval::eInnerCoverage:
      break;
  }

  return std::nullopt;
}



SignatureEntry::SignatureEntry() {
  m_semanticName.push_back('\0');
}


SignatureEntry::SignatureEntry(
        util::FourCC        tag,
        util::ByteReader&   reader) {
  /* Read individual fields */
  if (signatureHasStreamIndex(tag))
    reader.read(m_streamIndex);

  uint32_t nameOffset = 0u;
  reader.read(nameOffset);
  reader.read(m_semanticIndex);
  reader.read(m_systemValue);

  ComponentType componentType = ComponentType::eVoid;
  reader.read(componentType);

  reader.read(m_registerIndex);
  reader.read(m_componentMask);

  /* Precision seems to be encoded as a plain enum
   * value, no per-component shenanigans going on */
  MinPrecision precision = MinPrecision::eNone;

  if (signatureHasMinPrecision(tag))
    reader.read(precision);

  if (!reader) {
    resetOnError();
    return;
  }

  /* Work out what the actual underlying data type is */
  m_scalarType = resolveType(componentType, precision);

  /* Read null-terminated name from given offset
   * and without disturbing the original reader */
  util::ByteReader nameReader = reader;
  nameReader.moveTo(nameOffset + sizeof(ChunkHeader));
  nameReader.readString(m_semanticName);
  m_semanticName.push_back('\0');

  if (!nameReader) {
    resetOnError();
    return;
  }
}


SignatureEntry::SignatureEntry(
  const char*               semanticName,
        uint32_t            semanticIndex,
        int32_t             registerIndex,
        uint32_t            streamIndex,
        uint32_t            componentMask,
        SignatureSysval     systemValue,
        ir::ScalarType      scalarType)
: m_semanticIndex(semanticIndex)
, m_registerIndex(registerIndex)
, m_streamIndex(streamIndex)
, m_componentMask(componentMask)
, m_systemValue(systemValue)
, m_scalarType(scalarType) {
  for (size_t i = 0u; semanticName[i]; i++)
    m_semanticName.push_back(semanticName[i]);
  m_semanticName.push_back('\0');
}


SignatureEntry::~SignatureEntry() {

}


bool SignatureEntry::matches(const char* name) const {
  size_t index = 0u;

  while (name[index] && index < m_semanticName.size()) {
    if (!util::compareCharsCaseInsensitive(name[index], m_semanticName[index]))
      return false;

    index++;
  }

  return !name[index] && index < m_semanticName.size() && !m_semanticName[index];
}


bool SignatureEntry::write(util::ByteWriter& writer, util::FourCC tag) const {
  bool result = true;

  if (signatureHasStreamIndex(tag))
    result = result && writer.write(m_streamIndex);

  /* Insert dummy name offset for now */
  auto [componentType, precision] = determineComponentType(m_scalarType);

  result = result && writer.write(0u) &&
                     writer.write(m_semanticIndex) &&
                     writer.write(m_systemValue) &&
                     writer.write(componentType) &&
                     writer.write(m_registerIndex) &&
                     writer.write(m_componentMask);

  if (signatureHasMinPrecision(tag))
    result = result && writer.write(precision);

  return result;
}


bool SignatureEntry::writeName(util::ByteWriter& writer, util::FourCC tag, size_t chunkOffset, size_t entryOffset) const {
  auto nameOffset = writer.moveToEnd();

  if (!writer.write(m_semanticName.size(), m_semanticName.data()))
    return false;

  if (signatureHasStreamIndex(tag))
    entryOffset += sizeof(m_streamIndex);

  writer.moveTo(entryOffset);
  return writer.write(uint32_t(nameOffset - chunkOffset));
}


void SignatureEntry::resetOnError() {
  /* Reset everything to defaults if parsing failed */
  *this = SignatureEntry();
}



Signature::Signature() {

}


Signature::Signature(util::ByteReader reader) {
  ChunkHeader header(reader);
  dxbc_spv_assert(header);

  uint32_t entryCount = 0u;
  reader.read(entryCount);

  uint32_t unknown = 0u;
  reader.read(unknown);

  if (!reader) {
    Logger::err("Failed to read signature header.");

    resetOnError();
    return;
  }

  m_entries.reserve(entryCount);

  for (uint32_t i = 0u; i < entryCount; i++) {
    SignatureEntry e(header.tag, reader);

    if (!e) {
      Logger::err("Failed to parse signature element.");

      resetOnError();
      return;
    }

    m_entries.push_back(std::move(e));
  }

  m_tag = header.tag;
}


Signature::Signature(util::FourCC tag)
: m_tag(tag) {

}


Signature::~Signature() {

}


void Signature::add(SignatureEntry e) {
  m_entries.push_back(std::move(e));
}


bool Signature::write(util::ByteWriter& writer) const {
  auto headerOffset = writer.moveToEnd();

  /* We don't know the byte size yet, update later. */
  ChunkHeader header = { };
  header.tag = m_tag;

  if (!header.write(writer))
    return false;

  /* Start of the signature chunk data, relevant for offset calculations. */
  auto dataOffset = writer.moveToEnd();

  if (!writer.write(uint32_t(m_entries.size())) ||
      !writer.write(uint32_t(8u)))
    return false;

  /* Write out signature entries without any names */
  util::small_vector<size_t, EntryList::EmbeddedCapacity> entryOffsets;

  for (const auto& e : m_entries) {
    entryOffsets.push_back(writer.moveToEnd());

    if (!e.write(writer, m_tag))
      return false;
  }

  /* Write out semantic names */
  size_t index = 0u;

  for (const auto& e : m_entries) {
    if (!e.writeName(writer, m_tag, dataOffset, entryOffsets[index++]))
      return false;
  }

  /* Compute data size and update chunk header */
  header.size = writer.moveToEnd() - dataOffset;

  writer.moveTo(headerOffset);
  return header.write(writer);
}


void Signature::resetOnError() {
  m_tag = util::FourCC();
  m_entries.clear();
}


template<typename... T>
static std::ostream& printLeftPad(std::ostream& os, size_t len, const T&... args) {
  std::stringstream str;
  ((str << args), ...);

  auto string = str.str();

  for (size_t i = string.size(); i < len; i++)
    os << ' ';

  os << string;
  return os;
}


template<typename... T>
static std::ostream& printRightPad(std::ostream& os, size_t len, const T&... args) {
  std::stringstream str;
  ((str << args), ...);

  auto string = str.str();
  os << string;

  for (size_t i = string.size(); i < len; i++)
    os << ' ';

  return os;
}


static std::ostream& printMask(std::ostream& os, WriteMask mask) {
  for (uint32_t i = 0u; i < 4u; i++) {
    if (mask & componentBit(Component(i)))
      os << Component(i);
    else
      os << ' ';
  }

  return os;
}


std::ostream& operator << (std::ostream& os, const SignatureSysval& sv) {
  switch (sv) {
    case SignatureSysval::eNone:                    return os << "None";
    case SignatureSysval::ePosition:                return os << "Position";
    case SignatureSysval::eClipDistance:            return os << "ClipDistance";
    case SignatureSysval::eCullDistance:            return os << "CullDistance";
    case SignatureSysval::eRenderTargetArrayIndex:  return os << "RenderTargetArrayIndex";
    case SignatureSysval::eViewportIndex:           return os << "ViewportIndex";
    case SignatureSysval::eVertexId:                return os << "VertexId";
    case SignatureSysval::ePrimitiveId:             return os << "PrimitiveId";
    case SignatureSysval::eInstanceId:              return os << "InstanceId";
    case SignatureSysval::eIsFrontFace:             return os << "IsFrontFace";
    case SignatureSysval::eSampleIndex:             return os << "SampleIndex";
    case SignatureSysval::eQuadEdgeTessFactor:      return os << "QuadEdgeTessFactor";
    case SignatureSysval::eQuadInsideTessFactor:    return os << "QuadInsideTessFactor";
    case SignatureSysval::eTriEdgeTessFactor:       return os << "TriEdgeTessFactor";
    case SignatureSysval::eTriInsideTessFactor:     return os << "TriInsideTessFactor";
    case SignatureSysval::eLineDetailTessFactor:    return os << "LineDetailTessFactor";
    case SignatureSysval::eLineDensityTessFactor:   return os << "LineDensityTessFactor";
    case SignatureSysval::eBarycentrics:            return os << "Barycentrics";
    case SignatureSysval::eShadingRate:             return os << "ShadingRate";
    case SignatureSysval::eCullPrimitive:           return os << "CullPrimitive";
    case SignatureSysval::eTarget:                  return os << "Target";
    case SignatureSysval::eDepth:                   return os << "Depth";
    case SignatureSysval::eCoverage:                return os << "Coverage";
    case SignatureSysval::eDepthGreaterEqual:       return os << "DepthGreaterEqual";
    case SignatureSysval::eDepthLessEqual:          return os << "DepthLessEqual";
    case SignatureSysval::eStencilRef:              return os << "StencilRef";
    case SignatureSysval::eInnerCoverage:           return os << "InnerCoverage";
  }

  return os << "SignatureSysval(" << uint32_t(sv) << ")";
}


std::ostream& operator << (std::ostream& os, const Signature& sig) {
  static const std::array<std::pair<const char*, uint32_t>, 7u> s_headers = {{
    { "Stream",    6u },
    { "Semantic", 26u },
    { "Reg",       3u },
    { "Mask",      4u },
    { "Used",      4u },
    { "Type",     12u },
    { "Sysval",   32u },
  }};

  os << sig.getTag() << ":" << std::endl;

  if (sig.begin() == sig.end())
    return os << "  empty" << std::endl;

  /* Print header row */
  for (const auto& e : s_headers) {
    os << "| ";
    printRightPad(os, e.second + 1u, e.first);
  }

  os << '|' << std::endl;

  for (const auto& e : s_headers) {
    os << '|';

    for (uint32_t i = 0u; i < e.second + 2u; i++)
      os << '-';
  }

  os << '|' << std::endl;

  for (const auto& e : sig) {
    os << "| ";

    printLeftPad(os, s_headers.at(0u).second, e.getStreamIndex()) << " | ";

    if (e.getSemanticIndex())
      printRightPad(os, s_headers.at(1u).second, e.getSemanticName(), e.getSemanticIndex()) << " | ";
    else
      printRightPad(os, s_headers.at(1u).second, e.getSemanticName()) << " | ";

    printLeftPad(os, s_headers.at(2u).second, e.getRegisterIndex()) << " | ";
    printMask(os, e.getComponentMask()) << " | ";
    printMask(os, e.getUsedComponentMask()) << " | ";
    printRightPad(os, s_headers.at(5u).second, ir::BasicType(e.getRawScalarType(), e.getVectorType().getVectorSize())) << " | ";
    printRightPad(os, s_headers.at(6u).second, e.getSystemValue()) << " | ";
    os << std::endl;
  }

  return os;
}

}
