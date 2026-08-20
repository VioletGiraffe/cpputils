#include "3rdparty/catch2/catch.hpp"

#include "threading/cexecutionqueue.h"

#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

TEST_CASE("Execution queue runs untagged tasks in FIFO order", "[executionqueue]")
{
	CExecutionQueue queue;
	std::vector<int> executionOrder;

	queue.enqueue([&executionOrder] { executionOrder.push_back(1); });
	queue.enqueue([&executionOrder] { executionOrder.push_back(2); });
	queue.enqueue([&executionOrder] { executionOrder.push_back(3); });
	queue.exec();

	const std::vector<int> expectedOrder{ 1, 2, 3 };
	REQUIRE(executionOrder == expectedOrder);
}

TEST_CASE("Replacing a tagged task moves it to the back", "[executionqueue]")
{
	CExecutionQueue queue;
	std::vector<int> executionOrder;

	queue.enqueue([&executionOrder] { executionOrder.push_back(1); }, 7);
	queue.enqueue([&executionOrder] { executionOrder.push_back(2); });
	queue.enqueue([&executionOrder] { executionOrder.push_back(3); }, 7);
	queue.exec();

	const std::vector<int> expectedOrder{ 2, 3 };
	REQUIRE(executionOrder == expectedOrder);
}

TEST_CASE("Execution queue can execute one queued task at a time", "[executionqueue]")
{
	CExecutionQueue queue;
	int executedTaskCount = 0;

	queue.enqueue([&executedTaskCount] { ++executedTaskCount; });
	queue.enqueue([&executedTaskCount] { ++executedTaskCount; });
	queue.exec(CExecutionQueue::execFirst);
	REQUIRE(executedTaskCount == 1);

	queue.exec(CExecutionQueue::execFirst);
	REQUIRE(executedTaskCount == 2);
}

TEST_CASE("Execution queue defers tasks added while executing the current batch", "[executionqueue]")
{
	CExecutionQueue queue;
	std::vector<int> executionOrder;

	queue.enqueue([&queue, &executionOrder]
	{
		executionOrder.push_back(1);
		queue.enqueue([&executionOrder] { executionOrder.push_back(3); });
	});
	queue.enqueue([&executionOrder] { executionOrder.push_back(2); });

	queue.exec();
	const std::vector<int> firstBatchExpectedOrder{ 1, 2 };
	REQUIRE(executionOrder == firstBatchExpectedOrder);

	queue.exec();
	const std::vector<int> finalExpectedOrder{ 1, 2, 3 };
	REQUIRE(executionOrder == finalExpectedOrder);
}

TEST_CASE("Execution queue preserves tagged replacement during a batch", "[executionqueue]")
{
	CExecutionQueue queue;
	std::vector<int> executionOrder;

	queue.enqueue([&queue, &executionOrder]
	{
		executionOrder.push_back(1);
		queue.enqueue([&executionOrder] { executionOrder.push_back(3); }, 7);
	});
	queue.enqueue([&executionOrder] { executionOrder.push_back(2); }, 7);

	queue.exec();
	const std::vector<int> expectedOrder{ 1, 3 };
	REQUIRE(executionOrder == expectedOrder);
}

TEST_CASE("Execution queue accepts move-only tasks", "[executionqueue]")
{
	CExecutionQueue queue;
	int result = 0;

	queue.enqueue([value = std::make_unique<int>(42), &result] { result = *value; });
	queue.exec();

	REQUIRE(result == 42);
}

TEST_CASE("Execution queue accepts tasks from multiple producer threads", "[executionqueue]")
{
	CExecutionQueue queue;
	constexpr int producerCount = 4;
	constexpr int tasksPerProducer = 100;
	int executedTaskCount = 0;
	std::vector<std::thread> producers;

	for (int producer = 0; producer < producerCount; ++producer)
	{
		producers.emplace_back([&queue, &executedTaskCount]
		{
			for (int task = 0; task < tasksPerProducer; ++task)
				queue.enqueue([&executedTaskCount] { ++executedTaskCount; });
		});
	}

	for (auto& producer : producers)
		producer.join();

	queue.exec();
	REQUIRE(executedTaskCount == producerCount * tasksPerProducer);
}

TEST_CASE("Execution queue exec() on an empty queue is a no-op", "[executionqueue]")
{
	CExecutionQueue queue;
	queue.exec(CExecutionQueue::execFirst); // Neither mode may touch the empty deque or block
	queue.exec(CExecutionQueue::execAll);
	SUCCEED();
}

TEST_CASE("Execution queue execFirst runs the oldest task first", "[executionqueue]")
{
	CExecutionQueue queue;
	std::vector<int> executionOrder;

	queue.enqueue([&executionOrder] { executionOrder.push_back(1); });
	queue.enqueue([&executionOrder] { executionOrder.push_back(2); });
	queue.enqueue([&executionOrder] { executionOrder.push_back(3); });

	queue.exec(CExecutionQueue::execFirst);
	REQUIRE(executionOrder == std::vector<int>{ 1 });
	queue.exec(CExecutionQueue::execFirst);
	REQUIRE(executionOrder == std::vector<int>{ 1, 2 });
	queue.exec(CExecutionQueue::execFirst);
	REQUIRE(executionOrder == std::vector<int>{ 1, 2, 3 });
}

TEST_CASE("Replacing a tagged task leaves a different tag untouched", "[executionqueue]")
{
	CExecutionQueue queue;
	std::vector<int> executionOrder;

	queue.enqueue([&executionOrder] { executionOrder.push_back(1); }, 7);
	queue.enqueue([&executionOrder] { executionOrder.push_back(2); }, 8);
	queue.enqueue([&executionOrder] { executionOrder.push_back(3); }, 7); // Replaces only the tag-7 task

	queue.exec();

	// Tag 8 keeps its slot; the replacement tag-7 task runs where it was re-appended, after tag 8.
	const std::vector<int> expectedOrder{ 2, 3 };
	REQUIRE(executionOrder == expectedOrder);
}

TEST_CASE("Execution queue skips an empty task rather than invoking it", "[executionqueue]")
{
	CExecutionQueue queue;
	int executedTaskCount = 0;

	queue.enqueue({}); // Default-constructed, empty task: exec() must skip it via the `if (task.code)` guard
	queue.enqueue([&executedTaskCount] { ++executedTaskCount; });

	queue.exec();
	REQUIRE(executedTaskCount == 1);
}

TEST_CASE("Execution queue isolates a throwing task and runs the rest of the batch", "[executionqueue]")
{
	// The throw is contained + logged (assert_unconditional_r only logs in release); exec() must neither propagate
	// it nor skip the task queued behind it.
	CExecutionQueue queue;
	int executedAfterThrow = 0;

	queue.enqueue([] { throw std::runtime_error("deliberate task failure"); });
	queue.enqueue([&executedAfterThrow] { ++executedAfterThrow; });

	queue.exec();
	REQUIRE(executedAfterThrow == 1);
}
