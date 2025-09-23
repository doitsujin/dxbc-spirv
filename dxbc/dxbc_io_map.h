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

  /* Geometry shader stream index. If negative, this output is not
   * associated with any specific stream. */
  int32_t gsStream = -1;

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
   * various factors. For indexable outputs, this may be a function. */
  ir::SsaDef baseDef = { };

  /* Index into the base definition that corresponds to this variable.
   * If negative, the base definition cannot be dynamically indexed. */
  int32_t baseIndex = -1;

  /* Interpolation functions for dynamically indexed inputs. These all
   * take the register index and up to one additional parameter for the
   * interpolation instruction, and return the interpolated input vector. */
  ir::SsaDef evalCentroid = { };
  ir::SsaDef evalSample = { };
  ir::SsaDef evalSnapped = { };

  /* Checks whether the variable matches the given conditions */
  bool matches(RegisterType type, uint32_t index, int32_t stream, WriteMask mask) const {
    return type == regType && (mask & componentMask) && stream == gsStream &&
      (index >= regIndex && index < regIndex + regCount);
  }
};


/** Decomposed I/O register index */
struct IoRegisterIndex {
  RegisterType regType = RegisterType::eNull;
  ir::SsaDef vertexIndex = { };
  ir::SsaDef regIndexRelative = { };
  uint32_t regIndexAbsolute = 0u;
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
  bool init(const Container& dxbc, ShaderType shaderType);

  /** Handles geometry shader stream declarations. This affects subsequent
   *  I/O variable declarations, but not the way load/store ops work. */
  bool handleDclStream(const Operand& operand);

  /** Handles hull shader phases. Notably, this resets I/O indexing info. */
  void handleHsPhase();

  /** Handles an input or output declaration of any kind. If possible, this uses
   *  the signature to determine the correct layout for the declaration. */
  bool handleDclIoVar(ir::Builder& builder, const Instruction& op);

  /** Handles an index range declaration for I/O variables. */
  bool handleDclIndexRange(ir::Builder& builder, const Instruction& op);

  /** For geometry shaders, copies per-stream outputs from temporaries. */
  bool handleEmitVertex(ir::Builder& builder, uint32_t stream);

  /** Handles interpolation instruction on a given input. Since these operate
   *  on input variables directly rather than loading them first, they need
   *  special treatment. */
  bool handleEval(
          ir::Builder&            builder,
    const Instruction&            op);

  /** Loads an input or output value and returns a scalar or vector containing
   *  one element for each component in the component mask. Applies swizzles,
   *  but does not support modifiers in any capacity.
   *
   *  Uses the converter's functionality to process relative indices as necessary.
   *  The register index in particular must be immediate only, unless an index
   *  range is declared for the register in question.
   *
   *  Returns a \c null def on error. */
  ir::SsaDef emitLoad(
          ir::Builder&            builder,
    const Instruction&            op,
    const Operand&                operand,
          WriteMask               componentMask,
          ir::ScalarType          type);

  /** Stores a scalar or vector value to an output variable. The component
   *  type is ignored, but the component count must match that of the
   *  operand's write mask exactly.
   *
   *  Uses the converter's functionality to process relative indices as necessary.
   *  Indexing rules are identical to those for inputs.
   *
   *  Returns \c false on error. */
  bool emitStore(
          ir::Builder&            builder,
    const Instruction&            op,
    const Operand&                operand,
          ir::SsaDef              value);

  /** Emits hull shader control phase pass-through.
   *
   *  Declares all relevant I/O variables that haven't been emitted via fork and
   *  join phases yet, and emits loads and stores to forward inputs to the domain
   *  shader unmodified. Requires that the signatures match. */
  bool emitHsControlPointPhasePassthrough(ir::Builder& builder);

  /** Emits pass-through geometry shader based on the output
   *  signature of the incoming shader. */
  bool emitGsPassthrough(ir::Builder& builder);

  /** Clamps exported tess factors to the requested range. This must be the last
   *  thing done in any given patch constant phase to not disturb output loads. */
  bool applyMaxTessFactor(ir::Builder& builder);

private:

  Converter&      m_converter;
  ShaderType      m_shaderType = { };

