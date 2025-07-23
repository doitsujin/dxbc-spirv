#pragma once

#include <algorithm>
#include <optional>
#include <type_traits>
#include <utility>

#include "../ir/ir.h"

#include "../util/util_bit.h"
#include "../util/util_byte_stream.h"
#include "../util/util_fourcc.h"
#include "../util/util_small_vector.h"

#include "dxbc_types.h"

namespace dxbc_spv::dxbc {

/* Signature representation of system values. This is notably
 * different from the shader bytecode representation of the
 * same system values. */
enum class SignatureSysval : uint32_t {
  eNone                       = 0u,
  ePosition                   = 1u,
  eClipDistance               = 2u,
  eCullDistance               = 3u,
  eRenderTargetArrayIndex     = 4u,
  eViewportIndex              = 5u,
  eVertexId                   = 6u,
  ePrimitiveId                = 7u,
  eInstanceId                 = 8u,
  eIsFrontFace                = 9u,
  eSampleIndex                = 10u,
  eQuadEdgeTessFactor         = 11u,
  eQuadInsideTessFactor       = 12u,
  eTriEdgeTessFactor          = 13u,
  eTriInsideTessFactor        = 14u,
  eLineDetailTessFactor       = 15u,
  eLineDensityTessFactor      = 16u,
  eBarycentrics               = 23u,
  eShadingRate                = 24u,
  eCullPrimitive              = 25u,
  eTarget                     = 64u,
  eDepth                      = 65u,
  eCoverage                   = 66u,
  eDepthGreaterEqual          = 67u,
  eDepthLessEqual             = 68u,
  eStencilRef                 = 69u,
  eInnerCoverage              = 70u,
};

std::optional<Sysval> resolveSignatureSysval(SignatureSysval sv, uint32_t semanticIndex);


/* I/O signature entry */
class SignatureEntry {

public:

  SignatureEntry();

  /** Parses signature entry from DXBC binary */
  SignatureEntry(
          util::FourCC        tag,
          util::ByteReader&   reader);

  /** Creates signature entry. Intended to be
   *  used only when emitting DXBC binaries. */
  SignatureEntry(
    const char*               semanticName,
          uint32_t            semanticIndex,
          int32_t             registerIndex,
          uint32_t            streamIndex,
          uint32_t            componentMask,
          SignatureSysval     systemValue,
          ir::ScalarType      scalarType);

  ~SignatureEntry();

  /** Semantic name */
  const char* getSemanticName() const {
    return m_semanticName.data();
  }

  /** Semantic index */
  uint32_t getSemanticIndex() const {
    return m_semanticIndex;
  }

  /** I/O register index, if applicable. For some
   *  system values, this may actually be -1. */
  int32_t getRegisterIndex() const {
    return m_registerIndex;
  }

  /** Geometry stream index. Will be 0 outside of geometry
   *  shader output signature entries. */
  uint32_t getStreamIndex() const {
    return m_streamIndex;
  }

  /** Queries system value. */
  SignatureSysval getSystemValue() const {
    return m_systemValue;
  }

  /** Components declared by shader */
  WriteMask getComponentMask() const {
    return WriteMask(uint8_t(util::bextract(m_componentMask, 0u, 4u)));
  }

  /** Component use mask. For input signatures, this is the mask of
   *  components read by the shader. For output signatures, this is
   *  the mask of components not written, and may include components
   *  not declared in the actual component mask. */
  WriteMask getUsedComponentMask() const {
    return WriteMask(uint8_t(util::bextract(m_componentMask, 8u, 4u)));
  }

  /** Computes index of first declared component */
  uint32_t computeComponentIndex() const {
    return std::min(4u, util::tzcnt(uint8_t(getComponentMask())));
  }

  /** Computes number of declared components */
  uint32_t computeComponentCount() const {
    return util::popcnt(uint8_t(getComponentMask()));
  }

  /** Queries normalized scalar type. This will always be a 32-bit
   *  float or integer type, regardless of min-precision settings,
   *  or a boolean for specific system values. */
  ir::ScalarType getScalarType() const {
    switch (m_scalarType) {
      case ir::ScalarType::eMinI16: return ir::ScalarType::eI32;
      case ir::ScalarType::eMinU16: return ir::ScalarType::eU32;
      case ir::ScalarType::eMinF16: return ir::ScalarType::eF32;
      default: return m_scalarType;
    }
  }

  /** Queries raw scalar type, including min-precision information. */
  ir::ScalarType getRawScalarType() const {
    return m_scalarType;
  }

  /** Queries vector type including all vector components. */
  ir::BasicType getVectorType() const {
    return ir::BasicType(getScalarType(), computeComponentCount());
  }

  /** Checks whether this signature entry may use min precision. */
  bool isMinPrecision() const {
    return ir::BasicType(m_scalarType).isMinPrecisionType();
  }

  /** Compares semantic name to the given string.
   *  Semantic names are case-insensitive. */
  bool matches(const char* name) const;

