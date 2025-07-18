#pragma once

#include "test_api_common.h"

namespace dxbc_spv::test_api {

Builder test_arithmetic_fp32();
Builder test_arithmetic_fp32_precise();
Builder test_arithmetic_fp32_special();
Builder test_arithmetic_fp32_compare();

Builder test_arithmetic_fp64();
Builder test_arithmetic_fp64_compare();
Builder test_arithmetic_fp64_packing();

Builder test_arithmetic_fp16_scalar();
Builder test_arithmetic_fp16_vector();
Builder test_arithmetic_fp16_compare();
Builder test_arithmetic_fp16_packing();
Builder test_arithmetic_fp16_packing_legacy();

Builder test_arithmetic_sint32();
Builder test_arithmetic_uint32();
Builder test_arithmetic_sint16_scalar();
Builder test_arithmetic_sint16_vector();
Builder test_arithmetic_uint16_scalar();
Builder test_arithmetic_uint16_vector();

Builder test_arithmetic_sint32_compare();
Builder test_arithmetic_uint32_compare();
Builder test_arithmetic_sint16_compare();
Builder test_arithmetic_uint16_compare();

Builder test_arithmetic_int_extended();

Builder test_arithmetic_bool();

Builder test_convert_f_to_f();
Builder test_convert_f_to_i();
Builder test_convert_i_to_f();
Builder test_convert_i_to_i();

}
