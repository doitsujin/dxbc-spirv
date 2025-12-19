#pragma once

#include <cstdint>

#include "sm3_parser.h"
#include "sm3_types.h"

#include "../ir/ir.h"
#include "../ir/ir_builder.h"

namespace dxbc_spv::sm3 {

class Converter;

struct ConstantRange {
  /** Declaration of a buffer that has the debug CTAB name. (Only used for debugging.). */
  ir::SsaDef namedBufferDef = { };

  /** The constant register index of the element in this array. */
  uint32_t startIndex = 0u;

  /** The amount of constants in this array. */
  uint32_t count = 256u;
};

template<typename T>
struct Vec4 {
  T x;
  T y;
  T z;
  T w;
};

template<typename T>
struct DefinedConst {
  T value;
  uint32_t index;
  ir::SsaDef def;
};

template<typename T>
struct Constants {
  /** All constant array ranges for this constant type. This will only contain more than
   * one element if debug names are enabled. */
  util::small_vector<ConstantRange, 8u> constantRanges;

  /** The highest index of any constant of this type that gets read. */
  uint32_t maxAccessedConstant = 0u;

  /** All statically defined constants of this constant type. */
  util::small_vector<DefinedConst<T>, 2u> definedConstants;

  /** The highest index of any constant of this type that gets statically defined. */
  uint32_t maxDefinedConstant = 0u;

  ir::SsaDef bufferDef = { };
};


/** Resource variable map. Handles both texture declaration and access.
 *  Also takes care of textures getting accessed without getting declared first
 *  in SM1. On top of that it handles constant registers. */
class ResourceMap {

public:

  explicit ResourceMap(Converter& converter);

  ~ResourceMap();

  void initialize(ir::Builder& builder);

  void emitNamedConstantRanges(ir::Builder& builder, const ConstantTable& ctab);

  /** Loads data from a constant buffer using one or more BufferLoad
   *  instruction. If possible this will emit a vectorized load. */
  ir::SsaDef emitConstantLoad(
          ir::Builder&            builder,
    const Instruction&            op,
    const Operand&                operand,
          WriteMask               componentMask,
          ir::ScalarType          scalarType);

  void emitDefineConstant(
          ir::Builder& builder,
          RegisterType registerType,
          uint32_t index,
    const Operand& imm);

  void setInsertCursor(ir::SsaDef cursor) {
    m_functionInsertPoint = cursor;
  }

private:

  Converter& m_converter;

  Constants<Vec4<float>> m_floatConstants;
  Constants<Vec4<int32_t>> m_intConstants;
  Constants<bool> m_boolConstants;

  ir::SsaDef m_functionInsertPoint = { };

};

}
