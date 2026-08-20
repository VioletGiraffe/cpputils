#include "3rdparty/catch2/catch.hpp"

#include "threading/cinterruptablethread.h"

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

namespace {

// Generous upper bound for a worker to reach its "started" signal; a timeout here means the payload never ran.
constexpr auto startTimeout = std::chrono::seconds(5);

// Runs on the worker thread until cancellation is observed. No Catch macros here - those are not safe off the main test thread.
void spinUntilCancellationRequested(const std::atomic<bool>& cancellationRequested)
{
	while (!cancellationRequested.load())
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

}

TEST_CASE("Fresh instance is idle, uncancelled, and join() is a no-op", "[interruptiblethread]")
{
	CInterruptableThread thread("Test thread");

	REQUIRE(!thread.joinable());
	REQUIRE(!thread.cancellationRequested());

	thread.join(); // Idle: must return immediately without touching a thread
	REQUIRE(!thread.joinable());
}

TEST_CASE("Payload runs once to completion when never cancelled", "[interruptiblethread]")
{
	CInterruptableThread thread("Test thread");
	std::atomic<bool> flagAtEntry{ true }; // Seed with the opposite of the expected value so a missing write can't pass
	std::atomic<int> runCount{ 0 };

	thread.start([&flagAtEntry, &runCount](const std::atomic<bool>& cancellationRequested) {
		flagAtEntry = cancellationRequested.load();
		++runCount;
	});
	thread.join();

	REQUIRE(flagAtEntry.load() == false);
	REQUIRE(runCount.load() == 1);
	REQUIRE(!thread.joinable());
	REQUIRE(!thread.cancellationRequested());

	thread.join(); // Idle again after a completed run: no-op
	REQUIRE(runCount.load() == 1);
}

TEST_CASE("requestCancellation is non-blocking and visible to the running payload", "[interruptiblethread]")
{
	CInterruptableThread thread("Test thread");
	std::promise<void> started;
	auto startedFuture = started.get_future();
	std::atomic<bool> sawCancellation{ false };

	thread.start([&started, &sawCancellation](const std::atomic<bool>& cancellationRequested) {
		started.set_value();
		spinUntilCancellationRequested(cancellationRequested);
		sawCancellation = true;
	});

	// Proves the request below is issued while the payload is genuinely running.
	REQUIRE(startedFuture.wait_for(startTimeout) == std::future_status::ready);

	thread.requestCancellation();
	REQUIRE(thread.cancellationRequested()); // Already true before any join - the non-blocking part

	thread.join();
	REQUIRE(sawCancellation.load());
}

TEST_CASE("Repeated concurrent cancellation is idempotent", "[interruptiblethread]")
{
	CInterruptableThread thread("Test thread");
	std::promise<void> started;
	auto startedFuture = started.get_future();
	std::atomic<int> runCount{ 0 };

	thread.start([&started, &runCount](const std::atomic<bool>& cancellationRequested) {
		started.set_value();
		spinUntilCancellationRequested(cancellationRequested);
		++runCount; // On exit: witnesses the payload body ran and returned exactly once
	});

	REQUIRE(startedFuture.wait_for(startTimeout) == std::future_status::ready);

	thread.requestCancellation();
	thread.requestCancellation();
	thread.requestCancellation();

	std::thread helper([&thread] {
		for (int i = 0; i < 100; ++i)
			thread.requestCancellation();
	});
	helper.join();

	REQUIRE(thread.cancellationRequested());
	thread.join();
	REQUIRE(runCount.load() == 1);
}

TEST_CASE("start() resets the cancellation flag and the instance is reusable after join", "[interruptiblethread]")
{
	CInterruptableThread thread("Test thread");

	// Run 1: cancel the payload to make it exit.
	{
		std::promise<void> started;
		auto startedFuture = started.get_future();

		thread.start([&started](const std::atomic<bool>& cancellationRequested) {
			started.set_value();
			spinUntilCancellationRequested(cancellationRequested);
		});

		REQUIRE(startedFuture.wait_for(startTimeout) == std::future_status::ready);
		thread.requestCancellation();
		thread.join();
	}

	REQUIRE(thread.cancellationRequested()); // The flag persists after join, until the next start()

	// Run 2 on the same instance: start() must clear the flag before the payload sees it.
	{
		std::atomic<bool> flagAtEntry{ true };
		std::atomic<int> runCount{ 0 };

		thread.start([&flagAtEntry, &runCount](const std::atomic<bool>& cancellationRequested) {
			flagAtEntry = cancellationRequested.load();
			++runCount;
		});
		thread.join();

		REQUIRE(flagAtEntry.load() == false); // Reset by start(), despite run 1 having left it true
		REQUIRE(runCount.load() == 1);        // Run 2 executed - reuse across the join barrier works
	}
}

TEST_CASE("Destructor requests cancellation and joins the running payload", "[interruptiblethread]")
{
	// Declared outside the scope: the stores below must be observable after the destructor has run.
	std::atomic<bool> sawCancellation{ false };
	std::atomic<bool> payloadReturned{ false };

	{
		CInterruptableThread thread("Test thread");
		std::promise<void> started;
		auto startedFuture = started.get_future();

		thread.start([&started, &sawCancellation, &payloadReturned](const std::atomic<bool>& cancellationRequested) {
			started.set_value();
			spinUntilCancellationRequested(cancellationRequested);
			sawCancellation = true;
			payloadReturned = true; // Ordered last so its visibility below implies the whole payload finished
		});

		REQUIRE(startedFuture.wait_for(startTimeout) == std::future_status::ready);
		// Leaving the scope invokes ~CInterruptableThread(), which must request cancellation and join.
	}

	// Both stores happened-before the destructor returned - proving request-plus-join, not detach or a flagless join.
	REQUIRE(sawCancellation.load());
	REQUIRE(payloadReturned.load());
}

TEST_CASE("start() forwards a move-only payload and runs it exactly once", "[interruptiblethread]")
{
	CInterruptableThread thread("Test thread");
	std::atomic<int> observedValue{ 0 };
	std::atomic<int> runCount{ 0 };

	auto payloadState = std::make_unique<int>(42); // Move-only capture makes the whole payload move-only
	thread.start([state = std::move(payloadState), &observedValue, &runCount](const std::atomic<bool>&) {
		observedValue = *state;
		++runCount;
	});
	thread.join();

	REQUIRE(observedValue.load() == 42);
	REQUIRE(runCount.load() == 1);
}
