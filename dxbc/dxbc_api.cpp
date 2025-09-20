#include "dxbc_api.h"

#include "../util/util_log.h"

namespace dxbc_spv::dxbc {

std::optional<ir::Builder> compileShaderToIr(const void* data, size_t size, const Converter::Options& convertOptions) {
  Container dxbc(util::ByteReader(data, size));

  if (!dxbc) {
    Logger::err("Failed to parse DXBC container.");
    return std::nullopt;
  }

  ir::Builder builder = { };

  Converter converter(dxbc, convertOptions);

  if (!converter.convertShader(builder)) {
    Logger::err("Failed to convert DXBC shader.");
    return std::nullopt;
  }

  return std::make_optional(std::move(builder));
}


std::optional<ir::Builder> compileShaderToLegalizedIr(const void* data, size_t size,
  const Converter::Options& convertOptions, const ir::CompileOptions& compileOptions) {
  auto builder = compileShaderToIr(data, size, convertOptions);

  if (!builder)
    return builder;

  ir::legalizeIr(*builder, compileOptions);
  return builder;
}

}
