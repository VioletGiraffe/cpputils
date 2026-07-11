#include "storagespeed.hpp"

#ifdef _WIN32

#include <Windows.h>
#include <winioctl.h>

#include <iterator>
#include <string>

using VolumeKey = std::wstring;

static bool volumeKeyForPath(const std::filesystem::path& path, VolumeKey& key)
{
	wchar_t volumeRoot[MAX_PATH + 1];
	if (!GetVolumePathNameW(path.c_str(), volumeRoot, static_cast<DWORD>(std::size(volumeRoot))))
		return false;

	key = volumeRoot;
	return true;
}

static StorageSpeed queryVolumeSpeed(const VolumeKey& volumeRoot)
{
	switch (GetDriveTypeW(volumeRoot.c_str()))
	{
	case DRIVE_RAMDISK:
		return StorageSpeed::FastRandomAccess;
	case DRIVE_FIXED:
		break; // Query the physical device below
	default: // Remote, removable, CD-ROM, unknown
		return StorageSpeed::SlowOrUnknown;
	}

	// The volume GUID path handles volumes mounted in a folder as well as under a drive letter
	wchar_t volumeGuidPath[64]; // "\\?\Volume{GUID}\" is 49 chars
	if (!GetVolumeNameForVolumeMountPointW(volumeRoot.c_str(), volumeGuidPath, static_cast<DWORD>(std::size(volumeGuidPath))))
		return StorageSpeed::SlowOrUnknown;

	// CreateFile wants the volume path without the trailing backslash
	std::wstring volumeDevicePath = volumeGuidPath;
	if (!volumeDevicePath.empty() && volumeDevicePath.back() == L'\\')
		volumeDevicePath.pop_back();

	// Zero access mode: device metadata queries need no read/write access and thus no admin rights
	const HANDLE volume = CreateFileW(volumeDevicePath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (volume == INVALID_HANDLE_VALUE)
		return StorageSpeed::SlowOrUnknown;

	StorageSpeed result = StorageSpeed::SlowOrUnknown;
	DWORD bytesReturned = 0;
	STORAGE_PROPERTY_QUERY query{ .PropertyId = StorageDeviceSeekPenaltyProperty, .QueryType = PropertyStandardQuery, .AdditionalParameters = {} };

	DEVICE_SEEK_PENALTY_DESCRIPTOR seekPenalty{};
	if (DeviceIoControl(volume, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &seekPenalty, sizeof(seekPenalty), &bytesReturned, nullptr)
		&& !seekPenalty.IncursSeekPenalty)
	{
		// Claims to be an SSD; but USB bridges often misreport the seek penalty, and external disks want bounded IO regardless of medium
		query.PropertyId = StorageDeviceProperty;
		STORAGE_DEVICE_DESCRIPTOR deviceDescriptor{}; // Variable-size struct, but BusType fits in the fixed part
		if (DeviceIoControl(volume, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &deviceDescriptor, sizeof(deviceDescriptor), &bytesReturned, nullptr)
			&& deviceDescriptor.BusType != BusTypeUsb)
		{
			result = StorageSpeed::FastRandomAccess;
		}
	}

	CloseHandle(volume);
	return result;
}

#elif defined __linux__

#include <sys/stat.h>
#include <sys/sysmacros.h>

#include <fstream>
#include <stdint.h>
#include <string>

using VolumeKey = uint64_t; // st_dev

static bool volumeKeyForPath(const std::filesystem::path& path, VolumeKey& key)
{
	struct stat st;
	if (::stat(path.c_str(), &st) != 0)
		return false;

	key = static_cast<VolumeKey>(st.st_dev);
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

using VolumeKey = std::string; // The mount's BSD device name from statfs, e.g. "/dev/disk3s1"

static bool volumeKeyForPath(const std::filesystem::path& path, VolumeKey& key)
{
	struct statfs fsInfo;
	if (::statfs(path.c_str(), &fsInfo) != 0)
		return false;

	key = fsInfo.f_mntfromname;
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

#if defined _WIN32 || defined __linux__ || defined __APPLE__

#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>

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
