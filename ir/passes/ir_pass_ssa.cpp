#include "ir_pass_ssa.h"

#include "../../util/util_log.h"

namespace dxbc_spv::ir {

SsaConstructionPass::SsaConstructionPass(Builder& builder)
: m_builder(builder) {

}


SsaConstructionPass::~SsaConstructionPass() {

}


void SsaConstructionPass::runPass() {
  resolveTempLoadStore();
  removeTempDecls();
  resolveTrivialPhi();
}


bool SsaConstructionPass::resolveTrivialPhi() {
  std::vector<SsaDef> queue;

  /* Gather trivial phi */
  auto [a, b] = m_builder.getCode();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::ePhi) {
      if (getOnlyUniquePhiOperand(iter->getDef()))
        queue.push_back(iter->getDef());
    }
  }

  /* Recursively check and eliminate phi */
  bool progress = false;

  while (!queue.empty()) {
    auto phi = queue.back();
    queue.pop_back();

    /* Might already have eliminated phi? */
    if (!m_builder.getOp(phi))
      continue;

    /* Get replacement operand if phi is trivial */
    auto def = getOnlyUniquePhiOperand(phi);

    if (!def)
      continue;

    /* Add all phis affected by the rewrie to the queue */
    auto [a, b] = m_builder.getUses(phi);

    for (auto iter = a; iter != b; iter++) {
      if (iter->getOpCode() == OpCode::ePhi && iter->getDef() != phi)
        queue.push_back(iter->getDef());
    }

    m_builder.rewriteDef(phi, def);
    progress = true;
  }

  return progress;
}


void SsaConstructionPass::insertExitPhi() {
  Container<SsaExitPhiState> exitPhi;
  exitPhi.ensure(m_builder.getMaxValidDef());

  util::small_vector<std::pair<SsaDef, SsaDef>, 64u> loops = { };

  auto [a, b] = m_builder.getCode();

  for (auto iter = a; iter != b; iter++) {
    switch (iter->getOpCode()) {
      case OpCode::eLabel: {
        /* If this is the merge block of the innermost loop, we can
         * no longer encounter code belonging to that loop. */
        if (!loops.empty() && loops.back().second == iter->getDef())
          loops.pop_back();

        /* Add any loop header and its merge block to the loop stack */
        if (Construct(iter->getOperand(iter->getFirstLiteralOperandIndex())) == Construct::eStructuredLoop)
          loops.push_back(std::make_pair(iter->getDef(), SsaDef(iter->getOperand(0u))));

        m_block = iter->getDef();
      } break;

      case OpCode::eBranch:
      case OpCode::eBranchConditional:
      case OpCode::eSwitch:
      case OpCode::eUnreachable:
      case OpCode::eReturn: {
        m_block = SsaDef();
      } break;

      default: {
        /* Metdadata stuff, ignore */
        if (!m_block)
          break;

        /* If we are inside a loop, add metadata for the current op */
        if (!loops.empty() && !iter->getType().isVoidType()) {
          auto& phi = exitPhi.at(iter->getDef());
          std::tie(phi.loopHeader, phi.loopMerge) = loops.back();
        }

        /* Scan any operands that may require an exit phi */
        bool rewrite = false;

        Op op = *iter;

        for (uint32_t i = 0u; i < op.getFirstLiteralOperandIndex(); i++) {
          auto arg = SsaDef(op.getOperand(i));
          auto& phi = exitPhi.at(arg);

          if (!phi.exitPhi) {
            /* Operand was not defined inside a loop */
            if (!phi.loopHeader)
              continue;

            /* Ignore phi instructions in the loop merge block of the loop that
             * declared the value. This is basically an exit phi already. */
            if (phi.loopMerge == m_block && iter->getOpCode() == OpCode::ePhi)
              continue;

            /* If we are still inside the declaring loop, ignore */
            bool insideLoop = false;

            for (size_t i = loops.size(); i && !insideLoop; i--) {
              if (phi.loopHeader == loops.at(i - 1u).first)
                insideLoop = true;
            }

            if (insideLoop)
              continue;

            /* Otherwise, insert a phi node inside the loop merge block */
            phi.exitPhi = createExitPhi(arg, phi.loopMerge);
          }

          if (phi.exitPhi) {
            op.setOperand(i, phi.exitPhi);
            rewrite = true;
          }
        }

        if (rewrite)
          m_builder.rewriteOp(iter->getDef(), std::move(op));
      }
    }
  }
}


