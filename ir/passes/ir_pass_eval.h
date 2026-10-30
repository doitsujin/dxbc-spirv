#pragma once

#include <utility>

#include "../ir.h"
#include "../ir_builder.h"
#include "../ir_utils.h"

namespace dxbc_spv::ir {

/** Pass to evaluate compile-time instructions. */
class EvalCompileTimePass {

public:

  EvalCompileTimePass(Builder& builder);

  ~EvalCompileTimePass();

  EvalCompileTimePass             (const EvalCompileTimePass&) = delete;
  EvalCompileTimePass& operator = (const EvalCompileTimePass&) = delete;

  /** Runs pass. */
  bool run(bool positiveOnly);

  /** Initializes and runs pass on the given builder. */
  static bool runPass(Builder& builder, bool positiveOnly);

private:

  Builder& m_builder;

  bool evalIsSame(Builder::iterator op);

};

}
