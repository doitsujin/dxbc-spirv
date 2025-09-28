#include "ir_pass_descriptor_indexing.h"

#include "../../util/util_log.h"

namespace dxbc_spv::ir {

DescriptorIndexingPass::DescriptorIndexingPass(Builder& builder, const Options& options)
: m_builder(builder), m_options(options) {

}


DescriptorIndexingPass::~DescriptorIndexingPass() {

}


bool DescriptorIndexingPass::run() {
  if (!m_options.optimizeDescriptorIndexing)
    return false;

  gatherIndexableResourceInfos();

  if (m_indexableInfo.empty())
    return false;

  ensureDivergenceInfo();

  for (const auto& e : m_indexableInfo)
    rewriteIndexedResource(e);

  deduplicateBindings();
  return true;
}


bool DescriptorIndexingPass::deduplicateBindings() {
  util::small_vector<SsaDef, 256u> bindings;
  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eDclSrv || iter->getOpCode() == OpCode::eDclSampler)
      bindings.push_back(iter->getDef());
  }

  /* Sort bindings by register index to consistently maintain
   * the debug name of the first accessed resource */
  std::sort(bindings.begin(), bindings.end(), [&] (SsaDef a, SsaDef b) {
    auto aRegIndex = uint32_t(m_builder.getOp(a).getOperand(2u));
    auto bRegIndex = uint32_t(m_builder.getOp(b).getOperand(2u));

    return aRegIndex > bRegIndex;
  });

  bool progress = false;

  for (size_t i = 0u; i < bindings.size(); i++) {
    for (size_t j = 0u; j < i; j++) {
      if (!bindings.at(j))
        continue;

      const auto& a = m_builder.getOp(bindings.at(i));
      const auto& b = m_builder.getOp(bindings.at(j));

      if (isEquivalentResource(a, b)) {
        auto aRegSpace = uint32_t(a.getOperand(1u));
        auto bRegSpace = uint32_t(b.getOperand(1u));

        if (aRegSpace == bRegSpace) {
          auto aRegIndex = uint32_t(a.getOperand(2u));
          auto aRegCount = uint32_t(a.getOperand(3u));

          auto bRegIndex = uint32_t(b.getOperand(2u));
          auto bRegCount = uint32_t(b.getOperand(3u));

          if (aRegIndex + aRegCount > bRegIndex && bRegIndex + bRegCount > aRegIndex) {
            progress |= mergeBindings(a, b);
            bindings.at(j) = SsaDef();
          }
        }
      }
    }
  }

  return progress;
}


bool DescriptorIndexingPass::mergeBindings(const Op& a, const Op& b) {
  dxbc_spv_assert(isEquivalentResource(a, b));

  auto aRegIndex = uint32_t(a.getOperand(2u));
  auto bRegIndex = uint32_t(b.getOperand(2u));

  auto aRegCount = uint32_t(a.getOperand(3u));
  auto bRegCount = uint32_t(b.getOperand(3u));

  uint32_t mergedIndex = std::min(aRegIndex, bRegIndex);
  uint32_t mergedCount = std::max(aRegIndex + aRegCount, bRegIndex + bRegCount) - mergedIndex;

  uint32_t aOffset = aRegIndex - mergedIndex;
  uint32_t bOffset = bRegIndex - mergedIndex;

  bool progress = false;

  if (aOffset)
    progress |= rewriteDescriptorLoads(a, a, aOffset);

  progress |= rewriteDescriptorLoads(b, a, bOffset);

  Op newResource = Op(a)
    .setOperand(2u, mergedIndex)
    .setOperand(3u, mergedCount);

  m_builder.rewriteOp(a.getDef(), std::move(newResource));
  return progress;
}


