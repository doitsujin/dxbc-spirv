#include "ir_pass_cse.h"

#include "../../util/util_hash.h"
#include "../../util/util_log.h"

namespace dxbc_spv::ir {

CsePass::CsePass(Builder& builder, const Options& options)
: m_builder(builder), m_options(options), m_dom(builder) {

}


CsePass::~CsePass() {

}


bool CsePass::run() {
  bool progress = false;
  progress |= resolveDuplicateInstructions();
  progress |= resolveOverlappingLoads();
  return progress;
}


bool CsePass::resolveDuplicateInstructions() {
  std::unordered_multiset<Op, OpHash, OpEq> defs;

  bool progress = false;
  auto iter = m_builder.getCode().first;

  std::vector<SsaDef> blockList;

  while (iter != m_builder.getCode().second) {
    auto opType = classifyOp(*iter);

    if (opType & CseOpFlag::eHasSideEffects)
      m_functionIsPure = false;

    if (opType & CseOpFlag::eCanDeduplicate) {
      bool isTrivial = isTrivialOp(*iter);
      auto [a, b] = defs.equal_range(*iter);

      SsaDef next = { };

      for (auto i = a; i != b; i++) {
        if (m_dom.defDominates(i->getDef(), iter->getDef())) {
          next = m_builder.rewriteDef(iter->getDef(), i->getDef());
          break;
        }

        /* If this is a trivial instruction and there is a common block
         * dominating both instructions, relocate it to that block */
        if (isTrivial) {
          auto dom = m_dom.getClosestCommonDominator(
            m_dom.getBlockForDef(i->getDef()),
            m_dom.getBlockForDef(iter->getDef()));

          /* If the new block post-dominates a loop header but not the corresponding
           * merge block, i.e. if the instruction is located inside a loop but before
           * the loop exit, move it out of the loop to avoid code gen issues */
          if (dom) {
            auto loop = dom;

            while (loop) {
              const auto& loopOp = m_builder.getOp(loop);
              auto pred = m_dom.getImmediateDominator(loop);

              if (Construct(loopOp.getOperand(loopOp.getFirstLiteralOperandIndex())) == Construct::eStructuredLoop) {
                auto merge = m_builder.getOpForOperand(loopOp, 0u).getDef();

                if (m_dom.postDominates(dom, loop) && !m_dom.postDominates(dom, merge)) {
                  bool canRelocate = true;

                  for (uint32_t i = 0u; i < iter->getFirstLiteralOperandIndex(); i++)
                    canRelocate = canRelocate && m_dom.defDominates(SsaDef(iter->getOperand(i)), loop);

                  dom = canRelocate ? pred : SsaDef();
                  break;
                }
              }

              loop = pred;
            }
          }

          if (dom) {
            auto terminator = m_dom.getBlockTerminator(dom);

            m_dom.setBlockForDef(i->getDef(), dom);
            m_builder.reorderBefore(terminator, i->getDef(), i->getDef());
            next = m_builder.rewriteDef(iter->getDef(), i->getDef());
            break;
          }
        }
      }

      if (next) {
        iter = m_builder.iter(next);
        progress = true;
        continue;
      }

      defs.insert(*iter);
    } else if (iter->getOpCode() == OpCode::eLabel) {
      /* For phi processing */
      blockList.push_back(iter->getDef());
    }

    /* If no instruction or function call inside the current function has
     * side effects, mark the function as pure so that calls to it can get
     * deduplicated. Relevant for certain lowering steps. */
    switch (iter->getOpCode()) {
      case OpCode::eFunction: {
        m_functionIsPure = true;
        m_functionDef = iter->getDef();
      } break;

      case OpCode::eFunctionEnd: {
        if (m_functionIsPure)
          m_pureFunctions.insert(m_functionDef);

        m_functionDef = SsaDef();
      } break;

      default:
        break;
    }

    ++iter;
  }

  /* Eliminate redundant phi within each block. */
  for (auto block : blockList) {
    auto phi = m_builder.getNext(block);

    while (m_builder.getOp(phi).getOpCode() == OpCode::ePhi) {
      auto phiToTest = m_builder.getNext(block);
      auto next = ir::SsaDef();

      while (phiToTest != phi) {
        if (m_builder.getOp(phi).isEquivalent(m_builder.getOp(phiToTest))) {
          next = m_builder.rewriteDef(phi, phiToTest);
          break;
        }

        phiToTest = m_builder.getNext(phiToTest);
      }

      phi = next ? next : m_builder.getNext(phi);
    }
  }

  return progress;
}


bool CsePass::resolveOverlappingLoads() {
  if (!m_options.resolveOverlappingLoads)
    return false;

  bool progress = false;

  small_vector<std::pair<SsaDef, ResourceKind>, 64> descriptors;

  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eDclSrv) {
      auto kind = ResourceKind(iter->getOperand(4u));

      if (!resourceIsTyped(kind)) {
        auto [begin, end] = m_builder.getUses(iter->getDef());

        for (auto j = begin; j != end; j++) {
          if (j->getOpCode() == OpCode::eDescriptorLoad)
            descriptors.emplace_back(j->getDef(), kind);
        }
      }
    }
  }

  for (auto e : descriptors)
    progress |= resolveOverlappingLoads(e.second, e.first);

  return progress;
}


