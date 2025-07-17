#pragma once

#include "test_api_common.h"

namespace dxbc_spv::test_api {

Builder test_io_vs();
Builder test_io_vs_vertex_id();
Builder test_io_vs_instance_id();
Builder test_io_vs_clip_dist();
Builder test_io_vs_cull_dist();
Builder test_io_vs_clip_cull_dist();
Builder test_io_vs_layer();
Builder test_io_vs_viewport();

Builder test_io_ps_interpolate_centroid();
Builder test_io_ps_interpolate_sample();
Builder test_io_ps_interpolate_offset();
Builder test_io_ps_export_depth();
Builder test_io_ps_export_depth_less();
Builder test_io_ps_export_depth_greater();
Builder test_io_ps_export_stencil();
Builder test_io_ps_builtins();
Builder test_io_ps_fully_covered();

Builder test_io_gs_basic_point();
Builder test_io_gs_basic_line();
Builder test_io_gs_basic_line_adj();
Builder test_io_gs_basic_triangle();
Builder test_io_gs_basic_triangle_adj();
Builder test_io_gs_instanced();
Builder test_io_gs_xfb();
Builder test_io_gs_multi_stream_xfb_raster_0();
Builder test_io_gs_multi_stream_xfb_raster_1();

Builder test_io_hs_point();
Builder test_io_hs_line();
Builder test_io_hs_triangle_cw();
Builder test_io_hs_triangle_ccw();

Builder test_io_ds_isoline();
Builder test_io_ds_triangle();
Builder test_io_ds_quad();

Builder test_io_cs_builtins();

}
