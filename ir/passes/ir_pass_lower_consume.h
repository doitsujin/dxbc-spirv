#pragma once

#include <utility>

#include "../ir.h"
#include "../ir_builder.h"

#include "../../util/util_small_vector.h"

namespace dxbc_spv::ir {

/** Pass to lower ConsumeAs instructions to casts and conversions.
 *
 * Also provides utility functionality to deal with consumes while
 * consume operations are still present. */
class LowerConsumePass {

public:

  LowerConsumePass(Builder& builder);

  ~LowerConsumePass();

  LowerConsumePass             (const LowerConsumePass&) = delete;
  LowerConsumePass& operator = (const LowerConsumePass&) = delete;

  /** Eliminates redundant consume and cast operations.
   *  Returns true if any progress was made. */
  bool resolveCastChains();

  /** Initializes and runs pass to resolve cast
   *  chains on the given builder only. */
  static bool runResolveCastChainsPass(Builder& builder);

private:

  Builder& m_builder;

  std::pair<bool, Builder::iterator> handleCastChain(Builder::iterator op);

};

}
