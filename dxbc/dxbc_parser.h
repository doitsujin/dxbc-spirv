#pragma once

#include <array>
#include <iostream>
#include <type_traits>

#include "dxbc_types.h"

#include "../util/util_byte_stream.h"
#include "../util/util_debug.h"
#include "../util/util_small_vector.h"

namespace dxbc_spv::dxbc {

class Instruction;

/** Bit that indicates that an opcode or operand
 *  token is extended with another dword */
constexpr uint32_t ExtendedTokenBit = 1u << 31;


/** Program type */
enum class ShaderType : uint32_t {
  ePixel    = 0u,
  eVertex   = 1u,
  eGeometry = 2u,
  eHull     = 3u,
  eDomain   = 4u,
  eCompute  = 5u,
};


/** Translate DXBC program type to internal IR shader stage.
 *  Our IR uses the same enum order, but as a bit mask. */
inline ir::ShaderStage resolveShaderStage(ShaderType type) {
  return ir::ShaderStage(1u << uint32_t(type));
}


/** Shader code header */
class ShaderInfo {

public:

  constexpr static uint32_t DwordCount = 2u;

  ShaderInfo() = default;

  /** Constructs shader info. Note that the dword count must include
   *  the two dwords consumed by the info structure itself. */
  ShaderInfo(ShaderType type, uint32_t major, uint32_t minor, uint32_t dwordCount)
  : m_versionToken((uint32_t(type) << 16) | ((major & 0xfu) << 4u) | (minor & 0xfu))
  , m_lengthToken (dwordCount) { }

  /** Parses shader info */
  explicit ShaderInfo(util::ByteReader& reader);

  /** Extracts shader type from version token */
  ShaderType getType() const {
    return ShaderType(util::bextract(m_versionToken, 16u, 16u));
  }

  /** Extracts shader model version from version token */
  std::pair<uint32_t, uint32_t> getVersion() const {
    return std::make_pair(
      util::bextract(m_versionToken, 4u, 4u),
      util::bextract(m_versionToken, 0u, 4u));
  }

  /** Retrieves total dword count, including the two dwords consumed
   *  by the shader info structure itself. */
  uint32_t getDwordCount() const {
    return m_lengthToken;
  }

  /** Updates dword count. Useful when building shader binaries. */
  void setDwordCount(uint32_t n) {
    m_lengthToken = n;
  }

  /** Writes code header to binary blob. */
  bool write(util::ByteWriter& writer) const;

  /** Checks whether shader info is valid */
  explicit operator bool () const {
    return m_lengthToken >= DwordCount;
  }

private:

  uint32_t m_versionToken = 0u;
  uint32_t m_lengthToken  = 0u;

  void resetOnError();

};


/** Extended opcode type */
enum class ExtendedOpcodeType : uint32_t {
  eNone               = 0u,
  eSampleControls     = 1u,
  eResourceDim        = 2u,
  eResourceReturnType = 3u,
};

inline uint32_t makeExtendedOpcode(ExtendedOpcodeType type, uint32_t payload) {
  return uint32_t(type) | (payload << 6u);
}

inline ExtendedOpcodeType extractExtendedOpcodeType(uint32_t token) {
  return ExtendedOpcodeType(util::bextract(token, 0u, 6u));
}

inline uint32_t extractExtendedOpcodePayload(uint32_t token) {
  /* Skip the extended bit */
  return util::bextract(token, 6u, 25u);
}


/** Custom data type */
enum class CustomDataType : uint32_t {
  eComment            = 0u,
  eDebugInfo          = 1u,
  eOpaque             = 2u,
  eDclIcb             = 3u,
  eShaderMessage      = 4u,
  eShaderClipPlaneMap = 5u,
};


/** Sample control token */
class SampleControlToken {

public:

  SampleControlToken() = default;

  /** Initializes sample control token from raw payload. */
  explicit SampleControlToken(uint32_t token)
  : m_token(token) { }

  /** Initializes sample control token with the given offsets */
  SampleControlToken(int32_t u, int32_t v, int32_t w)
  : m_token((util::bextract(uint32_t(u), 0u, 4u) <<  3u) |
            (util::bextract(uint32_t(v), 0u, 4u) <<  7u) |
            (util::bextract(uint32_t(w), 0u, 4u) << 11u)) { }

