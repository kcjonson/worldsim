#pragma once

// VisionOverlay - scene-owned world-space debug overlay for colonist visibility
// polygons. Mirrors NavOverlay: plain class, same ctor pattern, setActive/isActive,
// render(w, h) calling Renderer::Primitives directly at kPixelsPerMeter.
//
// When active (V key) it draws for every entity with Position + Memory:
//   - Faint filled triangle fan from observer to each polygon edge (z 62)
//   - Polygon outline ring (z 64)
//   - GeometryIndex occluder segments in red (z 65)
//
// Outdoor observers (no cached polygon) are silently skipped.
// Off by default; GameScene toggles it with V.

#include <ecs/systems/VisionSystem.h>
#include <world/camera/WorldCamera.h>

namespace ecs {
	class World;
}

namespace world_sim {

	class VisionOverlay {
	  public:
		struct Args {
			ecs::World*					world;
			engine::world::WorldCamera* camera;
			ecs::VisionSystem*			vision;
		};

		VisionOverlay() = default;
		explicit VisionOverlay(const Args& args);

		void			   setActive(bool value) { active = value; }
		[[nodiscard]] bool isActive() const { return active; }

		/// Draw visibility polygons and occluder segments. No-op when inactive.
		void render(int viewportW, int viewportH);

	  private:
		ecs::World*					ecsWorld = nullptr;
		engine::world::WorldCamera* camera = nullptr;
		ecs::VisionSystem*			vision = nullptr;

		bool active = false;

		static constexpr float kPixelsPerMeter = 8.0F;
	};

} // namespace world_sim
