#pragma once

#include "../lang/type_traits_fast.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

template <typename T>
class CConsumerBlockingQueue
{
public:
	CConsumerBlockingQueue<T>& operator=(const CConsumerBlockingQueue<T>&) = delete;
	CConsumerBlockingQueue(const CConsumerBlockingQueue<T>&) = delete;

	explicit CConsumerBlockingQueue(size_t maxSize = static_cast<size_t>(int32_max));
	~CConsumerBlockingQueue() noexcept = default;

	struct QueuePushResult {
		bool pushed; // True on success, false if maxSize is reached
		size_t queueSize; // The current queue size after the operation
	};
	// Non-blocking
	template <typename U>
	QueuePushResult try_push(U &&item);

	// Blocking; returns the size of the queue after pushing into it
	template <typename U>
	size_t push(U &&item);

	// Non-blocking
	bool try_pop(T& item);
	// Blocking
	bool pop(T& receiver, uint32_t timeout_ms = uint32_max);

	// This method is needed for shutdown - to wake up all the threads that wait on this queue
	void wakeAllThreads();

	size_t size() const;
	bool empty() const;

private:
	const size_t              _maxSize;
	mutable std::mutex        _mutex;
	std::condition_variable   _cond;
	std::deque<T>             _queue;
};

// This method is needed for shutdown - to wake up all the threads that wait on this queue
template <typename T>
void CConsumerBlockingQueue<T>::wakeAllThreads()
{
	_cond.notify_all();
}

template <typename T>
CConsumerBlockingQueue<T>::CConsumerBlockingQueue(size_t maxSize) : _maxSize(maxSize)
{
}

template <typename T>
size_t CConsumerBlockingQueue<T>::size() const
{
	std::lock_guard <std::mutex> locker(_mutex);
	return _queue.size();
}

template <typename T>
bool CConsumerBlockingQueue<T>::empty() const
{
	std::lock_guard <std::mutex> locker(_mutex);
	return _queue.empty();
}

// Non-blocking access
template <typename T>
bool CConsumerBlockingQueue<T>::try_pop(T& item)
{
	std::lock_guard<std::mutex> locker(_mutex);
	if (_queue.empty())
		return false;

	item = std::move(_queue.front());
	_queue.pop_front();
	return true;
}

// Blocking access
template <typename T>
bool CConsumerBlockingQueue<T>::pop(T& receiver, const uint32_t timeout_ms)
{
	std::unique_lock<std::mutex> lock(_mutex);
	if (_queue.empty())
		_cond.wait_for(lock, std::chrono::milliseconds(timeout_ms));

	if (!_queue.empty())
	{
		receiver = std::move(_queue.front());
		_queue.pop_front();
		return true;
	}

	return false;
}

template<typename T>
template<typename U>
typename CConsumerBlockingQueue<T>::QueuePushResult CConsumerBlockingQueue<T>::try_push(U &&item)
{
	std::lock_guard<std::mutex> lock(_mutex);
	const auto queueSize = _queue.size();
	if (queueSize >= _maxSize) // No more space in the queue
		return { false, queueSize };

	_queue.emplace_back(std::forward<U>(item));
	_cond.notify_one();
	return { true, queueSize + 1 };
}

template<typename T>
template<typename U>
size_t CConsumerBlockingQueue<T>::push(U &&item)
{
	std::unique_lock lck{_mutex};
	std::size_t queueSize = 0;
	while ((queueSize = _queue.size()) >= _maxSize) // No more space in the queue
	{
		lck.unlock();
		std::this_thread::yield();
		lck.lock();
	}

	_queue.emplace_back(std::forward<U>(item));
	_cond.notify_one();
	return queueSize + 1;
}
