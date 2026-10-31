#pragma once

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ecs {

// A computed route for a mobile agent: an ordered list of world-meter waypoints
// from near the agent's current position to its goal. MovementSystem steers
// toward waypoints[current] and advances `current` on arrival; NavigationSystem
// answers the query that fills `waypoints`. Empty/invalid until a path is set.
struct NavPath {
	std::vector<glm::vec2> waypoints;	// world meters, start..goal; [0] is near current pos
	std::size_t			   current = 0; // index of the next waypoint to steer toward
	bool				   valid = false;

	// Staleness stamps captured when this route was planned. The replan-on-discovery
	// loop compares them against the colonist's current Memory::beliefVersion and the
	// NavigationSystem's generation(); a mismatch means the belief or the mesh moved
	// since planning, so the same-goal path must be re-requested. Cheap to compare, no
	// re-query needed to know the route is stale.
	std::uint64_t builtBeliefVersion = 0; // Memory::beliefVersion at plan time
	std::uint64_t builtNavVersion = 0;	  // NavigationSystem::generation() at plan time

	[[nodiscard]] bool done() const { return !valid || current >= waypoints.size(); }
};

} // namespace ecs
