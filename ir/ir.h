#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

#include "../util/util_debug.h"
#include "../util/util_flags.h"
#include "../util/util_hash.h"
#include "../util/util_float16.h"
#include "../util/util_small_vector.h"

namespace dxbc_spv::ir {

using util::float16_t;

class Op;

/** Fundamental scalar data type */
enum class ScalarType : uint8_t {
  /** Void type, for when an instruction returns nothing. */
  eVoid         = 0,

  /** Unknown type that has not been resolved. If the proper
   *  type cannot be inferred, this will be lowered to U32. */
  eUnknown      = 1,

  /** Unsized boolean. Does not use D3D semantics. */
  eBool         = 2,

  /** Signed integer types. */
  eI8           = 3,
  eI16          = 4,
  eI32          = 5,
  eI64          = 6,

  /** Unsigned integer types. */
  eU8           = 7,
  eU16          = 8,
  eU32          = 9,
  eU64          = 10,

  /** Float types. */
  eF16          = 11,
  eF32          = 12,
  eF64          = 13,

  /** Min precision types. These must be lowered to the corresponding
   *  16-bit or 32-bit types depending on device support. */
  eMinI16       = 14,
  eMinU16       = 15,
  eMinF16       = 16,

  /** Opaque descriptor types. Used for resource access. */
  eSampler      = 17,
  eCbv          = 18,
  eSrv          = 19,
  eUav          = 20,
  eUavCounter   = 21,

  eCount
};

constexpr uint32_t ScalarTypeBits = 5u;
static_assert(uint8_t(ScalarType::eCount) <= (1u << ScalarTypeBits));


/** Utility function to query the byte size of a scalar data type.
 *  Types must be fully resolved for this to me meaningful. */
inline uint32_t byteSize(ScalarType type) {
  switch (type) {
    case ScalarType::eI8:
    case ScalarType::eU8:
      return 1u;

    case ScalarType::eI16:
    case ScalarType::eU16:
    case ScalarType::eF16:
      return 2u;

    case ScalarType::eI32:
    case ScalarType::eU32:
    case ScalarType::eF32:
      return 4u;

    case ScalarType::eI64:
    case ScalarType::eU64:
    case ScalarType::eF64:
      return 8u;

    default:
      return 0u;
  }
}


/** Basic vector type. Represents either a scalar
 *  or a vector with two to four components. */
class BasicType {

public:

  explicit BasicType(ScalarType base, uint32_t size)
  : m_baseType  (uint8_t(base)),
    m_components(uint8_t(size ? size - 1u : 0u)),
    m_reserved  (0u) { }

  BasicType(ScalarType base)
  : BasicType(base, 0) { }

  BasicType()
  : BasicType(ScalarType::eVoid, 0) { }

  /** Queries underlying scalar type. */
  ScalarType getBaseType() const {
    return ScalarType(m_baseType);
  }

  /** Queries component count. */
  uint32_t getVectorSize() const {
    return m_components + 1u;
  }

  /** Checks whether the type is scalar. */
  bool isScalar() const {
    return !m_components;
  }

  /** Checks whether the type is a vector with two or more components. */
  bool isVector() const {
    return m_components > 0u;
  }

  /** Checks whether base type is void. */
  bool isVoidType() const {
    auto type = getBaseType();
    return type == ScalarType::eVoid;
  }

  /** Checks whether base type is unknown. */
  bool isUnknownType() const {
    auto type = getBaseType();
    return type == ScalarType::eUnknown;
  }

  /** Checks whether base type is boolean. */
  bool isBoolType() const {
    auto type = getBaseType();
    return type == ScalarType::eBool;
  }

  /** Checks whether base type is a signed integer type */
  bool isSignedIntType() const {
    auto type = getBaseType();

    return type == ScalarType::eI8
        || type == ScalarType::eI16
        || type == ScalarType::eI32
        || type == ScalarType::eI64
        || type == ScalarType::eMinI16;
  }

  /** Checks whether base type is an unsigned integer type */
  bool isUnsignedIntType() const {
    auto type = getBaseType();

    return type == ScalarType::eU8
        || type == ScalarType::eU16
        || type == ScalarType::eU32
        || type == ScalarType::eU64
        || type == ScalarType::eMinU16;
  }

  /** Checks whether base type is an integer type */
  bool isIntType() const {
    return isSignedIntType() || isUnsignedIntType();
  }

  /** Checks whether base type is a float type */
  bool isFloatType() const {
    auto type = getBaseType();

    return type == ScalarType::eF16
        || type == ScalarType::eF32
        || type == ScalarType::eF64
        || type == ScalarType::eMinF16;
  }

  /** Checks whether base type is a numeric type */
  bool isNumericType() const {
    return isFloatType() || isIntType() || isUnknownType();
  }

  /** Checks whether base type is a minimum precision type. */
  bool isMinPrecisionType() const {
    auto type = getBaseType();

    return type == ScalarType::eMinI16
        || type == ScalarType::eMinU16
        || type == ScalarType::eMinF16;
  }

  /** Checks whether base type is a descriptor type */
  bool isDescriptorType() const {
    auto type = getBaseType();

    return type == ScalarType::eSampler
        || type == ScalarType::eCbv
        || type == ScalarType::eSrv
        || type == ScalarType::eUav
        || type == ScalarType::eUavCounter;
  }

  /** Computes byte size of vector type */
  uint32_t byteSize() const {
    return ir::byteSize(getBaseType()) * getVectorSize();
  }

  /** Computes required byte alignment of vector type */
  uint32_t byteAlignment() const {
    return ir::byteSize(getBaseType());
  }

  /** Checks types for equality */
  bool operator == (const BasicType& other) const {
    return m_baseType   == other.m_baseType
        && m_components == other.m_components;
  }

  bool operator != (const BasicType& other) const {
    return !(this->operator == (other));
  }

  /** Helpers to construct types from basic C++ types. */
  static BasicType from(bool, uint32_t n) { return BasicType(ScalarType::eBool, n); }

  static BasicType from(uint8_t,  uint32_t n) { return BasicType(ScalarType::eU8, n);  }
  static BasicType from(uint16_t, uint32_t n) { return BasicType(ScalarType::eU16, n); }
  static BasicType from(uint32_t, uint32_t n) { return BasicType(ScalarType::eU32, n); }
  static BasicType from(uint64_t, uint32_t n) { return BasicType(ScalarType::eU64, n); }

  static BasicType from(int8_t,  uint32_t n) { return BasicType(ScalarType::eI8, n);  }
  static BasicType from(int16_t, uint32_t n) { return BasicType(ScalarType::eI16, n); }
  static BasicType from(int32_t, uint32_t n) { return BasicType(ScalarType::eI32, n); }
  static BasicType from(int64_t, uint32_t n) { return BasicType(ScalarType::eI64, n); }

  static BasicType from(float16_t, uint32_t n) { return BasicType(ScalarType::eF16, n); }
  static BasicType from(float,     uint32_t n) { return BasicType(ScalarType::eF32, n); }
  static BasicType from(double,    uint32_t n) { return BasicType(ScalarType::eF64, n); }

private:

  uint8_t m_baseType   : 5;
  uint8_t m_components : 2;
  uint8_t m_reserved   : 1;

};


/** Complete type class.
 *
 * A complete type can be any of the following categories:
 * - A basic type, i.e. a plain scalar or vector, or void.
 * - A struct consisting of multiple basic types.
 * - An array of any of the above, with up to three dimensions.
 *
 * Arrays can be unbounded or runtime-sized, in which case the outer-most
 * dimension has a declared size of 0.
 *
 * As an example the type \c vec4<f32>[n][] is a two-dimensional array of float
 * vectors, where \c a is the size of the inner dimension (index 0), and the
 * outer dimension (index 1) has a size not known at runtime.
 */
class Type {

public:

  static constexpr uint32_t MaxArrayDimensions = 3;
  static constexpr uint32_t MaxStructMembers = 24;

  /** Initializes void type */
  Type() : Type(ScalarType::eVoid) { }

  /** Initializes type from scalar type */
  Type(ScalarType base)
  : Type(BasicType(base)) { }

  /** Initializes type from basic type */
  Type(BasicType base)
  : m_structSize(base.isVoidType() ? 0u : 1u) {
    m_members[0u] = base;
  }

  /** Initializes basic vector type */
  Type(ScalarType base, uint32_t vectorSize)
  : Type(BasicType(base, vectorSize)) { }

  /** Queries base member type. */
  BasicType getBaseType(uint32_t memberIdx) const {
    return memberIdx < m_structSize ? m_members[memberIdx] : BasicType();
  }

  /** Checks whether type is void. */
  bool isVoidType() const {
    return !m_structSize;
  }

  /** Checks whether type is a scalar or vector */
  bool isBasicType() const {
    return !isStructType() && !isArrayType();
  }

  /** Checks whether type is scalar */
  bool isScalarType() const {
    return isBasicType() && getBaseType(0u).isScalar();
  }

  /** Checks whether type is a vector */
  bool isVectorType() const {
    return isBasicType() && getBaseType(0u).isVector();
  }

  /** Checks whether type is a structure with multiple
   *  members, but is not an array of any kind. */
  bool isStructType() const {
    return !m_dimensions && m_structSize > 1u;
  }

  /** Queries struct member count */
  uint32_t getStructMemberCount() const {
    return m_structSize;
  }

  /** Checks whether type is an array. */
  bool isArrayType() const {
    return m_dimensions > 0u;
  }

  /** Checks whether type is an array where every dimension
   *  has a pre-determined size. */
  bool isSizedArray() const {
    return m_dimensions && m_sizes[m_dimensions - 1u] > 0u;
  }

  /** Checks whether type is an array where the size of the
   *  outer-most dimension is not known at compile time. */
  bool isUnboundedArray() const {
    return m_dimensions && m_sizes[m_dimensions - 1u] == 0u;
  }

