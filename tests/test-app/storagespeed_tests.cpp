#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "system/storagespeed.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cwctype>
#elif defined __linux__
#include <sys/stat.h>
#include <sys/sysmacros.h>
#elif defined __APPLE__
#include <sys/mount.h>
#include <sys/param.h>
#endif

using namespace storage_speed_detail;

namespace {

// A fake file system for resolveLocalSymlinks: link path -> target. Records every path it is asked about.
struct FakeLinks
{
	std::map<std::string, std::string> links;
	std::vector<std::string> reads;

	[[nodiscard]] std::function<std::string(const std::string&)> reader()
	{
		return [this](const std::string& path) {
			reads.push_back(path);
			const auto it = links.find(path);
			return it == links.end() ? std::string{} : it->second;
		};
	}

	[[nodiscard]] bool readAnythingWithin(const std::string& folder) const
	{
		for (const std::string& read : reads)
		{
			if (read == folder || read.starts_with(folder + '/'))
				return true;
		}
		return false;
	}
};

constexpr auto quickly = std::chrono::seconds(1);

} // namespace

TEST_CASE("parseMountInfo reads mount points, devices and file system types", "[storagespeed]")
{
	const std::vector<Mount> mounts = parseMountInfo(
		"22 1 8:2 / / rw,relatime shared:1 - ext4 /dev/sda2 rw\n"
		"40 22 8:17 / /mnt/my\\040photos\\011tab\\134back rw shared:20 master:3 - btrfs /dev/sdb1 rw\n"
		"41 22 0:50 / /mnt/nas rw,relatime - cifs //server/share rw\n"
		"42 22 0:51 / /net rw - autofs auto.net rw\n"
		"43 22 0:52 / /run/user/1000/gvfs rw - fuse.gvfsd-fuse gvfsd-fuse rw\n"
		"44 22 8:33 / /media/usb rw - fuseblk /dev/sdc1 rw\n"
		"45 22 0:53 /sub /bind rw - somefs none rw\n");

	REQUIRE(mounts.size() == 7);
	CHECK(mounts[0].mountPoint == "/");
	CHECK(mounts[0].device == "8:2");
	CHECK(mounts[0].local);
	// Escapes decoded; several optional fields skipped
	CHECK(mounts[1].mountPoint == "/mnt/my photos\ttab\\back");
	CHECK(mounts[1].device == "8:17");
	CHECK(mounts[1].local);
	CHECK_FALSE(mounts[2].local); // network
	CHECK_FALSE(mounts[3].local); // autofs
	CHECK_FALSE(mounts[4].local); // FUSE, which can be remote
	CHECK(mounts[5].local);       // fuseblk is backed by a block device
	CHECK(mounts[6].mountPoint == "/bind"); // The mount point, not the root of the mount
	CHECK_FALSE(mounts[6].local); // Unknown type
}

TEST_CASE("parseMountInfo skips malformed lines and keeps incomplete escapes literally", "[storagespeed]")
{
	const std::vector<Mount> mounts = parseMountInfo(
		"\n"
		"garbage\n"
		"1 2 3:4\n"
		"1 2 3:4 / /no-separator rw\n"
		"1 2 3:4 / /nothing-after-separator rw -\n"
		"9 9 1:1 / /a\\04 rw - ext4 /dev/x rw\n"
		"9 9 1:2 / /b\\999 rw - ext4 /dev/x rw\n"
		"5 6 7:8 / /no-trailing-newline rw - ext4 /dev/x rw");

	REQUIRE(mounts.size() == 3);
	CHECK(mounts[0].mountPoint == "/a\\04");
	CHECK(mounts[1].mountPoint == "/b\\999");
	CHECK(mounts[2].mountPoint == "/no-trailing-newline");
}

