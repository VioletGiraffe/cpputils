#include "thread_helpers.h"

#ifdef _WIN32

// As per https://docs.microsoft.com/en-us/visualstudio/debugger/how-to-set-a-thread-name-in-native-code

#include <windows.h>

#include <array> // std::size

const DWORD MS_VC_EXCEPTION = 0x406D1388;

#pragma pack(push,8)
typedef struct tagTHREADNAME_INFO
{
	DWORD dwType = 0x1000; // Must be 0x1000.
	LPCSTR szName; // Pointer to name (in user addr space).
	DWORD dwThreadID = (DWORD)-1; // Thread ID (-1=caller thread).
	DWORD dwFlags = 0; // Reserved for future use, must be zero.
 } THREADNAME_INFO;
#pragma pack(pop)

void setThreadName(const char *asciiName)
{
	WCHAR multibyteName[256];
	const auto nChars = ::MultiByteToWideChar(CP_UTF8, 0, asciiName, -1, multibyteName, std::size(multibyteName));
	multibyteName[nChars] = 0;
	::SetThreadDescription(::GetCurrentThread(), multibyteName);
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
