#include "cperiodicexecutionthread.h"
#include "compiler/compiler_warnings_control.h"
#include "assert/advanced_assert.h"
#include "thread_helpers.h"


CPeriodicExecutionThread::CPeriodicExecutionThread(unsigned int period_ms, std::string threadName, std::function<void()> workload) :
	_workload{ std::move(workload) },
	_threadName{ std::move(threadName) },
	_period{ period_ms }
{
}

CPeriodicExecutionThread::~CPeriodicExecutionThread()
{
	terminate();
}

void CPeriodicExecutionThread::setWorkload(std::function<void()> workload)
{
	if (!_thread.joinable())
		_workload = std::move(workload);
	else
		assert_unconditional_r("The thread has already started");
}

void CPeriodicExecutionThread::start(std::function<void()>&& workload, uint32_t delayBeforeStartMs)
{
	if (!_thread.joinable())
	{
		_workload = std::move(workload);
		_thread = std::thread(&CPeriodicExecutionThread::threadFunc, this, delayBeforeStartMs);
	}
	else
		assert_unconditional_r("The thread has already started");
}

void CPeriodicExecutionThread::terminate()
{
	if (_thread.joinable())
	{
		{
			std::lock_guard locker{ _cvMutex };
			_terminate = true;
			_cv.notify_all(); // Wake the thread immediately, whether it's parked paused or mid-period
		}
		_thread.join();
		_terminate = false; // No lock needed: the thread is joined, nothing else touches this concurrently
	}
}

void CPeriodicExecutionThread::pause()
{
	std::lock_guard locker{ _cvMutex };
	_paused = true;
	_cv.notify_all(); // Cut the current period wait short so the thread parks right away instead of running the workload once more
}

void CPeriodicExecutionThread::resume()
{
	std::lock_guard locker{ _cvMutex };
	_paused = false;
	_cv.notify_all(); // Wake the thread out of its indefinite park; the workload runs again immediately
}

void CPeriodicExecutionThread::threadFunc(uint32_t delayBeforeStartMs)
{
	setThreadName(_threadName);

	assert_and_return_r(_workload, );

	if (delayBeforeStartMs > 0)
	{
		std::unique_lock locker{ _cvMutex };
		_cv.wait_for(locker, std::chrono::milliseconds(delayBeforeStartMs), [this] { return _terminate; });
	}

	for (;;) // Main workload loop
	{
		{
			// Parked here while paused; resume()/terminate() notify and wake it immediately.
			std::unique_lock locker{ _cvMutex };
			_cv.wait(locker, [this] { return !_paused || _terminate; });
			if (_terminate)
				return;
		}

		_workload();

		// Waits out the period, but pause()/terminate() cut it short via notify.
		std::unique_lock locker{ _cvMutex };
		_cv.wait_for(locker, std::chrono::milliseconds(_period), [this] { return _paused || _terminate; });
		if (_terminate)
			return;
	}
}
