// Render tile seams (terrain-polygons-architecture.md D16 step 5): a chunk's render
// apron holds its neighbors' own render tiles, so the land field is one function of
// world position across every chunk border. The chunks are generated independently,
// the way ChunkManager does it, and compared: a dry grassland, and a river that
// crosses borders with mud on its banks, whose apron tiles are post-processed from
// each chunk's own terrain polygons (D11).

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/MockWorldSampler.h"
#include "world/chunk/RiverTestWorld.h"
#include "world/chunk/SurfaceField.h"

#include <core/IntegerDivision.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

using namespace engine::world;

namespace {

	using ChunkMap = std::map<std::pair<int32_t, int32_t>, std::unique_ptr<Chunk>>;

	// Chunks (0..1, 0..1) of a dry grassland: four grass variants and dirt patches.
	const ChunkMap& dryWorld() {
		static const ChunkMap built = [] {
			const MockWorldSampler sampler(12345);
			ChunkMap			   out;
			for (int32_t y = 0; y <= 1; ++y) {
				for (int32_t x = 0; x <= 1; ++x) {
					auto chunk = std::make_unique<Chunk>(ChunkCoordinate{x, y}, sampler.sampleChunk({x, y}), sampler.getWorldSeed());
					chunk->generate();
					out[{x, y}] = std::move(chunk);
				}
			}
			return out;
		}();
		return built;
	}

	const Chunk* chunkAt(const ChunkMap& chunks, int64_t cx, int64_t cy) {
		const auto it = chunks.find({static_cast<int32_t>(cx), static_cast<int32_t>(cy)});
		return it == chunks.end() ? nullptr : it->second.get();
	}

	/// The chunk whose own square holds world tile (tx, ty), if generated.
	const Chunk* ownerOf(const ChunkMap& chunks, int64_t tx, int64_t ty) {
		return chunkAt(chunks, geometry::floorDiv(tx, kChunkSize), geometry::floorDiv(ty, kChunkSize));
	}

	// Every render tile a chunk holds outside its own square (its render apron) equals
	// the tile's owner's own render tile, surface and edge byte alike, wherever the
	// owner was generated: across every border, straight and diagonal, both ways.
	void expectApronsEqualOwnersTiles(const ChunkMap& chunks) {
		int compared	   = 0;
		int expected	   = 0;
		int edgeTiles	   = 0;
		for (const auto& [coord, chunk] : chunks) {
			for (int32_t dy = -1; dy <= 1; ++dy) {
				for (int32_t dx = -1; dx <= 1; ++dx) {
					if ((dx != 0 || dy != 0) && chunkAt(chunks, coord.first + dx, coord.second + dy) != nullptr) {
						expected += dx != 0 && dy != 0 ? kRenderApronTiles * kRenderApronTiles : kChunkSize * kRenderApronTiles;
					}
				}
			}
			const RenderTileView view	 = chunk->renderTiles();
			const int64_t		 originX = static_cast<int64_t>(coord.first) * kChunkSize;
			const int64_t		 originY = static_cast<int64_t>(coord.second) * kChunkSize;
			for (int64_t ty = originY - kRenderApronTiles; ty < originY + kChunkSize + kRenderApronTiles; ++ty) {
				for (int64_t tx = originX - kRenderApronTiles; tx < originX + kChunkSize + kRenderApronTiles; ++tx) {
					const Chunk* owner = ownerOf(chunks, tx, ty);
					if (owner == chunk.get() || owner == nullptr) {
						continue;
					}
					const TileRenderData& mine	 = view.at(tx, ty);
					const TileRenderData& theirs = owner->renderTiles().at(tx, ty);
					ASSERT_EQ(mine, theirs) << "tile (" << tx << ", " << ty << ") in chunk (" << coord.first << ", " << coord.second << ")";
					edgeTiles += (mine.edge & kRenderInteriorBit) == 0 ? 1 : 0;
					++compared;
				}
			}
		}
		EXPECT_EQ(compared, expected);
		EXPECT_GT(edgeTiles, 0) << "the borders should cross some surface edges";
	}

