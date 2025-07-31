#include "ir_pass_ssa.h"

#include "../ir_validation.h"

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
}


bool SsaConstructionPass::validatePreConditions(std::ostream& str) const {
  if (!Validator(m_builder).validateStructuredCfg(str))
    return false;

  Container<SsaDef> useFuncs;

  auto [a, b] = m_builder.getCode();

  SsaDef func = { };

  for (auto op = a; op != b; op++) {
    switch (op->getOpCode()) {
      case OpCode::eFunction: {
        func = op->getDef();
      } break;

      case OpCode::eTmpLoad:
      case OpCode::eTmpStore: {
        auto var = SsaDef(op->getOperand(0u));

        if (useFuncs[var] && useFuncs[var] != func) {
          str << "Temp " << var << " used in multiple functions (" << func << " and " << useFuncs[var] << ")." << std::endl;
          return false;
        }
      } break;

      default:
        break;
    }
  }

  return true;
}


bool SsaConstructionPass::validatePostConditions(std::ostream& str) const {
  if (!Validator(m_builder).validatePhi(str))
    return false;

  auto [a, b] = m_builder.getCode();

  for (auto op = a; op != b; op++) {
    bool result = [this, op, &str] {
      switch (op->getOpCode()) {
        case OpCode::eLabel:
          return validateLabel(str, *op);

        default:
          return true;
      }
    } ();

    if (!result)
      return result;
  }


  return true;
}


void SsaConstructionPass::runPass(Builder& builder) {
  SsaConstructionPass pass(builder);
  pass.runPass();
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

  /* If a block has no predecessor, seal it immediately */
  if (!m_blocks[m_block].isSealed && canSealBlock(m_block))
    sealBlock(m_block);

  return ++op;
}


Builder::iterator SsaConstructionPass::handleBlockTerminator(Builder::iterator op) {
  /* Commit all definitions to the global look-up table
   * and reset the block-local lookup table. */
  for (auto temp : m_localTemps)
    insertGlobalDef(m_block, temp, std::exchange(m_metadata[temp], SsaDef()));

  m_localTemps.clear();

  /* Reset local block tracking and make it easier to look
   * up the block for a given branch */
  auto block = std::exchange(m_block, SsaDef());
  m_metadata[op->getDef()] = block;

  /* Mark block as filled and recursively seal blocks. This
   * requires that the active block is set to null. */
  fillBlock(block, op->getDef());
  return ++op;
}


Builder::iterator SsaConstructionPass::handlePhi(Builder::iterator op) {
  auto var = m_metadata[op->getDef()];
  insertLocalDef(var, op->getDef());

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

  insertLocalDef(var, def);

  return m_builder.iter(m_builder.removeOp(*op));
}


SsaDef SsaConstructionPass::lookupVariableInBlock(SsaDef block, SsaDef var) {
  SsaDef def = { };

  if (block == m_block) {
    /* Query local look-up table */
    def = m_metadata[var];
  } else {
    /* Query global look-up table */
    auto entry = m_globalDefs.find(SsaPassTempKey(block, var));

    if (entry != m_globalDefs.end())
      def = entry->second;
  }

  if (def)
    return def;

  /* If the block only has one predecessor, use its definition directly */
  SsaDef pred = findOnlyPredecessor(block);

  if (pred) {
    def = lookupVariableInBlock(pred, var);
    insertDef(block, var, def);
    return def;
  }

  /* Insert operand-less phi. If the block is sealed, resolve it right away. */
  def = insertPhi(block, var);

  if (m_blocks[block].isSealed) {
    def = evaluatePhi(block, def);
    insertDef(block, var, def);
  }

  return def;
}


void SsaConstructionPass::insertLocalDef(SsaDef var, SsaDef def) {
  /* If this is the first time we're defining the variable in
   * the current block, we need to add it to the local list */
  if (!std::exchange(m_metadata[var], def))
    m_localTemps.push_back(var);
}


