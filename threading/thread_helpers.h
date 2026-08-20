#pragma once

#include <stdint.h>
#include <string>
#include <vector>

class CpuCount
{
public:
	struct CpuPerformanceClass
	{
		int32_t relativePerformance = 0; // Only comparable within one snapshot; higher means faster
		uint32_t physicalCoreCount = 0;
		uint32_t logicalProcessorCount = 0;
	};

	// Current scheduling resources: respects calling-thread affinity on Linux and process affinity/default CPU Sets on Windows.
	[[nodiscard]] static CpuCount get();

	[[nodiscard]] uint32_t logicalProcessorCount() const noexcept { return _logicalProcessorCount; }
	[[nodiscard]] uint32_t physicalCoreCount() const noexcept { return _physicalCoreCount; } // Always at least 1
	// Unknown or indistinguishable core types are all treated as the highest-performance tier
	[[nodiscard]] uint32_t performanceCoreCount() const noexcept { return _performanceClasses.empty() ? _physicalCoreCount : _performanceClasses.back().physicalCoreCount; }
	[[nodiscard]] uint32_t efficiencyCoreCount() const noexcept { return _physicalCoreCount - performanceCoreCount(); }
	// Ascending by relativePerformance; empty when the available CPUs do not have distinguishable performance classes
	[[nodiscard]] const std::vector<CpuPerformanceClass>& performanceClasses() const noexcept { return _performanceClasses; }

private:
	CpuCount(uint32_t logicalProcessorCount, uint32_t physicalCoreCount, std::vector<CpuPerformanceClass> performanceClasses = {});

private:
	uint32_t _logicalProcessorCount = 1;
	uint32_t _physicalCoreCount = 1;
	std::vector<CpuPerformanceClass> _performanceClasses;
};

void setThreadName(const char* asciiName);

inline void setThreadName(const std::string& str)
{
	setThreadName(str.c_str());
}