  /** Queries immediate offsets for the given components */
  int32_t u() const { return signExtend(util::bextract(m_token,  3u, 4u)); }
  int32_t v() const { return signExtend(util::bextract(m_token,  7u, 4u)); }
  int32_t w() const { return signExtend(util::bextract(m_token, 11u, 4u)); }

  /** Retrieves token value as an extended token. */
  uint32_t asToken() const {
    return makeExtendedOpcode(ExtendedOpcodeType::eSampleControls, m_token);
  }

  /** Compares two sample control tokens */
  bool operator == (const SampleControlToken& other) const { return m_token == other.m_token; }
  bool operator != (const SampleControlToken& other) const { return m_token != other.m_token; }

  /** Checks whether any sample offsets are non-zero */
  explicit operator bool () const {
    return util::bextract(m_token, 3u, 12u) != 0u;
  }

private:

  uint32_t m_token = 0u;

  static int32_t signExtend(uint32_t bits) {
    return int32_t(bits | -(bits & 0x8u));
  }

};


/** Resource dimension token.
 *
 * Used as an extended opcode token for some resource ops. */
class ResourceDimToken {

public:

  ResourceDimToken() = default;

  /** Initializes resource dimension token from raw payload. */
  explicit ResourceDimToken(uint32_t token)
  : m_token(token) { }

  /** Initializes resource dimension token with the given properties. */
  ResourceDimToken(ResourceDim dim, uint32_t structureStride)
  : m_token(uint32_t(dim)) {
    if (dim == ResourceDim::eStructuredBuffer)
      m_token |= structureStride << 5u;
  }

  /** Extracts resource dimension */
  ResourceDim getDim() const {
    return ResourceDim(util::bextract(m_token, 0u, 5u));
  }

  /** Extracts structure byte stride. Only valid if the
   *  resource dimension is structured buffer. */
  uint32_t getStructureStride() const {
    return util::bextract(m_token, 5u, 12u);
  }

  /** Retrieves token value as an extended token. */
  uint32_t asToken() const {
    return makeExtendedOpcode(ExtendedOpcodeType::eResourceDim, m_token);
  }

  /** Compares two resource dimension tokens */
  bool operator == (const ResourceDimToken& other) const { return m_token == other.m_token; }
  bool operator != (const ResourceDimToken& other) const { return m_token != other.m_token; }

  /** Checks whether extended token declares anything. */
  explicit operator bool() const {
    return m_token != 0u;
  }

private:

  uint32_t m_token = 0u;

};


/** Resource return type token.
 *
 * This is an immediate operand following resource declarations,
 * which encodes per-component resource return types. Also used
 * as an extended opcode token for some resource access ops. */
class ResourceTypeToken {

public:

  ResourceTypeToken() = default;

  /** Initializes resource type token from raw payload. */
  explicit ResourceTypeToken(uint32_t token)
  : m_token(token) { }

  /** Initializes token with per-component return values. */
  ResourceTypeToken(SampledType x, SampledType y, SampledType z, SampledType w)
  : m_token((uint32_t(x) << 0u) | (uint32_t(y) <<  4u) |
            (uint32_t(z) << 8u) | (uint32_t(w) << 12u)) { }

  /** Queries return type for any given component */
  SampledType x() const { return SampledType(util::bextract(m_token,  0u, 4u)); }
  SampledType y() const { return SampledType(util::bextract(m_token,  4u, 4u)); }
  SampledType z() const { return SampledType(util::bextract(m_token,  8u, 4u)); }
  SampledType w() const { return SampledType(util::bextract(m_token, 12u, 4u)); }

  /** Retrieves token value as an extended token. */
  uint32_t asToken() const {
    return makeExtendedOpcode(ExtendedOpcodeType::eResourceReturnType, m_token);
  }

  /** Retrieves raw token value for immmediate encoding. */
  uint32_t asImmediate() const {
    return m_token;
  }

  /** Compares two resource type tokens */
  bool operator == (const ResourceTypeToken& other) const { return m_token == other.m_token; }
  bool operator != (const ResourceTypeToken& other) const { return m_token != other.m_token; }

  /** Checks whether extended token declares anything. */
  explicit operator bool() const {
    return m_token != 0u;
  }

private:

