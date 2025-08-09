#pragma once

#include "../ir.h"
#include "../ir_builder.h"

namespace dxbc_spv::ir {

/** Simple pass to lower min-precision types to explicit types. This
 *  pass should be run early, and must be run before scalarization.
 *
 *  This pass also lowers MinValue / MaxValue instructions to the
 *  respective constants. */
class LowerMin16Pass {

public:

  struct Options {
    /** Whether to map MinF16 to F16 or F32. */
    bool enableFloat16 = true;
    /** Whether to map minimum-precision integer types
     *  to the respective 16-bit or 32-bit type. */
    bool enableInt16 = true;
  };

  LowerMin16Pass(Builder& builder, const Options& options);

  ~LowerMin16Pass();

  LowerMin16Pass             (const LowerMin16Pass&) = delete;
  LowerMin16Pass& operator = (const LowerMin16Pass&) = delete;

  /** Runs pass. */
  void run();

  /** Initializes and runs pass on the given builder. */
  static void runPass(Builder& builder, const Options& options);

private:

  Builder& m_builder;

  Options m_options;

  Builder::iterator handleConstant(Builder::iterator op);

  Builder::iterator handleUndef(Builder::iterator op);

  Builder::iterator handleOp(Builder::iterator op);

  Operand convertScalarConstant(Operand srcValue, ScalarType srcType) const;

  Type resolveType(Type type) const;

  BasicType resolveBasicType(BasicType type) const;

  ScalarType resolveScalarType(ScalarType type) const;

};

}
