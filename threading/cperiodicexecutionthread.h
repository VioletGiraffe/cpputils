#pragma once

#include "lang/type_traits_fast.hpp"

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class CPeriodicExecutionThread
{
public:
	explicit CPeriodicExecutionThread(unsigned int period_ms, std::string threadName, std::function<void()> workload = {});

	CPeriodicExecutionThread(const CPeriodicExecutionThread&) = delete;
	CPeriodicExecutionThread& operator=(const CPeriodicExecutionThread&) = delete;

	~CPeriodicExecutionThread();

	void setWorkload(std::function<void ()> workload);

	void start(std::function<void ()>&& workload, uint32_t delayBeforeStartMs = 0);

	void terminate();

	// Parks the thread (it stops calling the workload) without tearing it down; resume() picks back up where pause() left off.
	// Unlike terminate(), this never blocks the caller - safe to call while holding any lock the workload itself might take.
	void pause();
	// Unparks the thread; the workload runs again immediately, then back to the regular period.
	void resume();

private:
	void threadFunc(uint32_t delayBeforeStartMs);

private:
	std::thread             _thread;
	std::function<void ()>  _workload;
	std::string             _threadName;
	std::mutex              _cvMutex;          // Guards _terminate and _paused; also condvar's mutex
	std::condition_variable _cv;
	bool                    _terminate = false; // Guarded by _cvMutex
	bool                    _paused = false;    // Guarded by _cvMutex
	const uint32_t          _period = uint32_max; // milliseconds
};

