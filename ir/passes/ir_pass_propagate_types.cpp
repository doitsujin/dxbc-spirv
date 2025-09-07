#include "ir_pass_propagate_types.h"
#include "ir_pass_lower_consume.h"

#include "../../util/util_log.h"

namespace dxbc_spv::ir {

PropagateTypesPass::PropagateTypesPass(Builder& builder)
: m_builder(builder) {

}


PropagateTypesPass::~PropagateTypesPass() {

}


void PropagateTypesPass::runPass(Builder& builder) {
  PropagateTypesPass(builder).run();
}


void PropagateTypesPass::run() {
  /* Gather phi and select instructions that produce an 'Unknown' result. The
   * only other instructions that may produce these are loads, and only stores
   * may consume 'Unknown' values without returning one, and we handle both
   * those cases separately when determining resouce types. */
  auto iter = m_builder.getCode().first;

  while (iter != m_builder.end()) {
    if (iter->getType().isBasicType()) {
      auto type = iter->getType().getBaseType(0u);

      if (type.isUnknownType() && (iter->getOpCode() == OpCode::ePhi || iter->getOpCode() == OpCode::eSelect))
        m_opsToResolve.push_back(iter->getDef());
    }

    ++iter;
  }

  /* Resolve instructions until we can't infer any types anymore. This
   * will generally be the case for ops that only move memory around. */
  while (true) {
    bool progress = false;

    for (size_t i = 0u; i < m_opsToResolve.size(); ) {
      auto def = m_opsToResolve[i];
      auto [feedback, type] = resolveUnknownPhiSelect(m_builder.getOp(def));

      if (feedback) {
        progress = true;

        m_opsToResolve[i] = m_opsToResolve.back();
        m_opsToResolve.pop_back();
      } else {
        i++;
      }
    }

    if (!progress)
      break;
  }

  /* For any unknown ops that remain, fall back to plain u32. */
  for (size_t i = 0u; i < m_opsToResolve.size(); i++) {
    const auto& op = m_builder.getOp(m_opsToResolve[i]);
    auto type = op.getType().getBaseType(0u);

    rewriteResolvedOp(op, BasicType(ScalarType::eU32, type.getVectorSize()));
  }

  /* Get rid of the consume chains that we inevitably created. */
  LowerConsumePass::runResolveCastChainsPass(m_builder);
}


std::pair<bool, BasicType> PropagateTypesPass::resolveUnknownPhiSelect(const Op& op) {
  auto declaredType = op.getType().getBaseType(0u);

  /* Check whether all non-constant numeric operands agree on a type for this op */
  BasicType resolvedType = ScalarType::eVoid;

  for (uint32_t i = 0u; i < op.getFirstLiteralOperandIndex(); i++) {
    const auto& operand = m_builder.getOpForOperand(op, i);

    if (!operand.isDeclarative() && operand.getType().isBasicType() &&
        operand.getType().getBaseType(0u).isNumericType()) {
      /* Combine operand type with incoming type info */
      auto operandType = inferOpType(operand.getDef());
      resolvedType = resolveTypeForUnknownOp(resolvedType, operandType);
    }
  }

  if (resolvedType.isVoidType() || resolvedType.isUnknownType()) {
    resolvedType = ScalarType::eVoid;

    /* If we couldn't resolve the type based on its operands, check
     * whether all consumers of the instruction agree on a type and
     * back-propagate it if consistent. */
    auto [a, b] = m_builder.getUses(op.getDef());

    for (auto use = a; use != b; use++) {
      if (use->getOpCode() == OpCode::eConsumeAs) {
        resolvedType = resolveTypeForUnknownOp(
          resolvedType, inferOpType(use->getDef()));
      }
    }

    /* Abort if we still couldn't find a useful type for this op */
    if (resolvedType.isVoidType() || resolvedType.isUnknownType())
      return std::make_pair(false, declaredType);
  }

  /* Rewrite op to use the new type */
  rewriteResolvedOp(op, resolvedType);
  return std::make_pair(true, resolvedType);
}


SsaDef PropagateTypesPass::rewriteResolvedOp(const Op& op, BasicType type) {
  /* All ops that we support here expect all numeric operands to be of the same
   * type. Blocks for phi or conditions for select are implicitly ignored. */
  Op newOp(op.getOpCode(), type);
  newOp.setFlags(op.getFlags());

  for (uint32_t i = 0u; i < op.getFirstLiteralOperandIndex(); i++) {
    auto operand = SsaDef(op.getOperand(i));

    const auto& srcOp = m_builder.getOp(operand);

    if (srcOp.getType().isBasicType() && srcOp.getType().getBaseType(0u).isNumericType())
      operand = consumeAs(SsaDef(operand), type);

    newOp.addOperand(operand);
  }

  for (uint32_t i = op.getFirstLiteralOperandIndex(); i < op.getOperandCount(); i++)
    newOp.addOperand(op.getOperand(i));

  /* Insert rewritten op and consume it as the original type to maintain compatibility.
   * We will eliminate any newly created consume chains separately. */
  auto newDef = m_builder.addAfter(op.getDef(), std::move(newOp));

  m_builder.rewriteDef(op.getDef(), consumeAs(newDef, op.getType().getBaseType(0u)));
  return newDef;
}


SsaDef PropagateTypesPass::consumeAs(SsaDef def, BasicType type) {
  dxbc_spv_assert(def);

  const auto& op = m_builder.getOp(def);

  if (op.getType() == type)
    return def;

  if (op.isUndef())
    return m_builder.makeUndef(type);

  if (op.isConstant())
    return m_builder.add(consumeConstant(op, type));

  /* Skip phi ops before inserting instruction */
  auto ref = m_builder.getNext(def);

  while (m_builder.getOp(ref).getOpCode() == OpCode::ePhi)
    ref = m_builder.getNext(ref);

  /* Scan consume instructions that we may have already inserted */
  while (m_builder.getOp(ref).getOpCode() == OpCode::eConsumeAs) {
    const auto& srcOp = m_builder.getOpForOperand(ref, 0u);

    if (srcOp.getDef() == def && srcOp.getType().getBaseType(0u) == type)
      return ref;

    ref = m_builder.getNext(ref);
  }

  /* Insert new consume op */
  return m_builder.addBefore(ref, Op::ConsumeAs(type, def));
}


BasicType PropagateTypesPass::inferOpType(SsaDef def) {
  BasicType defType = m_builder.getOp(def).getType().getBaseType(0u);

  /* Follow consume chain all the way to the top */
  while (m_builder.getOp(def).getOpCode() == OpCode::eConsumeAs)
    def = SsaDef(m_builder.getOp(def).getOperand(0u));

  BasicType consumedType = m_builder.getOp(def).getType().getBaseType(0u);
  return consumedType.isUnknownType() ? defType : consumedType;
}


BasicType PropagateTypesPass::resolveTypeForUnknownOp(BasicType resolvedType, BasicType operandType) {
  /* If the types are the same, there's nothing to do */
  if (resolvedType == operandType)
    return resolvedType;

  /* Void is used as a starting point, so override it with the
   * operand type immediately. */
  if (resolvedType.isVoidType())
    return operandType;

  /* If the 'operand' type is void (e.g. a store), ignore */
  if (operandType.isVoidType())
    return resolvedType;

  /* Give up if vector sizes are weird and use the resolved type */
  if (resolvedType.getVectorSize() != operandType.getVectorSize())
    return BasicType(ScalarType::eUnknown, resolvedType.getVectorSize());

  /* If any operand is unknown, propagate unkown so we don't
   * make any bad assumptions about the operand bit size */
  if (resolvedType.isUnknownType() || operandType.isUnknownType())
    return BasicType(ScalarType::eUnknown, resolvedType.getVectorSize());

  /* Use U32 fallback when mixing float and int types */
  if (resolvedType.isFloatType() != operandType.isFloatType())
    return BasicType(ScalarType::eU32, resolvedType.getVectorSize());

  /* Prefer signed integer types for mixed-sign inputs */
  if (resolvedType.isIntType() && resolvedType.isSignedIntType() != operandType.isSignedIntType()) {
    resolvedType = makeIntTypeSigned(resolvedType);
    operandType = makeIntTypeSigned(operandType);
  }

  /* Use the larger of the two types. Since we're only following consume
   * chains here, all types involved will be no larger than 32 bits. */
  return resolvedType.byteSize() >= operandType.byteSize()
    ? resolvedType
    : operandType;
}


ScalarType PropagateTypesPass::makeIntTypeSigned(ScalarType t) {
  switch (t) {
    case ScalarType::eU8:     return ScalarType::eI8;
    case ScalarType::eU16:    return ScalarType::eI16;
    case ScalarType::eU32:    return ScalarType::eI32;
    case ScalarType::eU64:    return ScalarType::eI64;
    case ScalarType::eMinU16: return ScalarType::eMinI16;
    default:                  return t;
  }
}


BasicType PropagateTypesPass::makeIntTypeSigned(BasicType t) {
  return BasicType(makeIntTypeSigned(t.getBaseType()), t.getVectorSize());
}

}
