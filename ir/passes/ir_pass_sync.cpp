#include "ir_pass_sync.h"
#include "ir_pass_ssa.h"

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

  if (m_options.insertLdsBarriers || m_options.insertUavBarriers)
    insertBarriers();
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


void SyncPass::insertBarriers() {
  /* Could technically do UAV stuff in other stages, but
   * there are no known case where that is needed */
  if (m_stage != ShaderStage::eCompute)
    return;

  /* Prepare dominance and divergence analysis */
  SsaConstructionPass::runInsertExitPhiPass(m_builder);

  m_dominance.emplace(m_builder);
  m_divergence.emplace(m_builder, *m_dominance);

  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    switch (iter->getOpCode()) {
      case OpCode::eDclLds: {
        if (m_options.insertLdsBarriers)
          insertLdsBarriers(iter->getDef());
      } break;

      case OpCode::eDclUav: {
        if (m_options.insertUavBarriers) {
          auto uavFlags = UavFlags(iter->getOperand(iter->getFirstLiteralOperandIndex() + 4u));

          if (!(uavFlags & (UavFlag::eReadOnly | UavFlag::eWriteOnly)))
            insertUavBarriers(iter->getDef());
        }
      } break;

      default:
        break;
    }
  }

  if (m_insertedBarrierCount)
    Logger::debug("Inserted ", m_insertedBarrierCount, " barriers.");

  SsaConstructionPass::runResolveTrivialPhiPass(m_builder);
}


void SyncPass::insertLdsBarriers(SsaDef variable) {
  auto [a, b] = m_builder.getUses(variable);

  for (auto i = a; i != b; i++) {
    for (auto j = i; j != b; j++) {
      if (i != j && !i->isDeclarative() && !j->isDeclarative())
        resolveHazard(*i, *j, MemoryType::eLds);
    }
  }
}


void SyncPass::insertUavBarriers(SsaDef variable) {
  auto [a, b] = m_builder.getUses(variable);
  util::small_vector<SsaDef, 256u> uses;

  for (auto i = a; i != b; i++) {
    if (i->getOpCode() == OpCode::eDescriptorLoad)
      m_builder.getUses(i->getDef(), uses);
  }

  for (auto i = uses.begin(); i != uses.end(); i++) {
    for (auto j = i; j != uses.end(); j++) {
      const auto& iOp = m_builder.getOp(*i);
      const auto& jOp = m_builder.getOp(*j);

      if (i != j && !iOp.isDeclarative() && !jOp.isDeclarative())
        resolveHazard(iOp, jOp, MemoryType::eUav);
    }
  }
}


