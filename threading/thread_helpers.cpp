#include "thread_helpers.h"

#ifdef _WIN32

// As per https://docs.microsoft.com/en-us/visualstudio/debugger/how-to-set-a-thread-name-in-native-code

#include <windows.h>

const DWORD MS_VC_EXCEPTION = 0x406D1388;

#pragma pack(push,8)
typedef struct tagTHREADNAME_INFO
{
	DWORD dwType = 0x1000; // Must be 0x1000.
	LPCSTR szName; // Pointer to name (in user addr space).
	DWORD dwThreadID = -1; // Thread ID (-1=caller thread).
	DWORD dwFlags = 0; // Reserved for future use, must be zero.
 } THREADNAME_INFO;
#pragma pack(pop)

void setThreadName(const char *asciiName)
{
	THREADNAME_INFO info;
	info.szName = asciiName;

	__try {
		RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
	}
	__except (GetExceptionCode() == MS_VC_EXCEPTION){
	}
}

#else

void setThreadName(const char * /*asciiName*/)
{

}

#endif
