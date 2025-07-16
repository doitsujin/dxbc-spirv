#include "test_api.h"
#include "test_api_arithmetic.h"
#include "test_api_io.h"
#include "test_api_misc.h"
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

  ADD_TEST(test_io_ps_interpolate_centroid);
  ADD_TEST(test_io_ps_interpolate_sample);
  ADD_TEST(test_io_ps_interpolate_offset);
  ADD_TEST(test_io_ps_export_depth);
  ADD_TEST(test_io_ps_export_depth_less);
  ADD_TEST(test_io_ps_export_depth_greater);
  ADD_TEST(test_io_ps_export_stencil);

  ADD_TEST(test_io_gs_basic_point);
  ADD_TEST(test_io_gs_basic_line);
  ADD_TEST(test_io_gs_basic_line_adj);
  ADD_TEST(test_io_gs_basic_triangle);
  ADD_TEST(test_io_gs_basic_triangle_adj);
  ADD_TEST(test_io_gs_instanced);
  ADD_TEST(test_io_gs_xfb);
  ADD_TEST(test_io_gs_multi_stream_xfb_raster_0);
  ADD_TEST(test_io_gs_multi_stream_xfb_raster_1);

  ADD_TEST(test_io_hs);

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

  ADD_TEST(test_resource_srv_buffer_load_sparse_feedback);
  ADD_TEST(test_resource_srv_image_load_sparse_feedback);
  ADD_TEST(test_resource_srv_image_sample_sparse_feedback);
  ADD_TEST(test_resource_srv_image_sample_depth_sparse_feedback);
  ADD_TEST(test_resource_srv_image_gather_sparse_feedback);
  ADD_TEST(test_resource_srv_image_gather_depth_sparse_feedback);

  ADD_TEST(test_resource_uav_buffer_load_sparse_feedback);
  ADD_TEST(test_resource_uav_image_load_sparse_feedback);

  ADD_TEST(test_resource_rov);

  ADD_TEST(test_arithmetic_fp32);
  ADD_TEST(test_arithmetic_fp32_precise);
  ADD_TEST(test_arithmetic_fp32_special);
  ADD_TEST(test_arithmetic_fp32_compare);

  ADD_TEST(test_arithmetic_fp64);
  ADD_TEST(test_arithmetic_fp64_compare);
  ADD_TEST(test_arithmetic_fp64_packing);

  ADD_TEST(test_arithmetic_fp16_scalar);
  ADD_TEST(test_arithmetic_fp16_vector);
  ADD_TEST(test_arithmetic_fp16_compare);
  ADD_TEST(test_arithmetic_fp16_packing);
  ADD_TEST(test_arithmetic_fp16_packing_legacy);

  ADD_TEST(test_arithmetic_sint32);
  ADD_TEST(test_arithmetic_uint32);
  ADD_TEST(test_arithmetic_sint16_scalar);
  ADD_TEST(test_arithmetic_sint16_vector);
  ADD_TEST(test_arithmetic_uint16_scalar);
  ADD_TEST(test_arithmetic_uint16_vector);

  ADD_TEST(test_arithmetic_sint32_compare);
  ADD_TEST(test_arithmetic_uint32_compare);
  ADD_TEST(test_arithmetic_sint16_compare);
  ADD_TEST(test_arithmetic_uint16_compare);

  ADD_TEST(test_arithmetic_int_extended);

  ADD_TEST(test_arithmetic_bool);

  ADD_TEST(test_misc_scratch);
  ADD_TEST(test_misc_lds);
  ADD_TEST(test_misc_lds_atomic);
  ADD_TEST(test_misc_constant_load);
  ADD_TEST(test_misc_ps_demote);
  ADD_TEST(test_misc_ps_early_z);
  ADD_TEST(test_misc_function);
  ADD_TEST(test_misc_function_with_args);
  ADD_TEST(test_misc_function_with_return);

return result;
#undef ADD_TEST
}

}
