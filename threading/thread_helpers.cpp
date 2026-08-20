#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "thread_helpers.h"

#include <algorithm>
#include <map>
#include <thread>
#include <utility>

static uint32_t fallbackLogicalProcessorCount()
{
	return std::max(1u, std::thread::hardware_concurrency());
}

CpuCount::CpuCount(const uint32_t logicalProcessorCount, const uint32_t physicalCoreCount,
	std::vector<CpuPerformanceClass> performanceClasses) :
	_logicalProcessorCount(std::max(1u, logicalProcessorCount)),
	_physicalCoreCount(std::clamp(physicalCoreCount, 1u, _logicalProcessorCount)),
	_performanceClasses(std::move(performanceClasses))
{
}

struct CpuCore
{
	uint32_t logicalProcessorCount = 0;
	int32_t relativePerformance = 0;
};

template <typename CoreMap>
static std::vector<CpuCount::CpuPerformanceClass> distinguishablePerformanceClasses(const CoreMap& cores)
{
	std::map<int32_t, CpuCount::CpuPerformanceClass> classes;
	for (const auto& entry : cores)
	{
		const CpuCore& core = entry.second;
		auto& performanceClass = classes[core.relativePerformance];
		performanceClass.relativePerformance = core.relativePerformance;
		++performanceClass.physicalCoreCount;
		performanceClass.logicalProcessorCount += core.logicalProcessorCount;
	}
	if (classes.size() < 2)
		return {};

	std::vector<CpuCount::CpuPerformanceClass> result;
	result.reserve(classes.size());
	for (const auto& entry : classes)
		result.push_back(entry.second);
	return result;
}

#ifdef _WIN32
#include "assert/advanced_assert.h"
#include "utility/on_scope_exit.hpp"

#include <Windows.h>
#include <processthreadsapi.h>
#include <processtopologyapi.h>

#include <array> // std::size
#include <bit>
#include <cstddef>
#include <limits>
#include <unordered_set>

using GroupMasks = std::map<WORD, KAFFINITY>;

struct WindowsCpuSet
{
	ULONG id;
	WORD group;
	BYTE logicalProcessorIndex;
	BYTE coreIndex;
	BYTE relativePerformance;
	bool allocatedElsewhere;
};

static KAFFINITY activeProcessorMask(const WORD group)
{
	const DWORD count = ::GetActiveProcessorCount(group);
	if (count == 0)
		return 0;
	if (count >= static_cast<DWORD>(std::numeric_limits<KAFFINITY>::digits))
		return ~KAFFINITY{ 0 };
	return (KAFFINITY{ 1 } << count) - 1;
}

static GroupMasks processAffinityMasks()
{
	GroupMasks masks;
	std::vector<WORD> groups(::GetMaximumProcessorGroupCount());
	WORD groupCount = static_cast<WORD>(groups.size());
	if (::GetProcessGroupAffinity(::GetCurrentProcess(), &groupCount, groups.data()))
	{
		groups.resize(groupCount);
		for (const WORD group : groups)
			masks.emplace(group, activeProcessorMask(group));
	}

	GROUP_AFFINITY primaryGroup{};
	if (masks.empty() && ::GetThreadGroupAffinity(::GetCurrentThread(), &primaryGroup))
		masks.emplace(primaryGroup.Group, primaryGroup.Mask);

	DWORD_PTR processMask = 0;
	DWORD_PTR systemMask = 0;
	if (::GetProcessAffinityMask(::GetCurrentProcess(), &processMask, &systemMask)
		&& ::GetThreadGroupAffinity(::GetCurrentThread(), &primaryGroup))
	{
		masks[primaryGroup.Group] = static_cast<KAFFINITY>(processMask);
	}

	return masks;
}

static std::vector<ULONG> processDefaultCpuSetIds()
{
	using Function = BOOL (WINAPI *)(HANDLE, PULONG, ULONG, PULONG);
	const auto function = reinterpret_cast<Function>(::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "GetProcessDefaultCpuSets"));
	if (!function)
		return {};

	ULONG requiredCount = 0;
	if (function(::GetCurrentProcess(), nullptr, 0, &requiredCount))
		return {};
	if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || requiredCount == 0)
		return {};

	std::vector<ULONG> ids(requiredCount);
	if (!function(::GetCurrentProcess(), ids.data(), static_cast<ULONG>(ids.size()), &requiredCount))
		return {};
	ids.resize(requiredCount);
	return ids;
}

