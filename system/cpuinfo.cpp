#include "cpuinfo.hpp"
#include "../assert/advanced_assert.h"

#ifdef _WIN32
#include <Windows.h>
#elif defined __APPLE__
#include <sys/sysctl.h>
#else
#include <fstream>
#endif

#include <string_view>

// Brand strings are padded with spaces
static std::string trimmed(std::string_view text)
{
	const auto first = text.find_first_not_of(" \t");
	if (first == std::string_view::npos)
		return {};

	const auto last = text.find_last_not_of(" \t");
	return std::string{ text.substr(first, last - first + 1) };
}

#ifdef _WIN32
std::string cpuModelName()
{
	char name[256];
	DWORD size = sizeof(name);
	const LSTATUS status = ::RegGetValueA(HKEY_LOCAL_MACHINE, R"(HARDWARE\DESCRIPTION\System\CentralProcessor\0)", "ProcessorNameString", RRF_RT_REG_SZ, nullptr, name, &size);
	assert_and_return_message_r(status == ERROR_SUCCESS, "RegGetValueA failed", {});

	return trimmed(name);
}
#elif defined __APPLE__
std::string cpuModelName()
{
	char name[256];
	size_t size = sizeof(name);
	const int result = ::sysctlbyname("machdep.cpu.brand_string", name, &size, nullptr, 0);
	assert_and_return_message_r(result == 0, "sysctlbyname failed", {});

	return trimmed(name);
}
#else
std::string cpuModelName()
{
	std::ifstream cpuinfo{ "/proc/cpuinfo" };
	assert_and_return_message_r(cpuinfo.is_open(), "Failed to open /proc/cpuinfo", {});

	// x86 kernels report "model name"; ARM kernels may report only "Hardware" or "Model"
	std::string fallbackName;
	for (std::string line; std::getline(cpuinfo, line);)
	{
		const auto colon = line.find(':');
		if (colon == std::string::npos)
			continue;

		const std::string key = trimmed(std::string_view{ line }.substr(0, colon));
		const std::string value = trimmed(std::string_view{ line }.substr(colon + 1));
		if (key == "model name")
			return value;

		if (fallbackName.empty() && (key == "Hardware" || key == "Model"))
			fallbackName = value;
	}

	assert_and_return_message_r(!fallbackName.empty(), "No CPU model in /proc/cpuinfo", {});
	return fallbackName;
}
#endif