  /** Queries the number of array dimensions. */
  uint32_t getArrayDimensions() const {
    return m_dimensions;
  }

  /** Queries the array size for a given dimension. If 0, and
   *  if the dimension is valid, this indicates that the size
   *  is not known at compile-time. */
  uint32_t getArraySize(uint32_t dimension) const {
    return dimension < m_dimensions ? m_sizes[dimension] : 0u;
  }

  /** Adds a struct member. */
  Type& addStructMember(BasicType type);

  /** Adds an array dimension. Pass a size of 0 for an unbounded array. */
  Type& addArrayDimension(uint32_t size);

  /** Adds multiple array dimensions */
  template<typename... T>
  Type& addArrayDimensions(T... args) {
    return (addArrayDimension(args), ...);
  }

  /** Constructs type from indexing into it once, with the given index.
   *  Note that the index value is only relevant for struct types. */
  Type getSubType(uint32_t index) const;

  /** Resolves flattened scalar type at a given index. Useful to
   *  determine the operand type of a constant definition. */
  ScalarType resolveFlattenedType(uint32_t index) const;

  /** Computes flattened scalar count for type. */
  uint32_t computeFlattenedScalarCount() const;

  /** Computes top-level aggregate size. For an array type, this
   *  will return the size of the outer-most array, for a struct,
   *  this returns the struct member count, and for a vector, the
   *  number of vector components. */
  uint32_t computeTopLevelMemberCount() const;

  /** Computes byte size of type. If the outermost array dimension
   *  is unsized, returns the size of the underlying type. */
  uint32_t byteSize() const;

  /** Computes required byte alignment of type. This assumes scalar
   *  alignment, i.e. vectors are not treated in any special way. */
  uint32_t byteAlignment() const;

  /** Computes byte offset of a given member */
  uint32_t byteOffset(uint32_t member) const;

  /** Checks types for equality */
  bool operator == (const Type& other) const;
  bool operator != (const Type& other) const;

private:

  uint8_t m_dimensions = 0;
  uint8_t m_structSize = 0;

  std::array<uint16_t, MaxArrayDimensions> m_sizes = { };
  std::array<BasicType, MaxStructMembers> m_members = { };

};

static_assert(sizeof(Type) == 32);


/** Structured construct declaration for a block.
 *
 * Set on a label to declare that the block-ending
 * branch is part of an if, switch or loop construct.
 */
enum class Construct : uint32_t {
  eNone                 = 0,
  eStructuredSelection  = 1,
  eStructuredLoop       = 2,
};


/** Resource kind. */
enum class ResourceKind : uint32_t {
  /** Typed buffer, must be declared with a scalar or vector type. */
  eBufferTyped        = 0,
  /** Structured buffer, must be declared with a struct or sized array type.
   *  The byte size of the type directly maps to the structure stride. */
  eBufferStructured   = 1,
  /** Raw buffer, must be declared with a scalar type. */
  eBufferRaw          = 2,
  /** Image types. Like typed buffers, these must be declared
   *  with a scalar or vector type. */
  eImage1D            = 3,
  eImage1DArray       = 4,
  eImage2D            = 5,
  eImage2DArray       = 6,
  eImage2DMS          = 7,
  eImage2DMSArray     = 8,
  eImageCube          = 9,
  eImageCubeArray     = 10,
  eImage3D            = 11,
};


/* Resource image dimensions */
inline uint32_t resourceDimensions(ResourceKind kind) {
  switch (kind) {
    case ResourceKind::eBufferTyped:
    case ResourceKind::eBufferRaw:
    case ResourceKind::eBufferStructured:
    case ResourceKind::eImage1D:
    case ResourceKind::eImage1DArray:
      return 1u;

    case ResourceKind::eImage2D:
    case ResourceKind::eImage2DArray:
    case ResourceKind::eImageCube:
    case ResourceKind::eImageCubeArray:
    case ResourceKind::eImage2DMS:
    case ResourceKind::eImage2DMSArray:
      return 2u;

    case ResourceKind::eImage3D:
      return 3u;
  }

  dxbc_spv_unreachable();
  return 0u;
}


/* Computes required component count of the address vector for any
 * given resource kind. Does not include the array layer index. */
inline uint32_t resourceCoordComponentCount(ResourceKind kind) {
  switch (kind) {
    case ResourceKind::eBufferTyped:
    case ResourceKind::eBufferRaw:
    case ResourceKind::eImage1D:
    case ResourceKind::eImage1DArray:
      return 1u;

    case ResourceKind::eBufferStructured:
    case ResourceKind::eImage2D:
    case ResourceKind::eImage2DArray:
    case ResourceKind::eImage2DMS:
    case ResourceKind::eImage2DMSArray:
      return 2u;

    case ResourceKind::eImageCube:
    case ResourceKind::eImageCubeArray:
    case ResourceKind::eImage3D:
      return 3u;
  }

  dxbc_spv_unreachable();
  return 0u;
}


/** Checks whether resource is layered. */
inline bool resourceIsLayered(ResourceKind kind) {
  return kind == ResourceKind::eImage1DArray
      || kind == ResourceKind::eImage2DArray
      || kind == ResourceKind::eImage2DMSArray
      || kind == ResourceKind::eImageCubeArray;
}


/** Checks whether resource is multisampled. */
inline bool resourceIsMultisampled(ResourceKind kind) {
  return kind == ResourceKind::eImage2DMS
      || kind == ResourceKind::eImage2DMSArray;
}


/** Checks whether resource is a buffer. */
inline bool resourceIsBuffer(ResourceKind kind) {
  return kind == ResourceKind::eBufferTyped
      || kind == ResourceKind::eBufferStructured
      || kind == ResourceKind::eBufferRaw;
}


/** Checks whether resource is types. */
inline bool resourceIsTyped(ResourceKind kind) {
  return kind != ResourceKind::eBufferStructured
      && kind != ResourceKind::eBufferRaw;
}


/** Primitive type declaration for tessellation and geometry. */
enum class PrimitiveType : uint32_t {
  /** Points. Legal as GS input, GS output, and tessellation primitives. */
  ePoints             = 0,
  /** Lines. Legal as GS input, GS output, and tessellation
   *  primitives. Also legal as a tessellation domain. */
  eLines              = 1,
  /** Lines with adjacency. Legal as GS input. */
  eLinesAdj           = 2,
  /** Triangles. Legal as GS input, GS output, tessellation
   *  primitives, and tessellation domain. */
  eTriangles          = 3,
  /** Triangles with adjacency. Legal as GS input. */
  eTrianglesAdj       = 4,
  /** Quads. Only legal as a tessellation domain. */
  eQuads              = 5,

  /** First patch entry. Individual patch sizes have no declared
   *  enum, instead the integer enum value is used to calculate
   *  it. A patch with one vertex would have the value 7.
   *  Only valid for geometry shader input topologies. */
  ePatch              = 6,
};


/** Computes vertex count for primitive type. */
inline uint32_t primitiveVertexCount(PrimitiveType type) {
  switch (type) {
    case PrimitiveType::ePoints: return 1u;
    case PrimitiveType::eLines: return 2u;
    case PrimitiveType::eLinesAdj: return 4u;
    case PrimitiveType::eTriangles: return 3u;
    case PrimitiveType::eTrianglesAdj: return 6u;

    default: {
      uint32_t patchSize = uint32_t(type) - uint32_t(PrimitiveType::ePatch);

      dxbc_spv_assert(patchSize >= 1u && patchSize <= 32u);
      return patchSize;
    }
  }
}


/** Triangle winding order for tessellation */
enum class TessWindingOrder : uint32_t {
  eCcw = 0u,
  eCw  = 1u,
};


/** Tessellation partitioning */
enum class TessPartitioning : uint32_t {
  eInteger    = 0u,
  eFractOdd   = 1u,
  eFractEven  = 2u,
  ePow2       = 3u,
};


/** Built-in input or output declaration */
enum class BuiltIn : uint32_t {
  /** Vertex position in any geometry stage, or fragment location in
   *  pixel shaders. Uses SPIR-V semantics for the .w coordinate.
   *  Must be declared as a four-component float vector. */
  ePosition           = 0u,
  /** Clip distances. Must be declared as a sized float array. */
  eClipDistance       = 1u,
  /** Cull distances. Must be declared as a sized float array. */
  eCullDistance       = 2u,
  /** Vertex ID, starting at 0. Uses D3D semantics. Must be declared
   *  as a scalar unsigned integer. */
  eVertexId           = 3u,
  /** Instance ID, starting at 0. Uses D3D semantics. Must be
   *  declared as a scalar unsigned integer. */
  eInstanceId         = 4u,
  /** Primitive ID, starting at 0. Uses D3D semantics. Must be
   *  declared as a scalar unsigned integer. Can be written by
   *  certain shader stages. */
  ePrimitiveId        = 5u,
  /** Render target layer. Must be declared as an unsigned integer. */
  eLayerIndex         = 6u,
  /** Viewport index. Must be declared as an unsigned integer. */
  eViewportIndex      = 7u,
  /** Geometry shader instance ID. Must be declared as an unsigned integer. */
  eGsInstanceId       = 8u,
  /** Tessellation control point ID. Must be declared as an unsigned integer. */
  eTessControlPointId = 9u,
  /** Tessellation coordinates in domain shaders. Must be declared
   *  as a two-component float vector. */
  eTessCoord          = 10u,
  /** Inner tesellation factors. Must be declared as a sized float array. */
  eTessFactorInner    = 11u,
  /** Outer tesellation factors. Must be declared as a sized float array. */
  eTessFactorOuter    = 12u,
  /** Rasterizer sample count. Must be declared as an unsigned integer. */
  eSampleCount        = 13u,
  /** Sample ID in fragment shader. Must be declared as a scalar
   *  unsigned integer. Its use will trigger sample rate shading. */
  eSampleId           = 14u,
  /** Sample position in fragment shader. Must be declared as a scalar
   *  unsigned integer. Its use will trigger sample rate shading. */
  eSamplePosition     = 15u,
  /** Sample mask in fragment shader. Must be declared as a scalar unsigned
   *  integer to follow D3D semantics. Can be used as input and output. */
  eSampleMask         = 16u,
  /** Front-face flag in fragment shader. Must be declared as a boolean. */
  eIsFrontFace        = 17u,
  /** Fragment depth in fragment shader. Output only. Must be declared as
   *  a scalar float. */
  eDepth              = 18u,
  /** Stencil reference in fragment shader. Output only. Must be declared
   *  as a scalar unsigned integer. */
  eStencilRef         = 19u,
  /** Workgroup ID in compute shader. Must be declared as a three-component
   *  unsigned integer vector. */
  eWorkgroupId        = 20u,
  /** Global thread ID in compute shader. Must be declared as a
   *  three-component unsigned integer vector. */
  eGlobalThreadId     = 21u,
  /** Local thread ID in compute shader. Must be declared as a
   *  three-component unsigned integer vector. */
  eLocalThreadId      = 22u,
  /** Flattened local thread ID in compute shader. Must be declared
   *  as a scalar unsigned integer. */
  eLocalThreadIndex   = 23u,
};


/** Atomic operation type */
enum class AtomicOp : uint32_t {
  eLoad             = 0u,
  eStore            = 1u,
  eExchange         = 2u,
  eCompareExchange  = 3u,
  eAdd              = 4u,
  eSub              = 5u,
  eSMin             = 6u,
  eSMax             = 7u,
  eUMin             = 8u,
  eUMax             = 9u,
  eAnd              = 10u,
  eOr               = 11u,
  eXor              = 12u,
  eInc              = 13u,
  eDec              = 14u,
};


/** UAV flags */
enum class UavFlag : uint32_t {
  eCoherent           = (1u << 0),
  eReadOnly           = (1u << 1),
  eWriteOnly          = (1u << 2),
  eRasterizerOrdered  = (1u << 3),
  eFixedFormat        = (1u << 4),