void SsaConstructionPass::runPass(Builder& builder) {
  SsaConstructionPass pass(builder);
  pass.runPass();
}


bool SsaConstructionPass::runResolveTrivialPhiPass(Builder& builder) {
  SsaConstructionPass pass(builder);
  return pass.resolveTrivialPhi();
}


void SsaConstructionPass::runInsertExitPhiPass(Builder& builder) {
  SsaConstructionPass pass(builder);
  pass.insertExitPhi();
}


void SsaConstructionPass::resolveTempLoadStore() {
  auto iter = m_builder.getCode().first;

  while (iter != m_builder.end()) {
    switch (iter->getOpCode()) {
      case OpCode::eLabel:
        iter = handleLabel(iter);
        break;

      case OpCode::eBranch:
      case OpCode::eBranchConditional:
      case OpCode::eSwitch:
      case OpCode::eReturn:
      case OpCode::eUnreachable:
        iter = handleBlockTerminator(iter);
        break;

      case OpCode::ePhi:
        iter = handlePhi(iter);
        break;

      case OpCode::eTmpLoad:
        iter = handleTmpLoad(iter);
        break;

      case OpCode::eTmpStore:
        iter = handleTmpStore(iter);
        break;

      default:
        ++iter;
    }
  }

  while (!m_phi.empty()) {
    auto [block, def] = m_phi.back();
    m_phi.pop_back();

    evaluatePhi(block, def);
  }
}


void SsaConstructionPass::removeTempDecls() {
  auto iter = m_builder.getDeclarations().first;

  while (iter != m_builder.getDeclarations().second) {
    if (iter->getOpCode() == OpCode::eDclTmp) {
      /* Remove all uses, which should all be debug instructions */
      util::small_vector<SsaDef, 4u> uses;
      m_builder.getUses(iter->getDef(), uses);

      for (auto use : uses) {
        dxbc_spv_assert(m_builder.getOp(use).isDeclarative());
        m_builder.remove(use);
      }

      /* Remove instruction */
      iter = m_builder.iter(m_builder.removeOp(*iter));
    } else {
      ++iter;
    }
  }
}


Builder::iterator SsaConstructionPass::handleLabel(Builder::iterator op) {
  m_block = op->getDef();
  return ++op;
}


Builder::iterator SsaConstructionPass::handleBlockTerminator(Builder::iterator op) {
  /* Reset local block tracking and make it easier to look
   * up the block for a given branch */
  auto block = std::exchange(m_block, SsaDef());
  m_metadata[op->getDef()] = block;
  return ++op;
}


Builder::iterator SsaConstructionPass::handlePhi(Builder::iterator op) {
  auto var = m_metadata[op->getDef()];
  insertDef(m_block, var, op->getDef());

  return ++op;
}


Builder::iterator SsaConstructionPass::handleTmpLoad(Builder::iterator op) {
  auto var = SsaDef(op->getOperand(0u));
  auto def = lookupVariableInBlock(m_block, var);

  dxbc_spv_assert(def);

  return m_builder.iter(m_builder.rewriteDef(op->getDef(), def));
}


Builder::iterator SsaConstructionPass::handleTmpStore(Builder::iterator op) {
  auto var = SsaDef(op->getOperand(0u));
  auto def = SsaDef(op->getOperand(1u));

  insertDef(m_block, var, def);

  return m_builder.iter(m_builder.removeOp(*op));
}


