#include "test_api.h"
#include "test_api_io.h"

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

  return result;
#undef ADD_TEST
}

}
