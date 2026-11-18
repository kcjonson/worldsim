#pragma once

// MoodSystem: every colonist's target mood (the needs score plus their held thoughts' mood effects,
// clamped 0 to 100), the mood that follows it on the game clock, and the mood cause.
// See /docs/design/game-systems/colonists/mood.md.

#include "../EntityID.h"
#include "../ISystem.h"
#include "../components/Mood.h"

#include <assets/MoodConfig.h>

namespace ecs {

	/// Priority 12: after ThoughtSystem, so the target counts this frame's held thoughts.
	class MoodSystem : public ISystem {
	  public:
		explicit MoodSystem(const assets::MoodConfig& moodConfig);

		void update(float deltaTime) override;

		/// Gives a colonist with needs its Mood, at its target now. Spawning calls this; update calls it
		/// for a colonist that has none.
		Mood& startMood(EntityID colonist);

		[[nodiscard]] int		  priority() const override { return 12; }
		[[nodiscard]] const char* name() const override { return "Mood"; }

	  private:
		/// Sets the contributions, target, and cause from the colonist's needs and held thoughts.
		void refreshTarget(EntityID colonist, const NeedsComponent& needs, Mood& mood) const;
		[[nodiscard]] double now() const;

		const assets::MoodConfig& config;
	};

} // namespace ecs
