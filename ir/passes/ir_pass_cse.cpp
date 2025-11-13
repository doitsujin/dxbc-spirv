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
  auto iter = m_builder.getCode().first;

  std::vector<SsaDef> blockList;

  while (iter != m_builder.getCode().second) {
    auto opType = classifyOp(*iter);

    if (opType & CseOpFlag::eHasSideEffects)
      m_functionIsPure = false;

    if (opType & CseOpFlag::eCanDeduplicate) {
      bool isTrivial = isTrivialOp(*iter);
      auto [a, b] = m_defs.equal_range(*iter);

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

      m_defs.insert(*iter);
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
    case OpCode::eCompositeInsert:
    case OpCode::eCompositeExtract:
    case OpCode::eCompositeConstruct:
    case OpCode::eCheckSparseAccess:
    case OpCode::eParamLoad:
    case OpCode::ePushDataLoad:
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
    case OpCode::eFFract:
    case OpCode::eFRound:
    case OpCode::eFMin:
    case OpCode::eFMax:
    case OpCode::eFDot:
    case OpCode::eFDotLegacy:
    case OpCode::eFClamp:
    case OpCode::eFSin:
    case OpCode::eFCos:
    case OpCode::eFPow:
    case OpCode::eFPowLegacy:
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
         op.getOpCode() == OpCode::eCompositeInsert ||
         op.getOpCode() == OpCode::eInputLoad ||
         op.getOpCode() == OpCode::ePushDataLoad;
}

}
