#pragma once

// System resource monitoring for performance metrics.
// Provides CPU and memory usage sampling.

#include <cstdint>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <unistd.h>
#endif

namespace Foundation {

/// Snapshot of system resource usage
struct ResourceSnapshot {
	uint64_t memoryUsedBytes = 0;	// Process resident memory (RSS)
	uint64_t memoryPeakBytes = 0;	// Peak memory usage
	float cpuUsagePercent = 0.0F;	// CPU usage (0-100%, may exceed 100% on multi-core)
	uint32_t cpuCoreCount = 0;		// Number of CPU cores
};

/// Utility class for sampling system resources
class SystemResources {
  public:
	/// Sample current system resource usage
	static ResourceSnapshot sample() {
		ResourceSnapshot snapshot;

#if defined(__APPLE__)
		// Get memory usage via Mach task info
		mach_task_basic_info_data_t taskInfo{};
		mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
		if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
					  reinterpret_cast<task_info_t>(&taskInfo), &infoCount) == KERN_SUCCESS) {
			snapshot.memoryUsedBytes = taskInfo.resident_size;
		}

		// Get peak memory from rusage
		struct rusage usage{};
		if (getrusage(RUSAGE_SELF, &usage) == 0) {
			// On macOS, ru_maxrss is in bytes
			snapshot.memoryPeakBytes = static_cast<uint64_t>(usage.ru_maxrss);
		}

		// Get CPU core count
		int ncpu = 0;
		size_t len = sizeof(ncpu);
		if (sysctlbyname("hw.ncpu", &ncpu, &len, nullptr, 0) == 0) {
			snapshot.cpuCoreCount = static_cast<uint32_t>(ncpu);
		}

		// CPU usage would require tracking over time - leave at 0 for now
		snapshot.cpuUsagePercent = 0.0F;
#endif

		return snapshot;
	}
};

} // namespace Foundation
