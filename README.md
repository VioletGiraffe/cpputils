# Summary

This library contains various C++ classes and snippets I've found myself in need of as my experience grows (as well a the complexity of my projects). Keep reading for details.

# Components

## "Advanced Assert"

A collection of assert-like macros with two key differences from a regular C `assert`:
  * the code you pass to the advanced assert macros (ending with `_r`) is executed in release build as well as debug;
  * when assertion fails, a message can be printed or otherwise processed using a callback function that you supply.

Like  regular `assert`, the `assert*_r` macros *will not*  trigger the `abort()` function or display an error message box when assertion fails in release build.
You can set your error message handler with `AdvancedAssert::setLoggingFunc(const std::function<void (const char*)>& func)`.

So, the "advanced" assert macros:
  * don't alter the code execution between debug and release builds;
  * call the optional failed assertion handler (useful for logging);
  * behave like the standard `assert` otherwise.

I use these macros instead of the regular `assert` in my projects as it significantly simplifies debugging release builds by analyzing log files. Additionally, `assert_and_return*` macros produce nice and compact code in cases where you need to check for an error that's not expected during normal workflow and return from the current function. The `assert_and_return*` macros let you significantly simplify and compactify routine error-checking code. Compare

```cpp
bool doWork()
{
    if (!f1())
    {
        std::cout << "Error calling f1()";
        return false;
    }

    if (!f2())
    {
        std::cout << "Error calling f2()";
        return false;
    }

    if (!f3())
    {
        std::cout << "Error calling f3()";
        return false;
    }
    
    return true;
}
```
    
to the functionally identical code using the assert_r macros:

```cpp
#include "assert/advanced_assert.h"

bool doWork()
{
    assert_and_return_r(f1(), false);
    assert_and_return_r(f2(), false);
    assert_and_return_r(f3(), false);
    
    return true;
}
```

**WARNING**: The error logging callback is a static data member of the `AdvancedAssert` class. Make sure you understand the implications for projects with dynamic libraries (.so / .dll / .dylib etc.).

## compiler/compiler_warnings_control.h

This header contains macros for suppressing compiler warnings for the specific parts of the source file in as platform-independent way as possible.

The most common use case:

```cpp
#include "compiler/compiler_warnings_control.h"
    
// The following header files belong to a 3rd-party library.
// These headers produce various compiler warnings.
// So I simply wrap them in warning suppression macros:

DISABLE_COMPILER_WARNINGS
#include "libusb.h"
#include "zlib.h"
RESTORE_COMPILER_WARNINGS
```

## system/ctimeelapsed.h

This is a cross-platform class for measuring wall time between two or more events with high accuracy (as opposed to the `clock()` function that reports CPU time and is hugely inaccurate for most use cases).
Currently implemented for Windows, OS X and Linux.
Arbitrary resolution - the return value is converted to any `std::chrono` type supplied as the template parameter (ms by default). The internal accuracy is at least μs or better.
Can be paused and resumed.

## Threading

### CInterruptableThread

A simple worker thread class - a convenience wrapper around `std::thread` with support for graceful interruption of the thread function. For that purpose, the termination flag (`atomic<bool>`) is exposed via the `terminationFlag()` method that the thread function may poll.

### CExecutionQueue

A thread-safe queue for `std::function` items (e. g. executable code). The main use case is queuing execution in a different thread, e. g. a worker thread can use the queue to post UI actions on the UI thread.

### CConsumerBlockingQueue

A general purpose (template item type) thread-safe queue with the main purpose of easily organizing pipelines by connecting multiple worker threads in series. `CConsumerBlockingQueue::pop()` blocks until an item is available (uses condition variable). Non-blocking `try_pop` method is also available.

### CWorkerThread / CWorkerThreadPool

The `CWorkerThread` class is a worker thread that receives its work load from a `CConsumerBlockingQueue`. The `CWorkerThread` is not available directly, but only as a `CWorkerThreadPool` class of one or more threads. The thread pool spawns threads on demand rather than unconditionally - up to `maxNumThreads`.

### CPeriodicExecutionThread

Another simple `std::thread` wrapper that executes the specified code every `period_ms` milliseconds.

# Building

The companion header-only [cpp-template-utils](https://github.com/VioletGiraffe/cpp-template-utils) library is required.

  * A compiler with C++ 0x/11 support is required (std::thread, lambda functions etc.).
  * Windows: you can build using either Qt Creator or Visual Studio for IDE. Visual Studio 2013 or newer is required - v120 toolset or newer. Run `qmake -tp vc -r` to generate the solution for Visual Studio. I have not tried building with MinGW, but it should work as long as you enable C++ 11 support.
  * Linux: open the project file in Qt Creator and build it.
  * Mac OS X: You can use either Qt Creator (simply open the project in it) or Xcode (run `qmake -r -spec macx-xcode` and open the Xcode project that has been generated).

P. S. Even though I use `qmake` as the build system for personal convenience, none of the actual code in this library depends on Qt. Only the C++11 standard library is required. Building with a diffrent build system is trivial, use the Qt `.pro` and `.pri` files as a reference.
