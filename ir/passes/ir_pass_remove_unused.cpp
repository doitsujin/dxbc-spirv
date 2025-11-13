#include "ir_pass_remove_unused.h"

namespace dxbc_spv::ir {

RemoveUnusedPass::RemoveUnusedPass(Builder& builder)
: m_builder(builder) {

}


RemoveUnusedPass::~RemoveUnusedPass() {

}


void RemoveUnusedPass::run() {
  /* Gather ops that are unused as-is */
  std::vector<SsaDef> ops;

  for (const auto& op : m_builder) {
    if (canRemoveOp(op))
      ops.push_back(op.getDef());
  }

  /* Recursively remove unused operations and any of their
   * operands that may become unused as a result. */
  while (!ops.empty()) {
    Op op = m_builder.getOp(ops.back());
    ops.pop_back();

    /* We might enqueue some ops redundantly if they are used in an
     * instruction multiple times. Since this pass does not change
     * the definitions of existing ops, this is safe. */
    if (!op)
      continue;

    removeOp(op.getDef());

    for (uint32_t i = 0u; i < op.getFirstLiteralOperandIndex(); i++) {
      auto operand = SsaDef(op.getOperand(i));

      if (canRemoveOp(m_builder.getOp(operand)))
        ops.push_back(operand);
    }
  }
}


void RemoveUnusedPass::removeUnusedFloatModes() {
  SsaDef f16Mode = { };
  SsaDef f32Mode = { };
  SsaDef f64Mode = { };

  bool usesF16 = false;
  bool usesF32 = false;
  bool usesF64 = false;

  for (const auto& op : m_builder) {
    if (op.getOpCode() == OpCode::eSetFpMode) {
      /* Scan FP mode declarations */
      if (op.getType() == ScalarType::eF16)
        f16Mode = op.getDef();
      else if (op.getType() == ScalarType::eF32)
        f32Mode = op.getDef();
      else if (op.getType() == ScalarType::eF64)
        f64Mode = op.getDef();
    } else {
      /* Scan return type of each instruction */
      for (uint32_t i = 0u; i < op.getType().getStructMemberCount(); i++) {
        auto scalarType = op.getType().getBaseType(i).getBaseType();
        usesF16 = usesF16 || scalarType == ScalarType::eF16;
        usesF32 = usesF32 || scalarType == ScalarType::eF32;
        usesF64 = usesF64 || scalarType == ScalarType::eF64;
      }
    }

    /* If we finished scanning declarations, exit early once we
     * know that all declared float modes are actively used */
    if (!op.isDeclarative() &&
        (usesF16 || !f16Mode) &&
        (usesF32 || !f32Mode) &&
        (usesF64 || !f64Mode))
      return;
  }

  if (f16Mode && !usesF16)
    m_builder.remove(f16Mode);

  if (f32Mode && !usesF32)
    m_builder.remove(f32Mode);

  if (f64Mode && !usesF64)
    m_builder.remove(f64Mode);
}


void RemoveUnusedPass::runPass(Builder& builder) {
  RemoveUnusedPass(builder).run();
}


void RemoveUnusedPass::runRemoveUnusedFloatModePass(Builder& builder) {
  RemoveUnusedPass(builder).removeUnusedFloatModes();
}


bool RemoveUnusedPass::canRemoveOp(const Op& op) const {
  if (!op)
    return false;

  auto [a, b] = m_builder.getUses(op.getDef());

  for (auto use = a; use != b; use++) {
    if (use->getOpCode() != OpCode::eDebugName &&
        use->getOpCode() != OpCode::eDebugMemberName &&
        use->getOpCode() != OpCode::eSemantic)
      return false;
  }

  return !hasSideEffect(op.getOpCode());
}


void RemoveUnusedPass::removeOp(SsaDef def) {
  util::small_vector<SsaDef, 16u> uses;
  m_builder.getUses(def, uses);

  for (auto use : uses) {
    dxbc_spv_assert(m_builder.getOp(use).getOpCode() == OpCode::eDebugName ||
                    m_builder.getOp(use).getOpCode() == OpCode::eDebugMemberName ||
                    m_builder.getOp(use).getOpCode() == OpCode::eSemantic);

    m_builder.remove(use);
  }

  m_builder.remove(def);
}


bool RemoveUnusedPass::hasSideEffect(OpCode opCode) {
  switch (opCode) {
    /* Invalid opcodes */
    case OpCode::eUnknown:
    case OpCode::eLastDeclarative:
    case OpCode::Count:
      break;

    /* Debug instructions for instructions that may be in use. We remove these
     * explicitly when removing instructions that are actually not in use. */
    case OpCode::eDebugName:
    case OpCode::eDebugMemberName:

    /* Declarations that we need in the final binary */
    case OpCode::eEntryPoint:
    case OpCode::eSemantic:
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
    case OpCode::eDclOutput:
    case OpCode::eDclOutputBuiltIn:
    case OpCode::eDclXfb:
    case OpCode::eFunction:
    case OpCode::eFunctionEnd:

    /* Keep calls since functions may have side effects */
    case OpCode::eFunctionCall:

    /* Structured control flow instructions */
    case OpCode::eLabel:
    case OpCode::eBranch:
    case OpCode::eBranchConditional:
    case OpCode::eSwitch:
    case OpCode::eUnreachable:
    case OpCode::eReturn:

    /* Scoped control flow instructions */
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

    /* Synchronization */
    case OpCode::eBarrier:

    /* Store ops */
    case OpCode::eTmpStore:
    case OpCode::eScratchStore:
    case OpCode::eLdsStore:
    case OpCode::eOutputStore:
    case OpCode::eBufferStore:
    case OpCode::eMemoryStore:
    case OpCode::eImageStore:

    /* Atomics */
    case OpCode::eLdsAtomic:
    case OpCode::eBufferAtomic:
    case OpCode::eImageAtomic:
    case OpCode::eCounterAtomic:
    case OpCode::eMemoryAtomic:

    /* GS vertex / primitive export */
    case OpCode::eEmitVertex:
    case OpCode::eEmitPrimitive:

    /* PS demote */
    case OpCode::eDemote:

    /* ROV lock */
    case OpCode::eRovScopedLockBegin:
    case OpCode::eRovScopedLockEnd:

    /* Debug drain */
    case OpCode::eDrain:
      return true;


    /* Declarations that can be removed if unused */
    case OpCode::eUndef:
    case OpCode::eConstant:
    case OpCode::eDclInput:
    case OpCode::eDclInputBuiltIn:
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

    /* Memory loads with no side effect */
    case OpCode::eParamLoad:
    case OpCode::eTmpLoad:
    case OpCode::eScratchLoad:
    case OpCode::eLdsLoad:
    case OpCode::ePushDataLoad:
    case OpCode::eInputLoad:
    case OpCode::eOutputLoad:
    case OpCode::eDescriptorLoad:
    case OpCode::eBufferLoad:
    case OpCode::eBufferQuerySize:
    case OpCode::eMemoryLoad:
    case OpCode::eConstantLoad:
    case OpCode::eImageLoad:
    case OpCode::eImageQuerySize:
    case OpCode::eImageQueryMips:
    case OpCode::eImageQuerySamples:
    case OpCode::eImageSample:
    case OpCode::eImageGather:
    case OpCode::eImageComputeLod:

    /* Basic instructions */
    case OpCode::ePhi:

    case OpCode::eCheckSparseAccess:

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
      return false;
  }

  dxbc_spv_assert(false);
  return true;
}

}