  Signature       m_isgn = { };
  Signature       m_osgn = { };
  Signature       m_psgn = { };

  uint32_t        m_gsConvertedCount = 0u;

  ir::SsaDef      m_clipDistanceIn = { };
  ir::SsaDef      m_clipDistanceOut = { };

  ir::SsaDef      m_cullDistanceIn = { };
  ir::SsaDef      m_cullDistanceOut = { };

  ir::SsaDef      m_tessFactorInner = { };
  ir::SsaDef      m_tessFactorOuter = { };

  ir::SsaDef      m_vertexCountIn = { };

  ir::SsaDef      m_convertEvalOffsetFunction = { };

  IoVarList       m_variables;
  IoVarList       m_indexRanges;


  /* Declares and loads built-in for the incoming vertex count.
   * Used in geometry and tessellation shaders. */
  ir::SsaDef determineActualVertexCount(ir::Builder& builder);
  ir::SsaDef determineIncomingVertexCount(ir::Builder& builder, uint32_t arraySize);

  bool declareIoBuiltIn(ir::Builder& builder, RegisterType regType);

  bool declareIoRegisters(ir::Builder& builder, const Instruction& op, RegisterType regType);


  /* Declares non-builtin I/O variables based on the signature
   * that match the given register and component index. */
  bool declareIoSignatureVars(
          ir::Builder&            builder,
    const Signature*              signature,
          RegisterType            regType,
          uint32_t                regIndex,
          uint32_t                arraySize,
          WriteMask               componentMask,
          ir::InterpolationModes  interpolation);


  /* Declares built-in I/O variable for a system value that is
   * mapped to a regular register. */
  bool declareIoSysval(
          ir::Builder&            builder,
    const Signature*              signature,
          RegisterType            regType,
          uint32_t                regIndex,
          uint32_t                arraySize,
          WriteMask               componentMask,
          Sysval                  sv,
          ir::InterpolationModes  interpolation);


  /* Helper to declare a signature-backed built-in I/O variable. */
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


  /* Helper to declare a built-in I/O variable that uses a special
   * register in DXBC and generally does not have a signature entry. */
  bool declareDedicatedBuiltIn(
          ir::Builder&            builder,
          RegisterType            regType,
    const ir::BasicType&          type,
          ir::BuiltIn             builtIn,
    const char*                   semanticName);


  /* Declares clip and cull distances as indexable arrays,
   * taking the respective signature into account. */
  bool declareClipCullDistance(
          ir::Builder&            builder,
    const Signature*              signature,
          RegisterType            regType,
          uint32_t                regIndex,
          uint32_t                arraySize,
          WriteMask               componentMask,
          Sysval                  sv,
          ir::InterpolationModes  interpolation);


  /* Declares tess factor output as an indexable scalar array.
   * Index ranges for tess factors can map directly to this. */
  bool declareTessFactor(
          ir::Builder&            builder,
    const SignatureEntry*         signatureEntry,
          RegisterType            regType,
          uint32_t                regIndex,
          WriteMask               componentMask,
          Sysval                  sv);


  /* Declares and loads tess control point ID built-in. Relevant
   * for storing control point outputs. */
  ir::SsaDef loadTessControlPointId(ir::Builder& builder);


  /* Main function to load an I/O register. Returns a vector whose
   * component count matches that of the write mask exactly. */
  ir::SsaDef loadIoRegister(
          ir::Builder&            builder,
          ir::ScalarType          scalarType,
          RegisterType            regType,
          ir::SsaDef              vertexIndex,
          ir::SsaDef              regIndexRelative,
          uint32_t                regIndexAbsolute,
          Swizzle                 swizzle,
          WriteMask               writeMask);


  /* Main function to store an I/O register. The value must be a
   * vector type whose component count matches that of the write
   * mask exactly. */
  bool storeIoRegister(
          ir::Builder&            builder,
          RegisterType            regType,
          ir::SsaDef              vertexIndex,
          ir::SsaDef              regIndexRelative,
          uint32_t                regIndexAbsolute,
          int32_t                 stream,
          WriteMask               writeMask,
          ir::SsaDef              value);