bool DescriptorIndexingPass::rewriteDescriptorLoads(const Op& oldBinding, const Op& newBinding, uint32_t descriptorIndex) {
  util::small_vector<SsaDef, 256> uses;
  m_builder.getUses(oldBinding.getDef(), uses);

  bool progress = false;

  for (auto use : uses) {
    auto useOp = m_builder.getOp(use);

    if (useOp.getOpCode() == OpCode::eDescriptorLoad) {
      useOp.setOperand(0u, newBinding.getDef());

      if (descriptorIndex) {
        auto index = m_builder.addBefore(use, Op::IAdd(ScalarType::eU32,
          m_builder.getOpForOperand(useOp, 1u).getDef(),
          m_builder.makeConstant(descriptorIndex)));
        useOp.setOperand(1u, index);
      }

      m_builder.rewriteOp(use, std::move(useOp));
      progress = true;
    }
  }

  return progress;
}


bool DescriptorIndexingPass::runPass(Builder& builder, const Options& options) {
  return DescriptorIndexingPass(builder, options).run();
}


bool DescriptorIndexingPass::runDeduplicateBindingPass(Builder& builder, const Options& options) {
  return DescriptorIndexingPass(builder, options).deduplicateBindings();
}


bool DescriptorIndexingPass::validateAccessEntry(const ResourceAccessInfo& info) {
  /* Ignore entries with a size of 1 since indexing is a no-op in that case */
  if (info.indexHi == info.indexLo + 1)
    return false;

  ensureDominanceInfo();

  /* Find base block terminator and ensure that the fallback values actually
   * dominate it, otherwise we cannot use it in another phi. */
  auto terminator = m_dominance->getBlockTerminator(info.baseBlock);

  for (auto e : info.fallback) {
    if (e && !m_dominance->defDominates(e, terminator))
      return false;
  }

  return true;
}


void DescriptorIndexingPass::rewriteIndexedResource(const ResourceAccessInfo& info) {
  /* Emit resource declaration as a resource array */
  uint32_t descriptorCount = uint32_t(info.indexHi - info.indexLo);

  auto resourceOp = m_builder.getOp(info.baseResource);
  resourceOp.setOperand(3u, descriptorCount);

  auto resourceArray = m_builder.add(std::move(resourceOp));
  auto debugName = getDebugName(info.baseResource);

  if (!debugName.empty())
    m_builder.add(Op::DebugName(resourceArray, debugName.c_str()));

  /* If fallback values are used, we need to make the entire operation conditional */
  bool hasFallback = false;

  for (auto e : info.fallback)
    hasFallback = hasFallback || e;

  /* Find block terminator and insert resource access before it */
  auto terminator = m_dominance->getBlockTerminator(info.baseBlock);

  /* Insert block that conditionally performs the resource access */
  auto descriptorIndex = info.indexVar;
  auto indexType = m_builder.getOp(descriptorIndex).getType().getBaseType(0u);

  if (info.indexLo) {
    Op op(OpCode::eConstant, indexType);
    op.addOperand(uint32_t(info.indexLo));

    descriptorIndex = m_builder.addBefore(terminator,
      Op::ISub(indexType, descriptorIndex, m_builder.add(op)));
  }

  if (indexType != ScalarType::eU32) {
    if (indexType == ScalarType::eI32) {
      descriptorIndex = m_builder.addBefore(terminator,
        Op::Cast(ScalarType::eU32, descriptorIndex));
    } else {
      descriptorIndex = m_builder.addBefore(terminator,
        Op::ConvertItoI(ScalarType::eU32, descriptorIndex));
    }
  }

  SsaDef cond = { };

  if (hasFallback) {
    cond = m_builder.addBefore(terminator, Op::ULt(ScalarType::eBool,
      descriptorIndex, m_builder.makeConstant(descriptorCount)));
  }

  /* Clamp descriptor index to maximum array index */
  descriptorIndex = m_builder.addBefore(terminator, Op::UMin(
    ScalarType::eU32, descriptorIndex, m_builder.makeConstant(descriptorCount - 1u)));

  /* Emit descriptor load and mark it as non-uniform as necessary */
  auto descriptorLoad = m_builder.addBefore(terminator, Op::DescriptorLoad(
    ScalarType::eSrv, resourceArray, descriptorIndex));
  m_builder.setOpFlags(descriptorLoad, OpFlag::eInBounds);

  if (m_divergence->getUniformScopeForDef(info.indexVar) < Scope::eSubgroup)
    m_builder.setOpFlags(descriptorLoad, OpFlag::eNonUniform);

  /* Emit actual resource access op and composite extract ops */
  std::array<SsaDef, 4u> components = { };

  auto accessOp = info.accessOp;
  accessOp.setOperand(0u, descriptorLoad);

  auto accessDef = m_builder.addBefore(terminator, accessOp);
  auto componentType = accessOp.getType();

  if (accessOp.getType().isScalarType()) {
    components.at(0u) = accessDef;
  } else {
    componentType = accessOp.getType().getSubType(0u);

    for (uint32_t i = 0u; i < components.size(); i++) {
      if (info.phi.at(i)) {
        components.at(i) = m_builder.addBefore(terminator, Op::CompositeExtract(
          componentType, accessDef, m_builder.makeConstant(i)));
      }
    }
  }

  /* Insert casts and phis for each component as necessary */
  for (uint32_t i = 0u; i < components.size(); i++) {
    if (components.at(i)) {
      dxbc_spv_assert(info.phi.at(i));
      auto dstType = m_builder.getOp(info.phi.at(i)).getType();

      if (dstType != componentType)
        components.at(i) = m_builder.addBefore(terminator, Op::Cast(dstType, components.at(i)));

      if (hasFallback) {
        auto fallback = info.fallback.at(i);
        dxbc_spv_assert(fallback);

        components.at(i) = m_builder.addBefore(terminator, Op::Select(
          componentType, cond, components.at(i), fallback));
      }

      m_builder.rewriteDef(info.phi.at(i), components.at(i));
    }
  }
}


