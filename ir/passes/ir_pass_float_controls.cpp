#include <cmath>

#include "ir_pass_float_controls.h"

#include "../ir_utils.h"

namespace dxbc_spv::ir {

FloatControlPass::FloatControlPass(Builder& builder, const RangeAnalysis::Options& options)
: m_builder(builder), m_range(m_builder, options) {
  m_prserveOps.ensure(builder.getMaxValidDef());
}


FloatControlPass::~FloatControlPass() {

}


void FloatControlPass::run() {
  assignOpFlags();
}


void FloatControlPass::runPass(Builder& builder, const RangeAnalysis::Options& options) {
  FloatControlPass(builder, options).run();
}


void FloatControlPass::assignOpFlags() {
  auto [a, b] = m_builder.getCode();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getType().isBasicType() &&
        iter->getType().getBaseType(0u).isFloatType()) {
      m_builder.setOpFlags(iter->getDef(),
        iter->getFlags() | determineOpFlags(*iter));
    }
  }
}


OpFlags FloatControlPass::determineOpFlags(const Op& op) {
  auto flags = m_range.getFloatValue(op.getDef());

  /* uh? */
  if (!flags)
    return OpFlags();

  OpFlags result = { };

  if (!(flags & FloatValue::eNan))
    result |= OpFlag::eNoNan;

  if (!(flags & (FloatValue::eNegInf | FloatValue::ePosInf)))
    result |= OpFlag::eNoInf;

  if (!(flags & FloatValue::eNeg0))
    result |= OpFlag::eNoSz;

  if (!m_prserveOps.at(op.getDef()))
    result |= OpFlag::eNoSz;

  return result;
}

}
