#pragma once

// NavOverlay - scene-owned world-space overlay that makes the navmesh and live
// agent routes visible for debugging. The NavigationSystem owns the cached mesh
// and the per-entity NavPath components hold each colonist's current route;
// neither draws anything on its own. When toggled on (N), this draws the mesh
// triangle edges and each valid NavPath's waypoint polyline.
//
// Mirrors RoomOverlay: a plain class holding ecs::World*, the WorldCamera*, and
// the source system, calling Renderer::Primitives directly and projecting
// world->screen via camera->worldToScreen at kPixelsPerMeter. Off by default;
// GameScene flips it with a hotkey.
//
// Z sits above wall bands (~60-64): mesh edges 66, path polyline 70, current
// waypoint marker 71.

#include <ecs/systems/NavigationSystem.h>
#include <world/camera/WorldCamera.h>

namespace ecs {
	class World;
}

namespace world_sim {

	class NavOverlay {
	  public:
		struct Args {
			ecs::World*					world;
			engine::world::WorldCamera* camera;
			ecs::NavigationSystem*		navigation;
		};

		NavOverlay() = default;
		explicit NavOverlay(const Args& args);

		void			   setActive(bool value) { active = value; }
		[[nodiscard]] bool isActive() const { return active; }

		/// Draw the navmesh triangle edges and every valid NavPath route in world
		/// space. No-op when inactive or when no mesh has been built yet.
		void render(int viewportW, int viewportH);

	  private:
		ecs::World*					ecsWorld = nullptr;
		engine::world::WorldCamera* camera = nullptr;
		ecs::NavigationSystem*		navigation = nullptr;

		bool active = false;

		static constexpr float kPixelsPerMeter = 8.0F;
	};

} // namespace world_sim