  uint32_t m_token = 0u;

};


/** Opcode token, including extended tokens.
 *
 * Provides methods to query and modify the opcode-specific bit fields,
 * but does not validate which bit fields are actually valid for any
 * given instruction.
 *
 * Also provides methods to query and set the instruction length, in
 * dwords. The intention is for this to get used both for parsing and
 * encoding instruction streams. */
class OpToken {

public:

  OpToken() = default;

  /** Initializes opcode token with opcode. All other properties
   *  must be set manually, including the instruction length. */
  explicit OpToken(OpCode opCode)
  : m_token(uint32_t(opCode)) { }

  /** Initializes opcode token from raw dword. Useful to determine
   *  initial properties of the opcode token without parsing the
   *  entire thing. */
  explicit OpToken(uint32_t token)
  : m_token(token) { }

  /** Parses opcode token in byte stream. */
  explicit OpToken(util::ByteReader& reader);


  /** Extracts opcode from token */
  OpCode getOpCode() const {
    return OpCode(util::bextract(m_token, 0u, 11u));
  }


  /** Checks whether the instruction is a custom data declaration.
   *  Custom data ops do not use regular operand tokens, instead
   *  they declare a binary blob that needs to be parsed. */
  bool isCustomData() const {
    return getOpCode() == OpCode::eCustomData;
  }


  /** Custom data type if the instruction declares custom data. */
  CustomDataType getCustomDataType() const {
    return isCustomData()
      ? CustomDataType(util::bextract(m_token, 11u, 21u))
      : CustomDataType();
  }

  OpToken& setCustomDataType(CustomDataType type) {
    if (isCustomData())
      m_token = util::binsert(m_token, uint32_t(type), 11u, 21u);

    return *this;
  }


  /** Number of dword tokens, including the opcode tokens.
   *  Handles both regular and custom data instructions. */
  uint32_t getLength() const {
    return isCustomData() ? m_length : util::bextract(m_token, 24u, 7u);
  }

  OpToken& setLength(uint32_t n) {
    if (isCustomData())
      m_length = n;
    else
      m_token = util::binsert(m_token, n, 24u, 7u);

    return *this;
  }


  /** Saturation flag for floating point instructions. */
  bool isSaturated() const {
    return util::bextract(m_token, 13u, 1u) != 0u;
  }

  OpToken& setSaturated(bool enable) {
    m_token = util::binsert(m_token, uint32_t(enable), 13u, 1u);
    return *this;
  }


  /** Retrieves zero test for boolean operations. Only used in
   *  certain conditional instructions. */
  TestBoolean getZeroTest() const {
    return TestBoolean(util::bextract(m_token, 18u, 1u));
  }

  OpToken& setZeroTest(TestBoolean test) {
    m_token = util::binsert(m_token, uint32_t(test), 18u, 1u);
    return *this;
  }


  /** Retrieves mask of components for which the precise flag is
   *  set. Only valid for various floating point instructions. */
  WriteMask getPreciseMask() const {
    return WriteMask(util::bextract(m_token, 19u, 4u));
  }

  OpToken& setPreciseMask(WriteMask mask) {
    m_token = util::binsert(m_token, uint32_t(uint8_t(mask)), 19u, 4u);
    return *this;
  }


  /** Return type for the instruction. Appears to
   *  only be used for the sampleinfo instruction. */
  ReturnType getReturnType() const {
    return ReturnType(util::bextract(m_token, 11u, 1u));
  }

  OpToken& setReturnType(ReturnType type) {
    m_token = util::binsert(m_token, uint32_t(type), 11u, 1u);
    return *this;
  }


  /** Return type for the resinfo instruction. */
  ResInfoType getResInfoType() const {
    return ResInfoType(util::bextract(m_token, 11u, 2u));
  }

  OpToken& setResInfoType(ResInfoType type) {
    m_token = util::binsert(m_token, uint32_t(type), 11u, 2u);
    return *this;
  }


  /** Queries barrier flags for the sync instruction. */
  SyncFlags getSyncFlags() const {
    return SyncFlags(util::bextract(m_token, 11u, 4u));
  }

  OpToken& setSyncFlags(SyncFlags flags) {
    m_token = util::binsert(m_token, uint32_t(flags), 11u, 4u);
    return *this;
  }


