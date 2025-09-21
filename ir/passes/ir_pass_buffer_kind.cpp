#include "ir_pass_buffer_kind.h"

namespace dxbc_spv::ir {

ConvertBufferKindPass::ConvertBufferKindPass(Builder& builder, const Options& options)
: m_builder(builder), m_options(options) {

}


ConvertBufferKindPass::~ConvertBufferKindPass() {

}


void ConvertBufferKindPass::run() {
  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eDclSrv || iter->getOpCode() == OpCode::eDclUav) {
      if (shouldConvertToTypedBuffer(iter->getDef()))
        convertRawStructuredToTyped(iter->getDef());
      else if (shouldConvertToRawBuffer(iter->getDef()))
        convertTypedToRaw(iter->getDef());
      else if (iter->getOpCode() == OpCode::eDclUav)
        forceFormatForTypedUavLoad(iter->getDef());
    }
  }
}


void ConvertBufferKindPass::runPass(Builder& builder, const Options& options) {
  ConvertBufferKindPass(builder, options).run();
}


void ConvertBufferKindPass::forceFormatForTypedUavLoad(SsaDef def) {
  if (!m_options.forceFormatForTypedUavRead)
    return;

  /* Check whether the resource is a typed UAV */
  auto dclOp = m_builder.getOp(def);
  dxbc_spv_assert(dclOp.getOpCode() == OpCode::eDclUav);

  auto uavKind = ResourceKind(dclOp.getOperand(dclOp.getFirstLiteralOperandIndex() + 3u));

  if (!resourceIsTyped(uavKind))
    return;

  /* If the resource is already tagged or cannot be read, ignore */
  auto uavFlags = UavFlags(dclOp.getOperand(dclOp.getFirstLiteralOperandIndex() + 4u));

  if (uavFlags & (UavFlag::eWriteOnly | UavFlag::eFixedFormat))
    return;

  /* Check for any actual load instructions */
  bool hasLoad = false;

  forEachResourceUse(def, [&] (SsaDef use) {
    const auto& useOp = m_builder.getOp(use);

    hasLoad = hasLoad ||
      useOp.getOpCode() == OpCode::eBufferLoad ||
      useOp.getOpCode() == OpCode::eImageLoad;
  });

  if (!hasLoad)
    return;

  /* Rewrite declaration with adjusted UAV flags */
  dclOp.setOperand(dclOp.getFirstLiteralOperandIndex() + 4u, uavFlags | UavFlag::eFixedFormat);
  m_builder.rewriteOp(def, std::move(dclOp));
}


void ConvertBufferKindPass::convertTypedToRaw(SsaDef def) {
  /* Change the declaration and remove fixed-type UAV flag, since
   * that only ever applies to typed UAV. */
  auto dclOp = m_builder.getOp(def);
  dclOp.setType(Type(dclOp.getType()).addArrayDimension(0u));
  dclOp.setOperand(dclOp.getFirstLiteralOperandIndex() + 3u, ResourceKind::eBufferRaw);

  if (dclOp.getOpCode() == OpCode::eDclUav) {
    auto uavFlags = UavFlags(dclOp.getOperand(dclOp.getFirstLiteralOperandIndex() + 4u));
    dclOp.setOperand(dclOp.getFirstLiteralOperandIndex() + 4u, uavFlags - UavFlag::eFixedFormat);
  }

  m_builder.rewriteOp(def, std::move(dclOp));

  /* Fix up loads and stores to be scalar instead of vec4. For atomics, we don't
   * actually need to do anything since they use the correct return type already. */
  forEachResourceUse(def, [&] (SsaDef use) {
    const auto& useOp = m_builder.getOp(use);

    switch (useOp.getOpCode()) {
      case OpCode::eBufferLoad:
        convertTypedBufferLoadToRaw(use);
        break;

      case OpCode::eBufferStore:
        convertTypedBufferStoreToRaw(use);
        break;

      default:
        break;
    }
  });
}


