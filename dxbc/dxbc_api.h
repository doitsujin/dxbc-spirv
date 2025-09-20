#pragma once

#include <optional>

#include "dxbc_converter.h"

#include "../ir/ir_legalize.h"

namespace dxbc_spv::dxbc {

/** Compiles a DXBC binary to internal IR without performing any
 *  lowering or legalization. */
std::optional<ir::Builder> compileShaderToIr(const void* data, size_t size,
  const Converter::Options& convertOptions);

/** Compiles a DXBC binary to the internal IR with all required
 *  lowering passes. Convenience method that invokes */
std::optional<ir::Builder> compileShaderToLegalizedIr(const void* data, size_t size,
  const Converter::Options& convertOptions, const ir::CompileOptions& compileOptions);

}
