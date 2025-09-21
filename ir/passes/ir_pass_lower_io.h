#pragma once

#include <array>
#include <optional>

#include "../ir.h"
#include "../ir_builder.h"

#include "../../util/util_hash.h"

namespace dxbc_spv::ir {

/** I/O variable type */
enum class IoEntryType : uint8_t {
  ePerVertex  = 0u,
  ePerPatch   = 1u,
  eBuiltIn    = 2u,
};


/** I/O location metadata */
class IoLocation {
  static constexpr uint8_t TypeMask   = 0xc0u;
  static constexpr uint8_t TypeShift  = 6u;
  static constexpr uint8_t InfoMask   = (1u << TypeShift) - 1u;
public:

  IoLocation() = default;

  /** Creates location metadata for built-in variable. */
  explicit IoLocation(BuiltIn builtIn, uint8_t mask)
  : m_info(encodeBuiltIn(builtIn))
  , m_mask(mask) { }

  /** Creates location metadata for a regular I/O variable. */
  explicit IoLocation(IoEntryType type, uint8_t location, uint8_t mask)
  : m_info(encodeLocation(type, location))
  , m_mask(mask) { }

  /** Queries type of the I/O entry. */
  IoEntryType getType() const {
    return IoEntryType(m_info >> TypeShift);
  }

  /** Queries built-in. The returned value is undefined if
   *  this is not a built-in entry. */
  BuiltIn getBuiltIn() const {
    return BuiltIn(m_info & InfoMask);
  }

  /** Queries location index for regular (non-builtin) I/O registers.
   *  For built-ins, this will return an undefined value. */
  uint8_t getLocationIndex() const {
    return m_info & InfoMask;
  }

  /** Queries component mask for the given output. If this is a
   *  clip/cull distance or tess factor array, the number of bits
   *  set in the mask directly corresponds to the array size. */
  uint8_t getComponentMask() const {
    return m_mask;
  }

  /** Extracts first component mask bit. */
  uint8_t getFirstComponentBit() const {
    return m_mask & -m_mask;
  }

  /** Computes index of first component. */
  uint8_t getFirstComponentIndex() const {
    return util::tzcnt(getFirstComponentBit());
  }

  /** Computes number of components. */
  uint8_t computeComponentCount() const {
    return util::popcnt(m_mask);
  }

  /** Checks whether the entry should be ordered before another. This guarantees
   *  that two entries that are 'equal', i.e. neither is ordered before the other,
   *  have the same type, location or built-in, and share the first component.
   *  Only the exact component mask may differ. */
  bool isOrderedBefore(const IoLocation& other) const {
    uint16_t aBits = (uint16_t(this->m_info) << 8u) | uint16_t(this->getFirstComponentBit());
    uint16_t bBits = (uint16_t(other.m_info) << 8u) | uint16_t(other.getFirstComponentBit());

    return aBits < bBits;
  }

  /** Checks whether this overlaps another location. */
  bool overlaps(const IoLocation& other) const {
    if (getType() != other.getType())
      return false;

    bool sameLocation = (getType() == IoEntryType::eBuiltIn)
      ? getBuiltIn() == other.getBuiltIn()
      : getLocationIndex() == other.getLocationIndex();

    return sameLocation && (getComponentMask() & other.getComponentMask());
  }

  /** Checks whether the this entry fully covers another. */
  bool covers(const IoLocation& other) const {
    if (getType() != other.getType())
      return false;

    bool sameLocation = (getType() == IoEntryType::eBuiltIn)
      ? getBuiltIn() == other.getBuiltIn()
      : getLocationIndex() == other.getLocationIndex();

    return sameLocation && !(~getComponentMask() & other.getComponentMask());
  }

private:

  uint8_t m_info = 0u;
  uint8_t m_mask = 0u;

  static uint8_t encodeLocation(IoEntryType type, uint8_t location) {
    return (uint8_t(type) << TypeShift) | location;
  }

  static uint8_t encodeBuiltIn(BuiltIn b) {
    return encodeLocation(IoEntryType::eBuiltIn, uint32_t(b));
  }

};


/** I/O map of a given shader. Can also be used to represent vertex input,
 *  in which case every entry must be a vec4 at a unique location. */
class IoMap {

public:

  IoMap();

  ~IoMap();

  /** Adds an I/O location to the map. */
  void add(IoLocation entry);

  /** Number of entries */
  uint32_t getCount() const {
    return uint32_t(m_entries.size());
  }

