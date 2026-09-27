// Render tile seams (terrain-polygons-architecture.md D16 step 5): a chunk's render
// apron holds its neighbors' own render tiles, so the land field is one function of
// world position across every chunk border. The chunks are generated independently,
// the way ChunkManager does it, and compared.

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/MockWorldSampler.h"
#include "world/chunk/SurfaceField.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace engine::world;

namespace {

	// A dry grassland: four grass variants and dirt patches, no water (so no mud:
	// the apron's tiles are raw computeTile output, which equals a neighbor's own
	// tiles everywhere its post-processing did nothing).
	constexpr uint64_t		  kSeed = 12345;
	const ChunkCoordinate kOrigin{0, 0};

	std::unique_ptr<Chunk> generateChunk(const MockWorldSampler& sampler, ChunkCoordinate coord) {
		auto chunk = std::make_unique<Chunk>(coord, sampler.sampleChunk(coord), sampler.getWorldSeed());
		chunk->generate();
		return chunk;
	}

	int64_t floorDiv(int64_t a, int64_t b) {
		return a >= 0 ? a / b : -((-a + b - 1) / b);
	}

	class RenderTileSeamsTest : public ::testing::Test {
	  protected:
		static void SetUpTestSuite() {
			const MockWorldSampler sampler(kSeed);
			chunks = new std::map<std::pair<int32_t, int32_t>, std::unique_ptr<Chunk>>();
			for (int32_t dy = 0; dy <= 1; ++dy) {
				for (int32_t dx = 0; dx <= 1; ++dx) {
					(*chunks)[{kOrigin.x + dx, kOrigin.y + dy}] = generateChunk(sampler, {kOrigin.x + dx, kOrigin.y + dy});
				}
			}
		}

		static void TearDownTestSuite() {
			delete chunks;
			chunks = nullptr;
		}

		static const Chunk* chunkAt(int32_t cx, int32_t cy) {
			const auto it = chunks->find({cx, cy});
			return it == chunks->end() ? nullptr : it->second.get();
		}

		/// The chunk whose own square holds world tile (tx, ty), if generated.
		static const Chunk* ownerOf(int64_t tx, int64_t ty) {
			return chunkAt(static_cast<int32_t>(floorDiv(tx, kChunkSize)), static_cast<int32_t>(floorDiv(ty, kChunkSize)));
		}

		static std::map<std::pair<int32_t, int32_t>, std::unique_ptr<Chunk>>* chunks;
	};

	std::map<std::pair<int32_t, int32_t>, std::unique_ptr<Chunk>>* RenderTileSeamsTest::chunks = nullptr;

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

} // namespace

// The test world is what the comparison needs: land only, several surfaces along
// the borders.
TEST_F(RenderTileSeamsTest, WorldIsDryAndMixed) {
	std::set<uint8_t> surfaces;
	for (const auto& [coord, chunk] : *chunks) {
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

// Every render tile a chunk holds outside its own square (its render apron) equals
// the tile's owner's own render tile, surface and edge byte alike: across the
// east, south, and diagonal borders, in both directions.
TEST_F(RenderTileSeamsTest, ApronEqualsNeighborsOwnTiles) {
	int compared = 0;
	int edgeTiles = 0;
	for (const auto& [coord, chunk] : *chunks) {
		const RenderTileView view	 = chunk->renderTiles();
		const int64_t		 originX = static_cast<int64_t>(coord.first) * kChunkSize;
		const int64_t		 originY = static_cast<int64_t>(coord.second) * kChunkSize;
		for (int64_t ty = originY - kRenderApronTiles; ty < originY + kChunkSize + kRenderApronTiles; ++ty) {
			for (int64_t tx = originX - kRenderApronTiles; tx < originX + kChunkSize + kRenderApronTiles; ++tx) {
				const Chunk* owner = ownerOf(tx, ty);
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
	// Each chunk sees two full-length borders and one corner of its three neighbors.
	EXPECT_EQ(compared, 4 * (2 * kChunkSize * kRenderApronTiles + kRenderApronTiles * kRenderApronTiles));
	EXPECT_GT(edgeTiles, 0) << "the borders should cross some surface edges";
}

// Points within 2 m of each shared border, on both sides, evaluate to the same bits
// from the owning chunk's render tiles as from the world's (the composite). A point
// reads three tiles out, exactly the render apron, so each side answers for its own
// square, and both answer with the one world field.
TEST_F(RenderTileSeamsTest, FieldAgreesAcrossBorders) {
	const SurfaceFieldParams params = defaultSurfaceFieldParams(kSeed);
	constexpr int64_t		 kBand	= 8;

	auto composite = [](int64_t originX, int64_t originY, int32_t width, int32_t height) {
		Composite c{originX, originY, width, height, {}};
		c.tiles.reserve(static_cast<size_t>(width) * static_cast<size_t>(height));
		for (int64_t ty = originY; ty < originY + height; ++ty) {
			for (int64_t tx = originX; tx < originX + width; ++tx) {
				c.tiles.push_back(ownerOf(tx, ty)->renderTiles().at(tx, ty));
			}
		}
		return c;
	};

	int	 compared = 0;
	auto check	  = [&](const RenderTileView& world, double x, double y) {
		  const TilePoint			p		= tilePointOf(x, y);
		  const Chunk*			owner	= ownerOf(p.x, p.y);
		  const SurfaceFieldSample fromOwn = evaluateSurfaceField(owner->renderTiles(), p, params);
		  const SurfaceFieldSample fromAll = evaluateSurfaceField(world, p, params);
		  EXPECT_EQ(fromOwn.surface, fromAll.surface) << "(" << x << ", " << y << ")";
		  EXPECT_EQ(fromOwn.field, fromAll.field) << "(" << x << ", " << y << ")";
		  EXPECT_EQ(fromOwn.warpXM, fromAll.warpXM) << "(" << x << ", " << y << ")";
		  EXPECT_EQ(fromOwn.warpYM, fromAll.warpYM) << "(" << x << ", " << y << ")";
		  ++compared;
	};

	// East border x = 512 and south border y = 512 of chunk (0, 0), and the corner.
	const int64_t	line   = kChunkSize;
	const Composite east   = composite(line - kBand, 0, 2 * kBand, kChunkSize);
	const Composite south  = composite(0, line - kBand, kChunkSize, 2 * kBand);
	const Composite corner = composite(line - kBand, line - kBand, 2 * kBand, 2 * kBand);
	for (int k = 0; k < 2000; ++k) {
		const double along = 4.0 + 0.2517 * k;
		const double depth = -2.0 + 0.004 * (k % 1000);
		check(east.view(), static_cast<double>(line) + depth, along);
		check(south.view(), along, static_cast<double>(line) + depth);
	}
	for (int j = 0; j < 80; ++j) {
		for (int i = 0; i < 80; ++i) {
			check(corner.view(), static_cast<double>(line) - 2.0 + 0.05 * i + 0.013, static_cast<double>(line) - 2.0 + 0.05 * j + 0.007);
		}
	}
	EXPECT_EQ(compared, 2 * 2000 + 80 * 80);
}
