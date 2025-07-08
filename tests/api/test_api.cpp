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

return result;
#undef ADD_TEST
}

}
