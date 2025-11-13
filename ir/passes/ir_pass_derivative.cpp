#include "ir_pass_derivative.h"

#include "../ir_utils.h"

namespace dxbc_spv::ir {

DerivativePass::DerivativePass(Builder& builder, const Options& options)
: m_builder(builder), m_options(options) {
  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eEntryPoint) {
      m_stage = ShaderStage(iter->getOperand(iter->getFirstLiteralOperandIndex()));
      break;
    }
  }

  if (m_stage != ShaderStage::ePixel)
    return;

  m_dominance.emplace(m_builder);
  m_divergence.emplace(m_builder, *m_dominance);
}


DerivativePass::~DerivativePass() {

}


bool DerivativePass::run() {
  bool status = false;

  if (m_stage != ShaderStage::ePixel)
    return false;

  /* Gather instructions that can and need to be relocated */
  Scope currentBlockScope = Scope::eGlobal;

  auto iter = m_builder.getCode().first;

  while (iter != m_builder.getCode().second) {
    if (iter->getOpCode() == OpCode::eDerivX || iter->getOpCode() == OpCode::eDerivY) {
      const auto& arg = m_builder.getOpForOperand(*iter, 0u);

      if (derivativeIsZeroForArg(arg)) {
        iter = m_builder.iter(m_builder.rewriteDef(iter->getDef(), m_builder.makeConstantZero(iter->getType())));
        status = true;
        continue;
      }
    }

    if (iter->getOpCode() == OpCode::eLabel) {
      currentBlockScope = m_divergence->getUniformScopeForDef(iter->getDef());
    } else if (isBlockTerminator(iter->getOpCode())) {
      currentBlockScope = Scope::eGlobal;
    } else if (currentBlockScope < Scope::eQuad) {
      if (opRequiresQuadUniformControlFlow(*iter)) {
        /* Find quad-uniform predecessor and check whether we can hoist */
        SsaDef dom = m_dominance->getBlockForDef(iter->getDef());

        while (dom && m_divergence->getUniformScopeForDef(dom) < Scope::eQuad)
          dom = m_dominance->getImmediateDominator(dom);

        if (dom && canHoistDerivativeOp(*iter, dom))
          hoistInstruction(*iter, dom);
      }
    }

    ++iter;
  }

  if (m_opBlocks.empty())
    return status;

  relocateInstructions();
  return true;
}


bool DerivativePass::runPass(Builder& builder, const Options& options) {
  return DerivativePass(builder, options).run();
}


void DerivativePass::hoistInstruction(const Op& op, SsaDef block) {
  if (op.getOpCode() == OpCode::eImageSample) {
    const auto& coord = m_builder.getOpForOperand(op, 3u);
    dxbc_spv_assert(coord && coord.getType().isBasicType());

    auto coordType = coord.getType().getBaseType(0u);

    SsaDef dx = { };
    SsaDef dy = { };

    if (m_divergence->getUniformScopeForDef(coord.getDef()) < Scope::eQuad) {
      const auto& lodBias = m_builder.getOpForOperand(op, 6u);
      dxbc_spv_assert(!m_builder.getOpForOperand(op, 5u));

      /* If an LOD bias is specified, use it to scale the derivatives. */
      SsaDef lodBiasScale = { };

      if (lodBias) {
        lodBiasScale = m_builder.addBefore(op.getDef(),
          Op::FExp2(lodBias.getType(), lodBias.getDef()));
      }

      /* Explicitly compute derivatives for the given coordinates and
      * hoist the derivative op itself. */
      Op dxComposite(OpCode::eCompositeConstruct, coordType);
      Op dyComposite(OpCode::eCompositeConstruct, coordType);

      for (uint32_t i = 0u; i < coordType.getVectorSize(); i++) {
        auto coordScalar = coord.getDef();

        if (coordType.isVector()) {
          if (coord.getOpCode() == OpCode::eCompositeConstruct) {
            coordScalar = SsaDef(coord.getOperand(i));
          } else {
            coordScalar = m_builder.addBefore(op.getDef(),
              Op::CompositeExtract(coordType.getBaseType(), coord.getDef(), m_builder.makeConstant(i)));
          }
        }

        auto dx = m_builder.addBefore(op.getDef(),
          Op::DerivX(coordType.getBaseType(), coordScalar, DerivativeMode::eCoarse));
        auto dy = m_builder.addBefore(op.getDef(),
          Op::DerivY(coordType.getBaseType(), coordScalar, DerivativeMode::eCoarse));

        /* Only hoist the derivative, not the scaling */
        m_opBlocks.insert({ dx, block });
        m_opBlocks.insert({ dy, block });

        if (lodBiasScale) {
          dx = m_builder.addBefore(op.getDef(), Op::FMul(coordType.getBaseType(), dx, lodBiasScale));
          dy = m_builder.addBefore(op.getDef(), Op::FMul(coordType.getBaseType(), dy, lodBiasScale));
        }

        dxComposite.addOperand(dx);
        dyComposite.addOperand(dy);
      }

      dx = SsaDef(dxComposite.getOperand(0u));
      dy = SsaDef(dyComposite.getOperand(0u));

      if (coordType.isVector()) {
        dx = m_builder.addBefore(op.getDef(), std::move(dxComposite));
        dy = m_builder.addBefore(op.getDef(), std::move(dyComposite));
      }
    } else {
      /* Derivatives of uniform values are constant */
      dx = dy = m_builder.makeConstantZero(coordType);
    }

    /* Rewrite sample op to use derivatives */
    auto sampleOp = op;

    sampleOp.setOperand(6u, SsaDef());
    sampleOp.setOperand(8u, dx);
    sampleOp.setOperand(9u, dy);

    m_builder.rewriteOp(op.getDef(), std::move(sampleOp));
  } else {
    /* Trivial case, just hoist the entire op */
    m_opBlocks.insert({ op.getDef(), block });
  }
}