  /** Queries entry */
  const IoLocation& get(uint32_t index) const {
    return m_entries.at(index);
  }

  /** Returns iterators over I/O map */
  auto begin() const { return m_entries.cbegin(); }
  auto end() const { return m_entries.end(); }

  /** Build shader I/O map for shader inputs. */
  static IoMap forInputs(const Builder& builder);

  /** Build shader I/O map for shader outputs for a given geometry stream.
   *  For non-geometry stages, the stream parameter is ignored. */
  static IoMap forOutputs(const Builder& builder, uint32_t stream);

  /** Validates I/O compatibility. Returns true if every entry of
   *  the input map is covered by an entry of the output map. */
  static bool checkCompatibility(ShaderStage prevStage, const IoMap& prevStageOut,
          ShaderStage stage, const IoMap& stageIn);

  /** Checks whether a built-in is system generated. */
  static bool builtInIsGenerated(BuiltIn builtIn, ShaderStage prevStage, ShaderStage stage);

  /** Queries shader stage for the given shader */
  static ShaderStage getStageForBuilder(const Builder& builder);

  /** Encodes I/O variable */
  static IoLocation getEntryForOp(ShaderStage stage, const Op& op);

private:

  /** Encodes built-in I/O variable */
  static IoLocation getEntryForBuiltIn(const Op& op);

  /** Encodes regular I/O variable */
  static IoLocation getEntryForLocation(ShaderStage stage, const Op& op);

  util::small_vector<IoLocation, 32> m_entries;

};


/** Streamout entry for a given geometry shader output. */
struct IoXfbInfo {
  std::string semanticName;
  uint8_t     semanticIndex = 0u;
  uint8_t     componentMask = 0u;
  uint8_t     stream = 0u;
  uint8_t     buffer = 0u;
  uint16_t    offset = 0u;
  uint16_t    stride = 0u;

  bool operator == (const IoXfbInfo& other) const {
    return semanticName == other.semanticName
        && semanticIndex == other.semanticIndex
        && componentMask == other.componentMask
        && stream == other.stream
        && buffer == other.buffer
        && offset == other.offset
        && stride == other.stride;
  }

  bool operator != (const IoXfbInfo& other) const {
    return !operator == (other);
  }
};


/** I/O semantic type */
enum class IoSemanticType : uint32_t {
  eInput  = 0u,
  eOutput = 1u,
};


/** Output component mapping for render targets */
enum class IoOutputComponent : uint8_t {
  eX    = 0u,
  eY    = 1u,
  eZ    = 2u,
  eW    = 3u,
  eOne  = 4u,
  eZero = 5u,
};

/** Output component swizzle */
struct IoOutputSwizzle {
  IoOutputComponent x = IoOutputComponent::eX;
  IoOutputComponent y = IoOutputComponent::eY;
  IoOutputComponent z = IoOutputComponent::eZ;
  IoOutputComponent w = IoOutputComponent::eW;
};


/** Pass to investigate and fix up shader I/O for various use cases.
 *
 * This includes adjusting I/O locations for tessellation shaders to meet
 * Vulkan requirements, and moving streamout locations to dedicated output
 * locations if necessary and deduplicating multi-stream GS outputs in general. */
class LowerIoPass {
  static constexpr uint32_t IoLocationCount = 32u;
public:

  LowerIoPass(Builder& builder);

  ~LowerIoPass();

  /** Retrieves input data for a given named semantic, if defined. */
  std::optional<IoLocation> getSemanticInfo(const char* name, uint32_t index, IoSemanticType type, uint32_t stream) const;

  /** Rewrites geometry shader input primitive, as well as the outer array size
   *  of any relevant input variable to match the new vertex count. */
  bool changeGsInputPrimitiveType(PrimitiveType primitiveType);

  /** Removes outputs not in the given output set. This is primarily intended for
   *  pixel shader outputs. If any outputs are removed, all code contributing to
   *  those outputs will also be removed. */
  bool resolveUnusedOutputs(const IoMap& consumedOutputs);

  /** Adjusts shader outputs for transform feedback. Outputs may be rewritten,
   *  replaced or removed in their entirety, depending on whether they actually
   *  get written to a streamout buffer or are used in the rasterized stream. */
  bool resolveXfbOutputs(size_t entryCount, const IoXfbInfo* entries, int32_t rasterizedStream);

