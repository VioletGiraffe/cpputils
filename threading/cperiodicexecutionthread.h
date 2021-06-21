#pragma once

#include "lang/type_traits_fast.hpp"

#include <atomic>
#include <functional>
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

private:
	void threadFunc(uint32_t delayBeforeStartMs);

private:
	std::thread            _thread;
	std::function<void ()> _workload;
	std::string            _threadName;
	std::atomic<bool>      _terminate {false};
	const uint32_t         _period = uint32_max; // milliseconds
};

