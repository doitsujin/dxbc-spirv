#pragma once

#include "../ir/ir.h"
#include "../ir/ir_builder.h"

#include "../util/util_small_vector.h"

namespace dxbc_spv::dxbc {

/** Control flow tracker. Provides some convenience functions to deal with nested
 *  control flow and emit scoped control flow instructions with proper references. */
class ControlFlow {

public:

  /** Returns definition and op code of innermost scoped control flow construct. */
  std::pair<ir::SsaDef, ir::OpCode> getConstruct(ir::Builder& builder) const {
    if (m_constructs.empty())
      return std::make_pair(ir::SsaDef(), ir::OpCode::eUnknown);

    auto def = m_constructs.back();
    return std::make_pair(def, builder.getOp(def).getOpCode());
  }


  /** Returns innermost loop construct for continue instructions. */
  std::pair<ir::SsaDef, ir::OpCode> getContinueConstruct(ir::Builder& builder) const {
    return find(builder, [] (const ir::Builder& b, ir::SsaDef def) {
      return b.getOp(def).getOpCode() == ir::OpCode::eScopedLoop;
    });
  }


  /** Returns innermost loop or switch construct for break instructions. */
  std::pair<ir::SsaDef, ir::OpCode> getBreakConstruct(ir::Builder& builder) const {
    return find(builder, [] (const ir::Builder& b, ir::SsaDef def) {
      auto opCode = b.getOp(def).getOpCode();

      return opCode == ir::OpCode::eScopedLoop ||
             opCode == ir::OpCode::eScopedSwitch;
    });
  }


  /** Adds a nested control flow construct */
  void push(ir::SsaDef def) {
    m_constructs.push_back(def);
  }


  /** Removes innermost flow construct */
  void pop() {
    dxbc_spv_assert(!m_constructs.empty());
    m_constructs.pop_back();
  }

private:

  util::small_vector<ir::SsaDef, 64u> m_constructs;

  /** Returns definition and opcode of innermost construct matching a predicate. */
  template<typename Pred>
  std::pair<ir::SsaDef, ir::OpCode> find(ir::Builder& builder, const Pred& pred) const {
    for (size_t i = m_constructs.size(); i; i--) {
      auto def = m_constructs[i - 1u];

      if (pred(builder, def))
        return std::make_pair(def, builder.getOp(def).getOpCode());
    }

    return std::make_pair(ir::SsaDef(), ir::OpCode::eUnknown);
  }

};

}
