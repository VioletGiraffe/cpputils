#pragma once

#include <filesystem>

// The running executable's path, not necessarily absolute or free of symlinks
// Empty on failure
std::filesystem::path processFilePath();