void DescriptorIndexingPass::gatherIndexableResourceInfos() {
  auto [a, b] = m_builder.getCode();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::ePhi)
      gatherIndexableInfoForPhi(*iter);
  }

  auto e = m_indexableInfo.begin();

  while (e != m_indexableInfo.end()) {
    if (!validateAccessEntry(*e))
      e = m_indexableInfo.erase(e);
    else
      e++;
  }
}


void DescriptorIndexingPass::gatherIndexableInfoForPhi(const Op& op) {
  /* Check whether the phi value operands are actual resource accesses. We can
   * allow for one non-access operand, which can be either a fallback value or
   * another phi in case of an if-chain. */
  uint32_t numInvalidOperands = 0u;

  forEachPhiOperand(op, [&] (SsaDef, SsaDef value) {
    if (!extractAccessOp(m_builder.getOp(value)).first || !isOnlyUse(m_builder, value, op.getDef()))
      numInvalidOperands++;
  });

  if (numInvalidOperands > 1u)
    return;

  /* The block that the phi is contained in must be the merge block of a selection */
  SsaDef phiBlock = findContainingBlock(m_builder, op.getDef());
  SsaDef phiConstruct = { };

  auto [a, b] = m_builder.getUses(phiBlock);

  for (auto i = a; i != b; i++) {
    if (i->getOpCode() == OpCode::eLabel) {
      if (phiConstruct)
        return;

      auto construct = Construct(i->getOperand(i->getFirstLiteralOperandIndex()));

      if (construct == Construct::eStructuredSelection)
        phiConstruct = i->getDef();
    }
  }

  if (!phiConstruct)
    return;

  /* Investigate branch instruction */
  auto branch = m_builder.iter(phiConstruct);

  while (!isBlockTerminator(branch->getOpCode()))
    branch++;

  if (branch->getOpCode() == OpCode::eBranchConditional)
    gatherIndexableInfoForIf(op, *branch, phiConstruct);
  else if (branch->getOpCode() == OpCode::eSwitch)
    gatherIndexableInfoForSwitch(op, *branch, phiConstruct);
}


