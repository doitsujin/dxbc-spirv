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
    arraySize = MaxCbvSize;

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
      info->regSpace, info->resourceIndex, info->resourceCount, info->kind, getInitialUavFlags(op)));
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
      info->regSpace, info->resourceIndex, info->resourceCount, info->kind, getInitialUavFlags(op)));
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
      info->regSpace, info->resourceIndex, info->resourceCount, info->kind, getInitialUavFlags(op)));
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


void ResourceMap::setUavFlagsForLoad(ir::Builder& builder, const Instruction& op, const Operand& operand) {
  auto resource = getResourceInfo(builder, op, operand);

  dxbc_spv_assert(resource && resource->regType == RegisterType::eUav);

  auto flags = getUavFlags(builder, *resource);
  flags -= ir::UavFlag::eWriteOnly;

  setUavFlags(builder, *resource, flags);
}


void ResourceMap::setUavFlagsForStore(ir::Builder& builder, const Instruction& op, const Operand& operand) {
  auto resource = getResourceInfo(builder, op, operand);

  dxbc_spv_assert(resource && resource->regType == RegisterType::eUav);

  auto flags = getUavFlags(builder, *resource);
  flags -= ir::UavFlag::eReadOnly;

  setUavFlags(builder, *resource, flags);
}


void ResourceMap::setUavFlagsForAtomic(ir::Builder& builder, const Instruction& op, const Operand& operand) {
  auto resource = getResourceInfo(builder, op, operand);

  dxbc_spv_assert(resource && resource->regType == RegisterType::eUav);

  auto flags = getUavFlags(builder, *resource);
  flags -= ir::UavFlag::eReadOnly | ir::UavFlag::eWriteOnly;

  if (ir::resourceIsTyped(resource->kind))
    flags |= ir::UavFlag::eFixedFormat;

  setUavFlags(builder, *resource, flags);
}


void ResourceMap::normalizeUavFlags(ir::Builder& builder) {
  /* In some cases, a UAV might only be used inside resource queries, and we
   * would set both ReadOnly and WriteOnly. This does not make any sense, so
   * treat such a UAV as read-only. */
  for (const auto& e : m_resources) {
    if (e.regType == RegisterType::eUav) {
      auto flags = getUavFlags(builder, e);

      if ((flags & ir::UavFlag::eReadOnly) && (flags & ir::UavFlag::eWriteOnly))
        setUavFlags(builder, e, flags - ir::UavFlag::eWriteOnly);
    }
  }
}