void DerivativePass::relocateInstructions() {
  util::small_vector<DefBlockKey, 1024> queue;

  for (const auto& e : m_opBlocks)
    queue.push_back({ e.first, e.second });

  /* Recursively add operands to the queue */
  for (size_t i = 0u; i < queue.size(); i++) {
    auto [def, block] = queue.at(i);

    const auto& op = m_builder.getOp(def);

    for (uint32_t j = 0u; j < op.getFirstLiteralOperandIndex(); j++) {
      const auto& arg = m_builder.getOpForOperand(op, j);

      if (!arg || arg.isDeclarative())
        continue;

      /* May be null for newly inserted instructions */
      auto argBlock = m_dominance->getBlockForDef(arg.getDef());

      if (!argBlock)
        argBlock = findContainingBlock(m_builder, arg.getDef());

      if (m_dominance->dominates(argBlock, block))
        continue;

      /* Don't queue up same op multiple times in a row. We still need to make
       * sure that arguments are relocated before the consuming instruction. */
      dxbc_spv_assert(arg.getOpCode() != OpCode::ePhi);

      DefBlockKey key = { };
      key.def = arg.getDef();
      key.block = block;

      if (std::find(queue.begin() + i, queue.end(), key) != queue.end())
        continue;

      queue.push_back(key);
    }
  }

  /* Relocate expressions */
  std::unordered_map<DefBlockKey, SsaDef, DefBlockHash> relocatedOps;

  while (!queue.empty()) {
    auto entry = queue.back();
    queue.pop_back();

    /* Don't move the same instruction to the same block twice */
    if (relocatedOps.find(entry) != relocatedOps.end())
      continue;

    /* Rewrite op to use reloacted operands in the same block */
    auto op = m_builder.getOp(entry.def);

    for (uint32_t i = 0u; i < op.getFirstLiteralOperandIndex(); i++) {
      DefBlockKey key = { SsaDef(op.getOperand(i)), entry.block };

      auto argEntry = relocatedOps.find(key);

      if (argEntry != relocatedOps.end())
        op.setOperand(i, argEntry->second);
    }

    /* Insert new op at the end of the block */
    auto def = m_builder.addBefore(m_dominance->getBlockTerminator(entry.block), std::move(op));
    relocatedOps.insert({ entry, def });
  }

  /* Rewrite hoisted ops */
  for (const auto& e : m_opBlocks) {
    DefBlockKey key({ e.first, e.second });

    auto entry = relocatedOps.find(key);
    dxbc_spv_assert(entry != relocatedOps.end());

    m_builder.rewriteDef(key.def, entry->second);
  }
}


