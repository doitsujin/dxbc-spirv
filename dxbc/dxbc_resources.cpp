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
    info->regSpace, info->resourceIndex, info->resourceCount));

  emitDebugName(builder, info);
  return true;
}


bool ResourceMap::handleDclResourceRaw(ir::Builder& builder, const Instruction& op) {
  /* dcl_resource_structured and dcl_uav_structured have the following operands:
   * (dst0) The resource or uav register to declare
   * (imm0) The register space (SM5.1 only)
   */
  const auto& operand = op.getDst(0u);

  if (operand.getRegisterType() != RegisterType::eResource &&
      operand.getRegisterType() != RegisterType::eUav)
    return m_converter.logOpError(op, "Instruction does not declare a valid resource.");

  auto info = insertResourceInfo(op, operand);

  if (!info)
    return false;

  info->kind = ir::ResourceKind::eBufferRaw;
  info->type = ir::Type(ir::ScalarType::eUnknown)
    .addArrayDimension(0u);

  if (operand.getRegisterType() == RegisterType::eUav) {
    info->resourceDef = builder.add(ir::Op::DclUav(info->type, m_converter.getEntryPoint(),
      info->regSpace, info->resourceIndex, info->resourceCount, info->kind, getUavFlags(op)));
  } else {
    info->resourceDef = builder.add(ir::Op::DclSrv(info->type, m_converter.getEntryPoint(),
      info->regSpace, info->resourceIndex, info->resourceCount, info->kind));
  }

  emitDebugName(builder, info);
  return true;
}


bool ResourceMap::handleDclResourceStructured(ir::Builder& builder, const Instruction& op) {
  /* dcl_resource_structured and dcl_uav_structured have the following operands:
   * (dst0) The resource or uav register to declare
   * (imm0) The structure size / stride in bytes
   * (imm1) The register space (SM5.1 only)
   */
  const auto& operand = op.getDst(0u);

  if (operand.getRegisterType() != RegisterType::eResource &&
      operand.getRegisterType() != RegisterType::eUav)
    return m_converter.logOpError(op, "Instruction does not declare a valid resource.");

  auto info = insertResourceInfo(op, operand);

  if (!info)
    return false;

  /* Emit actual resource declaration */
  auto structSize = op.getImm(0u).getImmediate<uint32_t>(0u);

  info->kind = ir::ResourceKind::eBufferStructured;
  info->type = ir::Type(ir::ScalarType::eUnknown)
    .addArrayDimension(structSize / sizeof(uint32_t))
    .addArrayDimension(0u);

  if (operand.getRegisterType() == RegisterType::eUav) {
    info->resourceDef = builder.add(ir::Op::DclUav(info->type, m_converter.getEntryPoint(),
      info->regSpace, info->resourceIndex, info->resourceCount, info->kind, getUavFlags(op)));
  } else {
    info->resourceDef = builder.add(ir::Op::DclSrv(info->type, m_converter.getEntryPoint(),
      info->regSpace, info->resourceIndex, info->resourceCount, info->kind));
  }

  emitDebugName(builder, info);
  return true;
}


bool ResourceMap::handleDclResourceTyped(ir::Builder& builder, const Instruction& op) {
  /* dcl_resource_typed takes the following operands:
   * (dst0) The resource or uav register to declare
   * (imm0) Resource return type token as a dedicated immediate.
   *        Declarations do not use the resource type field of the opcode.
   * (imm1) The register space (SM5.1 only)
   */
  const auto& operand = op.getDst(0u);

  if (operand.getRegisterType() != RegisterType::eResource &&
      operand.getRegisterType() != RegisterType::eUav)
    return m_converter.logOpError(op, "Instruction does not declare a valid resource.");

  auto info = insertResourceInfo(op, operand);

  if (!info)
    return false;

  auto resourceKind = resolveResourceDim(op.getOpToken().getResourceDim());

  if (!resourceKind)
    return m_converter.logOpError(op, "Invalid resource dimension: ", op.getOpToken().getResourceDim());

  /* Parse resource type. For typed resources, we declare a scalar type only */
  ResourceTypeToken returnType(op.getImm(0u).getImmediate<uint32_t>(0u));

  info->kind = *resourceKind;
  info->type = resolveSampledType(returnType.x());

  if (info->type == ir::ScalarType::eUnknown)
    return m_converter.logOpError(op, "Invalid resource return type: ", returnType.x());

  if (operand.getRegisterType() == RegisterType::eUav) {
    info->resourceDef = builder.add(ir::Op::DclUav(info->type, m_converter.getEntryPoint(),
      info->regSpace, info->resourceIndex, info->resourceCount, info->kind, getUavFlags(op)));
  } else {
    info->resourceDef = builder.add(ir::Op::DclSrv(info->type, m_converter.getEntryPoint(),
      info->regSpace, info->resourceIndex, info->resourceCount, info->kind));
  }

  emitDebugName(builder, info);
  return true;
}


