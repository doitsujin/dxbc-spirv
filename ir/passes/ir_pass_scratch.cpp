#include "ir_pass_scratch.h"
#include "ir_pass_ssa.h"

#include "../../util/util_log.h"

namespace dxbc_spv::ir {

CleanupScratchPass::CleanupScratchPass(Builder& builder, const Options& options)
: m_builder(builder), m_options(options) {

}


CleanupScratchPass::~CleanupScratchPass() {

}


bool CleanupScratchPass::propagateCbvScratchCopies() {
  bool feedback = false;

  if (!m_options.resolveCbvCopy)
    return feedback;

  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eDclScratch)
      feedback |= promoteScratchCbvCopy(iter->getDef());
  }

  return feedback;
}


bool CleanupScratchPass::runResolveCbvToScratchCopyPass(Builder& builder, const Options& options) {
  return CleanupScratchPass(builder, options).propagateCbvScratchCopies();
}


bool CleanupScratchPass::unpackArrays() {
  auto [a, b] = m_builder.getDeclarations();

  bool feedback = false;

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eDclScratch)
      feedback |= tryUnpackArray(iter->getDef());
  }

  if (feedback)
    ir::SsaConstructionPass::runPass(m_builder);

  return feedback;
}


bool CleanupScratchPass::runUnpackArrayPass(Builder& builder, const Options& options) {
  return CleanupScratchPass(builder, options).unpackArrays();
}


bool CleanupScratchPass::enableBoundChecking() {
  bool feedback = false;

  if (!m_options.enableBoundChecking)
    return feedback;

  auto iter = m_builder.begin();

  while (iter != m_builder.getDeclarations().second) {
    if (iter->getOpCode() == OpCode::eDclScratch) {
      if (boundCheckScratchArray(iter->getDef())) {
        auto type = iter->getType();

        for (uint32_t i = 0u; i < type.getArrayDimensions(); i++)
          type.setArraySize(i, 1u + type.getArraySize(i));

        m_builder.setOpType(iter->getDef(), type);
        feedback = true;
      }
    } else if (iter->isConstant() && iter->getType().isArrayType()) {
      if (boundCheckScratchArray(iter->getDef())) {
        iter = rewriteBoundCheckedConstant(*iter);
        feedback = true;
        continue;
      }
    }

    ++iter;
  }

  return feedback;
}


bool CleanupScratchPass::runBoundCheckingPass(Builder& builder, const Options& options) {
  return CleanupScratchPass(builder, options).enableBoundChecking();
}


bool CleanupScratchPass::runPass(Builder& builder, const Options& options) {
  bool feedback = false;

  CleanupScratchPass instance(builder, options);
  feedback |= instance.propagateCbvScratchCopies();
  feedback |= instance.unpackArrays();
  feedback |= instance.enableBoundChecking();

  return feedback;
}


