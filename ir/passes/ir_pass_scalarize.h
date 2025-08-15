#pragma once

#include <iostream>

#include "../ir.h"
#include "../ir_builder.h"

#include "../../util/util_small_vector.h"

namespace dxbc_spv::ir {

/** Pass to scalarize arithmetic and conversion instructions.
 *
 * This ensures that arithmetic instructions all operate on either scalars,
 * or dword-width vectors if explicitly allowed. Additionally, this pass
 * will remove redundant composite construct and extract instructions. */
class ScalarizePass {

public:

  struct Options {
    /* Whether to allow sub-dword arithmetic operations to remain
     * vectorized to the extent that the vector size equals a dword. */
    bool subDwordVectors = true;
  };

  ScalarizePass(Builder& builder, const Options& options);

  ~ScalarizePass();

  ScalarizePass             (const ScalarizePass&) = delete;
  ScalarizePass& operator = (const ScalarizePass&) = delete;

  /** Runs scalarization pass and eliminates redundant composites. */
  void run();

  /** Resolves redundant composite construct and extract chains.
   *  Returns true if any progress was made. */
  bool resolveRedundantComposites();

  /** Initializes and runs pass on the given builder. */
  static void runPass(Builder& builder, const Options& options);

  /** Initializes and runs redundant composite resolve pass. */
  static bool runResolveRedundantCompositesPass(Builder& builder);

private:

  Builder& m_builder;

  Options m_options;

  void scalarizeVectorOps();

  uint32_t determineVectorSize(ScalarType type) const;

  SsaDef extractOperandComponents(SsaDef operand, uint32_t first, uint32_t count);

  SsaDef assembleResultVector(uint32_t partCount, const SsaDef* partDefs);

  Builder::iterator scalarizeOp(Builder::iterator op, uint32_t dstStep, uint32_t srcStep);

  Builder::iterator handlePhi(Builder::iterator op);

  Builder::iterator handleCastConsume(Builder::iterator op);

  Builder::iterator handleGenericOp(Builder::iterator op, bool vectorizeSubDword);

  std::pair<bool, Builder::iterator> resolveCompositeConstruct(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveCompositeConstructFromExtract(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveCompositeConstructFromConstant(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveCompositeConstructFromUndef(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveCompositeExtract(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveCompositeExtractFromConstruct(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveCompositeExtractFromConstant(Builder::iterator op);

  std::pair<bool, Builder::iterator> resolveCompositeExtractFromUndef(Builder::iterator op);

};

}
