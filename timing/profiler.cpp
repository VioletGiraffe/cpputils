#include "profiler.h"

#include <deque>
#include <format>
#include <string>

namespace {

std::function<void (const char*)>& loggingFunc()
{
	static std::function<void (const char*)> func;
	return func;
}

// Auto-starts on first use so a mark() before any explicit start() still reports a sane timeline.
CTimeElapsed& timeline()
{
	static CTimeElapsed clock{ true };
	return clock;
}

int& scopeDepth()
{
	static int depth = 0;
	return depth;
}

// A deque, not a vector: push_back must not invalidate references handed out earlier. Insertion order is the
// report order, so a profile can be diffed against another run.
std::deque<Profiler::Accumulator>& counters()
{
	static std::deque<Profiler::Accumulator> allCounters;
	return allCounters;
}

void log(const std::string& message)
{
	if (const auto& func = loggingFunc())
		func(message.c_str());
}

} // anonymous namespace

namespace Profiler {

void setLoggingFunc(std::function<void (const char*)> func)
{
	loggingFunc() = std::move(func);
}

void start()
{
	timeline().start();
}

void mark(const char* what)
{
	static uint64_t previousNs = 0;

	const uint64_t nowNs = timeline().nsElapsed();
	log(std::format("@{:9.2f} ms  +{:8.2f} ms  {}", nowNs / 1e6, (nowNs - previousNs) / 1e6, what));
	previousNs = nowNs;
}

Scope::Scope(const char* what) noexcept : _what{ what }, _indent{ scopeDepth()++ }, _timer{ true }
{
}

Scope::~Scope()
{
	const double elapsedMs = _timer.nsElapsed() / 1e6;
	--scopeDepth();

	log(std::format("@{:9.2f} ms  {:9.2f} ms  {}{}", timeline().nsElapsed() / 1e6, elapsedMs,
		std::string(static_cast<size_t>(_indent) * 2, ' '), _what));
}

void Accumulator::report() const
{
	log(std::format("@{:9.2f} ms  {:9.2f} ms  {}  ({} calls, {:.2f} us each)", timeline().nsElapsed() / 1e6,
		_ns / 1e6, _name, _calls, _calls > 0 ? (_ns / 1000.0) / static_cast<double>(_calls) : 0.0));
}

Accumulator& counter(std::string_view name)
{
	for (Accumulator& existing : counters())
	{
		// string_view on the left: the reversed form needs C++20's rewritten candidates to compile.
		if (name == existing.name())
			return existing;
	}

	return counters().emplace_back(std::string{ name });
}

void resetCounters()
{
	for (Accumulator& accumulator : counters())
		accumulator.reset();
}

void reportCounters()
{
	for (const Accumulator& accumulator : counters())
		accumulator.report();
}

} // namespace Profiler
