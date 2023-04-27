#include "thread_helpers.h"

#ifdef _WIN32
#include "assert/advanced_assert.h"
#include "utility/on_scope_exit.hpp"

#include <Windows.h>

#include <array> // std::size

void setThreadName(const char *asciiName)
{
	auto* k32 = ::LoadLibraryA("kernel32.dll");
	assert_and_return_r(k32, );

	EXEC_ON_SCOPE_EXIT([k32]{
		::FreeLibrary(k32);
	});

	auto* func = reinterpret_cast<decltype(&::SetThreadDescription)>(::GetProcAddress(k32, "SetThreadDescription"));
	if (func == nullptr)
		return;

	WCHAR multibyteName[256];
	const auto nChars = ::MultiByteToWideChar(CP_UTF8, 0, asciiName, -1, multibyteName, static_cast<int>(std::size(multibyteName)));
	multibyteName[nChars] = 0;
	func(::GetCurrentThread(), multibyteName);
}

#elif defined __APPLE__

#include <pthread.h>

void setThreadName(const char * asciiName)
{
	pthread_setname_np(asciiName);
}

#elif defined __linux__

#include <sys/prctl.h>

void setThreadName(const char * asciiName)
{
	prctl(PR_SET_NAME, asciiName, 0, 0, 0);
}
#elif defined __FreeBSD__
#include <pthread_np.h>
#include <pthread.h>
void setThreadName(const char * asciiName)
{
	pthread_set_name_np(pthread_self(),asciiName);
}

#endif
