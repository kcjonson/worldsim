#pragma once

// What the input tests share: a world whose seen record is the landing disc round the origin, and a
// camera over the origin that maps metres to screen pixels for the click handlers.

#include <ecs/World.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Transform.h>
#include <ecs/systems/PlayerVisibilitySystem.h>
#include <ecs/systems/VisionSystem.h>
#include <world/camera/WorldCamera.h>

#include <glm/vec2.hpp>

namespace world_sim::testing {

	constexpr int	kViewportWidth = 1000;
	constexpr int	kViewportHeight = 1000;
	constexpr float kPixelsPerMeter = 8.0F;
	constexpr float kSeenRadiusMeters = 40.0F;
	// Well inside the disc, and well outside it but still on screen.
	constexpr glm::vec2 kSeenPoint{10.0F, 5.0F};
	constexpr glm::vec2 kUnseenPoint{55.0F, 5.0F};

	struct SeenWorld {
		ecs::World					 world;
		ecs::VisionSystem&			 vision = world.registerSystem<ecs::VisionSystem>();
		ecs::PlayerVisibilitySystem& visibility = world.registerSystem<ecs::PlayerVisibilitySystem>();
		world::WorldCamera			 camera;

		SeenWorld() {
			camera.setPosition({0.0F, 0.0F});
			camera.setZoom(1.0F);
			visibility.revealLanding({0.0F, 0.0F}, kSeenRadiusMeters);
		}

		[[nodiscard]] glm::vec2 screenOf(glm::vec2 meters) const {
			const auto screen = camera.worldToScreen(meters.x, meters.y, kViewportWidth, kViewportHeight, kPixelsPerMeter);
			return {screen.x, screen.y};
		}

		// A player colonist who sees `sightRadius` round him once vision has run.
		ecs::EntityID addColonist(glm::vec2 at, float sightRadius) {
			const ecs::EntityID colonist = world.createEntity();
			world.addComponent<ecs::Position>(colonist, ecs::Position{at});
			world.addComponent<ecs::Colonist>(colonist, ecs::Colonist{"Tester"});
			ecs::Memory memory;
			memory.sightRadius = sightRadius;
			world.addComponent<ecs::Memory>(colonist, std::move(memory));
			return colonist;
		}

		// Ticks until vision has looked from where everyone stands.
		void look() {
			for (int tick = 0; tick < 12; ++tick) {
				world.update(0.016F);
			}
		}
	};

} // namespace world_sim::testing
