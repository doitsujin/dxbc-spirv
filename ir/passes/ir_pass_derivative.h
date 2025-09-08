#pragma once

#include <optional>
#include <unordered_map>

#include "../ir.h"
#include "../ir_builder.h"
#include "../ir_divergence.h"
#include "../ir_dominance.h"

namespace dxbc_spv::ir {

/** Pass to fix up derivatives in non-uniform control flow. */
class DerivativePass {

public:

  struct Options {
    /** Whether to hoist explicit derivative instructions with complex input
     *  operands. If false, only derivatives with input loads will be moved. */
    bool hoistNontrivialDerivativeOps = true;
    /** Whether to handle implicit LOD instructions, i.e. compute derivatives
     *  for image sample instructions and relocating compute LOD instructions,
     *  with complex texture coordinates. If false, this will only be done for
     *  instructions whose coordinate vector is a shader input. */
    bool hoistNontrivialImplicitLodOps = false;
    /** Whether to also allow relocating descriptor loads to an earlier block.
     *  This may be unsafe in a D3D12 environment even if the descriptor in
     *  question is uniform, but allows moving the QueryLOD instruction, which
     *  cannot be emulated with explicit derivative calculations. */
    bool hoistDescriptorLoads = true;
  };

  DerivativePass(Builder& builder, const Options& options);

  ~DerivativePass();

  DerivativePass             (const DerivativePass&) = delete;
  DerivativePass& operator = (const DerivativePass&) = delete;

  /** Runs derivative pass. Returns true if any changes to the code were made. */
  bool run();

  static bool runPass(Builder& builder, const Options& options);

private:

  Builder&      m_builder;
  Options       m_options;

  ShaderStage   m_stage = { };

  struct DefBlockKey {
    SsaDef def    = { };
    SsaDef block  = { };

    bool operator == (const DefBlockKey& k) const { return def == k.def && block == k.block; }
    bool operator != (const DefBlockKey& k) const { return def != k.def && block != k.block; }
  };

  struct DefBlockHash {
    size_t operator () (const DefBlockKey& k) const {
      std::hash<SsaDef> hash;
      return util::hash_combine(hash(k.def), hash(k.block));
    }
  };

  std::optional<DominanceGraph>       m_dominance;
  std::optional<DivergenceAnalysis>   m_divergence;

  std::unordered_map<SsaDef, SsaDef>  m_opBlocks;

  void hoistInstruction(const Op& op, SsaDef block);

  void relocateInstructions();

  bool opRequiresQuadUniformControlFlow(const Op& op) const;

  bool canHoistDerivativeOp(const Op& derivOp, SsaDef dstBlock) const;

  bool isReadOnlyResource(const Op& op) const;

  bool derivativeIsZeroForArg(const Op& op) const;

};

}
