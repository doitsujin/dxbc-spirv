#pragma once

#include "test_api_common.h"

namespace dxbc_spv::test_api {

Builder test_pass_scalarize_32_vec1();
Builder test_pass_scalarize_32_vec2();
Builder test_pass_scalarize_32_vec3();
Builder test_pass_scalarize_32_vec4();

Builder test_pass_scalarize_64_vec1();
Builder test_pass_scalarize_64_vec2();

Builder test_pass_scalarize_16_vec1();
Builder test_pass_scalarize_16_vec2();
Builder test_pass_scalarize_16_vec3();
Builder test_pass_scalarize_16_vec4();

Builder test_pass_scalarize_16_vec1_as_vec2();
Builder test_pass_scalarize_16_vec2_as_vec2();
Builder test_pass_scalarize_16_vec3_as_vec2();
Builder test_pass_scalarize_16_vec4_as_vec2();

Builder test_pass_scalarize_8_vec1();
Builder test_pass_scalarize_8_vec2();
Builder test_pass_scalarize_8_vec3();
Builder test_pass_scalarize_8_vec4();

Builder test_pass_scalarize_8_vec1_as_vec4();
Builder test_pass_scalarize_8_vec2_as_vec4();
Builder test_pass_scalarize_8_vec3_as_vec4();
Builder test_pass_scalarize_8_vec4_as_vec4();

}
