#include "GameBootstrap.h"

#include <assets/AssetBootstrap.h>
#include <primitives/Primitives.h>
#include <resources/TileAtlasBuilder.h>
#include <utils/Log.h>
#include <world/chunk/Chunk.h>

#include <memory>
#include <string>

namespace world_sim {

	namespace {
		std::unique_ptr<Renderer::TileAtlasBuilder> g_tileAtlasBuilder;

		// Resource path of the surface's SVG pattern. Sand, Rock, Water, Snow and Mud ship none (see
		// docs/technical/ground-textures.md) and get "", which the builder takes as no pattern; for any
		// other surface it warns when the file is missing.
		std::string surfacePatternPath(world::Surface surface) {
			switch (surface) {
				case world::Surface::Sand:
				case world::Surface::Rock:
				case world::Surface::Water:
				case world::Surface::Snow:
				case world::Surface::Mud:
					return {};
				default:
					return "assets/tiles/surfaces/" + world::surfaceToString(surface) + "/pattern.svg";
			}
		}

		// Bootstrap a default tile atlas (solid colors per surface) to exercise tile mode.
		void initializeTileAtlas() {
			constexpr int kSurfaceCount = static_cast<int>(world::Surface::Count);

			g_tileAtlasBuilder = std::make_unique<Renderer::TileAtlasBuilder>();
			auto rects = g_tileAtlasBuilder->buildForSurfaces(
				kSurfaceCount,
				[](int surfaceId) { return surfacePatternPath(static_cast<world::Surface>(surfaceId)); },
				[](int surfaceId) { return world::Chunk::getSurfaceColor(static_cast<world::Surface>(surfaceId)); }
			);

			if (!rects.empty()) {
				Renderer::Primitives::setTileAtlas(g_tileAtlasBuilder->texture(), rects);
				LOG_INFO(Renderer, "Initialized default tile atlas with %zu entries", rects.size());
			} else {
				LOG_WARNING(Renderer, "Tile atlas not initialized (no rects)");
			}
		}
	} // namespace

	void initializeResources(bool hasSceneArg) {
		initializeTileAtlas();
		// Async only on the normal splash path; a --scene override loads synchronously so
		// the jumped-to scene has assets ready before its onEnter.
		assets::initializeAssetSystem(!hasSceneArg);
	}

	void shutdownResources() {
		g_tileAtlasBuilder.reset();
	}

} // namespace world_sim
