#pragma once

#include "test_api_common.h"

namespace dxbc_spv::test_api {

Builder test_pass_buffer_kind_typed_uav_to_raw();

Builder test_pass_buffer_kind_raw_srv_to_typed();
Builder test_pass_buffer_kind_raw_srv_to_typed_sparse();
Builder test_pass_buffer_kind_raw_uav_to_typed();

Builder test_pass_buffer_kind_structured_srv_to_typed();
Builder test_pass_buffer_kind_structured_srv_to_typed_sparse();
Builder test_pass_buffer_kind_structured_uav_to_typed();

}