  /* Main function to interpolate an input register. Functions
   * similarly to the regular load function. */
  ir::SsaDef interpolateIoRegister(
          ir::Builder&            builder,
          ir::OpCode              opCode,
          ir::ScalarType          scalarType,
          RegisterType            regType,
          ir::SsaDef              regIndexRelative,
          uint32_t                regIndexAbsolute,
          Swizzle                 swizzle,
          WriteMask               writeMask,
          ir::SsaDef              argument);


  /* Computes address vector into the given I/O register variable.
   * Takes scalar, vector and array types into account. */
  ir::SsaDef computeRegisterAddress(
          ir::Builder&            builder,
    const IoVarInfo&              var,
          ir::SsaDef              vertexIndex,
          ir::SsaDef              regIndexRelative,
          uint32_t                regIndexAbsolute,
          WriteMask               component);



  /* Helper function to load dynamically indexed inputs. */
  std::pair<ir::Type, ir::SsaDef> emitDynamicLoadFunction(
          ir::Builder&            builder,
    const IoVarInfo&              var,
          uint32_t                arraySize);


  /* Helper function to store dynamically indexed outputs. */
  std::pair<ir::Type, ir::SsaDef> emitDynamicStoreFunction(
          ir::Builder&            builder,
    const IoVarInfo&              var,
          uint32_t                arraySize);


  /* Builds function to convert evalSnapped offsets */
  ir::SsaDef emitConvertEvalOffsetFunction(
          ir::Builder&            builder);


  /* Builds interpolation function for a dynamically indexed input. */
  ir::SsaDef emitInterpolationFunction(
          ir::Builder&            builder,
    const IoVarInfo&              var,
          ir::OpCode              opCode);


  /* Retrieves or creates interpolation function for a dynamically
   * indexed input variable. */
  ir::SsaDef getInterpolationFunction(
          ir::Builder&            builder,
          IoVarInfo&              var,
          ir::OpCode              opCode);


  /* Retrieves the instruction that starts the shader's main function, or
   * the current hull shader phase. Used when emitting additional functions. */
  ir::SsaDef getCurrentFunction() const;


  /* Determines scalar type for a dynamically indexed array */
  ir::ScalarType getIndexedBaseType(
    const IoVarInfo&              var);


  /* Helper to process I/O register indexing. This will decompose the
   * register index into its absolute and immediate part. */
  IoRegisterIndex loadRegisterIndices(
          ir::Builder&            builder,
    const Instruction&            op,
    const Operand&                operand);


  /* Converts input to the given scalar type. This will handle boolean
   * conversions as well. */
  ir::SsaDef convertScalar(ir::Builder& builder, ir::ScalarType dstType, ir::SsaDef value);


  /* Looks up matching I/O variable in the given list. */
  IoVarInfo* findIoVar(IoVarList& list, RegisterType regType, uint32_t regIndex, int32_t stream, WriteMask mask);


  /* Assigns current stream index to GS outputs and emits temporary
   * variables to deal with register aliasing. */
  bool handleGsOutputStreams(ir::Builder& builder);


  void emitSemanticName(ir::Builder& builder, ir::SsaDef def, const SignatureEntry& entry) const;

  void emitDebugName(ir::Builder& builder, ir::SsaDef def, RegisterType type, uint32_t index, WriteMask mask, const SignatureEntry* sigEntry) const;

  Sysval determineSysval(const Instruction& op) const;

  ir::InterpolationModes determineInterpolationMode(const Instruction& op) const;

  RegisterType normalizeRegisterType(RegisterType regType) const;

  bool sysvalNeedsMirror(RegisterType regType, Sysval sv) const;

  bool sysvalNeedsBuiltIn(RegisterType regType, Sysval sv) const;

  ir::Op& addDeclarationArgs(ir::Op& declaration, RegisterType type, ir::InterpolationModes interpolation) const;

  bool isInputRegister(RegisterType type) const;

  const Signature* selectSignature(RegisterType type) const;

  bool initSignature(Signature& sig, util::ByteReader reader);

  int32_t getCurrentGsStream() const;

  static bool isRegularIoRegister(RegisterType type);

  static bool isInvariant(ir::BuiltIn builtIn);

};

}