  /** Queries flags for the dcl_global_flags instruction. */
  GlobalFlags getGlobalFlags() const {
    return GlobalFlags(util::bextract(m_token, 11u, 13u));
  }

  OpToken& setGlobalFlags(GlobalFlags flags) {
    m_token = util::binsert(m_token, uint32_t(flags), 11u, 13u);
    return *this;
  }


  /** Interpolation mode for pixel shader input declarations. */
  InterpolationMode getInterpolationMode() const {
    return InterpolationMode(util::bextract(m_token, 11u, 4u));
  }

  OpToken& setInterpolationMode(InterpolationMode mode) {
    m_token = util::binsert(m_token, uint32_t(mode), 11u, 4u);
    return *this;
  }


  /** Primitive topology used in the dcl_gs_output_primitive_topology
   *  declarations only. GS input uses the regular primitive enum. */
  PrimitiveTopology getPrimitiveTopology() const {
    return PrimitiveTopology(util::bextract(m_token, 11u, 7u));
  }

  OpToken& setPrimitiveTopology(PrimitiveTopology topology) {
    m_token = util::binsert(m_token, uint32_t(topology), 11u, 7u);
    return *this;
  }


  /** Primitive type used in the dcl_gs_input_primitive instruction
   *  only. GS output uses the primitive topology enum instead. */
  PrimitiveType getPrimitiveType() const {
    return PrimitiveType(util::bextract(m_token, 11u, 6u));
  }

  OpToken& setPrimitiveType(PrimitiveType type) {
    m_token = util::binsert(m_token, uint32_t(type), 11u, 6u);
    return *this;
  }


  /** Tessellator domain used in the dcl_tess_domain instruction. */
  TessDomain getTessellatorDomain() const {
    return TessDomain(util::bextract(m_token, 11u, 2u));
  }

  OpToken& setTessellatorDomain(TessDomain domain) {
    m_token = util::binsert(m_token, uint32_t(domain), 11u, 2u);
    return *this;
  }


  /** Tessellator partitioning used in the dcl_tess_partitioning instruction. */
  TessPartitioning getTessellatorPartitioning() const {
    return TessPartitioning(util::bextract(m_token, 11u, 3u));
  }

  OpToken& setTessellatorPartitioning(TessPartitioning partitioning) {
    m_token = util::binsert(m_token, uint32_t(partitioning), 11u, 3u);
    return *this;
  }


  /** Queries tessellator output primitive type. Only used
   *  in the dcl_tess_poutput_primitive instruction. */
  TessOutput getTessellatorOutput() const {
    return TessOutput(util::bextract(m_token, 11u, 3u));
  }

  OpToken& setTessellatorOutput(TessOutput output) {
    m_token = util::binsert(m_token, uint32_t(output), 11u, 3u);
    return *this;
  }


  /** Queries tessellation control point count. */
  uint32_t getControlPointCount() const {
    return util::bextract(m_token, 11u, 6u);
  }

  OpToken& getControlPointCount(uint32_t count) {
    m_token = util::binsert(m_token, count, 11u, 6u);
    return *this;
  }


  /** Queries resource dimension. Valid for resource and UAV
   *  declarations only. */
  ResourceDim getResourceDim() const {
    return ResourceDim(util::bextract(m_token, 11u, 5u));
  }

  OpToken& setResourceDim(ResourceDim dim) {
    m_token = util::binsert(m_token, uint32_t(dim), 11u, 5u);
    return *this;
  }


  /** Queries CBV access pattern. */
  bool getCbvDynamicIndexingFlag() const {
    return util::bextract(m_token, 11u, 1u) != 0u;
  }

  OpToken& setCbvDynamicIndexingFlag(bool dynamicIndexing) {
    m_token = util::binsert(m_token, uint32_t(dynamicIndexing), 11u, 1u);
    return *this;
  }


  /** Queries UAV flags. Only valid for actual UAV declarations. */
  UavFlags getUavFlags() const {
    return UavFlags(util::bextract(m_token, 16u, 2u));
  }

  OpToken& setUavFlags(UavFlags flags) {
    m_token = util::binsert(m_token, uint32_t(flags), 16u, 2u);
    return *this;
  }