bool CsePass::runPass(Builder& builder, const Options& options) {
  return CsePass(builder, options).run();
}


size_t CsePass::OpHash::operator () (const Op& op) const {
  /* Ignore the definition, hash everything else */
  size_t hash = uint32_t(op.getOpCode());
  hash = util::hash_combine(hash, uint8_t(op.getFlags()));
  hash = util::hash_combine(hash, std::hash<Type>()(op.getType()));

  for (uint32_t i = 0u; i < op.getOperandCount(); i++)
    hash = util::hash_combine(hash, uint64_t(op.getOperand(i)));

  return hash;
}


bool CsePass::resolveOverlappingLoads(ResourceKind kind, SsaDef descriptor) {
  small_vector<SsaDef, 64> uses;

  auto [a, b] = m_builder.getUses(descriptor);

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eBufferLoad)
      uses.push_back(iter->getDef());
  }

  std::sort(uses.begin(), uses.end(), [this] (SsaDef a, SsaDef b) {
    return m_dom.defDominates(a, b);
  });

  bool progress = false;

  for (size_t i = 0u; i < uses.size(); i++) {
    if (!uses.at(i))
      continue;

    for (size_t j = i + 1u; j < uses.size(); j++) {
      const auto& a = m_builder.getOp(uses.at(i));
      const auto& b = m_builder.getOp(uses.at(j));

      if (!a || !b || !loadsOverlap(a, b) || !m_dom.defDominates(a.getDef(), b.getDef()))
        continue;

      /* Load A dominates and overlaps load B. Replace both loads with the larger
       * of the two loads and replace all uses with composite ops as necessary. */
      std::tie(uses.at(i), uses.at(j)) = resolveLoads(kind, a, b);
      progress = true;
    }
  }

  return progress;
}


std::pair<SsaDef, SsaDef> CsePass::resolveLoads(ResourceKind kind, const Op& a, const Op& b) {
  dxbc_spv_assert(m_dom.defDominates(a.getDef(), b.getDef()));

  /* Classic CSE case, can still happen here */
  if (a.getType() == b.getType()) {
    m_builder.rewriteDef(b.getDef(), a.getDef());
    return std::make_pair(a.getDef(), SsaDef());
  }

  switch (kind) {
    case ResourceKind::eBufferRaw:
      return resolveLoadsRaw(a, b);

    case ResourceKind::eBufferStructured:
      return resolveLoadsStructured(a, b);

    default:
      dxbc_spv_unreachable();
      return std::make_pair(SsaDef(), SsaDef());
  }
}


