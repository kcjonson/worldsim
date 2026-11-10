#pragma once

// Test helpers that pump a NavigationSystem until its async region builds land.
//
// There is no frame or wall-clock budget. A Debug build on a slow CI runner takes
// seconds per region build, and a fixed budget (500 frames of 2 ms) failed such a
// runner as "navmesh never built". A wait ends when its condition holds or when the
// system settles without it (isBuilding() false after an update: no build will land
// that could change the answer), so a gate that never fires still fails at once. A
// build that never finishes is caught by the TIMEOUT on the sim-tests ctest entry;
// every kHeartbeat the wait prints what it is waiting for and the system's region
// counts and generation, so a timed-out log shows whether builds were still landing.
//
// Only a wait for something that must happen belongs here. A check that a rebuild
// does NOT happen keeps its own fixed frame count.

#include "NavigationSystem.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace ecs {

	// Calls nav.update(0) until done() holds or the system settles, and returns done().
	// The zero step advances no sim time, so nothing else in the world moves however
	// long the build worker takes; the 2 ms sleep between frames leaves it the core.
	template <typename Done>
	[[nodiscard]] bool pumpUntil(NavigationSystem& nav, const char* what, Done&& done) {
		using Clock = std::chrono::steady_clock;
		constexpr auto kHeartbeat = std::chrono::seconds(30);

		const auto start = Clock::now();
		auto	   nextHeartbeat = start + kHeartbeat;
		for (;;) {
			nav.update(0.0F);
			if (done()) {
				return true;
			}
			if (!nav.isBuilding()) {
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			const auto now = Clock::now();
			if (now >= nextHeartbeat) {
				const auto secs = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
				printf(
					"[NavigationSystem] still waiting for %s after %lld s: %zu regions, %zu built, generation %llu\n",
					what,
					static_cast<long long>(secs),
					nav.regionCount(),
					nav.builtRegions().size(),
					static_cast<unsigned long long>(nav.generation())
				);
				fflush(stdout);
				nextHeartbeat += kHeartbeat;
			}
		}
	}

	// One start, one goal: the reachFrom/reaches pair the AI runs over many goals.
	[[nodiscard]] inline bool isReachable(const NavigationSystem& nav, glm::vec2 start, glm::vec2 goal, float agentRadius,
										   geometry::nav::BeliefFilter belief = {}) {
		return nav.reaches(nav.reachFrom(start, agentRadius, belief), goal);
	}

	[[nodiscard]] inline bool pumpUntilMesh(NavigationSystem& nav) {
		return pumpUntil(nav, "the first mesh", [&nav] { return nav.hasMesh(); });
	}

} // namespace ecs
