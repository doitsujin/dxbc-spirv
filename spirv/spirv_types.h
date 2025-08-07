#include "../submodules/spirv_headers/include/spirv/unified1/spirv.hpp"
#include "../submodules/spirv_headers/include/spirv/unified1/GLSL.std.450.h"

#include "../ir/ir.h"
#include "../ir/ir_builder.h"

#include "../util/util_hash.h"
#include "../util/util_small_vector.h"

namespace dxbc_spv::spirv {

/** SPIR-V binary header */
struct SpirvHeader {
  uint32_t magic = spv::MagicNumber;
  uint32_t version = 0x10600u;
  uint32_t generator = 0u;
  uint32_t boundIds = 1u;
  uint32_t schema = 0u;
};


/** Key for pointer type look-up. Associates
 *  an existing type with a storage class. */
struct SpirvPointerTypeKey {
  uint32_t baseTypeId = 0u;
  spv::StorageClass storageClass = spv::StorageClass(0u);

  bool operator == (const SpirvPointerTypeKey& other) const {
    return baseTypeId == other.baseTypeId &&
           storageClass == other.storageClass;
  }

  bool operator != (const SpirvPointerTypeKey& other) const {
    return !(operator == (other));
  }
};


/** Key for image type look-up. */
struct SpirvImageTypeKey {
  uint32_t sampledTypeId = 0u;
  spv::Dim dim = spv::Dim();
  uint32_t arrayed = 0u;
  uint32_t ms = 0u;
  uint32_t sampled = 0u;
  spv::ImageFormat format = spv::ImageFormatUnknown;

  bool operator == (const SpirvImageTypeKey& other) const {
    return sampledTypeId == other.sampledTypeId &&
           dim == other.dim && arrayed == other.arrayed &&
           ms == other.ms && sampled == other.sampled &&
           format == other.format;
  }

  bool operator != (const SpirvImageTypeKey& other) const {
    return !(operator == (other));
  }
};


/** Key for function type look-up. Consists of a return value
 *  type which may be void, and a list of parameter types. */
struct SpirvFunctionTypeKey {
  ir::Type returnType;
  util::small_vector<ir::Type, 4u> paramTypes;

  bool operator == (const SpirvFunctionTypeKey& other) const {
    bool eq = returnType == other.returnType &&
              paramTypes.size() == other.paramTypes.size();

    for (size_t i = 0u; i < paramTypes.size() && eq; i++)
      eq = paramTypes[i] == other.paramTypes[i];

    return eq;
  }

  bool operator != (const SpirvFunctionTypeKey& other) const {
    return !(operator == (other));
  }
};


/** Function parameter association. Useful to generate a unique
 *  ID for a function parameter in a given function. */
struct SpirvFunctionParameterKey {
  ir::SsaDef funcDef;
  ir::SsaDef paramDef;

  bool operator == (const SpirvFunctionParameterKey& other) const {
    return funcDef == other.funcDef && paramDef == other.paramDef;
  }

  bool operator != (const SpirvFunctionParameterKey& other) const {
    return !(operator == (other));
  }
};


/** Basic scalar or vector constant look-up structure
 *  for constant deduplication. */
struct SpirvConstant {
  spv::Op op = spv::OpNop;
  uint32_t typeId = 0u;
  std::array<uint32_t, 4u> constituents = { };

  bool operator == (const SpirvConstant& other) const {
    bool eq = op == other.op && typeId == other.typeId;

    for (size_t i = 0u; i < constituents.size() && eq; i++)
      eq = constituents[i] == other.constituents[i];

    return eq;
  }

  bool operator != (const SpirvConstant& other) const {
    return !(operator == (other));
  }
};


/** BDA pointer type */
struct SpirvBdaTypeKey {
  ir::Type type = { };
  ir::UavFlags flags = 0u;

  bool operator == (const SpirvBdaTypeKey& other) const {
    return type == other.type && flags == other.flags;
  }

  bool operator != (const SpirvBdaTypeKey& other) const {
    return !(operator == (other));
  }
};


/** Memory operands */
struct SpirvMemoryOperands {
  uint32_t flags         = 0;
  uint32_t alignment     = 0;
  uint32_t makeAvailable = 0;
  uint32_t makeVisible   = 0;

  uint32_t computeDwordCount() const {
    if (!flags)
      return 0u;

    uint32_t count = 1u;

    if (flags & spv::MemoryAccessAlignedMask)
      count += 1u;

    if (flags & spv::MemoryAccessMakePointerAvailableMask)
      count += 1u;

    if (flags & spv::MemoryAccessMakePointerVisibleMask)
      count += 1u;

    return count;
  }

  template<typename T>
  void pushTo(T& container) const {
    if (flags) {
      container.push_back(flags);

      for (uint32_t iter = flags; iter; iter &= iter - 1u) {
        uint32_t bit = iter & -iter;

        switch (bit) {
          case spv::MemoryAccessAlignedMask:
            container.push_back(alignment);
            break;

          case spv::MemoryAccessMakePointerAvailableMask:
            container.push_back(makeAvailable);
            break;

          case spv::MemoryAccessMakePointerVisibleMask:
            container.push_back(makeVisible);
            break;
        }
      }
    }
  }
};


/** Image operands */
struct SpirvImageOperands {
  uint32_t flags          = 0u;
  uint32_t lodBias        = 0u;
  uint32_t lodIndex       = 0u;
  uint32_t constOffset    = 0u;
  uint32_t gradX          = 0u;
  uint32_t gradY          = 0u;
  uint32_t dynamicOffset  = 0u;
  uint32_t sampleId       = 0u;
  uint32_t minLod         = 0u;
  uint32_t makeAvailable  = 0u;
  uint32_t makeVisible    = 0u;

