#pragma once

#include <map>

#include "dxbc_parser.h"
#include "dxbc_types.h"

#include "../ir/ir.h"
#include "../ir/ir_builder.h"

namespace dxbc_spv::dxbc {

class Converter;

/** Resource info */
struct ResourceInfo {
  /* Register / Resource type */
  RegisterType regType = { };

  /* Register index */
  uint32_t regIndex = 0u;

  /* Register space in Shader Model 5.1. For older shader
   * models, this is always going to be 0. */
  uint32_t regSpace = 0u;

  /* Declared register range. For Shader Model 5.1, the resource
   * count may be 0 which indicates an unbounded resource array.
   * For older shader models, resourceIndex equals regIndex, and
   * resourceCount will be 1. */
  uint32_t resourceIndex = 0u;
  uint32_t resourceCount = 0u;

  /* Resource kind being declared */
  ir::ResourceKind kind = { };

  /* Declared data type of the resource.
   *
   * - For constant buffers, this is a vec4 array fo an unknown type.
   * - For raw buffers, this is a plain unbounded array of unknown
   *   type, and will likely be mapped to u32 down the line.
   * - For structured buffers, this is an unbounded array of a sized
   *   array of unknown type, which may be promoted to a structure.
   * - For typed buffers and images, this is a scalar type that matches
   *   the returned scalar type of any sample or read operations. */
  ir::Type type = { };

  /* Declarations for the resource itself, as well as any UAV counter. */
  ir::SsaDef resourceDef = { };
  ir::SsaDef counterDef = { };
};


/** Retrieved typed resource parameters */
struct ResourceProperties {
  /* Resource kind */
  ir::ResourceKind kind = { };

  /* Scalar sampled type */
  ir::ScalarType type = { };

  /* Loaded descriptor */
  ir::SsaDef descriptor = { };
};


/** Resource look-up structure */
struct ResourceKey {
  RegisterType  regType  = { };
  uint32_t      regIndex = 0u;

  bool operator == (const ResourceKey& other) const { return regType == other.regType && regIndex == other.regIndex; }
  bool operator != (const ResourceKey& other) const { return regType != other.regType || regIndex != other.regIndex; }
  bool operator <  (const ResourceKey& other) const { return regType < other.regType || (regType == other.regType && regIndex <  other.regIndex); }
  bool operator <= (const ResourceKey& other) const { return regType < other.regType || (regType == other.regType && regIndex <= other.regIndex); }
  bool operator >  (const ResourceKey& other) const { return regType > other.regType || (regType == other.regType && regIndex >  other.regIndex); }
  bool operator >= (const ResourceKey& other) const { return regType > other.regType || (regType == other.regType && regIndex >= other.regIndex); }
};


/** Resource variable map. Handles both resource declaration and access,
 *  abstracting away the differences between Shader Model 5.0 and 5.1. */
class ResourceMap {
  static constexpr uint32_t Sm50CbvCount = 14u;
  static constexpr uint32_t Sm50SamplerCount = 16u;
  static constexpr uint32_t Sm50SrvCount = 128u;
  static constexpr uint32_t Sm50UavCount = 64u;

  static constexpr uint32_t MaxCbvSize = 4096u;
public:

  explicit ResourceMap(Converter& converter);

  ~ResourceMap();

  /** Processes a constant buffer declaration and adds it to the
   *  internal look-up table. */
  bool handleDclConstantBuffer(ir::Builder& builder, const Instruction& op);

  /** Processes a raw resource or UAV declaration. */
  bool handleDclResourceRaw(ir::Builder& builder, const Instruction& op);

  /** Processes a structured resource or UAV declaration. */
  bool handleDclResourceStructured(ir::Builder& builder, const Instruction& op);

  /** Handles typed resource or UAV declaration. */
  bool handleDclResourceTyped(ir::Builder& builder, const Instruction& op);

  /** Handles sampler declarations. */
  bool handleDclSampler(ir::Builder& builder, const Instruction& op);

