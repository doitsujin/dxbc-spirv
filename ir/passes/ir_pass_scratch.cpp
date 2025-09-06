#include "ir_pass_scratch.h"

#include "../../util/util_log.h"

namespace dxbc_spv::ir {

CleanupScratchPass::CleanupScratchPass(Builder& builder, const Options& options)
: m_builder(builder), m_options(options) {

}


CleanupScratchPass::~CleanupScratchPass() {

}


void CleanupScratchPass::propagateCbvScratchCopies() {
  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eDclScratch)
      promoteScratchCbvCopy(iter->getDef());
  }
}


void CleanupScratchPass::runResolveCbvToScratchCopyPass(Builder& builder, const Options& options) {
  CleanupScratchPass(builder, options).propagateCbvScratchCopies();
}


bool CleanupScratchPass::promoteScratchCbvCopy(SsaDef def) {
  constexpr uint32_t MaxSparseSize = 32u;

  if (!m_options.resolveCbvCopy)
    return false;

  /* Scratch type must be a flat array of scalars or vectors */
  auto type = m_builder.getOp(def).getType();

  if (!type.isArrayType() || !type.getSubType(0u).isBasicType())
    return false;

  auto baseType = type.getBaseType(0u);
  auto componentCount = baseType.getVectorSize();

  /* Mask of array indices written */
  uint32_t storeMask = 0u;

  /* Gather info about scratch stores. The stored value must be a buffer load
   * from a constant buffer with incrementing indices and matching components,
   * so that we can directly promote scratch loads to constant buffer loads. */
  util::small_vector<CbvInfo, 256> storeInfos;
  auto [a, b] = m_builder.getUses(def);

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eScratchStore) {
      /* Exit early if any store does not match our pattern */
      const auto& index = m_builder.getOpForOperand(*iter, 1u);
      const auto& value = m_builder.getOpForOperand(*iter, 2u);

      if (!index.isConstant())
        return false;

      uint32_t arrayIndex = uint32_t(index.getOperand(0u));

      if (arrayIndex < MaxSparseSize)
        storeMask |= 1u << arrayIndex;

      uint32_t componentIndex = 0u;

      if (index.getOperandCount() > 1u)
        componentIndex = uint32_t(index.getOperand(1u));

      dxbc_spv_assert(componentIndex < componentCount);
      auto storeIndex = arrayIndex * componentCount + componentIndex;

      if (storeIndex >= storeInfos.size())
        storeInfos.resize(storeIndex + 1u);

      auto& storeInfo = storeInfos.at(storeIndex);

      /* Multiple stores to the same location? */
      if (storeInfo.cbv)
        return false;

      /* Figure out CBV info for the current store. If we can't
       * map it to a CBV access at all, exit. */
      storeInfo = getCbvCopyMapping(value);

      if (!storeInfo.cbv)
        return false;
    }
  }

  if (storeInfos.empty())
    return false;

  /* Ensure that we can express scratch loads as buffer loads
   * by simply offsetting the index and component */
  if (storeInfos.size() % componentCount)
    return false;

  CbvInfo baseStore = { };
  uint32_t baseIndex = 0u;

  if (storeMask)
    baseIndex = util::tzcnt(storeMask);

  if (baseIndex >= storeInfos.size())
    return false;

  baseStore = storeInfos.at(baseIndex);

  for (uint32_t i = 0u; i < storeInfos.size() / componentCount; i++) {
    if (storeMask && !(storeMask & (1u << i)))
      continue;

    for (uint32_t j = 0u; j < componentCount; j++) {
      const auto& currStore = storeInfos.at(j + componentCount * i);

      if (currStore.cbv != baseStore.cbv ||
          currStore.cbvIndex != baseStore.cbvIndex ||
          currStore.cbvComponent != baseStore.cbvComponent + j)
        return false;

      if (!hasConstantIndexOffset(baseStore.cbvOffset, currStore.cbvOffset, i - baseIndex))
        return false;
    }
  }

  /* Now that we verified that we can map each scratch store to
   * a cbv load, verify that all stores dominate all loads */
  DominanceGraph dominance(m_builder);

  for (auto store = a; store != b; store++) {
    if (store->getOpCode() != OpCode::eScratchStore)
      continue;

    for (auto load = a; load != b; load++) {
      if (!dominance.dominates(store->getDef(), load->getDef()))
        return false;
    }
  }

  auto function = emitScratchCbvFunction(def, baseStore, baseIndex, storeMask);

  /* Replace all loads with a call to the helper function,
   * and extract the correct vector component as necessary. */
  util::small_vector<SsaDef, 256u> loads;
  util::small_vector<SsaDef, 256u> stores;

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eScratchLoad)
      loads.push_back(iter->getDef());
    else if (iter->getOpCode() == OpCode::eScratchStore)
      stores.push_back(iter->getDef());
  }

  for (auto load : loads) {
    const auto& loadOp = m_builder.getOp(load);
    const auto& indexOp = m_builder.getOpForOperand(loadOp, 1u);

    auto indexDef = indexOp.getDef();
    auto componentDef = SsaDef();

    if (indexOp.getType().isVectorType()) {
      indexDef = m_builder.addBefore(load, Op::CompositeExtract(ScalarType::eU32, indexOp.getDef(), m_builder.makeConstant(0u)));
      componentDef = m_builder.addBefore(load, Op::CompositeExtract(ScalarType::eU32, indexOp.getDef(), m_builder.makeConstant(1u)));
    }

    auto callType = m_builder.getOp(function).getType().getBaseType(0u);
    auto helperOp = Op::FunctionCall(callType, function).addParam(baseStore.cbvOffset).addParam(indexDef);

    /* Might need to extract a scalar component? */
    if (componentDef)
      helperOp = Op::CompositeExtract(callType.getBaseType(), m_builder.addBefore(load, std::move(helperOp)), componentDef);

    /* Convert to correct type as necessary */
    if (loadOp.getType() == helperOp.getType()) {
      if (helperOp.getType().isScalarType()) {
        helperOp = Op::ConsumeAs(loadOp.getType(), m_builder.addBefore(load, std::move(helperOp)));
      } else {
        auto helperDef = m_builder.addBefore(load, std::move(helperOp));
        helperOp = Op(OpCode::eCompositeConstruct, loadOp.getType());

        for (uint32_t i = 0u; i < loadOp.getType().getBaseType(0u).getVectorSize(); i++) {
          helperOp.addOperand(m_builder.addBefore(load, Op::ConsumeAs(loadOp.getType().getBaseType(0u).getBaseType(),
            m_builder.addBefore(load, Op::CompositeExtract(baseType.getBaseType(), helperDef, m_builder.makeConstant(i))))));
        }
      }
    }

    m_builder.rewriteOp(load, std::move(helperOp));
  }

  /* Remove all stores */
  for (auto store : stores)
    m_builder.remove(store);

  return true;
}