  /** Adjusts patch constant locations for tessellation shaders. Takes pre-computed
   *  output map info of the hull shader to find unused locations. */
  bool resolvePatchConstantLocations(const IoMap& hullOutput);

  /** Rewrites undefined input variables as constant zero based on the output map of
   *  the previous stage. Can be used for vertex shader inputs as well. Also fixes up
   *  cases where an input in one shader is fully defined but incompatible with the
   *  actual output declaration. Should not be called if stages are I/O-compatible. */
  bool resolveMismatchedIo(ShaderStage prevStage, const IoMap& prevStageOut);

  /** Rewrites multisampled image bindings as single-sampled, and adjusts load
   *  instructions as well as sample count queries accordingly. */
  bool demoteMultisampledSrv();

  /** Sets flat interpolation for the given pixel shader input locations.
   *  Built-ins are unaffected. Must only be used on pixel shaders. */
  void enableFlatInterpolation(uint32_t locationMask);

  /** Sets sample interpolation on all inputs that are not already declared as flat,
   *  sample or centroid. This will enable sample-rate shading for this shader. */
  void enableSampleInterpolation();

  /** Swizzles render target outputs in pixel shaders. Needed to support certain render
   *  target formats. This works by replacing the entry point with a wrapper function
   *  whose purpose it is to change export swizzles on the fly. */
  bool swizzleOutputs(uint32_t outputCount, const IoOutputSwizzle* swizzles);

  /** Lowers sample count built-in to a specialization constant. This mostly exists
   *  for debugging purposes and standalone tools. */
  void lowerSampleCountToSpecConstant(uint32_t specId);

private:

  struct OutputInfo {
    ScalarType              scalarType      = ScalarType::eVoid;
    uint8_t                 arraySize       = 0u;
    InterpolationModes      interpolation   = { };
    std::array<uint8_t, 4u> componentCounts = { };
  };

  using OutputComponentMap = std::array<OutputInfo, IoLocationCount>;

  struct XfbComponentInfo {
    uint8_t buffer         = 0u;
    uint8_t componentIndex = 0xffu;
  };

  using XfbComponentMap = std::array<XfbComponentInfo, IoLocationCount>;

  Builder&    m_builder;

  SsaDef      m_entryPoint = { };
  ShaderStage m_stage = { };

  void scalarizeInputLoads();

  void scalarizeInputLoadsForInput(Builder::iterator op);

  Builder::iterator resolveMismatchedBuiltIn(ShaderStage prevStage, const IoMap& prevStageOut, Builder::iterator op);

  void resolveMismatchedLocation(IoEntryType type, uint32_t location, const OutputInfo& outputs);

  void resolveMismatchedIoVar(const Op& oldVar, uint32_t oldComponent, SsaDef newVar, SsaDef newComponent);

  void rewriteBuiltInInputToZero(Builder::iterator op, uint32_t firstComponent);

  bool emitXfbForOutput(size_t entryCount, const IoXfbInfo* entries, Builder::iterator op, XfbComponentMap& map);

  const IoXfbInfo* findXfbEntry(size_t entryCount, const IoXfbInfo* entries, Builder::iterator op);

  std::pair<uint32_t, uint32_t> allocXfbOutput(XfbComponentMap& map, uint32_t buffer);

  Builder::iterator removeOutput(Builder::iterator op);

  void removeUnusedStreams();

  bool remapTessIoLocation(Builder::iterator op, uint32_t perPatchMask, uint32_t perVertexMask);

  bool rewriteMultisampledDescriptorUse(SsaDef descriptorDef);

  static uint32_t getStreamForIoVariable(const Op& op);

  static bool builtInIsArray(BuiltIn builtIn);

  static bool inputNeedsComponentIndex(const Op& op);

};

}

namespace std {

template<>
struct hash<dxbc_spv::ir::IoXfbInfo> {
  size_t operator () (const dxbc_spv::ir::IoXfbInfo& xfb) const {
    size_t hash = std::hash<std::string>()(xfb.semanticName);
    hash = dxbc_spv::util::hash_combine(hash, xfb.semanticIndex);
    hash = dxbc_spv::util::hash_combine(hash, xfb.componentMask);
    hash = dxbc_spv::util::hash_combine(hash, xfb.stream);
    hash = dxbc_spv::util::hash_combine(hash, xfb.buffer);
    hash = dxbc_spv::util::hash_combine(hash, xfb.offset);
    hash = dxbc_spv::util::hash_combine(hash, xfb.stride);
    return hash;
  }
};

}
