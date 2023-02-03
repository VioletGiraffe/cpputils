#include "3rdparty/catch2/catch.hpp"

#include "utility/macro_utils.h"
#include "threading/cworkerthread.h"

#include <array>

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

static void bench(const size_t nThreads)
{
	static constexpr size_t nWorkloadItems = 31;
	std::array <std::atomic_uint32_t, nWorkloadItems> workloadItems;

	CWorkerThreadPool pool(nThreads, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	pool.waitUntilStarted();

	static constexpr size_t N = 100'000;

	const auto countResult = [&workloadItems] {
		return std::accumulate(workloadItems.begin(), workloadItems.end(), 0);
	};

	const auto waitForCompletion = [&pool, &countResult] {
		while (pool.queueLength() > 0);
		for (int i = 0; i < 100; ++i)
		{
			if (auto n = countResult(); n != N)
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			else
				break;
		}
	};

	BENCHMARK("No future") {
		std::fill(workloadItems.begin(), workloadItems.end(), 0);
		for (size_t i = 0; i < N; ++i)
			pool.enqueue([&workloadItems, i{ i * 4999 }] { ++workloadItems[i % nWorkloadItems]; });

		waitForCompletion();

		REQUIRE(countResult() == N);
		return pool.queueLength();
	};

	std::vector<std::future<void>> futures;
	futures.reserve(N * 105); // Catch2 usually does 100 bench runs, and the following test cannot clear the vector

	BENCHMARK("With future - no wait") {
		std::fill(workloadItems.begin(), workloadItems.end(), 0);
		for (size_t i = 0; i < N; ++i)
		{
			futures.push_back(pool.enqueueWithFuture([&workloadItems, i{ i * 4999 }] { ++workloadItems[i % nWorkloadItems]; }));
		}

		while (pool.queueLength() > 0);
		return pool.queueLength();
	};

	futures.clear();
	REQUIRE(countResult() == N);
	REQUIRE(pool.queueLength() == 0);

	BENCHMARK("With future - waiting") {
		std::fill(workloadItems.begin(), workloadItems.end(), 0);
		for (size_t i = 0; i < N; ++i)
		{
			futures.push_back(pool.enqueueWithFuture([&workloadItems, i{ i * 4999 }] { ++workloadItems[i % nWorkloadItems]; }));
		}

		for (auto& f : futures)
			f.get();

		futures.clear();
		REQUIRE(countResult() == N);
		REQUIRE(pool.queueLength() == 0);
		return pool.queueLength();
	};
}

TEST_CASE("Benchmark - single thread", "[threadpool][benchmark]")
{
	bench(1);
}

TEST_CASE("Benchmark - multi thread", "[threadpool][benchmark]")
{
	const auto nThreads = std::max(2u, std::thread::hardware_concurrency() - 1);
	::printf("Hardware concurrency: %d\n", nThreads);
	bench(nThreads);
}

TEST_CASE("Benchmark - hyper thread", "[threadpool][benchmark]")
{
	const auto nThreads = 4 * std::thread::hardware_concurrency();
	::printf("Threads: %d\n", nThreads);
	bench(nThreads);
}
