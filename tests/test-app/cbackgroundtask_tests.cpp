#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "threading/cbackgroundtask.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <thread>
#include <utility>

using namespace std::chrono_literals;

namespace {

// Polls the task as its owners do. A timeout here means the payload never returned.
template <typename Result>
[[nodiscard]] bool finishesInTime(CBackgroundTask<Result>& task)
{
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (!task.finished())
	{
		if (std::chrono::steady_clock::now() > deadline)
			return false;
		std::this_thread::yield();
	}
	return true;
}

// Runs on the task's thread. No Catch macros here: they are not safe off the main test thread.
void spinUntilCancelled(const std::atomic<bool>& cancelled)
{
	while (!cancelled)
		std::this_thread::yield();
}

// Busy: sleep_for rounds sub-millisecond delays up to the OS timer tick
void spinFor(const std::chrono::microseconds duration)
{
	const auto end = std::chrono::steady_clock::now() + duration;
	while (std::chrono::steady_clock::now() < end) {}
}

// Copying it throws, so start() fails before creating the thread
struct PayloadThatFailsToCopy
{
	explicit PayloadThatFailsToCopy(std::atomic<bool>& invoked) : invoked{ &invoked } {}
	PayloadThatFailsToCopy(const PayloadThatFailsToCopy&) { throw std::runtime_error{ "Expected by the test" }; }
	PayloadThatFailsToCopy(PayloadThatFailsToCopy&&) noexcept = default;

	std::optional<int> operator()(const std::atomic<bool>&)
	{
		*invoked = true;
		return 1;
	}

	std::atomic<bool>* invoked = nullptr;
};

struct MoveOnlyValue
{
	explicit MoveOnlyValue(int v) : value{ v } {}
	MoveOnlyValue(MoveOnlyValue&&) noexcept = default;
	MoveOnlyValue& operator=(MoveOnlyValue&&) noexcept = default;

	int value;
};

}

TEST_CASE("CBackgroundTask: the payload's result is available once finished() reports it", "[backgroundtask]")
{
	CBackgroundTask<int> task{ "Test task" };
	task.start([](const std::atomic<bool>&) { return std::optional<int>{ 42 }; });

	REQUIRE(finishesInTime(task));
	CHECK(task.result() == 42);
}

TEST_CASE("CBackgroundTask: cancel() reaches the payload, which leaves no result", "[backgroundtask]")
{
	CBackgroundTask<int> task{ "Test task" };
	task.start([](const std::atomic<bool>& cancelled) -> std::optional<int> {
		spinUntilCancelled(cancelled);
		return std::nullopt;
	});

	CHECK_FALSE(task.finished());
	task.cancel();
	REQUIRE(finishesInTime(task));
	CHECK_FALSE(task.result().has_value());
}

TEST_CASE("CBackgroundTask: a payload that throws still finishes, with no result", "[backgroundtask]")
{
	CBackgroundTask<int> task{ "Test task" };
	task.start([](const std::atomic<bool>&) -> std::optional<int> { throw std::runtime_error{ "Expected by the test" }; });

	REQUIRE(finishesInTime(task));
	CHECK_FALSE(task.result().has_value());
}

TEST_CASE("CBackgroundTask: cancelAndWait() returns once the payload has, which leaves no result", "[backgroundtask]")
{
	std::atomic<bool> payloadReturned = false;
	CBackgroundTask<int> task{ "Test task" };
	task.start([&payloadReturned](const std::atomic<bool>& cancelled) -> std::optional<int> {
		spinUntilCancelled(cancelled);
		payloadReturned = true;
		return std::nullopt;
	});

	task.cancelAndWait();
	CHECK(payloadReturned);
	CHECK(task.finished());
	CHECK_FALSE(task.result().has_value());
}

TEST_CASE("CBackgroundTask: destruction cancels and joins a running payload", "[backgroundtask]")
{
	std::atomic<bool> payloadReturned = false;
	{
		CBackgroundTask<int> task{ "Test task" };
		task.start([&payloadReturned](const std::atomic<bool>& cancelled) -> std::optional<int> {
			spinUntilCancelled(cancelled);
			payloadReturned = true;
			return std::nullopt;
		});
	}

	CHECK(payloadReturned);
}

TEST_CASE("CBackgroundTask: finished() stays true once it has reported it", "[backgroundtask]")
{
	CBackgroundTask<int> task{ "Test task" };
	task.start([](const std::atomic<bool>&) { return std::optional<int>{ 42 }; });

	REQUIRE(finishesInTime(task));
	CHECK(task.finished());
	CHECK(task.finished());
	CHECK(task.result() == 42);
}

TEST_CASE("CBackgroundTask: a thread that fails to start finishes the task at once, with no result", "[backgroundtask]")
{
	std::atomic<bool> invoked = false;
	const PayloadThatFailsToCopy payload{ invoked };

	CBackgroundTask<int> task{ "Test task" };
	task.start(payload);

	CHECK(task.finished());
	CHECK_FALSE(task.result().has_value());
	CHECK_FALSE(invoked);
}