bool CleanupScratchPass::promoteScratchCbvCopy(SsaDef def) {
  constexpr uint32_t MaxSparseSize = 32u;

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


bool CleanupScratchPass::boundCheckScratchArray(SsaDef def) {
  const auto& scratchOp = m_builder.getOp(def);

  util::small_vector<SsaDef, 256u> uses;
  m_builder.getUses(def, uses);

  bool addedBoundCheck = false;

  for (auto use : uses) {
    auto useOp = m_builder.getOp(use);

    if (useOp.getFlags() & OpFlag::eInBounds)
      continue;

    if (useOp.getOpCode() != OpCode::eScratchLoad &&
        useOp.getOpCode() != OpCode::eScratchStore &&
        useOp.getOpCode() != OpCode::eConstantLoad)
      continue;

    /* Ignore op if the index is fully constant and in bounds */
    const auto& indexOp = m_builder.getOpForOperand(useOp, 1u);
    auto indexType = indexOp.getType().getBaseType(0u);

    uint32_t dims = std::min(scratchOp.getType().getArrayDimensions(), indexType.getVectorSize());

    SsaDef inBoundsCond = { };
    bool isInBoundsConstant = true;

    for (uint32_t i = 0u; i < dims; i++) {
      auto size = scratchOp.getType().getArraySize(scratchOp.getType().getArrayDimensions() - i - 1u);
      isInBoundsConstant = isInBoundsConstant && indexOp.isConstant() && uint32_t(indexOp.getOperand(i)) < size;
    }

    if (isInBoundsConstant)
      continue;

    /* Check all relevant index components against the respective array size */
    util::small_vector<SsaDef, 4u> indexDefs;

    for (uint32_t i = 0u; i < dims; i++) {
      auto size = scratchOp.getType().getArraySize(scratchOp.getType().getArrayDimensions() - i - 1u);

      auto& indexDef = indexDefs.emplace_back();
      indexDef = indexOp.getDef();

      if (indexType.isVector()) {
        indexDef = m_builder.addBefore(use, Op::CompositeExtract(
          indexType.getBaseType(), indexDef, m_builder.makeConstant(i)));
      }

      if (useOp.getOpCode() == OpCode::eScratchStore) {
        auto check = m_builder.addBefore(use,
          Op::ULt(ScalarType::eBool, indexDef, m_builder.makeConstant(size)));

        if (inBoundsCond) {
          inBoundsCond = m_builder.addBefore(use,
            Op::BAnd(ScalarType::eBool, inBoundsCond, check));
        } else {
          inBoundsCond = check;
        }
      }

      indexDef = m_builder.addBefore(use,
        Op::UMin(indexType.getBaseType(), indexDef, m_builder.makeConstant(size)));
    }

    for (uint32_t i = dims; i < indexType.getVectorSize(); i++) {
      indexDefs.push_back(m_builder.addBefore(use,
        Op::CompositeExtract(indexType.getBaseType(), indexOp.getDef(), m_builder.makeConstant(i))));
    }

    /* Build new index vector */
    auto newIndex = indexDefs.front();

    if (indexType.isVector()) {
      Op newIndexOp(OpCode::eCompositeConstruct, indexType);

      for (auto s : indexDefs)
        newIndexOp.addOperand(s);

      newIndex = m_builder.addBefore(use, std::move(newIndexOp));
    }

    useOp.setFlags(useOp.getFlags() | OpFlag::eInBounds);
    useOp.setOperand(1u, newIndex);

    /* If this is an out-of-bounds store, replace the value with zero */
    if (useOp.getOpCode() == OpCode::eScratchStore && inBoundsCond) {
      const auto& valueOp = m_builder.getOpForOperand(useOp, 2u);

      useOp.setOperand(2u, m_builder.addBefore(use, Op::Select(valueOp.getType(),
        inBoundsCond, valueOp.getDef(), m_builder.makeConstantZero(valueOp.getType()))));
    }

    m_builder.rewriteOp(use, std::move(useOp));
    addedBoundCheck = true;
  }

  return addedBoundCheck;
}


Builder::iterator CleanupScratchPass::rewriteBoundCheckedConstant(const Op& op) {
  /* Determine number of scalars in the innermost non-array type */
  Type type = op.getType();
  Type baseType = type;

  for (uint32_t i = 0u; i < type.getArrayDimensions(); i++)
    baseType = baseType.getSubType(0u);

  uint32_t scalarCount = baseType.computeFlattenedScalarCount();
  uint32_t scalarIndex = 0u;

  /* Build new constant by copying scalars from the source, and insert
   * zero constants as the last element in each array dimension. */
  util::small_vector<uint32_t, Type::MaxArrayDimensions> idx(type.getArrayDimensions());

  Op newConstant(OpCode::eConstant, Type());

  while (true) {
    /* Check whether we can copy source values or
     * need to insert zeroes in this iteration */
    bool copy = true;

    for (size_t i = 0u; i < idx.size(); i++) {
      if (idx.at(i) == type.getArraySize(i))
        copy = false;
    }

    /* Add constant operands */
    if (copy) {
      for (uint32_t i = 0u; i < scalarCount; i++)
        newConstant.addOperand(op.getOperand(scalarIndex++));
    } else {
      for (uint32_t i = 0u; i < scalarCount; i++)
        newConstant.addOperand(Operand());
    }

    /* Advance indices. If the last index, i.e. the one for the
     * outermost array dimension, exceeds the target array size,
     * we're done. */
    size_t dim = 0u;

    while (dim < idx.size() && (++idx.at(dim) > type.getArraySize(dim)))
      idx.at(dim++) = 0u;

    if (dim == idx.size())
      break;
  }

  /* Increase the size of each array dimension by one */
  for (uint32_t i = 0u; i < type.getArrayDimensions(); i++)
    type.setArraySize(i, type.getArraySize(i) + 1u);

  newConstant.setType(type);

  return m_builder.iter(m_builder.rewriteDef(op.getDef(),
    m_builder.add(std::move(newConstant))));
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


bool CleanupScratchPass::tryUnpackArray(SsaDef def) {
  const auto& arrayOp = m_builder.getOp(def);
  auto arraySize = arrayOp.getType().getArraySize(0u);

  bool hasDynamicLoads = !allLoadIndicesConstant(def);
  bool hasDynamicStores = !allStoreIndicesConstant(def);

  uint32_t maxSize = 0u;

  if (hasDynamicLoads || hasDynamicStores) {
    if (m_options.unpackSmallArrays) {
      maxSize = hasDynamicStores
        ? m_options.maxUnpackedDynamicStoreArraySize
        : m_options.maxUnpackedDynamicLoadArraySize;
    }
  } else if (m_options.unpackConstantIndexedArrays) {
    maxSize = -1u;
  }

  if (arraySize > maxSize)
    return false;

  /* Need to skip if any accesses are in different functions,
   * since temporaries simply won't work in that case. */
  if (!m_functionsForDef.getMaxValidDef())
    determineFunctionForDefs();

  SsaDef accessFunction = { };

  auto [a, b] = m_builder.getUses(def);

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eScratchLoad ||
        iter->getOpCode() == OpCode::eScratchStore) {
      auto function = m_functionsForDef.at(iter->getDef());

      if (!accessFunction)
        accessFunction = function;

      if (accessFunction != function)
        return false;
    }
  }

  return unpackArray(def);
}


