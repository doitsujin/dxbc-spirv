#pragma once

#include <unordered_map>
#include <utility>
#include <vector>

#include "ir_pass_ssa_utils.h"

#include "../ir.h"
#include "../ir_builder.h"
#include "../ir_utils.h"

#include "../../util/util_hash.h"

namespace dxbc_spv::ir {

/** Pass to resolve temporary (DclTemp) load/store into SSA form.
 *
 * The algorithm is based on 'Simple and Efficient Construction of
 * Static Single Assignment Form' (Braun, Buchwald et al.). The
 * paper can be found at: https://c9x.me/compile/bib/braun13cc.pdf */
class SsaConstructionPass {

public:

  SsaConstructionPass(Builder& builder);

  ~SsaConstructionPass();

  SsaConstructionPass             (const SsaConstructionPass&) = delete;
  SsaConstructionPass& operator = (const SsaConstructionPass&) = delete;

  /** Runs pass. */
  void runPass();

  /** Runs trivial phi elimination pass */
  void resolveTrivialPhi();

  /** Validates pre-conditions of the pass, specifically that control flow
   *  is valid and all tempooraries are used in exactly one function. */
  bool validatePreConditions(std::ostream& str) const;

  /** Validates post-conditions of the pass. This includes:
   *  - All temporaries are removed.
   *  - All blocks are filled and sealed.
   *  - All inserted phi instructions are valid. */
  bool validatePostConditions(std::ostream& str) const;

  /** Initializes and runs pass on the given builder. */
  static void runPass(Builder& builder);

  /** Runs pass to resolve trivial phi. This is automatically invoked
   *  once when running the regular SSA construction pass, but may be
   *  required after passes that change control flow or merge arithmetic
   *  operations as well. */
  static void runResolveTrivialPhiPass(Builder& builder);

private:

  Builder& m_builder;

  /* Current block */
  SsaDef m_block = { };

  /* Per-def metadata.
   * - For branch instructions, this points to the containing block.
   * - For phi instructions, this points to the temporary variable. */
  Container<SsaDef> m_metadata;

  /* Per-block state */
  Container<SsaPassBlockState> m_blocks;

  /* Global look-up table that stores the last valid definition of each
   * temporary variable for each block where it is used, including phis. */
  std::unordered_map<SsaPassTempKey, SsaDef> m_globalDefs;


  void resolveTempLoadStore();

  void removeTempDecls();

  Builder::iterator handleLabel(Builder::iterator op);

  Builder::iterator handleBlockTerminator(Builder::iterator op);

  Builder::iterator handlePhi(Builder::iterator op);

  Builder::iterator handleTmpLoad(Builder::iterator op);

  Builder::iterator handleTmpStore(Builder::iterator op);

  SsaDef lookupVariableInBlock(SsaDef block, SsaDef var);

  SsaDef getDefForVariable(SsaDef var);

  void insertDef(SsaDef block, SsaDef var, SsaDef def);

  SsaDef insertPhi(SsaDef block, SsaDef var);

  SsaDef evaluatePhi(SsaDef block, SsaDef phi);

  void fillBlock(SsaDef block, SsaDef terminator);

  void sealBlock(SsaDef block);

  bool canSealBlock(SsaDef block);

  SsaDef findOnlyPredecessor(SsaDef block);

  SsaDef findContainingBlock(SsaDef def);

  bool validateLabel(std::ostream& str, const Op& label) const;

  SsaDef getOnlyUniquePhiOperand(SsaDef phi);

};




}
