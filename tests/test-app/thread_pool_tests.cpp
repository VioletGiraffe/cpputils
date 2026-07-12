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

// A single completion counter would itself become the dominant contention point of a nanotask bench;
// 31 slots dilute the increments while still allowing an exact total.
struct BenchWorkload
{
	static constexpr size_t slotCount = 31;
	std::array<std::atomic_uint32_t, slotCount> slots;

	void reset() noexcept { std::fill(slots.begin(), slots.end(), 0); }
	[[nodiscard]] uint32_t total() const noexcept { return std::accumulate(slots.begin(), slots.end(), 0u); }
};

// Not just the queueLength() spin: queueLength() == 0 does not cover popped-but-still-executing tasks,
// and a straggler's increment landing after the next sample's reset() would corrupt that sample's count.
static void waitForCompletion(CWorkerThreadPool& pool, const BenchWorkload& workload, const size_t expectedTotal)
{
	// Yield: queueLength() is a single atomic load, and a full-speed spin on it would keep stealing the
	// cache line every worker must update per pop (and hog a core the oversubscribed benches need)
	while (pool.queueLength() > 0)
		std::this_thread::yield();
	for (int i = 0; i < 100 && workload.total() != expectedTotal; ++i)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

// Busy-work of a controlled duration. A volatile loop rather than sleep: a sleeping worker releases its core,
// understating contention, whereas real tasks compute throughout.
static void spinFor(const uint32_t iterations) noexcept
{
	for (volatile uint32_t i = 0; i < iterations; ++i) {}
}

// Calibrated once per run; the regime benches only need the right order of magnitude
static uint32_t spinIterationsPerMicrosecond()
{
	static const uint32_t iterationsPerUs = [] {
		constexpr uint32_t probeIterations = 4'000'000;
		const auto start = std::chrono::steady_clock::now();
		spinFor(probeIterations);
		const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
		return static_cast<uint32_t>(probeIterations / std::max<int64_t>(elapsedUs, 1));
	}();
	return iterationsPerUs;
}

static void bench(const uint32_t nThreads)
{
	BenchWorkload workload;

	CWorkerThreadPool pool(nThreads, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	pool.waitUntilStarted();

	static constexpr size_t N = 100'000;

	BENCHMARK("No future") {
		workload.reset();
		for (size_t i = 0; i < N; ++i)
			pool.enqueue([&workload, i{ i * 4999 }] { ++workload.slots[i % BenchWorkload::slotCount]; });

		waitForCompletion(pool, workload, N);

		REQUIRE(workload.total() == N);
		return pool.queueLength();
	};

	std::vector<std::future<void>> futures;
	futures.reserve(N * 105); // Catch2 usually does 100 bench runs, and the following test cannot clear the vector

	BENCHMARK("With future - no wait") {
		workload.reset();
		for (size_t i = 0; i < N; ++i)
		{
			futures.push_back(pool.enqueueWithFuture([&workload, i{ i * 4999 }] { ++workload.slots[i % BenchWorkload::slotCount]; }));
		}

		waitForCompletion(pool, workload, N);
		return pool.queueLength();
	};

	futures.clear();
	REQUIRE(workload.total() == N);
	REQUIRE(pool.queueLength() == 0);

	BENCHMARK("With future - waiting") {
		workload.reset();
		for (size_t i = 0; i < N; ++i)
		{
			futures.push_back(pool.enqueueWithFuture([&workload, i{ i * 4999 }] { ++workload.slots[i % BenchWorkload::slotCount]; }));
		}

		for (auto& f : futures)
			f.get();

		futures.clear();
		REQUIRE(workload.total() == N);
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

TEST_CASE("parallelForAsync executes every index exactly once, then completes", "[threadpool][parallelforasync]")
{
	CWorkerThreadPool pool(4, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	pool.waitUntilStarted();

	for (const size_t count : { size_t{ 0 }, size_t{ 1 }, size_t{ 3 }, size_t{ 4 }, size_t{ 100 }, size_t{ 10'000 } })
	{
		std::vector<std::atomic_int> hits(count);
		std::promise<void> donePromise;
		pool.parallelForAsync(count, [&hits](size_t i) { ++hits[i]; }, [&donePromise] { donePromise.set_value(); });
		donePromise.get_future().get(); // count == 0 must complete too - hangs here if it does not

		size_t wrongCount = 0;
		for (const auto& h : hits)
			wrongCount += h != 1 ? 1 : 0;
		INFO("count = " << count);
		REQUIRE(wrongCount == 0);
	}
}

TEST_CASE("parallelForAsync does not block the caller; onAllCompleted runs last, on a worker", "[threadpool][parallelforasync]")
{
	CWorkerThreadPool pool(4, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	pool.waitUntilStarted();

	std::atomic_bool callerReturned{ false };
	std::atomic_int executed{ 0 };
	int executedAtCompletion = -1;
	std::thread::id completionThreadId;
	std::promise<void> donePromise;

	pool.parallelForAsync(64,
		[&callerReturned, &executed](size_t) {
			// Gates every index on the caller having already returned: a blocking dispatch could never finish
			while (!callerReturned)
				std::this_thread::yield();
			++executed;
		},
		[&executed, &executedAtCompletion, &completionThreadId, &donePromise] {
			executedAtCompletion = executed;
			completionThreadId = std::this_thread::get_id();
			donePromise.set_value();
		});
	callerReturned = true;

	donePromise.get_future().get();
	REQUIRE(executedAtCompletion == 64);
	CHECK(completionThreadId != std::this_thread::get_id());
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

// Benchmark regime map - each case isolates a distinct operating regime of the pool:
// - single/multi/hyper thread: ONE producer spamming nanotasks into 1 / hw-1 / 4*hw workers. Supply-limited:
//   workers outpace the producer, park/unpark continuously, and the producer takes the notify path on nearly
//   every enqueue - a stress test of the idle machinery (and, with many workers, of the steal sweep).
// - multi-producer: concurrent enqueue() calls contending on the producer-side shared state.
// - saturated pool: a backlog is always present and workers never park - the enqueue fast path and pure
//   pop+execute throughput, with the idle machinery completely quiet.
// - parallelFor: the fork-join API in fine-grained (dispenser-bound), coarse (Darkroom image-op shape), and
//   repeated-small-batch (dispatch+join overhead) flavors.
// - task latency: one task in flight on an idle pool - the full wake-path round trip, not throughput.

TEST_CASE("Benchmark - single thread", "[threadpool][benchmark]")
{
	bench(1);
}

TEST_CASE("Benchmark - multi thread", "[threadpool][benchmark]")
{
	const auto hw = std::thread::hardware_concurrency();
	const auto nThreads = std::max(2u, hw - 1);
	::printf("Threads: %d, hardware concurrency: %d\n", nThreads, hw);
	bench(nThreads);
}

TEST_CASE("Benchmark - hyper thread", "[threadpool][benchmark]")
{
	const auto nThreads = 4 * std::thread::hardware_concurrency();
	::printf("Threads: %d\n", nThreads);
	bench(nThreads);
}

TEST_CASE("Benchmark - multi-producer", "[threadpool][benchmark]")
{
	const auto hw = std::thread::hardware_concurrency();
	const uint32_t nProducers = std::max(2u, hw / 2);
	const uint32_t nWorkers = std::max(2u, hw / 2); // Producers + workers ~ hw: full contention, no oversubscription noise
	::printf("Producers: %u, workers: %u\n", nProducers, nWorkers);

	BenchWorkload workload;
	CWorkerThreadPool pool(nWorkers, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	pool.waitUntilStarted();

	static constexpr size_t N = 100'000; // Total across all producers - comparable to the single-producer nanotask benches

	BENCHMARK("Nanotasks") {
		workload.reset();
		const size_t perProducer = N / nProducers;
		// Producer threads are spawned per sample; their startup cost (a handful of threads) is negligible
		// against a multi-ms sample
		std::vector<std::thread> producers;
		producers.reserve(nProducers);
		for (uint32_t p = 0; p < nProducers; ++p)
		{
			producers.emplace_back([&pool, &workload, perProducer] {
				for (size_t i = 0; i < perProducer; ++i)
					pool.enqueue([&workload, i{ i * 4999 }] { ++workload.slots[i % BenchWorkload::slotCount]; });
			});
		}
		for (auto& producer : producers)
			producer.join();

		waitForCompletion(pool, workload, perProducer * nProducers);
		REQUIRE(workload.total() == perProducer * nProducers);
		return pool.queueLength();
	};
}

TEST_CASE("Benchmark - saturated pool", "[threadpool][benchmark]")
{
	const uint32_t nWorkers = std::max(2u, std::thread::hardware_concurrency() - 1);
	// Task duration scales with the worker count so that one producer (~5-10 enqueues/us) always outruns the
	// drain rate (nWorkers / duration = 2 tasks/us) regardless of the machine: a backlog builds and workers
	// never park. This is the regime the enqueue fast path is designed for - _idleCount pinned at 0, no
	// notify, the idle machinery completely quiet.
	const uint32_t taskDurationUs = std::max(1u, nWorkers / 2);
	const uint32_t spinIterations = taskDurationUs * spinIterationsPerMicrosecond();
	::printf("Workers: %u, task duration: ~%u us\n", nWorkers, taskDurationUs);

	BenchWorkload workload;
	CWorkerThreadPool pool(nWorkers, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	pool.waitUntilStarted();

	static constexpr size_t N = 50'000;

	BENCHMARK("Microsecond tasks") {
		workload.reset();
		for (size_t i = 0; i < N; ++i)
			pool.enqueue([&workload, spinIterations, i{ i * 4999 }] { spinFor(spinIterations); ++workload.slots[i % BenchWorkload::slotCount]; });

		REQUIRE(pool.queueLength() > 0); // The regime under test: the backlog must outlive the producer, or workers were idling after all

		waitForCompletion(pool, workload, N);
		REQUIRE(workload.total() == N);
		return pool.queueLength();
	};
}

TEST_CASE("Benchmark - parallelFor", "[threadpool][benchmark]")
{
	const uint32_t nWorkers = std::max(2u, std::thread::hardware_concurrency() - 1);
	const uint32_t iterationsPerUs = spinIterationsPerMicrosecond();
	::printf("Workers: %u, spin calibration: %u iterations/us\n", nWorkers, iterationsPerUs);

	BenchWorkload workload;
	CWorkerThreadPool pool(nWorkers, "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	pool.waitUntilStarted();

	// Nano-indices: the shared dispenser (the nextIndex/completedCount fetch_adds) IS the workload
	BENCHMARK("Fine-grained: 100k nano indices") {
		workload.reset();
		pool.parallelFor(100'000, [&workload](size_t i) { ++workload.slots[(i * 4999) % BenchWorkload::slotCount]; });
		REQUIRE(workload.total() == 100'000);
		return workload.total();
	};

	// The Darkroom image-op shape: few indices, heavy work; pool overhead should vanish into the work
	const uint32_t coarseSpinIterations = 200 * iterationsPerUs;
	BENCHMARK("Coarse: 4x workers indices, ~200us each") {
		pool.parallelFor(size_t{ 4 } * nWorkers, [coarseSpinIterations](size_t) { spinFor(coarseSpinIterations); });
		return coarseSpinIterations;
	};

	// Repeated fork-join of the smallest useful batch - dispatch+join overhead paid 500 times per sample
	const uint32_t smallSpinIterations = 5 * iterationsPerUs;
	BENCHMARK("Fork-join x500: count == workers, ~5us each") {
		for (int rep = 0; rep < 500; ++rep)
			pool.parallelFor(nWorkers, [smallSpinIterations](size_t) { spinFor(smallSpinIterations); });
		return smallSpinIterations;
	};
}

TEST_CASE("Benchmark - task latency", "[threadpool][benchmark]")
{
	// Latency, not throughput: one task in flight at a time on an otherwise idle pool. Every iteration pays the
	// full wake path - notify, worker wakes from the condvar, pop, run, promise fulfilled, caller resumes. The
	// interactive shape (a UI action posting one job) that the throughput benches hide entirely.
	CWorkerThreadPool pool(std::max(2u, std::thread::hardware_concurrency() - 1), "Test thread pool " STRINGIFY_ARGUMENT(__LINE__));
	pool.waitUntilStarted();

	BENCHMARK("1000 single-task round trips") {
		for (int i = 0; i < 1000; ++i)
			pool.enqueueWithFuture([] {}).get();
		return pool.queueLength();
	};
}