  uint32_t computeDwordCount() const {
    if (!flags)
      return 0u;

    uint32_t count = 1u;

    if (flags & spv::ImageOperandsBiasMask)
      count += 1u;

    if (flags & spv::ImageOperandsLodMask)
      count += 1u;

    if (flags & spv::ImageOperandsGradMask)
      count += 2u;

    if (flags & spv::ImageOperandsConstOffsetMask)
      count += 1u;

    if (flags & spv::ImageOperandsOffsetMask)
      count += 1u;

    if (flags & spv::ImageOperandsSampleMask)
      count += 1u;

    if (flags & spv::ImageOperandsMinLodMask)
      count += 1u;

    if (flags & spv::ImageOperandsMakeTexelAvailableMask)
      count += 1u;

    if (flags & spv::ImageOperandsMakeTexelVisibleMask)
      count += 1u;

    return count;
  }

  template<typename T>
  void pushTo(T& container) const {
    if (flags) {
      container.push_back(flags);

      for (uint32_t iter = flags; iter; iter &= iter - 1u) {
        uint32_t bit = iter & -iter;

        switch (bit) {
          case spv::ImageOperandsBiasMask:
            container.push_back(lodBias);
            break;

          case spv::ImageOperandsLodMask:
            container.push_back(lodIndex);
            break;

          case spv::ImageOperandsGradMask:
            container.push_back(gradX);
            container.push_back(gradY);
            break;

          case spv::ImageOperandsConstOffsetMask:
            container.push_back(constOffset);
            break;

          case spv::ImageOperandsOffsetMask:
            container.push_back(dynamicOffset);
            break;

          case spv::ImageOperandsSampleMask:
            container.push_back(sampleId);
            break;

          case spv::ImageOperandsMinLodMask:
            container.push_back(minLod);
            break;

          case spv::ImageOperandsMakeTexelAvailableMask:
            container.push_back(makeAvailable);
            break;

          case spv::ImageOperandsMakeTexelVisibleMask:
            container.push_back(makeVisible);
            break;
        }
      }
    }
  }
};

}


namespace std {

template<>
struct hash<dxbc_spv::spirv::SpirvPointerTypeKey> {
  size_t operator () (const dxbc_spv::spirv::SpirvPointerTypeKey& k) const {
    return dxbc_spv::util::hash_combine(uint32_t(k.baseTypeId), uint32_t(k.storageClass));
  }
};

template<>
struct hash<dxbc_spv::spirv::SpirvFunctionTypeKey> {
  size_t operator () (const dxbc_spv::spirv::SpirvFunctionTypeKey& k) const {
    size_t hash = std::hash<dxbc_spv::ir::Type>()(k.returnType);

    for (const auto& t : k.paramTypes)
      hash = dxbc_spv::util::hash_combine(hash, std::hash<dxbc_spv::ir::Type>()(t));

    return hash;
  }
};

template<>
struct hash<dxbc_spv::spirv::SpirvImageTypeKey> {
  size_t operator () (const dxbc_spv::spirv::SpirvImageTypeKey& k) const {
    size_t hash = k.sampledTypeId;
    hash = dxbc_spv::util::hash_combine(hash, uint32_t(k.dim));
    hash = dxbc_spv::util::hash_combine(hash, k.arrayed);
    hash = dxbc_spv::util::hash_combine(hash, k.ms);
    hash = dxbc_spv::util::hash_combine(hash, k.sampled);
    hash = dxbc_spv::util::hash_combine(hash, uint32_t(k.format));
    return hash;
  }
};

template<>
struct hash<dxbc_spv::spirv::SpirvFunctionParameterKey> {
  size_t operator () (const dxbc_spv::spirv::SpirvFunctionParameterKey& k) const {
    return dxbc_spv::util::hash_combine(
      std::hash<dxbc_spv::ir::SsaDef>()(k.funcDef),
      std::hash<dxbc_spv::ir::SsaDef>()(k.paramDef));
  }
};

template<>
struct hash<dxbc_spv::spirv::SpirvConstant> {
  size_t operator () (const dxbc_spv::spirv::SpirvConstant& c) const {
    size_t hash = uint32_t(c.op);
    hash = dxbc_spv::util::hash_combine(hash, c.typeId);

    for (uint32_t i = 0u; i < c.constituents.size(); i++)
      hash = dxbc_spv::util::hash_combine(hash, c.constituents[i]);

    return hash;
  }
};

template<>
struct hash<dxbc_spv::spirv::SpirvBdaTypeKey> {
  size_t operator () (const dxbc_spv::spirv::SpirvBdaTypeKey& k) const {
    return dxbc_spv::util::hash_combine(std::hash<dxbc_spv::ir::Type>()(k.type), uint32_t(k.flags));
  }
};

}
