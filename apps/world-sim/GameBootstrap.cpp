#include "GameBootstrap.h"

#include <assets/AssetBootstrap.h>
#include <primitives/Primitives.h>
#include <resources/TileAtlasBuilder.h>
#include <utils/Log.h>
#include <world/chunk/Chunk.h>

#include <memory>

namespace world_sim {

	namespace {
		std::unique_ptr<Renderer::TileAtlasBuilder> tileAtlasBuilder;

		// Bootstrap a default tile atlas (solid colors per surface) to exercise tile mode.
		void initializeTileAtlas() {
			constexpr int kSurfaceCount = static_cast<int>(engine::world::Surface::Count);

			tileAtlasBuilder = std::make_unique<Renderer::TileAtlasBuilder>();
			auto rects = tileAtlasBuilder->buildForSurfaces(
				kSurfaceCount,
				[](int surfaceId) { return engine::world::surfaceToString(static_cast<engine::world::Surface>(surfaceId)); },
				[](int surfaceId) { return engine::world::Chunk::getSurfaceColor(static_cast<engine::world::Surface>(surfaceId)); }
			);

			if (!rects.empty()) {
				Renderer::Primitives::setTileAtlas(tileAtlasBuilder->texture(), rects);
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
		engine::assets::initializeAssetSystem(!hasSceneArg);
	}

	void shutdownResources() {
		tileAtlasBuilder.reset();
	}

} // namespace world_sim
