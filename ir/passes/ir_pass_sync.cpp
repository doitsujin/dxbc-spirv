#include "ir_pass_sync.h"

#include "../../util/util_log.h"

namespace dxbc_spv::ir {

SyncPass::SyncPass(Builder& builder, const Options& options)
: m_builder(builder), m_options(options) {
  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eEntryPoint) {
      m_entryPoint = SsaDef(iter->getOperand(0u));
      m_stage = ShaderStage(iter->getOperand(iter->getFirstLiteralOperandIndex()));
      break;
    }
  }

  if (!m_entryPoint) {
    Logger::err("No entry point found");
    return;
  }
}


SyncPass::~SyncPass() {

}


void SyncPass::run() {
  /* Scan declarations for any shared read-write resources or shared
   * memory. If none of those exist, we have nothing to do here. */
  if (!hasSharedVariables())
    return;

  processAtomicsAndBarriers();

  if (m_options.insertRovLocks)
    insertRovLocks();
}


void SyncPass::runPass(Builder& builder, const Options& options) {
  SyncPass(builder, options).run();
}


void SyncPass::processAtomicsAndBarriers() {
  auto uavScope = getUavMemoryScope();
  auto iter = m_builder.getCode().first;

  while (iter != m_builder.end()) {
    switch (iter->getOpCode()) {
      case OpCode::eBarrier: {
        iter = handleBarrier(iter, uavScope);
      } continue;

      /* Ignore counter atomics */
      case OpCode::eLdsAtomic:
      case OpCode::eBufferAtomic:
      case OpCode::eImageAtomic:
      case OpCode::eMemoryAtomic: {
        iter = handleAtomic(iter);
      } continue;

      default:
        break;
    }

    ++iter;
  }
}


Builder::iterator SyncPass::handleAtomic(Builder::iterator op) {
  if (!m_builder.getUseCount(op->getDef()))
    m_builder.setOpType(op->getDef(), ScalarType::eVoid);

  /* Work out instruction operands */
  auto atomicOp = AtomicOp(op->getOperand(op->getFirstLiteralOperandIndex()));

  auto argOperand = [&] {
    switch (op->getOpCode()) {
      case OpCode::eLdsAtomic:
      case OpCode::eBufferAtomic:
      case OpCode::eMemoryAtomic:
        return 2u;

      case OpCode::eImageAtomic:
        return 3u;

      default:
        break;
    }

    dxbc_spv_unreachable();
    return 0u;
  } ();

  /* Detect atomic load / store patterns */
  const auto& arg = m_builder.getOpForOperand(*op, argOperand);

  bool isLoad = false;
  bool isStore = false;

  bool isUsed = (!op->getType().isVoidType() && m_builder.getUseCount(op->getDef()));

  switch (atomicOp) {
    case AtomicOp::eCompareExchange: {
      /* If the expected and desired operands are the same, this operation will
       * not modify any memory and effectively only returns the current value */
      if (arg.getOpCode() == OpCode::eCompositeConstruct)
        isLoad = SsaDef(arg.getOperand(0u)) == SsaDef(arg.getOperand(1u));
    } break;

    case AtomicOp::eExchange: {
      /* Exchange with an unused result is a plain store. */
      isStore = !isUsed;
    } break;

    case AtomicOp::eAdd:
    case AtomicOp::eSub:
    case AtomicOp::eOr:
    case AtomicOp::eXor:
    case AtomicOp::eUMax: {
      /* These instructions with constant 0 are a plain load */
      isLoad = arg.isConstant() && !uint64_t(arg.getOperand(0u));
    } break;

    case AtomicOp::eAnd:
    case AtomicOp::eUMin: {
      /* And/UMin with constant 0 is equivalent to exchange or store
       * with constant 0, depending on whether the result is discarded */
      isStore = arg.isConstant() && !uint64_t(arg.getOperand(0u));
    } break;

    case AtomicOp::eLoad:
    case AtomicOp::eStore:
    case AtomicOp::eSMin:
    case AtomicOp::eSMax:
    case AtomicOp::eInc:
    case AtomicOp::eDec:
      break;
  }

  dxbc_spv_assert(!isLoad || !isStore);

  if (isLoad && !isUsed) {
    /* Unused load is weird, just replace with a barrier */
    auto [scope, memory] = op->getOpCode() == OpCode::eLdsAtomic
      ? std::pair(Scope::eWorkgroup, MemoryTypeFlags(MemoryType::eLds))
      : std::pair(Scope::eGlobal, MemoryType::eUav);

    m_builder.rewriteOp(op->getDef(), Op::Barrier(Scope::eThread, scope, memory));
    return op;
  } else if (isLoad || isStore) {
    Type type = ScalarType::eVoid;

    if (isUsed)
      type = op->getType();

    Op atomicOp(op->getOpCode(), type);
    atomicOp.setFlags(op->getFlags());

    for (uint32_t i = 0u; i < argOperand; i++)
      atomicOp.addOperand(op->getOperand(i));

    atomicOp.addOperand(isLoad ? SsaDef() : arg.getDef());
    atomicOp.addOperand(isLoad ? AtomicOp::eLoad :
      (isUsed ? AtomicOp::eExchange : AtomicOp::eStore));

    m_builder.rewriteOp(op->getDef(), std::move(atomicOp));
    return ++op;
  } else {
    /* Keep instruction as-is */
    return ++op;
  }
}


