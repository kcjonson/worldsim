#pragma once

// Test helpers shared by the NavigationSystem and AIDecisionSystem tests. Waiting on builds is
// NavigationSystem::finishPendingBuilds.

#include "NavigationSystem.h"

namespace ecs {

	// One start, one goal: the reachFrom/reaches pair the AI runs over many goals.
	[[nodiscard]] inline bool isReachable(const NavigationSystem& nav, glm::vec2 start, glm::vec2 goal, float agentRadius,
										   geometry::nav::BeliefFilter belief = {}) {
		return nav.reaches(nav.reachFrom(start, agentRadius, belief), goal);
	}

} // namespace ecs
