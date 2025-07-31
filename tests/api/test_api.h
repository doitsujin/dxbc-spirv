#pragma once

#include <cstring>
#include <string>
#include <vector>

#include "../../ir/ir_builder.h"

namespace dxbc_spv::test_api {

/** Test entry */
struct NamedTest {
  std::string category;
  std::string name;
  ir::Builder builder;
};

/** Retrieves available lowering tests. The filter is optional and
 *  may be used to retrieve only a subset of the named tests. */
std::vector<NamedTest> enumerateTests(const char* filter);

/** Enumerates test categories. */
std::vector<std::string> enumerateTestCategories();

/** Retrieves lowering tests specific to the SPIR-V backend. */
std::vector<NamedTest> enumerateTestsInCategory(const char* category, const char* filter);

}
