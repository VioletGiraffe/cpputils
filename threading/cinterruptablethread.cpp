#include "cinterruptablethread.h"
#include "assert/advanced_assert.h"
#include "thread_helpers.h"

CInterruptableThread::CInterruptableThread(std::string threadName, ExecBehavior behavior /*= InterruptIfRunning*/) :
	_threadName{std::move(threadName)},
	_behavior(behavior)
{
}

CInterruptableThread::~CInterruptableThread()
{
	interrupt();
}

bool CInterruptableThread::exec(std::function<void ()> executable)
{
	if (!executable)
		return false;

	if (_running)
	{
		if (_behavior == SkipIfRunning)
			return false;
		else if (_behavior != InterruptIfRunning)
			assert_unconditional_r("Unhandled exec behavior");
	}

	// Without the _thread.join() call, the subsequent _thread assignment fails.
	interrupt();
	_terminate = false;

	_thread = std::thread([this, payload{ std::move(executable) }]() {
		_running = true;
		setThreadName(_threadName.c_str());
		payload();
		_running = false;
	});

	return true;
}

// Signals the thread to stop and waits until the thread has exited via join()
void CInterruptableThread::interrupt()
{
	if (_thread.joinable())
	{
		_terminate = true;
		_thread.join();
		_running = false;
	}
}

bool CInterruptableThread::running() const
{
	return _running;
}

const std::atomic<bool>& CInterruptableThread::terminationFlag() const noexcept
{
	return _terminate;
}