void DescriptorIndexingPass::gatherIndexableInfoForIf(const Op& phi, const Op& branch, SsaDef block) {
  dxbc_spv_assert(branch.getOpCode() == OpCode::eBranchConditional);

  /* The condition needs to be a simple integer compare to a constant */
  const auto& condOp = m_builder.getOpForOperand(branch, 0u);

  if (condOp.getOpCode() != OpCode::eIEq || !m_builder.getOpForOperand(condOp, 1u).isConstant())
    return;

  const auto& indexVar = m_builder.getOpForOperand(condOp, 0u).getDef();
  const auto& indexValue = int64_t(m_builder.getOpForOperand(condOp, 1u).getOperand(0u));

  const auto& trueBlock = m_builder.getOpForOperand(branch, 1u);

  SsaDef trueDef = { };
  SsaDef falseDef = { };

  forEachPhiOperand(phi, [&] (SsaDef from, SsaDef value) {
    if (from == trueBlock.getDef())
      trueDef = value;
    else
      falseDef = value;
  });

  if (!trueDef || !falseDef)
    return;

  auto [accessT, componentT] = extractAccessOp(m_builder.getOp(trueDef));
  auto [accessF, componentF] = extractAccessOp(m_builder.getOp(falseDef));

  if (!accessT)
    return;

  const auto& accessOpT = m_builder.getOp(accessT);
  const auto& resourceT = m_builder.getOp(getResourceForAccessOp(accessOpT));

  auto regSpace = uint32_t(resourceT.getOperand(1u));
  auto regIndex = uint32_t(resourceT.getOperand(2u));

  if (accessF) {
    const auto& accessOpF = m_builder.getOp(accessF);
    const auto& resourceF = m_builder.getOp(getResourceForAccessOp(accessOpF));

    if (uint32_t(resourceF.getOperand(1u)) != regSpace ||
        uint32_t(resourceF.getOperand(2u)) != regIndex + 1u ||
        !isEquivalentAccessOp(accessOpT, accessOpF) ||
        (componentT != componentF))
      accessF = SsaDef();
  }

  /* The second operand can be another phi that we already added, in which
   * case we need to inspect and extend the existing entry. Otherwise, we
   * need to create a new entry for the if ladder in question. */
  ResourceAccessInfo info = { };
  info.baseResource = resourceT.getDef();
  info.baseBlock = block;
  info.accessOp = accessOpT;
  info.indexVar = indexVar;
  info.indexLo = indexValue;
  info.indexHi = indexValue + (accessF ? 2 : 1);
  info.regSpace = regSpace;
  info.regIndex = regIndex;
  info.phi.at(componentT) = phi.getDef();

  if (!accessF) {
    info.fallback.at(componentT) = falseDef;

    for (auto& e : m_indexableInfo) {
      if (e.phi.at(componentT) == falseDef) {
        /* Verify that the access operation is compatible */
        if (isEquivalentAccessOp(e.accessOp, info.accessOp) &&
            e.indexVar == info.indexVar &&
            e.indexLo == info.indexLo + 1 &&
            e.regSpace == info.regSpace &&
            e.regIndex == info.regIndex + 1u) {
          info.indexHi = e.indexHi;
          info.fallback.at(componentT) = e.fallback.at(componentT);
        }
      }
    }
  }

  addIndexableInfo(info);
}