  eFlagEnum           = 0u
};

using UavFlags = util::Flags<UavFlag>;


/** Interpolation mode flags */
enum class InterpolationMode : uint32_t {
  eFlat             = (1u << 0),
  eCentroid         = (1u << 1),
  eSample           = (1u << 2),
  eNoPerspective    = (1u << 3),

  eFlagEnum         = 0u
};

using InterpolationModes = util::Flags<InterpolationMode>;


/** Shader stage flags */
enum class ShaderStage : uint32_t {
  ePixel            = (1u << 0),
  eVertex           = (1u << 1),
  eGeometry         = (1u << 2),
  eHull             = (1u << 3),
  eDomain           = (1u << 4),
  eCompute          = (1u << 5),

  eFlagEnum         = 0u
};

using ShaderStageMask = util::Flags<ShaderStage>;


/** Scope */
enum class Scope : uint32_t {
  eThread           = 0u,
  eQuad             = 1u,
  eSubgroup         = 2u,
  eWorkgroup        = 3u,
  eGlobal           = 4u,
};


/** Memory type flags */
enum class MemoryType : uint32_t {
  eLds              = (1u << 0),
  eUavBuffer        = (1u << 1),
  eUavImage         = (1u << 2),

  eFlagEnum         = 0
};

using MemoryTypeFlags = util::Flags<MemoryType>;


/** Floating point round mode */
enum class RoundMode : uint32_t {
  eZero             = (1u << 0),
  eNearestEven      = (1u << 1),
  eNegativeInf      = (1u << 2),
  ePositiveInf      = (1u << 3),
};


/** Derivative mode */
enum class DerivativeMode : uint32_t {
  eDefault          = 0u,
  eCoarse           = 1u,
  eFine             = 2u,
};


/** SSA definition. Stores a unique ID that refers to an operation. */
class SsaDef {

public:

  SsaDef() = default;

  /** Creates SSA def from raw ID */
  explicit SsaDef(uint32_t id)
  : m_id(id) { }

  /** Queries raw ID. Used primarily for serialization and look-up purposes.
   *  An ID of 0 is a null definition and does not refer to any operation. */
  uint32_t getId() const {
    return m_id;
  }

  /** Checks whether definiton is non-null. */
  explicit operator bool () const {
    return m_id > 0u;
  }

  bool operator == (const SsaDef& other) const { return m_id == other.m_id; }
  bool operator != (const SsaDef& other) const { return m_id != other.m_id; }

private:

  uint32_t m_id = 0u;

};


/** Opcodes */
enum class OpCode : uint16_t {
  eUnknown                      = 0u,

  eEntryPoint                   = 1u,
  eDebugName                    = 2u,
  eConstant                     = 3u,
  eUndef                        = 4u,
  eSemantic                     = 5u,

  eSetCsWorkgroupSize           = 16u,
  eSetGsInstances               = 17u,
  eSetGsInputPrimitive          = 18u,
  eSetGsOutputVertices          = 19u,
  eSetGsOutputPrimitive         = 20u,
  eSetPsEarlyFragmentTest       = 21u,
  eSetPsDepthGreaterEqual       = 22u,
  eSetPsDepthLessEqual          = 23u,
  eSetTessPrimitive             = 24u,
  eSetTessDomain                = 25u,
  eSetTessControlPoints         = 26u,

  eDclInput                     = 32u,
  eDclInputBuiltIn              = 33u,
  eDclOutput                    = 34u,
  eDclOutputBuiltIn             = 35u,
  eDclSpecConstant              = 36u,
  eDclPushData                  = 37u,
  eDclSampler                   = 38u,
  eDclCbv                       = 39u,
  eDclSrv                       = 40u,
  eDclUav                       = 41u,
  eDclUavCounter                = 42u,
  eDclLds                       = 43u,
  eDclScratch                   = 44u,
  eDclTmp                       = 45u,
  eDclParam                     = 46u,
  eDclXfb                       = 47u,

  /** Last valid opcode for declarative instructions */
  eLastDeclarative              = 63u,

  eFunction                     = 64u,
  eFunctionEnd                  = 65u,
  eFunctionCall                 = 66u,

  eLabel                        = 96u,
  eBranch                       = 97u,
  eBranchConditional            = 98u,
  eSwitch                       = 99u,
  eUnreachable                  = 100u,
  eReturn                       = 101u,
  ePhi                          = 102u,

  eScopedIf                     = 128u,
  eScopedElse                   = 129u,
  eScopedEndIf                  = 130u,
  eScopedLoop                   = 131u,
  eScopedLoopBreak              = 132u,
  eScopedLoopContinue           = 133u,
  eScopedEndLoop                = 134u,
  eScopedSwitch                 = 135u,
  eScopedSwitchCase             = 136u,
  eScopedSwitchDefault          = 137u,
  eScopedSwitchBreak            = 138u,
  eScopedEndSwitch              = 139u,

  eBarrier                      = 160u,

  eConvertFtoF                  = 192u,
  eConvertFtoI                  = 193u,
  eConvertItoF                  = 194u,
  eConvertItoI                  = 195u,
  eConvertF32toPackedF16        = 196u,
  eConvertPackedF16toF32        = 197u,
  eCast                         = 198u,
  eConsumeAs                    = 199u,

  eCompositeInsert              = 224u,
  eCompositeExtract             = 225u,
  eCompositeConstruct           = 226u,

  eCheckSparseAccess            = 256u,

  eParamLoad                    = 288u,
  eTmpLoad                      = 289u,
  eTmpStore                     = 290u,
  eScratchLoad                  = 291u,
  eScratchStore                 = 292u,
  eLdsLoad                      = 293u,
  eLdsStore                     = 294u,
  ePushDataLoad                 = 295u,
  eSpecConstantLoad             = 296u,
  eInputLoad                    = 297u,
  eOutputLoad                   = 298u,
  eOutputStore                  = 299u,
  eDescriptorLoad               = 300u,
  eBufferLoad                   = 301u,
  eBufferStore                  = 302u,
  eBufferQuerySize              = 303u,
  eMemoryLoad                   = 304u,
  eMemoryStore                  = 305u,
  eConstantLoad                 = 306u,

  eLdsAtomic                    = 320u,
  eBufferAtomic                 = 321u,
  eImageAtomic                  = 322u,
  eCounterAtomic                = 323u,
  eMemoryAtomic                 = 324u,

  eImageLoad                    = 352u,
  eImageStore                   = 353u,
  eImageQuerySize               = 354u,
  eImageQueryMips               = 355u,
  eImageQuerySamples            = 356u,
  eImageSample                  = 357u,
  eImageGather                  = 358u,
  eImageComputeLod              = 359u,

  ePointer                      = 384u,
  ePointerAddress               = 385u,

  eEmitVertex                   = 416u,
  eEmitPrimitive                = 417u,

  eDemote                       = 448u,

  eInterpolateAtCentroid        = 480u,
  eInterpolateAtSample          = 481u,
  eInterpolateAtOffset          = 482u,

  eDerivX                       = 512u,
  eDerivY                       = 513u,

  eRovScopedLockBegin           = 544u,
  eRovScopedLockEnd             = 545u,

  eFEq                          = 576u,
  eFNe                          = 577u,
  eFLt                          = 578u,
  eFLe                          = 579u,
  eFGt                          = 580u,
  eFGe                          = 581u,
  eFIsNan                       = 582u,
  eIEq                          = 583u,
  eINe                          = 584u,
  eSLt                          = 585u,
  eSLe                          = 586u,
  eSGt                          = 587u,
  eSGe                          = 588u,
  eULt                          = 589u,
  eULe                          = 590u,
  eUGt                          = 591u,
  eUGe                          = 592u,

  eBAnd                         = 608u,
  eBOr                          = 609u,
  eBEq                          = 610u,
  eBNe                          = 611u,
  eBNot                         = 612u,

  eSelect                       = 640u,

