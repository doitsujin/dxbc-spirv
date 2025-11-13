#include "ir_divergence.h"

#include "../util/util_log.h"

namespace dxbc_spv::ir {

DivergenceAnalysis::DivergenceAnalysis(const Builder& builder, const DominanceGraph& dominance)
: m_builder(builder), m_dominance(dominance) {
  m_nodeScopes.ensure(m_builder.getMaxValidDef());

  gatherMetadata();

  while (runAnalysisPass())
    continue;
}


DivergenceAnalysis::~DivergenceAnalysis() {

}


bool DivergenceAnalysis::runAnalysisPass() {
  bool progress = false;

  for (const auto& op : m_builder)
    progress |= adjustScopeForDef(op.getDef(), determineScope(op));

  return progress;
}


Scope DivergenceAnalysis::determineScope(const Op& op) {
  switch (op.getOpCode()) {
    /* Constants are always globally uniform */
    case OpCode::eConstant:
    case OpCode::eUndef:
      return Scope::eGlobal;

    /* For pure functions, we adjust the scope based on its returns. For
     * now, assume that a function with non-void return has the same level
     * of divergence as its inputs. */
    case OpCode::eFunction: {
      m_currentFunction = op.getDef();
    } return Scope::eGlobal;

    case OpCode::eFunctionEnd: {
      m_currentFunction = SsaDef();
    } return Scope::eGlobal;

    /* For returns, take both the input and execution scope into account
     * and feed the resulting scope back into the function definition */
    case OpCode::eReturn: {
      auto scope = determineScopeForArgsAndBlock(op);
      adjustScopeForDef(m_currentFunction, scope);

      m_currentBlock = op.getDef();
      return scope;
    }

    /* Blocks depend on overall control flow properties */
    case OpCode::eLabel: {
      auto scope = determineScopeForBlock(op);

      m_currentBlock = op.getDef();
      return scope;
    }

    /* For branches, only consider the scope of the condition (if any)
     * as well as the block they are executed from. */
    case OpCode::eBranch:
    case OpCode::eUnreachable: {
      auto scope = getUniformScopeForDef(m_currentBlock);

      m_currentBlock = SsaDef();
      return scope;
    }

    case OpCode::eSwitch:
    case OpCode::eBranchConditional: {
      auto scope = std::min(getUniformScopeForDef(m_currentBlock),
        getUniformScopeForDef(SsaDef(op.getOperand(0u))));

      m_currentBlock = SsaDef();
      return scope;
    }

    case OpCode::eDemote:
    case OpCode::eEmitPrimitive:
    case OpCode::eEmitVertex:
    case OpCode::eRovScopedLockBegin:
    case OpCode::eRovScopedLockEnd: {
      taintFunction(m_currentFunction);
      return getUniformScopeForDef(m_currentBlock);
    }

    /* For sync instructions, use the same scope as for the result */
    case OpCode::eBarrier: {
      taintFunction(m_currentFunction);

      auto execScope = getUniformScopeForDef(m_currentBlock);
      auto syncScope = Scope(op.getOperand(0u));

      if (syncScope > execScope && syncScope >= Scope::eWorkgroup) {
        if (!std::exchange(m_hasNonUniformBarrier, true))
          Logger::warn("Barrier ", op.getDef(), " with scope ", syncScope, " found in block with scope ", execScope);
      }

      return execScope;
    }

    /* Consider all atomics divergent */
    case OpCode::eLdsAtomic:
    case OpCode::eBufferAtomic:
    case OpCode::eImageAtomic:
    case OpCode::eCounterAtomic:
    case OpCode::eMemoryAtomic: {
      taintFunction(m_currentFunction);
      return Scope::eThread;
    }

    /* Technically pessimizing, but this would only not be true when
     * interpolating flat inputs which should never happen to begin with. */
    case OpCode::eInterpolateAtCentroid:
    case OpCode::eInterpolateAtSample:
    case OpCode::eInterpolateAtOffset:
      return Scope::eThread;

    /* Never consider UAV loads more than subgroup-uniform if they are writable.
     * They could be workgroup-uniform, but we'd have to analyze barriers. */
    case OpCode::eBufferLoad:
    case OpCode::eImageLoad: {
      Scope scope = Scope::eGlobal;
      const auto& descriptorOp = m_builder.getOpForOperand(op, 0u);

      if (descriptorOp.getType() == ScalarType::eUav) {
        const auto& uavOp = m_builder.getOpForOperand(descriptorOp, 0u);
        auto uavFlags = UavFlags(uavOp.getOperand(uavOp.getFirstLiteralOperandIndex() + 4u));

        if (!(uavFlags & UavFlag::eReadOnly)) {
          taintFunction(m_currentFunction);
          scope = std::min(scope, Scope::eSubgroup);
        }
      }

      return std::min(scope, determineScopeForArgs(op));
    }

    case OpCode::eMemoryLoad: {
      taintFunction(m_currentFunction);
      return std::min(Scope::eSubgroup, determineScopeForArgs(op));
    }

    /* Derivatives are special: Coarse derivatives will always be quad-uniform,
     * and derivatives of quad-uniform inputs will be constant zero. */
    case OpCode::eDerivX:
    case OpCode::eDerivY: {
      const auto& arg = m_builder.getOpForOperand(op, 0u);

      if (determineScope(arg) >= Scope::eQuad)
        return Scope::eGlobal;

      if (DerivativeMode(op.getOperand(1u)) == DerivativeMode::eCoarse)
        return Scope::eQuad;

      return Scope::eThread;
    }

    /* When calling a tainted function, feed scopes back to the function
     * itself as well as its parameters. */
    case OpCode::eFunctionCall: {
      const auto& func = m_builder.getOpForOperand(op, 0u);
      auto& funcInfo = m_nodeScopes.at(func.getDef());

      /* Need to taint caller if the callee is not pure */
      if (funcInfo.tainted) {
        taintFunction(m_currentFunction);

        funcInfo.callScope = std::min(funcInfo.callScope,
          getUniformScopeForDef(m_currentBlock));
      }

      /* Function could return divergent results even for uniform inputs */
      auto scope = getUniformScopeForDef(func.getDef());

      for (uint32_t i = 1u; i < op.getFirstLiteralOperandIndex(); i++) {
        auto paramScope = getUniformScopeForDef(SsaDef(op.getOperand(i)));

        if (funcInfo.tainted)
          adjustScopeForDef(SsaDef(func.getOperand(i - 1u)), paramScope);

        scope = std::min(scope, paramScope);
      }

      return scope;
    }

    /* For most instructions, the scope at which results diverge is equivalent to
     * the smallest scope of any of the inputs. This includes phi instructions,
     * where we consider the scope of each value and its corresponding block. */
    case OpCode::ePhi:
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
    case OpCode::eConstantLoad:
    case OpCode::ePushDataLoad:
    case OpCode::eDescriptorLoad:
    case OpCode::eBufferQuerySize:
    case OpCode::eImageQuerySize:
    case OpCode::eImageQueryMips:
    case OpCode::eImageQuerySamples:
    case OpCode::eImageSample:
    case OpCode::eImageGather:
    case OpCode::eImageComputeLod:
    case OpCode::ePointer:
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
    case OpCode::eDrain:
      return determineScopeForArgs(op);

    /* Loads that observe side effects from other functions still
     * need to take callers into account */
    case OpCode::eLdsLoad:
    case OpCode::eScratchLoad:
    case OpCode::eOutputLoad: {
      taintFunction(m_currentFunction);
      return determineScopeForArgsAndBlock(op);
    }

    /* For stores, we additionally need to take the scope of the current block
     * into account. For scratch and output variables, feed the resulting scope
     * back into the variable definition so that loads can be adjusted. */
    case OpCode::eLdsStore:
    case OpCode::eBufferStore:
    case OpCode::eImageStore:
    case OpCode::eMemoryStore: {
      taintFunction(m_currentFunction);
      return determineScopeForArgsAndBlock(op);
    }

    case OpCode::eOutputStore:
    case OpCode::eScratchStore: {
      taintFunction(m_currentFunction);

      auto scope = determineScopeForArgsAndBlock(op);
      adjustScopeForDef(m_builder.getOpForOperand(op, 0u).getDef(), scope);
      return scope;
    }

    /* Inputs depend on their interpolation mode */
    case OpCode::eDclInput:
      return determineScopeForInput(op);

    /* Built-ins vary */
    case OpCode::eDclInputBuiltIn:
      return determineScopeForBuiltIn(op);

    /* Input loads can be more uniform than the declaration in some cases */
    case OpCode::eInputLoad:
      return determineScopeForInputLoad(op);

    /* LDS is workgroup-uniform by definition */
    case OpCode::eDclLds:
      return Scope::eWorkgroup;

    /* Declarative instructions with no real meaning for uniformity purposes */
    case OpCode::eEntryPoint:
    case OpCode::eSemantic:
    case OpCode::eDebugName:
    case OpCode::eDebugMemberName:
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
    case OpCode::eDclSpecConstant:
    case OpCode::eDclPushData:
    case OpCode::eDclSampler:
    case OpCode::eDclCbv:
    case OpCode::eDclSrv:
    case OpCode::eDclUav:
    case OpCode::eDclUavCounter:
    case OpCode::eDclScratch:
    case OpCode::eDclTmp:
    case OpCode::eDclParam:
    case OpCode::eDclXfb:
      return Scope::eGlobal;

    case OpCode::eDclOutput:
    case OpCode::eDclOutputBuiltIn: {
      /* For GS outputs, we'd need to prove that outputs receive the same
       * value sequentially, so just give up and mark as divergent */
      if (m_stage == ShaderStage::eGeometry)
        return Scope::eThread;
    } return Scope::eGlobal;

    /* Invalid opcodes */
    case OpCode::eUnknown:
    case OpCode::eLastDeclarative:
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
    case OpCode::Count:
      break;
  }

  dxbc_spv_unreachable();
  return Scope::eThread;
}


Scope DivergenceAnalysis::determineScopeForArgs(const Op& op) {
  Scope scope = Scope::eGlobal;

  for (uint32_t i = 0u; i < op.getFirstLiteralOperandIndex(); i++)
    scope = std::min(scope, getUniformScopeForDef(SsaDef(op.getOperand(i))));

  return scope;
}


Scope DivergenceAnalysis::determineScopeForArgsAndBlock(const Op& op) {
  Scope scope = getUniformScopeForDef(m_currentBlock);

  if (scope == Scope::eThread)
    return scope;

  return std::min(scope, determineScopeForArgs(op));
}


Scope DivergenceAnalysis::determineScopeForInputLoad(const Op& op) {
  const auto& input = m_builder.getOpForOperand(op, 0u);
  const auto& address = m_builder.getOpForOperand(op, 1u);

  if (input.getOpCode() == OpCode::eDclInputBuiltIn) {
    auto builtIn = BuiltIn(input.getOperand(1u));

    /* Thread IDs are uniform in any dimension where the workgroup size is 1. */
    if (address && (builtIn == BuiltIn::eGlobalThreadId || builtIn == BuiltIn::eLocalThreadId)) {
      dxbc_spv_assert(address.isConstant());

      uint32_t index = uint32_t(address.getOperand(0u));

      if ((index == 0u && m_workgroupSizeX == 1u) ||
          (index == 1u && m_workgroupSizeY == 1u) ||
          (index == 2u && m_workgroupSizeZ == 1u))
        return Scope::eWorkgroup;
    }
  }

  return determineScopeForArgs(op);
}


Scope DivergenceAnalysis::determineScopeForInput(const Op& op) {
  /* Non-interpolated pixel shader inputs are quad-uniform */
  if (m_stage == ShaderStage::ePixel) {
    auto interpolation = InterpolationModes(op.getOperand(op.getFirstLiteralOperandIndex() + 2u));

    if (interpolation == InterpolationMode::eFlat)
      return Scope::eQuad;
  }

  /* Tessellation and geometry shader inputs are the same for the
   * entire input primitive being processed */
  if (m_stage == ShaderStage::eHull ||
      m_stage == ShaderStage::eDomain ||
      m_stage == ShaderStage::eGeometry)
    return Scope::eQuad;

  return Scope::eThread;
}


Scope DivergenceAnalysis::determineScopeForBuiltIn(const Op& op) {
  auto builtIn = BuiltIn(op.getOperand(1u));

  InterpolationModes interpolation = { };

  if (m_stage == ShaderStage::ePixel)
    interpolation = InterpolationModes(op.getOperand(op.getFirstLiteralOperandIndex() + 1u));

  switch (builtIn) {
    /* Treat position as a regular input, but ignore interpolation.
     * Point size cannot be read in pixel shaders anyway. */
    case BuiltIn::ePosition:
    case BuiltIn::ePointSize: {
      if (m_stage == ShaderStage::eHull ||
          m_stage == ShaderStage::eGeometry)
        return Scope::eQuad;
    } return Scope::eThread;

    /* Treat clip/cull as a regular input */
    case BuiltIn::eClipDistance:
    case BuiltIn::eCullDistance: {
      if (interpolation == InterpolationMode::eFlat ||
          m_stage == ShaderStage::eHull ||
          m_stage == ShaderStage::eGeometry)
        return Scope::eQuad;
    } return Scope::eThread;

    /* Inputs that are divergent in nature */
    case BuiltIn::eVertexId:
    case BuiltIn::eTessControlPointId:
    case BuiltIn::eTessCoord:
    case BuiltIn::eSampleMask:
    case BuiltIn::eIsFullyCovered:
      return Scope::eThread;

    /* Per-primitive inputs, including in geometry stages. Use quad scope
     * to denote this, even if it has nothing to do with subgroup quads
     * outside of pixel shaders. */
    case BuiltIn::ePrimitiveId:
    case BuiltIn::eInstanceId:
    case BuiltIn::eGsInstanceId:
    case BuiltIn::eTessFactorInner:
    case BuiltIn::eTessFactorOuter:
    case BuiltIn::eLayerIndex:
    case BuiltIn::eViewportIndex:
    case BuiltIn::eIsFrontFace:
      return Scope::eQuad;

    /* Globally uniform inputs */
    case BuiltIn::eGsVertexCountIn:
    case BuiltIn::eTessControlPointCountIn:
    case BuiltIn::eSampleCount:
      return Scope::eGlobal;

    /* Quads all compute the same sample in different pixels */
    case BuiltIn::eSampleId:
    case BuiltIn::eSamplePosition:
      return Scope::eQuad;

    /* Workgroup-uniform */
    case BuiltIn::eWorkgroupId:
      return Scope::eWorkgroup;

    /* Workgroup-uniform if thread count is 1 */
    case BuiltIn::eGlobalThreadId:
    case BuiltIn::eLocalThreadId:
    case BuiltIn::eLocalThreadIndex: {
      return (m_workgroupSizeX * m_workgroupSizeY * m_workgroupSizeZ == 1u)
        ? Scope::eWorkgroup
        : Scope::eThread;
    }

    /* Not valid as inputs */
    case BuiltIn::eDepth:
    case BuiltIn::eStencilRef:
      break;
  }

  dxbc_spv_unreachable();
  return Scope::eThread;
}


Scope DivergenceAnalysis::determineScopeForBlock(const Op& op) {
  /* For merge blocks, if the block immediately post-dominates its header,
   * i.e. if none of the conditional branches of a structure return, then
   * assume that the block inherits the divergence properties of its header.
   *
   * This is not entirely correct for subgroup-uniform control flow, however
   * we can manually insert barriers or enable maximum reconvergence when
   * lowering to SPIR-V if that ever becomes an issue. */
  Scope functionScope = m_nodeScopes.at(m_currentFunction).callScope;

  if (m_patchConstantFunction == m_currentFunction)
    functionScope = std::min(functionScope, Scope::eQuad);

  Scope blockScope = functionScope;

  auto [a, b] = m_builder.getUses(op.getDef());

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eLabel) {
      auto construct = Construct(iter->getOperand(iter->getFirstLiteralOperandIndex()));

      const auto& mergeBlock = m_builder.getOpForOperand(*iter, 0u);

      if (op.getDef() == mergeBlock.getDef() && m_dominance.postDominates(op.getDef(), iter->getDef())) {
        /* Inherit scope from structured selection header */
        if (construct == Construct::eStructuredSelection)
          return getUniformScopeForDef(iter->getDef());

        if (construct == Construct::eStructuredLoop) {
         /* For loops, things get more complicated since the continue condition may
          * be non-uniform, but the loop merge may be uniform regardless. We need to
          * look at branches targeting the loop header that are not the continue block. */
          const auto& continueBlock = m_builder.getOpForOperand(*iter, 1u);

          Scope scope = functionScope;

          auto [a, b] = m_builder.getUses(iter->getDef());

          for (auto use = a; use != b; use++) {
            auto block = m_dominance.getBlockForDef(use->getDef());

            if (block != continueBlock.getDef())
              scope = std::min(scope, getUniformScopeForDef(use->getDef()));
          }

          return scope;
        }
      }

      if (construct == Construct::eStructuredLoop) {
        /* A loop iteration will either continue or break/return, thus the loop
         * exit condition has the same scope as the loop continue condition. */
        const auto& continueBlock = m_builder.getOpForOperand(*iter, 1u);

        if (op.getDef() == continueBlock.getDef())
          return getLoopExitScope(*iter);
      }
    } else if (isBranchInstruction(iter->getOpCode())) {
      /* Inherit scope from all branches targeting the block */
      blockScope = std::min(blockScope, getUniformScopeForDef(iter->getDef()));
    }
  }

  return blockScope;
}


