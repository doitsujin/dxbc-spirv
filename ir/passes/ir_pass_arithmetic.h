#pragma once

#include "../ir.h"
#include "../ir_builder.h"

namespace dxbc_spv::ir {

/** Simple arithmetic transforms. */
class ArithmeticPass {

public:

  struct Options {
    /** Whether to unconditionally fuse multiply-add instructions. */
    bool fuseMad = true;
    /** Whether to lower dot products to a multiply-add chain. */
    bool lowerDot = true;
    /** Whether to lower sin/cos using a custom approximation. */
    bool lowerSinCos = false;
    /** Whether to lower msad to a custom function. */
    bool lowerMsad = true;
    /** Whether to lower D3D9 legacy instructions to a sequence
     *  that drivers are known to optimize. */
    bool lowerMulLegacy = true;
    /** Whether to lower packed f32tof16 to a custom function
     *  in order to get more correct round-to-zero behaviour. */
    bool lowerF32toF16 = true;
  };

  ArithmeticPass(Builder& builder, const Options& options);

  ~ArithmeticPass();

  ArithmeticPass             (const ArithmeticPass&) = delete;
  ArithmeticPass& operator = (const ArithmeticPass&) = delete;

  /** Runs arithmetic pass */
  bool runPass();

  /** Runs one-time lowering passes and one-time transforms. */
  void runEarlyLowering();
  void runLateLowering();

  /** Initializes and runs optimization pass on the given builder. */
  static bool runPass(Builder& builder, const Options& options);

  /** Initializes and runs lowering passes on the given builder. */
  static void runEarlyLoweringPasses(Builder& builder, const Options& options);
  static void runLateLoweringPasses(Builder& builder, const Options& options);

private:

  Builder& m_builder;

  Options m_options;

  OpFlags m_fp16Flags = 0u;
  OpFlags m_fp32Flags = 0u;
  OpFlags m_fp64Flags = 0u;

  void lowerInstructionsPreTransform();
  void lowerInstructionsPostTransform();

  Builder::iterator lowerDot(Builder::iterator op);

  Builder::iterator lowerClamp(Builder::iterator op);

  SsaDef extractFromVector(SsaDef vector, uint32_t component);

  Builder::iterator tryFuseClamp(Builder::iterator op);

  Builder::iterator tryFuseMad(Builder::iterator op);

  std::pair<bool, Builder::iterator> selectCompare(Builder::iterator op);

  std::pair<bool, Builder::iterator> selectBitOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> selectOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> propagateAbsUnary(Builder::iterator op);

  std::pair<bool, Builder::iterator> propagateAbsBinary(Builder::iterator op);

  std::pair<bool, Builder::iterator> propagateSignUnary(Builder::iterator op);

  std::pair<bool, Builder::iterator> propagateSignBinary(Builder::iterator op);

  std::pair<bool, Builder::iterator> propagateAbsSignSelect(Builder::iterator op);

  std::pair<bool, Builder::iterator> propagateAbsSignPhi(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveIdentityArithmeticOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveIdentityBoolOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveIdentityCompareOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveIdentitySelect(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveIdentityOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> reorderConstantOperandsCompareOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> reorderConstantOperandsCommutativeOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> reorderConstantOperandsOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> constantFoldArithmeticOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> constantFoldBoolOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> constantFoldCompare(Builder::iterator op);

  std::pair<bool, Builder::iterator> constantFoldSelect(Builder::iterator op);

  std::pair<bool, Builder::iterator> constantFoldOp(Builder::iterator op);

  bool allOperandsConstant(const Op& op) const;

  uint64_t getConstantAsUint(const Op& op, uint32_t index) const;

  int64_t getConstantAsSint(const Op& op, uint32_t index) const;

  double getConstantAsFloat(const Op& op, uint32_t index) const;

  bool isConstantSelect(const Op& op) const;

  bool isZeroConstant(const Op& op) const;

  OpFlags getFpFlags(const Op& op) const;

  template<typename T>
  static Operand makeScalarOperand(const Type& type, T value);

};

}