bool DerivativePass::opRequiresQuadUniformControlFlow(const Op& op) const {
  switch (op.getOpCode()) {
    case OpCode::eDerivX:
    case OpCode::eDerivY:
    case OpCode::eImageComputeLod:
      return true;

    case OpCode::eImageSample: {
      auto explicitLod = SsaDef(op.getOperand(5u));

      auto derivX = SsaDef(op.getOperand(8u));
      auto derivY = SsaDef(op.getOperand(9u));

      return !explicitLod && (!derivX || !derivY);
    }

    default:
      return false;
  }
}


bool DerivativePass::canHoistDerivativeOp(const Op& derivOp, SsaDef dstBlock) const {
  /* A derivative-consuming instruction can be relocated if all of its operands
   * are either part of the destination block or dominate it, or if they can be
   * recursively relocated to the same block. */
  util::small_vector<SsaDef, 1024> queue;

  bool allowComplexInput = m_options.hoistNontrivialImplicitLodOps;

  switch (derivOp.getOpCode()) {
    case OpCode::eDerivX:
    case OpCode::eDerivY: {
      allowComplexInput = m_options.hoistNontrivialDerivativeOps;
    } [[fallthrough]];

    case OpCode::eImageComputeLod: {
      for (uint32_t i = 0u; i < derivOp.getFirstLiteralOperandIndex(); i++)
        queue.push_back(SsaDef(derivOp.getOperand(i)));
    } break;

    case OpCode::eImageSample: {
      /* Only check coordinates since we rewrite the op as
       * a sample instruction with derivatives */
      queue.push_back(SsaDef(derivOp.getOperand(3u)));
    } break;

    default:
      dxbc_spv_unreachable();
      return false;
  }

  for (size_t i = 0u; i < queue.size(); i++) {
    const auto& arg = m_builder.getOp(queue.at(i));

    /* Null operand, can happen */
    if (!arg)
      continue;

    /* Don't redundantly process the same instruction multiple times */
    if (std::find(queue.begin(), queue.begin() + i, arg.getDef()) != queue.begin() + i)
      continue;

    /* Check whether operand already dominates target block's terminator */
    if (!arg.isDeclarative()) {
      auto argBlock = m_dominance->getBlockForDef(arg.getDef());

      if (m_dominance->dominates(argBlock, dstBlock))
        continue;
    }

    switch (arg.getOpCode()) {
      /* Constants do not inhibit moving dependent ops */
      case OpCode::eConstant:
      case OpCode::eUndef:
        break;

      /* We can never relocate phis, they are only fine if they
       * already dominate the target block */
      case OpCode::ePhi:
        return false;

      /* For function calls, make sure the function has no side effects
       * and then check the call arguments */
      case OpCode::eFunctionCall: {
        if (!allowComplexInput || !m_divergence->functionIsPure(m_builder.getOpForOperand(arg, 0u).getDef()))
          return false;

        for (uint32_t i = 1u; i < arg.getFirstLiteralOperandIndex(); i++)
          queue.push_back(SsaDef(arg.getOperand(i)));
      } break;

      /* For descriptor loads, check the config option and the index */
      case OpCode::eDescriptorLoad: {
        if (!m_options.hoistDescriptorLoads)
          return false;

        queue.push_back(SsaDef(arg.getOperand(1u)));
      } break;

      /* Atomics and loads from writable locations cannot be relocated */
      case OpCode::eOutputLoad:
      case OpCode::eScratchLoad:
      case OpCode::eLdsLoad:
      case OpCode::eLdsAtomic:
      case OpCode::eBufferAtomic:
      case OpCode::eImageAtomic:
      case OpCode::eCounterAtomic:
      case OpCode::eMemoryLoad:
      case OpCode::eMemoryAtomic:
        return false;

      /* Resource loads can only be relocated if the resource is read-only */
      case OpCode::eBufferLoad:
      case OpCode::eImageLoad: {
        if (!allowComplexInput || !isReadOnlyResource(m_builder.getOpForOperand(arg, 0u)))
          return false;

        for (uint32_t i = 0u; i < arg.getFirstLiteralOperandIndex(); i++)
          queue.push_back(SsaDef(arg.getOperand(i)));
      } break;

      /* Skip declaration for other types of loads */
      case OpCode::eInterpolateAtCentroid:
      case OpCode::eInterpolateAtSample:
      case OpCode::eInterpolateAtOffset: {
        if (!!allowComplexInput)
          return false;
      } [[fallthrough]];

      case OpCode::eParamLoad:
      case OpCode::ePushDataLoad:
      case OpCode::eConstantLoad:
      case OpCode::eInputLoad: {
        for (uint32_t i = 1u; i < arg.getFirstLiteralOperandIndex(); i++)
          queue.push_back(SsaDef(arg.getOperand(i)));
      } break;

      /* All regular instructions can be moved if their operands can */
      case OpCode::eConvertFtoI:
      case OpCode::eConvertItoF:
      case OpCode::eConvertItoI:
      case OpCode::eConvertF32toPackedF16:
      case OpCode::eConvertPackedF16toF32:
      case OpCode::eCheckSparseAccess:
      case OpCode::eBufferQuerySize:
      case OpCode::eImageQuerySize:
      case OpCode::eImageQueryMips:
      case OpCode::eImageQuerySamples:
      case OpCode::eImageSample:
      case OpCode::eImageGather:
      case OpCode::eImageComputeLod:
      case OpCode::ePointer:
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
      case OpCode::eUMSad: {
        if (!allowComplexInput)
          return false;
      } [[fallthrough]];

      /* These instructions can trivially consume an input or constant
       * and may be generated by various passes */
      case OpCode::eCompositeInsert:
      case OpCode::eCompositeExtract:
      case OpCode::eCompositeConstruct:
      case OpCode::eConvertFtoF:
      case OpCode::eConsumeAs:
      case OpCode::eCast:
      case OpCode::eDrain: {
        for (uint32_t i = 0u; i < arg.getFirstLiteralOperandIndex(); i++)
          queue.push_back(SsaDef(arg.getOperand(i)));
      } break;

      /* Unexpected opcodes */
      case OpCode::eUnknown:
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
      case OpCode::eLastDeclarative:
      case OpCode::eFunction:
      case OpCode::eFunctionEnd:
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
      case OpCode::eBarrier:
      case OpCode::eDemote:
      case OpCode::eEmitVertex:
      case OpCode::eEmitPrimitive:
      case OpCode::eRovScopedLockBegin:
      case OpCode::eRovScopedLockEnd:
      case OpCode::eLabel:
      case OpCode::eBranch:
      case OpCode::eBranchConditional:
      case OpCode::eSwitch:
      case OpCode::eUnreachable:
      case OpCode::eReturn:
      case OpCode::eOutputStore:
      case OpCode::eLdsStore:
      case OpCode::eScratchStore:
      case OpCode::eBufferStore:
      case OpCode::eImageStore:
      case OpCode::eMemoryStore:
      case OpCode::Count:
        dxbc_spv_unreachable();
        return false;
    }
  }

  return true;
}


