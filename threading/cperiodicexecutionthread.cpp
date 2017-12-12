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

void CPeriodicExecutionThread::start(const std::function<void()>& workload /*= std::function<void ()>()*/)
{
	if (!_thread.joinable())
	{
		if (workload)
			_workload = workload;

		_thread = std::thread(&CPeriodicExecutionThread::threadFunc, this);
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

void CPeriodicExecutionThread::threadFunc()
{
	assert_and_return_r(_workload, );

	setThreadName(_threadName.c_str());

	DEBUG_LOG("Starting CPeriodicExecutionThread" << QString::fromStdString(_threadName));

	while (!_terminate) // Main threadFunc loop
	{
		_workload();

		std::this_thread::sleep_for(std::chrono::milliseconds(_period));
	}

	DEBUG_LOG("CPeriodicExecutionThread" << QString::fromStdString(_threadName) << "finished and exiting");
}
