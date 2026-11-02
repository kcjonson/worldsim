#include "ApronField.h"

#include "world/chunk/TerrainPolygonBuilder.h"

#include <array>
#include <cassert>

namespace world {

	namespace {

		// Index into a 3x3 array of neighbor offsets, dx/dy each in [-1, 1].
		size_t neighborIndex(int32_t dx, int32_t dy) {
			return static_cast<size_t>((dy + 1) * 3 + (dx + 1));
		}

	} // namespace

	ChunkNeighborhood::ChunkNeighborhood(ChunkCoordinate coord, const ChunkSampleResult& sampleData, uint64_t worldSeed)
		: center(coord),
		  sample(sampleData),
		  seed(worldSeed) {}

	const ChunkBiomes& ChunkNeighborhood::biomes(int32_t dx, int32_t dy) {
		assert(dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1);
		std::optional<ChunkBiomes>& chunk = chunks[neighborIndex(dx, dy)];
		if (!chunk) {
			const std::array<BiomeWeights, 4> corners = (dx == 0 && dy == 0) ? sample.cornerBiomes : sample.neighborCornerBiomes(dx, dy);
			chunk.emplace(ChunkCoordinate{center.x + dx, center.y + dy}, corners, seed);
		}
		return *chunk;
	}

	float ChunkNeighborhood::tileElevation(int32_t dx, int32_t dy, uint16_t localX, uint16_t localY) const {
		const std::array<float, 4> corners = (dx == 0 && dy == 0) ? sample.cornerElevations : sample.neighborCornerElevations(dx, dy);
		const float				   u = static_cast<float>(localX) / static_cast<float>(kChunkSize - 1);
		const float				   v = static_cast<float>(localY) / static_cast<float>(kChunkSize - 1);
		const float				   top = corners[0] * (1.0F - u) + corners[1] * u;
		const float				   bottom = corners[2] * (1.0F - u) + corners[3] * u;
		return top * (1.0F - v) + bottom * v;
	}

	ChunkNeighborhood::Owner ChunkNeighborhood::ownerOf(int64_t tx, int64_t ty) const {
		const int64_t lx = tx - static_cast<int64_t>(center.x) * kChunkSize;
		const int64_t ly = ty - static_cast<int64_t>(center.y) * kChunkSize;
		assert(lx >= -kChunkSize && lx < 2 * kChunkSize && ly >= -kChunkSize && ly < 2 * kChunkSize);
		const int32_t dx = (lx < 0) ? -1 : ((lx >= kChunkSize) ? 1 : 0);
		const int32_t dy = (ly < 0) ? -1 : ((ly >= kChunkSize) ? 1 : 0);
		return {
			.dx = dx,
			.dy = dy,
			.localX = static_cast<uint16_t>(lx - dx * kChunkSize),
			.localY = static_cast<uint16_t>(ly - dy * kChunkSize),
		};
	}

	TileBiome ChunkNeighborhood::tileBiomeAt(int64_t tx, int64_t ty) {
		const Owner owner = ownerOf(tx, ty);
		return biomes(owner.dx, owner.dy).at(owner.localX, owner.localY);
	}

	bool ChunkNeighborhood::isBiomeWaterAt(int64_t tx, int64_t ty) {
		const Owner		   owner = ownerOf(tx, ty);
		const ChunkBiomes& chunk = biomes(owner.dx, owner.dy);
		return chunk.anyWater() && isBiomeWater(chunk.at(owner.localX, owner.localY).primary);
	}

