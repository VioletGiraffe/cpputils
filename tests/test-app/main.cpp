#define CATCH_CONFIG_MAIN  // This tells Catch to provide a main() - only do this in one cpp file
#include "3rdparty/catch2/catch.hpp"

//#include "threading/cworkerthread.h"
//#include "threading/thread_helpers.h"
//
//int main()
//{
//	setThreadName("Main");
//	static constexpr int N = 100000;
//
//	CWorkerThreadPool pool(1, "Test thread pool");
//	pool.waitUntilStarted();
//
//	int a = 0;
//	for (int n = 0; n < 10; ++n)
//	{
//		a = 0;
//		for (size_t i = 0; i < N; ++i)
//			pool.enqueue([&a] {++a; });
//		while (pool.queueLength() > 0);
//	}
//
//	return a + (int)pool.queueLength();
//}
