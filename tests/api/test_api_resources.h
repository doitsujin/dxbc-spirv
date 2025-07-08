#pragma once

#include "test_api_common.h"

namespace dxbc_spv::test_api {

Builder test_resources_cbv();
Builder test_resources_cbv_dynamic();
Builder test_resources_cbv_indexed();
Builder test_resources_cbv_indexed_nonuniform();

}