bool ResourceMap::handleDclSampler(ir::Builder& builder, const Instruction& op) {
  /* dcl_sampler takes the following operands:
   * (dst0) The sampler register to declare.
   * (imm1) The register space (SM5.1 only)
   *
   * The opcode token itself declares the sampler type, but this has no
   * real relevance to us since depth-compare state is determined by the
   * opcode rather than the sampler or image types in question.
   */
  const auto& operand = op.getDst(0u);

  if (operand.getRegisterType() != RegisterType::eSampler)
    return m_converter.logOpError(op, "Instruction does not declare a valid sampler.");

  auto info = insertResourceInfo(op, operand);

  if (!info)
    return false;

  info->resourceDef = builder.add(ir::Op::DclSampler(m_converter.getEntryPoint(),
    info->regSpace, info->resourceIndex, info->resourceCount));

  emitDebugName(builder, info);
  return true;
}


ResourceProperties ResourceMap::emitDescriptorLoad(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand) {
  auto [descriptor, info] = loadDescriptor(builder, op, operand);

  if (!info)
    return ResourceProperties();

  ResourceProperties result = { };

  if (info->regType != RegisterType::eSampler) {
    result.kind = info->kind;

    if (result.kind != ir::ResourceKind::eBufferRaw &&
        result.kind != ir::ResourceKind::eBufferStructured)
      result.type = info->type.getBaseType(0u).getBaseType();
  }

  result.descriptor = descriptor;
  return result;
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


std::pair<ir::SsaDef, ir::SsaDef> ResourceMap::emitRawStructuredLoad(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand,
        ir::SsaDef              elementIndex,
        ir::SsaDef              elementOffset,
        WriteMask               componentMask,
        ir::ScalarType          scalarType) {
  auto [descriptor, resource] = loadDescriptor(builder, op, operand);

  if (!resource)
    return std::make_pair(ir::SsaDef(), ir::SsaDef());

  auto opCode = op.getOpToken().getOpCode();
  auto bufferType = resource->type.getBaseType(0u).getBaseType();

  /* If sparse feedback is enabled, load all requested components
   * to avoid having to merge multiple sparse feedback infos. */
  bool isSparse = (opCode == OpCode::eLdStructuredS || opCode == OpCode::eLdRawS) &&
    op.getDst(1u).getRegisterType() != RegisterType::eNull;
  auto readMask = operand.getSwizzle().getReadMask(componentMask);

  if (isSparse) {
    auto hiMask = 0x7fu >> util::lzcnt8(uint8_t(readMask));
    auto loMask = 0xffu << util::tzcnt(uint8_t(readMask));

    readMask = WriteMask(hiMask & loMask);
  }

  /* Emit vectorized loads like we do for constant buffers */
  std::array<ir::SsaDef, 4u> components = { };

  ir::SsaDef sparseFeedback = { };

  while (readMask) {
    /* Consecutive blocks of components to read */
    auto block = extractConsecutiveComponents(readMask);
    auto blockType = ir::BasicType(bufferType, util::popcnt(uint8_t(block)));

    dxbc_spv_assert(!isSparse || readMask == block);

    auto resultType = isSparse
      ? m_converter.makeSparseFeedbackType(blockType)
      : ir::Type(blockType);

    auto blockAlignment = computeRawStructuredAlignment(builder, *resource, elementOffset, block);

    auto address = resource->kind == ir::ResourceKind::eBufferStructured
      ? m_converter.computeStructuredAddress(builder, elementIndex, elementOffset, block)
      : m_converter.computeRawAddress(builder, elementIndex, block);

    /* Load buffer data and convert to desired result type */
    ir::SsaDef sparseFeedback = { };
    ir::SsaDef result = builder.add(ir::Op::BufferLoad(resultType, descriptor, address, blockAlignment));

    if (isSparse) {
      builder.setOpFlags(result, ir::OpFlag::eSparseFeedback);
      std::tie(sparseFeedback, result) = m_converter.decomposeResourceReturn(builder, result);
    }

    /* For regular loads, split the vector into scalars */
    for (uint32_t i = 0u; i < blockType.getVectorSize(); i++) {
      auto index = uint8_t(componentFromBit(block.first())) + i;
      components[index] = m_converter.extractFromVector(builder, result, i);

      if (scalarType != bufferType)
        components[index] = builder.add(ir::Op::ConsumeAs(scalarType, components[index]));
    }

    readMask -= block;
  }

  /* Build result vector */
  auto data = m_converter.composite(builder,
    m_converter.makeVectorType(scalarType, componentMask),
    components.data(), operand.getSwizzle(), componentMask);

  return std::make_pair(data, sparseFeedback);
}


bool ResourceMap::emitRawStructuredStore(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand,
        ir::SsaDef              elementIndex,
        ir::SsaDef              elementOffset,
        ir::SsaDef              data) {
  auto [descriptor, resource] = loadDescriptor(builder, op, operand);

  if (!resource)
    return false;

  /* Scalarize data vector and convert to resource type */
  auto bufferType = resource->type.getBaseType(0u).getBaseType();
  auto dataType = builder.getOp(data).getType().getBaseType(0u).getBaseType();

  auto writeMask = operand.getWriteMask();
  std::array<ir::SsaDef, 4u> components = { };

  uint32_t srcIndex = 0u;

  for (auto c : writeMask) {
    auto dstIndex = uint8_t(componentFromBit(c));
    components[dstIndex] = m_converter.extractFromVector(builder, data, srcIndex++);

    if (dataType != bufferType)
      components[dstIndex] = builder.add(ir::Op::ConsumeAs(bufferType, components[dstIndex]));
  }

  /* Walk over consecutive blocks, revectorize the components and store */
  while (writeMask) {
    auto block = extractConsecutiveComponents(writeMask);
    auto blockAlignment = computeRawStructuredAlignment(builder, *resource, elementOffset, block);

    auto blockVector = m_converter.composite(builder,
      m_converter.makeVectorType(bufferType, block),
      components.data(), Swizzle::identity(), block);

    auto address = resource->kind == ir::ResourceKind::eBufferStructured
      ? m_converter.computeStructuredAddress(builder, elementIndex, elementOffset, block)
      : m_converter.computeRawAddress(builder, elementIndex, block);

    builder.add(ir::Op::BufferStore(descriptor, address, blockVector, blockAlignment));

    writeMask -= block;
  }

  return true;
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


void ResourceMap::emitDebugName(ir::Builder& builder, const ResourceInfo* info) {
  if (m_converter.m_options.includeDebugNames) {
    auto name = m_converter.makeRegisterDebugName(info->regType, info->regIndex, WriteMask());
    builder.add(ir::Op::DebugName(info->resourceDef, name.c_str()));
  }
}


uint32_t ResourceMap::computeRawStructuredAlignment(
        ir::Builder&            builder,
  const ResourceInfo&           resource,
        ir::SsaDef              elementOffset,
        WriteMask               components) {
  /* Raw buffer, alignment can be just about anything */
  if (!elementOffset)
    return sizeof(uint32_t);

  /* Determine struct offset based on the address parameter.
   * If it is not constant, assume small alignment. */
  const auto& elementOp = builder.getOp(elementOffset);

  if (!elementOp.isConstant())
    return sizeof(uint32_t);

  uint32_t componentIndex = uint8_t(componentFromBit(components.first()));
  uint32_t structOffset = uint32_t(elementOp.getOperand(0u)) + componentIndex + sizeof(uint32_t);

  /* Compute alignment of the underlying structure and cap at 16 bytes,
   * which is the largest vector unit we can possibly load in one go */
  uint32_t structAlignment = resource.type.getSubType(0u).byteSize();
  structAlignment &= -structAlignment;
  structAlignment = std::min(structAlignment, 16u);

  /* Compute actual offset alignment */
  uint32_t offsetAlignment = structAlignment | structOffset;
  return offsetAlignment & -offsetAlignment;
}


ir::UavFlags ResourceMap::getUavFlags(
  const Instruction&            op) {
  auto flags = op.getOpToken().getUavFlags();

  ir::UavFlags result = 0u;

  if (flags & UavFlag::eGloballyCoherent)
    result |= ir::UavFlag::eCoherent;

  if (flags & UavFlag::eRasterizerOrdered)
    result |= ir::UavFlag::eRasterizerOrdered;

  return result;
}

}
