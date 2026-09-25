#include "ApronField.h"

#include <array>
#include <cassert>

namespace engine::world {

	namespace {

		// Index into a 3x3 array of neighbor offsets, dx/dy each in [-1, 1].
		size_t neighborIndex(int32_t dx, int32_t dy) {
			return static_cast<size_t>((dy + 1) * 3 + (dx + 1));
		}

	} // namespace

	ApronField ApronField::build(ChunkCoordinate coord, const ChunkSampleResult& sampleData, uint64_t worldSeed) {
		ApronField field;
		field.m_top.resize(static_cast<size_t>(kExtendedSize) * static_cast<size_t>(kApronTiles));
		field.m_bottom.resize(static_cast<size_t>(kExtendedSize) * static_cast<size_t>(kApronTiles));
		field.m_left.resize(static_cast<size_t>(kApronTiles) * static_cast<size_t>(kChunkSize));
		field.m_right.resize(static_cast<size_t>(kApronTiles) * static_cast<size_t>(kChunkSize));

		// Each of the 8 neighbors' own biome/elevation grid, built once from the
		// sample data's neighborhood corner lattice (D4) and reused for every apron
		// tile that neighbor owns. Lazily filled; (0,0) [this chunk] is unused.
		std::array<ChunkSampleResult, 9> neighborGrids{};
		std::array<bool, 9> built{};
		auto neighborGrid = [&](int32_t dx, int32_t dy) -> const ChunkSampleResult& {
			const size_t idx = neighborIndex(dx, dy);
			if (!built[idx]) {
				neighborGrids[idx].cornerBiomes = sampleData.neighborCornerBiomes(dx, dy);
				neighborGrids[idx].cornerElevations = sampleData.neighborCornerElevations(dx, dy);
				neighborGrids[idx].computeSectorGrid();
				built[idx] = true;
			}
			return neighborGrids[idx];
		};

		// Compute one extended-region tile: find the neighbor chunk that owns it,
		// resolve biome/elevation from that neighbor's own grid, and run it through
		// the same tile-compute core the neighbor's own generate() would use.
		// Hydrology (river/pond) always comes from `sampleData` itself: it is a
		// world-position query already gathered over the extended AABB (D4), so it
		// covers every apron tile regardless of which neighbor conceptually owns it.
		auto computeExtended = [&](int32_t ex, int32_t ey) -> TileData {
			const int32_t lx = ex - kApronTiles;
			const int32_t ly = ey - kApronTiles;
			const int32_t dx = (lx < 0) ? -1 : ((lx >= kChunkSize) ? 1 : 0);
			const int32_t dy = (ly < 0) ? -1 : ((ly >= kChunkSize) ? 1 : 0);
			assert((dx != 0 || dy != 0) && "extended coordinate falls inside the chunk's own square");
			const auto nx = static_cast<uint16_t>(lx - dx * kChunkSize);
			const auto ny = static_cast<uint16_t>(ly - dy * kChunkSize);
			const ChunkCoordinate neighborCoord{coord.x + dx, coord.y + dy};
			const ChunkSampleResult& grid = neighborGrid(dx, dy);

			return Chunk::computeTileFrom({
				.coord = neighborCoord,
				.localX = nx,
				.localY = ny,
				.biomeWeights = grid.getTileBiome(nx, ny),
				.elevationMeters = grid.getTileElevation(nx, ny),
				.hydrology = &sampleData,
				.worldSeed = worldSeed,
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

} // namespace engine::world
