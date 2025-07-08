#pragma once

#ifndef NDEBUG
#include <cassert>

#define dxbc_spv_assert(cond) assert(cond)
#define dxbc_spv_unreachable(cond) assert(false)
#else
#define dxbc_spv_assert(cond) do { } while (0)
#define dxbc_spv_unreachable(cond) do { } while (0)
#endif