  /** Queries sampler mode. Used only in sampler declarations. */
  SamplerMode getSamplerMode() const {
    return SamplerMode(util::bextract(m_token, 11u, 4u));
  }

  OpToken& setSamplerMode(SamplerMode mode) {
    m_token = util::binsert(m_token, uint32_t(mode), 11u, 4u);
    return *this;
  }


  /** Retrieves extended sample control token. Used in various
   *  image load and sample instructions to provide an offset. */
  SampleControlToken getSampleControlToken() const {
    return m_sampleControls;
  }

  OpToken& setSampleControlToken(SampleControlToken controls) {
    m_sampleControls = controls;
    return *this;
  }


  /** Retrieves extended resource dimension token. Used in
   *  instructions that access resources of any kind. */
  ResourceDimToken getResourceDimToken() const {
    return m_resourceDim;
  }

  OpToken& setResourceDimToken(ResourceDimToken token) {
    m_resourceDim = token;
    return *this;
  }


  /** Extended resource return type token. Used in instructions
   *  that access resource data of any kind, and will usually
   *  not contain useful info that the resource delaration does
   *  not already provide. */
  ResourceTypeToken getResourceTypeToken() const {
    return m_resourceType;
  }

  OpToken& setResourceTypeToken(ResourceTypeToken token) {
    m_resourceType = token;
    return *this;
  }


  /** Writes opcode token, including all useful extended
   *  tokens, to a binary blob. */
  bool write(util::ByteWriter& writer) const;


  /** Checks whether opcode token is valid */
  explicit operator bool () const {
    return m_token != 0u;
  }

private:

  uint32_t m_token  = 0u;
  uint32_t m_length = 0u;

  SampleControlToken    m_sampleControls = { };
  ResourceDimToken      m_resourceDim = { };
  ResourceTypeToken     m_resourceType = { };

  void resetOnError();

};

/** Operand index type */
enum class IndexType : uint32_t {
  eImm32              = 0u,
  eImm64              = 1u,
  eRelative           = 2u,
  eImm32PlusRelative  = 3u,
  eImm64PlusRelative  = 4u,
};

inline bool hasAbsoluteIndexing(IndexType type) {
  return type != IndexType::eRelative;
}

inline bool hasRelativeIndexing(IndexType type) {
  return type >= IndexType::eRelative;
}

/** Component selection */
enum class ComponentCount : uint32_t {
  e0Component = 0u,
  e1Component = 1u,
  e4Component = 2u,
  eNComponent = 3u,
};


/** Component selection */
enum class SelectionMode : uint32_t {
  eMask       = 0u,
  eSwizzle    = 1u,
  eSelect1    = 2u,
};


/** Extended operand type */
enum class ExtendedOperandType : uint32_t {
  eNone       = 0u,
  eModifiers  = 1u,
};


/** Extended operand token */
class OperandModifiers {

public:

  OperandModifiers() = default;

  /** Creates modifier token from raw dword */
  explicit OperandModifiers(uint32_t token)
  : m_token(token) { }

  /** Creates extended operand modifier token */
  OperandModifiers(bool neg, bool abs, MinPrecision precision, bool nonuniform)
  : m_token(uint32_t(ExtendedOperandType::eModifiers) |
            uint32_t(neg ? (1u << 6u) : 0u) |
            uint32_t(abs ? (1u << 7u) : 0u) |
            (uint32_t(precision) << 14u) |
            uint32_t(nonuniform ? (1u << 17u) : 0u)) { }

  /** Queries negation flag of operand */
  bool isNegated() const {
    return util::bextract(m_token, 6u, 1u) != 0u;
  }

  /** Queries absolute flag of operand */
  bool isAbsolute() const {
    return util::bextract(m_token, 7u, 1u) != 0u;
  }

  /** Queries minimum precision status for the operand. */
  MinPrecision getPrecision() const {
    return MinPrecision(util::bextract(m_token, 14u, 3u));
  }

  /** Queries non-uniform status of the operand */
  bool isNonUniform() const {
    return util::bextract(m_token, 17u, 1u) != 0u;
  }

  /** Compares two operand modifier tokens */
  bool operator == (const OperandModifiers& other) const { return m_token == other.m_token; }
  bool operator != (const OperandModifiers& other) const { return m_token != other.m_token; }

