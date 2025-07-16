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

}