void DescriptorIndexingPass::gatherIndexableInfoForSwitch(const Op& phi, const Op& branch, SsaDef block) {
  dxbc_spv_assert(branch.getOpCode() == OpCode::eSwitch);

  const auto& index = m_builder.getOpForOperand(branch, 0u);

  if (!index.getType().getBaseType(0u).isIntType())
    return;

  ResourceAccessInfo info = { };
  info.baseBlock = block;
  info.indexVar = index.getDef();

  /* Find index range */
  util::small_vector<std::pair<int64_t, SsaDef>, 128u> branches;

  int64_t caseCount = 0;

  for (uint32_t i = 2u; i < branch.getOperandCount(); i += 2u) {
    const auto& constant = m_builder.getOpForOperand(branch, i);
    dxbc_spv_assert(constant.isConstant());

    int64_t value = int64_t(constant.getOperand(0u));

    if (info.indexLo == info.indexHi) {
      info.indexLo = value;
      info.indexHi = value + 1;
    } else if (value < info.indexLo) {
      info.indexLo = value;
    } else if (value >= info.indexHi) {
      info.indexHi = value + 1;
    }

    branches.emplace_back(value, m_builder.getOpForOperand(branch, i + 1u).getDef());

    caseCount += 1;
  }

  /* Each index inside the range must be used. Since switch construct cannot
   * have duplicate cases, this is trivially the case if the number of cases
   * matches the size of the index range. */
  if (caseCount != info.indexHi - info.indexLo)
    return;

  /* Find default value to use if the index is out of bounds */
  auto defaultValue = findPhiValue(phi, block);

  if (!defaultValue)
    defaultValue = findPhiValue(phi, m_builder.getOpForOperand(branch, 1u).getDef());

  /* Investigate access ops */
  std::pair<Op, uint32_t> reference = { };

  for (const auto& e : branches) {
    auto value = findPhiValue(phi, e.second);

    auto [access, component] = extractAccessOp(m_builder.getOp(value));

    if (!access)
      return;

    const auto& accessOp = m_builder.getOp(access);

    if (!reference.first) {
      reference.first = accessOp;
      reference.second = component;
    }

    if (!isEquivalentAccessOp(accessOp, reference.first) || component != reference.second)
      return;

    const auto& resource = m_builder.getOp(getResourceForAccessOp(accessOp));

    auto regSpace = uint32_t(resource.getOperand(1u));
    auto regIndex = uint32_t(resource.getOperand(2u));

    uint32_t baseIndex = regIndex - uint32_t(e.first - info.indexLo);

    if (!info.baseResource) {
      info.baseResource = resource.getDef();
      info.accessOp = accessOp;
      info.regSpace = regSpace;
      info.regIndex = baseIndex;
      info.phi.at(component) = phi.getDef();
      info.fallback.at(component) = defaultValue;
    } else {
      if (info.regSpace != regSpace || info.regIndex != baseIndex)
        return;

      if (e.first == info.indexLo)
        info.baseResource = resource.getDef();
    }
  }

  addIndexableInfo(info);
}


bool DescriptorIndexingPass::isEligibleAccessOp(const Op& op) const {
  switch (op.getOpCode()) {
    case OpCode::eImageLoad:
    case OpCode::eImageSample:
    case OpCode::eImageGather:
    case OpCode::eImageQueryMips:
    case OpCode::eImageQuerySize:
    case OpCode::eImageQuerySamples:
    case OpCode::eImageComputeLod:
    case OpCode::eBufferLoad:
    case OpCode::eBufferQuerySize: {
      /* Sparse feedback is annoying to deal with and may be unsafe depending
       * on hardware capabilities, ignore. */
      if (op.getFlags() & OpFlag::eSparseFeedback)
        return false;

      /* The accessed resource must be a typed shader resource. We don't want
       * to handle structured buffers here, and relocating UAV access would
       * get tricky since we have to maintain program order for all accesses. */
      const auto& descriptorOp = m_builder.getOpForOperand(op, 0u);
      const auto& resourceOp = m_builder.getOpForOperand(descriptorOp, 0u);

      if (resourceOp.getOpCode() != OpCode::eDclSrv)
        return false;

      auto kind = ResourceKind(resourceOp.getOperand(4u));

      if (!resourceIsTyped(kind))
        return false;
    } return true;

    default:
      return false;
  }
}


