#pragma once

#include <vector>

#include "../ir.h"
#include "../ir_builder.h"
#include "../ir_range.h"

namespace dxbc_spv::ir {

/** Pass to tag floating point operations with float control
 *  hints. Should be run *before* the final lowering passes
 *  to retain semantics of various instructions. */
class FloatControlPass {

public:

  FloatControlPass(Builder& builder, const RangeAnalysis::Options& options);

  ~FloatControlPass();

  FloatControlPass             (const FloatControlPass&) = delete;
  FloatControlPass& operator = (const FloatControlPass&) = delete;

  /** Runs pass. */
  void run();

  /** Initializes and runs pass on the given builder. */
  static void runPass(Builder& builder, const RangeAnalysis::Options& options);

private:

  Builder& m_builder;

  RangeAnalysis m_range;

  Container<bool> m_prserveOps;

  void assignOpFlags();

  OpFlags determineOpFlags(const Op& op);

};

}