TEST_CASE("mountForPath picks the longest mount point on whole path components", "[storagespeed]")
{
	const std::vector<Mount> mounts{
		{ "/", "root", true },
		{ "/mnt", "mnt", true },
		{ "/mnt/nas", "nas", false },
		{ "/mnt/nas/inner", "inner", true },
		{ "/mnt/disk", "hidden", true },
		{ "/mnt/disk", "on-top", true },
	};
	const auto deviceFor = [&mounts](const std::string& path) {
		const Mount* mount = mountForPath(mounts, path);
		return mount ? mount->device : std::string{ "none" };
	};

	CHECK(deviceFor("/") == "root");
	CHECK(deviceFor("/home/user") == "root");
	CHECK(deviceFor("/mnt") == "mnt");
	CHECK(deviceFor("/mnt/nas") == "nas");
	CHECK(deviceFor("/mnt/nas/") == "nas");
	CHECK(deviceFor("/mnt/nas/photo.jpg") == "nas");
	CHECK(deviceFor("/mnt/nasty/photo.jpg") == "mnt");
	CHECK(deviceFor("/mnt/nas/inner/photo.jpg") == "inner");
	CHECK(deviceFor("/mnt/disk/photo.jpg") == "on-top");

	CHECK(mountForPath({}, "/home") == nullptr);
	CHECK(mountForPath({ { "/mnt", "mnt", true } }, "/home") == nullptr);
}

TEST_CASE("parseFirmlinks and canonicalPath rewrite paths into the data volume", "[storagespeed]")
{
	const std::vector<Firmlink> firmlinks = parseFirmlinks(
		"/Applications\tApplications\n"
		"/Users\tUsers\n"
		"/usr/local\tusr/local\n"
		"relative\tx\n"
		"/no-tab\n"
		"/empty-target\t\n"
		"\n",
		"/System/Volumes/Data");

	REQUIRE(firmlinks.size() == 3);
	CHECK(firmlinks[1].path == "/Users");
	CHECK(firmlinks[1].target == "/System/Volumes/Data/Users");

	CHECK(canonicalPath("/Users/me/photo.jpg", firmlinks) == "/System/Volumes/Data/Users/me/photo.jpg");
	CHECK(canonicalPath("/Users", firmlinks) == "/System/Volumes/Data/Users");
	CHECK(canonicalPath("/usr/local/bin/tool", firmlinks) == "/System/Volumes/Data/usr/local/bin/tool"); // Nested firmlink
	CHECK(canonicalPath("/usr/bin/tool", firmlinks) == "/usr/bin/tool");
	CHECK(canonicalPath("/Usersx/photo.jpg", firmlinks) == "/Usersx/photo.jpg");
	CHECK(canonicalPath("/", firmlinks) == "/");
	CHECK(canonicalPath("/Users/me", {}) == "/Users/me");
}

TEST_CASE("resolveLocalSymlinks follows links as the kernel does", "[storagespeed]")
{
	const std::vector<Mount> mounts{ { "/", "root", true }, { "/mnt/hdd", "hdd", true } };
	FakeLinks fake;
	fake.links = {
		{ "/home/u/photos", "/mnt/hdd/photos" },
		{ "/home/u/relative", "../../mnt/hdd" },
		{ "/home/u/chain1", "chain2" },
		{ "/home/u/chain2", "/mnt/hdd" },
		{ "/mnt/hdd/back", "/home/u" },
		{ "/home/u/dots", "./../u/./photos/" },
	};
	const auto resolve = [&](const std::string& path) { return resolveLocalSymlinks(path, mounts, {}, fake.reader()); };

	CHECK(resolve("/home/u/photos/a.jpg") == "/mnt/hdd/photos/a.jpg");   // Absolute target, link in the middle
	CHECK(resolve("/home/u/photos") == "/mnt/hdd/photos");               // Link as the last component
	CHECK(resolve("/home/u/relative/a.jpg") == "/mnt/hdd/a.jpg");        // Relative target climbing out
	CHECK(resolve("/home/u/chain1/a.jpg") == "/mnt/hdd/a.jpg");          // Link to a link
	CHECK(resolve("/mnt/hdd/back/photos/a.jpg") == "/mnt/hdd/photos/a.jpg"); // Target holding a link
	CHECK(resolve("/home/u/dots/a.jpg") == "/mnt/hdd/photos/a.jpg");     // Dot segments in the target
	CHECK(resolve("/no/such/path") == "/no/such/path");
	CHECK(resolve("/") == "/");
}

