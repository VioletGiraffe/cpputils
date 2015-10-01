#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

class CInterruptableThread
{
public:
	enum ExecBehavior {
		InterruptIfRunning,
		SkipIfRunning
	};

	explicit CInterruptableThread(const std::string& threadName, ExecBehavior behavior = InterruptIfRunning);
	~CInterruptableThread();

	bool exec(const std::function<void()>& executable);
	// Signals the thread to stop and waits until the thread has exited via join()
	void interrupt();
	bool running() const;

	const std::atomic<bool>& terminationFlag() const;

private:
	std::string _threadName;
	const ExecBehavior _behavior;
	std::thread _thread;
	std::atomic<bool> _running {false};
	std::atomic<bool> _terminate {false};
};

