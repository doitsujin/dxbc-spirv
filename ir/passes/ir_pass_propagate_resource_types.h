#pragma once

#include <optional>
#include <utility>

#include "../ir.h"
#include "../ir_builder.h"
#include "../ir_utils.h"

namespace dxbc_spv::ir {

/** Per-element remapping info */
struct PropagateResourceTypeElementInfo {
  /* Resolved access type. */
  ScalarType resolvedType = ScalarType::eVoid;

  /* Access size for vectorized loads or stores. If 0, this
   * element is never addressed directly, but may be accessed
   * indirectly via a vector load/store. */
  uint8_t accessSize = 0u;

  /* Flag indicating whether or not this field is accessed
   * atomically. If true, the type must not be overwritten. */
  bool isAtomicallyAccessed = false;

  /* Vector component index, if applicable. If the type being
   * addressed is scalar anyway, this will be negative. Can be
   * ignored if an access has the same size as the vector. */
  int8_t componentIndex = -1;

  /* Struct member index or array element index, if applicable.
   * If the addressed type is a scalar or vector, this will be
   * negative. If the type is flattened, this is the scalar
   * offset in the flattened array for a given structure. */
  int16_t memberIndex = -1;

  bool isUsed() const {
    return resolvedType != ScalarType::eVoid;
  }
};


/** Resource type rewrite info. Stores all the information necessary to
 *  rewrite resource declarations and usage instructions to a new type. */
struct PropagateResourceTypeRewriteInfo {
  /* Per-dword mapping from the old type to the new type. */
  util::small_vector<PropagateResourceTypeElementInfo, 512u> elements;

  /* Old type of the resource. Must be an array type of an
   * unknown scalar or vector type. */
  Type oldType = { };

  /* New type of the resource. Must be similar in layout to
   * the old type. If this declares a struct type, the byte
   * size of the struct must match that of the old type. */
  Type newType = { };

  /* Number of outer array dimensions in the old type. This
   * is the number of index components to ignore in address
   * operands. These dimensions are included in oldType. */
  uint32_t oldOuterArrayDims = 0u;

  /* Number of outer array dimensions in the new type. If less
   * than the old outer array dimension count, the outermost
   * dimensions were removed. If greater, new dimensions were
   * added and must be indexed with constant 0. */
  uint32_t newOuterArrayDims = 0u;

  /* Flag to indicate whether any field in the structure is
   * accessed via an atomic operation. This is most relevant
   * in combination with dynamic indexing. */
  bool isAtomicallyAccessed = false;

  /* Whether the inner structure itself is dynamically indexed.
   * If true, the old and new inner types must both be a basic
   * type with the same vector size or an array thereof, and
   * addresses between the old and new type are fully compatible
   * if the structure was not also flattened. */
  bool isDynamicallyIndexed = false;

  /* Fallback scalar type in case the structure is dynamically
   * indexed. If there are atomic accesses, this must match the
   * type of the atomic operation. */
  ScalarType fallbackType = ScalarType::eVoid;

  /* Whether to flatten the inner structure. In this case, the
   * new type must be a scalar with at least one outer array
   * dimension, and any address calculations will be done on
   * the innermost array index. */
  bool isFlattened = false;

  /* If flattened, this represents the number of scalars per
   * flattened structure and must be greater than 0. */
  uint32_t flattenedScalarCount = 0u;

  /* Resolves types by traversing the outer array dimensions. */
  Type getOldInnerType() const;
  Type getNewInnerType() const;

  /* Generates LDS type. Typically emits a basic struct. */
  void processLocalLayout(bool flatten, bool allowSubDword, bool removeTrivialArrays);

  /* Generates constant buffer type. Respects vec4 layout for
   * constant buffers, but may subdivide vectors depending on
   * the access patterns. Provides the option to emit a plain
   * vec4 array with adjusted underlying scalar type. */
  void processConstantBufferLayout(bool structured);

  /* Generates resource buffer layout. For raw buffers, this
   * may merely change the underlying scalar type. Structured
   * buffers can be emitted as either nested arrays or fully
   * typed structs depending on usage patterns. */
  void processResourceBufferLayout(bool structured);

private:

  static Type traverseType(Type t, uint32_t n);

  void normalizeElementAccess();

  void normalizeTypesTo32Bit();

  void handleLdsUnusedElements();

  void handleConstantBufferUnusedElements();

  void handleResourceBufferUnusedElements();