TEST_CASE("resolveLocalSymlinks never reads a link on a remote mount", "[storagespeed]")
{
	const std::vector<Mount> mounts{ { "/", "root", true }, { "/mnt/nas", "nas", false }, { "/net", "autofs", false } };
	FakeLinks fake;
	fake.links = {
		{ "/home/u/nas", "/mnt/nas/photos" },
		{ "/mnt/nas", "/should-not-be-read" },
		{ "/mnt/nas/link", "/home" },
	};
	const auto resolve = [&](const std::string& path) { return resolveLocalSymlinks(path, mounts, {}, fake.reader()); };

	CHECK(resolve("/mnt/nas/link/a.jpg") == "/mnt/nas/link/a.jpg");
	CHECK(resolve("/home/u/nas/a.jpg") == "/mnt/nas/photos/a.jpg"); // A local link into the remote mount is followed up to it
	CHECK(resolve("/net/host/share/a.jpg") == "/net/host/share/a.jpg");
	CHECK_FALSE(fake.readAnythingWithin("/mnt/nas"));
	CHECK_FALSE(fake.readAnythingWithin("/net"));
}

TEST_CASE("resolveLocalSymlinks gives up on link loops", "[storagespeed]")
{
	const std::vector<Mount> mounts{ { "/", "root", true } };
	FakeLinks fake;
	fake.links = { { "/a", "/b" }, { "/b", "a" }, { "/self", "self" } };

	CHECK_FALSE(resolveLocalSymlinks("/a/x", mounts, {}, fake.reader()).empty());
	CHECK_FALSE(resolveLocalSymlinks("/self/x", mounts, {}, fake.reader()).empty());
	CHECK(fake.reads.size() < 200);
}

TEST_CASE("resolveLocalSymlinks on the macOS layout: symlinks into firmlinks, a share under /Volumes", "[storagespeed]")
{
	const std::vector<Firmlink> firmlinks = parseFirmlinks("/Users\tUsers\n/Volumes\tVolumes\n/private\tprivate\n", "/System/Volumes/Data");
	// Mount points are canonical, as currentMounts makes them
	const std::vector<Mount> mounts{
		{ "/", "/dev/disk1s1s1", true },
		{ "/System/Volumes/Data", "/dev/disk1s5", true },
		{ canonicalPath("/Volumes/nas", firmlinks), "//user@server/share", false },
	};
	FakeLinks fake;
	fake.links = {
		{ "/var", "private/var" },
		{ "/System/Volumes/Data/Users/me/nas", "/Volumes/nas/photos" },
	};
	const auto deviceFor = [&](const std::string& path) {
		const Mount* mount = mountForPath(mounts, resolveLocalSymlinks(canonicalPath(path, firmlinks), mounts, firmlinks, fake.reader()));
		return mount ? mount->device : std::string{};
	};

	CHECK(deviceFor("/var/folders/x/T/photo.jpg") == "/dev/disk1s5"); // Symlink, then firmlink
	CHECK(deviceFor("/Users/me/photo.jpg") == "/dev/disk1s5");
	CHECK(deviceFor("/System/Library/x") == "/dev/disk1s1s1");
	CHECK(deviceFor("/Volumes/nas/photo.jpg") == "//user@server/share");
	CHECK(deviceFor("/Users/me/nas/photo.jpg") == "//user@server/share");
	CHECK_FALSE(fake.readAnythingWithin("/System/Volumes/Data/Volumes/nas"));
}

TEST_CASE("storageSpeedForPath does not need the path to exist", "[storagespeed]")
{
	const std::filesystem::path existing = std::filesystem::temp_directory_path();
	CHECK(storageSpeedForPath(existing / "no-such-folder" / "no-such-file.jpg") == storageSpeedForPath(existing));
}

#ifdef _WIN32