  eFAbs                         = 672u,
  eFNeg                         = 673u,
  eFAdd                         = 674u,
  eFSub                         = 675u,
  eFMul                         = 676u,
  eFMulLegacy                   = 677u,
  eFMad                         = 678u,
  eFMadLegacy                   = 679u,
  eFDiv                         = 680u,
  eFRcp                         = 681u,
  eFSqrt                        = 682u,
  eFRsq                         = 683u,
  eFExp2                        = 684u,
  eFLog2                        = 685u,
  eFFract                       = 686u,
  eFRound                       = 687u,
  eFMin                         = 688u,
  eFMax                         = 689u,
  eFDot                         = 690u,
  eFDotLegacy                   = 691u,
  eFClamp                       = 692u,
  eFSin                         = 693u,
  eFCos                         = 694u,

  eIAnd                         = 704u,
  eIOr                          = 705u,
  eIXor                         = 706u,
  eINot                         = 707u,
  eIBitInsert                   = 708u,
  eUBitExtract                  = 709u,
  eSBitExtract                  = 710u,
  eIShl                         = 711u,
  eSShr                         = 712u,
  eUShr                         = 713u,
  eIBitCount                    = 714u,
  eIBitReverse                  = 715u,
  eIFindLsb                     = 716u,
  eSFindMsb                     = 717u,
  eUFindMsb                     = 718u,

  eIAdd                         = 736u,
  eIAddCarry                    = 737u,
  eISub                         = 738u,
  eISubBorrow                   = 739u,
  eIAbs                         = 740u,
  eINeg                         = 741u,
  eIMul                         = 742u,
  eSMulExtended                 = 743u,
  eUMulExtended                 = 744u,
  eUDiv                         = 745u,
  eUMod                         = 746u,
  eSMin                         = 747u,
  eSMax                         = 748u,
  eSClamp                       = 749u,
  eUMin                         = 750u,
  eUMax                         = 751u,
  eUClamp                       = 752u,
  eUMSad                        = 753u,

  Count
};

constexpr uint32_t OpCodeBits = 10;
static_assert(uint16_t(OpCode::Count) <= (1u << OpCodeBits));


/** Operation flags */
enum class OpFlag : uint8_t {
  /** Flag to indicate that the instruction cannot be used
   *  in transforms that would affect the result. */
  ePrecise = (1u << 0),
  /** Instruction is explicitly marked as non-uniform.
   *  May be used for descriptor access. */
  eNonUniform = (1u << 1),
  /** Flag to indicate that the operation returns
   *  sparse feedback rather than raw data. */
  eSparseFeedback = (1u << 2),

  eFlagEnum = 0
};

constexpr uint32_t OpFlagBits = 3;
using OpFlags = util::Flags<OpFlag>;


/** Operand data. Stores either a reference to another instruction
 *  as an SSA definition, or a literal constant. */
class Operand {

public:

  Operand() = default;

  /** Creates operand from SSA value */
  explicit Operand(SsaDef def) : m_data(def.getId()) { }
  explicit Operand(const Op* op);

  /** Creates operand from enum value */
  template<typename T, std::enable_if_t<std::is_enum_v<T>, bool> = true>
  explicit Operand(T v) : Operand(std::underlying_type_t<T>(v)) { }

  /** Creates operand from enum value */
  template<typename T, T V = T::eFlagEnum>
  explicit Operand(util::Flags<T> v) : Operand(typename util::Flags<T>::IntType(v)) { }

  /** Creates operand from boolean */
  explicit Operand(bool v) : Operand(v ? 1u : 0u) { }

  /** Creates operand from signed integer */
  explicit Operand(int8_t v) : Operand(uint64_t(v)) { }
  explicit Operand(int16_t v) : Operand(uint64_t(v)) { }
  explicit Operand(int32_t v) : Operand(uint64_t(v)) { }
  explicit Operand(int64_t v) : Operand(uint64_t(v)) { }

  /** Creates operand from unsigned integer */
  explicit Operand(uint8_t v) : m_data(v) { }
  explicit Operand(uint16_t v) : m_data(v) { }
  explicit Operand(uint32_t v) : m_data(v) { }
  explicit Operand(uint64_t v) : m_data(v) { }

  /** Creates operand from 16-bit float */
  explicit Operand(float16_t v) : Operand(v.data) { }

  /** Creates operand from 32-bit float */
  explicit Operand(float v) {
    uint32_t dw = 0u;
    std::memcpy(&dw, &v, sizeof(dw));

    m_data = dw;
  }

  /** Creates operand from 64-bit float */
  explicit Operand(double v) {
    std::memcpy(&m_data, &v, sizeof(m_data));
  }

  /** Extracts referenced SSA definiton, if any. */
  explicit operator SsaDef() const {
    return SsaDef(m_data);
  }

  /** Extracts boolean value. */
  explicit operator bool() const {
    return bool(m_data);
  }

  /** Reads literal as 16-bit float */
  explicit operator float16_t() const {
    return float16_t::fromRaw(m_data);
  }

  /** Reads literal as 32-bit float */
  explicit operator float() const {
    uint32_t dw = m_data;

    float f;
    std::memcpy(&f, &dw, sizeof(f));
    return f;
  }

  /** Reads literal as 64-bit float */
  explicit operator double() const {
    double d;
    std::memcpy(&d, &m_data, sizeof(d));
    return d;
  }

  /** Reads literal as signed integer */
  explicit operator int8_t() const { return int8_t(m_data); }
  explicit operator int16_t() const { return int16_t(m_data); }
  explicit operator int32_t() const { return int32_t(m_data); }
  explicit operator int64_t() const { return int64_t(m_data); }

  /** Reads literal as unsigned integer */
  explicit operator uint8_t() const { return uint8_t(m_data); }
  explicit operator uint16_t() const { return uint16_t(m_data); }
  explicit operator uint32_t() const { return uint32_t(m_data); }
  explicit operator uint64_t() const { return uint64_t(m_data); }

  /** Reads literal as enum */
  template<typename T, std::enable_if_t<std::is_enum_v<T>, bool> = true>
  explicit operator T() const {
    return T(std::underlying_type_t<T>(*this));
  }

  /** Reads literal as a set of flags */
  template<typename T, T V = T::eFlagEnum>
  explicit operator util::Flags<T> () const {
    return util::Flags<T>(typename util::Flags<T>::IntType(*this));
  }

  /** Reads data as a potentially null-terminated string. */
  bool getToString(std::string& str) const;

  bool operator == (const Operand& other) const { return m_data == other.m_data; }
  bool operator != (const Operand& other) const { return m_data != other.m_data; }

private:

  uint64_t m_data = 0u;

};

static_assert(sizeof(Operand) == sizeof(uint64_t));


/** Instruction class. */
class Op {
  constexpr static uint32_t MaxEmbeddedOperands = 11u;
public:

  Op() = default;

  Op(OpCode opCode, OpFlags flags, Type resultType, uint32_t operandCount, const Operand* operands)
  : m_opCode(opCode), m_flags(flags)
  , m_resultType(resultType) {
    if (operands) {
      for (uint32_t i = 0u; i < operandCount; i++)
        m_operands.push_back(operands[i]);
    } else {
      m_operands.resize(operandCount);
    }
  }

  Op(OpCode opCode, Type resultType)
  : Op(opCode, OpFlags(), resultType, 0u, nullptr) { }

  Op             (const Op&) = default;
  Op& operator = (const Op&) = default;

  Op(Op&& other)
  : m_def         (std::exchange(other.m_def, SsaDef()))
  , m_opCode      (std::exchange(other.m_opCode, OpCode::eUnknown))
  , m_flags       (std::exchange(other.m_flags, OpFlags()))
  , m_resultType  (std::exchange(other.m_resultType, Type()))
  , m_operands    (std::move(other.m_operands)) { }

  Op& operator = (Op&& other) {
    m_def         = std::exchange(other.m_def, SsaDef());
    m_opCode      = std::exchange(other.m_opCode, OpCode::eUnknown);
    m_flags       = std::exchange(other.m_flags, OpFlags());
    m_resultType  = std::exchange(other.m_resultType, Type());
    m_operands    = std::move(other.m_operands);
    return *this;
  }

  /** Queries SSA definition. This is valid even if the return type is void,
   *  so that all instructions can be referenced. The SSA definiton will be
   *  assigned when the operation is added to the shader module builder.
   *  Before that, it is a \c null reference and must not be used. */
  SsaDef getDef() const {
    return m_def;
  }

  /** Queries opcode. */
  OpCode getOpCode() const {
    return m_opCode;
  }

  /** Queries flags. */
  OpFlags getFlags() const {
    return m_flags;
  }

  /** Queries number of operands. */
  uint32_t getOperandCount() const {
    return uint32_t(m_operands.size());
  }

  /** Queries given operand. */
  Operand getOperand(uint32_t operandIdx) const {
    return operandIdx < getOperandCount()
      ? m_operands[operandIdx]
      : Operand();
  }

  /** Queries result type. */
  const Type& getType() const {
    return m_resultType;
  }

  /** Sets flags */
  Op& setFlags(OpFlags flags) {
    m_flags = flags;
    return *this;
  }

  /** Sets result type. */
  Op& setResultType(const Type& type) {
    m_resultType = type;
    return *this;
  }

  /** Appends an operand and increments operand count. */
  template<typename T>
  Op& addOperand(T arg) {
    m_operands.push_back(Operand(arg));
    return *this;
  }

  /** Appends multiple operands. */
  template<typename T, typename... Tx>
  Op& addOperands(T arg, Tx... args) {
    return addOperand(arg).addOperands(args...);
  }

  /** Appends no operands */
  Op& addOperands() {
    return *this;
  }

  /** Adds a literal string as operand tokens */
  Op& addLiteralString(const char* string);

  /** Overrides an existing operand. */
  Op& setOperand(uint32_t index, Operand arg) {
    dxbc_spv_assert(index < getOperandCount());

    m_operands[index] = arg;
    return *this;
  }