  /** Retrieves raw token value */
  explicit operator uint32_t () const {
    return m_token;
  }

  /** Checks whether extended token defines any semantics.
   *  If false, the token can be omitted in encoding. */
  explicit operator bool () const {
    return m_token != 0u;
  }

private:

  uint32_t m_token = 0u;

};


/** Operand type. Used internally in order to set up the instruction
 *  layout and to distinguish operand tokens from immediate values
 *  when parsing instructions. */
enum class OperandKind : uint8_t {
  eNone   = 0u,
  eDstReg = 1u,
  eSrcReg = 2u,
  eImm32  = 3u,
  eIndex  = 4u,
  eExtra  = 5u,
};


/** Operand info */
struct OperandInfo {
  OperandKind     kind = { };
  ir::ScalarType  type = ir::ScalarType::eUnknown;
};


/** Operand info. Stores the base and extended operand tokens if
 *  applicable, as well as immediates or index values, depending
 *  on the operand type. */
class Operand {
  constexpr static uint32_t MaxIndexDim = 3u;
public:

  Operand() = default;

  /** Creates operand and sets up the component count and operand
   *  type. Other properties need to be set manually. */
  Operand(OperandInfo info, RegisterType type, ComponentCount componentCount)
  : m_token(uint32_t(componentCount) | (uint32_t(type) << 12u)), m_info(info) { }

  /** Recursively parses operand in byte stream, index operands. */
  Operand(util::ByteReader& reader, const OperandInfo& info, Instruction& op);


  /** Operand metadata. Not part of the operand tokens, but useful
   *  when processing operands depending on the instruction layout. */
  OperandInfo getInfo() const {
    return m_info;
  }


  /** Queries component count */
  ComponentCount getComponentCount() const {
    return ComponentCount(util::bextract(m_token, 0u, 2u));
  }

  /** Queries component selection mode. Determines the existence
   *  of a swizzle or write mask in the operand token. */
  SelectionMode getSelectionMode() const {
    return SelectionMode(util::bextract(m_token, 2u, 2u));
  }


  /** Queries component write mask. Only useful for destination
   *  operands. Handles different component selection types. */
  WriteMask getWriteMask() const;

  /** Queries component swizzle. Only useful for source operands.
   *  Handles different component selection types. */
  Swizzle getSwizzle() const;

  /** Sets write mask for a four-component operand. */
  Operand& setWriteMask(WriteMask mask);

  /** Sets swizzle for a four-component operand. */
  Operand& setSwizzle(Swizzle swizzle);

  /** Sets single component index for a four-component operand.
   *  Used for certain instructions that operate on scalars. */
  Operand& setComponent(Component component);

  /** Queries register type */
  RegisterType getRegisterType() const {
    return RegisterType(util::bextract(m_token, 12u, 8u));
  }

  /** Queries number of index dimensions */
  uint32_t getIndexDimensions() const {
    return util::bextract(m_token, 20u, 2u);
  }

  /** Sets number of index dimensions */
  Operand& setIndexDimensions(uint32_t n) {
    dxbc_spv_assert(n <= MaxIndexDim);

    m_token = util::binsert(m_token, n, 20u, 2u);
    return *this;
  }


  /** Queries index representation for a given index */
  IndexType getIndexType(uint32_t idx) const {
    return IndexType(util::bextract(m_token, 22u + 3u * idx, 3u));
  }

  /** Queries absolute index for a given index dimension. Convenience
   *  method that retrieves the corresponding immediate value. */
  uint32_t getIndex(uint32_t dim) const {
    return getImmediate<uint32_t>(dim);
  }

  /** Queries relative operand index for the given index dimension. Returns
   *  invalid index if the given dimension does not use relative indexing. */
  uint32_t getIndexOperand(uint32_t dim) const {
    return hasRelativeIndexing(getIndexType(dim)) ? m_idx[dim] : -1u;
  }

  /** Sets index and index type for a given dimension. The index type will
   *  be determined automatically depending on the operands passed in. */
  Operand& setIndex(uint32_t dim, uint32_t absolute, uint32_t relative);

  /** Adds index dimension with the given properties. */
  Operand& addIndex(uint32_t absolute, uint32_t relative);

