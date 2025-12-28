#include "NeedsDecaySystem.h"

#include "../World.h"
#include "../components/Needs.h"
#include "TimeSystem.h"

namespace ecs {

void NeedsDecaySystem::update(float deltaTime) {
	// Get effective time scale from TimeSystem (0 when paused)
	auto& timeSystem = world->getSystem<TimeSystem>();
	float gameMinutes = deltaTime * timeSystem.effectiveTimeScale();

	// Skip decay if paused (gameMinutes will be 0)
	if (gameMinutes <= 0.0F) {
		return;
	}

	// Decay all needs for entities with NeedsComponent
	for (auto [entity, needs] : world->view<NeedsComponent>()) {
		for (size_t i = 0; i < static_cast<size_t>(NeedType::Count); ++i) {
			needs.needs[i].decay(gameMinutes);
		}
	}
}

}  // namespace ecs