  /** Assigns SSA definition. Generally, this does not need to be called
   *  directly as SSA defs are assigned when adding instructions to the
   *  builder. */
  Op& setSsaDef(SsaDef def) {
    m_def = def;
    return *this;
  }

  /** Checks whether instruction is declarative */
  bool isDeclarative() const {
    return m_opCode <= OpCode::eLastDeclarative;
  }

  /** Checks whether instruction is a constant. */
  bool isConstant() const {
    return m_opCode == OpCode::eConstant;
  }

  /** Checks whether instruction is an undefined value. */
  bool isUndef() const {
    return m_opCode == OpCode::eUndef;
  }

  /** Checks whether operation has a valid opcode. */
  explicit operator bool () const {
    return m_opCode != OpCode::eUnknown;
  }

  /** Queries index of first literal operand. Instructions that use literal
   *  operands must have them all at the end. Returns the operand count if
   *  the instruction does not have any literal operands. */
  uint32_t getFirstLiteralOperandIndex() const;

  /** Retrieves a literal string starting at the given operand index.
   *  Literal strings may consist of multiple operands. */
  std::string getLiteralString(uint32_t index) const;

  /** Checks whether two instruction definitions are equivalent. This is the
   *  case if the op code, op flags, return types and all operands are equal. */
  bool isEquivalent(const Op& other) const;

  /** Helpers to construct entry point op. */
  static Op EntryPoint(SsaDef function, ShaderStage stage) {
    return Op(OpCode::eEntryPoint, Type())
      .addOperand(function)
      .addOperand(stage);
  };

  static Op EntryPoint(SsaDef controlPointFunction, SsaDef patchConstantFunction, ShaderStage stage) {
    return Op(OpCode::eEntryPoint, Type())
      .addOperand(controlPointFunction)
      .addOperand(patchConstantFunction)
      .addOperand(stage);
  };

  /** Helpers to construct scalar and vector constants */
  template<typename T>
  static Op Constant(T v) {
    auto t = Type(BasicType::from(T(), 1u));
    return Op(OpCode::eConstant, t).addOperand(v);
  }

  template<typename T>
  static Op Constant(T v0, T v1) {
    auto t = Type(BasicType::from(T(), 2u));
    return Op(OpCode::eConstant, t)
      .addOperand(v0)
      .addOperand(v1);
  }

  template<typename T>
  static Op Constant(T v0, T v1, T v2) {
    auto t = Type(BasicType::from(T(), 3u));
    return Op(OpCode::eConstant, t)
      .addOperand(v0)
      .addOperand(v1)
      .addOperand(v2);
  }

  template<typename T>
  static Op Constant(T v0, T v1, T v2, T v3) {
    auto t = Type(BasicType::from(T(), 4u));
    return Op(OpCode::eConstant, t)
      .addOperand(v0)
      .addOperand(v1)
      .addOperand(v2)
      .addOperand(v3);
  }

  static Op Undef(const Type& type) {
    return Op(OpCode::eUndef, type);
  }

  /** Helper to construct debug name ops */
  static Op DebugName(SsaDef def, const char* name) {
    return Op(OpCode::eDebugName, Type())
      .addOperand(def)
      .addLiteralString(name);
  }

  static Op Semantic(SsaDef def, uint32_t index, const char* name) {
    return Op(OpCode::eSemantic, Type())
      .addOperand(def)
      .addOperand(index)
      .addLiteralString(name);
  }

  /** Helper to construct declaration ops */
  static Op DclInput(Type type, SsaDef entryPoint, uint32_t location, uint32_t component) {
    return Op(OpCode::eDclInput, type)
      .addOperand(entryPoint)
      .addOperand(location)
      .addOperand(component);
  }

  static Op DclInput(Type type, SsaDef entryPoint, uint32_t location, uint32_t component, InterpolationModes interpolation) {
    return Op(OpCode::eDclInput, type)
      .addOperand(entryPoint)
      .addOperand(location)
      .addOperand(component)
      .addOperand(interpolation);
  }

  static Op DclInputBuiltIn(Type type, SsaDef entryPoint, BuiltIn builtin) {
    return Op(OpCode::eDclInputBuiltIn, type)
      .addOperand(entryPoint)
      .addOperand(builtin);
  }

  static Op DclInputBuiltIn(Type type, SsaDef entryPoint, BuiltIn builtin, InterpolationModes interpolation) {
    return Op(OpCode::eDclInputBuiltIn, type)
      .addOperand(entryPoint)
      .addOperand(builtin)
      .addOperand(interpolation);
  }

  static Op DclOutput(Type type, SsaDef entryPoint, uint32_t location, uint32_t component) {
    return Op(OpCode::eDclOutput, type)
      .addOperand(entryPoint)
      .addOperand(location)
      .addOperand(component);
  }

  static Op DclOutput(Type type, SsaDef entryPoint, uint32_t location, uint32_t component, uint32_t gsStream) {
    return Op(OpCode::eDclOutput, type)
      .addOperand(entryPoint)
      .addOperand(location)
      .addOperand(component)
      .addOperand(gsStream);
  }

  static Op DclOutputBuiltIn(Type type, SsaDef entryPoint, BuiltIn builtin) {
    return Op(OpCode::eDclOutputBuiltIn, type)
      .addOperand(entryPoint)
      .addOperand(builtin);
  }

  static Op DclOutputBuiltIn(Type type, SsaDef entryPoint, BuiltIn builtin, uint32_t gsStream) {
    return Op(OpCode::eDclOutputBuiltIn, type)
      .addOperand(entryPoint)
      .addOperand(builtin)
      .addOperand(gsStream);
  }

  static Op DclSpecConstant(Type type, SsaDef entryPoint, uint32_t specId, Operand defaultValue) {
    return Op(OpCode::eDclSpecConstant, type)
      .addOperand(entryPoint)
      .addOperand(specId)
      .addOperand(defaultValue);
  }

  static Op DclPushData(Type type, SsaDef entryPoint, uint32_t offset, ShaderStageMask stageMask) {
    return Op(OpCode::eDclPushData, type)
      .addOperand(entryPoint)
      .addOperand(offset)
      .addOperand(stageMask);
  }

  static Op DclSampler(SsaDef entryPoint, uint32_t regSpace, uint32_t regIdx, uint32_t count) {
    return Op(OpCode::eDclSampler, Type())
      .addOperand(entryPoint)
      .addOperand(regSpace)
      .addOperand(regIdx)
      .addOperand(count);
  }

  static Op DclCbv(Type type, SsaDef entryPoint, uint32_t regSpace, uint32_t regIdx, uint32_t count) {
    return Op(OpCode::eDclCbv, type)
      .addOperand(entryPoint)
      .addOperand(regSpace)
      .addOperand(regIdx)
      .addOperand(count);
  }

  static Op DclSrv(Type type, SsaDef entryPoint, uint32_t regSpace, uint32_t regIdx, uint32_t count, ResourceKind kind) {
    return Op(OpCode::eDclSrv, type)
      .addOperand(entryPoint)
      .addOperand(regSpace)
      .addOperand(regIdx)
      .addOperand(count)
      .addOperand(kind);
  }

  static Op DclUav(Type type, SsaDef entryPoint, uint32_t regSpace, uint32_t regIdx, uint32_t count, ResourceKind kind, UavFlags flags) {
    return Op(OpCode::eDclUav, type)
      .addOperand(entryPoint)
      .addOperand(regSpace)
      .addOperand(regIdx)
      .addOperand(count)
      .addOperand(kind)
      .addOperand(flags);
  }

  static Op DclUavCounter(SsaDef entryPoint, SsaDef uav) {
    return Op(OpCode::eDclUavCounter, ScalarType::eU32)
      .addOperand(entryPoint)
      .addOperand(uav);
  }

  static Op DclLds(Type type, SsaDef entryPoint) {
    return Op(OpCode::eDclLds, type)
      .addOperand(entryPoint);
  }

  static Op DclScratch(Type type, SsaDef entryPoint) {
    return Op(OpCode::eDclScratch, type)
      .addOperand(entryPoint);
  }

  static Op DclTmp(Type type, SsaDef entryPoint) {
    return Op(OpCode::eDclTmp, type)
      .addOperand(entryPoint);
  }

  static Op DclParam(Type type) {
    return Op(OpCode::eDclParam, type);
  }

  static Op DclXfb(SsaDef output, uint32_t buffer, uint32_t stride, uint32_t offset) {
    return Op(OpCode::eDclXfb, Type())
      .addOperand(output)
      .addOperand(buffer)
      .addOperand(stride)
      .addOperand(offset);
  }

  /** Helpers to construct mode setting ops */
  static Op SetCsWorkgroupSize(SsaDef def, uint32_t x, uint32_t y, uint32_t z) {
    return Op(OpCode::eSetCsWorkgroupSize, Type())
      .addOperand(def)
      .addOperand(x)
      .addOperand(y)
      .addOperand(z);
  }

  static Op SetGsInstances(SsaDef def, uint32_t n) {
    return Op(OpCode::eSetGsInstances, Type())
      .addOperand(def)
      .addOperand(n);
  }

  static Op SetGsInputPrimitive(SsaDef def, PrimitiveType type) {
    return Op(OpCode::eSetGsInputPrimitive, Type())
      .addOperand(def)
      .addOperand(type);
  }

  static Op SetGsOutputVertices(SsaDef def, uint32_t n) {
    return Op(OpCode::eSetGsOutputVertices, Type())
      .addOperand(def)
      .addOperand(n);
  }

  static Op SetGsOutputPrimitive(SsaDef def, PrimitiveType type, uint32_t stream) {
    return Op(OpCode::eSetGsOutputPrimitive, Type())
      .addOperand(def)
      .addOperand(type)
      .addOperand(stream);
  }

  static Op SetPsEarlyFragmentTest(SsaDef def) {
    return Op(OpCode::eSetPsEarlyFragmentTest, Type()).addOperand(def);
  }