CleanupScratchPass::CbvInfo CleanupScratchPass::getCbvCopyMapping(const Op& op) {
  /* Ignore consume ops since they only mess around with the type */
  if (op.getOpCode() == OpCode::eConsumeAs)
    return getCbvCopyMapping(m_builder.getOpForOperand(op, 0u));

  /* If we extract from a vector, add the extracted component
   * index to the base component index */
  if (op.getOpCode() == OpCode::eCompositeExtract) {
    auto mapping = getCbvCopyMapping(m_builder.getOpForOperand(op, 0u));

    if (mapping.cbv) {
      const auto& indexOp = m_builder.getOpForOperand(op, 1u);
      mapping.cbvComponent += uint32_t(indexOp.getOperand(0u));
    }

    return mapping;
  }

  if (op.getOpCode() == OpCode::eBufferLoad) {
    /* Operand must be a CBV */
    const auto& cbvDescriptor = m_builder.getOpForOperand(op, 0u);

    if (cbvDescriptor.getType() != ScalarType::eCbv)
      return CbvInfo();

    /* CBV must be an array of scalars or vectors so we can index properly */
    const auto& cbv = m_builder.getOpForOperand(cbvDescriptor, 0u);
    dxbc_spv_assert(cbv.getOpCode() == OpCode::eDclCbv);

    if (!cbv.getType().isArrayType() ||
        !cbv.getType().getSubType(0u).isBasicType())
      return CbvInfo();

    const auto& cbvOffset = m_builder.getOpForOperand(op, 1u);

    CbvInfo result = { };
    result.cbv = cbv.getDef();
    result.cbvIndex = m_builder.getOpForOperand(cbvDescriptor, 1u).getDef();

    if (cbvOffset.getType().isScalarType()) {
      result.cbvOffset = cbvOffset.getDef();
      result.cbvComponent = 0u;
      return result;
    } else if (cbvOffset.isConstant()) {
      result.cbvOffset = m_builder.makeConstant(uint32_t(cbvOffset.getOperand(0u)));
      result.cbvComponent = uint32_t(cbvOffset.getOperand(1u));
      return result;
    } else if (cbvOffset.getOpCode() == OpCode::eCompositeConstruct) {
      result.cbvOffset = m_builder.getOpForOperand(cbvOffset, 0u).getDef();
      result.cbvComponent = uint32_t(m_builder.getOpForOperand(cbvOffset, 1u).getOperand(0u));
      return result;
    } else {
      return CbvInfo();
    }
  }

  /* No CBV load */
  return CbvInfo();
}


