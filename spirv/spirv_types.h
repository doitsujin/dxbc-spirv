#include <spirv/unified1/spirv.hpp>
#include <spirv/unified1/GLSL.std.450.h>

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

}
