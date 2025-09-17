#pragma once

#include "test_api_common.h"

namespace dxbc_spv::test_api {

Builder test_pass_arithmetic_constant_fold_int32();
Builder test_pass_arithmetic_constant_fold_int16();
Builder test_pass_arithmetic_constant_fold_int16_vec2();
Builder test_pass_arithmetic_constant_fold_bool();
Builder test_pass_arithmetic_constant_fold_compare();
Builder test_pass_arithmetic_constant_fold_select();

Builder test_pass_arithmetic_identities_bool();
Builder test_pass_arithmetic_identities_compare();
Builder test_pass_arithmetic_identities_select();
Builder test_pass_arithmetic_propagate_sign();
Builder test_pass_arithmetic_fuse_mad();

Builder test_pass_arithmetic_lower_legacy();

}