  /** Compares semantic name and index. */
  bool matches(uint32_t stream, const char* name, uint32_t index) const {
    return stream == m_streamIndex && index == m_semanticIndex && matches(name);
  }

  /** Compares semantic name and stream. */
  bool matches(uint32_t stream, const char* name) const {
    return stream == m_streamIndex && matches(name);
  }

  /** Writes entry to byte array, without semantic name. */
  bool write(util::ByteWriter& writer, util::FourCC tag) const;

  /** Writes semantic to byte array and updates name offset. */
  bool writeName(util::ByteWriter& writer, util::FourCC tag, size_t chunkOffset, size_t entryOffset) const;

  /** Checks whether signature element is valid */
  explicit operator bool () const {
    return m_scalarType != ir::ScalarType::eUnknown;
  }

private:

  util::small_vector<char, 40u> m_semanticName;
  uint32_t                      m_semanticIndex = 0u;
  int32_t                       m_registerIndex = 0u;
  uint32_t                      m_streamIndex   = 0u;
  uint32_t                      m_componentMask = 0u;
  SignatureSysval               m_systemValue   = SignatureSysval::eNone;
  ir::ScalarType                m_scalarType    = ir::ScalarType::eUnknown;

  void resetOnError();

  static bool compareChars(char a, char b);

};


/** Signature iterator adapter to filter elements with the given predicate. */
template<typename Iter, typename Pred>
class SignatureFilter {

public:

  using iterator_category = std::forward_iterator_tag;
  using difference_type = typename std::iterator_traits<Iter>::difference_type;
  using value_type = typename std::iterator_traits<Iter>::value_type;
  using reference = typename std::iterator_traits<Iter>::reference;
  using pointer = typename std::iterator_traits<Iter>::pointer;

  SignatureFilter(Iter begin, Iter end, Pred&& pred)
  : m_cur(begin), m_end(end), m_pred(std::move(pred)) {
    if (m_cur != m_end && !pred(*m_cur))
      next();
  }

  SignatureFilter& operator ++ () {
    next();
    return *this;
  }

  SignatureFilter operator ++ (int) {
    auto result = *this;
    next();
    return result;
  }

  pointer operator -> () const { return &(*m_cur); }
  reference operator * () const { return *m_cur; }

  bool operator == (const Iter& iter) const { return m_cur == iter; }
  bool operator != (const Iter& iter) const { return m_cur != iter; }

  bool operator == (const SignatureFilter& other) const { return m_cur == other.m_cur; }
  bool operator != (const SignatureFilter& other) const { return m_cur != other.m_cur; }

  /** Applies additional filter to the filter iterator */
  template<typename Pred_>
  auto filter(Pred_&& pred) const {
    return filterJoin([a = m_pred, b = std::move(pred)] (const SignatureEntry& e) {
      return a(e) && b(e);
    });
  }

private:

  Iter m_cur;
  Iter m_end;

  Pred m_pred;

  void next() {
    do {
      m_cur++;
    } while (m_cur != m_end && !m_pred(*m_cur));
  }

  template<typename Pred_>
  SignatureFilter<Iter, Pred_> filterJoin(Pred_&& pred) const {
    return SignatureFilter<Iter, Pred_>(m_cur, m_end, std::move(pred));
  }

};


/** I/O signature */
class Signature {
  using EntryList = util::small_vector<SignatureEntry, 32u>;
public:

  using iterator = typename EntryList::const_iterator;

  Signature();

  /** Parses signature entry from DXBC binary */
  explicit Signature(util::ByteReader reader);

  /** Creates signature with the given tag. Intended to
   *  be used only when emitting DXBC binaries. */
  explicit Signature(util::FourCC tag);

  ~Signature();

  /** Queries tag of the signature chunk */
  util::FourCC getTag() const {
    return m_tag;
  }

  /** Iterators over the signature entries */
  auto begin() const { return m_entries.begin(); }
  auto end() const { return m_entries.end(); }

  /** Generic filter for signature elements. Returns an iterator
   *  adapter that only includes elements matching the predicate. */
  template<typename Pred>
  SignatureFilter<iterator, Pred> filter(Pred&& p) const {
    return SignatureFilter<iterator, Pred>(begin(), end(), std::move(p));
  }

  /** Finds signature element by name and index. Returns
   *  an end iterator if no such element is found. */
  auto findSemantic(uint32_t stream, const char* name, uint32_t index) const {
    return std::find_if(begin(), end(), [=] (const SignatureEntry& e) {
      return e.matches(stream, name, index);
    });
  }

  /** Adds signature element. Intended to be used only when
   *  emitting DXBC binaries. */
  void add(SignatureEntry e);

  /** Writes out signature chunk to memory */
  bool write(util::ByteWriter& writer) const;

  /** Checks whether signature element is valid */
  explicit operator bool () const {
    return m_tag != util::FourCC();
  }

private:

  util::FourCC  m_tag = { };
  EntryList     m_entries;

  void resetOnError();

};


std::ostream& operator << (std::ostream& os, const SignatureSysval& sv);
std::ostream& operator << (std::ostream& os, const Signature& sig);

}