	ApronField ApronField::build(ChunkNeighborhood& neighborhood, const ChunkSampleResult& hydrology) {
		ApronField field;
		field.m_top.resize(static_cast<size_t>(kExtendedSize) * static_cast<size_t>(kApronTiles));
		field.m_bottom.resize(static_cast<size_t>(kExtendedSize) * static_cast<size_t>(kApronTiles));
		field.m_left.resize(static_cast<size_t>(kApronTiles) * static_cast<size_t>(kChunkSize));
		field.m_right.resize(static_cast<size_t>(kApronTiles) * static_cast<size_t>(kChunkSize));

		// Compute one extended-region tile: find the neighbor chunk that owns it,
		// take its biome and elevation as that neighbor computes them, and run it
		// through the same tile-compute core the neighbor's own generate() uses.
		// Hydrology (river/pond) always comes from `hydrology`: it is a
		// world-position query already gathered over the extended AABB (D4), so it
		// covers every apron tile regardless of which neighbor conceptually owns it.
		const ChunkCoordinate coord = neighborhood.coord();
		auto				  computeExtended = [&](int32_t ex, int32_t ey) -> TileData {
			 const int32_t lx = ex - kApronTiles;
			 const int32_t ly = ey - kApronTiles;
			 const int32_t dx = (lx < 0) ? -1 : ((lx >= kChunkSize) ? 1 : 0);
			 const int32_t dy = (ly < 0) ? -1 : ((ly >= kChunkSize) ? 1 : 0);
			 assert((dx != 0 || dy != 0) && "extended coordinate falls inside the chunk's own square");
			 const auto nx = static_cast<uint16_t>(lx - dx * kChunkSize);
			 const auto ny = static_cast<uint16_t>(ly - dy * kChunkSize);
			 return Chunk::computeTileFrom({
				 .coord = {coord.x + dx, coord.y + dy},
				 .localX = nx,
				 .localY = ny,
				 .biome = neighborhood.biomes(dx, dy).at(nx, ny),
				 .elevationMeters = neighborhood.tileElevation(dx, dy, nx, ny),
				 .hydrology = &hydrology,
				 .worldSeed = neighborhood.worldSeed(),
			 });
		};

		for (int32_t ey = 0; ey < kApronTiles; ++ey) {
			for (int32_t ex = 0; ex < kExtendedSize; ++ex) {
				field.m_top[static_cast<size_t>(ey * kExtendedSize + ex)] = computeExtended(ex, ey);
			}
		}
		for (int32_t ey = kApronTiles + kChunkSize; ey < kExtendedSize; ++ey) {
			const int32_t row = ey - (kApronTiles + kChunkSize);
			for (int32_t ex = 0; ex < kExtendedSize; ++ex) {
				field.m_bottom[static_cast<size_t>(row * kExtendedSize + ex)] = computeExtended(ex, ey);
			}
		}
		for (int32_t ey = kApronTiles; ey < kApronTiles + kChunkSize; ++ey) {
			const int32_t row = ey - kApronTiles;
			for (int32_t ex = 0; ex < kApronTiles; ++ex) {
				field.m_left[static_cast<size_t>(row * kApronTiles + ex)] = computeExtended(ex, ey);
			}
		}
		for (int32_t ey = kApronTiles; ey < kApronTiles + kChunkSize; ++ey) {
			const int32_t row = ey - kApronTiles;
			for (int32_t ex = kApronTiles + kChunkSize; ex < kExtendedSize; ++ex) {
				const int32_t col = ex - (kApronTiles + kChunkSize);
				field.m_right[static_cast<size_t>(row * kApronTiles + col)] = computeExtended(ex, ey);
			}
		}

		return field;
	}

	const TileData& ApronField::tileAt(int32_t ex, int32_t ey) const {
		assert(ex >= 0 && ex < kExtendedSize && ey >= 0 && ey < kExtendedSize);
		if (ey < kApronTiles) {
			return m_top[static_cast<size_t>(ey * kExtendedSize + ex)];
		}
		if (ey >= kApronTiles + kChunkSize) {
			return m_bottom[static_cast<size_t>((ey - (kApronTiles + kChunkSize)) * kExtendedSize + ex)];
		}
		if (ex < kApronTiles) {
			return m_left[static_cast<size_t>((ey - kApronTiles) * kApronTiles + ex)];
		}
		assert(ex >= kApronTiles + kChunkSize && "(ex, ey) is inside the chunk's own square, not the apron");
		return m_right[static_cast<size_t>((ey - kApronTiles) * kApronTiles + (ex - (kApronTiles + kChunkSize)))];
	}

	const TileData& ExtendedTiles::at(int32_t ex, int32_t ey) const {
		const int32_t lx = ex - kApronTiles;
		const int32_t ly = ey - kApronTiles;
		if (lx >= 0 && lx < kChunkSize && ly >= 0 && ly < kChunkSize) {
			return m_chunk.getTile(static_cast<uint16_t>(lx), static_cast<uint16_t>(ly));
		}
		return m_apron.tileAt(ex, ey);
	}

} // namespace world