void ConvertBufferKindPass::convertRawStructuredToTyped(SsaDef def) {
  /* Convert to u32, this should work for all possible access types */
  auto resourceType = m_builder.getOp(def).getType();

  auto dclOp = m_builder.getOp(def);
  dclOp.setType(ScalarType::eU32);
  dclOp.setOperand(dclOp.getFirstLiteralOperandIndex() + 3u, ResourceKind::eBufferTyped);

  if (dclOp.getOpCode() == OpCode::eDclUav) {
    auto uavFlags = UavFlags(dclOp.getOperand(dclOp.getFirstLiteralOperandIndex() + 4u));
    dclOp.setOperand(dclOp.getFirstLiteralOperandIndex() + 4u, uavFlags | UavFlag::eFixedFormat);
  }

  m_builder.rewriteOp(def, std::move(dclOp));

  /* Fix up loads, stores and atomics. */
  forEachResourceUse(def, [&] (SsaDef use) {
    const auto& useOp = m_builder.getOp(use);

    switch (useOp.getOpCode()) {
      case OpCode::eBufferLoad:
        convertRawStructuredBufferLoadToTyped(use, resourceType);
        break;

      case OpCode::eBufferStore:
        convertRawStructuredBufferStoreToTyped(use, resourceType);
        break;

      case OpCode::eBufferAtomic:
        convertRawStructuredBufferAtomicToTyped(use, resourceType);
        break;

      case OpCode::eBufferQuerySize:
        convertRawStructuredBufferQueryToTyped(use, resourceType);
        break;

      default:
        break;
    }
  });
}


void ConvertBufferKindPass::convertRawStructuredBufferLoadToTyped(SsaDef use, const Type& resourceType) {
  const auto& loadOp = m_builder.getOp(use);

  util::small_vector<SsaDef, 4u> sparseFeedbacks;
  util::small_vector<SsaDef, 4u> scalars;

  /* Scalarize vector load and extract X component */
  bool hasSparseFeedback = bool(loadOp.getFlags() & OpFlag::eSparseFeedback);

  auto baseAddress = flattenAddress(resourceType, use, SsaDef(loadOp.getOperand(1u)));

  auto valueType = loadOp.getType().getBaseType(hasSparseFeedback ? 1u : 0u);
  auto vectorType = BasicType(ScalarType::eU32, 4u);

  auto loadType = hasSparseFeedback
    ? Type().addStructMember(ScalarType::eU32).addStructMember(vectorType)
    : Type().addStructMember(vectorType);

  for (uint32_t i = 0u; i < valueType.getVectorSize(); i++) {
    auto address = m_builder.addBefore(use, Op::IAdd(ScalarType::eU32, baseAddress, m_builder.makeConstant(i)));

    auto value = m_builder.addBefore(use, Op::BufferLoad(
      loadType, SsaDef(loadOp.getOperand(0u)), address, 0u).setFlags(loadOp.getFlags()));

    if (hasSparseFeedback) {
      sparseFeedbacks.push_back(m_builder.addBefore(use,
        Op::CompositeExtract(ScalarType::eU32, value, m_builder.makeConstant(0u))));
    }

    auto scalar = m_builder.addBefore(use, Op::CompositeExtract(ScalarType::eU32, value,
      hasSparseFeedback ? m_builder.makeConstant(1u, 0u) : m_builder.makeConstant(0u)));
    scalar = m_builder.addBefore(use, Op::ConsumeAs(valueType.getBaseType(), scalar));
    scalars.push_back(scalar);
  }

  /* Build vector from scalars as necessary */
  SsaDef result = scalars.at(0u);

  if (scalars.size() > 1u) {
    Op compositeOp(OpCode::eCompositeConstruct, Type(valueType.getBaseType(), scalars.size()));

    for (auto s : scalars)
      compositeOp.addOperand(s);

    result = m_builder.addBefore(use, std::move(compositeOp));
  }

  /* Resolve sparse feedback by checking all feedback values if there are
   * multiple, and use a non-resident return code if one of the loads failed. */
  if (hasSparseFeedback) {
    SsaDef sparseFeedback = sparseFeedbacks.at(0u);

    for (uint32_t i = 1u; i < sparseFeedbacks.size(); i++) {
      sparseFeedback = m_builder.addBefore(use, Op::Select(ScalarType::eU32,
        m_builder.addBefore(use, Op::CheckSparseAccess(sparseFeedbacks.at(i))),
        sparseFeedback, sparseFeedbacks.at(i)));
    }

    result = m_builder.addBefore(use, Op::CompositeConstruct(loadOp.getType(), sparseFeedback, result));
  }

  /* Replace load with assembled result */
  m_builder.rewriteDef(use, result);
}


