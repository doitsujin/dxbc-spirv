#pragma once

#include <optional>

#include "dxbc_converter.h"

#include "../ir/passes/ir_pass_arithmetic.h"
#include "../ir/passes/ir_pass_buffer_kind.h"
#include "../ir/passes/ir_pass_cfg_cleanup.h"
#include "../ir/passes/ir_pass_cfg_convert.h"
#include "../ir/passes/ir_pass_cse.h"
#include "../ir/passes/ir_pass_derivative.h"
#include "../ir/passes/ir_pass_lower_consume.h"
#include "../ir/passes/ir_pass_lower_min16.h"
#include "../ir/passes/ir_pass_propagate_resource_types.h"
#include "../ir/passes/ir_pass_propagate_types.h"
#include "../ir/passes/ir_pass_remove_unused.h"
#include "../ir/passes/ir_pass_scalarize.h"
#include "../ir/passes/ir_pass_ssa.h"
#include "../ir/passes/ir_pass_sync.h"

namespace dxbc_spv::dxbc {

/** Compilation and lowering options for DXBC. */
struct CompileOptions {
  /* Whether to validate the DXBC container hash. */
  bool validateHash = false;
  /* DXBC conversion options. Includes the shader name,
   * which should always be provided. */
  Converter::Options convertOptions = { };
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
};


/** Compiles a DXBC binary to internal IR without performing any
 *  lowering or legalization. */
std::optional<ir::Builder> compileShaderToIr(const void* data, size_t size, const CompileOptions& options);

/** Invokes all required lowering passes on the IR on the given builder. */
void legalizeIr(ir::Builder& builder, const CompileOptions& options);

/** Compiles a DXBC binary to the internal IR with all required
 *  lowering passes. Convenience method that invokes */
std::optional<ir::Builder> compileShaderToLegalizedIr(const void* data, size_t size, const CompileOptions& options);

}
