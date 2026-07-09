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

static int64_t elapsedMs(std::chrono::steady_clock::time_point start)
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

// The two [stealing] tests document required scheduling behavior the pool does not have yet: a task queued to a
// busy lane must be picked up by an idle worker instead of waiting for the lane's owner. Expected to FAIL until
// work stealing lands. [!mayfail] reports the failures without failing the run - remove it together with the
// stealing implementation so they become enforcing.
// The rdtsc-hash lane placement cannot be targeted from a test, so both tests are statistical; the task counts
// are chosen to make the pass/fail outcome near-certain (collision probabilities in the comments).

TEST_CASE("N sleep tasks on N threads run concurrently", "[threadpool][stealing][!mayfail]")
{
	// 8 tasks on 8 lanes collide (some lane gets two) in ~99.8% of rounds, serializing two 100 ms sleeps on one
	// worker while at least one other worker idles: a round then takes >= 200 ms. With stealing, every round
	// must take ~100 ms regardless of placement. Sleep-based, so core count does not matter.
	CWorkerThreadPool pool(8, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	pool.waitUntilStarted();

	for (int round = 0; round < 3; ++round)
	{
		std::vector<std::future<void>> futures;
		const auto start = std::chrono::steady_clock::now();
		for (int i = 0; i < 8; ++i)
			futures.push_back(pool.enqueueWithFuture([] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); }));
		for (auto& f : futures)
			f.get();

		const auto wallMs = elapsedMs(start);
		INFO("Round " << round << " took " << wallMs << " ms");
		CHECK(wallMs < 175);
	}
}

TEST_CASE("Tasks queued behind a busy worker are picked up promptly", "[threadpool][stealing][!mayfail]")
{
	// A long task occupies its lane's owner; of the 32 trivial tasks, ~8 hash onto that lane (all 32 missing it:
	// ~0.01%) and currently wait out the blocker while three workers idle. Required: idle workers take them over.
	std::atomic_bool blockerStarted{ false };
	CWorkerThreadPool pool(4, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	pool.waitUntilStarted();

	pool.enqueue([&blockerStarted] {
		blockerStarted = true;
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	});
	while (!blockerStarted)
		std::this_thread::yield();

	std::vector<std::future<void>> futures;
	const auto start = std::chrono::steady_clock::now();
	for (int i = 0; i < 32; ++i)
		futures.push_back(pool.enqueueWithFuture([] {}));
	for (auto& f : futures)
		f.get();

	CHECK(elapsedMs(start) < 250);
}

// The tests below pass today and pin behavior the stealing refactor must preserve.

TEST_CASE("Shutdown of an idle pool is prompt", "[threadpool]")
{
	// Parked workers sit in a 5000 ms wait; stop() must wake them via the notify, not the timeout
	// (guards the mutex-held wakeAllThreads and any future changes to the wait predicate).
	CWorkerThreadPool pool(4, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	pool.waitUntilStarted();
	std::this_thread::sleep_for(std::chrono::milliseconds(50)); // let the workers reach the parked wait

	const auto start = std::chrono::steady_clock::now();
	pool.finishAllThreads();
	CHECK(elapsedMs(start) < 1000);
}

TEST_CASE("retire()", "[threadpool]")
{
	// A single-thread pool has one lane, so enqueue order is execution order - the only way to deterministically
	// place tasks behind a running one.
	SECTION("Removes queued tasks with the tag, keeps others")
	{
		std::atomic_bool blockerStarted{ false };
		std::atomic_int taggedRun{ 0 }, untaggedRun{ 0 };
		CWorkerThreadPool pool(1, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));

		pool.enqueue([&blockerStarted] {
			blockerStarted = true;
			std::this_thread::sleep_for(std::chrono::milliseconds(300));
		});
		while (!blockerStarted)
			std::this_thread::yield();

		static constexpr uint64_t tag = 7;
		for (int i = 0; i < 5; ++i)
			pool.enqueue([&taggedRun] { ++taggedRun; }, tag);
		std::vector<std::future<void>> untaggedFutures;
		for (int i = 0; i < 3; ++i)
			untaggedFutures.push_back(pool.enqueueWithFuture([&untaggedRun] { ++untaggedRun; }));

		pool.retire(tag);
		// The contract: after retire() returns, no tagged task is running or queued. It does NOT promise none ran:
		// when the blocker finishes, the worker's next shared-lock acquisition can beat retire()'s exclusive one
		// and execute some tagged tasks first - retire() then waits them out. So assert stability, not zero.
		const int taggedAtReturn = taggedRun;

		for (auto& f : untaggedFutures)
			f.get();
		REQUIRE(untaggedRun == 3);
		REQUIRE(taggedRun == taggedAtReturn);
	}

	SECTION("Waits out an in-flight task with the tag")
	{
		std::atomic_bool taskStarted{ false }, taskFinished{ false };
		CWorkerThreadPool pool(1, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));

		static constexpr uint64_t tag = 42;
		pool.enqueue([&taskStarted, &taskFinished] {
			taskStarted = true;
			std::this_thread::sleep_for(std::chrono::milliseconds(300));
			taskFinished = true;
		}, tag);
		while (!taskStarted)
			std::this_thread::yield();

		pool.retire(tag);
		REQUIRE(taskFinished == true);
	}
}

TEST_CASE("finishAllThreads(true) completes the queued backlog", "[threadpool]")
{
	std::atomic_int counter{ 0 };
	CWorkerThreadPool pool(4, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	for (int i = 0; i < 200; ++i)
		pool.enqueue([&counter] { ++counter; });

	pool.finishAllThreads(true);
	REQUIRE(counter == 200);
}
