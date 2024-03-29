#pragma once

#include <stddef.h>
#include <stdint.h>

inline constexpr uint32_t uint16_max = UINT16_MAX;
inline constexpr int32_t int16_max = INT16_MAX;
inline constexpr uint32_t uint32_max = UINT32_MAX;
inline constexpr int32_t int32_max = INT32_MAX;
inline constexpr uint64_t uint64_max = UINT64_MAX;
inline constexpr int64_t int64_min = INT64_MIN;
inline constexpr int64_t int64_max = INT64_MAX;
inline constexpr size_t size_t_max = sizeof(size_t) == 8 ? UINT64_MAX : UINT32_MAX;

inline constexpr float float_max = 3.402823e+38f;

static_assert (sizeof(size_t) == 8 || sizeof(size_t) == 4);
