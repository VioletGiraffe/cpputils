#pragma once

#include <atomic>
#include <thread>

class SimpleThread
{
public:
	inline SimpleThread() = default;

	template <class... Args>
	inline explicit SimpleThread(Args&&... args) noexcept :
		_th{ std::forward<Args>(args)... }
	{}

	inline ~SimpleThread() noexcept {
		stop(true);
	}

	template <class... Args>
	inline void start(Args&&... args) noexcept {
		_th = std::thread{ std::forward<Args>(args)... };
	}

	inline bool terminationRequested() noexcept {
		return _terminate;
	}

	inline void stop(bool join = false) noexcept {
		_terminate = true;
		if (join && _th.joinable())
			_th.join();
	}

	inline bool isRunning() const noexcept {
		return _th.joinable();
	}

private:
	std::thread _th;
	std::atomic_bool _terminate = false;
};