SsaDef CleanupScratchPass::emitScratchCbvFunction(SsaDef def, const CbvInfo& baseStore, uint32_t baseIndex, uint32_t storeMask) {
  auto type = m_builder.getOp(def).getType();
  auto baseType = type.getBaseType(0u);
  auto componentCount = baseType.getVectorSize();

  /* Check whether store mask is sparse */
  bool isSparse = util::tzcnt(storeMask + 1u) < type.getArraySize(0u);

  /* Build helper function to perform re-mapped CBV loads */
  auto ref = m_builder.getCode().first->getDef();

  if (baseType.isUnknownType())
    baseType = BasicType(ScalarType::eU32, componentCount);

  auto paramBase = m_builder.add(Op::DclParam(ScalarType::eU32));
  m_builder.add(Op::DebugName(paramBase, "base"));

  auto paramIndex = m_builder.add(Op::DclParam(ScalarType::eU32));
  m_builder.add(Op::DebugName(paramIndex, "index"));

  auto function = m_builder.addBefore(ref, Op::Function(baseType).addParam(paramBase).addParam(paramIndex));
  m_builder.add(Op::DebugName(function, getScratchCbvFunctionName(def).c_str()));

  m_builder.setCursor(function);
  m_builder.add(Op::Label());

  /* Bound-check against array size or sparse index map as necessary */
  auto base = m_builder.add(Op::ParamLoad(ScalarType::eU32, function, paramBase));
  auto index = m_builder.add(Op::ParamLoad(ScalarType::eU32, function, paramIndex));
  auto isInBounds = m_builder.makeConstant(true);

  if (m_options.enableBoundChecking) {
    isInBounds = m_builder.add(Op::BAnd(ScalarType::eBool, isInBounds,
      m_builder.add(Op::ULt(ScalarType::eBool, index, m_builder.makeConstant(type.getArraySize(0u))))));
  }

  if (isSparse) {
    isInBounds = m_builder.add(Op::BAnd(ScalarType::eBool, isInBounds,
      m_builder.add(Op::INe(ScalarType::eBool, m_builder.makeConstant(0u),
        m_builder.add(Op::UBitExtract(ScalarType::eU32, m_builder.makeConstant(storeMask), index, m_builder.makeConstant(1u)))))));
  }

  /* Load CBV descriptor and compute actual offset */
  auto cbvType = m_builder.getOp(baseStore.cbv).getType().getBaseType(0u);

  auto descriptor = m_builder.add(Op::DescriptorLoad(ScalarType::eCbv, baseStore.cbv, baseStore.cbvIndex));
  auto address = m_builder.add(Op::IAdd(ScalarType::eU32, base, index));

  if (baseIndex)
    address = m_builder.add(Op::ISub(ScalarType::eU32, address, m_builder.makeConstant(baseIndex)));

  auto vector = m_builder.add(Op::BufferLoad(cbvType, descriptor, address, cbvType.byteAlignment()));

  /* Convert and check each scalar separately, then assemble vector as necesssary */
  util::small_vector<SsaDef, 4u> scalars;

  for (uint32_t i = 0u; i < componentCount; i++) {
    auto& scalar = scalars.emplace_back();
    scalar = m_builder.add(Op::CompositeExtract(cbvType.getBaseType(),
      vector, m_builder.makeConstant(baseStore.cbvComponent + i)));

    if (cbvType.getBaseType() != baseType.getBaseType())
      scalar = m_builder.add(Op::ConsumeAs(baseType.getBaseType(), scalar));

    scalar = m_builder.add(Op::Select(baseType.getBaseType(),
      isInBounds, scalar, m_builder.makeConstantZero(baseType.getBaseType())));
  }

  if (componentCount == 1u) {
    m_builder.add(Op::Return(baseType, scalars.at(0u)));
  } else {
    Op vectorOp(OpCode::eCompositeConstruct, baseType);

    for (auto s : scalars)
      vectorOp.addOperand(s);

    m_builder.add(Op::Return(baseType, m_builder.add(std::move(vectorOp))));
  }

  m_builder.add(Op::FunctionEnd());
  return function;
}