  static Op SetPsDepthGreaterEqual(SsaDef def) {
    return Op(OpCode::eSetPsDepthGreaterEqual, Type()).addOperand(def);
  }

  static Op SetPsDepthLessEqual(SsaDef def) {
    return Op(OpCode::eSetPsDepthLessEqual, Type()).addOperand(def);
  }

  static Op SetTessPrimitive(SsaDef def, PrimitiveType type, TessWindingOrder winding, TessPartitioning partitioning) {
    return Op(OpCode::eSetTessPrimitive, Type())
      .addOperand(def)
      .addOperand(type)
      .addOperand(winding)
      .addOperand(partitioning);
  }

  static Op SetTessDomain(SsaDef def, PrimitiveType domain) {
   return Op(OpCode::eSetTessDomain, Type())
      .addOperand(def)
      .addOperand(domain);
  }

  static Op SetTessControlPoints(SsaDef def, uint32_t inCount, uint32_t outCount) {
    return Op(OpCode::eSetTessControlPoints, Type())
      .addOperand(def)
      .addOperand(inCount)
      .addOperand(outCount);
  }

  /* Helpers for function-related instructions */
  static Op Function(Type type) {
    return Op(OpCode::eFunction, type);
  }

  static Op FunctionEnd() {
    return Op(OpCode::eFunctionEnd, Type());
  }

  static Op FunctionCall(Type type, SsaDef function) {
    return Op(OpCode::eFunctionCall, type)
      .addOperand(function);
  }

  Op& addParam(SsaDef param) {
    return addOperand(param);
  }

  /* Helpers for structured control-flow instructions */
  static Op Label() {
    return Op(OpCode::eLabel, Type())
      .addOperand(Construct::eNone);
  }

  static Op LabelSelection(SsaDef mergeBlock) {
    return Op(OpCode::eLabel, Type())
      .addOperand(mergeBlock)
      .addOperand(Construct::eStructuredSelection);
  }

  static Op LabelLoop(SsaDef mergeBlock, SsaDef continueBlock) {
    return Op(OpCode::eLabel, Type())
      .addOperand(mergeBlock)
      .addOperand(continueBlock)
      .addOperand(Construct::eStructuredLoop);
  }

  static Op Branch(SsaDef block) {
    return Op(OpCode::eBranch, Type())
      .addOperand(block);
  }

  static Op BranchConditional(SsaDef cond, SsaDef trueBlock, SsaDef falseBlock) {
    return Op(OpCode::eBranchConditional, Type())
      .addOperand(cond)
      .addOperand(trueBlock)
      .addOperand(falseBlock);
  }

  static Op Switch(SsaDef value, SsaDef defaultBlock) {
    return Op(OpCode::eSwitch, Type())
      .addOperand(value)
      .addOperand(defaultBlock);
  }

  Op& addCase(SsaDef value, SsaDef block) {
    return addOperand(value)
          .addOperand(block);
  }

  static Op Unreachable() {
    return Op(OpCode::eUnreachable, Type());
  }

  static Op Phi(Type type) {
    return Op(OpCode::ePhi, type);
  }

  Op& addPhi(SsaDef block, SsaDef value) {
    return addOperand(block)
          .addOperand(value);
  }

  static Op Return() {
    return Op(OpCode::eReturn, Type());
  }

  static Op Return(Type type, SsaDef value) {
    return Op(OpCode::eReturn, type)
      .addOperand(value);
  }

  static Op ScopedIf(SsaDef cond) {
    return Op(OpCode::eScopedIf, Type())
      .addOperand(cond);
  }

  static Op ScopedElse() {
    return Op(OpCode::eScopedElse, Type());
  }

  static Op ScopedEndIf() {
    return Op(OpCode::eScopedEndIf, Type());
  }

  static Op ScopedLoop() {
    return Op(OpCode::eScopedLoop, Type());
  }

  static Op ScopedLoopBreak() {
    return Op(OpCode::eScopedLoopBreak, Type());
  }

  static Op ScopedLoopContinue() {
    return Op(OpCode::eScopedLoopContinue, Type());
  }

  static Op ScopedEndLoop() {
    return Op(OpCode::eScopedEndLoop, Type());
  }

  static Op ScopedSwitch(SsaDef value) {
    return Op(OpCode::eScopedSwitch, Type())
      .addOperand(value);
  }

  static Op ScopedSwitchCase(Operand literal) {
    return Op(OpCode::eScopedSwitchCase, Type())
      .addOperand(literal);
  }

  static Op ScopedSwitchDefault() {
    return Op(OpCode::eScopedSwitchDefault, Type());
  }

  static Op ScopedSwitchBreak() {
    return Op(OpCode::eScopedSwitchBreak, Type());
  }

  static Op ScopedEndSwitch() {
    return Op(OpCode::eScopedEndSwitch, Type());
  }

  static Op Barrier(Scope execScope, Scope memScope, MemoryTypeFlags memTypes) {
    return Op(OpCode::eBarrier, Type())
      .addOperand(execScope)
      .addOperand(memScope)
      .addOperand(memTypes);
  }

  static Op ConvertFtoF(Type type, SsaDef value) {
    return Op(OpCode::eConvertFtoF, type)
      .addOperand(value);
  }

  static Op ConvertFtoI(Type type, SsaDef value) {
    return Op(OpCode::eConvertFtoI, type)
      .addOperand(value);
  }

  static Op ConvertItoF(Type type, SsaDef value) {
    return Op(OpCode::eConvertItoF, type)
      .addOperand(value);
  }

  static Op ConvertItoI(Type type, SsaDef value) {
    return Op(OpCode::eConvertItoI, type)
      .addOperand(value);
  }

  static Op ConvertF32toPackedF16(SsaDef value) {
    return Op(OpCode::eConvertF32toPackedF16, ScalarType::eU32)
      .addOperand(value);
  }

  static Op ConvertPackedF16toF32(SsaDef value) {
    return Op(OpCode::eConvertPackedF16toF32, BasicType(ScalarType::eF32, 2u))
      .addOperand(value);
  }

  static Op Cast(Type type, SsaDef value) {
    return Op(OpCode::eCast, type)
      .addOperand(value);
  }

  static Op ConsumeAs(Type type, SsaDef value) {
    return Op(OpCode::eConsumeAs, type)
      .addOperand(value);
  }

  static Op CompositeInsert(Type type, SsaDef composite, SsaDef address, SsaDef value) {
    return Op(OpCode::eCompositeInsert, type)
      .addOperand(composite)
      .addOperand(address)
      .addOperand(value);
  }

  static Op CompositeExtract(Type type, SsaDef composite, SsaDef address) {
    return Op(OpCode::eCompositeExtract, type)
      .addOperand(composite)
      .addOperand(address);
  }

  template<typename... T>
  static Op CompositeConstruct(Type type, T... args) {
    return Op(OpCode::eCompositeConstruct, type)
      .addOperands(SsaDef(args)...);
  }

  static Op CheckSparseAccess(SsaDef feedback) {
    return Op(OpCode::eCheckSparseAccess, ScalarType::eBool)
      .addOperand(feedback);
  }

  static Op ParamLoad(Type type, SsaDef function, SsaDef decl) {
    return Op(OpCode::eParamLoad, type)
      .addOperand(function)
      .addOperand(decl);
  }

  static Op TmpLoad(Type type, SsaDef decl) {
    return Op(OpCode::eTmpLoad, type)
      .addOperand(decl);
  }

  static Op TmpStore(SsaDef decl, SsaDef value) {
    return Op(OpCode::eTmpStore, Type())
      .addOperand(decl)
      .addOperand(value);
  }

  static Op ScratchLoad(Type type, SsaDef decl, SsaDef address) {
    return Op(OpCode::eScratchLoad, type)
      .addOperand(decl)
      .addOperand(address);
  }

  static Op ScratchStore(SsaDef decl, SsaDef address, SsaDef value) {
    return Op(OpCode::eScratchStore, Type())
      .addOperand(decl)
      .addOperand(address)
      .addOperand(value);
  }

  static Op LdsLoad(Type type, SsaDef decl, SsaDef address) {
    return Op(OpCode::eLdsLoad, type)
      .addOperand(decl)
      .addOperand(address);
  }

  static Op LdsStore(SsaDef decl, SsaDef address, SsaDef value) {
    return Op(OpCode::eLdsStore, Type())
      .addOperand(decl)
      .addOperand(address)
      .addOperand(value);
  }

  static Op PushDataLoad(Type type, SsaDef decl, SsaDef address) {
    return Op(OpCode::ePushDataLoad, type)
      .addOperand(decl)
      .addOperand(address);
  }

  static Op SpecConstantLoad(Type type, SsaDef decl, SsaDef address) {
    return Op(OpCode::eSpecConstantLoad, type)
      .addOperand(decl)
      .addOperand(address);
  }

  static Op InputLoad(Type type, SsaDef decl, SsaDef address) {
    return Op(OpCode::eInputLoad, type)
      .addOperand(decl)
      .addOperand(address);
  }

  static Op OutputLoad(Type type, SsaDef decl, SsaDef address) {
    return Op(OpCode::eOutputLoad, type)
      .addOperand(decl)
      .addOperand(address);
  }

  static Op OutputStore(SsaDef decl, SsaDef address, SsaDef value) {
    return Op(OpCode::eOutputStore, Type())
      .addOperand(decl)
      .addOperand(address)
      .addOperand(value);
  }

  static Op DescriptorLoad(Type type, SsaDef decl, SsaDef index) {
    return Op(OpCode::eDescriptorLoad, type)
      .addOperand(decl)
      .addOperand(index);
  }

  static Op BufferLoad(Type type, SsaDef descriptor, SsaDef address, uint32_t alignment) {
    return Op(OpCode::eBufferLoad, type)
      .addOperand(descriptor)
      .addOperand(address)
      .addOperand(alignment);
  }

