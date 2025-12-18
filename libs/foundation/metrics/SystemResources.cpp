// SystemResources - macOS implementation using mach APIs.

#include "metrics/SystemResources.h"

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/task_info.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#endif

#include <chrono>
#include <thread>

namespace Foundation {

	// Static members for CPU tracking
	uint64_t SystemResources::s_lastUserTime = 0;
	uint64_t SystemResources::s_lastSystemTime = 0;
	uint64_t SystemResources::s_lastSampleTime = 0;

	ResourceSnapshot SystemResources::sample() {
		ResourceSnapshot snapshot;

#ifdef __APPLE__
		// Get memory info
		mach_task_basic_info_data_t taskInfo;
		mach_msg_type_number_t		infoCount = MACH_TASK_BASIC_INFO_COUNT;

		if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&taskInfo), &infoCount) ==
			KERN_SUCCESS) {
			snapshot.memoryUsedBytes = taskInfo.resident_size;
			snapshot.memoryPeakBytes = taskInfo.resident_size_max;
		}

		// Get CPU usage
		thread_array_t		  threadList;
		mach_msg_type_number_t threadCount;

		if (task_threads(mach_task_self(), &threadList, &threadCount) == KERN_SUCCESS) {
			uint64_t totalUserTime = 0;
			uint64_t totalSystemTime = 0;

			for (mach_msg_type_number_t i = 0; i < threadCount; ++i) {
				thread_basic_info_data_t threadInfo;
				mach_msg_type_number_t	 threadInfoCount = THREAD_BASIC_INFO_COUNT;

				if (thread_info(threadList[i], THREAD_BASIC_INFO, reinterpret_cast<thread_info_t>(&threadInfo),
								&threadInfoCount) == KERN_SUCCESS) {
					// Convert to microseconds
					totalUserTime +=
						static_cast<uint64_t>(threadInfo.user_time.seconds) * 1000000 + threadInfo.user_time.microseconds;
					totalSystemTime += static_cast<uint64_t>(threadInfo.system_time.seconds) * 1000000 +
									   threadInfo.system_time.microseconds;
				}

				mach_port_deallocate(mach_task_self(), threadList[i]);
			}

			vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threadList),
						  threadCount * sizeof(thread_t));

			// Calculate CPU percentage since last sample
			auto now =
				std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
					.count();

			if (s_lastSampleTime > 0) {
				uint64_t elapsedUs = static_cast<uint64_t>(now) - s_lastSampleTime;
				uint64_t cpuTimeUs = (totalUserTime - s_lastUserTime) + (totalSystemTime - s_lastSystemTime);

				if (elapsedUs > 0) {
					snapshot.cpuUsagePercent = static_cast<float>(cpuTimeUs) / static_cast<float>(elapsedUs) * 100.0F;
				}
			}

			s_lastUserTime = totalUserTime;
			s_lastSystemTime = totalSystemTime;
			s_lastSampleTime = static_cast<uint64_t>(now);
		}
#endif

		// Get CPU core count (cached after first call)
		snapshot.cpuCoreCount = static_cast<uint32_t>(std::thread::hardware_concurrency());

		return snapshot;
	}

} // namespace Foundation
