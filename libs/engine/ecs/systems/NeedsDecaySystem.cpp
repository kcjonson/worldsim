#include "NeedsDecaySystem.h"

#include "../World.h"
#include "../components/Needs.h"

namespace ecs {

void NeedsDecaySystem::update(float deltaTime) {
    // Convert real-time to game-time
    float gameMinutes = deltaTime * gameTimeScale;

    // Decay all needs for entities with NeedsComponent
    for (auto [entity, needs] : world->view<NeedsComponent>()) {
        for (size_t i = 0; i < static_cast<size_t>(NeedType::Count); ++i) {
            needs.needs[i].decay(gameMinutes);
        }
    }
}

}  // namespace ecs