bool DerivativePass::isReadOnlyResource(const Op& op) const {
  dxbc_spv_assert(op.getOpCode() == OpCode::eDescriptorLoad);

  if (op.getType() == ScalarType::eUavCounter)
    return false;

  if (op.getType() != ScalarType::eUav)
    return true;

  const auto& dcl = m_builder.getOpForOperand(op, 0u);
  dxbc_spv_assert(dcl.getOpCode() == OpCode::eDclUav);

  auto uavFlags = UavFlags(dcl.getOperand(dcl.getFirstLiteralOperandIndex() + 4u));
  return bool(uavFlags & UavFlag::eReadOnly);
}


bool DerivativePass::derivativeIsZeroForArg(const Op& op) const {
  if (m_divergence->getUniformScopeForDef(op.getDef()) < Scope::eQuad)
    return false;

  if ((op.getFlags() & OpFlag::eNoNan) && (op.getFlags() & OpFlag::eNoInf))
    return true;

  if (op.isConstant() && op.getType().getBaseType(0u).getBaseType() == ScalarType::eF32) {
    bool noInfNan = true;

    for (uint32_t i = 0u; i < op.getOperandCount(); i++) {
      auto kind = std::fpclassify(float(op.getOperand(i)));
      noInfNan = noInfNan && kind != FP_NAN && kind != FP_INFINITE;
    }

    return noInfNan;
  }

  return false;
}

}