Scope DivergenceAnalysis::getLoopExitScope(const Op& loopHeader) {
  /* Check divergence of any of the branches targeting the merge block */
  const auto& mergeBlock = m_builder.getOpForOperand(loopHeader, 0u);
  Scope scope = getUniformScopeForDef(mergeBlock.getDef());

  auto [a, b] = m_builder.getUses(mergeBlock.getDef());

  for (auto iter = a; iter != b; iter++) {
    if (isBranchInstruction(iter->getOpCode()))
      scope = std::min(scope, getUniformScopeForDef(iter->getDef()));
  }

  return scope;
}


void DivergenceAnalysis::taintFunction(SsaDef function) {
  m_nodeScopes.at(function).tainted = true;
}


void DivergenceAnalysis::gatherMetadata() {
  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    switch (iter->getOpCode()) {
      case OpCode::eEntryPoint: {
        m_stage = ShaderStage(iter->getOperand(iter->getFirstLiteralOperandIndex()));

        if (m_stage == ShaderStage::eHull)
          m_patchConstantFunction = SsaDef(iter->getOperand(1u));
      } break;

      case OpCode::eSetCsWorkgroupSize: {
        m_workgroupSizeX = uint32_t(iter->getOperand(iter->getFirstLiteralOperandIndex() + 0u));
        m_workgroupSizeY = uint32_t(iter->getOperand(iter->getFirstLiteralOperandIndex() + 1u));
        m_workgroupSizeZ = uint32_t(iter->getOperand(iter->getFirstLiteralOperandIndex() + 2u));
      } break;

      default:
        break;
    }
  }
}

}
