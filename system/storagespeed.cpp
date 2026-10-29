#include "storagespeed.hpp"

#include <algorithm>
#include <iterator>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>

static bool isWithinFolder(const std::string_view path, const std::string_view folder)
{
	return path.starts_with(folder) && (path.size() == folder.size() || folder.ends_with('/') || path[folder.size()] == '/');
}

// mountinfo writes a space, tab, newline or backslash in a mount point as an octal escape: \040, \011, \012, \134
static std::string unescapedMountPoint(const std::string_view escaped)
{
	const auto isOctalDigit = [](const char c) { return c >= '0' && c <= '7'; };
	std::string result;
	result.reserve(escaped.size());
	for (size_t i = 0; i < escaped.size(); ++i)
	{
		if (escaped[i] == '\\' && i + 3 < escaped.size() && isOctalDigit(escaped[i + 1]) && isOctalDigit(escaped[i + 2]) && isOctalDigit(escaped[i + 3]))
		{
			result.push_back(static_cast<char>((escaped[i + 1] - '0') * 64 + (escaped[i + 2] - '0') * 8 + (escaped[i + 3] - '0')));
			i += 3;
		}
		else
			result.push_back(escaped[i]);
	}
	return result;
}

// An allowlist: an unknown type, remote or not, only loses symlink resolution.
// autofs is excluded: reading a path on it can trigger a network mount.
static bool isLocalFileSystemType(const std::string_view type)
{
	static constexpr std::string_view localTypes[]{
		"ext2", "ext3", "ext4", "xfs", "btrfs", "f2fs", "jfs", "reiserfs", "zfs", "bcachefs", "nilfs2",
		"vfat", "msdos", "exfat", "ntfs", "ntfs3", "fuseblk", "hfs", "hfsplus", "udf", "iso9660", "squashfs", "erofs",
		"overlay", "tmpfs", "ramfs", "devtmpfs", "devpts", "proc", "sysfs"
	};
	return std::find(std::begin(localTypes), std::end(localTypes), type) != std::end(localTypes);
}

static std::string lexicallyNormal(const std::string& path)
{
	return std::filesystem::path{ path }.lexically_normal().generic_string();
}

namespace storage_speed_detail {

std::vector<Mount> parseMountInfo(const std::string_view mountInfo)
{
	std::vector<Mount> mounts;
	std::istringstream lines{ std::string{ mountInfo } };
	for (std::string line; std::getline(lines, line);)
	{
		// Mount ID, parent ID, major:minor, root, mount point, options, optional fields, "-", type, source, super options
		std::istringstream fields{ line };
		std::string mountId, parentId, device, root, mountPoint, field, type;
		if (!(fields >> mountId >> parentId >> device >> root >> mountPoint))
			continue;
		while (fields >> field && field != "-") {}
		if (field != "-" || !(fields >> type))
			continue;

		mounts.push_back({ unescapedMountPoint(mountPoint), device, isLocalFileSystemType(type) });
	}
	return mounts;
}

std::vector<Firmlink> parseFirmlinks(const std::string_view firmlinks, const std::string_view dataVolumeRoot)
{
	std::vector<Firmlink> result;
	std::istringstream lines{ std::string{ firmlinks } };
	for (std::string line; std::getline(lines, line);)
	{
		// "<path>\t<target relative to the data volume>"
		const size_t tab = line.find('\t');
		if (!line.starts_with('/') || tab == std::string::npos || tab + 1 == line.size())
			continue;
		result.push_back({ line.substr(0, tab), std::string{ dataVolumeRoot } + '/' + line.substr(tab + 1) });
	}
	return result;
}

std::string canonicalPath(const std::string_view path, const std::vector<Firmlink>& firmlinks)
{
	const Firmlink* match = nullptr;
	for (const Firmlink& firmlink : firmlinks)
	{
		if (isWithinFolder(path, firmlink.path) && (!match || firmlink.path.size() > match->path.size()))
			match = &firmlink;
	}
	return match ? match->target + std::string{ path.substr(match->path.size()) } : std::string{ path };
}

const Mount* mountForPath(const std::vector<Mount>& mounts, const std::string_view path)
{
	const Mount* match = nullptr;
	for (const Mount& mount : mounts)
	{
		if (isWithinFolder(path, mount.mountPoint) && (!match || mount.mountPoint.size() >= match->mountPoint.size()))
			match = &mount;
	}
	return match;
}

std::string resolveLocalSymlinks(std::string path, const std::vector<Mount>& mounts, const std::vector<Firmlink>& firmlinks,
	const std::function<std::string(const std::string&)>& readLinkTarget)
{
	int hopsLeft = 40; // Linux's own limit on links in one lookup
	// path[0, componentStart) holds no links; a component starts after each '/'
	for (size_t componentStart = 1; componentStart < path.size();)
	{
		const size_t slash = path.find('/', componentStart);
		const size_t componentEnd = slash == std::string::npos ? path.size() : slash;
		const std::string prefix = path.substr(0, componentEnd);
		// The mount of the prefix itself: a remote mount point's own entry is read from the remote side
		const Mount* mount = mountForPath(mounts, prefix);
		if (!mount || !mount->local)
			break;

		const std::string target = readLinkTarget(prefix);
		if (target.empty())
		{
			componentStart = componentEnd + 1;
			continue;
		}
		if (--hopsLeft == 0)
			break;

		const std::string linkedPath = target.starts_with('/') ? target : path.substr(0, componentStart) + target;
		path = canonicalPath(lexicallyNormal(linkedPath + path.substr(componentEnd)), firmlinks);
		componentStart = 1; // The target can hold links of its own
	}
	return path;
}

} // namespace storage_speed_detail