ResourceProperties ResourceMap::emitDescriptorLoad(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand) {
  auto [descriptor, info] = loadDescriptor(builder, op, operand, false);

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


ir::SsaDef ResourceMap::emitUavCounterDescriptorLoad(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand) {
  auto [descriptor, info] = loadDescriptor(builder, op, operand, true);

  if (!info)
    return ir::SsaDef();

  return descriptor;
}


ir::SsaDef ResourceMap::emitConstantBufferLoad(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand,
        WriteMask               componentMask,
        ir::ScalarType          scalarType) {
  auto [descriptor, resource] = loadDescriptor(builder, op, operand, false);

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
  auto [descriptor, resource] = loadDescriptor(builder, op, operand, false);

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
    auto hiMask = 0xffu >> util::lzcnt8(uint8_t(readMask));
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
  auto [descriptor, resource] = loadDescriptor(builder, op, operand, false);

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
  const Operand&                operand,
        bool                    uavCounter) {
  auto resourceInfo = getResourceInfo(builder, op, operand);

  if (!resourceInfo)
    return std::make_pair(ir::SsaDef(), resourceInfo);

  auto descriptorType = [resourceInfo, uavCounter] {
    switch (resourceInfo->regType) {
      case RegisterType::eSampler:
        return ir::ScalarType::eSampler;

      case RegisterType::eCbv:
        return ir::ScalarType::eCbv;

      case RegisterType::eResource:
        return ir::ScalarType::eSrv;

      case RegisterType::eUav:
        return uavCounter ? ir::ScalarType::eUavCounter : ir::ScalarType::eUav;

      default:
        break;
    }

    dxbc_spv_unreachable();
    return ir::ScalarType::eVoid;
  } ();

  ir::SsaDef descriptorIndex = { };

  if (m_converter.isSm51()) {
    /* Second index contains the actual index, but as an absolute register
     * index. Deal with it the same way we do for I/O registers and split
     * the index into its absolute and relative parts. */
    descriptorIndex = m_converter.loadOperandIndex(builder, op, operand, 1u);
    descriptorIndex = builder.add(ir::Op::ISub(ir::ScalarType::eU32,
        descriptorIndex, builder.makeConstant(resourceInfo->resourceIndex)));
  } else {
    /* First index is the resource index. In most cases these are going to
     * be constant, but shaders may also use uniform indexing together with
     * class linkage. */
    if (resourceInfo->resourceCount > 1u) {
      descriptorIndex = m_converter.loadOperandIndex(builder, op, operand, 0u);
      descriptorIndex = builder.add(ir::Op::ISub(ir::ScalarType::eU32,
        descriptorIndex, builder.makeConstant(resourceInfo->resourceIndex)));
    } else {
      descriptorIndex = builder.makeConstant(0u);
    }
  }

  /* Retrieve resource definition. If necessary, declare the UAV counter. */
  auto baseDef = resourceInfo->resourceDef;

  if (uavCounter)
    baseDef = declareUavCounter(builder, *resourceInfo);

  auto def = builder.add(ir::Op::DescriptorLoad(descriptorType, baseDef, descriptorIndex));

  /* Apply non-uniform modifier to the descriptor load */
  if (operand.getModifiers().isNonUniform())
    builder.setOpFlags(def, ir::OpFlag::eNonUniform);

  return std::make_pair(def, resourceInfo);
}


ResourceInfo* ResourceMap::insertResourceInfo(
  const Instruction&            op,
  const Operand&                operand) {
  auto& info = m_resources.emplace_back();
  info.regType = operand.getRegisterType();
  info.regIndex = operand.getIndex(0u);

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


ir::SsaDef ResourceMap::declareUavCounter(
        ir::Builder&            builder,
        ResourceInfo&           resource) {
  if (resource.counterDef)
    return resource.counterDef;

  resource.counterDef = builder.add(ir::Op::DclUavCounter(m_converter.getEntryPoint(), resource.resourceDef));

  if (m_converter.m_options.includeDebugNames) {
    auto name = m_converter.makeRegisterDebugName(resource.regType, resource.regIndex, WriteMask()) + "_ctr";
    builder.add(ir::Op::DebugName(resource.counterDef, name.c_str()));
  }

  return resource.counterDef;
}


void ResourceMap::emitDebugName(ir::Builder& builder, const ResourceInfo* info) {
  if (m_converter.m_options.includeDebugNames) {
    auto name = m_converter.makeRegisterDebugName(info->regType, info->regIndex, WriteMask());
    builder.add(ir::Op::DebugName(info->resourceDef, name.c_str()));
  }
}


bool ResourceMap::matchesResource(
  const Instruction&            op,
  const Operand&                operand,
  const ResourceInfo&           info) const {
  if (info.regType != operand.getRegisterType())
    return false;

  if (!hasRelativeIndexing(operand.getIndexType(0u)) && info.regIndex == operand.getIndex(0u))
    return true;

  /* For SM5.1, the first index is the variable index, which must match */
  if (m_converter.isSm51())
    return false;

  /* For SM5.0 dynamic indexing, check if the entry is a descriptor array
   * covering the entire register space with matching properties. */
  if (info.resourceIndex || info.resourceCount == 1u)
    return false;

  switch (info.regType) {
    case RegisterType::eSampler:
    case RegisterType::eCbv:
      return true;

    case RegisterType::eResource:
    case RegisterType::eUav: {
      auto resourceType = getResourceTypeForToken(op.getOpToken());

      if (!resourceType) {
        m_converter.logOpError(op, "No resource type tokens found");
        return false;
      }

      return info.kind == resourceType->first &&
             info.type == resourceType->second;
    };

    default:
      return false;
  }
}


void ResourceMap::rewriteSm50ResourceAccess(
        ir::Builder&            builder,
  const ResourceInfo&           oldResource,
  const ResourceInfo&           newResource) {
  /* Rewrite descriptor loads to use the newly declared array */
  util::small_vector<ir::SsaDef, 256u> uses;

  if (oldResource.resourceDef)
    builder.getUses(oldResource.resourceDef, uses);

  if (oldResource.counterDef)
    builder.getUses(oldResource.counterDef, uses);

  for (auto use : uses) {
    auto op = builder.getOp(use);

    if (op.getOpCode() == ir::OpCode::eDescriptorLoad) {
      auto resource = builder.getOpForOperand(op, 0u).getDef();

      resource = (resource == oldResource.counterDef)
        ? newResource.counterDef
        : newResource.resourceDef;

      auto index = builder.getOpForOperand(op, 1u).getDef();
      index = builder.addBefore(use, ir::Op::IAdd(ir::ScalarType::eU32,
        index, builder.makeConstant(oldResource.resourceIndex - newResource.resourceIndex)));

      op.setOperand(0u, resource);
      op.setOperand(1u, index);

      builder.rewriteOp(use, std::move(op));
    }
  }

  /* Adjust UAV flags */
  if (newResource.regType == RegisterType::eUav) {
    auto andFlags = ir::UavFlag::eReadOnly | ir::UavFlag::eWriteOnly;

    auto oldFlags = getUavFlags(builder, oldResource);
    auto newFlags = getUavFlags(builder, newResource);

    auto mergedFlags = oldFlags | newFlags;
    mergedFlags -= andFlags;
    mergedFlags |= andFlags & oldFlags & newFlags;

    setUavFlags(builder, newResource, mergedFlags);
  }
}


ResourceInfo* ResourceMap::declareSm50ResourceArray(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand) {
  auto resourceType = getResourceTypeForToken(op.getOpToken());

  ResourceInfo info = { };
  info.regType = operand.getRegisterType();
  info.resourceCount = determineSm50ResourceArraySize(info.regType);

  switch (info.regType) {
    case RegisterType::eSampler: {
      info.resourceDef = builder.add(ir::Op::DclSampler(
        m_converter.getEntryPoint(), 0u, 0u, info.resourceCount));

      builder.add(ir::Op::DebugName(info.resourceDef, "s"));
    } break;

    case RegisterType::eCbv: {
      info.kind = ir::ResourceKind::eBufferStructured;
      info.type = ir::Type(ir::ScalarType::eUnknown, 4u).addArrayDimension(MaxCbvSize);
      info.resourceDef = builder.add(ir::Op::DclCbv(info.type,
        m_converter.getEntryPoint(), 0u, 0u, info.resourceCount));

      builder.add(ir::Op::DebugName(info.resourceDef, "cb"));
    } break;

    case RegisterType::eResource: {
      if (!resourceType) {
        m_converter.logOpError(op, "No resource type tokens found");
        return nullptr;
      }

      info.kind = resourceType->first;
      info.type = resourceType->second;
      info.resourceDef = builder.add(ir::Op::DclSrv(info.type,
        m_converter.getEntryPoint(), 0u, 0u, info.resourceCount, info.kind));

      builder.add(ir::Op::DebugName(info.resourceDef, "t"));
    } break;

    case RegisterType::eUav: {
      if (!resourceType) {
        m_converter.logOpError(op, "No resource type tokens found");
        return nullptr;
      }

      info.kind = resourceType->first;
      info.type = resourceType->second;
      info.resourceDef = builder.add(ir::Op::DclUav(info.type,
        m_converter.getEntryPoint(), 0u, 0u, info.resourceCount, info.kind,
        ir::UavFlag::eWriteOnly | ir::UavFlag::eReadOnly));

      if (!ir::resourceIsTyped(info.kind)) {
        info.counterDef = builder.add(ir::Op::DclUavCounter(
          m_converter.getEntryPoint(), info.resourceDef));
        builder.add(ir::Op::DebugName(info.resourceDef, "u_ctr"));
      }

      builder.add(ir::Op::DebugName(info.resourceDef, "u"));
    } break;

    default:
      return nullptr;
  }

  /* For constant buffers, we only care about the element type.
   * We declare all of them as arrays, so this is safe to do. */
  bool isCbv = info.regType == RegisterType::eCbv;
  auto dstType = isCbv ? info.type.getSubType(0u) : info.type;

  for (size_t i = 0u; i < m_resources.size(); ) {
    auto& e = m_resources.at(i);
    auto srcType = isCbv ? info.type.getSubType(0u) : info.type;

    if (e.regType == info.regType &&
        e.regSpace == info.regSpace &&
        e.resourceIndex + e.resourceCount > info.resourceIndex &&
        e.resourceIndex < info.resourceIndex + info.resourceCount &&
        e.kind == info.kind && srcType == dstType) {
      rewriteSm50ResourceAccess(builder, e, info);

      e = m_resources.back();
      m_resources.pop_back();
    } else {
      i++;
    }
  }

  return &m_resources.emplace_back(info);
}


ResourceInfo* ResourceMap::getResourceInfo(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand) {
  for (auto& e : m_resources) {
    if (matchesResource(op, operand, e))
      return &e;
  }

  /* For SM5.0, we only expect to encounter undeclared resources in
   * combination with class linkage dynamic indexing, so declare a
   * resource array and merge any overlapping bindings into it. */
  if (!m_converter.isSm51()) {
    auto info = declareSm50ResourceArray(builder, op, operand);

    if (info)
      return info;
  }

  auto name = m_converter.makeRegisterDebugName(operand.getRegisterType(), operand.getIndex(0u), WriteMask());
  Logger::err("Resource ", name, " not declared.");
  return nullptr;
}


uint32_t ResourceMap::determineSm50ResourceArraySize(
        RegisterType            type) {
  switch (type) {
    case RegisterType::eCbv:
      return Sm50CbvCount;
    case RegisterType::eSampler:
      return Sm50SamplerCount;
    case RegisterType::eResource:
      return Sm50SrvCount;
    case RegisterType::eUav:
      return Sm50UavCount;
    default:
      return 0u;
  }
}


ir::UavFlags ResourceMap::getUavFlags(
        ir::Builder&            builder,
  const ResourceInfo&           info) {
  const auto& op = builder.getOp(info.resourceDef);
  return ir::UavFlags(op.getOperand(5u));
}


void ResourceMap::setUavFlags(
        ir::Builder&            builder,
  const ResourceInfo&           info,
        ir::UavFlags            flags) {
  auto op = builder.getOp(info.resourceDef);
  op.setOperand(5u, flags);

  builder.rewriteOp(info.resourceDef, std::move(op));
}


std::optional<std::pair<ir::ResourceKind, ir::Type>> ResourceMap::getResourceTypeForToken(
  const OpToken&                tok) {
  auto resourceDim = tok.getResourceDimToken();

  if (!resourceDim)
    return std::nullopt;

  auto kind = resolveResourceDim(resourceDim.getDim());

  if (!kind)
    return std::nullopt;

  ir::Type type = { };

  switch (*kind) {
    case ir::ResourceKind::eBufferRaw: {
      type = ir::Type(ir::ScalarType::eUnknown).addArrayDimension(0u);
    } break;

    case ir::ResourceKind::eBufferStructured: {
      type = ir::Type(ir::ScalarType::eUnknown)
        .addArrayDimension(resourceDim.getStructureStride() / sizeof(uint32_t))
        .addArrayDimension(0u);
    } break;

    default: {
      auto resourceType = tok.getResourceTypeToken();

      if (!resourceType)
        return std::nullopt;

      type = resolveSampledType(resourceType.x());
    } break;
  }

  return std::make_pair(*kind, std::move(type));
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
  uint32_t structOffset = uint32_t(elementOp.getOperand(0u)) + componentIndex * sizeof(uint32_t);

  /* Compute alignment of the underlying structure and cap at 16 bytes,
   * which is the largest vector unit we can possibly load in one go */
  uint32_t structAlignment = resource.type.getSubType(0u).byteSize();
  structAlignment &= -structAlignment;
  structAlignment = std::min(structAlignment, 16u);

  /* Compute actual offset alignment */
  uint32_t offsetAlignment = structAlignment | structOffset;
  return offsetAlignment & -offsetAlignment;
}


ir::UavFlags ResourceMap::getInitialUavFlags(
  const Instruction&            op) {
  auto flags = op.getOpToken().getUavFlags();

  /* We'll delete the read-only and write-only flags
   * based on actual resource usage afterwards */
  ir::UavFlags result = ir::UavFlag::eReadOnly |
                        ir::UavFlag::eWriteOnly;

  if (flags & UavFlag::eGloballyCoherent)
    result |= ir::UavFlag::eCoherent;

  if (flags & UavFlag::eRasterizerOrdered)
    result |= ir::UavFlag::eRasterizerOrdered;

  return result;
}

}
