#pragma once

#include "test_api_common.h"

namespace dxbc_spv::test_api {

Builder test_misc_scratch();
Builder test_misc_lds();
Builder test_misc_lds_atomic();
Builder test_misc_constant_load();
Builder test_misc_ps_demote();
Builder test_misc_ps_early_z();
Builder test_misc_function();
Builder test_misc_function_with_args();
Builder test_misc_function_with_return();
Builder test_misc_function_with_undef();

Builder test_cfg_if();
Builder test_cfg_if_else();
Builder test_cfg_loop_once();
Builder test_cfg_loop_infinite();
Builder test_cfg_switch_simple();
Builder test_cfg_switch_complex();

Builder test_pass_function_shared_temps();

}