bool CleanupScratchPass::unpackArray(SsaDef def) {
  const auto& arrayOp = m_builder.getOp(def);

  /* For simplicity, only accept scalar or vector arrays */
  if (!arrayOp.getType().isArrayType() ||
      !arrayOp.getType().getSubType(0u).isBasicType())
    return false;

  auto arraySize = arrayOp.getType().getArraySize(0u);
  auto baseType = arrayOp.getType().getBaseType(0u);

  util::small_vector<SsaDef, 256u> temps(arraySize * baseType.getVectorSize());

  for (size_t i = 0u; i < temps.size(); i++)
    temps.at(i) = m_builder.add(Op::DclTmp(baseType.getBaseType(), SsaDef(arrayOp.getOperand(0u))));

  /* Replace constant index loads and stores with a load or store using the
   * temporary corresponding to the constant index and component. If the index
   * is dynamic, use a helper function to emit an if ladder. */
  util::small_vector<SsaDef, 256u> uses;
  m_builder.getUses(def, uses);

  for (auto use : uses) {
    const auto& useOp = m_builder.getOp(use);

    switch (useOp.getOpCode()) {
      case OpCode::eScratchLoad: {
        rewriteScratchLoadToTemp(useOp, temps.size(), temps.data());
      } break;

      case OpCode::eScratchStore: {
        rewriteScratchStoreToTemp(useOp, temps.size(), temps.data());
      } break;

      default:
        break;
    }
  }

  return true;
}


