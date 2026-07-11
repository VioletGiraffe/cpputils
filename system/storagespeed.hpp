#pragma once

#include <filesystem>

enum class StorageSpeed {
	FastRandomAccess, // Internal SSD or RAM disk: parallel IO requests are fine
	SlowOrUnknown     // Spinning, external, remote, or undetermined: IO requests should be serialized
};

// Classifies the volume holding the given path. The result is cached per volume. Thread-safe.
StorageSpeed storageSpeedForPath(const std::filesystem::path& path);
