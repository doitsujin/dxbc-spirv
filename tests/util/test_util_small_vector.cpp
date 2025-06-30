#include "../../util/util_small_vector.h"

#include "../test_common.h"

#include <array>

namespace dxbc_spv::tests::util {

using dxbc_spv::util::small_vector;

void testSmallVectorEmbedded() {
  int32_t ctr = 0u;

  /* Test creating empty vector */
  { small_vector<ScopedCounter, 8> vec;

    ok(vec.empty());
    ok(vec.is_embedded());
    ok(vec.capacity() == 8u);
  }

  /* Test creating vector with initial size */
  { small_vector<ScopedCounter, 8> vec(8u, ScopedCounter(&ctr));

    ok(ctr == 8u);

    ok(!vec.empty());
    ok(vec.size() == 8u);

    ok(vec.is_embedded());
    ok(vec.capacity() == 8u);
  }

  ok(ctr == 0u);

  /* Test calling resize and appending elements */
  { small_vector<ScopedCounter, 8> vec;
    vec.resize(5u, ScopedCounter(&ctr));

    ok(ctr == 5u);

    ok(vec.size() == 5u);

    ok(vec.is_embedded());

    vec.push_back(ScopedCounter(&ctr));
    vec.push_back(ScopedCounter(&ctr));
    vec.push_back(ScopedCounter(&ctr));

    ok(ctr == 8u);

    ok(vec.size() == 8u);
    ok(vec.is_embedded());
  }

  ok(ctr == 0u);

  /* Test shrinking and clearing embedded vector */
  { small_vector<ScopedCounter, 8> vec(8u, ScopedCounter(&ctr));

    vec.resize(3u);

    ok(ctr == 3u);
    ok(vec.size() == 3u);

    ok(vec.is_embedded());

    vec.clear();

    ok(vec.empty());
    ok(vec.is_embedded());
  }

  /* Test removing elements from the back */
  { small_vector<ScopedCounter, 8> vec(8u, ScopedCounter(&ctr));

    while (!vec.empty()) {
      vec.pop_back();
      ok(ctr == int32_t(vec.size()));
    }

    ok(vec.empty());
  }

  ok(ctr == 0u);
}


void testSmallVectorReserve() {
  int32_t ctr = 0u;

  small_vector<ScopedCounter, 8> vec;

  ok(vec.capacity() == 8u);
  ok(vec.is_embedded());

  vec.reserve(1u);

  ok(vec.capacity() == 8u);
  ok(vec.is_embedded());

  vec.push_back(ScopedCounter(&ctr));

  ok(ctr == 1u);

  auto* oldPtr = &vec.front();

  vec.reserve(10u);

  ok(vec.capacity() >= 10u);
  ok(!vec.is_embedded());

  auto* newPtr = &vec.front();

  ok(oldPtr != newPtr);
  ok(ctr == 1u);

  vec.clear();

  ok(ctr == 0u);
}


void testSmallVectorLarge() {
  int32_t ctr = 0u;

  /* Test creating vector with initial size */
  { small_vector<ScopedCounter, 4> vec(8u, ScopedCounter(&ctr));

    ok(ctr == 8u);
    ok(vec.size() == 8u);
    ok(!vec.is_embedded());
    ok(vec.capacity() >= vec.size());
  }

  ok(ctr == 0u);

  /* Test resizing vector */
  { small_vector<ScopedCounter, 4> vec;
    vec.resize(8u, ScopedCounter(&ctr));

    ok(ctr == 8u);
    ok(vec.size() == 8u);
    ok(!vec.is_embedded());
    ok(vec.capacity() >= vec.size());
  }

  ok(ctr == 0u);
}


void testSmallVectorMove() {
  std::array<int32_t, 12> counters = { };

  /* Test move-constructing populated vector */
  small_vector<ScopedCounter, 4> vec0;

  vec0.emplace_back(&counters[0]);
  vec0.emplace_back(&counters[1]);

  ok(vec0.size() == 2u);

  ok(counters[0] == 1u);
  ok(counters[1] == 1u);

  small_vector<ScopedCounter, 4> vec1(std::move(vec0));

  ok(vec0.empty());
  ok(vec1.size() == 2u);
  ok(vec1.is_embedded());

  for (uint32_t i = 0u; i < vec1.size(); i++)
    ok(counters[i] == 1u);

  vec0.emplace_back(&counters[2]);
  ok(counters[2] == 1u);

  /* Test move-assigning back to original vector */
  vec0 = std::move(vec1);

  ok(vec1.empty());
  ok(vec0.size() == 2u);
  ok(vec0.is_embedded());

  ok(counters[0] == 1u);
  ok(counters[1] == 1u);
  ok(counters[2] == 0u);

  /* Test move-construction into vector with larger capacity */
  small_vector<ScopedCounter, 8> vec2(std::move(vec0));

  ok(vec0.empty());
  ok(vec2.size() == 2u);
  ok(vec2.is_embedded());

  ok(counters[0] == 1u);
  ok(counters[1] == 1u);

  /* Test move-assignment back into smaller vector */
  vec0.emplace_back(&counters[2]);
  ok(counters[2] == 1u);

  vec0 = std::move(vec2);

  ok(vec2.empty());

  ok(vec0.size() == 2u);
  ok(vec0.is_embedded());

  ok(counters[0] == 1u);
  ok(counters[1] == 1u);

  /* Add enough items to exceed capacity and move-construct into a
   * vector of a larger capacity */
  vec0.emplace_back(&counters[2]);
  vec0.emplace_back(&counters[3]);
  vec0.emplace_back(&counters[4]);
  vec0.emplace_back(&counters[5]);

  ok(vec0.size() == 6u);
  ok(!vec0.is_embedded());

  small_vector<ScopedCounter, 8> vec3(std::move(vec0));

  ok(vec0.size() == 0u);
  ok(!vec0.is_embedded());

  ok(vec3.size() == 6u);
  ok(vec3.is_embedded());

  for (uint32_t i = 0u; i < counters.size(); i++)
    ok(counters[i] == (i < vec3.size() ? 1 : 0));

  /* Move-construct back into a smaller vector */
  small_vector<ScopedCounter, 4> vec4(std::move(vec3));

  ok(vec4.size() == 6u);
  ok(!vec4.is_embedded());

  for (uint32_t i = 0u; i < counters.size(); i++)
    ok(counters[i] == (i < vec4.size() ? 1 : 0));

  /* Move-assign into larger vector */
  vec3 = std::move(vec4);

  ok(vec4.empty());
  ok(vec3.size() == 6u);
  ok(vec3.is_embedded());

  for (uint32_t i = 0u; i < counters.size(); i++)
    ok(counters[i] == (i < vec3.size() ? 1 : 0));

  /* Add some elements and move-construct to different vector */
  vec3.emplace_back(&counters[6]);
  vec3.emplace_back(&counters[7]);
  vec3.emplace_back(&counters[8]);

  ok(!vec3.is_embedded());

  auto* oldPtr = &vec3.front();

  small_vector<ScopedCounter, 4> vec5(std::move(vec3));

  ok(vec5.size() == 9u);
  ok(!vec5.is_embedded());

  ok(vec3.empty());
  ok(vec3.is_embedded());

  for (uint32_t i = 0u; i < counters.size(); i++)
    ok(counters[i] == (i < vec5.size() ? 1 : 0));

  auto* newPtr = &vec5.front();

  ok(oldPtr == newPtr);

  /* Move-assign back to the old vector */
  vec3 = std::move(vec5);

  ok(vec3.size() == 9u);
  ok(!vec3.is_embedded());

  ok(vec5.empty());
  ok(vec5.is_embedded());

  for (uint32_t i = 0u; i < counters.size(); i++)
    ok(counters[i] == (i < vec3.size() ? 1 : 0));

  newPtr = &vec3.front();

  ok(oldPtr == newPtr);
}


void testSmallVectorCopy() {
  std::array<int32_t, 8> counters = { };

  small_vector<ScopedCounter, 4> vec0;

  for (uint32_t i = 0u; i < 6u; i++)
    vec0.emplace_back(&counters[i]);

  ok(!vec0.is_embedded());

  /* Copy-construct larger vector */
  small_vector<ScopedCounter, 8> vec1(vec0);

  ok(vec1.size() == vec0.size());
  ok(vec1.is_embedded());

  for (uint32_t i = 0u; i < 6u; i++)
    ok(counters[i] == 2);

  /* Copy-assign back to old vector */
  vec0.clear();
  vec0.emplace_back(&counters[6]);
  vec0.emplace_back(&counters[7]);

  ok(!vec0.is_embedded());

  for (uint32_t i = 0u; i < 8u; i++)
    ok(counters[i] == 1);

  vec0 = vec1;

  for (uint32_t i = 0u; i < 8u; i++)
    ok(counters[i] == (i < 6u ? 2 : 0));
}


void testSmallVectorErase() {
  small_vector<int32_t, 4> vec;
  vec.push_back(1);
  vec.push_back(2);
  vec.push_back(-1);
  vec.push_back(3);
  vec.push_back(4);
  vec.push_back(5);

  ok(vec.size() == 6u);

  vec.erase(2);

  ok(vec.size() == 5u);

  for (uint32_t i = 0u; i < vec.size(); i++)
    ok(vec[i] == int32_t(i + 1));
}


void testSmallVectorIterator() {
  small_vector<uint32_t, 4> vec;

  for (auto& _ : vec)
    ok(false);

  vec.push_back(1);
  vec.push_back(2);
  vec.push_back(4);
  vec.push_back(8);

  uint32_t index = 0;

  for (auto& i : vec)
    ok(i == 1u << (index++));

  ok(index == vec.size());

  vec.push_back(16);
  vec.push_back(32);

  index = 0;

  for (auto& i : vec)
    ok(i == 1u << (index++));

  ok(index == vec.size());
}


void testSmallVectorShrinkToFit() {
  std::array<int32_t, 8> counters = { };

  small_vector<ScopedCounter, 4> vec;
  vec.reserve(16u);

  for (uint32_t i = 0u; i < 6u; i++)
    vec.emplace_back(&counters[i]);

  ok(vec.size() == 6u);
  ok(vec.capacity() == 16u);

  vec.shrink_to_fit();

  ok(vec.capacity() == vec.size());

  for (uint32_t i = 0u; i < 6u; i++)
    ok(counters[i] == 1);

  vec.resize(2u);

  for (uint32_t i = 0u; i < 6u; i++)
    ok(counters[i] == (i < 2u) ? 1 : 0);

  vec.shrink_to_fit();

  ok(vec.size() == 2);
  ok(vec.capacity() == 4);
  ok(vec.is_embedded());

  vec.clear();

  for (uint32_t i = 0u; i < counters.size(); i++)
    ok(counters[i] == 0);

  vec.shrink_to_fit();
  ok(vec.capacity() == 4);
}


void testSmallVector() {
  RUN_TEST(testSmallVectorEmbedded);
  RUN_TEST(testSmallVectorReserve);
  RUN_TEST(testSmallVectorLarge);
  RUN_TEST(testSmallVectorMove);
  RUN_TEST(testSmallVectorCopy);
  RUN_TEST(testSmallVectorErase);
  RUN_TEST(testSmallVectorIterator);
  RUN_TEST(testSmallVectorShrinkToFit);
}

}