void ConvertBufferKindPass::convertRawStructuredBufferStoreToTyped(SsaDef use, const Type& resourceType) {
  /* Need to scalarize stores and wrap the value into a vec4. */
  const auto& storeOp = m_builder.getOp(use);
  const auto& valueOp = m_builder.getOpForOperand(storeOp, 2u);

  auto baseAddress = flattenAddress(resourceType, use, SsaDef(storeOp.getOperand(1u)));
  auto valueType = valueOp.getType().getBaseType(0u);
  auto vectorType = BasicType(ScalarType::eU32, 4u);

  auto zero = m_builder.makeConstant(0u);

  for (uint32_t i = 0u; i < valueType.getVectorSize(); i++) {
    auto address = m_builder.addBefore(use, Op::IAdd(ScalarType::eU32, baseAddress, m_builder.makeConstant(i)));

    auto value = extractFromVector(use, valueOp.getDef(), i);
    value = m_builder.addBefore(use, Op::ConsumeAs(ScalarType::eU32, value));
    value = m_builder.addBefore(use, Op::CompositeConstruct(vectorType, value, zero, zero, zero));

    m_builder.addBefore(use, Op::BufferStore(SsaDef(storeOp.getOperand(0u)), address, value, 0u));
  }

  m_builder.remove(use);
}


void ConvertBufferKindPass::convertRawStructuredBufferAtomicToTyped(SsaDef use, const Type& resourceType) {
  /* All we have to do here is flatten the address, everything else should already be correct*/
  auto atomicOp = m_builder.getOp(use);
  auto address = flattenAddress(resourceType, use, SsaDef(atomicOp.getOperand(1u)));

  atomicOp.setOperand(1u, address);
  m_builder.rewriteOp(use, std::move(atomicOp));
}


void ConvertBufferKindPass::convertRawStructuredBufferQueryToTyped(SsaDef use, const Type& resourceType) {
  /* For structured buffers, compute appropriate struct count. Both
   * typed and raw buffers use element count semantics here already,
   * so no special care is needed. */
  uint32_t dwordsPerStructure = resourceType.getSubType(0u).byteSize() / sizeof(uint32_t);

  if (dwordsPerStructure != 1u) {
    auto queryDef = m_builder.addBefore(use, m_builder.getOp(use));
    m_builder.rewriteOp(use, Op::UDiv(ScalarType::eU32, queryDef, m_builder.makeConstant(dwordsPerStructure)));
  }
}


void ConvertBufferKindPass::convertTypedBufferLoadToRaw(SsaDef use) {
  auto loadOp = m_builder.getOp(use);

  auto vectorType = loadOp.getType();
  dxbc_spv_assert(vectorType.isBasicType());

  loadOp.setType(vectorType.getSubType(0u));
  loadOp.setOperand(2u, uint32_t(sizeof(uint32_t)));

  auto loadDef = m_builder.addBefore(use, std::move(loadOp));

  /* Build vector with appropriate zero constants */
  auto zeroDef = m_builder.add(Op(OpCode::eConstant, vectorType.getSubType(0u)).addOperand(Operand()));

  Op compositeOp(OpCode::eCompositeConstruct, vectorType);
  compositeOp.addOperand(loadDef);

  for (uint32_t i = 1u; i < vectorType.getBaseType(0u).getVectorSize(); i++)
    compositeOp.addOperand(zeroDef);

  m_builder.rewriteOp(use, std::move(compositeOp));
}


void ConvertBufferKindPass::convertTypedBufferStoreToRaw(SsaDef use) {
  auto storeOp = m_builder.getOp(use);

  const auto& value = m_builder.getOpForOperand(use, 2u);

  /* Extract X component of typed vector */
  auto valueDef = m_builder.addBefore(use, Op::CompositeExtract(
    value.getType().getSubType(0u), value.getDef(), m_builder.makeConstant(0u)));

  storeOp.setOperand(2u, valueDef);
  storeOp.setOperand(3u, uint32_t(sizeof(uint32_t)));

  m_builder.rewriteOp(use, std::move(storeOp));
}


SsaDef ConvertBufferKindPass::flattenAddress(Type type, SsaDef ref, SsaDef address) const {
  auto addressType = m_builder.getOp(address).getType().getBaseType(0u);

  SsaDef flattenedAddress = { };

  for (uint32_t i = 0u; i < addressType.getVectorSize(); i++) {
    SsaDef dwordIndex = { };

    if (type.isArrayType()) {
      type = type.getSubType(0u);

      auto dwordCount = type.byteSize() / sizeof(uint32_t);
      dwordIndex = extractFromVector(ref, address, i);

      if (dwordCount > 1u) {
        dwordIndex = m_builder.addBefore(ref, Op::IMul(addressType.getBaseType(),
          dwordIndex, m_builder.makeConstant(uint32_t(dwordCount))));
      }
    } else {
      auto index = getConstantAddress(address, i);
      dwordIndex = m_builder.makeConstant(uint32_t(type.byteOffset(index) / sizeof(uint32_t)));

      type = type.getSubType(index);
    }

    flattenedAddress = flattenedAddress
      ? m_builder.addBefore(ref, Op::IAdd(addressType.getBaseType(), flattenedAddress, dwordIndex))
      : dwordIndex;
  }

  return flattenedAddress;
}


