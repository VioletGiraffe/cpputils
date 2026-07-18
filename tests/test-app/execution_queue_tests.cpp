#include "3rdparty/catch2/catch.hpp"

#include "threading/cexecutionqueue.h"

#include <memory>
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
