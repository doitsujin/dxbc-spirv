#pragma once

#include "test_api_common.h"

namespace dxbc_spv::test_api {

Builder test_pass_lower_io_xfb_simple();
Builder test_pass_lower_io_xfb_partial();
Builder test_pass_lower_io_xfb_multi_stream();
Builder test_pass_lower_io_xfb_multi_stream_with_raster();
Builder test_pass_lower_io_xfb_multi_stream_raster_only();

Builder test_pass_lower_io_patch_constant_locations_hs();
Builder test_pass_lower_io_patch_constant_locations_ds();

Builder test_pass_lower_io_rewrite_gs_primitive_type();

Builder test_pass_lower_io_mismatch_vs_basic();
Builder test_pass_lower_io_mismatch_vs_missing_input();

Builder test_pass_lower_io_mismatch_hs_basic();
Builder test_pass_lower_io_mismatch_hs_missing_input();

Builder test_pass_lower_io_mismatch_ds_basic();
Builder test_pass_lower_io_mismatch_ds_missing_input();
Builder test_pass_lower_io_mismatch_ds_straddle_input();

Builder test_pass_lower_io_mismatch_gs_basic();
Builder test_pass_lower_io_mismatch_gs_missing_input();
Builder test_pass_lower_io_mismatch_gs_partial_input();
Builder test_pass_lower_io_mismatch_gs_straddle();

Builder test_pass_lower_io_mismatch_ps_basic();
Builder test_pass_lower_io_mismatch_ps_missing_input();
Builder test_pass_lower_io_mismatch_ps_partial_input();
Builder test_pass_lower_io_mismatch_ps_missing_builtin();
Builder test_pass_lower_io_mismatch_ps_straddle();
Builder test_pass_lower_io_mismatch_ps_clip_distance_small();
Builder test_pass_lower_io_mismatch_ps_clip_distance_large();

Builder test_pass_lower_io_enable_flat_shading();
Builder test_pass_lower_io_enable_sample_shading();
Builder test_pass_lower_io_swizzle_rt();

}
