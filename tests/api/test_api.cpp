#include "test_api.h"
#include "test_api_io.h"
#include "test_api_resources.h"

namespace dxbc_spv::test_api {

using GetTestPfn = ir::Builder (*)();

void addTest(std::vector<NamedTest>& tests, const char* filter,
    const char* name, GetTestPfn fn) {
  if (filter && !std::strstr(name, filter))
    return;

  auto& test = tests.emplace_back();
  test.name = name;
  test.builder = fn();
}

std::vector<NamedTest> enumerateTests(const char* filter) {
#define ADD_TEST(test) addTest(result, filter, #test, &test)
  std::vector<NamedTest> result;

  ADD_TEST(test_io_vs);
  ADD_TEST(test_io_vs_vertex_id);
  ADD_TEST(test_io_vs_instance_id);
  ADD_TEST(test_io_vs_clip_dist);
  ADD_TEST(test_io_vs_cull_dist);
  ADD_TEST(test_io_vs_clip_cull_dist);
  ADD_TEST(test_io_vs_layer);
  ADD_TEST(test_io_vs_viewport);

  ADD_TEST(test_resources_cbv);
  ADD_TEST(test_resources_cbv_dynamic);
  ADD_TEST(test_resources_cbv_indexed);
  ADD_TEST(test_resources_cbv_indexed_nonuniform);

  ADD_TEST(test_resources_srv_buffer_typed_load);
  ADD_TEST(test_resources_srv_buffer_typed_query);
  ADD_TEST(test_resources_srv_buffer_raw_load);
  ADD_TEST(test_resources_srv_buffer_raw_query);
  ADD_TEST(test_resources_srv_buffer_structured_load);
  ADD_TEST(test_resources_srv_buffer_structured_query);

  ADD_TEST(test_resources_srv_indexed_buffer_typed_load);
  ADD_TEST(test_resources_srv_indexed_buffer_typed_query);
  ADD_TEST(test_resources_srv_indexed_buffer_raw_load);
  ADD_TEST(test_resources_srv_indexed_buffer_raw_query);
  ADD_TEST(test_resources_srv_indexed_buffer_structured_load);
  ADD_TEST(test_resources_srv_indexed_buffer_structured_query);

  ADD_TEST(test_resources_uav_buffer_typed_load);
  ADD_TEST(test_resources_uav_buffer_typed_store);
  ADD_TEST(test_resources_uav_buffer_typed_atomic);
  ADD_TEST(test_resources_uav_buffer_typed_query);
  ADD_TEST(test_resources_uav_buffer_raw_load);
  ADD_TEST(test_resources_uav_buffer_raw_store);
  ADD_TEST(test_resources_uav_buffer_raw_atomic);
  ADD_TEST(test_resources_uav_buffer_raw_query);
  ADD_TEST(test_resources_uav_buffer_structured_load);
  ADD_TEST(test_resources_uav_buffer_structured_store);
  ADD_TEST(test_resources_uav_buffer_structured_atomic);
  ADD_TEST(test_resources_uav_buffer_structured_query);

  ADD_TEST(test_resources_uav_indexed_buffer_typed_load);
  ADD_TEST(test_resources_uav_indexed_buffer_typed_store);
  ADD_TEST(test_resources_uav_indexed_buffer_typed_atomic);
  ADD_TEST(test_resources_uav_indexed_buffer_typed_query);
  ADD_TEST(test_resources_uav_indexed_buffer_raw_load);
  ADD_TEST(test_resources_uav_indexed_buffer_raw_store);
  ADD_TEST(test_resources_uav_indexed_buffer_raw_atomic);
  ADD_TEST(test_resources_uav_indexed_buffer_raw_query);
  ADD_TEST(test_resources_uav_indexed_buffer_structured_load);
  ADD_TEST(test_resources_uav_indexed_buffer_structured_store);
  ADD_TEST(test_resources_uav_indexed_buffer_structured_atomic);
  ADD_TEST(test_resources_uav_indexed_buffer_structured_query);

  ADD_TEST(test_resource_uav_counter);
  ADD_TEST(test_resource_uav_counter_indexed);

  ADD_TEST(test_resource_srv_image_1d_load);
  ADD_TEST(test_resource_srv_image_1d_query);
  ADD_TEST(test_resource_srv_image_1d_sample);
  ADD_TEST(test_resource_srv_image_1d_array_load);
  ADD_TEST(test_resource_srv_image_1d_array_query);
  ADD_TEST(test_resource_srv_image_1d_array_sample);
  ADD_TEST(test_resource_srv_image_2d_load);
  ADD_TEST(test_resource_srv_image_2d_query);
  ADD_TEST(test_resource_srv_image_2d_sample);
  ADD_TEST(test_resource_srv_image_2d_sample_depth);
  ADD_TEST(test_resource_srv_image_2d_gather);
  ADD_TEST(test_resource_srv_image_2d_gather_depth);
  ADD_TEST(test_resource_srv_image_2d_array_load);
  ADD_TEST(test_resource_srv_image_2d_array_query);
  ADD_TEST(test_resource_srv_image_2d_array_sample);
  ADD_TEST(test_resource_srv_image_2d_array_sample_depth);
  ADD_TEST(test_resource_srv_image_2d_array_gather);
  ADD_TEST(test_resource_srv_image_2d_array_gather_depth);
  ADD_TEST(test_resource_srv_image_2d_ms_load);
  ADD_TEST(test_resource_srv_image_2d_ms_query);
  ADD_TEST(test_resource_srv_image_2d_ms_array_load);
  ADD_TEST(test_resource_srv_image_2d_ms_array_query);
  ADD_TEST(test_resource_srv_image_cube_query);
  ADD_TEST(test_resource_srv_image_cube_sample);
  ADD_TEST(test_resource_srv_image_cube_sample_depth);
  ADD_TEST(test_resource_srv_image_cube_gather);
  ADD_TEST(test_resource_srv_image_cube_gather_depth);
  ADD_TEST(test_resource_srv_image_cube_array_query);
  ADD_TEST(test_resource_srv_image_cube_array_sample);
  ADD_TEST(test_resource_srv_image_cube_array_sample_depth);
  ADD_TEST(test_resource_srv_image_cube_array_gather);
  ADD_TEST(test_resource_srv_image_cube_array_gather_depth);
  ADD_TEST(test_resource_srv_image_3d_load);
  ADD_TEST(test_resource_srv_image_3d_query);
  ADD_TEST(test_resource_srv_image_3d_sample);

  ADD_TEST(test_resource_srv_indexed_image_1d_load);
  ADD_TEST(test_resource_srv_indexed_image_1d_query);
  ADD_TEST(test_resource_srv_indexed_image_1d_sample);
  ADD_TEST(test_resource_srv_indexed_image_1d_array_load);
  ADD_TEST(test_resource_srv_indexed_image_1d_array_query);
  ADD_TEST(test_resource_srv_indexed_image_1d_array_sample);
  ADD_TEST(test_resource_srv_indexed_image_2d_load);
  ADD_TEST(test_resource_srv_indexed_image_2d_query);
  ADD_TEST(test_resource_srv_indexed_image_2d_sample);
  ADD_TEST(test_resource_srv_indexed_image_2d_sample_depth);
  ADD_TEST(test_resource_srv_indexed_image_2d_gather);
  ADD_TEST(test_resource_srv_indexed_image_2d_gather_depth);
  ADD_TEST(test_resource_srv_indexed_image_2d_array_load);
  ADD_TEST(test_resource_srv_indexed_image_2d_array_query);
  ADD_TEST(test_resource_srv_indexed_image_2d_array_sample);
  ADD_TEST(test_resource_srv_indexed_image_2d_array_sample_depth);
  ADD_TEST(test_resource_srv_indexed_image_2d_array_gather);
  ADD_TEST(test_resource_srv_indexed_image_2d_array_gather_depth);
  ADD_TEST(test_resource_srv_indexed_image_2d_ms_load);
  ADD_TEST(test_resource_srv_indexed_image_2d_ms_query);
  ADD_TEST(test_resource_srv_indexed_image_2d_ms_array_load);
  ADD_TEST(test_resource_srv_indexed_image_2d_ms_array_query);
  ADD_TEST(test_resource_srv_indexed_image_cube_query);
  ADD_TEST(test_resource_srv_indexed_image_cube_sample);
  ADD_TEST(test_resource_srv_indexed_image_cube_sample_depth);
  ADD_TEST(test_resource_srv_indexed_image_cube_gather);
  ADD_TEST(test_resource_srv_indexed_image_cube_gather_depth);
  ADD_TEST(test_resource_srv_indexed_image_cube_array_query);
  ADD_TEST(test_resource_srv_indexed_image_cube_array_sample);
  ADD_TEST(test_resource_srv_indexed_image_cube_array_sample_depth);
  ADD_TEST(test_resource_srv_indexed_image_cube_array_gather);
  ADD_TEST(test_resource_srv_indexed_image_cube_array_gather_depth);
  ADD_TEST(test_resource_srv_indexed_image_3d_load);
  ADD_TEST(test_resource_srv_indexed_image_3d_query);
  ADD_TEST(test_resource_srv_indexed_image_3d_sample);

  ADD_TEST(test_resource_uav_image_1d_load);
  ADD_TEST(test_resource_uav_image_1d_query);
  ADD_TEST(test_resource_uav_image_1d_store);
  ADD_TEST(test_resource_uav_image_1d_atomic);
  ADD_TEST(test_resource_uav_image_1d_array_load);
  ADD_TEST(test_resource_uav_image_1d_array_query);
  ADD_TEST(test_resource_uav_image_1d_array_store);
  ADD_TEST(test_resource_uav_image_1d_array_atomic);
  ADD_TEST(test_resource_uav_image_2d_load);
  ADD_TEST(test_resource_uav_image_2d_query);
  ADD_TEST(test_resource_uav_image_2d_store);
  ADD_TEST(test_resource_uav_image_2d_atomic);
  ADD_TEST(test_resource_uav_image_2d_array_load);
  ADD_TEST(test_resource_uav_image_2d_array_query);
  ADD_TEST(test_resource_uav_image_2d_array_store);
  ADD_TEST(test_resource_uav_image_2d_array_atomic);
  ADD_TEST(test_resource_uav_image_3d_load);
  ADD_TEST(test_resource_uav_image_3d_query);
  ADD_TEST(test_resource_uav_image_3d_store);
  ADD_TEST(test_resource_uav_image_3d_atomic);

  ADD_TEST(test_resource_uav_indexed_image_1d_load);
  ADD_TEST(test_resource_uav_indexed_image_1d_query);
  ADD_TEST(test_resource_uav_indexed_image_1d_store);
  ADD_TEST(test_resource_uav_indexed_image_1d_atomic);
  ADD_TEST(test_resource_uav_indexed_image_1d_array_load);
  ADD_TEST(test_resource_uav_indexed_image_1d_array_query);
  ADD_TEST(test_resource_uav_indexed_image_1d_array_store);
  ADD_TEST(test_resource_uav_indexed_image_1d_array_atomic);
  ADD_TEST(test_resource_uav_indexed_image_2d_load);
  ADD_TEST(test_resource_uav_indexed_image_2d_query);
  ADD_TEST(test_resource_uav_indexed_image_2d_store);
  ADD_TEST(test_resource_uav_indexed_image_2d_atomic);
  ADD_TEST(test_resource_uav_indexed_image_2d_array_load);
  ADD_TEST(test_resource_uav_indexed_image_2d_array_query);
  ADD_TEST(test_resource_uav_indexed_image_2d_array_store);
  ADD_TEST(test_resource_uav_indexed_image_2d_array_atomic);
  ADD_TEST(test_resource_uav_indexed_image_3d_load);
  ADD_TEST(test_resource_uav_indexed_image_3d_query);
  ADD_TEST(test_resource_uav_indexed_image_3d_store);
  ADD_TEST(test_resource_uav_indexed_image_3d_atomic);

return result;
#undef ADD_TEST
}

}
