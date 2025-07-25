#include "ir_pass_cfg_convert.h"

namespace dxbc_spv::ir {

ConvertControlFlowPass::ConvertControlFlowPass(Builder& builder)
: m_builder(builder) {

}


ConvertControlFlowPass::~ConvertControlFlowPass() {

}


void ConvertControlFlowPass::run() {
  auto iter = m_builder.getCode().first;

  while (iter != m_builder.end()) {
    switch (iter->getOpCode()) {
      case OpCode::eLabel:
        iter = handleLabel(iter);
        break;

      case OpCode::eFunction:
        iter = handleFunction(iter);
        break;

      case OpCode::eFunctionEnd:
        iter = handleFunctionEnd(iter);
        break;

      case OpCode::eReturn:
      case OpCode::eUnreachable:
        iter = handleReturn(iter);
        break;

      case OpCode::eScopedIf:
        iter = handleIf(iter);
        break;

      case OpCode::eScopedElse:
        iter = handleElse(iter);
        break;

      case OpCode::eScopedEndIf:
        iter = handleEndIf(iter);
        break;

      case OpCode::eScopedLoop:
        iter = handleLoop(iter);
        break;

      case OpCode::eScopedLoopBreak:
        iter = handleLoopBreak(iter);
        break;

      case OpCode::eScopedLoopContinue:
        iter = handleLoopContinue(iter);
        break;

      case OpCode::eScopedEndLoop:
        iter = handleEndLoop(iter);
        break;

      case OpCode::eScopedSwitch:
        iter = handleSwitch(iter);
        break;

      case OpCode::eScopedSwitchCase:
        iter = handleCase(iter);
        break;

      case OpCode::eScopedSwitchDefault:
        iter = handleDefault(iter);
        break;

      case OpCode::eScopedSwitchBreak:
        iter = handleSwitchBreak(iter);
        break;

      case OpCode::eScopedEndSwitch:
        iter = handleEndSwitch(iter);
        break;

      default:
        ++iter;
    }
  }
}


void ConvertControlFlowPass::runPass(Builder& builder) {
  ConvertControlFlowPass pass(builder);
  pass.run();
}


ConvertControlFlowPass::ConstructInfo& ConvertControlFlowPass::findConstruct(SsaDef def) {
  /* If the code is valid, there will be a valid construct for the block */
  size_t index = m_constructs.size();

  while (m_constructs.at(--index).def != def)
    continue;

  return m_constructs.at(index);
}


Builder::iterator ConvertControlFlowPass::handleFunction(Builder::iterator op) {
  /* Create a new label immediately after the function declaration, but do not add it
   * to the list of labels to check for removal since it is never directly referenced
   * by any instruction. */
  dxbc_spv_assert(!m_currBlock);

  auto block = m_builder.addAfter(op->getDef(), Op::Label());
  return m_builder.iter(block);
}


Builder::iterator ConvertControlFlowPass::handleFunctionEnd(Builder::iterator op) {
  /* Add return instruction to terminate the current block, if any. */
  m_builder.addBefore(op->getDef(), Op::Return());

  m_currBlock = ir::SsaDef();
  return ++op;
}


Builder::iterator ConvertControlFlowPass::handleReturn(Builder::iterator op) {
  /* Begin new block immediately after the return instruction */
  auto block = m_builder.addAfter(op->getDef(), Op::Label());
  return m_builder.iter(block);
}


Builder::iterator ConvertControlFlowPass::handleIf(Builder::iterator op) {
  auto constructEnd = SsaDef(op->getOperand(0u));
  dxbc_spv_assert(m_builder.getOp(constructEnd).getOpCode() == OpCode::eScopedEndIf);

  /* Create if construct with no else block yet, we can do that on the fly */
  auto& construct = m_constructs.emplace_back();
  construct.def = op->getDef();
  construct.if_.mergeBlock = m_builder.addAfter(constructEnd, Op::Label());
  construct.if_.condTrueBlock = m_builder.addAfter(op->getDef(), Op::Label());
  construct.if_.condFalseBlock = construct.if_.mergeBlock;

  /* Add structured selection info to current block */
  m_builder.rewriteOp(m_currBlock, Op::LabelSelection(construct.if_.mergeBlock));
  return m_builder.iter(construct.if_.condTrueBlock);
}


Builder::iterator ConvertControlFlowPass::handleElse(Builder::iterator op) {
  auto& construct = findConstruct(SsaDef(op->getOperand(0u)));

  /* Create block for 'else' branch and replace 'else' instruction
   * with a branch from the 'if' block to the merge block. */
  construct.if_.condFalseBlock = m_builder.addAfter(op->getDef(), Op::Label());
  m_builder.rewriteOp(op->getDef(), Op::Branch(construct.if_.mergeBlock));

  return m_builder.iter(construct.if_.condFalseBlock);
}


Builder::iterator ConvertControlFlowPass::handleEndIf(Builder::iterator op) {
  auto& construct = findConstruct(SsaDef(op->getOperand(0u)));
  auto next = m_builder.iter(construct.if_.mergeBlock);

  /* Replace 'endif' instruction with branch to merge block */
  m_builder.rewriteOp(op->getDef(), Op::Branch(construct.if_.mergeBlock));

  /* Replace 'if' instruction with conditional branch */
  auto cond = SsaDef(m_builder.getOp(construct.def).getOperand(1u));

  m_builder.rewriteOp(construct.def, Op::BranchConditional(cond,
    construct.if_.condTrueBlock, construct.if_.condFalseBlock));

  m_constructs.pop_back();
  return next;
}


Builder::iterator ConvertControlFlowPass::handleLoop(Builder::iterator op) {
  auto constructEnd = SsaDef(op->getOperand(0u));
  dxbc_spv_assert(m_builder.getOp(constructEnd).getOpCode() == OpCode::eScopedEndLoop);

  /* Create loop construct and declare merge block after the loop. */
  auto& construct = m_constructs.emplace_back();
  construct.def = op->getDef();
  construct.loop_.mergeBlock = m_builder.addAfter(constructEnd, Op::Label());

  /* Declare continue block. This will branch back to the loop header. */
  construct.loop_.continueBlock = m_builder.addAfter(constructEnd, Op::Label());

  /* Declare body block. This is where the actual loop code will go. */
  construct.loop_.bodyBlock = m_builder.addAfter(op->getDef(), Op::Label());

  /* Declare header block with structured loop info. This block only branches to
   * the loop body and receives phi instructions during SSA constructions, and is
   * the branch target of the continue block. */
  construct.loop_.headerBlock = m_builder.addAfter(op->getDef(),
    Op::LabelLoop(construct.loop_.mergeBlock, construct.loop_.continueBlock));
  m_builder.addAfter(construct.loop_.headerBlock, Op::Branch(construct.loop_.bodyBlock));

  /* Insert branch back to the loop header inside the continue block. */
  m_builder.addAfter(construct.loop_.continueBlock, Op::Branch(construct.loop_.headerBlock));

  return m_builder.iter(construct.loop_.bodyBlock);
}


Builder::iterator ConvertControlFlowPass::handleLoopBreak(Builder::iterator op) {
  auto& construct = findConstruct(SsaDef(op->getOperand(0u)));

  /* Declare new block after the branch. This may be unreachable. */
  auto block = m_builder.addAfter(op->getDef(), Op::Label());

  /* Replace 'break' instruction with branch to merge block */
  m_builder.rewriteOp(op->getDef(), Op::Branch(construct.loop_.mergeBlock));

  return m_builder.iter(block);
}


Builder::iterator ConvertControlFlowPass::handleLoopContinue(Builder::iterator op) {
  auto& construct = findConstruct(SsaDef(op->getOperand(0u)));

  /* Declare new block after the branch. This may be unreachable. */
  auto block = m_builder.addAfter(op->getDef(), Op::Label());

  /* Replace 'continue' instruction with branch to continue block */
  m_builder.rewriteOp(op->getDef(), Op::Branch(construct.loop_.continueBlock));

  return m_builder.iter(block);
}


Builder::iterator ConvertControlFlowPass::handleEndLoop(Builder::iterator op) {
  auto& construct = findConstruct(SsaDef(op->getOperand(0u)));
  auto next = m_builder.iter(construct.loop_.mergeBlock);

  /* Replace 'endloop' instruction with branch to continue block */
  m_builder.rewriteOp(op->getDef(), Op::Branch(construct.loop_.continueBlock));

  /* Replace original 'loop' instruction with branch to loop header */
  m_builder.rewriteOp(construct.def, Op::Branch(construct.loop_.headerBlock));

  m_constructs.pop_back();
  return next;
}


Builder::iterator ConvertControlFlowPass::handleSwitch(Builder::iterator op) {
  auto constructEnd = SsaDef(op->getOperand(0u));
  dxbc_spv_assert(m_builder.getOp(constructEnd).getOpCode() == OpCode::eScopedEndSwitch);

  auto value = ir::SsaDef(op->getOperand(1u));

  /* Declare switch construct with a merge block, and assign it as the default. */
  auto& construct = m_constructs.emplace_back();
  construct.def = op->getDef();
  construct.switch_.mergeBlock = m_builder.addAfter(constructEnd, Op::Label());
  construct.switch_.switchOp = Op::Switch(value, construct.switch_.mergeBlock);

  /* Add structured selection info to current block */
  m_builder.rewriteOp(m_currBlock, Op::LabelSelection(construct.switch_.mergeBlock));

  /* Declare dummy block after the switch instruction, this will be unreachable. */
  auto block = m_builder.addAfter(op->getDef(), Op::Label());

  return m_builder.iter(block);
}


Builder::iterator ConvertControlFlowPass::handleCase(Builder::iterator op) {
  auto& construct = findConstruct(SsaDef(op->getOperand(0u)));
  auto cond = ir::SsaDef(m_builder.getOp(construct.def).getOperand(1u));

  /* Case values must be some sort of 32-bit integer */
  auto valueType = m_builder.getOp(cond).getType();
  auto value = m_builder.add(Op(OpCode::eConstant, valueType).addOperand(op->getOperand(1u)));

  /* Create label and add the case to the switch op */
  auto block = m_builder.addAfter(op->getDef(), Op::Label());
  construct.switch_.switchOp.addCase(value, block);

  /* Replace 'case' instruction with branch to the block */
  m_builder.rewriteOp(op->getDef(), Op::Branch(block));

  return m_builder.iter(block);
}


Builder::iterator ConvertControlFlowPass::handleDefault(Builder::iterator op) {
  auto& construct = findConstruct(SsaDef(op->getOperand(0u)));

  /* Create label and assign it as the default block for the switch */
  auto block = m_builder.addAfter(op->getDef(), Op::Label());
  construct.switch_.switchOp.setOperand(1u, block);

  /* Replace 'default' instruction with branch to the block */
  m_builder.rewriteOp(op->getDef(), Op::Branch(block));

  return m_builder.iter(block);
}


Builder::iterator ConvertControlFlowPass::handleSwitchBreak(Builder::iterator op) {
  auto& construct = findConstruct(SsaDef(op->getOperand(0u)));

  /* Declare new block after the branch. This may be unreachable. */
  auto block = m_builder.addAfter(op->getDef(), Op::Label());

  /* Replace 'break' instruction with branch to merge block */
  m_builder.rewriteOp(op->getDef(), Op::Branch(construct.switch_.mergeBlock));

  return m_builder.iter(block);
}


Builder::iterator ConvertControlFlowPass::handleEndSwitch(Builder::iterator op) {
  auto& construct = findConstruct(SsaDef(op->getOperand(0u)));
  auto next = m_builder.iter(construct.switch_.mergeBlock);

  /* Replace 'endif' instruction with branch to merge block */
  m_builder.rewriteOp(op->getDef(), Op::Branch(construct.switch_.mergeBlock));

  /* Replace 'switch' instruction with the built switch op */
  m_builder.rewriteOp(construct.def, std::move(construct.switch_.switchOp));

  m_constructs.pop_back();
  return next;
}


Builder::iterator ConvertControlFlowPass::handleLabel(Builder::iterator op) {
  m_currBlock = op->getDef();
  return ++op;
}


}
