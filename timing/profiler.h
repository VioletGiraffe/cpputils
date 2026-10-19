#pragma once

// Scoped timing helper: a timeline of marks, RAII scope timers, and named accumulators for per-iteration costs.
// Cheap enough to enable in a release build (~50 ns per sample), so a profile can be collected from a shipped
// binary without a profiler attached - but for the same reason it is not honest about intervals below ~1 us.
//
// The PROFILE_* macros compile to nothing unless PROFILING_ENABLED is defined.
// Output goes nowhere until setLoggingFunc() installs a sink.
//
// Accumulators and the counter registry are thread-safe.
// mark() and Scope are single-threaded: the previous mark and the scope depth are plain globals.

#include "ctimeelapsed.h"

#include <atomic>
#include <functional>
#include <stdint.h>
#include <string>
#include <string_view>

namespace Profiler {

void setLoggingFunc(std::function<void (const char*)> func);

// Restarts the timeline that every logged line is timestamped against.
void start();

// Timeline checkpoint. The delta is against the previous mark from anywhere, not from this call site.
void mark(const char* what);

// Logs the duration of the enclosing scope on exit, indented by nesting level. Nested scopes report before
// the scope containing them, because that is when they end.
class Scope
{
public:
	explicit Scope(const char* what) noexcept;
	~Scope();

	Scope(const Scope&) = delete;
	Scope& operator=(const Scope&) = delete;

private:
	const char* _what;
	int _indent;
	CTimeElapsed _timer;
};

// Sums many short intervals under one name. Per-call cost is what makes a loop's stages comparable, so it is
// reported alongside the total.
class Accumulator
{
public:
	explicit Accumulator(std::string name) noexcept : _name{ std::move(name) } {}

	Accumulator(const Accumulator&) = delete;
	Accumulator& operator=(const Accumulator&) = delete;

	void reset() noexcept
	{
		_ns.store(0, std::memory_order_relaxed);
		_calls.store(0, std::memory_order_relaxed);
	}

	void report() const;

	[[nodiscard]] const std::string& name() const noexcept { return _name; }

	// The only part of the profiler on a hot path, hence inline: out of line, the call would cost a
	// noticeable fraction of the interval being measured.
	class Sample
	{
	public:
		explicit Sample(Accumulator& accumulator) noexcept : _accumulator{ accumulator }, _timer{ true } {}

		~Sample()
		{
			_accumulator._ns.fetch_add(_timer.nsElapsed(), std::memory_order_relaxed);
			_accumulator._calls.fetch_add(1, std::memory_order_relaxed);
		}

		Sample(const Sample&) = delete;
		Sample& operator=(const Sample&) = delete;

	private:
		Accumulator& _accumulator;
		CTimeElapsed _timer;
	};

private:
	std::string _name;
	std::atomic<uint64_t> _ns{ 0 };
	std::atomic<uint64_t> _calls{ 0 };
};

// Creates the counter on first use. The reference stays valid for the process lifetime, so a call site can
// cache it in a function-local static and never look it up again - which PROFILE_SAMPLE does.
[[nodiscard]] Accumulator& counter(std::string_view name);

void resetCounters();
void reportCounters();

} // namespace Profiler

#define PROFILER_CONCAT2(a, b) a##b
#define PROFILER_CONCAT(a, b) PROFILER_CONCAT2(a, b)

// __COUNTER__ is expanded once as an argument, so both names below get the same suffix.
#define PROFILER_SAMPLE_IMPL(name, id) \
	static Profiler::Accumulator& PROFILER_CONCAT(_profilerCounter, id) = Profiler::counter(name); \
	Profiler::Accumulator::Sample PROFILER_CONCAT(_profilerSample, id) { PROFILER_CONCAT(_profilerCounter, id) }

#ifdef PROFILING_ENABLED
	#define PROFILE_MARK(what)   Profiler::mark(what)
	#define PROFILE_SCOPE(what)  Profiler::Scope PROFILER_CONCAT(_profilerScope, __COUNTER__) { what }
	#define PROFILE_SAMPLE(name) PROFILER_SAMPLE_IMPL(name, __COUNTER__)
	#define PROFILE_RESET_COUNTERS()  Profiler::resetCounters()
	#define PROFILE_REPORT_COUNTERS() Profiler::reportCounters()
#else
	#define PROFILE_MARK(what)   ((void)0)
	#define PROFILE_SCOPE(what)  ((void)0)
	#define PROFILE_SAMPLE(name) ((void)0)
	#define PROFILE_RESET_COUNTERS()  ((void)0)
	#define PROFILE_REPORT_COUNTERS() ((void)0)
#endif
