#include "processfilepath.hpp"
#include "../assert/advanced_assert.h"

#ifdef _WIN32
#include <Windows.h>
#elif defined __linux__
#include <unistd.h>
#elif defined __APLLE_
#include <mach-o/dyld.h>
#endif

#include <codecvt>
#include <locale>

#ifndef _WIN32
inline std::wstring stringToWstring(const char* utf8Bytes, const size_t numBytes)
{
	//setup converter
	using convert_type = std::codecvt_utf8<typename std::wstring::value_type>;
	std::wstring_convert<convert_type, typename std::wstring::value_type> converter;

	//use converter (.to_bytes: wstr->str, .from_bytes: str->wstr)
	return converter.from_bytes(utf8Bytes, utf8Bytes + numBytes);
}
#endif

#ifdef _WIN32
std::wstring processFilePath()
{
	constexpr size_t pathBufferSize = 8192 /* max possible path length */ + 1 /* null-terminator */ + 1 /* just in case */;
	WCHAR thisProcessPath[pathBufferSize];
	const auto actualSize = GetModuleFileNameW(nullptr, thisProcessPath, pathBufferSize);
	assert_and_return_message_r(actualSize != pathBufferSize, "Insufficient buffer size.", std::wstring());

	return std::wstring(thisProcessPath, actualSize);
}
#elif defined __linux__
std::wstring processFilePath()
{
	constexpr size_t pathBufferSize = 8192 /* max possible path length */ + 1 /* null-terminator */ + 1 /* just in case */;
	char thisProcessPath[pathBufferSize];
	// Read the symbolic link '/proc/self/exe'.
	const auto actualSize = readlink("/proc/self/exe", thisProcessPath, pathBufferSize - 1);
	assert_and_return_message_r(actualSize > 0, "Insufficient buffer size.", std::wstring());

	return stringToWstring(thisProcessPath, actualSize);
}
#elif defined __APPLE__
std::wstring processFilePath()
{
	constexpr size_t pathBufferSize = 8192 /* max possible path length */ + 1 /* null-terminator */ + 1 /* just in case */;
	char thisProcessPath[pathBufferSize];

	/*
	 * _NSGetExecutablePath() copies the path of the main executable into the buffer. The bufsize parameter
	 * should initially be the size of the buffer.  The function returns 0 if the path was successfully copied,
	 * and *bufsize is left unchanged. It returns -1 if the buffer is not large enough, and *bufsize is set
	 * to the size required.
	 *
	 * Note that _NSGetExecutablePath will return "a path" to the executable not a "real path" to the executable.
	 * That is the path may be a symbolic link and not the real file. With deep directories the total bufsize
	 * needed could be more than MAXPATHLEN.
	 */

	uint32_t actualSize = 0;
	auto resultCode = _NSGetExecutablePath(thisProcessPath, &actualSize);
	assert_and_return_r(resultCode == -1 && actualSize > 0, std::wstring());

	resultCode = _NSGetExecutablePath(thisProcessPath, &actualSize);
	assert_and_return_r(resultCode == 0, std::wstring());

	return stringToWstring(thisProcessPath, actualSize);
}
#endif
