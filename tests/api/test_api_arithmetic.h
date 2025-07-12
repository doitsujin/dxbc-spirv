#pragma once

#include "test_api_common.h"

namespace dxbc_spv::test_api {

Builder test_arithmetic_fp32();
Builder test_arithmetic_fp32_precise();
Builder test_arithmetic_fp32_special();

Builder test_arithmetic_fp64();

Builder test_arithmetic_fp16_scalar();
Builder test_arithmetic_fp16_vector();

}
