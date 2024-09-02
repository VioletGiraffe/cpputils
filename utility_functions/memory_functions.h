#pragma once

#include <stddef.h>

[[nodiscard]] const void* memfind(const void* haystack, size_t haystackSize, const void* needle, size_t needleSize) noexcept;
