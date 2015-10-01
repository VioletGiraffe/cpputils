#include "cinterruptablethread.h"
#include "assert/advanced_assert.h"

CInterruptableThread::CInterruptableThread(const std::string& threadName, ExecBehavior behavior /*= InterruptIfRunning*/) :
	_threadName(threadName),
	_behavior(behavior)
{
}

bool CInterruptableThread::exec(const std::function<void ()>& executable)
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

	// Without the _thread.join() call, the subsequent _thread assignment fails. Why?
	interrupt();

	_thread = std::thread([this, executable](){
		_running = true;
		executable();
		_running = false;
	});

	return true;
}

void CInterruptableThread::interrupt()
{
	if (_thread.joinable())
	{
		_terminate = true;
		_thread.join();
		_terminate = false;
	}
}

bool CInterruptableThread::running() const
{
	return _running;
}

const std::atomic<bool>& CInterruptableThread::terminationFlag() const
{
	return _terminate;
}

