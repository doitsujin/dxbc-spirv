#include "ir_pass_scalarize.h"

#include "../ir_utils.h"

namespace dxbc_spv::ir {

ScalarizePass::ScalarizePass(Builder& builder, const Options& options)
: m_builder(builder), m_options(options) {

}


ScalarizePass::~ScalarizePass() {

}


void ScalarizePass::run() {
  scalarizeVectorOps();

  while (resolveRedundantComposites())
    continue;
}


bool ScalarizePass::resolveRedundantComposites() {
  auto iter = m_builder.getCode().first;
  bool feedback = false;

  while (iter != m_builder.end()) {
    switch (iter->getOpCode()) {
      case OpCode::eCompositeConstruct: {
        auto [progress, next] = resolveCompositeConstruct(iter);
        feedback |= progress;
        iter = next;
      } break;

      case OpCode::eCompositeExtract: {
        auto [progress, next] = resolveCompositeExtract(iter);
        feedback |= progress;
        iter = next;
      } break;

      default:
        ++iter;
    }
  }

  return feedback;
}


void ScalarizePass::runPass(Builder& builder, const Options& options) {
  ScalarizePass(builder, options).run();
}


bool ScalarizePass::runResolveRedundantCompositesPass(Builder& builder) {
  return ScalarizePass(builder, ScalarizePass::Options()).resolveRedundantComposites();
}


void ScalarizePass::scalarizeVectorOps() {
  auto iter = m_builder.getCode().first;

  while (iter != m_builder.end()) {
    switch (iter->getOpCode()) {
      case OpCode::ePhi:
        iter = handlePhi(iter);
        continue;

      case OpCode::eCast:
      case OpCode::eConsumeAs:
        iter = handleCastConsume(iter);
        continue;

      /* Operations that need to be scalarized fully,
       * regardless of their operand types. */
      case OpCode::eConvertFtoF:
      case OpCode::eConvertFtoI:
      case OpCode::eConvertItoF:
      case OpCode::eConvertItoI:
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
      case OpCode::eFMulLegacy:
      case OpCode::eFMadLegacy:
      case OpCode::eFSqrt:
      case OpCode::eFRsq:
      case OpCode::eFExp2:
      case OpCode::eFLog2:
      case OpCode::eFSin:
      case OpCode::eFCos:
      case OpCode::eFPow:
      case OpCode::eFPowLegacy:
      case OpCode::eUMSad:
      case OpCode::eIAddCarry:
      case OpCode::eISubBorrow:
      case OpCode::eSMulExtended:
      case OpCode::eUMulExtended:
        iter = handleGenericOp(iter, false);
        continue;

      /* Operations that can support sub-dword vectors
       * if explicitly enabled through the options. */
      case OpCode::eFAbs:
      case OpCode::eFNeg:
      case OpCode::eFAdd:
      case OpCode::eFSub:
      case OpCode::eFMul:
      case OpCode::eFMad:
      case OpCode::eFDiv:
      case OpCode::eFRcp:
      case OpCode::eFFract:
      case OpCode::eFRound:
      case OpCode::eFMin:
      case OpCode::eFMax:
      case OpCode::eFClamp:
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
      case OpCode::eISub:
      case OpCode::eIAbs:
      case OpCode::eINeg:
      case OpCode::eIMul:
      case OpCode::eUDiv:
      case OpCode::eUMod:
      case OpCode::eSMin:
      case OpCode::eSMax:
      case OpCode::eSClamp:
      case OpCode::eUMin:
      case OpCode::eUMax:
      case OpCode::eUClamp:
        iter = handleGenericOp(iter, true);
        continue;

      /* Opcodes to which scalarization does not apply in general, or which
       * are allowed to return vectors. In particular, we do not want to
       * scalarize loads and stores. */
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
      case OpCode::eFunction:
      case OpCode::eFunctionEnd:
      case OpCode::eFunctionCall:
      case OpCode::eLabel:
      case OpCode::eBranch:
      case OpCode::eBranchConditional:
      case OpCode::eSwitch:
      case OpCode::eUnreachable:
      case OpCode::eReturn:
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
      case OpCode::eBarrier:
      case OpCode::eConvertF32toPackedF16:
      case OpCode::eConvertPackedF16toF32:
      case OpCode::eCompositeInsert:
      case OpCode::eCompositeExtract:
      case OpCode::eCompositeConstruct:
      case OpCode::eCheckSparseAccess:
      case OpCode::eParamLoad:
      case OpCode::eTmpLoad:
      case OpCode::eTmpStore:
      case OpCode::eScratchLoad:
      case OpCode::eScratchStore:
      case OpCode::eLdsLoad:
      case OpCode::eLdsStore:
      case OpCode::ePushDataLoad:
      case OpCode::eInputLoad:
      case OpCode::eOutputLoad:
      case OpCode::eOutputStore:
      case OpCode::eDescriptorLoad:
      case OpCode::eBufferLoad:
      case OpCode::eBufferStore:
      case OpCode::eBufferQuerySize:
      case OpCode::eMemoryLoad:
      case OpCode::eMemoryStore:
      case OpCode::eConstantLoad:
      case OpCode::eLdsAtomic:
      case OpCode::eBufferAtomic:
      case OpCode::eImageAtomic:
      case OpCode::eCounterAtomic:
      case OpCode::eMemoryAtomic:
      case OpCode::eImageLoad:
      case OpCode::eImageStore:
      case OpCode::eImageQuerySize:
      case OpCode::eImageQueryMips:
      case OpCode::eImageQuerySamples:
      case OpCode::eImageSample:
      case OpCode::eImageGather:
      case OpCode::eImageComputeLod:
      case OpCode::ePointer:
      case OpCode::eEmitVertex:
      case OpCode::eEmitPrimitive:
      case OpCode::eDemote:
      case OpCode::eRovScopedLockBegin:
      case OpCode::eRovScopedLockEnd:
      case OpCode::eFDot:
      case OpCode::eFDotLegacy:
      case OpCode::eDrain:
        break;

      /* Invalid opcodes */
      case OpCode::eLastDeclarative:
      case OpCode::eUnknown:
      case OpCode::Count:
        dxbc_spv_unreachable();
        break;
    }

    ++iter;
  }
}


uint32_t ScalarizePass::determineVectorSize(ScalarType type) const {
  if (!m_options.subDwordVectors)
    return 1u;

  return std::max(1u, uint32_t(sizeof(uint32_t) / byteSize(type)));
}


SsaDef ScalarizePass::extractOperandComponents(SsaDef operand, uint32_t first, uint32_t count) {
  /* Use current insertion cursor to emit composite ops */
  const auto& operandOp = m_builder.getOp(operand);
  auto operandType = operandOp.getType().getBaseType(0u);
  dxbc_spv_assert(first + count <= operandType.getVectorSize());

  if (operandType.isScalar()) {
    /* Trivial case */
    return operand;
  } else if (operandOp.isUndef()) {
    /* Emit undef of the base type */
    return m_builder.makeUndef(BasicType(operandType.getBaseType(), count));
  } else if (operandOp.isConstant()) {
    /* Extract requested scalars from the constant */
    Op constant(OpCode::eConstant, BasicType(operandType.getBaseType(), count));

    for (uint32_t i = 0u; i < count; i++)
      constant.addOperand(operandOp.getOperand(first + i));

    return m_builder.add(std::move(constant));
  } else if (operandOp.getOpCode() == OpCode::eCompositeConstruct) {
    /* If the source instruction is a composite construct already, reuse the
     * scalar operands directly for performance reasons. */
    if (count == 1u)
      return SsaDef(operandOp.getOperand(first));

    Op resultOp(OpCode::eCompositeConstruct, BasicType(operandType.getBaseType(), count));

    for (uint32_t i = 0u; i < count; i++)
      resultOp.addOperand(SsaDef(operandOp.getOperand(first + i)));

    return m_builder.add(std::move(resultOp));
  } else if (count == 1u) {
    /* Simple vector extract if we want a scalar */
    return m_builder.add(Op::CompositeExtract(
      operandType.getBaseType(), operand, m_builder.makeConstant(first)));
  } else {
    /* Create a new composite from individual components, we should be able
     * to optimize the vector spam away in a subsequent pass. */
    Op resultOp(OpCode::eCompositeConstruct, BasicType(operandType.getBaseType(), count));

    for (uint32_t i = 0u; i < count; i++) {
      resultOp.addOperand(m_builder.add(Op::CompositeExtract(
        operandType.getBaseType(), operand, m_builder.makeConstant(first + i))));
    }

    return m_builder.add(std::move(resultOp));
  }
}


SsaDef ScalarizePass::assembleResultVector(uint32_t partCount, const SsaDef* partDefs) {
  dxbc_spv_assert(partCount);

  if (partCount == 1u)
    return partDefs[0u];

  BasicType type = { };

  /* Assemble vector from individual components. If those components
   * are vectors themselves, flatten them into scalars first. */
  Op resultOp(OpCode::eCompositeConstruct, type);

  for (uint32_t i = 0u; i < partCount; i++) {
    const auto& op = m_builder.getOp(partDefs[i]);
    auto partType = op.getType().getBaseType(0u);

    for (uint32_t j = 0u; j < partType.getVectorSize(); j++)
      resultOp.addOperand(extractOperandComponents(op.getDef(), j, 1u));

    type = BasicType(partType.getBaseType(), resultOp.getOperandCount());
  }

  /* Use current insertion cursor to emit composite ops */
  return m_builder.add(std::move(resultOp.setType(type)));
}


Builder::iterator ScalarizePass::scalarizeOp(Builder::iterator op, uint32_t dstStep, uint32_t srcStep) {
  util::small_vector<SsaDef, 4u> result;
  auto dstType = op->getType().getBaseType(0u);

  /* Insert any new instructions after the original one */
  m_builder.setCursor(op->getDef());

  for (auto dstIndex = 0u, srcIndex = 0u; dstIndex < dstType.getVectorSize(); dstIndex += dstStep, srcIndex += srcStep) {
    if (dstIndex + dstStep > dstType.getVectorSize()) {
      auto divisor = std::min(dstStep, srcStep);

      srcStep /= divisor;
      dstStep /= divisor;
    }

    Op splitOp(op->getOpCode(), BasicType(dstType.getBaseType(), dstStep));
    splitOp.setFlags(op->getFlags());

    for (uint32_t i = 0u; i < op->getFirstLiteralOperandIndex(); i++)
      splitOp.addOperand(extractOperandComponents(SsaDef(op->getOperand(i)), srcIndex, srcStep));

    for (uint32_t i = op->getFirstLiteralOperandIndex(); i < op->getOperandCount(); i++)
      splitOp.addOperand(op->getOperand(i));

    result.push_back(m_builder.add(std::move(splitOp)));
  }

  auto resultDef = assembleResultVector(result.size(), result.data());

  m_builder.rewriteDef(op->getDef(), resultDef);
  return m_builder.iter(resultDef)++;
}


Builder::iterator ScalarizePass::handlePhi(Builder::iterator op) {
  dxbc_spv_assert(op->getType().isBasicType());

  /* Nothing to do if the instruction is scalar already */
  auto vectorType = op->getType().getBaseType(0u);

  if (vectorType.isScalar())
    return ++op;

  /* Decide vector size based on the scalar type. If the vector size
   * of the incoming type already matches, don't do anything. */
  uint32_t vectorSize = determineVectorSize(vectorType.getBaseType());

  if (vectorType.getVectorSize() == vectorSize)
    return ++op;

  /* Phi is a special case since we cannot insert the composite ops
   * in place, instead we need to extract where the operands are
   * generated and construct after the last phi in the block. */
  util::small_vector<std::pair<SsaDef, SsaDef>, 64u> operandMap;
  util::small_vector<SsaDef, 4u> result;

  for (auto index = 0u; index < vectorType.getVectorSize(); index += vectorSize) {
    if (index + vectorSize > vectorType.getVectorSize())
      vectorSize = 1u;

    /* Deduplicate phi operands as much as possible */
    Op phi(op->getOpCode(), BasicType(vectorType.getBaseType(), vectorSize));
    phi.setFlags(op->getFlags());

    forEachPhiOperand(*op, [&] (SsaDef block, SsaDef value) {
      SsaDef def = { };

      for (const auto& map : operandMap) {
        if (map.first == value) {
          def = map.second;
          break;
        }
      }

      if (!def) {
        /* If the operand is also a phi, insert extract op after
         * the phi section of the block */
        auto location = value;

        while (m_builder.getOp(location).getOpCode() == OpCode::ePhi) {
          m_builder.setCursor(location);
          location = m_builder.getNext(location);
        }

        def = extractOperandComponents(value, index, vectorSize);
        operandMap.push_back(std::make_pair(value, def));
      }

      phi.addPhi(block, def);
    });

    /* Insert new phi before the existing phi for now */
    result.push_back(m_builder.addBefore(op->getDef(), std::move(phi)));
    operandMap.clear();
  }

  /* Find last phi instruction in block and create composite */
  auto iter = m_builder.iter(op->getDef());

  while (iter->getOpCode() == OpCode::ePhi) {
    m_builder.setCursor(iter->getDef());
    iter++;
  }

  auto resultDef = assembleResultVector(result.size(), result.data());

  m_builder.rewriteDef(op->getDef(), resultDef);
  return m_builder.iter(result.at(0u));
}


Builder::iterator ScalarizePass::handleCastConsume(Builder::iterator op) {
  dxbc_spv_assert(op->getType().isBasicType());

  /* Cast and ConsumeAs can bit-cast between vectors and scalars as long as
   * the overall bit size of both types matches. If either type is scalar,
   * it is already optimal. */
  const auto& srcOp = m_builder.getOpForOperand(*op, 0u);

  auto srcType = srcOp.getType().getBaseType(0u);
  auto dstType = op->getType().getBaseType(0u);

  if (srcType.isScalar() || dstType.isScalar())
    return ++op;

  /* When converting two vector types of the same component count,
   * treat the operation as a regular instruction. */
  if (srcType.getVectorSize() == dstType.getVectorSize())
    return handleGenericOp(op, true);

  /* If we're actually casting between vectors of different component counts,
   * work out how many components to process in one go. */
  auto minCount = std::min(srcType.getVectorSize(), dstType.getVectorSize());

  auto srcStep = srcType.getVectorSize() / minCount;
  auto dstStep = dstType.getVectorSize() / minCount;

  return scalarizeOp(op, dstStep, srcStep);
}


Builder::iterator ScalarizePass::handleGenericOp(Builder::iterator op, bool vectorizeSubDword) {
  dxbc_spv_assert(op->getType().isBasicType());

  /* Nothing to do if the instruction is scalar already */
  auto vectorType = op->getType().getBaseType(0u);

  if (vectorType.isScalar())
    return ++op;

  /* Decide vector size based on the scalar type. If the vector size
   * of the incoming type already matches, don't do anything. */
  uint32_t vectorSize = 1u;

  if (vectorizeSubDword)
    vectorSize = determineVectorSize(vectorType.getBaseType());

  if (vectorType.getVectorSize() == vectorSize)
    return ++op;

  /* Otherwise, split the instruction. We don't need to look at the
   * source operands here since they will all match the component
   * count of the result type. */
  return scalarizeOp(op, vectorSize, vectorSize);
}


std::pair<bool, Builder::iterator> ScalarizePass::resolveCompositeConstruct(Builder::iterator op) {
  /* Remove unused composite instructions that may already exist */
  auto [removed, next] = removeIfUnused(m_builder, op->getDef());

  if (removed)
    return std::make_pair(true, m_builder.iter(next));

  /* Check first argument to make a decision. If all operands come from the
   * same source, or the same type of source, we can rewrite the instruction. */
  const auto& valueOp = m_builder.getOpForOperand(*op, 0u);

  if (valueOp.getOpCode() == OpCode::eCompositeExtract)
    return resolveCompositeConstructFromExtract(op);

  if (valueOp.getOpCode() == OpCode::eConstant)
    return resolveCompositeConstructFromConstant(op);

  if (valueOp.getOpCode() == OpCode::eUndef)
    return resolveCompositeConstructFromUndef(op);

  return std::make_pair(false, ++op);
}


std::pair<bool, Builder::iterator> ScalarizePass::resolveCompositeConstructFromExtract(Builder::iterator op) {
  /* We only know for sure that one operand is an extract, check all the others. */
  for (uint32_t i = 0u; i < op->getOperandCount(); i++) {
    if (m_builder.getOpForOperand(*op, i).getOpCode() != OpCode::eCompositeExtract)
      return std::make_pair(false, ++op);
  }

  /* This can obviously only work if the source type is the same */
  auto baseComposite = SsaDef(m_builder.getOpForOperand(*op, 0u).getOperand(0u));

  if (m_builder.getOp(baseComposite).getType() != op->getType())
    return std::make_pair(false, ++op);

  for (uint32_t i = 0u; i < op->getOperandCount(); i++) {
    /* Check that the base composite is the same for all oprands */
    const auto& extractOp = m_builder.getOpForOperand(*op, i);
    auto composite = SsaDef(extractOp.getOperand(0u));

    if (composite != baseComposite)
      return std::make_pair(false, ++op);

    /* Check that components are in the same order */
    const auto& addressOp = m_builder.getOpForOperand(extractOp, 1u);
    dxbc_spv_assert(addressOp.isConstant());

    if (!addressOp.getType().isScalarType() || (uint32_t(addressOp.getOperand(0u)) != i))
      return std::make_pair(false, ++op);
  }

  /* We can replace the instruction with the base composite. Remove
   * any composite extract ops that go unused because of this. */
  auto removedOp = *op;
  auto next = m_builder.rewriteDef(op->getDef(), baseComposite);

  for (uint32_t i = 0u; i < removedOp.getOperandCount(); i++)
    removeIfUnused(m_builder, SsaDef(removedOp.getOperand(i)));

  return std::make_pair(true, m_builder.iter(next));
}


std::pair<bool, Builder::iterator> ScalarizePass::resolveCompositeConstructFromConstant(Builder::iterator op) {
  /* Ensure that all operands are constant */
  for (uint32_t i = 0u; i < op->getOperandCount(); i++) {
    if (!m_builder.getOpForOperand(*op, i).isConstant())
      return std::make_pair(false, ++op);
  }

  /* We can trivially concatenate constant operands */
  Op constant(OpCode::eConstant, op->getType());

  for (uint32_t i = 0u; i < op->getOperandCount(); i++) {
    const auto& srcOp = m_builder.getOpForOperand(*op, i);

    for (uint32_t j = 0u; j < srcOp.getOperandCount(); j++)
      constant.addOperand(srcOp.getOperand(j));
  }

  auto next = m_builder.rewriteDef(op->getDef(), m_builder.add(std::move(constant)));
  return std::make_pair(true, m_builder.iter(next));
}


std::pair<bool, Builder::iterator> ScalarizePass::resolveCompositeConstructFromUndef(Builder::iterator op) {
  /* Ensure that all operands are undef */
  for (uint32_t i = 0u; i < op->getOperandCount(); i++) {
    if (!m_builder.getOpForOperand(*op, i).isUndef())
      return std::make_pair(false, ++op);
  }

  /* Rewrite composite instruction as undef */
  auto next = m_builder.rewriteDef(op->getDef(), m_builder.makeUndef(op->getType()));
  return std::make_pair(true, m_builder.iter(next));
}


std::pair<bool, Builder::iterator> ScalarizePass::resolveCompositeExtract(Builder::iterator op) {
  /* Remove unused composite instructions that may already exist */
  auto [removed, next] = removeIfUnused(m_builder, op->getDef());

  if (removed)
    return std::make_pair(true, m_builder.iter(next));

  /* If the argument is a composite construct instruction, and if the
   * address is scalar, we can forward the scalar component. */
  const auto& valueOp = m_builder.getOpForOperand(*op, 0u);

  if (valueOp.getOpCode() == OpCode::eCompositeConstruct)
    return resolveCompositeExtractFromConstruct(op);

  /* If the argument is a constant, we can simply extract the operand. */
  if (valueOp.isConstant())
    return resolveCompositeExtractFromConstant(op);

  /* If the argument is undef, we can create an undef of the sub type. */
  if (valueOp.isUndef())
    return resolveCompositeExtractFromUndef(op);

  return std::make_pair(false, ++op);
}


std::pair<bool, Builder::iterator> ScalarizePass::resolveCompositeExtractFromConstruct(Builder::iterator op) {
  const auto& addressOp = m_builder.getOpForOperand(*op, 1u);
  dxbc_spv_assert(addressOp.isConstant());

  /* Fetch operand from composite instruction */
  const auto& valueOp = m_builder.getOpForOperand(*op, 0u);

  auto index = uint32_t(addressOp.getOperand(0u));
  auto value = SsaDef(valueOp.getOperand(index));

  auto addressType = addressOp.getType().getBaseType(0u);

  if (addressType.isScalar()) {
    auto next = m_builder.rewriteDef(op->getDef(), value);

    removeIfUnused(m_builder, valueOp.getDef());
    return std::make_pair(true, m_builder.iter(next));
  } else {
    /* Create new address with the first operand removed */
    Op newAddressOp(OpCode::eConstant, BasicType(
      addressType.getBaseType(), addressType.getVectorSize() - 1u));

    for (uint32_t i = 1u; i < addressType.getVectorSize(); i++)
      newAddressOp.addOperand(addressOp.getOperand(i));

    m_builder.rewriteOp(op->getDef(), Op::CompositeExtract(op->getType(),
      value, m_builder.add(std::move(newAddressOp))));

    /* Re-evaluate instruction that we just added */
    removeIfUnused(m_builder, valueOp.getDef());
    return std::make_pair(true, op);
  }
}


std::pair<bool, Builder::iterator> ScalarizePass::resolveCompositeExtractFromConstant(Builder::iterator op) {
  const auto& addressOp = m_builder.getOpForOperand(*op, 1u);
  dxbc_spv_assert(addressOp.isConstant());

  /* Fetch constant op */
  const auto& valueOp = m_builder.getOpForOperand(*op, 0u);
  auto index = uint32_t(addressOp.getOperand(0u));

  /* Compute index and number of scalar operands we need to extract */
  uint32_t scalarIndex = 0u;
  uint32_t scalarCount = op->getType().computeFlattenedScalarCount();

  for (uint32_t i = 0u; i < index; i++)
    scalarIndex += valueOp.getType().getSubType(i).computeFlattenedScalarCount();

  /* Assemble new constant op */
  Op constant(OpCode::eConstant, op->getType());

  for (uint32_t i = 0u; i < scalarCount; i++)
    constant.addOperand(valueOp.getOperand(scalarIndex + i));

  auto next = m_builder.rewriteDef(op->getDef(), m_builder.add(std::move(constant)));
  return std::make_pair(true, m_builder.iter(next));
}


std::pair<bool, Builder::iterator> ScalarizePass::resolveCompositeExtractFromUndef(Builder::iterator op) {
  auto next = m_builder.rewriteDef(op->getDef(), m_builder.makeUndef(op->getType()));
  return std::make_pair(true, m_builder.iter(next));
}

}
