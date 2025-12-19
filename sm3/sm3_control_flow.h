#pragma once

#include "../ir/ir.h"
#include "../ir/ir_builder.h"

#include "../util/util_small_vector.h"

namespace dxbc_spv::sm3 {

struct ControlFlowConstruct {
  ir::SsaDef def;
  ir::SsaDef loopCounter;
  ir::SsaDef loopStep;
};

/** Control flow tracker. Provides some convenience functions to deal with nested
 *  control flow and emit scoped control flow instructions with proper references. */
class ControlFlow {

public:

  /** Returns definition and op code of innermost scoped control flow construct. */
  std::pair<const ControlFlowConstruct*, ir::OpCode> getConstruct(ir::Builder& builder) const {
    if (m_constructs.empty())
      return std::make_pair(nullptr, ir::OpCode::eUnknown);

    const auto& construct = m_constructs.back();
    return std::make_pair(&construct, builder.getOp(construct.def).getOpCode());
  }

  /** Returns definition of innermost scoped control flow construct. */
  const ControlFlowConstruct* getConstruct() const {
    if (m_constructs.empty())
      return nullptr;

    const auto& construct = m_constructs.back();
    return &construct;
  }


  /** Returns innermost loop construct for break instructions. */
  const ControlFlowConstruct* getBreakConstruct(ir::Builder& builder) const {
    return find(builder, [] (const ir::Builder& b, ir::SsaDef def) {
      return b.getOp(def).getOpCode() == ir::OpCode::eScopedLoop;
    });
  }


  /** Adds a nested control flow construct */
  ControlFlowConstruct& push(ir::SsaDef def) {
    auto& construct = m_constructs.emplace_back();
    construct.def = def;
    return construct;
  }


  /** Removes innermost flow construct */
  void pop() {
    dxbc_spv_assert(!m_constructs.empty());
    m_constructs.pop_back();
  }

private:

  util::small_vector<ControlFlowConstruct, 8u> m_constructs;

  /** Returns definition and opcode of innermost construct matching a predicate. */
  template<typename Pred>
  const ControlFlowConstruct* find(ir::Builder& builder, const Pred& pred) const {
    for (size_t i = m_constructs.size(); i; i--) {
      const auto& construct = m_constructs[i - 1u];

      if (pred(builder, construct.def))
        return &construct;
    }

    return nullptr;
  }

};

}
