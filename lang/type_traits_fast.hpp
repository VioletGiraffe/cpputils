#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr auto uint32_max = UINT32_MAX;
constexpr auto int32_max = INT32_MAX;
constexpr auto uint64_max = UINT64_MAX;
constexpr auto int64_min = INT64_MIN;
constexpr auto int64_max = INT64_MAX;
constexpr auto size_t_max = sizeof(size_t) == 8 ? UINT64_MAX : UINT32_MAX;

#ifdef _DEBUG
static_assert (sizeof(size_t) == 8 || sizeof(size_t) == 4);
#endif