	// Render tiles of a window filled from each tile's owner chunk: the world's
	// render tiles, not any one chunk's copy of them.
	struct Composite {
		int64_t						originX;
		int64_t						originY;
		int32_t						width;
		int32_t						height;
		std::vector<TileRenderData> tiles;

		[[nodiscard]] RenderTileView view() const { return {tiles, width, height, originX, originY}; }
	};

	Composite composite(const ChunkMap& chunks, int64_t originX, int64_t originY, int32_t width, int32_t height) {
		Composite c{originX, originY, width, height, {}};
		c.tiles.reserve(static_cast<size_t>(width) * static_cast<size_t>(height));
		for (int64_t ty = originY; ty < originY + height; ++ty) {
			for (int64_t tx = originX; tx < originX + width; ++tx) {
				c.tiles.push_back(ownerOf(chunks, tx, ty)->renderTiles().at(tx, ty));
			}
		}
		return c;
	}

	struct FieldTally {
		int compared = 0;
		int mud		 = 0; ///< points painted Mud
		int overWater = 0; ///< points on a Water tile (painted as its bed)
	};

	// Points within 2 m of every border between generated chunks, on both sides,
	// evaluate to the same bits from the owning chunk's render tiles as from the
	// world's (the composite). A point reads three tiles out, exactly the render
	// apron, so each side answers for its own square, and both answer with the one
	// world field.
	FieldTally expectFieldAgreesAcrossBorders(const ChunkMap& chunks, uint64_t worldSeed) {
		const SurfaceFieldParams params = defaultSurfaceFieldParams(worldSeed);
		constexpr int64_t		 kBand	= 8;

		FieldTally tally;
		auto	   check = [&](const RenderTileView& world, double x, double y) {
			  const TilePoint			p		= tilePointOf(x, y);
			  const Chunk*				owner	= ownerOf(chunks, p.x, p.y);
			  const SurfaceFieldSample fromOwn = evaluateSurfaceField(owner->renderTiles(), p, params);
			  const SurfaceFieldSample fromAll = evaluateSurfaceField(world, p, params);
			  EXPECT_EQ(fromOwn.surface, fromAll.surface) << "(" << x << ", " << y << ")";
			  EXPECT_EQ(fromOwn.field, fromAll.field) << "(" << x << ", " << y << ")";
			  EXPECT_EQ(fromOwn.warpXM, fromAll.warpXM) << "(" << x << ", " << y << ")";
			  EXPECT_EQ(fromOwn.warpYM, fromAll.warpYM) << "(" << x << ", " << y << ")";
			  const auto lx = static_cast<uint16_t>(p.x - geometry::floorDiv(p.x, kChunkSize) * kChunkSize);
			  const auto ly = static_cast<uint16_t>(p.y - geometry::floorDiv(p.y, kChunkSize) * kChunkSize);
			  tally.mud += fromOwn.surface == Surface::Mud ? 1 : 0;
			  tally.overWater += owner->getTile(lx, ly).surface == Surface::Water ? 1 : 0;
			  ++tally.compared;
		};

		for (const auto& [coord, chunk] : chunks) {
			const int64_t x0 = static_cast<int64_t>(coord.first) * kChunkSize;
			const int64_t y0 = static_cast<int64_t>(coord.second) * kChunkSize;
			// The east border x = x0 + 512 and the south border y = y0 + 512, each
			// sampled along its length at a depth sweeping [-2, 2) m.
			if (chunkAt(chunks, coord.first + 1, coord.second) != nullptr) {
				const int64_t	line = x0 + kChunkSize;
				const Composite east = composite(chunks, line - kBand, y0, 2 * kBand, kChunkSize);
				for (int k = 0; k < 2000; ++k) {
					check(east.view(), static_cast<double>(line) - 2.0 + 0.004 * (k % 1000), static_cast<double>(y0) + 4.0 + 0.2517 * k);
				}
			}
			if (chunkAt(chunks, coord.first, coord.second + 1) != nullptr) {
				const int64_t	line  = y0 + kChunkSize;
				const Composite south = composite(chunks, x0, line - kBand, kChunkSize, 2 * kBand);
				for (int k = 0; k < 2000; ++k) {
					check(south.view(), static_cast<double>(x0) + 4.0 + 0.2517 * k, static_cast<double>(line) - 2.0 + 0.004 * (k % 1000));
				}
			}
			// The southeast corner, where four chunks meet.
			if (chunkAt(chunks, coord.first + 1, coord.second) != nullptr && chunkAt(chunks, coord.first, coord.second + 1) != nullptr &&
				chunkAt(chunks, coord.first + 1, coord.second + 1) != nullptr) {
				const int64_t	cornerX = x0 + kChunkSize;
				const int64_t	cornerY = y0 + kChunkSize;
				const Composite corner	= composite(chunks, cornerX - kBand, cornerY - kBand, 2 * kBand, 2 * kBand);
				for (int j = 0; j < 80; ++j) {
					for (int i = 0; i < 80; ++i) {
						check(corner.view(), static_cast<double>(cornerX) - 2.0 + 0.05 * i + 0.013, static_cast<double>(cornerY) - 2.0 + 0.05 * j + 0.007);
					}
				}
			}
		}
		return tally;
	}

