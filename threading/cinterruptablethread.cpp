#include "cinterruptablethread.h"

CInterruptableThread::CInterruptableThread(std::string threadName) :
	_threadName{std::move(threadName)}
{
}

CInterruptableThread::~CInterruptableThread()
{
	requestCancellation();
	join();
}

void CInterruptableThread::requestCancellation() noexcept
{
	_cancellationRequested = true;
}

bool CInterruptableThread::cancellationRequested() const noexcept
{
	return _cancellationRequested;
}

bool CInterruptableThread::joinable() const noexcept
{
	return _thread.joinable();
}

void CInterruptableThread::join()
{
	if (_thread.joinable())
		_thread.join();
}
