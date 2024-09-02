#pragma once

#include <stddef.h>

[[nodiscard]] const void* memmem(const void* haystack, size_t haystackSize, const void* needle, size_t needleSize) noexcept;
