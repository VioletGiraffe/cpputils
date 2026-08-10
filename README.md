# cpputils

Cross-platform C++ utility library. It has no Qt dependency; compiled facilities form a static library and templates remain in their headers. `cpp-template-utils` is required.

## Facilities

### Diagnostics

#### `assert/advanced_assert.h`: advanced assertions

A collection of assert-like macros with two key differences from a regular C `assert`:

- The code passed to the advanced assertion macros ending with `_r` is executed in release builds as well as debug builds.
- When an assertion fails, a message can be printed or otherwise processed using a callback supplied by the application.

Like regular `assert`, the `assert*_r` macros do not call `abort()` or display an error message box when an assertion fails in a release build. Set the error-message handler with `AdvancedAssert::setLoggingFunc(std::function<void (const char*)> func)`.

The advanced assertion macros therefore:

- Do not alter code execution between debug and release builds.
- Call the optional failed-assertion handler, which is useful for logging.
- Behave like the standard `assert` otherwise.

I use these macros instead of regular `assert` in my projects because they significantly simplify debugging release builds through log analysis. The `assert_and_return*` macros also produce compact code when checking for an error that is not expected during normal operation and returning from the current function. Compare:

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

with the functionally identical code using the assertion macros:

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

**Warning:** The error-logging callback is a static data member of `AdvancedAssert`. Account for the resulting module boundaries when using the library from dynamic libraries (`.so`, `.dll`, or `.dylib`).

#### `debugger/debugger_is_attached.h`

Detects whether the current process is being debugged on Windows and `/proc`-based systems.

### Threading

| Header | Facility |
|---|---|
| `threading/cconsumerblockingqueue.h` | Bounded, thread-safe deque with blocking and non-blocking push/pop, timed waits, predicate wakeups, removal, inspection, and shutdown notification. |
| `threading/cexecutionqueue.h` | Thread-safe queue of move-only callbacks for deferred or cross-thread execution. A tag replaces older queued work with the same tag; execution can consume one item or the entry-time backlog and contains task exceptions. |
| `threading/cinterruptablethread.h` | Named owned thread whose payload receives a cancellation flag; destruction requests cancellation and joins, and uncaught payload exceptions are logged. |
| `threading/cperiodicexecutionthread.h` | Named worker that runs a replaceable callback at a fixed period, with optional startup delay plus non-blocking pause/resume and terminating join. |
| `threading/cthreadpool.h` | Work-stealing pool with fire-and-forget tasks and completion futures, owner tags with `retire()` lifetime barriers, synchronous/async parallel index loops, queue metrics, and optional backlog draining at shutdown. |
| `threading/simplethread.h` | Minimal legacy `std::thread` owner with a cooperative termination flag. |
| `threading/thread_helpers.h` | Affinity-aware logical/physical CPU counts, heterogeneous performance classes where the OS exposes them, and portable current-thread naming. |

### Timing and measurement

| Header | Facility |
|---|---|
| `timing/ctimeelapsed.h` | Pauseable/resumable steady-clock stopwatch with arbitrary `std::chrono` result units and nanosecond/microsecond/millisecond shortcuts. |
| `timing/profiler.h` | Opt-out lightweight timeline marks, RAII scope timing, and named accumulating samples with a pluggable log sink. The profiler is intentionally single-threaded. |
| `system/timing.h` | Millisecond elapsed-time clock (monotonic on Windows/Linux, wall-clock fallback elsewhere) and direct timestamp-counter access; the ARM counter helper is suitable only as thread-local entropy, not cross-core timing. |

### Hashing and statistics

| Header | Facility |
|---|---|
| `hash/sha3.h` | C API for incremental SHA3-256, SHA3-384, and SHA3-512. |
| `hash/sha3_hasher.hpp` | Typed incremental SHA-3 wrapper for byte ranges, strings, and trivially serializable values, returning the full digest or a 64-bit prefix; also provides `sha3_64bit()`. |
| `math/cmeancounter.h` | Streaming arithmetic, geometric, and exponentially smoothed means with reset support. |

### System integration

| Header | Facility |
|---|---|
| `system/processfilepath.hpp` | Returns the current executable path as a wide string on Windows, Linux, and macOS; the result is not guaranteed absolute or canonical. |
| `system/consoleapplicationexithandler.h` | Registers a callback for Windows console close, Ctrl-C, break, logoff, and shutdown events. Other platforms currently retain the callback but install no OS handler. |
| `system/storagespeed.hpp` | Thread-safe, per-volume cached classification into fast random-access storage or slow/unknown storage, using native Windows, Linux, and macOS metadata. |
| `system/win_utils.hpp` | Windows-only COM initialization RAII and readable messages for Win32 error codes, `GetLastError()`, and `HRESULT`; definitions collapse to a no-op COM macro elsewhere. |

### Language and memory helpers

| Header | Facility |
|---|---|
| `lang/enum.h` | Base template for enum-like types backed by a declared value/name table, with checked construction, name lookup, conversion, and iteration. |
| `lang/type_traits_fast.hpp` | Convenient compile-time limits for fixed-width integers, `size_t`, and `float`. |
| `utility_functions/memory_functions.h` | `memfind()`, a binary-safe search for a byte sequence inside another byte range. |

## Building

The library targets C++23. qmake (`cpputils.pro`) is the primary project; `CMakeLists.txt` is also provided. Supported implementations cover Windows, Linux, and macOS, with limited FreeBSD fallbacks. Add this repository and `cpp-template-utils` to the include path; qmake consumers should also include `dependencies.pri` so platform link dependencies are propagated.
