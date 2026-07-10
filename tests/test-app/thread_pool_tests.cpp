#include "3rdparty/catch2/catch.hpp"

#include "utility/macro_utils.h"
#include "threading/cworkerthread.h"

#include <array>
#include <sstream>

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
		// Yield: queueLength() is now a single atomic load, and a full-speed spin on it would keep stealing the
		// cache line every worker must update per pop (and hog a core the oversubscribed benches need)
		while (pool.queueLength() > 0)
			std::this_thread::yield();
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

		// Not just the queueLength() spin: queueLength() == 0 does not cover popped-but-still-executing tasks,
		// and a straggler's increment landing after the next iteration's std::fill would corrupt the count
		waitForCompletion();
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

static int64_t elapsedMs(std::chrono::steady_clock::time_point start)
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

// The two [stealing] tests pin the work-stealing behavior: a task queued to a busy lane is picked up by an idle
// worker instead of waiting for the lane's owner. The hashed lane placement cannot be targeted from a test, so
// both tests are statistical; the task counts make the outcome near-certain (probabilities in the comments).

TEST_CASE("N sleep tasks on N threads run concurrently", "[threadpool][stealing]")
{
	// 8 tasks on 8 lanes collide (some lane gets two or more) in ~99.8% of rounds; each collided task must get
	// a helper woken for it, so that every round takes ~100 ms regardless of placement. Sleep-based, so core
	// count does not matter.
	CWorkerThreadPool pool(8, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	pool.waitUntilStarted();

	// Diagnostics for failures: when and on which worker each task ran. One writer per slot, synchronized by
	// the futures, so no atomics needed.
	struct TaskTrace { int64_t startMs = -1; std::thread::id threadId; };

	for (int round = 0; round < 3; ++round)
	{
		std::array<TaskTrace, 8> traces{};
		std::vector<std::future<void>> futures;
		const auto start = std::chrono::steady_clock::now();
		for (int i = 0; i < 8; ++i)
		{
			futures.push_back(pool.enqueueWithFuture([&traces, i, start] {
				traces[i] = { elapsedMs(start), std::this_thread::get_id() };
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}));
		}
		for (auto& f : futures)
			f.get();

		int64_t latestStartMs = 0;
		std::ostringstream diag;
		for (int i = 0; i < 8; ++i)
		{
			latestStartMs = std::max(latestStartMs, traces[i].startMs);
			diag << "task " << i << ": started +" << traces[i].startMs << " ms on thread " << traces[i].threadId << '\n';
		}
		INFO(diag.str());
		INFO("Round " << round << " took " << elapsedMs(start) << " ms");
		// Assert on start times, not wall time: a missed steal shows as a straggler starting at +100 ms or later
		// (once the first worker frees up), while wall time also includes sleep_for overshoot, which is pure noise
		// on a loaded CI runner (observed on a shared macOS runner: the 100 ms of sleeping took 175 ms).
		CHECK(latestStartMs < 75);
	}
}

TEST_CASE("Tasks queued behind a busy worker are picked up promptly", "[threadpool][stealing]")
{
	// A long task occupies its lane's owner; of the 32 trivial tasks, ~8 hash onto that lane (all 32 missing it:
	// ~0.01%); idle workers must take them over instead of letting them wait out the blocker.
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

TEST_CASE("Park/wake churn does not lose wakeups", "[threadpool]")
{
	// Each round drains the pool to idle and then a single task must arrive through the idle-wake gate.
	// 1000 rounds sample both worker states: parked in the condvar wait, and mid-transition around it.
	// One lost wake stalls its round for the full 5000 ms idle timeout - unmissable against this budget.
	CWorkerThreadPool pool(4, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	pool.waitUntilStarted();

	const auto start = std::chrono::steady_clock::now();
	for (int i = 0; i < 1000; ++i)
		pool.enqueueWithFuture([] {}).get();

	// Generous for loaded CI runners (1000 sequential wake round-trips), yet a single lost wake costs +5000 ms
	CHECK(elapsedMs(start) < 4000);
}

TEST_CASE("Concurrent enqueue from multiple producer threads", "[threadpool]")
{
	// enqueue() must be callable from any thread: the lane mutexes, _queuedCount and the idle-wake gate are
	// the shared state under test. The final count proves no task was lost or duplicated.
	std::atomic_int counter{ 0 };
	CWorkerThreadPool pool(4, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	pool.waitUntilStarted();

	static constexpr int nProducers = 8;
	static constexpr int tasksPerProducer = 10'000;

	std::vector<std::thread> producers;
	for (int p = 0; p < nProducers; ++p)
	{
		producers.emplace_back([&pool, &counter] {
			for (int i = 0; i < tasksPerProducer; ++i)
				pool.enqueue([&counter] { ++counter; });
		});
	}
	for (auto& t : producers)
		t.join();

	pool.finishAllThreads(true);
	REQUIRE(counter == nProducers * tasksPerProducer);
}

// Executes one node of a binary task tree: counts itself, spawns two children down to depth 0
static void runFanOutTask(CWorkerThreadPool& pool, std::atomic_int& counter, int depth)
{
	++counter;
	if (depth > 0)
	{
		pool.enqueue([&pool, &counter, depth] { runFanOutTask(pool, counter, depth - 1); });
		pool.enqueue([&pool, &counter, depth] { runFanOutTask(pool, counter, depth - 1); });
	}
}

TEST_CASE("Tasks enqueueing further tasks", "[threadpool]")
{
	// A worker is also a valid producer: mid-task enqueues must wake idle peers and must not deadlock
	// (enqueue takes no pool-wide lock, so enqueueing while the calling worker holds the shared pop+execute
	// lock is safe - this pins that property).
	std::atomic_int counter{ 0 };
	CWorkerThreadPool pool(4, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	pool.waitUntilStarted();

	static constexpr int depth = 12;
	static constexpr int expected = (1 << (depth + 1)) - 1; // node count of the full binary tree
	pool.enqueue([&pool, &counter] { runFanOutTask(pool, counter, depth); });

	const auto start = std::chrono::steady_clock::now();
	while (counter < expected && elapsedMs(start) < 5000)
		std::this_thread::yield();
	REQUIRE(counter == expected);
}

TEST_CASE("parallelFor executes every index exactly once", "[threadpool][parallelfor]")
{
	CWorkerThreadPool pool(4, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	pool.waitUntilStarted();

	for (const size_t count : { size_t{ 0 }, size_t{ 1 }, size_t{ 3 }, size_t{ 4 }, size_t{ 100 }, size_t{ 10'000 } })
	{
		std::vector<std::atomic_int> hits(count);
		pool.parallelFor(count, [&hits](size_t i) { ++hits[i]; });
		size_t wrongCount = 0;
		for (const auto& h : hits)
			wrongCount += h != 1 ? 1 : 0;
		INFO("count = " << count);
		REQUIRE(wrongCount == 0);
	}
}

TEST_CASE("parallelFor runs indices concurrently", "[threadpool][parallelfor]")
{
	// 8 sleeps over 7 workers + the calling thread: concurrent execution takes ~100 ms; any serialization
	// shows up as a multiple of that. Sleep-based, so core count does not matter.
	CWorkerThreadPool pool(7, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	pool.waitUntilStarted();

	const auto start = std::chrono::steady_clock::now();
	pool.parallelFor(8, [](size_t) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });
	CHECK(elapsedMs(start) < 400); // fully sequential would be 800 ms; generous margin for loaded CI runners
}

TEST_CASE("parallelFor with every worker busy completes via the calling thread", "[threadpool][parallelfor]")
{
	// Both workers are held by 500 ms blockers, so nothing but the calling thread can run the indices: it must
	// drain the whole range alone, well before any worker frees up. This pins the caller-participation property
	// that makes parallelFor deadlock-free on a saturated pool.
	std::atomic_int blockersStarted{ 0 };
	CWorkerThreadPool pool(2, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	pool.waitUntilStarted();

	for (int i = 0; i < 2; ++i)
	{
		pool.enqueue([&blockersStarted] {
			++blockersStarted;
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		});
	}
	while (blockersStarted < 2)
		std::this_thread::yield();

	const auto start = std::chrono::steady_clock::now();
	std::atomic_int counter{ 0 };
	pool.parallelFor(64, [&counter](size_t) { ++counter; });
	REQUIRE(counter == 64);
	CHECK(elapsedMs(start) < 250);
}

TEST_CASE("Nested parallelFor from inside pool tasks does not deadlock", "[threadpool][parallelfor]")
{
	// More outer tasks than workers, and every outer task blocks inside a parallelFor of its own - no worker is
	// ever free to help another's batch, so each nested call must complete through its calling (worker) thread.
	std::atomic_int total{ 0 };
	CWorkerThreadPool pool(4, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	pool.waitUntilStarted();

	std::vector<std::future<void>> futures;
	for (int i = 0; i < 8; ++i)
	{
		futures.push_back(pool.enqueueWithFuture([&pool, &total] {
			pool.parallelFor(1000, [&total](size_t) { ++total; });
		}));
	}
	for (auto& f : futures)
		f.get();
	REQUIRE(total == 8000);
}

// The tests below pin behavior that work stealing must not break.

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

TEST_CASE("Destruction with a busy worker and a queued backlog", "[threadpool]")
{
	// One thread makes the outcome deterministic (with more, a not-yet-stopped peer may legitimately run part
	// of the backlog while an earlier worker is being joined). The destructor's stop() flags terminate while
	// the worker is mid-task; once the blocker finishes, the worker must exit its loop abandoning the backlog
	// (the default is finishPendingTasks == false) - promptly, not via the idle timeout.
	std::atomic_bool blockerStarted{ false };
	std::atomic_int backlogRun{ 0 };
	std::chrono::steady_clock::time_point destructionStart;
	{
		CWorkerThreadPool pool(1, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
		pool.enqueue([&blockerStarted] {
			blockerStarted = true;
			std::this_thread::sleep_for(std::chrono::milliseconds(300));
		});
		while (!blockerStarted)
			std::this_thread::yield();

		for (int i = 0; i < 50; ++i)
			pool.enqueue([&backlogRun] { ++backlogRun; });
		destructionStart = std::chrono::steady_clock::now();
	}
	CHECK(elapsedMs(destructionStart) < 2000); // ~300 ms of blocker remainder, never the 5000 ms idle timeout
	REQUIRE(backlogRun == 0);
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

TEST_CASE("retire() on a busy multi-thread pool", "[threadpool]")
{
	// The single-thread retire() tests pin the contract deterministically; here retire()'s exclusive lock must
	// wedge itself between 4 workers continuously holding the shared pop+execute lock. The contract half under
	// test: no tagged task may run after retire() returns - each tagged task checks the flag that is set right
	// at that point. (Tagged tasks running shortly BEFORE the return are allowed, see the single-thread test.)
	std::atomic_bool retired{ false };
	std::atomic_int taggedRunAfterRetire{ 0 };
	CWorkerThreadPool pool(4, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	pool.waitUntilStarted();

	static constexpr uint64_t tag = 13;
	std::vector<std::future<void>> untaggedFutures;
	for (int i = 0; i < 200; ++i)
	{
		pool.enqueue([&retired, &taggedRunAfterRetire] {
			if (retired)
				++taggedRunAfterRetire;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}, tag);
		untaggedFutures.push_back(pool.enqueueWithFuture([] { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }));
	}

	pool.retire(tag); // Lands while the backlog is deep and all 4 workers are mid-task
	retired = true;

	for (auto& f : untaggedFutures)
		f.get();
	REQUIRE(taggedRunAfterRetire == 0);
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
