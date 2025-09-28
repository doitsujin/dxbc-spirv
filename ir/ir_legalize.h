#pragma once

#include "ir.h"
#include "ir_builder.h"

#include "./passes/ir_pass_arithmetic.h"
#include "./passes/ir_pass_buffer_kind.h"
#include "./passes/ir_pass_cfg_cleanup.h"
#include "./passes/ir_pass_cfg_convert.h"
#include "./passes/ir_pass_cse.h"
#include "./passes/ir_pass_derivative.h"
#include "./passes/ir_pass_descriptor_indexing.h"
#include "./passes/ir_pass_function.h"
#include "./passes/ir_pass_lower_consume.h"
#include "./passes/ir_pass_lower_min16.h"
#include "./passes/ir_pass_propagate_resource_types.h"
#include "./passes/ir_pass_propagate_types.h"
#include "./passes/ir_pass_remove_unused.h"
#include "./passes/ir_pass_scalarize.h"
#include "./passes/ir_pass_scratch.h"
#include "./passes/ir_pass_ssa.h"
#include "./passes/ir_pass_sync.h"

namespace dxbc_spv::ir {

/** Compilation and lowering options */
struct CompileOptions {
  /* Scratch clean-up pass options */
  ir::CleanupScratchPass::Options scratchOptions = { };
  /* Arithmetic pass options. Enables lowering for certain
   * instructions and some basic code transforms. */
  ir::ArithmeticPass::Options arithmeticOptions = { };
  /* Min16 lowering options, these declare whether or not
   * integer or float types should be lowered to native
   * 16-bit types or remain 32-bit. */
  ir::LowerMin16Pass::Options min16Options = { };
  /* Resource type propagation options. These affect the
   * final layout of raw and structured buffers, constant
   * buffers, LDS variables, scratch variables, and the
   * immediate constant buffer. */
  ir::PropagateResourceTypesPass::Options resourceOptions = { };
  /* Options for when to use typed vs raw and structured buffers. */
  ir::ConvertBufferKindPass::Options bufferOptions = { };
  /* Scalarization options. Can be used to toggle between
   * full scalarization and maintaining vec2 for min16 code. */
  ir::ScalarizePass::Options scalarizeOptions = { };
  /* Options for the synchronization pass. */
  ir::SyncPass::Options syncOptions = { };
  /* Options for the derivative pass */
  ir::DerivativePass::Options derivativeOptions = { };
  /* Options for the common subexpression pass */
  ir::CsePass::Options cseOptions = { };
  /* Options for the descriptor indexing pass */
  ir::DescriptorIndexingPass::Options descriptorIndexing = { };
};

/** Invokes all required lowering passes on the IR on the given builder. */
void legalizeIr(ir::Builder& builder, const CompileOptions& options);

}
