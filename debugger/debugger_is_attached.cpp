#include "debugger_is_attached.h"
#include "utility/on_scope_exit.hpp"
#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#endif

bool debuggerIsAttached()
{
#ifdef _WIN32
	return IsDebuggerPresent() != 0;
#else
	char buf[4096];

	const int status_fd = ::open("/proc/self/status", O_RDONLY);
	if (status_fd == -1)
		return false;

	EXEC_ON_SCOPE_EXIT([&status_fd](){
		::close(status_fd);
	});

	const ssize_t num_read = ::read(status_fd, buf, sizeof(buf) - 1);
	if (num_read <= 0)
		return false;

	buf[num_read] = '\0';
	constexpr char tracerPidString[] = "TracerPid:";
	const auto tracer_pid_ptr = ::strstr(buf, tracerPidString);
	if (!tracer_pid_ptr)
		return false;

	for (const char* characterPtr = tracer_pid_ptr + sizeof(tracerPidString) - 1; characterPtr <= buf + num_read; ++characterPtr)
	{
		if (::isspace(*characterPtr))
			continue;
		else
			return ::isdigit(*characterPtr) != 0 && *characterPtr != '0';
	}

	return false;
#endif
}