template<typename Fn>
void ConvertBufferKindPass::forEachResourceUse(SsaDef def, const Fn& fn) {
  auto dclUses = m_builder.getUses(def);

  for (auto d = dclUses.first; d != dclUses.second; d++) {
    if (d->getOpCode() != OpCode::eDescriptorLoad)
      continue;

    util::small_vector<SsaDef, 256u> descriptorUses;
    m_builder.getUses(d->getDef(), descriptorUses);

    for (auto use : descriptorUses)
      fn(use);
  }
}


bool ConvertBufferKindPass::shouldConvertToTypedBuffer(SsaDef def) const {
  const auto& dcl = m_builder.getOp(def);

  auto kind = ResourceKind(dcl.getOperand(dcl.getFirstLiteralOperandIndex() + 3u));

  if (kind != ResourceKind::eBufferRaw && kind != ResourceKind::eBufferStructured)
    return false;

  /* Convert raw buffers if explicitly requested */
  if (kind == ResourceKind::eBufferRaw && m_options.useTypedForRaw)
    return true;

  /* Convert structured buffers with an unaligned struct size */
  if (kind == ResourceKind::eBufferStructured && m_options.useTypedForStructured &&
      (!m_options.minStructureAlignment || (dcl.getType().byteSize() % m_options.minStructureAlignment)))
    return true;

  /* Convert buffers used with sparse feedback loads to typed. */
  if (m_options.useTypedForSparseFeedback) {
    auto dclUses = m_builder.getUses(def);

    for (auto d = dclUses.first; d != dclUses.second; d++) {
      if (d->getOpCode() != OpCode::eDescriptorLoad)
        continue;

      auto descriptorUses = m_builder.getUses(d->getDef());

      for (auto use = descriptorUses.first; use != descriptorUses.second; use++) {
        if (use->getFlags() & OpFlag::eSparseFeedback)
          return true;
      }
    }
  }

  return false;
}


bool ConvertBufferKindPass::shouldConvertToRawBuffer(SsaDef def) const {
  const auto& dcl = m_builder.getOp(def);

  if (dcl.getOpCode() != OpCode::eDclUav || !m_options.useRawForTypedAtomic)
    return false;

  /* Only promote non-arrayed resources since buffers may be accessed in different ways */
  auto count = uint32_t(dcl.getOperand(dcl.getFirstLiteralOperandIndex() + 2u));
  auto kind = ResourceKind(dcl.getOperand(dcl.getFirstLiteralOperandIndex() + 3u));
  auto flags = UavFlags(dcl.getOperand(dcl.getFirstLiteralOperandIndex() + 4u));

  if (kind != ResourceKind::eBufferTyped || count != 1u || !(flags & UavFlag::eFixedFormat))
    return false;

  /* Don't bother supporting sparse feedback here for now */
  return !resourceHasSparseFeedbackLoads(def);
}


bool ConvertBufferKindPass::resourceHasSparseFeedbackLoads(SsaDef def) const {
  auto dclUses = m_builder.getUses(def);

  for (auto d = dclUses.first; d != dclUses.second; d++) {
    if (d->getOpCode() != OpCode::eDescriptorLoad)
      continue;

    auto descriptorUses = m_builder.getUses(d->getDef());

    for (auto use = descriptorUses.first; use != descriptorUses.second; use++) {
      if (use->getFlags() & OpFlag::eSparseFeedback)
        return true;
    }
  }

  return false;
}


uint32_t ConvertBufferKindPass::getConstantAddress(SsaDef address, uint32_t component) const {
  const auto& op = m_builder.getOp(address);

  if (op.isConstant())
    return uint32_t(op.getOperand(component));

  if (op.getOpCode() == OpCode::eCompositeConstruct)
    return getConstantAddress(SsaDef(op.getOperand(component)), 0u);

  dxbc_spv_unreachable();
  return 0u;
}


SsaDef ConvertBufferKindPass::extractFromVector(SsaDef ref, SsaDef vector, uint32_t component) const {
  const auto& op = m_builder.getOp(vector);

  if (op.getType().isScalarType())
    return vector;

  if (op.isConstant()) {
    return m_builder.add(Op(OpCode::eConstant, op.getType().getSubType(0u))
      .addOperand(op.getOperand(component)));
  }

  if (op.getOpCode() == OpCode::eCompositeConstruct)
    return SsaDef(op.getOperand(component));

  return m_builder.addBefore(ref, Op::CompositeExtract(op.getType().getSubType(0u),
    vector, m_builder.makeConstant(component)));
}

}