bool DescriptorIndexingPass::isEquivalentAccessOp(const Op& a, const Op& b) const {
  auto aOp = Op(a).setOperand(0u, SsaDef());
  auto bOp = Op(b).setOperand(0u, SsaDef());

  if (!aOp.isEquivalent(bOp))
    return false;

  auto aResource = m_builder.getOp(getResourceForAccessOp(a));
  auto bResource = m_builder.getOp(getResourceForAccessOp(b));

  return isEquivalentResource(aResource, bResource);
}


bool DescriptorIndexingPass::isEquivalentResource(const Op& a, const Op& b) const {
  /* Resources may only differ in the register range */
  auto aOp = Op(a).setOperand(2u, 0u).setOperand(3u, 0u);
  auto bOp = Op(b).setOperand(2u, 0u).setOperand(3u, 0u);

  return aOp.isEquivalent(bOp);
}


std::pair<SsaDef, uint32_t> DescriptorIndexingPass::extractAccessOp(const Op& op) const {
  switch (op.getOpCode()) {
    case OpCode::eCast:
      return extractAccessOp(m_builder.getOpForOperand(op, 0u));

    case OpCode::eCompositeExtract: {
      auto [def, index] = extractAccessOp(m_builder.getOpForOperand(op, 0u));

      if (def && m_builder.getOp(def).getType().isVectorType()) {
        index += uint32_t(m_builder.getOpForOperand(op, 1u).getOperand(0u));
        return std::make_pair(def, index);
      }
    } break;

    default:
      if (isEligibleAccessOp(op))
        return std::make_pair(op.getDef(), 0u);
  }

  return std::make_pair(SsaDef(), 0u);
}


SsaDef DescriptorIndexingPass::getResourceForAccessOp(const Op& op) const {
  const auto& descriptorOp = m_builder.getOpForOperand(op, 0u);
  dxbc_spv_assert(descriptorOp.getOpCode() == OpCode::eDescriptorLoad);

  return m_builder.getOpForOperand(descriptorOp, 0u).getDef();
}


void DescriptorIndexingPass::addIndexableInfo(const ResourceAccessInfo& info) {
  for (auto& e : m_indexableInfo) {
    if (e.baseResource == info.baseResource &&
        e.baseBlock == info.baseBlock &&
        isEquivalentAccessOp(e.accessOp, info.accessOp) &&
        e.indexVar == info.indexVar &&
        e.indexLo == info.indexLo &&
        e.indexHi == info.indexHi &&
        e.regSpace == info.regSpace &&
        e.regIndex == info.regIndex) {
      bool canMerge = true;

      for (uint32_t i = 0u; i < 4u; i++) {
        if (e.phi.at(i) && info.phi.at(i))
          canMerge = false;
      }

      if (canMerge) {
        for (uint32_t i = 0u; i < 4u; i++) {
          if (info.phi.at(i)) {
            e.phi.at(i) = info.phi.at(i);
            e.fallback.at(i) = info.fallback.at(i);
            return;
          }
        }
      }
    }
  }

  m_indexableInfo.push_back(info);
}


void DescriptorIndexingPass::ensureDominanceInfo() {
  if (!m_dominance)
    m_dominance.emplace(m_builder);
}


void DescriptorIndexingPass::ensureDivergenceInfo() {
  ensureDominanceInfo();

  if (!m_divergence)
    m_divergence.emplace(m_builder, *m_dominance);
}


std::string DescriptorIndexingPass::getDebugName(SsaDef def) {
  auto [a, b] = m_builder.getUses(def);

  for (auto i = a; i != b; i++) {
    if (i->getOpCode() == OpCode::eDebugName)
      return i->getLiteralString(i->getFirstLiteralOperandIndex());
  }

  return std::string();
}


SsaDef DescriptorIndexingPass::findPhiValue(const Op& op, SsaDef block) {
  SsaDef result = { };

  forEachPhiOperand(op, [&result, &block] (SsaDef from, SsaDef value) {
    if (from == block)
      result = value;
  });

  return result;
}

}
