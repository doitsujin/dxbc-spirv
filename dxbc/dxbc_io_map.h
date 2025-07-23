#pragma once

#include "dxbc_container.h"
#include "dxbc_parser.h"
#include "dxbc_signature.h"
#include "dxbc_types.h"

#include "../ir/ir_builder.h"

#include "../util/util_small_vector.h"

namespace dxbc_spv::dxbc {

class Converter;

/** I/O variable mapping entry. Note that each I/O variable may have
 *  multiple mappings, e.g. if a built-in output is mirrored to a
 *  regular I/O variable, or if an input is part of an index range. */
struct IoVarInfo {
  /* Normalized register type to match. Must be one of Input, Output,
   * ControlPoint*, PatchConstant, or a built-in register. */
  RegisterType regType = RegisterType::eNull;

  /* Base register index and count in the declared range. For any
   * non-indexable indexed register, the register count must be 1. */
  uint32_t regIndex = 0u;
  uint32_t regCount = 0u;

  /* System value represented by this variable. There may be two entries
   * with overlapping register components where one has a system value
   * and the other does not. */
  Sysval sv = Sysval::eNone;

  /* Component write mask to match, if applicable. */
  WriteMask componentMask = { };

  /* Type of the underlying variable. Will generally match the declared
   * type of the base definition, unless that is a function. */
  ir::Type baseType = { };

  /* Variable definition. May be an input, output, control point input,
   * control point output, scratch, or temporary variable, depending on
   * various factors. For the fork / join instance ID, this is a function
   * parameter. */
  ir::SsaDef baseDef = { };

  /* Index into the base definition that corresponds to this variable.
   * If negative, the base definition cannot be dynamically indexed. */
  int32_t baseIndex = -1;

  /* Checks whether the variable matches the given conditions */
  bool matches(RegisterType type, uint32_t index, WriteMask mask) const {
    return type == regType && (mask & componentMask) &&
      (index >= regIndex && index < regIndex + regCount);
  }
};


/** I/O register map.
 *
 * This helper class resolves the complexities around declaring,
 * mapping and accessing input and output registers with the help
 * of I/O signatures. */
class IoMap {
  constexpr static uint32_t MaxIoArraySize = 32u;

  using IoVarList = util::small_vector<IoVarInfo, 32u>;
public:

  explicit IoMap(Converter& converter);

  ~IoMap();

  /** Initializes I/O map. If an error occurs with signature parsing, this
   *  will return false and shader processing must be aborted. */
  bool init(const Container& dxbc, ShaderInfo shaderInfo);

  /** Handles geometry shader stream declarations. This affects subsequent
   *  I/O variable declarations, but not the way load/store ops work. */
  bool handleDclStream(const Operand& operand);

  /** Handles hull shader phases. Notably, this resets I/O indexing info. */
  bool handleHsPhase();

  /** Handles an input or output declaration of any kind. If possible, this uses
   *  the signature to determine the correct layout for the declaration. */
  bool handleDclIoVar(ir::Builder& builder, const Instruction& op);

  /** Handles an index range declaration for I/O variables. */
  bool handleDclIndexRange(ir::Builder& builder, const Instruction& op);

  /** Loads an input or output value and returns a scalar or vector containing
   *  one element for each component in the component mask. Applies swizzles,
   *  but does not support modifiers in any capacity.
   *
   *  Uses the converter's functionality to process relative indices as necessary.
   *  The register index in particular must be immediate only, unless an index
   *  range is declared for the register in question.
   *
   *  Returns a \c null def on error. */
  ir::SsaDef emitLoad(ir::Builder& builder, const Operand& operand, WriteMask componentMask);

  /** Stores a scalar or vector value to an output variable. The component
   *  type is ignored, but the component count must match that of the
   *  operand's write mask exactly.
   *
   *  Uses the converter's functionality to process relative indices as necessary.
   *  Indexing rules are identical to those for inputs.
   *
   *  Returns \c false on error. */
  bool emitStore(ir::Builder& builder, const Operand& operand, ir::SsaDef value);

  /** Emits hull shader control phase pass-through.
   *
   *  Declares all relevant I/O variables that haven't been emitted via fork and
   *  join phases yet, and emits loads and stores to forward inputs to the domain
   *  shader unmodified. Requires that the signatures match. */
  bool emitHsControlPointPhasePassthrough(ir::Builder& builder);

private:

  Converter&      m_converter;
  ShaderInfo      m_shaderInfo = { };

  Signature       m_isgn = { };
  Signature       m_osgn = { };
  Signature       m_psgn = { };

  uint32_t        m_gsStream = 0u;

  ir::SsaDef      m_clipDistance = { };
  ir::SsaDef      m_cullDistance = { };

  ir::SsaDef      m_tessFactorInner = { };
  ir::SsaDef      m_tessFactorOuter = { };

