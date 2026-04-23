#pragma once

#include "test_api_common.h"

namespace dxbc_spv::test_api {

Builder test_spirv_spec_constant();
Builder test_spirv_push_data();
Builder test_spirv_raw_pointer();
Builder test_spirv_cbv_srv_uav_structs();
Builder test_spirv_point_size();
Builder test_spirv_input_target();
Builder test_spirv_input_target_ms();

}