std::pair<SsaDef, SsaDef> CsePass::resolveLoadsRaw(const Op& a, const Op& b) {
  std::array<SsaDef, 2> loads = { a.getDef(), b.getDef() };

  /* Fully scalarize raw loads, duplicates can be eliminated later */
  for (auto& load : loads) {
    const auto& loadOp = m_builder.getOp(load);
    auto loadBlock = m_dom.getBlockForDef(load);

    auto loadType = loadOp.getType().getBaseType(0u);
    auto baseType = loadType.getBaseType();

    if (loadType.isScalar())
      continue;

    auto baseAddress = SsaDef(loadOp.getOperand(1u));
    auto compositeOp = Op(OpCode::eCompositeConstruct, loadType);

    for (uint32_t i = 0u; i < loadType.getVectorSize(); i++) {
      auto address = baseAddress;
      auto addressType = m_builder.getOp(baseAddress).getType().getBaseType(0u);

      if (i) {
        address = m_builder.addBefore(load, Op::IAdd(
          addressType, baseAddress, makeTypedConstant(m_builder, addressType, i)));
        m_dom.setBlockForDef(address, loadBlock);
      }

      auto scalar = m_builder.addBefore(load, Op::BufferLoad(baseType,
        SsaDef(loadOp.getOperand(0u)), address, byteSize(baseType)).setFlags(loadOp.getFlags()));
      m_dom.setBlockForDef(scalar, loadBlock);
      compositeOp.addOperand(scalar);
    }

    m_builder.rewriteOp(load, std::move(compositeOp));
    load = SsaDef();
  }

  return std::make_pair(loads.at(0), loads.at(1));
}


std::pair<SsaDef, SsaDef> CsePass::resolveLoadsStructured(const Op& a, const Op& b) {
  auto addressA = m_builder.getOpForOperand(a, 1u);
  auto addressB = m_builder.getOpForOperand(b, 1u);

  auto aCount = addressA.getType().getBaseType(0u).getVectorSize();
  auto bCount = addressB.getType().getBaseType(0u).getVectorSize();

  auto minCount = std::min(aCount, bCount);
  auto maxCount = std::max(aCount, bCount);

  /* Replace small load with large load and extract composites as necessary.
   * Should be safe due to per-element robustness. Note that we can in some
   * cases load vectors from a scalar array, so we actually need to check
   * the laod type size here. */
  auto [smallLoad, largeLoad] = a.getType().byteSize() < b.getType().byteSize()
    ? std::make_pair(a.getDef(), b.getDef())
    : std::make_pair(b.getDef(), a.getDef());

  auto smallType = m_builder.getOp(smallLoad).getType();
  auto largeType = m_builder.getOp(largeLoad).getType();

  auto loadBlock = m_dom.getBlockForDef(smallLoad);

  /* We can duplicate the address op because they can only possibly
   * differ in constant IDs. There won't be any dominance issues. */
  auto newAddress = m_builder.getOpForOperand(largeLoad, 1u).getDef();

  if (!m_dom.defDominates(newAddress, smallLoad)) {
    newAddress = m_builder.addBefore(smallLoad, Op(m_builder.getOp(newAddress)));
    m_dom.setBlockForDef(newAddress, loadBlock);
  }

  auto newLoad = m_builder.addBefore(smallLoad, Op(m_builder.getOp(largeLoad)).setOperand(1u, newAddress));
  m_dom.setBlockForDef(newLoad, loadBlock);

  if (maxCount > minCount) {
    auto indexType = BasicType(ScalarType::eU32, maxCount - minCount);
    auto indexOp = Op(OpCode::eConstant, indexType);

    for (uint32_t i = minCount; i < maxCount; i++) {
      const auto& smallAddress = m_builder.getOpForOperand(smallLoad, 1u);

      if (smallAddress.isConstant()) {
        indexOp.addOperand(uint32_t(smallAddress.getOperand(i)));
      } else {
        dxbc_spv_assert(smallAddress.getOpCode() == OpCode::eCompositeConstruct);
        indexOp.addOperand(uint32_t(m_builder.getOpForOperand(smallAddress, i).getOperand(0)));
      }
    }

    m_builder.rewriteOp(smallLoad, Op::CompositeExtract(
      smallType, newLoad, m_builder.add(indexOp)));
  } else {
    dxbc_spv_assert(smallType.isBasicType() && largeType.isVectorType());

    /* Need to assemble the small vector type from the larger one */
    if (smallType.isVectorType()) {
      Op compositeOp(OpCode::eCompositeConstruct, smallType);

      for (uint32_t i = 0u; i < smallType.getBaseType(0u).getVectorSize(); i++) {
        auto scalar = m_builder.addBefore(smallLoad, Op::CompositeExtract(
          smallType.getBaseType(0u).getBaseType(), newLoad, m_builder.makeConstant(i)));
        m_dom.setBlockForDef(scalar, loadBlock);

        compositeOp.addOperand(scalar);
      }

      m_builder.rewriteOp(smallLoad, std::move(compositeOp));
    } else {
      m_builder.rewriteOp(smallLoad, Op::CompositeExtract(
        smallType.getBaseType(0u).getBaseType(), newLoad, m_builder.makeConstant(0)));
    }
  }

  return std::make_pair(
    a.getDef() != smallLoad ? a.getDef() : SsaDef(),
    b.getDef() != smallLoad ? b.getDef() : SsaDef());
}


