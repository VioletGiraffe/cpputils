#include "3rdparty/catch2/catch.hpp"

#include "utility/macro_utils.h"
#include "threading/cworkerthread.h"

TEST_CASE("thread pool construction and destruction", "[threadpool]")
{
try {
	SECTION("Single thread") {
		CWorkerThreadPool pool(1, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	}

	SECTION("Many threads") {
		CWorkerThreadPool pool(std::thread::hardware_concurrency() * 3, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	}
}
catch(...) {
	FAIL();
	return;
}

	SUCCEED();
}

TEST_CASE("Basic functionality", "[threadpool]")
{
	SECTION("Single thread") {
		{
			int a = 0;
			CWorkerThreadPool pool(1, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
			pool.enqueue([&a] {++a;});
			while (pool.queueLength() > 0);
			pool.finishAllThreads();
			REQUIRE(a == 1);
		}

		{
			std::atomic_int a = 0;
			CWorkerThreadPool pool(1, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
			for (int i = 0; i < 100; ++i)
				pool.enqueue([&a] {++a;});

			while (pool.queueLength() > 0);
			pool.finishAllThreads();
			REQUIRE(a == 100);
		}
	}

	SECTION("Many threads") {
		{
			int a = 0;
			CWorkerThreadPool pool(std::thread::hardware_concurrency() * 3, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
			pool.enqueue([&a] {++a; });
			while (pool.queueLength() > 0);
			pool.finishAllThreads();
			REQUIRE(a == 1);
		}

		{
			std::atomic_int a = 0;
			CWorkerThreadPool pool(std::thread::hardware_concurrency() * 3, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
			static constexpr size_t N = 500;
			for (size_t i = 0; i < N; ++i)
				pool.enqueue([&a] {++a; });

			while (pool.queueLength() > 0);
			pool.finishAllThreads();
			REQUIRE(a == N);
		}
	}

	SUCCEED();
}

TEST_CASE("Benchmark - single thread", "[threadpool][benchmark]")
{
	int a = 0;
	CWorkerThreadPool pool(1, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	pool.waitUntilStarted();

	static constexpr size_t N = 100'000;

	BENCHMARK("No future") {
		a = 0;
		for (size_t i = 0; i < N; ++i)
			pool.enqueue([&a] {++a; });

		while (pool.queueLength() > 0);
		REQUIRE(a == N);
		return a + pool.queueLength();
	};

	std::vector<std::future<void>> futures;
	futures.reserve(N * 10);

	BENCHMARK("With future - no wait") {
		a = 0;
		for (size_t i = 0; i < N; ++i)
		{
			futures.push_back(pool.enqueueWithFuture([&a] {++a; }));
		}

		while (pool.queueLength() > 0);
		return a + pool.queueLength();
	};

	futures.clear();
	REQUIRE(pool.queueLength() == 0);
	REQUIRE(a == N);

	BENCHMARK("With future - waiting") {
		a = 0;
		for (size_t i = 0; i < N; ++i)
		{
			futures.push_back(pool.enqueueWithFuture([&a] {++a; }));
		}

		for (auto& f : futures)
			f.get();

		futures.clear();
		REQUIRE(a == N);
		REQUIRE(pool.queueLength() == 0);
		return a + pool.queueLength();
	};
}
