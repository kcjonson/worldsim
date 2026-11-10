#pragma once

// Test helpers shared by the NavigationSystem and AIDecisionSystem tests. Waiting on builds is
// NavigationSystem::finishPendingBuilds.

#include "NavigationSystem.h"

#include "../World.h"
#include "../components/Colonist.h"
#include "../components/Transform.h"

#include <vector>

namespace ecs {

	[[nodiscard]] inline NavigationSystem::Reach reachOf(const NavigationSystem& nav, glm::vec2 start, float agentRadius,
													  geometry::nav::BeliefFilter belief = {}) {
		NavigationSystem::Reach reach;
		nav.reachFrom(start, agentRadius, belief, reach);
		return reach;
	}

	// One start, one goal: the reachFrom/reaches pair the AI runs over many goals.
	[[nodiscard]] inline bool isReachable(const NavigationSystem& nav, glm::vec2 start, glm::vec2 goal, float agentRadius,
										   geometry::nav::BeliefFilter belief = {}) {
		return nav.reaches(reachOf(nav, start, agentRadius, belief), goal);
	}

	// Colonist-tagged entities with nothing else on them (no system but navigation acts on them), one
	// per corner of the square `center` +/- `halfExtentMeters`, inset by the colonist radius, so their
	// squares cluster into one colonist region exactly that square. halfExtentMeters is 30..60 m.
	inline std::vector<EntityID> driveColonistRegion(World& world, glm::vec2 center, float halfExtentMeters) {
		const float inset = halfExtentMeters - static_cast<float>(NavigationSystem::kSimRadiusMm) / 1000.0F;
		std::vector<EntityID> drivers;
		for (const glm::vec2 corner : {glm::vec2{-1.0F, -1.0F}, glm::vec2{1.0F, -1.0F}, glm::vec2{-1.0F, 1.0F}, glm::vec2{1.0F, 1.0F}}) {
			const EntityID driver = world.createEntity();
			world.addComponent<Position>(driver, Position{center + corner * inset});
			world.addComponent<Colonist>(driver, Colonist{"Driver"});
			drivers.push_back(driver);
		}
		return drivers;
	}

} // namespace ecs
