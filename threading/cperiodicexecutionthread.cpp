#include "cperiodicexecutionthread.h"
#include "compiler/compiler_warnings_control.h"

#ifdef QT_VERSION
	DISABLE_COMPILER_WARNINGS
	#include <QDebug>
	RESTORE_COMPILER_WARNINGS
	#define DEBUG_LOG(X) qDebug() << X
#else
	#define DEBUG_LOG(X)
#endif


#include <assert.h>

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
		assert(!"The thread has already started");
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
		assert(!"The thread has already started");
}

void CPeriodicExecutionThread::terminate()
{
	try
	{
		if (_thread.joinable())
		{
			_terminate = true;
			_thread.join();
			_terminate = false;
		}
	}
	catch (const std::exception& e)
	{
		_terminate = false;
		DEBUG_LOG(__FUNCTION__ << "exception caught:" << e.what());
		(void)e;
	}
}

void CPeriodicExecutionThread::threadFunc()
{
	if (!_workload)
		return;

	DEBUG_LOG("Starting CPeriodicExecutionThread" << QString::fromStdString(_threadName));

	while (!_terminate) // Main threadFunc loop
	{
		_workload();

		std::this_thread::sleep_for(std::chrono::milliseconds(_period));
	}

	DEBUG_LOG("CPeriodicExecutionThread" << QString::fromStdString(_threadName) << "finished and exiting");
}