void SyncPass::resolveHazard(const Op& srcAccess, const Op& dstAccess, MemoryType type) {
  /* Deliberately ignore store->store or atomic->atomic, there are
   * no situations where those accesses are known to be a problem. */
  if (srcAccess.getOpCode() == dstAccess.getOpCode())
    return;

  /* If both accesses happen within the same block, just check if there is a
   * barrier between the two accesses and insert one if necessary. If the
   * destination access happens before the source access, we need to take
   * the regular path since there may still be a hazard e.g. inside a loop. */
  auto srcBlock = m_dominance->getBlockForDef(srcAccess.getDef());
  auto dstBlock = m_dominance->getBlockForDef(dstAccess.getDef());

  if (srcBlock == dstBlock) {
    auto iter = m_builder.iter(srcAccess.getDef());

    while (!isBlockTerminator(iter->getOpCode())) {
      if (iter->getOpCode() == OpCode::eBarrier && barrierSynchronizes(*iter, type))
        return;

      if (iter->getDef() == dstAccess.getDef()) {
        insertBarrierBefore(dstAccess.getDef());
        return;
      }

      ++iter;
    }
  }

  /* Check if there is a barrier after the source access within the same block */
  auto iter = m_builder.iter(srcAccess.getDef());

  while (!isBlockTerminator(iter->getOpCode())) {
    if (iter->getOpCode() == OpCode::eBarrier && barrierSynchronizes(*iter, type))
      return;

    iter++;
  }

  /* Check if there is a barrier before the destination access in the same block */
  iter = m_builder.iter(dstAccess.getDef());

  while (iter->getOpCode() != OpCode::eLabel) {
    if (iter->getOpCode() == OpCode::eBarrier && barrierSynchronizes(*iter, type))
      return;

    iter--;
  }

  SsaDef dominator = { };

  if (m_dominance->dominates(srcBlock, dstBlock)) {
    /* Trivial case, we can insert a barrier after the source access */
    dominator = srcBlock;
  } else {
    /* Check if there is a path from the source access to the destination access
     * that does not go through a barrier. We can't use post-dominance information
     * here since loops are going to break it. */
    util::small_vector<SsaDef, 4096u> blocks;

    forEachBranchTarget(m_builder.getOp(m_dominance->getBlockTerminator(srcBlock)),
      [&blocks] (SsaDef target) { blocks.push_back(target); });

    for (size_t i = 0u; i < blocks.size(); i++) {
      auto to = blocks.at(i);

      /* Skip source block if the blocks are the same since we already verified that
       * there is no barrier dominating the destination access in that case. */
      if (to != srcBlock && (scanBlockBarriers(to) & type))
        continue;

      /* If we reach a block that dominates the destination access without encountering
       * a barrier, stop the search and set the innermost dominator as a candidate for
       * inserting a new barrier. */
      if (m_dominance->dominates(to, dstBlock)) {
        if (!dominator || m_dominance->dominates(dominator, to)) {
          dominator = to;

          if (dominator == dstBlock)
            break;
        }

        continue;
      }

      /* Recursively scan successors of the block in question */
      forEachBranchTarget(m_builder.getOp(m_dominance->getBlockTerminator(to)), [&blocks] (SsaDef target) {
        if (std::find(blocks.begin(), blocks.end(), target) == blocks.end())
          blocks.push_back(target);
      });
    }
  }

  /* No hazard */
  if (!dominator)
    return;

  /* Check if there is a barrier in the path from the innermost dominator
   * to the destination access. Ignore the source block again. */
  for (auto block = dstBlock; block != dominator; ) {
    block = m_dominance->getImmediateDominator(block);

    if (block && block != srcBlock && (scanBlockBarriers(block) & type))
      return;
  }

  /* Find last post-dominator that still dominates the destination access
   * to insert barrier as late as possible. This should also minimize the
   * number of barriers inserted, compared to any approach that inserts
   * barriers close to either access. */
  while (true) {
    auto next = m_dominance->getImmediatePostDominator(dominator);

    if (!next || !m_dominance->dominates(next, dstBlock))
      break;

    dominator = next;
  }

  /* Insert new barrier at the start of the selected dominator, unless this
   * is the source block itself, in which case we need to insert it at the end. */
  if (dominator != srcBlock) {
    iter = m_builder.iter(dominator);

    while (iter->getOpCode() == OpCode::eLabel || iter->getOpCode() == OpCode::ePhi)
      iter++;

    insertBarrierBefore(iter->getDef());
  } else {
    insertBarrierBefore(m_dominance->getBlockTerminator(dominator));
  }
}


MemoryTypeFlags SyncPass::scanBlockBarriers(SsaDef block) {
  auto e = m_blockBarriers.find(block);

  if (e != m_blockBarriers.end())
    return e->second;

  MemoryTypeFlags flags = { };

  for (auto iter = m_builder.iter(block); !isBlockTerminator(iter->getOpCode()); iter++) {
    if (iter->getOpCode() == OpCode::eBarrier)
      flags |= MemoryTypeFlags(iter->getOperand(2u));
  }

  m_blockBarriers.insert({ block, flags });
  return flags;
}


void SyncPass::addBlockBarrier(SsaDef block, MemoryTypeFlags types) {
  types |= scanBlockBarriers(block);
  m_blockBarriers.at(block) = types;

  m_insertedBarrierCount++;
}


void SyncPass::insertBarrierBefore(SsaDef ref) {
  auto block = m_dominance->getBlockForDef(ref);

  if (!block)
    block = findContainingBlock(m_builder, ref);

  Scope execScope = Scope::eThread;

  if (m_stage == ShaderStage::eCompute) {
    execScope = m_divergence->getUniformScopeForDef(block) >= Scope::eWorkgroup
      ? Scope::eWorkgroup
      : Scope::eSubgroup;
  }

  Scope memScope = execScope;
  MemoryTypeFlags memTypes = { };

  if (m_options.insertLdsBarriers) {
    memScope = std::max(memScope, Scope::eWorkgroup);
    memTypes |= MemoryType::eLds;
  }

  if (m_options.insertUavBarriers) {
    memScope = std::max(memScope, getUavMemoryScope());
    memTypes |= MemoryType::eUav;
  }

  m_builder.addBefore(ref, Op::Barrier(execScope, memScope, memTypes));
  addBlockBarrier(block, memTypes);
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

bool SyncPass::barrierSynchronizes(const Op& barrier, MemoryType type) {
  dxbc_spv_assert(barrier.getOpCode() == OpCode::eBarrier);

  auto types = MemoryTypeFlags(barrier.getOperand(2u));
  return bool(types & type);
}

}
