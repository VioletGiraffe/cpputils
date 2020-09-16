#include "cperiodicexecutionthread.h"
#include "compiler/compiler_warnings_control.h"
#include "assert/advanced_assert.h"
#include "thread_helpers.h"

#ifdef QT_VERSION
	DISABLE_COMPILER_WARNINGS
	#include <QDebug>
	RESTORE_COMPILER_WARNINGS
	#define DEBUG_LOG(X) qInfo() << X
#else
	#define DEBUG_LOG(X)
#endif

CPeriodicExecutionThread::CPeriodicExecutionThread(unsigned int period_ms, const std::string& threadName, const std::function<void()>& workload /*= std::function<void ()>()*/) :
	_workload(workload),
	_threadName(threadName),
	_period(period_ms)
{

}

CPeriodicExecutionThread::~CPeriodicExecutionThread()
{
	terminate();
}

void CPeriodicExecutionThread::setWorkload(const std::function<void()>& workload)
{
	if (!_thread.joinable())
		_workload = workload;
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
	setThreadName(_threadName.c_str());

	assert_and_return_r(_workload, );

	DEBUG_LOG("Starting CPeriodicExecutionThread" << QString::fromStdString(_threadName));

	constexpr const uint32_t sleepChunkLength = 100u;
	const auto nSleepChunks = _period / sleepChunkLength;
	const auto remainder = _period % sleepChunkLength;

	if (delayBeforeStartMs > 0)
	{
		uint32_t elapsed = 0;
		while (elapsed < delayBeforeStartMs && !_terminate)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(sleepChunkLength));
			elapsed += sleepChunkLength;
		}
	}

	while (!_terminate) // Main workload loop
	{
		
		_workload();

		for (uint32_t i = 0; i < nSleepChunks && !_terminate; ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(sleepChunkLength));
		if (remainder != 0 && !_terminate)
			std::this_thread::sleep_for(std::chrono::milliseconds(remainder));
	}

	DEBUG_LOG("CPeriodicExecutionThread" << QString::fromStdString(_threadName) << "finished and exiting");
}