Builder::iterator SyncPass::handleBarrier(Builder::iterator op, Scope uavScope) {
  auto controlScope = Scope(op->getOperand(0u));
  auto memoryScope = Scope(op->getOperand(1u));
  auto memoryTypes = MemoryTypeFlags(op->getOperand(2u));

  if (memoryTypes & MemoryType::eUav)
    memoryScope = uavScope;

  if (!memoryTypes)
    memoryScope = Scope::eThread;

  m_builder.rewriteOp(op->getDef(), Op::Barrier(controlScope, memoryScope, memoryTypes));
  return ++op;
}


void SyncPass::insertRovLocks() {
  if (!hasRovResources())
    return;

  /* Insert wrapper function to wrap the entire entry point in ROV locks. We
   * could be smarter here in theory and find uniform blocks that dominate and
   * post-dominate accesses respectively, but content using ROV is very rare. */
  auto mainFunc = m_builder.addAfter(m_entryPoint, Op::Function(ScalarType::eVoid));
  m_builder.add(Op::DebugName(mainFunc, "main_rov_locked"));

  m_builder.setCursor(m_entryPoint);

  m_builder.add(Op::Label());
  m_builder.add(Op::RovScopedLockBegin(MemoryType::eUav, RovScope::ePixel));
  m_builder.add(Op::FunctionCall(ScalarType::eVoid, mainFunc));
  m_builder.add(Op::RovScopedLockEnd(MemoryType::eUav));
  m_builder.add(Op::Return());

  auto entryPointEnd = m_builder.add(Op::FunctionEnd());
  m_builder.reorderBefore(SsaDef(), m_entryPoint, entryPointEnd);
}


bool SyncPass::hasSharedVariables() const {
  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eDclLds)
      return true;

    if (iter->getOpCode() == OpCode::eDclUav) {
      auto uavFlags = UavFlags(iter->getOperand(iter->getFirstLiteralOperandIndex() + 4u));

      if (!(uavFlags & (UavFlag::eReadOnly | UavFlag::eWriteOnly)) || (uavFlags & UavFlag::eRasterizerOrdered))
        return true;
    }
  }

  return false;
}


bool SyncPass::hasRovResources() const {
  if (m_stage != ShaderStage::ePixel)
    return false;

  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eDclUav) {
      auto uavFlags = UavFlags(iter->getOperand(iter->getFirstLiteralOperandIndex() + 4u));

      if (uavFlags & UavFlag::eRasterizerOrdered)
        return true;
    }
  }

  return false;
}


Scope SyncPass::getUavMemoryScope() const {
  if (m_stage != ShaderStage::eCompute)
    return Scope::eGlobal;

  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eDclUav) {
      auto uavFlags = UavFlags(iter->getOperand(iter->getFirstLiteralOperandIndex() + 4u));

      if (!(uavFlags & (UavFlag::eReadOnly | UavFlag::eWriteOnly))) {
        if (uavFlags & UavFlag::eCoherent)
          return Scope::eGlobal;
      }
    }
  }

  return Scope::eWorkgroup;
}

}