TEST_CASE("storageSpeedForPath classifies an unreachable network path without accessing it", "[storagespeed]")
{
	// 192.0.2.0/24 is reserved for documentation: accessing it waits for the network timeout.
	// Each spelling uses its own host: Windows remembers a failed host for a while, which would hide a slow lookup.
	for (const wchar_t* path : { LR"(\\192.0.2.1\share\photo.jpg)", LR"(\\?\UNC\192.0.2.2\share\photo.jpg)", L"//192.0.2.3/share/photo.jpg" })
	{
		INFO(std::filesystem::path{ path }.string());
		const auto start = std::chrono::steady_clock::now();
		CHECK(storageSpeedForPath(path) == StorageSpeed::SlowOrUnknown);
		CHECK(std::chrono::steady_clock::now() - start < quickly);
	}
}

TEST_CASE("storageSpeedForPath classifies a drive letter that is not present without accessing it", "[storagespeed]")
{
	const DWORD presentDrives = GetLogicalDrives(); // Mapped network drives included
	wchar_t freeLetter = 0;
	for (wchar_t letter = L'Z'; letter >= L'D' && !freeLetter; --letter)
	{
		if (!(presentDrives & (1u << (letter - L'A'))))
			freeLetter = letter;
	}
	REQUIRE(freeLetter != 0);

	const auto start = std::chrono::steady_clock::now();
	CHECK(storageSpeedForPath(std::wstring{ freeLetter } + L":\\photos\\photo.jpg") == StorageSpeed::SlowOrUnknown);
	CHECK(std::chrono::steady_clock::now() - start < quickly);
}

TEST_CASE("storageSpeedForPath classifies every spelling of a local path alike", "[storagespeed]")
{
	const std::filesystem::path temp = std::filesystem::temp_directory_path().lexically_normal();
	const std::wstring native = temp.native();
	REQUIRE(native.size() > 3);
	REQUIRE(native[1] == L':');
	const StorageSpeed expected = storageSpeedForPath(temp);

	std::wstring lowercaseDrive = native;
	lowercaseDrive[0] = static_cast<wchar_t>(towlower(lowercaseDrive[0]));
	CHECK(storageSpeedForPath(lowercaseDrive) == expected);
	CHECK(storageSpeedForPath(temp.generic_wstring()) == expected); // Forward slashes
	CHECK(storageSpeedForPath(L"\\\\?\\" + native) == expected);
	CHECK(storageSpeedForPath(temp / L"." / L"x" / L".." / L"photo.jpg") == expected);

	const std::filesystem::path current = std::filesystem::current_path();
	CHECK(storageSpeedForPath(".") == storageSpeedForPath(current));
	CHECK(storageSpeedForPath("relative/photo.jpg") == storageSpeedForPath(current));
}

TEST_CASE("storageSpeedForPath returns promptly for device namespace paths", "[storagespeed]")
{
	const auto start = std::chrono::steady_clock::now();
	(void)storageSpeedForPath(LR"(\\.\C:\)");
	(void)storageSpeedForPath(LR"(\\.\PhysicalDrive0)");
	CHECK(std::chrono::steady_clock::now() - start < quickly);
}

#else // Linux and macOS

