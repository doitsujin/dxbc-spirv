#pragma once

#include "../../ir/ir_builder.h"

namespace dxbc_spv::test_api {

using namespace dxbc_spv::ir;

inline SsaDef setupTestFunction(Builder& builder, ShaderStage stage) {
  SsaDef defFuncA = builder.add(Op::Function(Type()));
  builder.add(Op::FunctionEnd());

  SsaDef defFuncB;
  SsaDef entryPoint;

  if (stage == ShaderStage::eHull) {
    defFuncB = builder.add(Op::Function(Type()));
    builder.add(Op::FunctionEnd());

    entryPoint = builder.add(Op::EntryPoint(defFuncA, defFuncB, stage));

    builder.add(Op::DebugName(defFuncA, "control_point"));
    builder.add(Op::DebugName(defFuncB, "patch_constant"));
  } else {
    entryPoint = builder.add(Op::EntryPoint(defFuncA, stage));
    builder.add(Op::DebugName(defFuncA, "main"));
  }

  builder.setCursor(defFuncA);

  return entryPoint;
}

}
