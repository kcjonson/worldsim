#pragma once

// MoodSystem: every colonist's target mood (the needs score plus their held thoughts' mood effects,
// clamped 0 to 100), the mood that follows it on the game clock, and the mood cause.
// See /docs/design/game-systems/colonists/mood.md.

#include "../ISystem.h"
#include "../components/Mood.h"

#include <assets/MoodConfig.h>

#include <cstdint>

namespace ecs {

	/// Priority 12: after ThoughtSystem, so the target counts this frame's held thoughts.
	class MoodSystem : public ISystem {
	  public:
		explicit MoodSystem(const assets::MoodConfig& moodConfig);

		void update(float deltaTime) override;

		[[nodiscard]] int		  priority() const override { return 12; }
		[[nodiscard]] const char* name() const override { return "Mood"; }

	  private:
		const assets::MoodConfig& config;
		NeedMoodWeights			  needWeights{};
		uint64_t				  resolvedGeneration = 0;
	};

} // namespace ecs
