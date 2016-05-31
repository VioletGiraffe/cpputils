#include "cworkerthread.h"

CWorkerThreadPool::CWorkerThread::CWorkerThread(CConsumerBlockingQueue<std::function<void ()> >& queue, const std::string& threadName) :
	_queue(queue),
	_threadName(threadName)
{
}

CWorkerThreadPool::CWorkerThread::~CWorkerThread()
{
	stop();
}

void CWorkerThreadPool::CWorkerThread::start()
{
	if (_working)
		return;

	if (_thread.joinable())
		_thread.join();

	_working = true;
	_thread = std::thread(&CWorkerThread::threadFunc, this);
}

void CWorkerThreadPool::CWorkerThread::stop()
{
	_terminate = true;
	if (_thread.joinable())
		_thread.join();
	_terminate = false;
	_working = false;
}

void CWorkerThreadPool::CWorkerThread::threadFunc()
{
	_working = true;
	while (!_terminate)
	{
		std::function<void()> task;
		_queue.pop(task);
		if (task)
			task();
	}
	_working = false;
}


CWorkerThreadPool::CWorkerThreadPool(size_t maxNumThreads, const std::string& poolName) :
	_poolName(poolName),
	_maxNumThreads(maxNumThreads)
{
}

CWorkerThreadPool::~CWorkerThreadPool()
{
	for (auto& thread : _workerThreads)
	{
		// Pushing a dummy item in the task queue so that pop() wakes up and the thread may resume in order to finish
		// TODO: this is unreliable
		for (size_t i = 0; i < _workerThreads.size(); ++i)
			_queue.push([](){
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		});
		thread.stop();
	}
}

void CWorkerThreadPool::enqueue(const std::function<void ()>& task)
{
	_queue.push(task);
	if (_workerThreads.size() < _maxNumThreads && _queue.size() > _workerThreads.size())
	{
		std::stringstream ss;
		ss << _poolName << " worker thread #" << _workerThreads.size() + 1;
		_workerThreads.emplace_back(_queue, ss.str());
		_workerThreads.back().start();
	}
}