static std::vector<WindowsCpuSet> windowsCpuSets()
{
	using Function = BOOL (WINAPI *)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);
	const auto function = reinterpret_cast<Function>(::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "GetSystemCpuSetInformation"));
	if (!function)
		return {};

	ULONG requiredBytes = 0;
	if (function(nullptr, 0, &requiredBytes, ::GetCurrentProcess(), 0) || ::GetLastError() != ERROR_INSUFFICIENT_BUFFER || requiredBytes == 0)
		return {};

	std::vector<std::byte> buffer(requiredBytes);
	if (!function(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data()), requiredBytes, &requiredBytes, ::GetCurrentProcess(), 0))
		return {};

	std::vector<WindowsCpuSet> cpuSets;
	for (ULONG offset = 0; offset < requiredBytes; )
	{
		const auto* info = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buffer.data() + offset);
		if (info->Size == 0 || info->Size > requiredBytes - offset)
			return {};

		if (info->Type == CpuSetInformation)
		{
			const auto& cpuSet = info->CpuSet;
			cpuSets.push_back({ cpuSet.Id, cpuSet.Group, cpuSet.LogicalProcessorIndex, cpuSet.CoreIndex, cpuSet.EfficiencyClass,
				cpuSet.Allocated && !cpuSet.AllocatedToTargetProcess });
		}
		offset += info->Size;
	}
	return cpuSets;
}

static void applyProcessDefaultCpuSets(GroupMasks& affinityMasks, const std::vector<WindowsCpuSet>& cpuSets)
{
	const auto ids = processDefaultCpuSetIds();
	if (ids.empty())
		return;

	const std::unordered_set<ULONG> selectedIds(ids.cbegin(), ids.cend());
	GroupMasks cpuSetMasks;
	for (const WindowsCpuSet& cpuSet : cpuSets)
	{
		if (selectedIds.contains(cpuSet.id) && !cpuSet.allocatedElsewhere)
			cpuSetMasks[cpuSet.group] |= KAFFINITY{ 1 } << cpuSet.logicalProcessorIndex;
	}

	for (auto& [group, mask] : cpuSetMasks)
	{
		const auto affinity = affinityMasks.find(group);
		mask &= affinity == affinityMasks.end() ? KAFFINITY{ 0 } : affinity->second;
	}
	std::erase_if(cpuSetMasks, [](const auto& entry) { return entry.second == 0; });
	if (!cpuSetMasks.empty())
		affinityMasks = std::move(cpuSetMasks);
}

CpuCount CpuCount::get()
{
	GroupMasks availableMasks = processAffinityMasks();
	if (availableMasks.empty())
	{
		for (WORD group = 0; group < ::GetActiveProcessorGroupCount(); ++group)
			availableMasks.emplace(group, activeProcessorMask(group));
	}
	const std::vector<WindowsCpuSet> cpuSets = windowsCpuSets();
	applyProcessDefaultCpuSets(availableMasks, cpuSets);

	uint32_t affinityLogicalProcessorCount = 0;
	for (const auto& entry : availableMasks)
		affinityLogicalProcessorCount += static_cast<uint32_t>(std::popcount(entry.second));

	uint32_t logicalProcessorCount = 0;
	std::map<std::pair<WORD, BYTE>, CpuCore> cores;
	for (const WindowsCpuSet& cpuSet : cpuSets)
	{
		const auto available = availableMasks.find(cpuSet.group);
		if (cpuSet.allocatedElsewhere || available == availableMasks.end()
			|| !(available->second & (KAFFINITY{ 1 } << cpuSet.logicalProcessorIndex)))
			continue;

		++logicalProcessorCount;
		auto [coreIt, inserted] = cores.try_emplace({ cpuSet.group, cpuSet.coreIndex });
		CpuCore& core = coreIt->second;
		++core.logicalProcessorCount;
		if (inserted)
			core.relativePerformance = cpuSet.relativePerformance;
	}

	if (logicalProcessorCount == 0)
	{
		logicalProcessorCount = affinityLogicalProcessorCount != 0 ? affinityLogicalProcessorCount : fallbackLogicalProcessorCount();
		return { logicalProcessorCount, logicalProcessorCount };
	}

	auto classList = distinguishablePerformanceClasses(cores);
	return { logicalProcessorCount, static_cast<uint32_t>(cores.size()), std::move(classList) };
}

void setThreadName(const char *asciiName)
{
	auto* k32 = ::LoadLibraryA("kernel32.dll");
	assert_and_return_r(k32, );

	EXEC_ON_SCOPE_EXIT([k32]{
		::FreeLibrary(k32);
	});

	auto* func = reinterpret_cast<decltype(&::SetThreadDescription)>(::GetProcAddress(k32, "SetThreadDescription"));
	if (func == nullptr)
		return;

	WCHAR multibyteName[256];
	const auto nChars = ::MultiByteToWideChar(CP_UTF8, 0, asciiName, -1, multibyteName, static_cast<int>(std::size(multibyteName)));
	multibyteName[nChars] = 0;
	func(::GetCurrentThread(), multibyteName);
}