void CleanupScratchPass::rewriteScratchLoadToTemp(const Op& loadOp, size_t tempCount, const SsaDef* temps) {
  (void)tempCount;

  dxbc_spv_assert(loadOp.getType().isBasicType());

  const auto& scratchOp = m_builder.getOpForOperand(loadOp, 0u);
  const auto& indexOp = m_builder.getOpForOperand(loadOp, 1u);

  auto scratchType = scratchOp.getType().getBaseType(0u);
  auto valueType = loadOp.getType().getBaseType(0u);
  auto indexInfo = extractConstantIndex(indexOp);

  util::small_vector<SsaDef, 4u> scalars;

  if (!indexInfo.indexDef) {
    /* Emit single load for each vector component */
    for (uint32_t c = 0u; c < valueType.getVectorSize(); c++) {
      auto tempIndex = indexInfo.index * scratchType.getVectorSize() + indexInfo.component + c;
      dxbc_spv_assert(tempIndex < tempCount);

      scalars.push_back(m_builder.addBefore(loadOp.getDef(),
        Op::TmpLoad(valueType.getBaseType(), temps[tempIndex])));
    }
  } else {
    if (m_options.enableBoundChecking && !(loadOp.getFlags() & OpFlag::eInBounds)) {
      /* Start with a zero constant in case of an out-of-bound index */
      for (uint32_t c = 0u; c < valueType.getVectorSize(); c++)
        scalars.push_back(m_builder.makeConstantZero(valueType.getBaseType()));
    }

    for (uint32_t a = 0u; a < scratchOp.getType().getArraySize(0u); a++) {
      auto indexCond = m_builder.addBefore(loadOp.getDef(),
        Op::IEq(ScalarType::eBool, indexInfo.indexDef, m_builder.makeConstant(a)));

      for (uint32_t c = 0u; c < valueType.getVectorSize(); c++) {
        auto tempIndex = a * scratchType.getVectorSize() + indexInfo.component + c;
        dxbc_spv_assert(tempIndex < tempCount);

        auto tempValue = m_builder.addBefore(loadOp.getDef(),
          Op::TmpLoad(valueType.getBaseType(), temps[tempIndex]));

        auto& scalar = scalars.at(c);

        if (scalar) {
          scalar = m_builder.addBefore(loadOp.getDef(),
            Op::Select(valueType.getBaseType(), indexCond, tempValue, scalar));
        } else {
          scalar = tempValue;
        }
      }
    }
  }

  /* Assemble vector as necessary */
  if (valueType.isVector()) {
    Op compositeOp(OpCode::eCompositeConstruct, valueType);

    for (auto s : scalars)
      compositeOp.addOperand(s);

    m_builder.rewriteOp(loadOp.getDef(), std::move(compositeOp));
  } else {
    m_builder.rewriteDef(loadOp.getDef(), scalars.front());
  }
}


void CleanupScratchPass::rewriteScratchStoreToTemp(const Op& storeOp, size_t tempCount, const SsaDef* temps) {
  (void)tempCount;

  const auto& scratchOp = m_builder.getOpForOperand(storeOp, 0u);
  const auto& indexOp = m_builder.getOpForOperand(storeOp, 1u);
  const auto& valueOp = m_builder.getOpForOperand(storeOp, 2u);
  dxbc_spv_assert(valueOp.getType().isBasicType());

  auto scratchType = scratchOp.getType().getBaseType(0u);
  auto valueType = valueOp.getType().getBaseType(0u);
  auto indexInfo = extractConstantIndex(indexOp);

  /* Extract scalars from value vector as necessary */
  util::small_vector<SsaDef, 4u> scalars;

  for (uint32_t c = 0u; c < valueType.getVectorSize(); c++) {
    auto& scalar = scalars.emplace_back();
    scalar = valueOp.getDef();

    if (valueType.isVector()) {
      scalar = m_builder.addBefore(storeOp.getDef(), Op::CompositeExtract(
        scratchType.getBaseType(), scalar, m_builder.makeConstant(c)));
    }
  }

  if (!indexInfo.indexDef) {
    /* Emit single store for each vector component */
    for (uint32_t c = 0u; c < valueType.getVectorSize(); c++) {
      auto tempIndex = indexInfo.index * scratchType.getVectorSize() + indexInfo.component + c;
      dxbc_spv_assert(tempIndex < tempCount);

      m_builder.addBefore(storeOp.getDef(), Op::TmpStore(temps[tempIndex], scalars.at(c)));
    }
  } else {
    /* Load each temporary and replace its value if the index matches */
    for (uint32_t a = 0u; a < scratchOp.getType().getArraySize(0u); a++) {
      auto indexCond = m_builder.addBefore(storeOp.getDef(),
        Op::IEq(ScalarType::eBool, indexInfo.indexDef, m_builder.makeConstant(a)));

      for (uint32_t c = 0u; c < valueType.getVectorSize(); c++) {
        auto tempIndex = a * scratchType.getVectorSize() + indexInfo.component + c;
        dxbc_spv_assert(tempIndex < tempCount);

        auto tempValue = m_builder.addBefore(storeOp.getDef(),
          Op::TmpLoad(scratchType.getBaseType(), temps[tempIndex]));

        tempValue = m_builder.addBefore(storeOp.getDef(),
          Op::Select(scratchType.getBaseType(), indexCond, scalars.at(c), tempValue));

        m_builder.addBefore(storeOp.getDef(),
          Op::TmpStore(temps[tempIndex], tempValue));
      }
    }
  }

  m_builder.remove(storeOp.getDef());
}