  bool setupLocalType();

  bool setupConstantBufferType();

  bool setupResourceBufferType();

  void setupArrayType();

  void flattenStructureType();

  void flattenTrivialArrays();

  void normalizeFinalElementTypes();

  bool hasOverlappingVectorAccess() const;

  ScalarType determineCommonScalarType() const;

};


/** Pass to resolve types of buffer resources, scratch and LDS variables.
 */
class PropagateResourceTypesPass {

public:

  struct Options {
    /* Whether to allow sub-dword types for scratch and LDS memory. If
     * not set, loads and stores will always be promoted to 32-bit. */
    bool allowSubDwordScratchAndLds = true;
    /* Whether to flatten structured LDS arrays to a scalar array. */
    bool flattenLds = false;
    /* Whether to flatten vectorized scratch arrays to a scalar array. */
    bool flattenScratch = false;
    /* Whether to allow fully structured constant buffers. If false,
     * constant buffers are guaranteed to be a vector array. */
    bool structuredCbv = true;
    /* Whether to allow fully structured resource buffers. If false,
     * structured buffers are represented as a nested array. */
    bool structuredSrvUav = true;
  };

  PropagateResourceTypesPass(Builder& builder, const Options& options);

  ~PropagateResourceTypesPass();

  PropagateResourceTypesPass             (const PropagateResourceTypesPass&) = delete;
  PropagateResourceTypesPass& operator = (const PropagateResourceTypesPass&) = delete;

  /** Runs type propagation pass. Assumes that consume chains have been
   *  eliminated already, i.e. there are no ConsumeAs instructions that
   *  have another ConsumeAs as an operand. */
  void run();

  /** Rewrites partial vector loads, i.e. loads that access multiple vector
   *  components from a larger vector, to load the full vector and emit a
   *  swizzle. This is run once as part of the normal pass, but can be run as
   *  a dedicated pass in case the full type propagation pass is not needed. */
  void rewritePartialVectorLoads();

  /** Initializes and runs pass on the given builder. */
  static void runPass(Builder& builder, const Options& options);

  /** Runs partial vector load fix-up. */
  static void runPartialVectorLoadRewritePass(Builder& builder);

private:

  Builder&  m_builder;
  Options   m_options;

  void rewriteLoad(SsaDef def, const PropagateResourceTypeRewriteInfo& info);

  void rewriteStore(SsaDef def, const PropagateResourceTypeRewriteInfo& info);

  void rewriteAtomic(SsaDef def, const PropagateResourceTypeRewriteInfo& info);

  ScalarType getMappedType(const PropagateResourceTypeRewriteInfo& info, SsaDef address);

  SsaDef rewriteAddress(const PropagateResourceTypeRewriteInfo& info, SsaDef address, uint32_t accessVectorSize);

  SsaDef rewriteValue(SsaDef value, ScalarType type);

  SsaDef computeFlattenedScalarIndex(const PropagateResourceTypeRewriteInfo& info, SsaDef address);

  std::optional<uint32_t> extractConstantFromAddress(SsaDef address, uint32_t component) const;

  SsaDef extractIndexFromAddress(SsaDef address, uint32_t component) const;

  std::optional<uint32_t> computeDwordIndex(Type type, SsaDef address, uint32_t firstComponent);

  PropagateResourceTypeElementInfo lookupElement(const PropagateResourceTypeRewriteInfo& info, SsaDef address);

  BasicType determineAccessTypeForLoad(const Op& op);

  BasicType determineAccessTypeForStore(const Op& op);

  BasicType determineAccessTypeForAtomic(const Op& op);

  void determineElementTypeForAccess(const Op& op, PropagateResourceTypeRewriteInfo& info, BasicType accessType);

  void determineElementTypeForUses(const Op& op, PropagateResourceTypeRewriteInfo& info);

  void determineDeclarationType(const Op& op, PropagateResourceTypeRewriteInfo& info);

  void rewriteAccessOps(SsaDef def, const PropagateResourceTypeRewriteInfo& info);

  void rewriteDeclaration(SsaDef def, const PropagateResourceTypeRewriteInfo& info);

  void rewritePartialVectorLoad(const Type& type, SsaDef def);

  void rewritePartialVectorLoadsForDescriptor(const Type& type, SsaDef def);

  bool canRemoveTrivialArrays(const Op& op);

  static bool isUntypedDeclaration(const Op& op);

};

}