	using river_test::realRiverWorld;

} // namespace

// The dry world is what its comparison needs: land only, several surfaces along the
// borders.
TEST(RenderTileSeamsTest, DryWorldIsLandOfSeveralSurfaces) {
	std::set<uint8_t> surfaces;
	for (const auto& [coord, chunk] : dryWorld()) {
		for (uint16_t y = 0; y < kChunkSize; ++y) {
			for (uint16_t x = 0; x < kChunkSize; ++x) {
				const Surface s = chunk->getTile(x, y).surface;
				ASSERT_NE(s, Surface::Water) << "chunk (" << coord.first << ", " << coord.second << ")";
				surfaces.insert(static_cast<uint8_t>(s));
			}
		}
	}
	EXPECT_GE(surfaces.size(), 3U);
}

TEST(RenderTileSeamsTest, DryApronEqualsNeighborsOwnTiles) {
	expectApronsEqualOwnersTiles(dryWorld());
}

TEST(RenderTileSeamsTest, DryFieldAgreesAcrossBorders) {
	const FieldTally tally = expectFieldAgreesAcrossBorders(dryWorld(), 12345);
	// Two borders and the corner of the 2 x 2 block.
	EXPECT_EQ(tally.compared, 4 * 2000 + 80 * 80);
}

// The river world's borders carry what the water case needs: Water and Mud tiles
// within the render tiles' reach of a border, which the apron post-processes.
TEST(RenderTileSeamsTest, RiverWorldCrossesBordersWithWaterAndMud) {
	int water = 0;
	int mud	  = 0;
	for (const auto& [coord, chunk] : realRiverWorld().chunks) {
		for (int32_t y = 0; y < kChunkSize; ++y) {
			for (int32_t x = 0; x < kChunkSize; ++x) {
				if (std::min({x, y, kChunkSize - 1 - x, kChunkSize - 1 - y}) >= kRenderSurfaceReachTiles) {
					continue;
				}
				const Surface s = chunk->getTile(static_cast<uint16_t>(x), static_cast<uint16_t>(y)).surface;
				water += s == Surface::Water ? 1 : 0;
				mud += s == Surface::Mud ? 1 : 0;
			}
		}
	}
	EXPECT_GT(water, 100);
	EXPECT_GT(mud, 20);
}

TEST(RenderTileSeamsTest, RiverApronEqualsNeighborsOwnTiles) {
	expectApronsEqualOwnersTiles(realRiverWorld().chunks);
}

TEST(RenderTileSeamsTest, RiverFieldAgreesAcrossBorders) {
	const FieldTally tally = expectFieldAgreesAcrossBorders(realRiverWorld().chunks, realRiverWorld().worldSeed);
	std::cout << "[ river field seams ] " << tally.compared << " points, " << tally.mud << " painted mud, " << tally.overWater
			  << " over water tiles\n";
	// Seven borders and two corners of the 3 x 2 block.
	EXPECT_EQ(tally.compared, 7 * 2000 + 2 * 80 * 80);
	EXPECT_GT(tally.mud, 0);
	EXPECT_GT(tally.overWater, 0);
}
