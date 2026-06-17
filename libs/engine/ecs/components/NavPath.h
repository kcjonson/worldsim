#pragma once

#include <glm/vec2.hpp>

#include <cstddef>
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

	[[nodiscard]] bool done() const { return !valid || current >= waypoints.size(); }
};

} // namespace ecs