bool CsePass::loadsOverlap(const Op& a, const Op& b) {
  dxbc_spv_assert(a.getOpCode() == OpCode::eBufferLoad && b.getOpCode() == OpCode::eBufferLoad);

  /* Descriptors must obviously be the same */
  if (a.getOperand(0u) != b.getOperand(0u))
    return false;

  /* If the address is identical, the only thing that can differ is the
   * load type for raw buffer loads, in which case they obviously overlap. */
  auto addressA = m_builder.getOpForOperand(a, 1u);
  auto addressB = m_builder.getOpForOperand(b, 1u);

  if (addressA.getDef() == addressB.getDef())
    return true;

  /* Assume constants are fully folded */
  if (addressA.isConstant() != addressB.isConstant())
    return false;

  if (addressA.isUndef() || addressB.isUndef())
    return false;

  if ((!addressA.isConstant() && addressA.getType().isVectorType() && addressA.getOpCode() != OpCode::eCompositeConstruct)
   || (!addressB.isConstant() && addressB.getType().isVectorType() && addressB.getOpCode() != OpCode::eCompositeConstruct))
    return false;

  /* Given that we either have scalars, constants or composites, we can now
   * simply compare the operands, which are either both IDs or both literals */
  auto aSize = addressA.getType().getBaseType(0u).getVectorSize();
  auto bSize = addressB.getType().getBaseType(0u).getVectorSize();

  for (uint32_t i = 0u; i < std::min(aSize, bSize); i++) {
    auto aScalar = Operand(addressA.getDef());
    auto bScalar = Operand(addressB.getDef());

    if (addressA.isConstant() || addressA.getType().isVectorType())
      aScalar = addressA.getOperand(i);

    if (addressB.isConstant() || addressB.getType().isVectorType())
      bScalar = addressB.getOperand(i);

    if (aScalar != bScalar)
      return false;
  }

  /* We can only promote partial loads if the last indices are all constant */
  if (!addressA.isConstant() && !addressB.isConstant()) {
    for (uint32_t i = std::min(aSize, bSize); i < std::max(aSize, bSize); i++) {
      if ((i < aSize && !m_builder.getOpForOperand(addressA, i).isConstant())
       || (i < bSize && !m_builder.getOpForOperand(addressB, i).isConstant()))
        return false;
    }
  }

  return true;
}


