#pragma once

// NeedsDecaySystem - Decays colonist needs over time.
//
// Uses TimeSystem::effectiveTimeScale() to get speed-adjusted time.
// When game is paused, needs don't decay.

#include "../ISystem.h"

namespace ecs {

/// Decays all needs over time based on game-time scaling from TimeSystem.
/// Priority: 50 (runs early, before movement decisions)
class NeedsDecaySystem : public ISystem {
  public:
	void update(float deltaTime) override;

	[[nodiscard]] int priority() const override { return 50; }
	[[nodiscard]] const char* name() const override { return "NeedsDecay"; }
};

}  // namespace ecs