  ir::SsaDef      m_vertexCountIn = { };

  IoVarList       m_variables;
  IoVarList       m_indexRanges;

  ir::SsaDef determineIncomingVertexCount(ir::Builder& builder, uint32_t arraySize);

  bool declareIoBuiltIn(ir::Builder& builder, RegisterType regType);

  bool declareIoRegisters(ir::Builder& builder, const Instruction& op, RegisterType regType);

  bool declareIoSignatureVars(
          ir::Builder&            builder,
    const Signature*              signature,
          RegisterType            regType,
          uint32_t                regIndex,
          uint32_t                arraySize,
          WriteMask               componentMask,
          ir::InterpolationModes  interpolation);

  bool declareIoSysval(
          ir::Builder&            builder,
    const Signature*              signature,
          RegisterType            regType,
          uint32_t                regIndex,
          uint32_t                arraySize,
          WriteMask               componentMask,
          Sysval                  sv,
          ir::InterpolationModes  interpolation);

  bool declareSimpleBuiltIn(
          ir::Builder&            builder,
    const SignatureEntry*         signatureEntry,
          RegisterType            regType,
          uint32_t                regIndex,
          WriteMask               componentMask,
          Sysval                  sv,
    const ir::Type&               type,
          ir::BuiltIn             builtIn,
          ir::InterpolationModes  interpolation);

  bool declareDedicatedBuiltIn(
          ir::Builder&            builder,
          RegisterType            regType,
    const ir::BasicType&          type,
          ir::BuiltIn             builtIn,
    const char*                   semanticName);

  bool declareClipCullDistance(
          ir::Builder&            builder,
    const Signature*              signature,
          RegisterType            regType,
          uint32_t                regIndex,
          uint32_t                arraySize,
          WriteMask               componentMask,
          Sysval                  sv,
          ir::InterpolationModes  interpolation);

  bool declareTessFactor(
          ir::Builder&            builder,
    const SignatureEntry*         signatureEntry,
          RegisterType            regType,
          uint32_t                regIndex,
          WriteMask               componentMask,
          Sysval                  sv);

  ir::SsaDef loadTessControlPointId(ir::Builder& builder);

  ir::SsaDef loadIoRegister(
          ir::Builder&            builder,
          ir::ScalarType          scalarType,
          RegisterType            regType,
          ir::SsaDef              vertexIndex,
          ir::SsaDef              regIndexRelative,
          uint32_t                regIndexAbsolute,
          Swizzle                 swizzle,
          WriteMask               writeMask);

  bool storeIoRegister(
          ir::Builder&            builder,
          RegisterType            regType,
          ir::SsaDef              vertexIndex,
          ir::SsaDef              regIndexRelative,
          uint32_t                regIndexAbsolute,
          WriteMask               writeMask,
          ir::SsaDef              value);

  ir::SsaDef computeRegisterAddress(
          ir::Builder&            builder,
    const IoVarInfo&              var,
          ir::SsaDef              vertexIndex,
          ir::SsaDef              regIndexRelative,
          uint32_t                regIndexAbsolute,
          WriteMask               component);

  std::pair<ir::Type, ir::SsaDef> emitDynamicLoadFunction(
          ir::Builder&            builder,
    const IoVarInfo&              var,
          uint32_t                arraySize);

  std::pair<ir::Type, ir::SsaDef> emitDynamicStoreFunction(
          ir::Builder&            builder,
    const IoVarInfo&              var,
          uint32_t                arraySize);

  ir::SsaDef getCurrentFunction() const;

  ir::ScalarType getIndexedBaseType(
    const IoVarInfo&              var) const;

  const IoVarInfo* findIoVar(const IoVarList& list, RegisterType regType, uint32_t regIndex, WriteMask mask) const;

  void emitSemanticName(ir::Builder& builder, ir::SsaDef def, const SignatureEntry& entry) const;

  void emitDebugName(ir::Builder& builder, ir::SsaDef def, RegisterType type, uint32_t index, WriteMask mask) const;

  Sysval determineSysval(const Instruction& op) const;

  ir::InterpolationModes determineInterpolationMode(const Instruction& op) const;

  RegisterType normalizeRegisterType(RegisterType regType) const;

  bool sysvalNeedsMirror(RegisterType regType, Sysval sv) const;

  bool sysvalNeedsBuiltIn(RegisterType regType, Sysval sv) const;

  ir::Op& addDeclarationArgs(ir::Op& declaration, RegisterType type, ir::InterpolationModes interpolation) const;

  bool isInputRegister(RegisterType type) const;

  const Signature* selectSignature(RegisterType type) const;

  bool initSignature(Signature& sig, util::ByteReader reader);

  uint32_t mapLocation(RegisterType regType, uint32_t regIndex) const;

  static bool isRegularIoRegister(RegisterType type);

};

}