TEST_CASE("CBackgroundTask: a cancel after the payload has returned keeps its result", "[backgroundtask]")
{
	CBackgroundTask<int> task{ "Test task" };
	task.start([](const std::atomic<bool>&) { return std::optional<int>{ 42 }; });
	REQUIRE(finishesInTime(task));

	task.cancel();
	task.cancelAndWait();
	CHECK(task.finished());
	CHECK(task.result() == 42);
}

TEST_CASE("CBackgroundTask: a cancelled payload's value is the result", "[backgroundtask]")
{
	CBackgroundTask<int> task{ "Test task" };
	task.start([](const std::atomic<bool>& cancelled) -> std::optional<int> {
		spinUntilCancelled(cancelled);
		return 7;
	});

	task.cancel();
	REQUIRE(finishesInTime(task));
	CHECK(task.result() == 7);
}

TEST_CASE("CBackgroundTask: a cancellation requested before start() does not reach the payload", "[backgroundtask]")
{
	CBackgroundTask<int> task{ "Test task" };
	SECTION("cancel()") { task.cancel(); }
	SECTION("cancelAndWait()") { task.cancelAndWait(); }

	std::atomic<bool> cancelledOnEntry = true;
	task.start([&cancelledOnEntry](const std::atomic<bool>& cancelled) {
		cancelledOnEntry = cancelled.load();
		return std::optional<int>{ 1 };
	});

	REQUIRE(finishesInTime(task));
	CHECK_FALSE(cancelledOnEntry);
	CHECK(task.result() == 1);
}

TEST_CASE("CBackgroundTask: a task never started is not finished, and neither cancelAndWait() nor destruction waits", "[backgroundtask]")
{
	CBackgroundTask<int> task{ "Test task" };
	CHECK_FALSE(task.finished());

	task.cancelAndWait();
	CHECK_FALSE(task.finished());
}

TEST_CASE("CBackgroundTask: a move-only result type without a default constructor, from a move-only mutable payload", "[backgroundtask]")
{
	CBackgroundTask<MoveOnlyValue> task{ "Test task" };
	task.start([value = std::make_unique<int>(5)](const std::atomic<bool>&) mutable -> std::optional<MoveOnlyValue> {
		const std::unique_ptr<int> taken = std::move(value);
		return MoveOnlyValue{ *taken };
	});

	REQUIRE(finishesInTime(task));
	REQUIRE(task.result().has_value());
	CHECK(task.result()->value == 5);
}

TEST_CASE("CBackgroundTask: the payload's captures are released once finished() reports it", "[backgroundtask]")
{
	const auto resource = std::make_shared<int>(3);

	CBackgroundTask<int> task{ "Test task" };
	task.start([resource](const std::atomic<bool>&) { return std::optional<int>{ *resource }; });

	REQUIRE(finishesInTime(task));
	CHECK(resource.use_count() == 1);
	CHECK(task.result() == 3);
}

TEST_CASE("CBackgroundTask: under cancellation racing the payload, every task finishes with what its payload returned", "[backgroundtask]")
{
	enum class Wait { Poll, CancelAndWait, Destruction };

	const auto seed = std::random_device{}();
	CAPTURE(seed);
	std::mt19937 random{ seed };

	for (int i = 0; i < 1000; ++i)
	{
		const bool waitsForCancellation = random() % 2 == 0;
		const auto payloadDelay = std::chrono::microseconds{ random() % 200 };
		const auto cancelDelay = std::chrono::microseconds{ random() % 200 };
		const auto wait = static_cast<Wait>(random() % 3);
		CAPTURE(i, waitsForCancellation, payloadDelay.count(), cancelDelay.count(), static_cast<int>(wait));

		static constexpr int notReturned = -1, returnedNothing = -2;
		std::atomic<int> returned = notReturned;
		{
			CBackgroundTask<int> task{ "Test task" };
			task.start([&returned, i, waitsForCancellation, payloadDelay](const std::atomic<bool>& cancelled) -> std::optional<int> {
				if (waitsForCancellation)
					spinUntilCancelled(cancelled);
				else
					spinFor(payloadDelay);

				const std::optional<int> value = cancelled ? std::nullopt : std::optional<int>{ i };
				returned = value.value_or(returnedNothing);
				return value;
			});

			spinFor(cancelDelay);
			if (wait == Wait::Poll)
			{
				task.cancel();
				REQUIRE(finishesInTime(task));
			}
			else if (wait == Wait::CancelAndWait)
			{
				task.cancelAndWait();
				REQUIRE(task.finished());
			}

			if (wait != Wait::Destruction)
			{
				const int value = returned.load();
				REQUIRE(value != notReturned);
				REQUIRE(task.result() == (value == returnedNothing ? std::nullopt : std::optional<int>{ value }));
			}
		} // Destroying the task cancels and joins it

		REQUIRE(returned != notReturned);
	}
}
