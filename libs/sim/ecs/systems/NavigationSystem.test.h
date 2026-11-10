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

#include "../World.h"
#include "../components/Colonist.h"
#include "../components/Transform.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

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

	[[nodiscard]] inline bool pumpUntilMesh(NavigationSystem& nav) {
		return pumpUntil(nav, "the first mesh", [&nav] { return nav.hasMesh(); });
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