  /** Adjusts UAV flags for the given UAV based on usage. */
  void setUavFlagsForLoad(ir::Builder& builder, const Instruction& op, const Operand& operand);
  void setUavFlagsForStore(ir::Builder& builder, const Instruction& op, const Operand& operand);
  void setUavFlagsForAtomic(ir::Builder& builder, const Instruction& op, const Operand& operand);

  void normalizeUavFlags(ir::Builder& builder);

  /** Loads a resource or sampler descriptor and retrieves basic
   *  properties required to perform any operations on typed resources. */
  ResourceProperties emitDescriptorLoad(
          ir::Builder&            builder,
    const Instruction&            op,
    const Operand&                operand);

  /** Declares and loads UAV counter descriptor for the given UAV. */
  ir::SsaDef emitUavCounterDescriptorLoad(
          ir::Builder&            builder,
    const Instruction&            op,
    const Operand&                operand);

  /** Loads data from a constant buffer using one or more BufferLoad
   *  instruction. If possiblem this will emit a vectorized load. */
  ir::SsaDef emitConstantBufferLoad(
          ir::Builder&            builder,
    const Instruction&            op,
    const Operand&                operand,
          WriteMask               componentMask,
          ir::ScalarType          scalarType);

  /** Loads vectorized data from a raw or structured buffer. The element
   *  offset is the raw byte offset into the structure for structured
   *  buffers, and must be null for raw buffers.
   *  If the instruction is a sparse feedback instruction, the sparse
   *  feedback value will be returned in the second part of the result. */
  std::pair<ir::SsaDef, ir::SsaDef> emitRawStructuredLoad(
          ir::Builder&            builder,
    const Instruction&            op,
    const Operand&                operand,
          ir::SsaDef              elementIndex,
          ir::SsaDef              elementOffset,
          WriteMask               componentMask,
          ir::ScalarType          scalarType);

  /** Stores vectorized data into a raw or structured buffer. Addressing
   *  works exactly the same as it does for loads. */
  bool emitRawStructuredStore(
          ir::Builder&            builder,
    const Instruction&            op,
    const Operand&                operand,
          ir::SsaDef              elementIndex,
          ir::SsaDef              elementOffset,
          ir::SsaDef              data);

private:

  Converter&      m_converter;

  util::small_vector<ResourceInfo, 256u> m_resources;

  std::pair<ir::SsaDef, const ResourceInfo*> loadDescriptor(
          ir::Builder&            builder,
    const Instruction&            op,
    const Operand&                operand,
          bool                    uavCounter);

  ResourceInfo* insertResourceInfo(
    const Instruction&            op,
    const Operand&                operand);

  ir::SsaDef declareUavCounter(
          ir::Builder&            builder,
          ResourceInfo&           resource);

  void emitDebugName(
          ir::Builder&            builder,
    const ResourceInfo*           info);

  bool matchesResource(
    const Instruction&            op,
    const Operand&                operand,
    const ResourceInfo&           info) const;

  void rewriteSm50ResourceAccess(
          ir::Builder&            builder,
    const ResourceInfo&           oldResource,
    const ResourceInfo&           newResource);

  ResourceInfo* declareSm50ResourceArray(
          ir::Builder&            builder,
    const Instruction&            op,
    const Operand&                operand);

  ResourceInfo* getResourceInfo(
          ir::Builder&            builder,
    const Instruction&            op,
    const Operand&                operand);

  static uint32_t determineSm50ResourceArraySize(
          RegisterType            type);

  static ir::UavFlags getUavFlags(
          ir::Builder&            builder,
    const ResourceInfo&           info);

  static void setUavFlags(
          ir::Builder&            builder,
    const ResourceInfo&           info,
          ir::UavFlags            flags);

  static std::optional<std::pair<ir::ResourceKind, ir::Type>> getResourceTypeForToken(
    const OpToken&                tok);

  static uint32_t computeRawStructuredAlignment(
          ir::Builder&            builder,
    const ResourceInfo&           resource,
          ir::SsaDef              elementOffset,
          WriteMask               components);

  static ir::UavFlags getInitialUavFlags(
    const Instruction&            op);

};

}