  static Op BufferStore(SsaDef descriptor, SsaDef address, SsaDef value, uint32_t alignment) {
    return Op(OpCode::eBufferStore, Type())
      .addOperand(descriptor)
      .addOperand(address)
      .addOperand(value)
      .addOperand(alignment);
  }

  static Op BufferQuerySize(SsaDef descriptor) {
    return Op(OpCode::eBufferQuerySize, ScalarType::eU32)
      .addOperand(descriptor);
  }

  static Op MemoryLoad(Type type, SsaDef pointer, SsaDef address, uint32_t alignment) {
    return Op(OpCode::eMemoryLoad, type)
      .addOperand(pointer)
      .addOperand(address)
      .addOperand(alignment);
  }

  static Op MemoryStore(SsaDef pointer, SsaDef address, SsaDef value, uint32_t alignment) {
    return Op(OpCode::eMemoryStore, Type())
      .addOperand(pointer)
      .addOperand(address)
      .addOperand(value)
      .addOperand(alignment);
  }

  static Op ConstantLoad(Type type, SsaDef constant, SsaDef address) {
    return Op(OpCode::eConstantLoad, type)
      .addOperand(constant)
      .addOperand(address);
  }

  static Op LdsAtomic(AtomicOp op, Type type, SsaDef decl, SsaDef address, SsaDef operands) {
    return Op(OpCode::eLdsAtomic, type)
      .addOperand(decl)
      .addOperand(address)
      .addOperand(operands)
      .addOperand(op);
  }

  static Op BufferAtomic(AtomicOp op, Type type, SsaDef descriptor, SsaDef address, SsaDef operands) {
    return Op(OpCode::eBufferAtomic, type)
      .addOperand(descriptor)
      .addOperand(address)
      .addOperand(operands)
      .addOperand(op);
  }

  static Op ImageAtomic(AtomicOp op, Type type, SsaDef descriptor, SsaDef layer, SsaDef coord, SsaDef operands) {
    return Op(OpCode::eImageAtomic, type)
      .addOperand(descriptor)
      .addOperand(layer)
      .addOperand(coord)
      .addOperand(operands)
      .addOperand(op);
  }

  static Op CounterAtomic(AtomicOp op, Type type, SsaDef descriptor) {
    return Op(OpCode::eCounterAtomic, type)
      .addOperand(descriptor)
      .addOperand(op);
  }

  static Op MemoryAtomic(AtomicOp op, Type type, SsaDef pointer, SsaDef address, SsaDef operands) {
    return Op(OpCode::eMemoryAtomic, type)
      .addOperand(pointer)
      .addOperand(address)
      .addOperand(operands)
      .addOperand(op);
  }

  static Op ImageLoad(Type type, SsaDef descriptor, SsaDef mip, SsaDef layer, SsaDef coord, SsaDef sample, SsaDef offset) {
    return Op(OpCode::eImageLoad, type)
      .addOperand(descriptor)
      .addOperand(mip)
      .addOperand(layer)
      .addOperand(coord)
      .addOperand(sample)
      .addOperand(offset);
  }

  static Op ImageStore(SsaDef descriptor, SsaDef layer, SsaDef coord, SsaDef value) {
    return Op(OpCode::eImageStore, Type())
      .addOperand(descriptor)
      .addOperand(layer)
      .addOperand(coord)
      .addOperand(value);
  }

  static Op ImageQuerySize(Type type, SsaDef descriptor, SsaDef mip) {
    return Op(OpCode::eImageQuerySize, type)
      .addOperand(descriptor)
      .addOperand(mip);
  }

  static Op ImageQueryMips(Type type, SsaDef descriptor) {
    return Op(OpCode::eImageQueryMips, type)
      .addOperand(descriptor);
  }

  static Op ImageQuerySamples(Type type, SsaDef descriptor) {
    return Op(OpCode::eImageQuerySamples, type)
      .addOperand(descriptor);
  }

  static Op ImageSample(Type type, SsaDef descriptor, SsaDef sampler,
      SsaDef layer, SsaDef coord, SsaDef offset, SsaDef lodIndex, SsaDef lodBias, SsaDef lodClamp,
      SsaDef derivX, SsaDef derivY, SsaDef depthValue) {
    return Op(OpCode::eImageSample, type)
      .addOperand(descriptor)
      .addOperand(sampler)
      .addOperand(layer)
      .addOperand(coord)
      .addOperand(offset)
      .addOperand(lodIndex)
      .addOperand(lodBias)
      .addOperand(lodClamp)
      .addOperand(derivX)
      .addOperand(derivY)
      .addOperand(depthValue);
  }

  static Op ImageGather(Type type, SsaDef descriptor, SsaDef sampler,
      SsaDef layer, SsaDef coord, SsaDef offset, SsaDef depthValue, uint32_t component) {
    return Op(OpCode::eImageGather, type)
      .addOperand(descriptor)
      .addOperand(sampler)
      .addOperand(layer)
      .addOperand(coord)
      .addOperand(offset)
      .addOperand(depthValue)
      .addOperand(component);
  }

  static Op ImageComputeLod(Type type, SsaDef descriptor, SsaDef sampler, SsaDef coord) {
    return Op(OpCode::eImageComputeLod, type)
      .addOperand(descriptor)
      .addOperand(sampler)
      .addOperand(coord);
  }

  static Op Pointer(Type type, SsaDef address) {
    return Op(OpCode::ePointer, type)
      .addOperand(address);
  }

  static Op PointerAddress(SsaDef pointer) {
    return Op(OpCode::ePointerAddress, ScalarType::eU64)
      .addOperand(pointer);
  }

  static Op EmitVertex(uint32_t stream) {
    return Op(OpCode::eEmitVertex, Type())
      .addOperand(stream);
  }

  static Op EmitPrimitive(uint32_t stream) {
    return Op(OpCode::eEmitPrimitive, Type())
      .addOperand(stream);
  }

  static Op Demote() {
    return Op(OpCode::eDemote, Type());
  }

  static Op InterpolateAtCentroid(Type type, SsaDef input) {
    return Op(OpCode::eInterpolateAtCentroid, type)
      .addOperand(input);
  }

  static Op InterpolateAtSample(Type type, SsaDef input, SsaDef sample) {
    return Op(OpCode::eInterpolateAtSample, type)
      .addOperand(input)
      .addOperand(sample);
  }

  static Op InterpolateAtOffset(Type type, SsaDef input, SsaDef offset) {
    return Op(OpCode::eInterpolateAtOffset, type)
      .addOperand(input)
      .addOperand(offset);
  }

  static Op DerivX(Type type, SsaDef value, DerivativeMode mode) {
    return Op(OpCode::eDerivX, type)
      .addOperand(value)
      .addOperand(mode);
  }

  static Op DerivY(Type type, SsaDef value, DerivativeMode mode) {
    return Op(OpCode::eDerivY, type)
      .addOperand(value)
      .addOperand(mode);
  }

  static Op RovScopedLockBegin() {
    return Op(OpCode::eRovScopedLockBegin, Type());
  }

  static Op RovScopedLockEnd() {
    return Op(OpCode::eRovScopedLockEnd, Type());
  }

  static Op FEq(SsaDef a, SsaDef b) {
    return Op(OpCode::eFEq, ScalarType::eBool)
      .addOperand(a)
      .addOperand(b);
  }

  static Op FNe(SsaDef a, SsaDef b) {
    return Op(OpCode::eFNe, ScalarType::eBool)
      .addOperand(a)
      .addOperand(b);
  }

  static Op FLt(SsaDef a, SsaDef b) {
    return Op(OpCode::eFLt, ScalarType::eBool)
      .addOperand(a)
      .addOperand(b);
  }

  static Op FLe(SsaDef a, SsaDef b) {
    return Op(OpCode::eFLe, ScalarType::eBool)
      .addOperand(a)
      .addOperand(b);
  }

  static Op FGt(SsaDef a, SsaDef b) {
    return Op(OpCode::eFGt, ScalarType::eBool)
      .addOperand(a)
      .addOperand(b);
  }

  static Op FGe(SsaDef a, SsaDef b) {
    return Op(OpCode::eFGe, ScalarType::eBool)
      .addOperand(a)
      .addOperand(b);
  }

  static Op FIsNan(SsaDef a) {
    return Op(OpCode::eFIsNan, ScalarType::eBool)
      .addOperand(a);
  }

  static Op IEq(SsaDef a, SsaDef b) {
    return Op(OpCode::eIEq, ScalarType::eBool)
      .addOperand(a)
      .addOperand(b);
  }

  static Op INe(SsaDef a, SsaDef b) {
    return Op(OpCode::eINe, ScalarType::eBool)
      .addOperand(a)
      .addOperand(b);
  }

  static Op SLt(SsaDef a, SsaDef b) {
    return Op(OpCode::eSLt, ScalarType::eBool)
      .addOperand(a)
      .addOperand(b);
  }

  static Op SLe(SsaDef a, SsaDef b) {
    return Op(OpCode::eSLe, ScalarType::eBool)
      .addOperand(a)
      .addOperand(b);
  }

  static Op SGt(SsaDef a, SsaDef b) {
    return Op(OpCode::eSGt, ScalarType::eBool)
      .addOperand(a)
      .addOperand(b);
  }

  static Op SGe(SsaDef a, SsaDef b) {
    return Op(OpCode::eSGe, ScalarType::eBool)
      .addOperand(a)
      .addOperand(b);
  }

  static Op ULt(SsaDef a, SsaDef b) {
    return Op(OpCode::eULt, ScalarType::eBool)
      .addOperand(a)
      .addOperand(b);
  }

  static Op ULe(SsaDef a, SsaDef b) {
    return Op(OpCode::eULe, ScalarType::eBool)
      .addOperand(a)
      .addOperand(b);
  }

  static Op UGt(SsaDef a, SsaDef b) {
    return Op(OpCode::eUGt, ScalarType::eBool)
      .addOperand(a)
      .addOperand(b);
  }

