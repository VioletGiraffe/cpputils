#pragma once

#include <string>

// The CPU model name as the platform reports it, e.g. "13th Gen Intel(R) Core(TM) i9-13900K"
// Empty on failure
[[nodiscard]] std::string cpuModelName();
