#pragma once

#include "sm3_parser.h"
#include "sm3_types.h"

#include "../ir/ir_builder.h"

#include "../util/util_small_vector.h"

namespace dxbc_spv::sm3 {

class Converter;

/** I/O variable mapping entry. */
struct IoVarInfo {
  /* Semantic used to link vertex inputs to the vertex declaration
   * or shader I/O across stages in the original SM1-3 DXBC shader. */
  Semantic semantic = { };

  /* Register type
   * Has to be v/o on SM 3. */
  RegisterType registerType = RegisterType::eInput;

  /* Register index
   * o Registers are only used on VS 3.
   * v Registers are used on VS 1-3 or PS 3. */
  uint32_t registerIndex = 0u;

  /* Semantic used to link vertex inputs to the vertex declaration
   * or shader I/O across stages in the compiled shader.
   * We cannot use the registerIndex for this because VS and PS
   * might assign different indices to the same semantics and expect
   * linking to be done based on the semantic. */
  uint32_t location = 0u;

  /* Type of the underlying variable. Will generally match the declared
   * type of the base definition, unless that is a function. */
  ir::Type baseType = { };

   /* Variable definition. May be an input or an output. */
  ir::SsaDef baseDef = { };

  /* Scalar temp definitions for outputs. Outputs can't be turned into SSA
   * so work with those until the end of the shader. */
  std::array<ir::SsaDef, 4u> tempDefs = { };
};

/** I/O register map.
 *
 * This helper class resolves the complexities around declaring,
 * mapping and accessing input and output registers. */
class IoMap {
  constexpr static uint32_t SM3VSInputArraySize = 16u;
  constexpr static uint32_t SM3VSOutputArraySize = 12u;
  constexpr static uint32_t SM3PSInputArraySize = 10u;
  constexpr static uint32_t MaxIoArraySize = SM3VSInputArraySize;

  constexpr static uint32_t SM2TexCoordCount = 8u;
  constexpr static uint32_t SM2ColorCount = 2u;

  using IoVarList = util::small_vector<IoVarInfo, MaxIoArraySize>;
public:

  explicit IoMap(Converter& converter);

  ~IoMap();

  void initialize(ir::Builder& builder);

  void finalize(ir::Builder& builder);

  /** Handles an input or output declaration of any kind. If possible, this uses
   *  the semantic and register type to determine the correct layout for the declaration. */
  bool handleDclIoVar(ir::Builder& builder, const Instruction& op);

  /** Loads an input or output value and returns a scalar or vector containing
   *  one element for each component in the component mask. Applies swizzles,
   *  but does not support modifiers in any capacity.
   *
   *  Uses the converter's functionality to process relative addressing as necessary.
   *
   *  Returns a \c null def on error. */
  ir::SsaDef emitLoad(
          ir::Builder&            builder,
    const Instruction&            op,
    const Operand&                operand,
          WriteMask               componentMask,
          Swizzle                 swizzle,
          ir::ScalarType          type);

  /** Stores a scalar or vector value to an output variable. The component
   *  type is ignored, but the component count must match that of the
   *  operand's write mask exactly.
   *
   *  Uses the converter's functionality to process relative addressing as necessary.
   *
   *  Returns \c false on error. */
  bool emitStore(
          ir::Builder&            builder,
    const Instruction&            op,
    const Operand&                operand,
          WriteMask               writeMask,
          ir::SsaDef              predicateVec,
          ir::SsaDef              value);

private:

  Converter&      m_converter;

  IoVarList       m_variables;
  uint32_t        m_nextInputLocation = 12u;
  uint32_t        m_nextOutputLocation = 12u;

  ir::SsaDef      m_inputSwitchFunction = { };
  ir::SsaDef      m_outputSwitchFunction = { };

  ir::SsaDef emitDynamicLoadFunction(ir::Builder& builder) const;
  ir::SsaDef emitDynamicStoreFunction(ir::Builder& builder) const;

  void flushOutputs(ir::Builder& builder);

  static bool registerTypeIsInput(RegisterType regType, ShaderType shaderType) {
    return regType == RegisterType::eInput
      || (regType == RegisterType::eTexture && shaderType == ShaderType::ePixel)
      || regType == RegisterType::eMiscType
      || regType == RegisterType::ePixelTexCoord;
  }

  std::optional<ir::BuiltIn> determineBuiltinForRegister(RegisterType regType, uint32_t regIndex, Semantic semantic);

  void dclIoVar(
    ir::Builder& builder,
    RegisterType registerType,
    uint32_t     registerIndex,
    Semantic     semantic);

  /** Turns a front face boolean into a float. 1.0 for the front face, -1.0 for the back face. */
  ir::SsaDef emitFrontFaceFloat(ir::Builder& builder, ir::SsaDef isFrontFaceDef) const;

 /** Looks up matching I/O variable in the given list. */
 IoVarInfo* findIoVar(IoVarList& list, RegisterType regType, uint32_t regIndex);

  /** Converts input to the given scalar type. */
  ir::SsaDef convertScalar(ir::Builder& builder, ir::ScalarType dstType, ir::SsaDef value);

 /** Determines the appropriate semantic for a given register in shader model 1/2 */
 std::optional<Semantic> determineSemanticForRegister(RegisterType regType, uint32_t regIndex);

  void emitDebugName(
    ir::Builder& builder,
    ir::SsaDef def,
    RegisterType registerType,
    uint32_t registerIndex,
    WriteMask writeMask,
    Semantic semantic,
    bool isTemp) const;
};

}