SsaDef CleanupScratchPass::extractCbvArrayIndex(const Op& op) {
  if (op.getType().isScalarType())
    return op.getDef();

  if (op.isConstant())
    return m_builder.makeConstant(uint32_t(op.getOperand(0u)));

  if (op.getOpCode() == OpCode::eCompositeConstruct)
    return SsaDef(op.getOperand(0u));

  return SsaDef();
}


SsaDef CleanupScratchPass::extractCbvComponent(const Op& op) {
  if (op.getType().isScalarType())
    return SsaDef();

  dxbc_spv_assert(op.getType().isVectorType());

  if (op.isConstant())
    return m_builder.makeConstant(uint32_t(op.getOperand(1u)));

  dxbc_spv_assert(op.getOpCode() == OpCode::eCompositeConstruct);
  return SsaDef(op.getOperand(1u));
}


bool CleanupScratchPass::hasConstantIndexOffset(SsaDef baseIndex, SsaDef index, uint32_t offset) const {
  const auto& baseOp = m_builder.getOp(baseIndex);
  const auto& testOp = m_builder.getOp(index);

  auto [baseRelative, baseAbsolute] = extractBaseAndOffset(baseOp);
  auto [testRelative, testAbsolute] = extractBaseAndOffset(testOp);

  if (testRelative == baseIndex && testAbsolute == offset)
    return true;

  if (testRelative == baseRelative && testAbsolute == baseAbsolute + offset)
    return true;

  return false;
}


std::pair<SsaDef, uint64_t> CleanupScratchPass::extractBaseAndOffset(const Op& op) const {
  dxbc_spv_assert(op.getType().isScalarType());

  if (op.isConstant())
    return std::make_pair(SsaDef(), uint64_t(op.getOperand(0u)));

  if (op.getOpCode() == OpCode::eIAdd) {
    const auto& a = m_builder.getOpForOperand(op, 0u);
    const auto& b = m_builder.getOpForOperand(op, 0u);

    auto [aBase, aOffset] = extractBaseAndOffset(a);
    auto [bBase, bOffset] = extractBaseAndOffset(b);

    if (!aBase || !bBase)
      return std::make_pair(aBase ? aBase : bBase, aOffset + bOffset);
  }

  return std::make_pair(op.getDef(), uint64_t(0u));
}


std::string CleanupScratchPass::getScratchCbvFunctionName(SsaDef def) const {
  std::stringstream str;
  str << "load_";

  auto [a, b] = m_builder.getUses(def);
  bool foundDebugName = false;

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eDebugName) {
      str << iter->getLiteralString(iter->getFirstLiteralOperandIndex());
      foundDebugName = true;
      break;
    }
  }

  if (!foundDebugName)
    str << "scratch";

  str << "_cbv";
  return str.str();
}

}
