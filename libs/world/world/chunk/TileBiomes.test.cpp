// The per-tile biome pick (terrain-polygons-architecture.md, "Biome edges"): a pure function of the
// world tile, so every chunk that computes a tile agrees on it (D14), and its edges follow the noise,
// not the old 16 m sector grid.

#include "world/chunk/TileBiomes.h"

#include "world/chunk/ApronField.h"
#include "world/chunk/ChunkSampleResult.h"
#include "world/chunk/TerrainPolygonBuilder.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <vector>

namespace world {

	namespace {

		constexpr uint64_t kSeed = 0x5EED0000ABCD1234ULL;

		// A planet-like blend across a straight cell edge through (x0, y0) at `angle`: `above` on the side
		// the normal points to, `below` on the other, 50/50 on the edge and pure 500 m out
		// (PlanetSampler's blend band).
		struct CellEdge {
			double x0 = 0.0;
			double y0 = 0.0;
			double angle = 0.0;
			Biome  above = Biome::SemiDesert;
			Biome  below = Biome::TemperateGrassland;

			[[nodiscard]] BiomeWeights at(WorldPosition p) const {
				const double d = (p.x - x0) * std::cos(angle) + (p.y - y0) * std::sin(angle);
				const double t = std::min(1.0, std::abs(d) / 500.0);
				const auto	 near = static_cast<float>(0.5 * (1.0 + t));
				BiomeWeights weights;
				weights.set(d >= 0.0 ? above : below, near);
				weights.set(d >= 0.0 ? below : above, 1.0F - near);
				weights.normalize();
				return weights;
			}
		};

		using BiomeField = std::function<BiomeWeights(WorldPosition)>;

		ChunkSampleResult sampleOf(ChunkCoordinate coord, const BiomeField& biomeAt) {
			ChunkSampleResult				 sample;
			const std::array<ChunkCorner, 4> corners = {
				ChunkCorner::NorthWest, ChunkCorner::NorthEast, ChunkCorner::SouthWest, ChunkCorner::SouthEast
			};
			for (size_t i = 0; i < corners.size(); ++i) {
				sample.cornerBiomes[i] = biomeAt(coord.corner(corners[i]));
				sample.cornerElevations[i] = 10.0F;
			}
			fillNeighborhoodCorners(sample, coord, biomeAt, [](WorldPosition) { return 10.0F; });
			return sample;
		}

		// A grassland/semi-desert edge crossing chunk (0, 0) diagonally, and an ocean edge north of it whose
		// blend band reaches the chunks around (0, -1), so some chunks hold three biomes and a water pair.
		BiomeWeights threeBiomeWorld(WorldPosition p) {
			const CellEdge	   land{.x0 = 256.0, .y0 = 300.0, .angle = 0.6};
			const CellEdge	   coast{.x0 = 0.0, .y0 = -250.0, .angle = -1.9, .above = Biome::Ocean, .below = Biome::TemperateGrassland};
			const BiomeWeights inland = land.at(p);
			const BiomeWeights sea = coast.at(p);
			const float		   ocean = sea.get(Biome::Ocean);
			if (ocean <= 0.0F) {
				return inland;
			}
			BiomeWeights weights;
			weights.set(Biome::Ocean, ocean);
			weights.set(Biome::TemperateGrassland, inland.get(Biome::TemperateGrassland) * (1.0F - ocean));
			weights.set(Biome::SemiDesert, inland.get(Biome::SemiDesert) * (1.0F - ocean));
			weights.normalize();
			return weights;
		}

	} // namespace

