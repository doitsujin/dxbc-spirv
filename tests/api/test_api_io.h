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

}
