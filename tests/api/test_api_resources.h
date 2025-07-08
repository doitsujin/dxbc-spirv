#pragma once

#include "test_api_common.h"

namespace dxbc_spv::test_api {

Builder test_resources_cbv();
Builder test_resources_cbv_dynamic();
Builder test_resources_cbv_indexed();
Builder test_resources_cbv_indexed_nonuniform();

Builder test_resources_srv_buffer_typed_load();
Builder test_resources_srv_buffer_typed_query();
Builder test_resources_srv_buffer_raw_load();
Builder test_resources_srv_buffer_raw_query();
Builder test_resources_srv_buffer_structured_load();
Builder test_resources_srv_buffer_structured_query();

Builder test_resources_srv_indexed_buffer_typed_load();
Builder test_resources_srv_indexed_buffer_typed_query();
Builder test_resources_srv_indexed_buffer_raw_load();
Builder test_resources_srv_indexed_buffer_raw_query();
Builder test_resources_srv_indexed_buffer_structured_load();
Builder test_resources_srv_indexed_buffer_structured_query();

Builder test_resources_uav_buffer_typed_load();
Builder test_resources_uav_buffer_typed_store();
Builder test_resources_uav_buffer_typed_atomic();
Builder test_resources_uav_buffer_typed_query();
Builder test_resources_uav_buffer_raw_load();
Builder test_resources_uav_buffer_raw_store();
Builder test_resources_uav_buffer_raw_atomic();
Builder test_resources_uav_buffer_raw_query();
Builder test_resources_uav_buffer_structured_load();
Builder test_resources_uav_buffer_structured_store();
Builder test_resources_uav_buffer_structured_atomic();
Builder test_resources_uav_buffer_structured_query();

Builder test_resources_uav_indexed_buffer_typed_load();
Builder test_resources_uav_indexed_buffer_typed_store();
Builder test_resources_uav_indexed_buffer_typed_atomic();
Builder test_resources_uav_indexed_buffer_typed_query();
Builder test_resources_uav_indexed_buffer_raw_load();
Builder test_resources_uav_indexed_buffer_raw_store();
Builder test_resources_uav_indexed_buffer_raw_atomic();
Builder test_resources_uav_indexed_buffer_raw_query();
Builder test_resources_uav_indexed_buffer_structured_load();
Builder test_resources_uav_indexed_buffer_structured_store();
Builder test_resources_uav_indexed_buffer_structured_atomic();
Builder test_resources_uav_indexed_buffer_structured_query();

}