#ifdef _WIN32

#include <Windows.h>
#include <winioctl.h>

#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>

struct VolumeKey
{
	std::wstring guidPath;
	std::wstring mountPath;

	bool operator==(const VolumeKey& other) const
	{
		if (guidPath.empty() != other.guidPath.empty())
			return false;

		return guidPath.empty() ? mountPath == other.mountPath : guidPath == other.guidPath;
	}
};

struct VolumeMount
{
	std::wstring path;
	std::wstring guidPath;
};

static std::wstring normalizedWindowsPath(std::wstring path)
{
	std::replace(path.begin(), path.end(), L'/', L'\\');
	return path;
}

static bool equalIgnoringCase(const std::wstring_view left, const std::wstring_view right)
{
	return left.size() == right.size()
		&& CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

static bool hasPathPrefix(const std::wstring_view path, const std::wstring_view prefix)
{
	return path.size() >= prefix.size()
		&& CompareStringOrdinal(path.data(), static_cast<int>(prefix.size()), prefix.data(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
}

// \\server\share and \\?\UNC\server\share; \\?\ and \\.\ paths to local volumes and devices are not UNC
static bool isUncPath(const std::wstring_view path)
{
	if (hasPathPrefix(path, L"\\\\?\\UNC\\"))
		return true;
	return path.starts_with(L"\\\\") && !path.starts_with(L"\\\\?\\") && !path.starts_with(L"\\\\.\\");
}

// "X:\" for a path on drive X, also in the \\?\X:\ form; empty otherwise
static std::wstring driveRoot(std::wstring_view path)
{
	if (path.starts_with(L"\\\\?\\"))
		path.remove_prefix(4);
	const bool hasDriveLetter = path.size() >= 2 && path[1] == L':'
		&& ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z'));
	return hasDriveLetter ? std::wstring{ path[0], L':', L'\\' } : std::wstring{};
}

static std::vector<VolumeMount> enumerateVolumeMounts()
{
	std::vector<VolumeMount> mounts;
	wchar_t volumeGuidPath[MAX_PATH + 1];
	const HANDLE volumeSearch = FindFirstVolumeW(volumeGuidPath, static_cast<DWORD>(std::size(volumeGuidPath)));
	if (volumeSearch == INVALID_HANDLE_VALUE)
		return mounts;

	do
	{
		std::vector<wchar_t> pathNames(MAX_PATH + 1);
		DWORD requiredLength = 0;
		if (!GetVolumePathNamesForVolumeNameW(volumeGuidPath, pathNames.data(), static_cast<DWORD>(pathNames.size()), &requiredLength))
		{
			if (GetLastError() != ERROR_MORE_DATA)
				continue;

			pathNames.resize(requiredLength);
			if (!GetVolumePathNamesForVolumeNameW(volumeGuidPath, pathNames.data(), static_cast<DWORD>(pathNames.size()), &requiredLength))
				continue;
		}

		for (const wchar_t* pathName = pathNames.data(); *pathName; pathName += std::wcslen(pathName) + 1)
			mounts.push_back({ normalizedWindowsPath(pathName), volumeGuidPath });
	}
	while (FindNextVolumeW(volumeSearch, volumeGuidPath, static_cast<DWORD>(std::size(volumeGuidPath))));

	FindVolumeClose(volumeSearch);
	std::sort(mounts.begin(), mounts.end(), [](const VolumeMount& left, const VolumeMount& right) { return left.path.size() > right.path.size(); });
	return mounts;
}

struct VolumeMountTable
{
	std::mutex mutex;
	std::vector<VolumeMount> mounts = enumerateVolumeMounts();
};

static VolumeMountTable& volumeMountTable()
{
	static VolumeMountTable table;
	return table;
}

static void assignVolumeKey(VolumeKey& key, const std::wstring& mountPath, const std::wstring& guidPath)
{
	key.mountPath = mountPath;
	key.guidPath = guidPath;
}

static bool volumeKeyFromMountTable(const std::wstring& normalizedPath, VolumeKey& key)
{
	VolumeMountTable& table = volumeMountTable();
	std::lock_guard lock{ table.mutex };

	for (int attempt = 0; attempt != 2; ++attempt)
	{
		const auto mount = std::find_if(table.mounts.cbegin(), table.mounts.cend(), [&normalizedPath](const VolumeMount& candidate) {
			return hasPathPrefix(normalizedPath, candidate.path);
		});
		if (mount == table.mounts.cend())
			return false;

		// Drive letters are routinely reused for different removable volumes, so validate the snapshot on every hit.
		wchar_t currentVolumeGuidPath[64]; // "\\?\Volume{GUID}\" is 49 chars
		const BOOL foundVolumeGuid = GetVolumeNameForVolumeMountPointW(
			mount->path.c_str(), currentVolumeGuidPath, static_cast<DWORD>(std::size(currentVolumeGuidPath)));
		if (foundVolumeGuid && equalIgnoringCase(mount->guidPath, currentVolumeGuidPath))
		{
			assignVolumeKey(key, mount->path, mount->guidPath);
			return true;
		}

		if (attempt != 0)
			return false;

		table.mounts = enumerateVolumeMounts();
	}

	return false;
}

static bool volumeKeyFromSystem(const std::filesystem::path& path, VolumeKey& key)
{
	wchar_t volumeRoot[MAX_PATH + 1];
	if (!GetVolumePathNameW(path.c_str(), volumeRoot, static_cast<DWORD>(std::size(volumeRoot))))
		return false;

	wchar_t volumeGuidPath[64]; // "\\?\Volume{GUID}\" is 49 chars
	const BOOL foundVolumeGuid = GetVolumeNameForVolumeMountPointW(volumeRoot, volumeGuidPath, static_cast<DWORD>(std::size(volumeGuidPath)));
	assignVolumeKey(key, volumeRoot, foundVolumeGuid ? volumeGuidPath : L"");
	return true;
}

static bool volumeKeyForPath(const std::filesystem::path& path, VolumeKey& key)
{
	// Only local drives are looked up: the lookup accesses the path, which blocks for the network timeout on an unreachable share.
	std::error_code ec;
	const std::wstring normalizedPath = normalizedWindowsPath(std::filesystem::absolute(path, ec).native());
	if (ec || isUncPath(normalizedPath))
		return false;
	if (const std::wstring root = driveRoot(normalizedPath); !root.empty())
	{
		const UINT driveType = GetDriveTypeW(root.c_str());
		if (driveType != DRIVE_FIXED && driveType != DRIVE_RAMDISK)
			return false;
	}

	if (volumeKeyFromMountTable(normalizedPath, key))
		return true;

	if (!volumeKeyFromSystem(normalizedPath, key))
		return false;

	if (!key.guidPath.empty())
	{
		// The first lookup for a newly attached volume used the system fallback; refresh the table for subsequent paths.
		VolumeMountTable& table = volumeMountTable();
		std::lock_guard lock{ table.mutex };
		table.mounts = enumerateVolumeMounts();
	}
	return true;
}

static StorageSpeed queryVolumeSpeed(const VolumeKey& volume)
{
	switch (GetDriveTypeW(volume.mountPath.c_str()))
	{
	case DRIVE_RAMDISK:
		return StorageSpeed::FastRandomAccess;
	case DRIVE_FIXED:
		break; // Query the physical device below
	default: // Remote, removable, CD-ROM, unknown
		return StorageSpeed::SlowOrUnknown;
	}
	if (volume.guidPath.empty())
		return StorageSpeed::SlowOrUnknown;

	// CreateFile wants the volume path without the trailing backslash
	std::wstring volumeDevicePath = volume.guidPath;
	if (!volumeDevicePath.empty() && volumeDevicePath.back() == L'\\')
		volumeDevicePath.pop_back();

	// Zero access mode: device metadata queries need no read/write access and thus no admin rights
	const HANDLE volumeHandle = CreateFileW(volumeDevicePath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (volumeHandle == INVALID_HANDLE_VALUE)
		return StorageSpeed::SlowOrUnknown;

	StorageSpeed result = StorageSpeed::SlowOrUnknown;
	DWORD bytesReturned = 0;
	STORAGE_PROPERTY_QUERY query{ .PropertyId = StorageDeviceSeekPenaltyProperty, .QueryType = PropertyStandardQuery, .AdditionalParameters = {} };

	DEVICE_SEEK_PENALTY_DESCRIPTOR seekPenalty{};
	if (DeviceIoControl(volumeHandle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &seekPenalty, sizeof(seekPenalty), &bytesReturned, nullptr)
		&& !seekPenalty.IncursSeekPenalty)
	{
		// Claims to be an SSD; but USB bridges often misreport the seek penalty, and external disks want bounded IO regardless of medium
		query.PropertyId = StorageDeviceProperty;
		STORAGE_DEVICE_DESCRIPTOR deviceDescriptor{}; // Variable-size struct, but BusType fits in the fixed part
		if (DeviceIoControl(volumeHandle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &deviceDescriptor, sizeof(deviceDescriptor), &bytesReturned, nullptr)
			&& deviceDescriptor.BusType != BusTypeUsb)
		{
			result = StorageSpeed::FastRandomAccess;
		}
	}

	CloseHandle(volumeHandle);
	return result;
}

#elif defined __linux__

#include <sys/sysmacros.h>

#include <cstdio>
#include <fstream>
#include <stdint.h>

using VolumeKey = uint64_t; // st_dev

static std::vector<storage_speed_detail::Mount> currentMounts(const std::vector<storage_speed_detail::Firmlink>& /*firmlinks*/)
{
	std::ifstream file{ "/proc/self/mountinfo" };
	std::ostringstream contents;
	contents << file.rdbuf();
	return storage_speed_detail::parseMountInfo(contents.str());
}

static bool volumeKeyFromDevice(const std::string& device, VolumeKey& key)
{
	unsigned int deviceMajor = 0, deviceMinor = 0;
	if (std::sscanf(device.c_str(), "%u:%u", &deviceMajor, &deviceMinor) != 2)
		return false;

	key = static_cast<VolumeKey>(makedev(deviceMajor, deviceMinor));
	return true;
}

static StorageSpeed queryVolumeSpeed(const VolumeKey dev)
{
	std::error_code ec;
	const std::filesystem::path sysDevLink = "/sys/dev/block/" + std::to_string(major(dev)) + ':' + std::to_string(minor(dev));
	const std::filesystem::path devicePath = std::filesystem::canonical(sysDevLink, ec);
	if (ec) // No block device behind this st_dev: network FS, tmpfs, etc.
		return StorageSpeed::SlowOrUnknown;

	// External disks want bounded IO regardless of medium (and UAS bridges may misreport 'rotational')
	if (devicePath.native().find("/usb") != std::string::npos)
		return StorageSpeed::SlowOrUnknown;

	// 'queue/rotational' lives on the whole-disk node; for a partition that is the parent directory
	for (const auto& queueDir : { devicePath / "queue", devicePath.parent_path() / "queue" })
	{
		char rotational = 0;
		if (std::ifstream rotationalFile{ queueDir / "rotational" }; rotationalFile >> rotational)
			return rotational == '0' ? StorageSpeed::FastRandomAccess : StorageSpeed::SlowOrUnknown;
	}

	return StorageSpeed::SlowOrUnknown;
}

#elif defined __APPLE__

#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOStorageDeviceCharacteristics.h>
#include <IOKit/storage/IOStorageProtocolCharacteristics.h>
#include <sys/param.h>
#include <sys/mount.h>

#include <string>

using VolumeKey = std::string; // The mount's BSD device name, e.g. "/dev/disk3s1"

static std::vector<storage_speed_detail::Mount> currentMounts(const std::vector<storage_speed_detail::Firmlink>& firmlinks)
{
	// MNT_NOWAIT: the kernel's cached list, without waiting on any file system.
	// getfsstat, not getmntinfo: getmntinfo reuses one buffer across calls, so it is not thread-safe.
	std::vector<storage_speed_detail::Mount> result;
	const int mountCount = ::getfsstat(nullptr, 0, MNT_NOWAIT);
	if (mountCount <= 0)
		return result;
	std::vector<struct statfs> mounts(static_cast<size_t>(mountCount) + 8); // Room for mounts added between the two calls
	const int filledCount = ::getfsstat(mounts.data(), static_cast<int>(mounts.size() * sizeof(struct statfs)), MNT_NOWAIT);

	for (int i = 0; i < filledCount; ++i)
	{
		const struct statfs& mount = mounts[static_cast<size_t>(i)];
		// autofs is excluded: reading a path on it can trigger a network mount
		const bool local = (mount.f_flags & MNT_LOCAL) != 0 && std::string_view{ mount.f_fstypename } != "autofs";
		result.push_back({ storage_speed_detail::canonicalPath(mount.f_mntonname, firmlinks), mount.f_mntfromname, local });
	}
	return result;
}

static bool volumeKeyFromDevice(const std::string& device, VolumeKey& key)
{
	key = device;
	return true;
}

// Reads a string property from one of the characteristics dictionaries ("Device Characteristics" / "Protocol
// Characteristics"), searching upward from the media object: an APFS volume is a synthesized device, the physical
// device's properties live further up the registry chain. Returns an empty string when not found.
static std::string registryCharacteristic(const io_service_t media, CFStringRef characteristicsDictKey, CFStringRef propertyKey)
{
	const CFTypeRef dictRef = IORegistryEntrySearchCFProperty(media, kIOServicePlane, characteristicsDictKey,
		kCFAllocatorDefault, kIORegistryIterateRecursively | kIORegistryIterateParents);
	if (!dictRef)
		return {};

	std::string result;
	if (CFGetTypeID(dictRef) == CFDictionaryGetTypeID())
	{
		const CFTypeRef value = CFDictionaryGetValue(static_cast<CFDictionaryRef>(dictRef), propertyKey);
		if (value && CFGetTypeID(value) == CFStringGetTypeID())
		{
			char buffer[64]; // The characteristics values are short fixed strings ("Solid State", "USB", "PCI-Express", ...)
			if (CFStringGetCString(static_cast<CFStringRef>(value), buffer, sizeof(buffer), kCFStringEncodingUTF8))
				result = buffer;
		}
	}

	CFRelease(dictRef);
	return result;
}

static StorageSpeed queryVolumeSpeed(const VolumeKey& deviceName)
{
	// Mounts not backed by a local block device (network, virtual) have no "/dev/..." origin
	if (!deviceName.starts_with("/dev/"))
		return StorageSpeed::SlowOrUnknown;

#if defined __MAC_OS_X_VERSION_MAX_ALLOWED && __MAC_OS_X_VERSION_MAX_ALLOWED >= 120000
	const mach_port_t iokitPort = kIOMainPortDefault;
#else
	const mach_port_t iokitPort = kIOMasterPortDefault; // pre-macOS-12 SDK name of the same (null) port constant
#endif
	// IOServiceGetMatchingService consumes the matching dictionary - no release needed
	const io_service_t media = IOServiceGetMatchingService(iokitPort, IOBSDNameMatching(iokitPort, 0, deviceName.c_str() + 5 /* skip "/dev/" */));
	if (media == IO_OBJECT_NULL)
		return StorageSpeed::SlowOrUnknown;

	// Medium type first: the common case (internal HDD/SSD) is decided by it alone
	StorageSpeed result = StorageSpeed::SlowOrUnknown;
	if (registryCharacteristic(media, CFSTR(kIOPropertyDeviceCharacteristicsKey), CFSTR(kIOPropertyMediumTypeKey)) == kIOPropertyMediumTypeSolidStateKey)
	{
		// A "Solid State" claim is vetoed for USB (bridges' reporting is unreliable, and external disks want bounded IO
		// regardless of medium - same policy as the other platforms) and for an undetermined interconnect (empty string)
		const std::string interconnect = registryCharacteristic(media, CFSTR(kIOPropertyProtocolCharacteristicsKey), CFSTR(kIOPropertyPhysicalInterconnectTypeKey));
		if (!interconnect.empty() && interconnect != kIOPropertyPhysicalInterconnectTypeUSB)
			result = StorageSpeed::FastRandomAccess;
	}

	IOObjectRelease(media);
	return result;
}

#endif

#if defined __linux__ || defined __APPLE__

#include <climits>
#include <fstream>
#include <unistd.h>

// Lexical: resolving symlinks would access the path
static bool lexicallyAbsolutePath(const std::filesystem::path& path, std::string& absolutePath)
{
	std::error_code ec;
	absolutePath = std::filesystem::absolute(path, ec).lexically_normal().native();
	return !ec;
}

// Empty when path is not a symlink
static std::string symlinkTarget(const std::string& path)
{
	char target[PATH_MAX];
	const ssize_t length = ::readlink(path.c_str(), target, sizeof(target));
	return length > 0 && static_cast<size_t>(length) < sizeof(target) ? std::string(target, static_cast<size_t>(length)) : std::string{};
}

// Empty except on macOS, where /Users and other system folders are firmlinks into the data volume
static const std::vector<storage_speed_detail::Firmlink>& systemFirmlinks()
{
#ifdef __APPLE__
	static const std::vector<storage_speed_detail::Firmlink> firmlinks = [] {
		std::ifstream file{ "/usr/share/firmlinks" };
		std::ostringstream contents;
		contents << file.rdbuf();
		return storage_speed_detail::parseFirmlinks(contents.str(), "/System/Volumes/Data");
	}();
#else
	static const std::vector<storage_speed_detail::Firmlink> firmlinks;
#endif
	return firmlinks;
}

std::string storage_speed_detail::volumeDeviceForPath(const std::filesystem::path& path)
{
	// From the mount tables, not stat() or statfs() on the path: those access it, which blocks on a hung network mount
	std::string absolutePath;
	if (!lexicallyAbsolutePath(path, absolutePath))
		return {};

	const std::vector<Firmlink>& firmlinks = systemFirmlinks();
	const std::vector<Mount> mounts = currentMounts(firmlinks);
	const std::string resolvedPath = resolveLocalSymlinks(canonicalPath(absolutePath, firmlinks), mounts, firmlinks, symlinkTarget);
	const Mount* mount = mountForPath(mounts, resolvedPath);
	return mount ? mount->device : std::string{};
}

static bool volumeKeyForPath(const std::filesystem::path& path, VolumeKey& key)
{
	const std::string device = storage_speed_detail::volumeDeviceForPath(path);
	return !device.empty() && volumeKeyFromDevice(device, key);
}

#endif

#if defined _WIN32 || defined __linux__ || defined __APPLE__

StorageSpeed storageSpeedForPath(const std::filesystem::path& path)
{
	VolumeKey key;
	if (!volumeKeyForPath(path, key))
		return StorageSpeed::SlowOrUnknown;

	static std::mutex cacheMutex;
	static std::vector<std::pair<VolumeKey, StorageSpeed>> cachedVolumeSpeeds; // A handful of volumes at most - linear search

	std::lock_guard lock{ cacheMutex };
	const auto it = std::find_if(cachedVolumeSpeeds.begin(), cachedVolumeSpeeds.end(), [&key](const auto& entry) { return entry.first == key; });
	if (it != cachedVolumeSpeeds.end())
		return it->second;

	const StorageSpeed speed = queryVolumeSpeed(key);
	cachedVolumeSpeeds.emplace_back(std::move(key), speed);
	return speed;
}

#else // Not implemented for this platform

StorageSpeed storageSpeedForPath(const std::filesystem::path& /*path*/)
{
	return StorageSpeed::SlowOrUnknown;
}

#endif
