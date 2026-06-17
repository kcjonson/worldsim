#pragma once

// The single place the navmesh pipeline crosses the meters<->millimeters boundary.
// Game data (entity positions, tile sizes) is float meters; committed geometry and
// the navmesh are integer millimeters (geometry::Vec2i64). Every conversion in the
// nav extractors routes through these inlines so the rounding rule lives in one
// spot and is trivially auditable. tile == meter in this game (kTileSize = 1.0), so
// a tile coordinate is a meter coordinate is a *1000 mm coordinate.

#include "world/chunk/ChunkCoordinate.h"

#include <core/Vec2i64.h>

#include <glm/vec2.hpp>

#include <cmath>
#include <cstdint>

namespace engine::nav {

	inline geometry::Vec2i64 toMm(glm::vec2 meters) {
		return {
			std::llround(static_cast<double>(meters.x) * static_cast<double>(geometry::kMillimetersPerMeter)),
			std::llround(static_cast<double>(meters.y) * static_cast<double>(geometry::kMillimetersPerMeter)),
		};
	}

	inline glm::vec2 toMeters(geometry::Vec2i64 mm) {
		return {
			static_cast<float>(static_cast<double>(mm.x) / static_cast<double>(geometry::kMillimetersPerMeter)),
			static_cast<float>(static_cast<double>(mm.y) / static_cast<double>(geometry::kMillimetersPerMeter)),
		};
	}

	// Chunk origin (its bottom-left tile corner) in mm. A chunk is kChunkSize tiles
	// on a side at kTileSize meters per tile; both are exact integers here, so the
	// origin is exact (no float intermediate). int64 multiply guards the large
	// products at far chunk coordinates.
	inline geometry::Vec2i64 chunkOriginMm(world::ChunkCoordinate coord) {
		const std::int64_t tilesPerChunk = static_cast<std::int64_t>(world::kChunkSize);
		return {
			static_cast<std::int64_t>(coord.x) * tilesPerChunk * geometry::kMillimetersPerMeter,
			static_cast<std::int64_t>(coord.y) * tilesPerChunk * geometry::kMillimetersPerMeter,
		};
	}

} // namespace engine::nav