  /** Adds immediate index. */
  Operand& addIndex(uint32_t absolute) {
    return addIndex(absolute, -1u);
  }


  /** Queries immediate value for a given component. If the operand
   *  is an indexed register, the component index corresponds to the
   *  index dimension. */
  template<typename T, std::enable_if_t<std::is_arithmetic_v<T>, bool> = true>
  T getImmediate(uint32_t idx) const {
    util::uint_type_t<T> data = { };

    if (getRegisterType() == RegisterType::eImm64) {
      dxbc_spv_assert(idx < 2u);
      data = util::uint_type_t<T>(m_imm[2u * idx + 0u]);

      if constexpr (sizeof(data) == 8u)
        data |= util::uint_type_t<T>(m_imm[2u * idx + 1u]) << 32u;
    } else {
      dxbc_spv_assert(idx < 4u);
      data = util::uint_type_t<T>(m_imm[idx]);
    }

    T result;
    std::memcpy(&result, &data, sizeof(result));
    return result;
  }

  template<typename T, std::enable_if_t<std::is_enum_v<T>, bool> = true>
  T getImmediate(uint32_t idx) const {
    return T(getImmediate<std::underlying_type_t<T>>(idx));
  }


  /** Sets immediate value for a given component. Automatically handles
   *  32-bit vs 64-bit types depending on the register type. */
  template<typename T, std::enable_if_t<(std::is_arithmetic_v<T>), bool> = true>
  Operand& setImmediate(uint32_t idx, T value) {
    auto type = getRegisterType();

    dxbc_spv_assert(type == RegisterType::eImm32 ||
                    type == RegisterType::eImm64);

    util::uint_type_t<T> data;
    std::memcpy(&data, &value, sizeof(data));

    if (type == RegisterType::eImm64) {
      dxbc_spv_assert(idx < 2u);
      m_imm[2u * idx + 0u] = uint32_t(data);

      if constexpr (sizeof(data) == 8u)
        m_imm[2u * idx + 1u] = uint32_t(data >> 32u);
      else
        m_imm[2u * idx + 1u] = 0u;
    } else {
      dxbc_spv_assert(idx < 4u);
      m_imm[idx] = uint32_t(data);
    }

    return *this;
  }

  template<typename T, std::enable_if_t<std::is_enum_v<T>, bool> = true>
  Operand& getImmediate(uint32_t idx, T value) const {
    return setImmediate(idx, std::underlying_type_t<T>(value));
  }



  /** Queries operand modifier token. */
  OperandModifiers getModifiers() const {
    return m_modifiers;
  }

  /** Sets modifier token. */
  Operand& setModifiers(OperandModifiers modifiers) {
    m_modifiers = modifiers;
    return *this;
  }


  /** Recursively writes operand as well as any index operands to a
   *  binary blob. Immediate values are handled accordingly. */
  bool write(util::ByteWriter& writer, const Instruction& op) const;


  /** Checks operand info is valid. A default token of 0 is
   *  nonsensical since it refers to a 0-component temp. */
  explicit operator bool () const {
    return m_token != 0u;
  }

private:

  uint32_t                  m_token = 0u;
  OperandModifiers          m_modifiers = { };

  std::array<uint32_t, 4u>  m_imm = { };
  std::array<uint8_t, 4u>   m_idx = { };

  OperandInfo               m_info = { };

  Operand& setSelectionMode(SelectionMode mode);

  void resetOnError();

  static ExtendedOperandType extractExtendedOperandType(uint32_t token);

};


/** Instruction layout. Stores the number of operands, and
 *  whether each operand is a source or destination register
 *  or an immediate. */
struct InstructionLayout {
  util::small_vector<OperandInfo, 8u> operands = { };
};


/** Instruction class. Stores opcode info and all operands,
 *  including nested operands used for relative indexing. */
class Instruction {

public:

  Instruction() = default;

  /** Initializes instruction with opcode and no operands. This will
   *  not interact with the instruction layout in any way. */
  explicit Instruction(OpCode opCode)
  : m_token(opCode) { }

  /** Initializes instruction with opcode token. */
  explicit Instruction(OpToken token)
  : m_token(token) { }

  /** Parses an instruction in a code chunk. */
  Instruction(util::ByteReader& reader, const ShaderInfo& info);