#elif defined __APPLE__

#include <pthread.h>
#include <sys/sysctl.h>

static uint32_t macLogicalProcessorCount()
{
	uint32_t count = 0;
	size_t size = sizeof(count);
	if (::sysctlbyname("hw.logicalcpu", &count, &size, nullptr, 0) != 0 || count == 0)
		return fallbackLogicalProcessorCount();
	return count;
}

CpuCount CpuCount::get()
{
	const uint32_t count = macLogicalProcessorCount();
	return { count, count, {} }; // File Commander targets arm64 macOS, whose cores have no SMT
}

void setThreadName(const char * asciiName)
{
	pthread_setname_np(asciiName);
}

#elif defined __linux__

#include <sched.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <errno.h>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>

struct CpuSetDeleter
{
	void operator()(cpu_set_t* set) const
	{
		CPU_FREE(set);
	}
};

using DynamicCpuSet = std::unique_ptr<cpu_set_t, CpuSetDeleter>;

static std::vector<uint32_t> allowedLinuxCpus()
{
	const long configuredCount = ::sysconf(_SC_NPROCESSORS_CONF);
	size_t bitCount = configuredCount > 0 ? static_cast<size_t>(configuredCount) : size_t{ CPU_SETSIZE };
	bitCount = std::max(bitCount, size_t{ CPU_SETSIZE });

	for (;; bitCount *= 2)
	{
		DynamicCpuSet set{ CPU_ALLOC(bitCount) };
		if (!set)
			return {};

		const size_t byteCount = CPU_ALLOC_SIZE(bitCount);
		CPU_ZERO_S(byteCount, set.get());
		if (::sched_getaffinity(0, byteCount, set.get()) == 0)
		{
			std::vector<uint32_t> cpus;
			for (size_t cpu = 0; cpu < bitCount; ++cpu)
			{
				if (CPU_ISSET_S(cpu, byteCount, set.get()))
					cpus.push_back(static_cast<uint32_t>(cpu));
			}
			return cpus;
		}
		if (errno != EINVAL || bitCount > static_cast<size_t>(UINT32_MAX) / 2)
			return {};
	}
}

template <typename T>
static bool readValue(const std::string& path, T& value)
{
	std::ifstream stream{ path };
	return static_cast<bool>(stream >> value);
}

CpuCount CpuCount::get()
{
	const std::vector<uint32_t> allowedCpus = allowedLinuxCpus();
	if (allowedCpus.empty())
	{
		const uint32_t logicalProcessorCount = fallbackLogicalProcessorCount();
		return { logicalProcessorCount, logicalProcessorCount };
	}

	std::unordered_map<std::string, CpuCore> cores;
	bool allCoresHaveRelativePerformance = true;
	for (const uint32_t cpu : allowedCpus)
	{
		const std::string cpuDirectory = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + '/';
		std::string coreCpus;
		if (!readValue(cpuDirectory + "topology/core_cpus_list", coreCpus)
			&& !readValue(cpuDirectory + "topology/thread_siblings_list", coreCpus))
		{
			const uint32_t logicalProcessorCount = static_cast<uint32_t>(allowedCpus.size());
			return { logicalProcessorCount, logicalProcessorCount };
		}

		auto [coreIt, inserted] = cores.try_emplace(std::move(coreCpus));
		CpuCore& core = coreIt->second;
		++core.logicalProcessorCount;
		if (inserted)
			allCoresHaveRelativePerformance &= readValue(cpuDirectory + "cpu_capacity", core.relativePerformance);
	}

	const uint32_t logicalProcessorCount = static_cast<uint32_t>(allowedCpus.size());
	const uint32_t physicalCoreCount = static_cast<uint32_t>(cores.size());
	if (!allCoresHaveRelativePerformance)
		return { logicalProcessorCount, physicalCoreCount };

	auto classList = distinguishablePerformanceClasses(cores);
	return { logicalProcessorCount, physicalCoreCount, std::move(classList) };
}

void setThreadName(const char * asciiName)
{
	prctl(PR_SET_NAME, asciiName, 0, 0, 0);
}
#elif defined __FreeBSD__
#include <pthread_np.h>
#include <pthread.h>

CpuCount CpuCount::get()
{
	const uint32_t logicalProcessorCount = fallbackLogicalProcessorCount();
	return { logicalProcessorCount, logicalProcessorCount };
}

void setThreadName(const char * asciiName)
{
	pthread_set_name_np(pthread_self(),asciiName);
}

#else

CpuCount CpuCount::get()
{
	const uint32_t logicalProcessorCount = fallbackLogicalProcessorCount();
	return { logicalProcessorCount, logicalProcessorCount };
}

#endif