CseOpFlags CsePass::classifyOp(const Op& op) const {
  switch (op.getOpCode()) {
    /* Simple instructions that can be deduplicated */
    case OpCode::eConvertFtoF:
    case OpCode::eConvertFtoI:
    case OpCode::eConvertItoF:
    case OpCode::eConvertItoI:
    case OpCode::eConvertF32toPackedF16:
    case OpCode::eConvertPackedF16toF32:
    case OpCode::eCast:
    case OpCode::eConsumeAs:
    case OpCode::eCompositeExtract:
    case OpCode::eCompositeConstruct:
    case OpCode::eCheckSparseAccess:
    case OpCode::eParamLoad:
    case OpCode::ePushDataLoad:
    case OpCode::eInputTargetLoad:
    case OpCode::eInputLoad:
    case OpCode::eDescriptorLoad:
    case OpCode::eBufferQuerySize:
    case OpCode::eImageQuerySize:
    case OpCode::eImageQueryMips:
    case OpCode::eImageQuerySamples:
    case OpCode::eImageSample:
    case OpCode::eImageGather:
    case OpCode::eImageComputeLod:
    case OpCode::eConstantLoad:
    case OpCode::ePointer:
    case OpCode::eInterpolateAtCentroid:
    case OpCode::eInterpolateAtSample:
    case OpCode::eInterpolateAtOffset:
    case OpCode::eDerivX:
    case OpCode::eDerivY:
    case OpCode::eFEq:
    case OpCode::eFNe:
    case OpCode::eFLt:
    case OpCode::eFLe:
    case OpCode::eFGt:
    case OpCode::eFGe:
    case OpCode::eFIsNan:
    case OpCode::eIEq:
    case OpCode::eINe:
    case OpCode::eSLt:
    case OpCode::eSLe:
    case OpCode::eSGt:
    case OpCode::eSGe:
    case OpCode::eULt:
    case OpCode::eULe:
    case OpCode::eUGt:
    case OpCode::eUGe:
    case OpCode::eBAnd:
    case OpCode::eBOr:
    case OpCode::eBEq:
    case OpCode::eBNe:
    case OpCode::eBNot:
    case OpCode::eSelect:
    case OpCode::eFAbs:
    case OpCode::eFNeg:
    case OpCode::eFAdd:
    case OpCode::eFSub:
    case OpCode::eFMul:
    case OpCode::eFMulLegacy:
    case OpCode::eFMad:
    case OpCode::eFMadLegacy:
    case OpCode::eFDiv:
    case OpCode::eFRcp:
    case OpCode::eFSqrt:
    case OpCode::eFRsq:
    case OpCode::eFExp2:
    case OpCode::eFLog2:
    case OpCode::eFLog2Legacy:
    case OpCode::eFFract:
    case OpCode::eFRound:
    case OpCode::eFMin:
    case OpCode::eFMax:
    case OpCode::eFDot:
    case OpCode::eFDotLegacy:
    case OpCode::eFDotAdd:
    case OpCode::eFDotAddLegacy:
    case OpCode::eFClamp:
    case OpCode::eFSin:
    case OpCode::eFCos:
    case OpCode::eFPow:
    case OpCode::eFPowLegacy:
    case OpCode::eFSgn:
    case OpCode::eIAnd:
    case OpCode::eIOr:
    case OpCode::eIXor:
    case OpCode::eINot:
    case OpCode::eIBitInsert:
    case OpCode::eUBitExtract:
    case OpCode::eSBitExtract:
    case OpCode::eIShl:
    case OpCode::eSShr:
    case OpCode::eUShr:
    case OpCode::eIBitCount:
    case OpCode::eIBitReverse:
    case OpCode::eIFindLsb:
    case OpCode::eSFindMsb:
    case OpCode::eUFindMsb:
    case OpCode::eIAdd:
    case OpCode::eIAddCarry:
    case OpCode::eISub:
    case OpCode::eISubBorrow:
    case OpCode::eIAbs:
    case OpCode::eINeg:
    case OpCode::eIMul:
    case OpCode::eSMulExtended:
    case OpCode::eUMulExtended:
    case OpCode::eUDiv:
    case OpCode::eUMod:
    case OpCode::eSMin:
    case OpCode::eSMax:
    case OpCode::eSClamp:
    case OpCode::eUMin:
    case OpCode::eUMax:
    case OpCode::eUClamp:
    case OpCode::eUMSad:
      return CseOpFlag::eCanDeduplicate;

    case OpCode::eBufferLoad:
    case OpCode::eImageLoad: {
      /* Eliminate redundant loads if the resource is read-only */
      const auto& descriptorOp = m_builder.getOpForOperand(op, 0u);

      bool isPure = descriptorOp.getType() != ScalarType::eUav;
      return isPure ? CseOpFlag::eCanDeduplicate : CseOpFlag::eHasSideEffects;
    }

    case OpCode::eFunctionCall: {
      const auto& function = m_builder.getOpForOperand(op, 0u);

      bool isPure = m_pureFunctions.find(function.getDef()) != m_pureFunctions.end();
      return isPure ? CseOpFlag::eCanDeduplicate : CseOpFlag::eHasSideEffects;
    }

    /* Phi needs special treatment due to forward references */
    case OpCode::ePhi:
      return CseOpFlags();

    /* Control flow and function declarations */
    case OpCode::eFunction:
    case OpCode::eFunctionEnd:
    case OpCode::eLabel:
    case OpCode::eBranch:
    case OpCode::eBranchConditional:
    case OpCode::eSwitch:
    case OpCode::eUnreachable:
    case OpCode::eReturn:
      return CseOpFlags();

    /* Instructions with observable side effects */
    case OpCode::eBarrier:
    case OpCode::eScratchLoad:
    case OpCode::eScratchStore:
    case OpCode::eLdsLoad:
    case OpCode::eLdsStore:
    case OpCode::eOutputLoad:
    case OpCode::eOutputStore:
    case OpCode::eBufferStore:
    case OpCode::eMemoryLoad:
    case OpCode::eMemoryStore:
    case OpCode::eLdsAtomic:
    case OpCode::eBufferAtomic:
    case OpCode::eImageAtomic:
    case OpCode::eCounterAtomic:
    case OpCode::eMemoryAtomic:
    case OpCode::eImageStore:
    case OpCode::eEmitVertex:
    case OpCode::eEmitPrimitive:
    case OpCode::eDemote:
    case OpCode::eRovScopedLockBegin:
    case OpCode::eRovScopedLockEnd:
      return CseOpFlag::eHasSideEffects;

    /* Optimization barrier */
    case OpCode::eDrain:
      return CseOpFlags();

    /* Declarative ops that we shouldn't reach */
    case OpCode::eEntryPoint:
    case OpCode::eSemantic:
    case OpCode::eDebugName:
    case OpCode::eDebugMemberName:
    case OpCode::eConstant:
    case OpCode::eUndef:
    case OpCode::eSetCsWorkgroupSize:
    case OpCode::eSetGsInstances:
    case OpCode::eSetGsInputPrimitive:
    case OpCode::eSetGsOutputVertices:
    case OpCode::eSetGsOutputPrimitive:
    case OpCode::eSetPsEarlyFragmentTest:
    case OpCode::eSetPsDepthGreaterEqual:
    case OpCode::eSetPsDepthLessEqual:
    case OpCode::eSetTessPrimitive:
    case OpCode::eSetTessDomain:
    case OpCode::eSetTessControlPoints:
    case OpCode::eSetFpMode:
    case OpCode::eDclInput:
    case OpCode::eDclInputBuiltIn:
    case OpCode::eDclOutput:
    case OpCode::eDclOutputBuiltIn:
    case OpCode::eDclSpecConstant:
    case OpCode::eDclPushData:
    case OpCode::eDclSampler:
    case OpCode::eDclCbv:
    case OpCode::eDclSrv:
    case OpCode::eDclUav:
    case OpCode::eDclUavCounter:
    case OpCode::eDclLds:
    case OpCode::eDclScratch:
    case OpCode::eDclTmp:
    case OpCode::eDclParam:
    case OpCode::eDclXfb:
    case OpCode::eDclInputTarget:
      break;

    /* Instructions that must be lowered by now */
    case OpCode::eScopedIf:
    case OpCode::eScopedElse:
    case OpCode::eScopedEndIf:
    case OpCode::eScopedLoop:
    case OpCode::eScopedLoopBreak:
    case OpCode::eScopedLoopContinue:
    case OpCode::eScopedEndLoop:
    case OpCode::eScopedSwitch:
    case OpCode::eScopedSwitchCase:
    case OpCode::eScopedSwitchDefault:
    case OpCode::eScopedSwitchBreak:
    case OpCode::eScopedEndSwitch:
    case OpCode::eTmpLoad:
    case OpCode::eTmpStore:
      break;

    /* Invalid opcodes */
    case OpCode::eUnknown:
    case OpCode::eLastDeclarative:
    case OpCode::Count:
      break;
  }

  dxbc_spv_unreachable();
  return CseOpFlag();
}


bool CsePass::isTrivialOp(const Op& op) const {
  if (op.getOpCode() == OpCode::eDescriptorLoad)
    return m_options.relocateDescriptorLoad;

  return op.getOpCode() == OpCode::eCast ||
         op.getOpCode() == OpCode::eCompositeConstruct ||
         op.getOpCode() == OpCode::eCompositeExtract ||
         op.getOpCode() == OpCode::eInputLoad ||
         op.getOpCode() == OpCode::ePushDataLoad;
}

}
