#pragma once

// TerrainPolygonOverlay - scene-owned world-space debug overlay that draws the
// per-chunk terrain polygons (waterline/channel/pond rings, D2/D4 in
// docs/technical/organic-terrain/terrain-polygons-architecture.md). Nothing else
// consumes Chunk::terrainPolygons() yet, so this is the only way to see the
// rings the builder produces.
//
// Mirrors NavOverlay: a plain class holding the WorldCamera* and the
// ChunkManager*, calling Renderer::Primitives directly and projecting
// world->screen via camera->worldToScreen at kPixelsPerMeter. Off by default;
// GameScene flips it with a hotkey.
//
// For every loaded, ready chunk intersecting the view it draws:
//   - the chunk's own 512 m square, thin and faint, so seams are easy to find
//   - `rings` edges (cyan), with synthetic apron-closure edges (D4, the start
//     vertex's ShoreProfile::kFlagSynthetic) in magenta instead
//   - `navRings` edges (yellow), the same rings clipped to the chunk square,
//     so a border where two chunks' navRings should meet is visible
//   - vertex dots, only once zoomed in enough that they would not just be
//     visual noise
//
// Z sits below the construction/room/nav/vision overlays: chunk outline 40,
// ring edges 42, navRing edges 44, vertex dots 46.

#include <world/camera/WorldCamera.h>
#include <world/chunk/ChunkManager.h>

namespace world_sim {

	class TerrainPolygonOverlay {
	  public:
		struct Args {
			engine::world::WorldCamera*	 camera;
			engine::world::ChunkManager* chunks;
		};

		TerrainPolygonOverlay() = default;
		explicit TerrainPolygonOverlay(const Args& args);

		void			   setActive(bool value) { active = value; }
		[[nodiscard]] bool isActive() const { return active; }

		/// Draw the terrain polygons of every loaded, ready chunk visible in the
		/// viewport. No-op when inactive or when no chunk manager is set.
		void render(int viewportW, int viewportH);

	  private:
		engine::world::WorldCamera*	 camera = nullptr;
		engine::world::ChunkManager* chunks = nullptr;

		bool active = false;

		static constexpr float kPixelsPerMeter = 8.0F;

		// Below this screen-pixels-per-meter, individual vertex dots would just
		// be noise; the ring edges alone still read fine at that zoom.
		static constexpr float kVertexDotMinPixelsPerMeter = 24.0F;
	};

} // namespace world_sim