void SsaConstructionPass::insertGlobalDef(SsaDef block, SsaDef var, SsaDef def) {
  m_globalDefs.insert_or_assign(SsaPassTempKey(block, var), def);
}


void SsaConstructionPass::insertDef(SsaDef block, SsaDef var, SsaDef def) {
  if (block == m_block)
    insertLocalDef(var, def);
  else
    insertGlobalDef(block, var, def);
}


SsaDef SsaConstructionPass::insertPhi(SsaDef block, SsaDef var) {
  dxbc_spv_assert(m_builder.getOp(block).getOpCode() == OpCode::eLabel);

  /* Keep phis in insertion order. Not super important but makes
   * the output a bit clearer to read. */
  auto phi = m_builder.addAfter(block, Op::Phi(m_builder.getOp(var).getType()));
  m_metadata[phi] = var;

  insertDef(block, var, phi);
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
  return normalizePhi(phi);
}


SsaDef SsaConstructionPass::normalizePhi(SsaDef phi) {
  const auto& phiOp = m_builder.getOp(phi);

  SsaDef def = { };
  bool isTrivial = true;

  /* Filter out phis that have only a self-reference
   * and a single unique value */
  forEachPhiOperand(phiOp, [&] (SsaDef, SsaDef value) {
    if (def != value && def != phi)
      isTrivial = !def;

    if (!def && value != phi)
      def = value;
  });

  if (!isTrivial)
    return phi;

  /* Block has no predecessors */
  if (!def)
    def = m_builder.makeUndef(phiOp.getType());

  /* Remember phi ops that use this phi as an operand */
  util::small_vector<SsaDef, 16> uses;

  auto [a, b] = m_builder.getUses(phi);

  for (auto use = a; use != b; use++) {
    if (use->getOpCode() == OpCode::ePhi && use->getDef() != phi)
      uses.push_back(use->getDef());
  }

  /* Replace all uses of the phi with the unique value */
  m_builder.rewriteDef(phi, def);

  /* Recursively normalize phi uses */
  for (auto use : uses)
    normalizePhi(use);

  return def;
}


void SsaConstructionPass::fillBlock(SsaDef block, SsaDef terminator) {
  m_blocks[block].isFilled = true;

  /* Mark successors as sealed if all their predecessors are */
  const auto& terminatorOp = m_builder.getOp(terminator);

  forEachBranchTarget(terminatorOp, [this] (SsaDef target) {
    if (canSealBlock(target))
      sealBlock(target);
  });
}


void SsaConstructionPass::sealBlock(SsaDef block) {
  /* All phi instructions immediately follow the label. Since all predecessors
   * are filled, we can now gather phi operands. Trivial phis may get removed. */
  auto iter = m_builder.iter(m_builder.getNext(block));

  while (iter->getOpCode() == OpCode::ePhi) {
    auto phi = iter++;

    if (!phi->getOperandCount())
      evaluatePhi(block, phi->getDef());
  }

  m_blocks[block].isSealed = true;
}


bool SsaConstructionPass::canSealBlock(SsaDef block) {
  dxbc_spv_assert(m_builder.getOp(block).getOpCode() == OpCode::eLabel);

  auto [a, b] = m_builder.getUses(block);

  for (auto use = a; use != b; use++) {
    if (isBranchInstruction(use->getOpCode())) {
      /* Look up containing block for branch instruction. If this is null,
       * we haven't processed the block yet and it cannot be filled. */
      auto pred = m_metadata[use->getDef()];

      if (!pred || !m_blocks[pred].isFilled)
        return false;
    }
  }

  return true;
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


bool SsaConstructionPass::validateLabel(std::ostream& str, const Op& label) const {
  if (!m_blocks[label.getDef()].isFilled) {
    str << "Block " << label.getDef() << " not filled." << std::endl;
    return false;
  }

  if (!m_blocks[label.getDef()].isSealed) {
    str << "Block " << label.getDef() << " not sealed." << std::endl;
    return false;
  }

  return true;
}

}
