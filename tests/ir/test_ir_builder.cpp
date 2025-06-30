#include "../../ir/ir_builder.h"

#include "../test_common.h"

namespace dxbc_spv::tests::ir {

using namespace dxbc_spv::ir;

void testIrBuilderEmpty() {
  Builder builder;

  ok(builder.getDefCount() == 1u);
  ok(builder.begin() == builder.end());

  auto [first, second] = builder.getInstructions();
  ok(first == second);

  std::tie(first, second) = builder.getDeclarations();
  ok(first == second);

  std::tie(first, second) = builder.getCode();
  ok(first == second);

  ok(!builder.getOp(SsaDef()));
}


void testIrBuilderInsertCode() {
  static const std::array<OpCode, 4u> s_declOps = {{
    OpCode::eEntryPoint,
    OpCode::eSetCsWorkgroupSize,
    OpCode::eDebugName,
    OpCode::eDebugName,
  }};

  static const std::array<OpCode, 4u> s_codeOps = {{
    OpCode::eFunction,
    OpCode::eLabel,
    OpCode::eReturn,
    OpCode::eFunctionEnd,
  }};

  Builder builder;
  auto funcEndDef = builder.add(Op::FunctionEnd());
  auto funcDef = builder.addBefore(funcEndDef, Op::Function(ScalarType::eVoid));

  auto entryPointDef = builder.add(Op::EntryPoint(funcDef, ShaderStage::eCompute));
  builder.add(Op::SetCsWorkgroupSize(entryPointDef, 16, 4, 1));

  builder.add(Op::DebugName(funcDef, "main"));
  builder.add(Op::DebugName(entryPointDef, "shader_name"));

  builder.addBefore(funcEndDef, Op::Return());
  builder.addAfter(funcDef, Op::Label());

  /* Verify that all declarative instructions are in the right place */
  auto decl = builder.getDeclarations();
  auto declCount = 0u;

  for (auto i = decl.first; i != decl.second; i++) {
    ok(i->getOpCode() == s_declOps[declCount++]);
    ok(i->isDeclarative());
  }

  ok(declCount == s_declOps.size());

  /* Verify that all code instructions are present and in the correct order */
  auto code = builder.getCode();
  auto codeCount = 0u;

  for (auto i = code.first; i != code.second; i++) {
    ok(i->getOpCode() == s_codeOps[codeCount++]);
    ok(!i->isDeclarative());
  }

  ok(codeCount == s_codeOps.size());

  /* Verify that all instructions have a unique SSA def */
  std::vector<bool> defs(builder.getDefCount());
  uint32_t defCount = 0u;

  for (auto op : builder) {
    defCount++;
    ok(!defs.at(op.getDef().getId()));
    defs.at(op.getDef().getId()) = true;
  }

  ok(defCount == codeCount + declCount);

  /* Insert dummy function, then remove it again */
  auto constDef = builder.add(Op::Constant(1));

  auto newFuncA = builder.addBefore(funcDef, Op::Function(ScalarType::eI32));
  auto newFuncB = builder.addAfter(newFuncA, Op::Return(ScalarType::eI32, constDef));
  auto newFuncC = builder.addAfter(newFuncB, Op::FunctionEnd());

  auto iter = builder.getCode().first;
  ok((iter++)->getOpCode() == OpCode::eFunction);
  ok((iter++)->getOpCode() == OpCode::eReturn);
  ok((iter++)->getOpCode() == OpCode::eFunctionEnd);
  ok((iter++)->getOpCode() == OpCode::eFunction);

  builder.remove(newFuncC);
  builder.remove(newFuncA);
  builder.remove(newFuncB);

  code = builder.getCode();
  codeCount = 0u;

  for (auto i = code.first; i != code.second; i++) {
    ok(i->getOpCode() == s_codeOps[codeCount++]);
    ok(!i->isDeclarative());
  }

  ok(codeCount == s_codeOps.size());

  /* Verify that adding new instructions does not bump the def count
   * since we should be reusing the previously allocated ones */
  uint32_t oldDef = builder.getDefCount();

  builder.addBefore(funcEndDef, Op::Label());
  builder.addBefore(funcEndDef, Op::Return());

  ok(builder.getDefCount() == oldDef);

  builder.addBefore(funcEndDef, Op::Label());
  builder.addBefore(funcEndDef, Op::Return());

  ok(builder.getDefCount() == oldDef + 1u);

  /* Ensure that there still aren't any duplicate assignments */
  defs = std::vector<bool>(builder.getDefCount());
  defCount = 0u;

  for (auto op : builder) {
    defCount++;
    ok(!defs.at(op.getDef().getId()));
    defs.at(op.getDef().getId()) = true;
  }

  ok(defCount == codeCount + declCount + 5u);
}


void testIrBuilder() {
  RUN_TEST(testIrBuilderEmpty);
  RUN_TEST(testIrBuilderInsertCode);
}

}
