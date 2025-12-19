#pragma once

// SystemResources - Platform-specific helpers for CPU/memory monitoring.
//
// Provides lightweight resource monitoring for performance diagnostics.
// Uses mach APIs on macOS for accurate per-process metrics.

#include <cstdint>

namespace Foundation {

	struct ResourceSnapshot {
		uint64_t memoryUsedBytes{};	 // Resident set size (physical memory)
		uint64_t memoryPeakBytes{};	 // Peak RSS
		float	 cpuUsagePercent{};	 // CPU usage since last sample (0-100%+)
		uint32_t cpuCoreCount{};	 // Number of CPU cores
	};

	class SystemResources {
	  public:
		/// Get current resource snapshot
		/// CPU usage is calculated since last call to this function
		static ResourceSnapshot sample();

	  private:
		// For CPU usage calculation between samples
		static uint64_t s_lastUserTime;
		static uint64_t s_lastSystemTime;
		static uint64_t s_lastSampleTime;
	};

} // namespace Foundation