SsaDef SsaConstructionPass::lookupVariableInBlock(SsaDef block, SsaDef var) {
  util::small_vector<SsaDef, 16u> blockQueue;

  /* Query global look-up table */
  SsaDef def = { };

  while (block) {
    auto entry = m_globalDefs.find(SsaPassTempKey(block, var));

    if (entry != m_globalDefs.end())
      def = entry->second;

    if (def)
      break;

    /* If the block only has one predecessor, use
     * its definition of the variable directly */
    SsaDef pred = findOnlyPredecessor(block);

    if (!pred) {
      def = insertPhi(block, var);
      break;
    }

    blockQueue.push_back(std::exchange(block, pred));
  }

  while (!blockQueue.empty()) {
    insertDef(blockQueue.back(), var, def);
    blockQueue.pop_back();
  }

  return def;
}


void SsaConstructionPass::insertDef(SsaDef block, SsaDef var, SsaDef def) {
  m_globalDefs.insert_or_assign(SsaPassTempKey(block, var), def);
}


SsaDef SsaConstructionPass::insertPhi(SsaDef block, SsaDef var) {
  dxbc_spv_assert(m_builder.getOp(block).getOpCode() == OpCode::eLabel);

  /* Keep phis in insertion order. Not super important but makes
   * the output a bit clearer to read. */
  auto phi = m_builder.addAfter(block, Op::Phi(m_builder.getOp(var).getType()));
  m_metadata[phi] = var;

  insertDef(block, var, phi);

  m_phi.emplace_back(block, phi);
  return phi;
}


SsaDef SsaConstructionPass::evaluatePhi(SsaDef block, SsaDef phi) {
  Op op = m_builder.getOp(phi);

  /* Variable that this phi was for */
  auto var = m_metadata[phi];

  /* Iterate over all predecessors and */
  auto [a, b] = m_builder.getUses(block);

  for (auto use = a; use != b; use++) {
    if (isBranchInstruction(use->getOpCode())) {
      /* Predecessor is filled */
      auto pred = m_metadata[use->getDef()];
      dxbc_spv_assert(pred);

      auto def = lookupVariableInBlock(pred, var);
      op.addPhi(pred, def);
    }
  }

  m_builder.rewriteOp(phi, std::move(op));
  return phi;
}


SsaDef SsaConstructionPass::findOnlyPredecessor(SsaDef block) {
  SsaDef pred = { };

  auto [a, b] = m_builder.getUses(block);

  for (auto use = a; use != b; use++) {
    if (isBranchInstruction(use->getOpCode())) {
      if (pred) {
        /* Multiple predecessors */
        return SsaDef();
      }

      pred = findContainingBlock(use->getDef());
    }
  }

  return pred;
}


SsaDef SsaConstructionPass::findContainingBlock(SsaDef def) {
  dxbc_spv_assert(isBlockTerminator(m_builder.getOp(def).getOpCode()));

  if (!m_metadata[def])
    m_metadata[def] = ir::findContainingBlock(m_builder, def);

  return m_metadata[def];
}


SsaDef SsaConstructionPass::getOnlyUniquePhiOperand(SsaDef phi) {
  dxbc_spv_assert(m_builder.getOp(phi).getOpCode() == OpCode::ePhi);

  SsaDef unique = { };

  forEachPhiOperand(m_builder.getOp(phi), [phi, &unique] (SsaDef, SsaDef value) {
    if (!unique && value != phi)
      unique = value;
    else if (value != unique && value != phi)
      unique = phi;
  });

  if (unique == phi)
    return SsaDef();

  if (!unique)
    unique = m_builder.makeUndef(m_builder.getOp(phi).getType());

  return unique;
}


SsaDef SsaConstructionPass::createExitPhi(SsaDef def, SsaDef block) {
  Op phi(OpCode::ePhi, m_builder.getOp(def).getType());

  auto [a, b] = m_builder.getUses(block);

  for (auto iter = a; iter != b; iter++) {
    if (isBranchInstruction(iter->getOpCode()))
      phi.addPhi(ir::findContainingBlock(m_builder, iter->getDef()), def);
  }

  return m_builder.addAfter(block, std::move(phi));
}

}