	// A tile read from a neighbor's apron, through the generating chunk's corner lattice, is the tile its
	// owner picks from its own corners, along every border and corner of several chunks, the edge
	// crossing them and the noise and ecotone in play.
	TEST(TileBiomesTest, ApronTilesEqualTheOwnersOwn) {
		std::set<Biome> seen;
		size_t			compared = 0;
		size_t			water = 0;
		for (const ChunkCoordinate center :
			 {ChunkCoordinate{0, 0}, ChunkCoordinate{1, 0}, ChunkCoordinate{0, -1}, ChunkCoordinate{-1, 1}}) {
			const ChunkSampleResult sample = sampleOf(center, threeBiomeWorld);
			ChunkNeighborhood		neighborhood(center, sample, kSeed);
			for (int32_t dy = -1; dy <= 1; ++dy) {
				for (int32_t dx = -1; dx <= 1; ++dx) {
					if (dx == 0 && dy == 0) {
						continue;
					}
					const ChunkCoordinate	owner{center.x + dx, center.y + dy};
					const ChunkSampleResult ownSample = sampleOf(owner, threeBiomeWorld);
					const ChunkBiomes		own(owner, ownSample.cornerBiomes, kSeed);
					// The owner's tiles within the apron's reach of the center chunk.
					const int32_t x0 = dx < 0 ? kChunkSize - kApronTiles : 0;
					const int32_t x1 = dx > 0 ? kApronTiles : kChunkSize;
					const int32_t y0 = dy < 0 ? kChunkSize - kApronTiles : 0;
					const int32_t y1 = dy > 0 ? kApronTiles : kChunkSize;
					for (int32_t ly = y0; ly < y1; ++ly) {
						for (int32_t lx = x0; lx < x1; ++lx) {
							const int64_t	tx = static_cast<int64_t>(owner.x) * kChunkSize + lx;
							const int64_t	ty = static_cast<int64_t>(owner.y) * kChunkSize + ly;
							const TileBiome apron = neighborhood.tileBiomeAt(tx, ty);
							const TileBiome mine = own.at(static_cast<uint16_t>(lx), static_cast<uint16_t>(ly));
							ASSERT_EQ(apron.primary, mine.primary) << "tile " << tx << ", " << ty;
							ASSERT_EQ(apron.secondary, mine.secondary) << "tile " << tx << ", " << ty;
							ASSERT_EQ(apron.blend, mine.blend) << "tile " << tx << ", " << ty;
							ASSERT_EQ(neighborhood.isBiomeWaterAt(tx, ty), isBiomeWater(mine.primary)) << "tile " << tx << ", " << ty;
							seen.insert(mine.primary);
							water += isBiomeWater(mine.primary) ? 1 : 0;
							++compared;
						}
					}
				}
			}
		}
		// Only worth something if the borders cross the edges.
		EXPECT_EQ(seen, (std::set<Biome>{Biome::TemperateGrassland, Biome::SemiDesert, Biome::Ocean}));
		EXPECT_GT(water, 1000U);
		EXPECT_EQ(compared, 4U * (4U * kApronTiles * kChunkSize + 4U * kApronTiles * kApronTiles));
	}

	// The edge across a chunk is no longer the sector grid's staircase: about one boundary edge in
	// sixteen falls on a sector line, as it would anywhere, and none runs along one for long (5 tiles
	// at most here). The old pick put every boundary edge on a sector line, in runs of 16.
	TEST(TileBiomesTest, EdgesDoNotFollowTheSectorGrid) {
		const CellEdge	edge{.x0 = 256.0, .y0 = 256.0, .angle = 0.6};
		size_t			boundary = 0;
		size_t			onSectorLine = 0;
		int32_t			longestRun = 0;
		std::set<Biome> seen;
		for (const ChunkCoordinate coord : {ChunkCoordinate{0, 0}, ChunkCoordinate{-1, 1}, ChunkCoordinate{1, -1}}) {
			const ChunkBiomes  biomes(coord, sampleOf(coord, [&edge](WorldPosition p) { return edge.at(p); }).cornerBiomes, kSeed);
			std::vector<Biome> tiles(static_cast<size_t>(kChunkSize) * kChunkSize);
			for (uint16_t y = 0; y < kChunkSize; ++y) {
				for (uint16_t x = 0; x < kChunkSize; ++x) {
					tiles[static_cast<size_t>(y) * kChunkSize + x] = biomes.at(x, y).primary;
					seen.insert(tiles[static_cast<size_t>(y) * kChunkSize + x]);
				}
			}
			auto biomeAt = [&tiles](int32_t x, int32_t y) {
				return tiles[static_cast<size_t>(y) * kChunkSize + static_cast<size_t>(x)];
			};
			// Boundary edges between horizontal and between vertical neighbors, and the runs of them
			// along each sector line (x or y a multiple of 16).
			for (int32_t line = 1; line < kChunkSize; ++line) {
				int32_t runX = 0;
				int32_t runY = 0;
				for (int32_t along = 0; along < kChunkSize; ++along) {
					const bool crossX = biomeAt(line - 1, along) != biomeAt(line, along);
					const bool crossY = biomeAt(along, line - 1) != biomeAt(along, line);
					const bool sector = line % 16 == 0;
					boundary += (crossX ? 1 : 0) + (crossY ? 1 : 0);
					onSectorLine += sector ? (crossX ? 1 : 0) + (crossY ? 1 : 0) : 0;
					runX = (sector && crossX) ? runX + 1 : 0;
					runY = (sector && crossY) ? runY + 1 : 0;
					longestRun = std::max({longestRun, runX, runY});
				}
			}
		}
		ASSERT_EQ(seen, (std::set<Biome>{Biome::TemperateGrassland, Biome::SemiDesert}));
		ASSERT_GT(boundary, 1000U);
		EXPECT_LT(static_cast<double>(onSectorLine) / static_cast<double>(boundary), 2.0 / 16.0);
		EXPECT_LE(longestRun, 10);
	}

} // namespace world
