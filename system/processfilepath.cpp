#include "processfilepath.hpp"
#include "../assert/advanced_assert.h"

#ifdef _WIN32
#include <Windows.h>
#elif defined __linux__
#include <unistd.h>
#elif defined __APPLE__
#include <mach-o/dyld.h>
#endif

#include <cstddef>
#include <cstdint>
#include <string_view>

// Room for an 8192-character path and a terminator
static constexpr size_t pathBufferSize = 8192 + 1;

#ifdef _WIN32
std::filesystem::path processFilePath()
{
	WCHAR thisProcessPath[pathBufferSize];
	const auto actualSize = GetModuleFileNameW(nullptr, thisProcessPath, pathBufferSize);
	assert_and_return_message_r(actualSize != pathBufferSize, "Insufficient buffer size.", {});

	return std::wstring_view{ thisProcessPath, actualSize };
}
#elif defined __linux__
std::filesystem::path processFilePath()
{
	char thisProcessPath[pathBufferSize];
	// readlink writes no terminator: a result filling the buffer may be truncated
	const ssize_t actualSize = readlink("/proc/self/exe", thisProcessPath, pathBufferSize);
	assert_and_return_message_r(actualSize > 0, "readlink failed.", {});
	assert_and_return_message_r(static_cast<size_t>(actualSize) < pathBufferSize, "Insufficient buffer size.", {});

	return std::string_view{ thisProcessPath, static_cast<size_t>(actualSize) };
}
#elif defined __APPLE__
std::filesystem::path processFilePath()
{
	char thisProcessPath[pathBufferSize];
	uint32_t bufferSize{ pathBufferSize };
	// -1 when the buffer is too small; on success the path is null-terminated
	const int resultCode = _NSGetExecutablePath(thisProcessPath, &bufferSize);
	assert_and_return_message_r(resultCode == 0, "Insufficient buffer size.", {});

	return thisProcessPath;
}
#endif