namespace {

[[nodiscard]] std::string readWholeFile(const std::filesystem::path& path)
{
	std::ifstream file{ path };
	std::ostringstream contents;
	contents << file.rdbuf();
	return contents.str();
}

// The kernel's own answer, which follows symlinks and firmlinks: what volumeDeviceForPath must reproduce from the mount tables
[[nodiscard]] std::string kernelDevice(const std::filesystem::path& path)
{
#ifdef __linux__
	struct stat st;
	if (::stat(path.c_str(), &st) != 0)
		return {};
	return std::to_string(major(st.st_dev)) + ':' + std::to_string(minor(st.st_dev));
#else
	struct statfs fs;
	return ::statfs(path.c_str(), &fs) == 0 ? std::string{ fs.f_mntfromname } : std::string{};
#endif
}

void checkMatchesKernel(const std::filesystem::path& path)
{
	const std::string expected = kernelDevice(path);
	if (expected.empty())
		return; // Absent on this system, or not accessible
	INFO(path.string());
	CHECK(volumeDeviceForPath(path) == expected);
}

// A fresh folder under the temp directory. Links are removed one by one before the folder, so the cleanup cannot follow them.
class ScratchFolder
{
public:
	ScratchFolder()
		: _path{ std::filesystem::temp_directory_path() / ("cpputils-storagespeed-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) }
	{
		std::filesystem::create_directories(_path);
	}

	~ScratchFolder()
	{
		std::error_code ec;
		for (const std::filesystem::path& link : _links)
			std::filesystem::remove(link, ec);
		std::filesystem::remove_all(_path, ec);
	}

	ScratchFolder(const ScratchFolder&) = delete;
	ScratchFolder& operator=(const ScratchFolder&) = delete;

	[[nodiscard]] const std::filesystem::path& path() const { return _path; }

	std::filesystem::path link(const std::string& name, const std::filesystem::path& target)
	{
		const std::filesystem::path link = _path / name;
		std::filesystem::create_symlink(target, link);
		_links.push_back(link);
		return link;
	}

private:
	std::filesystem::path _path;
	std::vector<std::filesystem::path> _links;
};

} // namespace

TEST_CASE("volumeDeviceForPath: every local mount point maps to its own device", "[storagespeed]")
{
	// Checks the mount table parsing, escapes and canonical forms against every mount this system has
	std::vector<std::string> mountPoints;
#ifdef __linux__
	const std::vector<Mount> mounts = parseMountInfo(readWholeFile("/proc/self/mountinfo"));
	REQUIRE_FALSE(mounts.empty());
	for (const Mount& mount : mounts)
	{
		// A hidden mount reports the device of the one on top of it
		if (mount.local && mountForPath(mounts, mount.mountPoint) == &mount)
			mountPoints.push_back(mount.mountPoint);
	}
#else
	const int count = ::getfsstat(nullptr, 0, MNT_NOWAIT);
	REQUIRE(count > 0);
	std::vector<struct statfs> mounts(static_cast<size_t>(count));
	const int filled = ::getfsstat(mounts.data(), static_cast<int>(mounts.size() * sizeof(struct statfs)), MNT_NOWAIT);
	for (int i = 0; i < filled; ++i)
	{
		const struct statfs& mount = mounts[static_cast<size_t>(i)];
		if ((mount.f_flags & MNT_LOCAL) && std::string_view{ mount.f_fstypename } != "autofs")
			mountPoints.push_back(mount.f_mntonname);
	}
#endif
	REQUIRE_FALSE(mountPoints.empty());
	for (const std::string& mountPoint : mountPoints)
		checkMatchesKernel(mountPoint);
}

TEST_CASE("volumeDeviceForPath matches the kernel for everyday locations", "[storagespeed]")
{
	checkMatchesKernel("/");
	checkMatchesKernel(std::filesystem::temp_directory_path());
	checkMatchesKernel(std::filesystem::current_path());
	if (const char* home = std::getenv("HOME"))
	{
		checkMatchesKernel(home);
		checkMatchesKernel(std::filesystem::path{ home } / "Pictures");
	}
	for (const char* path : { "/usr/bin", "/etc/hosts", "/tmp", "/var/log", "/dev", "/opt" })
		checkMatchesKernel(path);
#ifdef __linux__
	for (const char* path : { "/proc", "/proc/self", "/proc/self/cwd", "/sys", "/sys/class", "/run", "/dev/shm", "/dev/pts", "/home", "/boot", "/snap" })
		checkMatchesKernel(path);
#else
	// Symlinks into /private, firmlinks into the data volume (one nested), the sealed system volume
	for (const char* path : { "/private/var", "/etc", "/Applications", "/Library", "/Users", "/usr/local", "/System/Library", "/System/Volumes/Data", "/Volumes" })
		checkMatchesKernel(path);
#endif
}

TEST_CASE("volumeDeviceForPath follows symlinks as the kernel does", "[storagespeed]")
{
	ScratchFolder scratch;
	const std::filesystem::path& folder = scratch.path();
	// Another mount than the root and, mostly, than the temp folder
#ifdef __linux__
	const std::filesystem::path otherMount = "/proc";
	const std::filesystem::path linkInsideTarget = "/proc/self"; // A symlink itself
#else
	const std::filesystem::path otherMount = "/dev";
	const std::filesystem::path linkInsideTarget = "/tmp"; // A symlink into a firmlink
#endif

	checkMatchesKernel(scratch.link("toRoot", "/"));
	checkMatchesKernel(scratch.link("toOtherMount", otherMount));
	checkMatchesKernel(scratch.link("toUsrBin", "/usr") / "bin"); // Link in the middle of the path
	checkMatchesKernel(scratch.link("toParent", ".."));
	checkMatchesKernel(scratch.link("toLink", linkInsideTarget));

	scratch.link("chainEnd", otherMount);
	checkMatchesKernel(scratch.link("chainStart", "chainEnd"));

	// Climbs out of the folder and back in by name
	checkMatchesKernel(scratch.link("outAndBack", "../" + folder.filename().string() + "/chainEnd"));

	// Unlike the kernel, which takes ".." after following the link, the lookup takes it lexically first
	CHECK(volumeDeviceForPath(folder / "toOtherMount" / ".." / "toRoot") == volumeDeviceForPath(folder / "toRoot"));

	// The kernel cannot follow these, so they are compared with what they lexically stand for
	scratch.link("dangling", "/no-such-folder-cpputils/inner");
	CHECK(volumeDeviceForPath(folder / "dangling" / "photo.jpg") == volumeDeviceForPath("/"));
	scratch.link("loopA", "loopB");
	scratch.link("loopB", "loopA");
	scratch.link("self", "self");
	const auto start = std::chrono::steady_clock::now();
	CHECK(volumeDeviceForPath(folder / "loopA" / "photo.jpg") == volumeDeviceForPath(folder));
	CHECK(volumeDeviceForPath(folder / "self" / "photo.jpg") == volumeDeviceForPath(folder));
	CHECK(std::chrono::steady_clock::now() - start < quickly);
}

TEST_CASE("volumeDeviceForPath accepts paths that are not plain absolute ones", "[storagespeed]")
{
	const std::filesystem::path temp = std::filesystem::temp_directory_path();
	const std::string expected = volumeDeviceForPath(temp);
	REQUIRE_FALSE(expected.empty());

	CHECK(volumeDeviceForPath(temp.string() + "/") == expected);
	CHECK(volumeDeviceForPath(temp / "no-such-folder" / ".." / "." / "photo.jpg") == expected);
	CHECK(volumeDeviceForPath(temp / "no-such-folder" / "deeper" / "photo.jpg") == expected);
	CHECK(volumeDeviceForPath("/usr//bin") == volumeDeviceForPath("/usr/bin"));
	CHECK(volumeDeviceForPath(".") == volumeDeviceForPath(std::filesystem::current_path()));
	CHECK(volumeDeviceForPath("relative/photo.jpg") == volumeDeviceForPath(std::filesystem::current_path()));
}

#ifdef __APPLE__
TEST_CASE("macOS: /usr/share/firmlinks has the format the lookup expects", "[storagespeed]")
{
	const std::string contents = readWholeFile("/usr/share/firmlinks");
	REQUIRE_FALSE(contents.empty());
	const std::vector<Firmlink> firmlinks = parseFirmlinks(contents, "/System/Volumes/Data");

	// Every non-empty line parses
	size_t lineCount = 0;
	std::istringstream lines{ contents };
	for (std::string line; std::getline(lines, line);)
		lineCount += line.empty() ? 0 : 1;
	CHECK(firmlinks.size() == lineCount);

	const auto has = [&firmlinks](const std::string& path) {
		return std::any_of(firmlinks.begin(), firmlinks.end(), [&path](const Firmlink& firmlink) { return firmlink.path == path; });
	};
	CHECK(has("/Users"));
	CHECK(has("/private"));
	CHECK(has("/Volumes"));
	for (const Firmlink& firmlink : firmlinks)
	{
		INFO(firmlink.path);
		CHECK(std::filesystem::exists(firmlink.target));
	}
}
#endif

#endif