  static Op UGe(SsaDef a, SsaDef b) {
    return Op(OpCode::eUGe, ScalarType::eBool)
      .addOperand(a)
      .addOperand(b);
  }

  static Op BAnd(SsaDef a, SsaDef b) {
    return Op(OpCode::eBAnd, ScalarType::eBool)
      .addOperand(a)
      .addOperand(b);
  }

  static Op BOr(SsaDef a, SsaDef b) {
    return Op(OpCode::eBOr, ScalarType::eBool)
      .addOperand(a)
      .addOperand(b);
  }

  static Op BEq(SsaDef a, SsaDef b) {
    return Op(OpCode::eBEq, ScalarType::eBool)
      .addOperand(a)
      .addOperand(b);
  }

  static Op BNe(SsaDef a, SsaDef b) {
    return Op(OpCode::eBNe, ScalarType::eBool)
      .addOperand(a)
      .addOperand(b);
  }

  static Op BNot(SsaDef a) {
    return Op(OpCode::eBNot, ScalarType::eBool)
      .addOperand(a);
  }

  static Op Select(Type type, SsaDef cond, SsaDef t, SsaDef f) {
    return Op(OpCode::eSelect, type)
      .addOperand(cond)
      .addOperand(t)
      .addOperand(f);
  }

  static Op FAbs(Type type, SsaDef a) {
    return Op(OpCode::eFAbs, type)
      .addOperand(a);
  }

  static Op FNeg(Type type, SsaDef a) {
    return Op(OpCode::eFNeg, type)
      .addOperand(a);
  }

  static Op FAdd(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eFAdd, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op FSub(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eFSub, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op FMul(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eFMul, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op FMulLegacy(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eFMulLegacy, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op FMad(Type type, SsaDef a, SsaDef b, SsaDef c) {
    return Op(OpCode::eFMad, type)
      .addOperand(a)
      .addOperand(b)
      .addOperand(c);
  }

  static Op FMadLegacy(Type type, SsaDef a, SsaDef b, SsaDef c) {
    return Op(OpCode::eFMadLegacy, type)
      .addOperand(a)
      .addOperand(b)
      .addOperand(c);
  }

  static Op FDiv(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eFDiv, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op FRcp(Type type, SsaDef a) {
    return Op(OpCode::eFRcp, type)
      .addOperand(a);
  }

  static Op FSqrt(Type type, SsaDef a) {
    return Op(OpCode::eFSqrt, type)
      .addOperand(a);
  }

  static Op FRsq(Type type, SsaDef a) {
    return Op(OpCode::eFRsq, type)
      .addOperand(a);
  }

  static Op FExp2(Type type, SsaDef a) {
    return Op(OpCode::eFExp2, type)
      .addOperand(a);
  }

  static Op FLog2(Type type, SsaDef a) {
    return Op(OpCode::eFLog2, type)
      .addOperand(a);
  }

  static Op FFract(Type type, SsaDef a) {
    return Op(OpCode::eFFract, type)
      .addOperand(a);
  }

  static Op FRound(Type type, SsaDef a, RoundMode mode) {
    return Op(OpCode::eFRound, type)
      .addOperand(a)
      .addOperand(mode);
  }

  static Op FMin(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eFMin, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op FMax(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eFMax, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op FDot(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eFDot, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op FDotLegacy(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eFDotLegacy, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op FClamp(Type type, SsaDef a, SsaDef lo, SsaDef hi) {
    return Op(OpCode::eFClamp, type)
      .addOperand(a)
      .addOperand(lo)
      .addOperand(hi);
  }

  static Op FSin(Type type, SsaDef a) {
    return Op(OpCode::eFSin, type)
      .addOperand(a);
  }

  static Op FCos(Type type, SsaDef a) {
    return Op(OpCode::eFCos, type)
      .addOperand(a);
  }

  static Op IAnd(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eIAnd, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op IOr(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eIOr, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op IXor(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eIXor, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op INot(Type type, SsaDef a) {
    return Op(OpCode::eINot, type)
      .addOperand(a);
  }

  static Op IBitInsert(Type type, SsaDef base, SsaDef insert, SsaDef offset, SsaDef count) {
    return Op(OpCode::eIBitInsert, type)
      .addOperand(base)
      .addOperand(insert)
      .addOperand(offset)
      .addOperand(count);
  }

  static Op UBitExtract(Type type, SsaDef base, SsaDef offset, SsaDef count) {
    return Op(OpCode::eUBitExtract, type)
      .addOperand(base)
      .addOperand(offset)
      .addOperand(count);
  }

  static Op SBitExtract(Type type, SsaDef base, SsaDef offset, SsaDef count) {
    return Op(OpCode::eSBitExtract, type)
      .addOperand(base)
      .addOperand(offset)
      .addOperand(count);
  }

  static Op IShl(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eIShl, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op SShr(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eSShr, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op UShr(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eUShr, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op IBitCount(Type type, SsaDef a) {
    return Op(OpCode::eIBitCount, type)
      .addOperand(a);
  }

  static Op IBitReverse(Type type, SsaDef a) {
    return Op(OpCode::eIBitReverse, type)
      .addOperand(a);
  }

  static Op IFindLsb(Type type, SsaDef a) {
    return Op(OpCode::eIFindLsb, type)
      .addOperand(a);
  }

  static Op SFindMsb(Type type, SsaDef a) {
    return Op(OpCode::eSFindMsb, type)
      .addOperand(a);
  }

  static Op UFindMsb(Type type, SsaDef a) {
    return Op(OpCode::eUFindMsb, type)
      .addOperand(a);
  }

  static Op IAdd(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eIAdd, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op IAddCarry(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eIAddCarry, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op ISub(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eISub, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op ISubBorrow(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eISubBorrow, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op INeg(Type type, SsaDef a) {
    return Op(OpCode::eINeg, type)
      .addOperand(a);
  }

  static Op IMul(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eIMul, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op UDiv(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eUDiv, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op UMod(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eUMod, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op SMin(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eSMin, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op SMax(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eSMax, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op SClamp(Type type, SsaDef a, SsaDef lo, SsaDef hi) {
    return Op(OpCode::eSClamp, type)
      .addOperand(a)
      .addOperand(lo)
      .addOperand(hi);
  }

  static Op UMin(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eUMin, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op UMax(Type type, SsaDef a, SsaDef b) {
    return Op(OpCode::eUMax, type)
      .addOperand(a)
      .addOperand(b);
  }

  static Op UClamp(Type type, SsaDef a, SsaDef lo, SsaDef hi) {
    return Op(OpCode::eUClamp, type)
      .addOperand(a)
      .addOperand(lo)
      .addOperand(hi);
  }

  static Op UMSad(Type type, SsaDef ref, SsaDef src, SsaDef accum) {
    return Op(OpCode::eUMSad, type)
      .addOperand(ref)
      .addOperand(src)
      .addOperand(accum);
  }

private:

  SsaDef m_def = { };

  OpCode m_opCode = OpCode::eUnknown;
  OpFlags m_flags = { };

  Type m_resultType = { };

  util::small_vector<Operand, MaxEmbeddedOperands> m_operands = { };

};


inline Operand::Operand(const Op* op) {
  dxbc_spv_assert(!op || op->getDef());
  m_data = op ? op->getDef().getId() : 0u;
}

std::ostream& operator << (std::ostream& os, const ScalarType& ty);
std::ostream& operator << (std::ostream& os, const BasicType& ty);
std::ostream& operator << (std::ostream& os, const Type& ty);
std::ostream& operator << (std::ostream& os, const Construct& construct);
std::ostream& operator << (std::ostream& os, const ResourceKind& kind);
std::ostream& operator << (std::ostream& os, const PrimitiveType& primitive);
std::ostream& operator << (std::ostream& os, const TessWindingOrder& winding);
std::ostream& operator << (std::ostream& os, const TessPartitioning& partitioning);
std::ostream& operator << (std::ostream& os, const BuiltIn& builtIn);
std::ostream& operator << (std::ostream& os, const AtomicOp& atomicOp);
std::ostream& operator << (std::ostream& os, const UavFlag& flag);
std::ostream& operator << (std::ostream& os, const InterpolationMode& flag);
std::ostream& operator << (std::ostream& os, const ShaderStage& stage);
std::ostream& operator << (std::ostream& os, const Scope& stage);
std::ostream& operator << (std::ostream& os, const MemoryType& stage);
std::ostream& operator << (std::ostream& os, const DerivativeMode& stage);
std::ostream& operator << (std::ostream& os, const RoundMode& stage);
std::ostream& operator << (std::ostream& os, const SsaDef& def);
std::ostream& operator << (std::ostream& os, const OpFlag& flag);
std::ostream& operator << (std::ostream& os, const OpCode& opCode);

}

namespace std {

template<>
struct hash<dxbc_spv::ir::SsaDef> {
  size_t operator () (const dxbc_spv::ir::SsaDef& value) const {
    return size_t(value.getId());
  }
};

template<>
struct hash<dxbc_spv::ir::BasicType> {
  size_t operator () (const dxbc_spv::ir::BasicType& type) const {
    return uint32_t(type.getBaseType()) | ((type.getVectorSize() - 1u) << dxbc_spv::ir::ScalarTypeBits);
  }
};

template<>
struct hash<dxbc_spv::ir::Type> {
  size_t operator () (const dxbc_spv::ir::Type& type) const {
    size_t v = type.getArrayDimensions();
    v = dxbc_spv::util::hash_combine(v, type.getStructMemberCount());

    for (uint32_t i = 0u; i < type.getArrayDimensions(); i++)
      v = dxbc_spv::util::hash_combine(v, type.getArraySize(i));

    for (uint32_t i = 0u; i < type.getStructMemberCount(); i++) {
      v = dxbc_spv::util::hash_combine(v,
        std::hash<dxbc_spv::ir::BasicType>()(type.getBaseType(i)));
    }

    return v;
  }
};

}
