#include "dxbc_converter.h"
#include "dxbc_resources.h"

namespace dxbc_spv::dxbc {

ResourceMap::ResourceMap(Converter& converter)
: m_converter (converter) {

}


ResourceMap::~ResourceMap() {

}


bool ResourceMap::handleDclConstantBuffer(ir::Builder& builder, const Instruction& op) {
  /* dcl_constant_buffer operands depend on the shader model in use.
   *
   * For SM5.0 and older:
   * (dst0) The constant buffer register, using the first index dimension
   *        to declare the register, and the second dimension to declare
   *        the size in units of vec4.
   *
   * For SM5.1:
   * (dst0) The register, declaring the lower and upper range of registers
   *        in the register space in index 1 and 2, respectively. Index 0
   *        serves as a variable ID to identify the register range.
   * (imm0) The constant buffer size, in units of vec4.
   * (imm1) The register space index.
   */
  const auto& operand = op.getDst(0u);

  if (operand.getRegisterType() != RegisterType::eCbv)
    return m_converter.logOpError(op, "Instruction does not declare a valid constant buffer.");

  auto info = insertResourceInfo(op, operand);

  if (!info)
    return false;

  /* Determine declared array size. If the constant buffer is dynamically
   * indexed, ignore it and declare it with the maximum supported size
   * anyway to avoid undefined behaviour down the line. */
  uint32_t arraySize = 0u;

  if (!op.getOpToken().getCbvDynamicIndexingFlag()) {
    if (m_converter.isSm51())
      arraySize = op.getImm(0u).getImmediate<uint32_t>(0u);
    else
      arraySize = operand.getIndex(1u);
  }

  if (!arraySize)
    arraySize = 65536u / 16u;

  info->kind = ir::ResourceKind::eBufferStructured;
  info->type = ir::Type(ir::ScalarType::eUnknown, 4u).addArrayDimension(arraySize);
  info->resourceDef = builder.add(ir::Op::DclCbv(info->type, m_converter.getEntryPoint(),
    info->resourceIndex, info->resourceCount, info->regSpace));
  return true;
}


ir::SsaDef ResourceMap::emitConstantBufferLoad(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand,
        WriteMask               componentMask,
        ir::ScalarType          scalarType) {
  auto [descriptor, resource] = loadDescriptor(builder, op, operand);

  if (!resource)
    return ir::SsaDef();

  /* Index into vector array. If we're not loading the whole thing,
   * we need to further index into the buffer itself. */
  auto bufferType = resource->type.getBaseType(0u).getBaseType();

  auto index = m_converter.loadOperandIndex(builder, op, operand, m_converter.isSm51() ? 2u : 1u);
  auto readMask = operand.getSwizzle().getReadMask(componentMask);

  std::array<ir::SsaDef, 4u> components = { };

  if (readMask == ComponentBit::eAll) {
    /* Read entire vector in one go, no need to addres into scalars */
    auto result = builder.add(ir::Op::BufferLoad(
      ir::BasicType(bufferType, 4u), descriptor, index, 16u));

    for (uint32_t i = 0u; i < components.size(); i++)
      components[i] = m_converter.extractFromVector(builder, result, i);
  } else {
    /* Absolute component alignment, in dwords */
    constexpr uint32_t ComponentAlignments = 0x1214;

    while (readMask) {
      /* Consecutive blocks of components to read */
      auto block = extractConsecutiveComponents(readMask);
      auto blockType = ir::BasicType(bufferType, util::popcnt(uint8_t(block)));

      /* First component in the block */
      auto componentIndex = uint8_t(componentFromBit(block.first()));
      auto blockAlignment = 4u * util::bextract(ComponentAlignments, 4u * componentIndex, 4u);

      /* Build address vector with the component index */
      auto address = builder.add(ir::Op::CompositeConstruct(
        ir::BasicType(ir::ScalarType::eU32, 2u), index,
        builder.makeConstant(uint32_t(componentIndex))));

      /* Emit actual buffer load for the block and write back scalars */
      auto result = builder.add(ir::Op::BufferLoad(blockType, descriptor, address, blockAlignment));

      for (uint32_t i = 0u; i < blockType.getVectorSize(); i++)
        components[componentIndex + i] = m_converter.extractFromVector(builder, result, i);

      readMask -= block;
    }
  }

  /* Convert scalars to the requested type */
  for (auto& scalar : components) {
    if (scalar && scalarType != bufferType)
      scalar = builder.add(ir::Op::ConsumeAs(scalarType, scalar));
  }

  /* Build result vector */
  return m_converter.composite(builder,
    m_converter.makeVectorType(scalarType, componentMask),
    components.data(), operand.getSwizzle(), componentMask);
}


std::pair<ir::SsaDef, const ResourceInfo*> ResourceMap::loadDescriptor(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand) {
  ResourceKey key = { };
  key.regType = operand.getRegisterType();
  key.regIndex = operand.getIndex(0u);

  auto entry = m_resources.find(key);

  if (entry == m_resources.end()) {
    auto name = m_converter.makeRegisterDebugName(key.regType, key.regIndex, WriteMask());
    m_converter.logOpError(op, "Resource ", name, " not declared.");
    return std::make_pair(ir::SsaDef(), nullptr);
  }

  auto descriptorType = [&key] {
    switch (key.regType) {
      case RegisterType::eSampler:  return ir::ScalarType::eSampler;
      case RegisterType::eCbv:      return ir::ScalarType::eCbv;
      case RegisterType::eResource: return ir::ScalarType::eSrv;
      case RegisterType::eUav:      return ir::ScalarType::eUav;
      default:                      break;
    }

    dxbc_spv_unreachable();
    return ir::ScalarType::eVoid;
  } ();

  ir::SsaDef descriptorIndex = { };

  if (m_converter.isSm51()) {
    /* Second index contains the actual index, but as an absolute register
     * index. Deal with it the same way we do for I/O registers and split
     * the index into its absolute and relative parts. */
    uint32_t absIndex = operand.getIndex(1u) - entry->second.resourceIndex;

    if (hasRelativeIndexing(operand.getIndexType(1u))) {
      descriptorIndex = m_converter.loadSrcModified(builder, op,
        op.getRawOperand(operand.getIndexOperand(1u)),
        ComponentBit::eX, ir::ScalarType::eU32);

      if (absIndex) {
        descriptorIndex = builder.add(ir::Op::IAdd(
          ir::ScalarType::eU32, descriptorIndex, builder.makeConstant(absIndex)));
      }
    } else {
      descriptorIndex = builder.makeConstant(absIndex);
    }

  } else {
    /* No descriptor indexing in SM5.0 */
    descriptorIndex = builder.makeConstant(0u);
  }

  auto def = builder.add(ir::Op::DescriptorLoad(descriptorType,
    entry->second.resourceDef, descriptorIndex));

  /* Apply non-uniform modifier to the descriptor load */
  if (operand.getModifiers().isNonUniform())
    builder.setOpFlags(def, ir::OpFlag::eNonUniform);

  return std::make_pair(def, &entry->second);
}


ResourceInfo* ResourceMap::insertResourceInfo(
  const Instruction&            op,
  const Operand&                operand) {
  ResourceKey key = { };
  key.regType = operand.getRegisterType();
  key.regIndex = operand.getIndex(0u);

  auto entry = m_resources.emplace(std::piecewise_construct, std::tuple(key), std::tuple());

  if (!entry.second) {
    auto name = m_converter.makeRegisterDebugName(key.regType, key.regIndex, WriteMask());
    m_converter.logOpError(op, "Resource ", name, " already declared.");
    return nullptr;
  }

  auto& info = entry.first->second;
  info.regType = key.regType;
  info.regIndex = key.regIndex;

  if (m_converter.isSm51()) {
    dxbc_spv_assert(op.getImmCount());

    info.regSpace = op.getImm(op.getImmCount() - 1u).getImmediate<uint32_t>(0u);
    info.resourceIndex = operand.getIndex(1u);
    info.resourceCount = operand.getIndex(2u) + 1u;

    /* If the high index is -1 (and thus, adding one results in 0),
     * this is an unbounded register array. */
    if (info.resourceCount)
       info.resourceCount -= info.resourceIndex;
  } else {
    info.resourceIndex = info.regIndex;
    info.resourceCount = 1u;
  }

  return &info;
}

}
