#include "test_api.h"
#include "test_api_arithmetic.h"
#include "test_api_io.h"
#include "test_api_misc.h"
#include "test_api_pass_arithmetic.h"
#include "test_api_pass_buffer_kind.h"
#include "test_api_pass_lower_io.h"
#include "test_api_pass_scalarize.h"
#include "test_api_resources.h"
#include "test_api_spirv.h"

#define ADD_TEST(test) addTest(result, category, filter, #test, &test)

#define ADD_CATEGORY(cat, fn) addTestCategory(result, cat, category, \
  [] (std::vector<NamedTest>& r, const char* c, const char* f) { fn(r, c, f); }, filter)

namespace dxbc_spv::test_api {

using GetTestPfn = ir::Builder (*)();

std::vector<std::string> enumerateTestCategories() {
  std::vector<std::string> result = {
    std::string("lowering"),
    std::string("spirv"),
  };

  return result;
}


void addTest(std::vector<NamedTest>& tests, const char* category, const char* filter,
    const char* name, GetTestPfn fn) {
  if (filter && !std::strstr(name, filter))
    return;

  auto& test = tests.emplace_back();
  test.name = name;
  test.category = category;
  test.builder = fn();
}


template<typename Fn>
void addTestCategory(std::vector<NamedTest>& result, const char* name, const char* categoryFilter, const Fn& fn, const char* filter) {
  if (categoryFilter && std::string(categoryFilter) != name)
    return;

  fn(result, name, filter);
}


void enumerateLoweringTests(std::vector<NamedTest>& result, const char* category, const char* filter) {
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
  ADD_TEST(test_io_ps_builtins);
  ADD_TEST(test_io_ps_fully_covered);

  ADD_TEST(test_io_gs_basic_point);
  ADD_TEST(test_io_gs_basic_line);
  ADD_TEST(test_io_gs_basic_line_adj);
  ADD_TEST(test_io_gs_basic_triangle);
  ADD_TEST(test_io_gs_basic_triangle_adj);
  ADD_TEST(test_io_gs_instanced);
  ADD_TEST(test_io_gs_xfb);
  ADD_TEST(test_io_gs_multi_stream_xfb_raster_0);
  ADD_TEST(test_io_gs_multi_stream_xfb_raster_1);

  ADD_TEST(test_io_hs_point);
  ADD_TEST(test_io_hs_line);
  ADD_TEST(test_io_hs_triangle_cw);
  ADD_TEST(test_io_hs_triangle_ccw);

  ADD_TEST(test_io_ds_isoline);
  ADD_TEST(test_io_ds_triangle);
  ADD_TEST(test_io_ds_quad);

  ADD_TEST(test_io_cs_builtins);

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

  ADD_TEST(test_convert_f_to_f);
  ADD_TEST(test_convert_f_to_i);
  ADD_TEST(test_convert_i_to_f);
  ADD_TEST(test_convert_i_to_i);

  ADD_TEST(test_misc_scratch);
  ADD_TEST(test_misc_lds);
  ADD_TEST(test_misc_lds_atomic);
  ADD_TEST(test_misc_constant_load);
  ADD_TEST(test_misc_ps_demote);
  ADD_TEST(test_misc_ps_early_z);
  ADD_TEST(test_misc_function);
  ADD_TEST(test_misc_function_with_args);
  ADD_TEST(test_misc_function_with_return);
  ADD_TEST(test_misc_function_with_undef);

  ADD_TEST(test_cfg_if);
  ADD_TEST(test_cfg_if_else);
  ADD_TEST(test_cfg_loop_once);
  ADD_TEST(test_cfg_loop_infinite);
  ADD_TEST(test_cfg_switch_simple);
  ADD_TEST(test_cfg_switch_complex);
}


void enumerateSpirvTests(std::vector<NamedTest>& result, const char* category, const char* filter) {
  ADD_TEST(test_spirv_spec_constant);
  ADD_TEST(test_spirv_push_data);
  ADD_TEST(test_spirv_raw_pointer);
  ADD_TEST(test_spirv_cbv_srv_uav_structs);
  ADD_TEST(test_spirv_point_size);
}


void enumeratePassesTests(std::vector<NamedTest>& result, const char* category, const char* filter) {
  ADD_TEST(test_pass_scalarize_32_vec1);
  ADD_TEST(test_pass_scalarize_32_vec2);
  ADD_TEST(test_pass_scalarize_32_vec3);
  ADD_TEST(test_pass_scalarize_32_vec4);

  ADD_TEST(test_pass_scalarize_64_vec1);
  ADD_TEST(test_pass_scalarize_64_vec2);

  ADD_TEST(test_pass_scalarize_16_vec1);
  ADD_TEST(test_pass_scalarize_16_vec2);
  ADD_TEST(test_pass_scalarize_16_vec3);
  ADD_TEST(test_pass_scalarize_16_vec4);

  ADD_TEST(test_pass_scalarize_16_vec1_as_vec2);
  ADD_TEST(test_pass_scalarize_16_vec2_as_vec2);
  ADD_TEST(test_pass_scalarize_16_vec3_as_vec2);
  ADD_TEST(test_pass_scalarize_16_vec4_as_vec2);

  ADD_TEST(test_pass_scalarize_8_vec1);
  ADD_TEST(test_pass_scalarize_8_vec2);
  ADD_TEST(test_pass_scalarize_8_vec3);
  ADD_TEST(test_pass_scalarize_8_vec4);

  ADD_TEST(test_pass_scalarize_8_vec1_as_vec4);
  ADD_TEST(test_pass_scalarize_8_vec2_as_vec4);
  ADD_TEST(test_pass_scalarize_8_vec3_as_vec4);
  ADD_TEST(test_pass_scalarize_8_vec4_as_vec4);

  ADD_TEST(test_pass_arithmetic_constant_fold_int32);
  ADD_TEST(test_pass_arithmetic_constant_fold_int16);
  ADD_TEST(test_pass_arithmetic_constant_fold_int16_vec2);
  ADD_TEST(test_pass_arithmetic_constant_fold_bool);
  ADD_TEST(test_pass_arithmetic_constant_fold_compare);
  ADD_TEST(test_pass_arithmetic_constant_fold_select);

  ADD_TEST(test_pass_arithmetic_identities_bool);
  ADD_TEST(test_pass_arithmetic_identities_compare);
  ADD_TEST(test_pass_arithmetic_identities_select);
  ADD_TEST(test_pass_arithmetic_propagate_sign);
  ADD_TEST(test_pass_arithmetic_fuse_mad);

  ADD_TEST(test_pass_arithmetic_lower_legacy);

  ADD_TEST(test_pass_buffer_kind_typed_uav_to_raw);
  ADD_TEST(test_pass_buffer_kind_raw_srv_to_typed);
  ADD_TEST(test_pass_buffer_kind_raw_srv_to_typed_sparse);
  ADD_TEST(test_pass_buffer_kind_raw_uav_to_typed);
  ADD_TEST(test_pass_buffer_kind_structured_srv_to_typed);
  ADD_TEST(test_pass_buffer_kind_structured_srv_to_typed_sparse);
  ADD_TEST(test_pass_buffer_kind_structured_uav_to_typed);

  ADD_TEST(test_pass_lower_io_xfb_simple);
  ADD_TEST(test_pass_lower_io_xfb_partial);
  ADD_TEST(test_pass_lower_io_xfb_multi_stream);
  ADD_TEST(test_pass_lower_io_xfb_multi_stream_with_raster);
  ADD_TEST(test_pass_lower_io_xfb_multi_stream_raster_only);

  ADD_TEST(test_pass_lower_io_patch_constant_locations_hs);
  ADD_TEST(test_pass_lower_io_patch_constant_locations_ds);

  ADD_TEST(test_pass_lower_io_rewrite_gs_primitive_type);

  ADD_TEST(test_pass_lower_io_mismatch_vs_basic);
  ADD_TEST(test_pass_lower_io_mismatch_vs_missing_input);

  ADD_TEST(test_pass_lower_io_mismatch_hs_basic);
  ADD_TEST(test_pass_lower_io_mismatch_hs_missing_input);

  ADD_TEST(test_pass_lower_io_mismatch_ds_basic);
  ADD_TEST(test_pass_lower_io_mismatch_ds_missing_input);
  ADD_TEST(test_pass_lower_io_mismatch_ds_straddle_input);

  ADD_TEST(test_pass_lower_io_mismatch_gs_basic);
  ADD_TEST(test_pass_lower_io_mismatch_gs_missing_input);
  ADD_TEST(test_pass_lower_io_mismatch_gs_partial_input);
  ADD_TEST(test_pass_lower_io_mismatch_gs_straddle);

  ADD_TEST(test_pass_lower_io_mismatch_ps_basic);
  ADD_TEST(test_pass_lower_io_mismatch_ps_missing_input);
  ADD_TEST(test_pass_lower_io_mismatch_ps_partial_input);
  ADD_TEST(test_pass_lower_io_mismatch_ps_missing_builtin);
  ADD_TEST(test_pass_lower_io_mismatch_ps_straddle);
  ADD_TEST(test_pass_lower_io_mismatch_ps_clip_distance_small);
  ADD_TEST(test_pass_lower_io_mismatch_ps_clip_distance_large);

  ADD_TEST(test_pass_lower_io_enable_flat_shading);
  ADD_TEST(test_pass_lower_io_enable_sample_shading);
  ADD_TEST(test_pass_lower_io_swizzle_rt);

  ADD_TEST(test_pass_function_shared_temps);
}


std::vector<NamedTest> enumerateTestsInCategory(const char* category, const char* filter) {
  std::vector<NamedTest> result;
  ADD_CATEGORY("lowering", enumerateLoweringTests);
  ADD_CATEGORY("spirv", enumerateSpirvTests);
  ADD_CATEGORY("passes", enumeratePassesTests);
  return result;
}


std::vector<NamedTest> enumerateTests(const char* filter) {
  return enumerateTestsInCategory("lowering", filter);
}


}
