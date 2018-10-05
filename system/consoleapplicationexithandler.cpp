#include "consoleapplicationexithandler.h"
#include "../assert/advanced_assert.h"

#include <utility>

static std::function<void ()> exitHandler;

#ifdef _WIN32

#include <Windows.h>
static BOOL WINAPI CtrlHandler(DWORD fdwCtrlType)
{
	switch (fdwCtrlType)
	{
	// Handle the CTRL-C signal.
	case CTRL_C_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		exitHandler();
		return TRUE;

	default:
		return FALSE;
	}
}
#else
#endif

void registerExithandler(std::function<void ()>&& onExit)
{
	exitHandler = std::move(onExit);

#ifdef _WIN32
	assert_r(SetConsoleCtrlHandler(CtrlHandler, TRUE));
#else
#endif
}
