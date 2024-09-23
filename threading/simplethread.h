#pragma once

#include <atomic>
#include <thread>

class SimpleThread
{
public:
	template <class... Args>
	inline explicit SimpleThread(Args&&... args) noexcept :
		_th{ std::forward<Args>(args)... }
	{}

	inline ~SimpleThread() noexcept {
		stop(true);
	}

	inline bool terminationRequested() noexcept {
		return _terminate;
	}

	inline void stop(bool join = false) noexcept {
		_terminate = true;
		if (join && _th.joinable())
			_th.join();
	}

private:
	std::thread _th;
	std::atomic_bool _terminate = false;
};