void CleanupScratchPass::determineFunctionForDefs() {
  m_functionsForDef.ensure(m_builder.getMaxValidDef());

  auto [a, b] = m_builder.getCode();
  SsaDef function = { };

  for (auto iter = a; iter != b; iter++) {
    switch (iter->getOpCode()) {
      case OpCode::eFunction: {
        function = iter->getDef();
        m_functionsForDef.at(iter->getDef()) = function;
      } break;

      case OpCode::eFunctionEnd: {
        m_functionsForDef.at(iter->getDef()) = function;
        function = SsaDef();
      } break;

      default:
        m_functionsForDef.at(iter->getDef()) = function;
    }
  }
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


bool CleanupScratchPass::allStoreIndicesConstant(SsaDef def) const {
  auto [a, b] = m_builder.getUses(def);

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eScratchStore) {
      const auto& index = m_builder.getOpForOperand(*iter, 1u);

      if (!isConstantIndex(index))
        return false;
    }
  }

  return true;
}


bool CleanupScratchPass::allLoadIndicesConstant(SsaDef def) const {
  auto [a, b] = m_builder.getUses(def);

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eScratchLoad) {
      const auto& index = m_builder.getOpForOperand(*iter, 1u);

      if (!isConstantIndex(index))
        return false;
    }
  }

  return true;
}


bool CleanupScratchPass::isConstantIndex(const Op& op) const {
  if (op.isConstant())
    return true;

  if (op.getOpCode() == OpCode::eCompositeConstruct) {
    const auto& a = m_builder.getOpForOperand(op, 0u);
    const auto& b = m_builder.getOpForOperand(op, 1u);

    return a.isConstant() && b.isConstant();
  }

  return false;
}


CleanupScratchPass::IndexInfo CleanupScratchPass::extractConstantIndex(const Op& op) const {
  if (op.isConstant()) {
    IndexInfo result = { };
    result.index = uint32_t(op.getOperand(0u));
    result.component = uint32_t(op.getOperand(1u));
    return result;
  }

  if (op.getOpCode() == OpCode::eCompositeConstruct) {
    const auto& a = m_builder.getOpForOperand(op, 0u);
    const auto& b = m_builder.getOpForOperand(op, 1u);
    dxbc_spv_assert(b.isConstant());

    IndexInfo result = { };
    result.index = a.isConstant() ? uint32_t(a.getOperand(0u)) : 0u;
    result.indexDef = a.isConstant() ? SsaDef() : a.getDef();
    result.component = uint32_t(b.getOperand(0u));
    return result;
  }

  dxbc_spv_assert(op.getType().isScalarType());

  IndexInfo result = { };
  result.indexDef = op.getDef();
  return result;
}


}
