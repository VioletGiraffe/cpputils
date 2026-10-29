#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

enum class StorageSpeed {
	FastRandomAccess, // Internal SSD or RAM disk: parallel IO requests are fine
	SlowOrUnknown     // Spinning, external, remote, or undetermined: IO requests should be serialized
};

// Classifies the volume holding the given path. The result is cached per volume. Thread-safe.
// Finds the volume from the OS mount tables without accessing remote storage, so an unreachable share cannot block the call.
// Symlinks are followed only on local storage: one on remote storage is classified by the volume holding it.
StorageSpeed storageSpeedForPath(const std::filesystem::path& path);

// The platform-independent parts of the Linux and macOS volume lookup, exposed for the tests
namespace storage_speed_detail {

struct Mount
{
	std::string mountPoint; // Rewritten through the firmlinks on macOS
	std::string device;     // Linux: "major:minor"; macOS: the BSD device name
	bool local = false;     // Reading a link on it cannot wait on remote storage
};

struct Firmlink
{
	std::string path;   // e.g. "/Users"
	std::string target; // Absolute, e.g. "/System/Volumes/Data/Users"
};

// From the contents of /proc/self/mountinfo
[[nodiscard]] std::vector<Mount> parseMountInfo(std::string_view mountInfo);
// From the contents of /usr/share/firmlinks; dataVolumeRoot is where the data volume is mounted
[[nodiscard]] std::vector<Firmlink> parseFirmlinks(std::string_view firmlinks, std::string_view dataVolumeRoot);
// path with its longest matching firmlink replaced by the firmlink's target
[[nodiscard]] std::string canonicalPath(std::string_view path, const std::vector<Firmlink>& firmlinks);
// The mount holding path; null when none does. A later mount on the same point hides the earlier ones.
[[nodiscard]] const Mount* mountForPath(const std::vector<Mount>& mounts, std::string_view path);
// Resolves the symlinks in an absolute, lexically normal and canonical path while it lies on local mounts; stops at the first remote one.
// readLinkTarget returns a link's target, empty when its argument is not a link.
[[nodiscard]] std::string resolveLocalSymlinks(std::string path, const std::vector<Mount>& mounts, const std::vector<Firmlink>& firmlinks,
	const std::function<std::string(const std::string&)>& readLinkTarget);

#if defined __linux__ || defined __APPLE__
// The device of the mount holding path, in Mount::device form; empty when none is found
[[nodiscard]] std::string volumeDeviceForPath(const std::filesystem::path& path);
#endif

} // namespace storage_speed_detail
