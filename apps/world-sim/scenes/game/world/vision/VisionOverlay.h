#pragma once

// VisionOverlay - scene-owned world-space debug overlay for colonist visibility
// polygons. Mirrors NavOverlay: plain class, same ctor pattern, setActive/isActive,
// render(w, h) calling Renderer::Primitives directly at kPixelsPerMeter.
//
// When active (V key) it draws for every entity with Position + Memory (sight-softening.md 5.2):
//   - His sight shape as a faint cyan fan (z 62)
//   - The forest shape of his sight where he stands in green, and on the polygon path the hard
//     polygon the opaque bodies leave, cut to his sight, in cyan (z 64)
//   - His sight bodies, opaque in red and soft in yellow, and the soft ray's width as a magenta
//     circle that wide round the build eye (z 65)
//
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
			world::WorldCamera*			camera;
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
		world::WorldCamera*			camera = nullptr;
		ecs::VisionSystem*			vision = nullptr;

		bool active = false;

		static constexpr float kPixelsPerMeter = 8.0F;
	};

} // namespace world_sim
