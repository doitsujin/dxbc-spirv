#pragma once

#include "dxbc_parser.h"
#include "dxbc_types.h"

#include "../ir/ir_builder.h"

#include "../util/util_small_vector.h"

namespace dxbc_spv::dxbc {

class Converter;

/** Temporary register file.
 *
 * Implements temp array declarations (x#), as well as
 * loads and stores to x# and r# registers. */
class RegisterFile {

public:

  explicit RegisterFile(Converter& converter);

  ~RegisterFile();

  /** Handles hull shader phase. Each phase has its local set of
   *  temporary registers, so we need to discard them. */
  void handleHsPhase();


  /** Declares temporary array. */
  bool handleDclIndexableTemp(ir::Builder& builder, const Instruction& op);


  /** Loads temporary register. */
  ir::SsaDef emitLoad(
          ir::Builder&            builder,
    const Instruction&            op,
    const Operand&                operand,
          WriteMask               componentMask,
          ir::ScalarType          type);


  /** Stores temporary register. */
  bool emitStore(
          ir::Builder&            builder,
    const Instruction&            op,
    const Operand&                operand,
          ir::SsaDef              value);

private:

  Converter& m_converter;

  util::small_vector<ir::SsaDef, 256u> m_rRegs;
  util::small_vector<ir::SsaDef,  16u> m_xRegs;

  ir::SsaDef loadArrayIndex(ir::Builder& builder, const Instruction& op, const Operand& operand);

  ir::SsaDef getOrDeclareTemp(ir::Builder& builder, uint32_t index, Component component);

  ir::SsaDef getIndexableTemp(uint32_t index);

};

}
