#include "TilePostProcessor.h"

#include "world/chunk/Chunk.h"
#include "world/chunk/TerrainPolygonBuilder.h"
#include "world/chunk/TerrainPolygonQuery.h"

#include <core/IntegerDivision.h>
#include <core/Vec2i64.h>

namespace engine::world {

Surface TilePostProcessor::finalSurface(const FinalSurfaceArgs& args) {
	if (args.terrain->isPointBar(args.tileX, args.tileY)) {
		return Surface::Sand;
	}
	// Mud forms on any grass variant, dirt, or sand. Sand matters for ponds and
	// oases in deserts/beaches: without it those water bodies get no bank, and
	// the riparian flora keyed on near="Mud" (reeds, bankside bushes) can't seat.
	const Surface raw = args.raw;
	if (raw != Surface::Grass && raw != Surface::Dirt && raw != Surface::Sand && raw != Surface::GrassTall &&
		raw != Surface::GrassShort && raw != Surface::GrassMeadow) {
		return raw;
	}
	const geometry::Vec2i64 center{
		args.tileX * TerrainPolygonBuilder::kTileMm + TerrainPolygonBuilder::kTileMm / 2,
		args.tileY * TerrainPolygonBuilder::kTileMm + TerrainPolygonBuilder::kTileMm / 2
	};
	const double distanceMm = args.terrain->distanceToWaterMm(center, kMudBands.back().reachMm);
	for (const MudBand& band : kMudBands) {
		if (distanceMm > band.reachMm) {
			continue;
		}
		// Keyed by the world tile (its owning chunk and local coordinates), so any
		// chunk that evaluates the tile rolls the same number.
		const ChunkCoordinate owner{
			static_cast<int32_t>(geometry::floorDiv(args.tileX, kChunkSize)), static_cast<int32_t>(geometry::floorDiv(args.tileY, kChunkSize))
		};
		const auto localX = static_cast<uint16_t>(args.tileX - static_cast<int64_t>(owner.x) * kChunkSize);
		const auto localY = static_cast<uint16_t>(args.tileY - static_cast<int64_t>(owner.y) * kChunkSize);
		const uint32_t hash = Chunk::tileHash(owner, localX, localY, args.worldSeed ^ kMudSalt);
		const double roll = static_cast<double>(hash) / 4294967296.0;
		return roll < band.probability ? Surface::Mud : raw;
	}
	return raw;
}

void TilePostProcessor::process(std::array<TileData, kChunkSize * kChunkSize>& tiles, const ProcessArgs& args) {
	const int64_t originX = static_cast<int64_t>(args.coord.x) * kChunkSize;
	const int64_t originY = static_cast<int64_t>(args.coord.y) * kChunkSize;
	for (int32_t y = 0; y < kChunkSize; ++y) {
		for (int32_t x = 0; x < kChunkSize; ++x) {
			TileData& tile = tiles[static_cast<size_t>(y) * kChunkSize + static_cast<size_t>(x)];
			tile.surface = finalSurface({
				.raw = tile.surface,
				.tileX = originX + x,
				.tileY = originY + y,
				.terrain = args.terrain,
				.worldSeed = args.worldSeed,
			});
		}
	}
}

}  // namespace engine::world
