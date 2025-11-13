#pragma once

#include <optional>
#include <unordered_set>

#include "../ir.h"
#include "../ir_builder.h"

namespace dxbc_spv::ir {

/** Simple arithmetic transforms. */
class ArithmeticPass {
  constexpr static double pi = 3.14159265359;
public:

  struct Options {
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
    /** Whether to lower float-to-int conversions to a custom
     *  function that clamps the representable range of the
     *  target integer type. */
    bool lowerConvertFtoI = true;
    /** Whether to lower the geometry shader input vertex count
     *  to a constant based on the declared primitive type. */
    bool lowerGsVertexCountIn = false;
    /** Work around an Nvidia bug where unsigned int-to-float
     *  conversions are broken for very large numbers. */
    bool hasNvUnsignedItoFBug = false;
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

  /** Runs pass to propagate invariance property */
  void propagateInvariance();

  /** Initializes and runs optimization pass on the given builder. */
  static bool runPass(Builder& builder, const Options& options);

  /** Initializes and runs lowering passes on the given builder. */
  static void runEarlyLoweringPasses(Builder& builder, const Options& options);
  static void runLateLoweringPasses(Builder& builder, const Options& options);

  /** Initializes and runs invariance pass */
  static void runPropagateInvariancePass(Builder& builder);

private:

  struct ConvertFunc {
    ScalarType dstType = ScalarType::eVoid;
    ScalarType srcType = ScalarType::eVoid;
    SsaDef function = { };
  };

  struct DotFunc {
    OpCode opCode = { };
    BasicType vectorType = { };
    ScalarType resultType = { };
    SsaDef function = { };
  };

  struct MulLegacyFunc {
    OpCode opCode = { };
    BasicType type = { };
    SsaDef function = { };
  };

  struct PowLegacyFunc {
    BasicType type = { };
    SsaDef function = { };
  };

  Builder& m_builder;

  Options m_options;

  OpFlags m_fp16Flags = 0u;
  OpFlags m_fp32Flags = 0u;
  OpFlags m_fp64Flags = 0u;

  util::small_vector<ConvertFunc, 8u> m_convertFunctions;
  util::small_vector<DotFunc, 8u> m_dotFunctions;
  util::small_vector<MulLegacyFunc, 8u> m_mulLegacyFunctions;
  util::small_vector<PowLegacyFunc, 8u> m_powLegacyFunctions;

  SsaDef m_f32tof16Function = { };
  SsaDef m_msadFunction = { };
  SsaDef m_sincosFunction = { };

  uint32_t m_gsInputVertexCount = 0u;

  std::unordered_set<SsaDef> m_visitedBlocks;

  void lowerInstructionsPreTransform();
  void lowerInstructionsPostTransform();

  void fuseMultiplyAdd();

  void propagateInvariance(const Op& base);

  Builder::iterator fuseMad(Builder::iterator op);

  Builder::iterator lowerLegacyOp(Builder::iterator op);

  Builder::iterator lowerDot(Builder::iterator op);

  Builder::iterator lowerClamp(Builder::iterator op);

  Builder::iterator lowerConvertFtoI(Builder::iterator op);

  Builder::iterator lowerConvertItoF(Builder::iterator op);

  Builder::iterator lowerF32toF16(Builder::iterator op);

  Builder::iterator lowerF16toF32(Builder::iterator op);

  Builder::iterator lowerMsad(Builder::iterator op);

  Builder::iterator lowerSinCos(Builder::iterator op);

  Builder::iterator lowerInputBuiltIn(Builder::iterator op);

  Op emitMulLegacy(const Type& type, SsaDef a, SsaDef b);

  Op emitMadLegacy(const Type& type, SsaDef a, SsaDef b, SsaDef c);

  SsaDef buildMulLegacyFunc(OpCode opCode, BasicType type);

  SsaDef buildPowLegacyFunc(BasicType type);

  SsaDef buildF32toF16Func();

  Builder::iterator tryFuseClamp(Builder::iterator op);

  std::pair<bool, Builder::iterator> selectCompare(Builder::iterator op);

  std::pair<bool, Builder::iterator> selectBitOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> selectPhi(Builder::iterator op);

  std::pair<bool, Builder::iterator> selectFDot(Builder::iterator op);

  std::pair<bool, Builder::iterator> selectFMul(Builder::iterator op);

  std::pair<bool, Builder::iterator> selectFAdd(Builder::iterator op);

  std::pair<bool, Builder::iterator> selectOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveCastOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveIdentityArithmeticOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveIdentityBoolOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveIdentityCompareOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveIdentitySelect(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveIdentityF16toF32(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveIdentityConvertFtoF(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveIsNanCheck(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveCompositeExtract(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveIdentityOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveBuiltInCompareOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> vectorizeF32toF16(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveIntSignCompareOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveIntSignBinaryOp(Builder::iterator op, bool considerConstants);

  std::pair<bool, Builder::iterator> resolveIntSignUnaryOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveIntSignUnaryConsumeOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveIntSignShiftOp(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveIntSignOp(Builder::iterator op);

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

  bool isFloatSelect(const Op& op) const;

  bool isConstantSelect(const Op& op) const;

  bool isConstantValue(const Op& op, int64_t value) const;

  std::optional<bool> evalBAnd(const Op& a, const Op& b) const;

  bool shouldFlipOperands(const Op& op) const;

  OpFlags getFpFlags(const Op& op) const;

  std::optional<BuiltIn> getBuiltInInput(const Op& op) const;

  template<typename T>
  static Operand makeScalarOperand(const Type& type, T value);

  static bool checkIntTypeCompatibility(const Type& a, const Type& b);

  static bool isBitPreservingOp(const Op& op);

  static constexpr float sincosTaylorFactor(uint32_t power) {
    double result = 1.0;

    for (uint32_t i = 1; i <= power; i++)
      result *= pi * 0.25 / double(i);

    return float(result);
  }

};

}
