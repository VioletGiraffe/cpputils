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
		_terminate = true;
		_thread.join();
		_terminate = false;
	}
}

void CPeriodicExecutionThread::threadFunc(uint32_t delayBeforeStartMs)
{
	setThreadName(_threadName);

	assert_and_return_r(_workload, );

	static constexpr const uint32_t sleepChunkLengthMs = 100u;

	if (delayBeforeStartMs > 0)
	{
		const auto nSleepChunksBeforeStart = delayBeforeStartMs / sleepChunkLengthMs;
		for (size_t i = 0; i < nSleepChunksBeforeStart && !_terminate; ++i)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(sleepChunkLengthMs));
		}
	}

	const auto nSleepChunks = _period / sleepChunkLengthMs;
	const auto remainder = _period % sleepChunkLengthMs;

	while (!_terminate) // Main workload loop
	{
		_workload();

		for (size_t i = 0; i < nSleepChunks && !_terminate; ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(sleepChunkLengthMs));

		if (remainder != 0 && !_terminate)
			std::this_thread::sleep_for(std::chrono::milliseconds(remainder));
	}
}
