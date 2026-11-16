#pragma once

#include "../EntityID.h"
#include "../World.h"
#include "../components/AgentRadius.h"
#include "../components/Memory.h"
#include "../components/Transform.h"

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <cmath>
#include <initializer_list>

namespace ecs {

	// Still movers along the polyline `way` (no Memory, so each sees kDefaultSightRadius), one at
	// every corner and none more than that sight radius from the next. What they mesh holds the
	// strip a colonist walking `way` would have seen, so a test can stand on ground "walked" without
	// walking anyone. They are not colonists: nothing but nav acts on them.
	inline void meshBand(World& world, std::initializer_list<glm::vec2> way) {
		const auto stand = [&world](glm::vec2 at) {
			const EntityID e = world.createEntity();
			world.addComponent<Position>(e, Position{at});
			world.addComponent<AgentRadius>(e, AgentRadius{});
		};
		const glm::vec2* previous = nullptr;
		for (const glm::vec2& corner : way) {
			if (previous == nullptr) {
				stand(corner);
			} else {
				const int steps = static_cast<int>(std::ceil(glm::distance(*previous, corner) / kDefaultSightRadius));
				for (int i = 1; i <= steps; ++i) {
					stand(*previous + (corner - *previous) * (static_cast<float>(i) / static_cast<float>(steps)));
				}
			}
			previous = &corner;
		}
	}

} // namespace ecs
