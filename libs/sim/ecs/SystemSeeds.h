#pragma once

#include <random/SplitMix64.h>

#include <cstdint>

namespace ecs {

	/// RNG seeds for the systems that draw random numbers, all mixed from one session seed so that
	/// logging the session seed is enough to replay a session.
	struct SystemSeeds {
		uint32_t aiDecision;
		uint32_t action;
		uint32_t thought;
	};

	constexpr SystemSeeds deriveSystemSeeds(uint64_t sessionSeed) {
		constexpr uint64_t kAiDecisionStream = 1;
		constexpr uint64_t kActionStream = 2;
		constexpr uint64_t kThoughtStream = 3;
		return SystemSeeds{
			.aiDecision = static_cast<uint32_t>(foundation::deriveSeed(sessionSeed, kAiDecisionStream)),
			.action = static_cast<uint32_t>(foundation::deriveSeed(sessionSeed, kActionStream)),
			.thought = static_cast<uint32_t>(foundation::deriveSeed(sessionSeed, kThoughtStream)),
		};
	}

} // namespace ecs