  /** Retrieves opcode info. */
  const OpToken& getOpToken() const {
    return m_token;
  }

  /** Retrieves number of destination operands */
  uint32_t getDstCount() const {
    return uint32_t(m_dstOperands.size());
  }

  /** Retrieves number of source operands */
  uint32_t getSrcCount() const {
    return uint32_t(m_srcOperands.size());
  }

  /** Retrieves number of immediate operands */
  uint32_t getImmCount() const {
    return uint32_t(m_immOperands.size());
  }

  /** Retrieves number of extra operands */
  uint32_t getExtraCount() const {
    return uint32_t(m_extraOperands.size());
  }

  /** Queries destination operand. */
  const Operand& getDst(uint32_t index) const {
    return getRawOperand(m_dstOperands.at(index));
  }

  /** Queries source operand. */
  const Operand& getSrc(uint32_t index) const {
    return getRawOperand(m_srcOperands.at(index));
  }

  /** Queries immediate operand. */
  const Operand& getImm(uint32_t index) const {
    return getRawOperand(m_immOperands.at(index));
  }

  /** Queries extra operand. */
  const Operand& getExtra(uint32_t index) const {
    return getRawOperand(m_extraOperands.at(index));
  }

  /** Queries raw operand index. Generally used
   *  when processing relative operand indices. */
  const Operand& getRawOperand(uint32_t index) const {
    dxbc_spv_assert(index < m_operands.size());
    return m_operands[index];
  }

  /** Adds a raw operand and returns the absolute operand index.
   *  This will implicitly update the source, dest or immediate
   *  operand look-up tables depending on the operand kind. */
  uint32_t addOperand(const Operand& operand);


  /** Queries instruction layout for the given instruction,
   *  with the given shader model in mind. The shader model
   *  changes the layout of resource declaration ops. */
  InstructionLayout getLayout(const ShaderInfo& info) const;


  /** Writes instruction and all its operands to a binary blob. */
  bool write(util::ByteWriter& writer, const ShaderInfo& info) const;


  /** Retrieves custom data range as a sequence of dwords. */
  std::pair<const uint32_t*, size_t> getCustomData() const {
    return std::make_pair(m_customData.data(), m_customData.size());
  }


  /** Checks whether instruction is valid */
  explicit operator bool () const {
    return bool(m_token);
  }

private:

  OpToken m_token = { };

  util::small_vector<uint8_t, 4u> m_dstOperands = { };
  util::small_vector<uint8_t, 8u> m_srcOperands = { };
  util::small_vector<uint8_t, 4u> m_immOperands = { };
  util::small_vector<uint8_t, 4u> m_extraOperands = { };

  util::small_vector<Operand, 16u> m_operands = { };

  std::vector<uint32_t> m_customData = { };

  void resetOnError();

};


/** Binary code parser. Essentially provides a stateful
 *  instruction iterator over the SHEX/SHDR chunk. */
class Parser {

public:

  Parser() = default;

  explicit Parser(util::ByteReader reader);

  /** Queries shader info, including the shader type and version. This
   *  is always available since it is stored at the start of the chunk. */
  ShaderInfo getShaderInfo() const {
    return m_info;
  }

  /** Parses the next instruction. Will return an invalid instruction
   *  if parsing has reached the end of the chunk, or if parsing fails. */
  Instruction parseInstruction();

  /** Checks whether any more instructions are
   *  available to be parsed. */
  explicit operator bool () const {
    return m_reader.getRemaining();
  }

private:

  util::ByteReader m_reader;

  ShaderInfo m_info = { };

};


/** Builder. Provides a simple way to create an instruction sequence
 *  and generate a valid SHEX or SHDR chunk out of it. */
class Builder {

public:

  /** Initializes builder with shader type info */
  Builder(ShaderType type, uint32_t major, uint32_t minor);

  ~Builder();

  /** Appends an instruction to the list */
  void add(Instruction ins);

  /** Generates the SHEX or SHDR chunk. */
  bool write(util::ByteWriter& writer) const;

private:

  ShaderInfo m_info;

  std::vector<Instruction> m_instructions;

};


std::ostream& operator << (std::ostream& os, ShaderType type);
std::ostream& operator << (std::ostream& os, const ShaderInfo& info);

}
