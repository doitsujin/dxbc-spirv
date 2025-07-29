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

  /** Runs one-time lowering passes and one-time transforms. */
  void runLowering();

  /** Initializes and runs lowering passes on the given builder. */
  static void runLoweringPasses(Builder& builder, const Options& options);

private:

  Builder& m_builder;

  Options m_options;

  OpFlags m_fp16Flags = 0u;
  OpFlags m_fp32Flags = 0u;
  OpFlags m_fp64Flags = 0u;

  void lowerInstructions();

  void performInitialTransforms();

  Builder::iterator lowerDot(Builder::iterator op);

  Builder::iterator fuseMad(Builder::iterator op);

  Builder::iterator fuseMadOperand(Builder::iterator op, uint32_t operand);

  bool canFuseMadOperand(Builder::iterator op, uint32_t operand) const;

  SsaDef extractFromVector(SsaDef vector, uint32_t component);

  OpFlags getFpFlags(const Op& op) const;

};

}
