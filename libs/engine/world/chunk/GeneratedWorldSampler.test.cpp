// GeneratedWorldSampler integration tests — verifies that 3D drainage data
// becomes 2D river water through the full chunk-generation path
// (sampleChunk -> ChunkSampleResult.riverSegments -> Chunk::computeTile).
//
// The world is built as SemiDesert everywhere: DesertGenerator never produces
// water, so any Surface::Water tile in a generated chunk must come from the
// river override. A control world with no drainage flags must yield zero water.

#include "GeneratedWorldSampler.h"

#include "world/chunk/Chunk.h"
#include "world/chunk/RiverTestWorld.h"

#include <worldgen/grid/SphereGrid.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

using namespace engine::world;

namespace {

using river_test::carveRiver;
using river_test::makeSemiDesertWorld;

uint32_t countWater(const Chunk& chunk) {
    uint32_t water = 0;
    for (uint16_t y = 0; y < kChunkSize; ++y)
        for (uint16_t x = 0; x < kChunkSize; ++x)
            if (chunk.getTile(x, y).surface == Surface::Water) ++water;
    return water;
}

// Chunk holds two 4 MB tile arrays; heap-allocate it (as the game does) to keep
// it off the stack.
std::unique_ptr<Chunk> generateChunk(ChunkCoordinate coord, ChunkSampleResult result,
                                     uint64_t seed) {
    auto chunk = std::make_unique<Chunk>(coord, std::move(result), seed);
    chunk->generate();
    return chunk;
}

// Reference channel half-width at (x, y): the widest segment whose linearly
// interpolated half-width covers the point, with `floor` applied per endpoint
// first. floor = 0 is the rule tiles follow now (true width); floor = 0.8 is the
// old tile-raster floor (kTileRasterMinHalfM, and before it RiverNetwork2D's
// kRenderMinHalf) that kept sub-tile streams contiguous on the grid back when
// tiles drew the river. Recomputed here independently of riverHalfWidthAt so the
// tile check below is not the production function checked against itself.
float referenceHalfWidthAt(const std::vector<worldgen::RiverNetwork2D::Segment>& segs,
                           double x, double y, float floor) {
    float best = 0.0f;
    for (const auto& s : segs) {
        const float hw0 = std::max(s.halfWidth0, floor);
        const float hw1 = std::max(s.halfWidth1, floor);
        const double dx = s.x1 - s.x0;
        const double dy = s.y1 - s.y0;
        const double len2 = dx * dx + dy * dy;
        double t = 0.0;
        if (len2 > 0.0) {
            t = ((x - s.x0) * dx + (y - s.y0) * dy) / len2;
            t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
        }
        const double cx = s.x0 + dx * t;
        const double cy = s.y0 + dy * t;
        const double ex = x - cx;
        const double ey = y - cy;
        const float hw = static_cast<float>(static_cast<double>(hw0) +
                                            (static_cast<double>(hw1) - static_cast<double>(hw0)) * t);
        if (ex * ex + ey * ey <= static_cast<double>(hw) * static_cast<double>(hw) && hw > best) best = hw;
    }
    return best;
}

// Mirrors Chunk.cpp's file-private waterDepthFromWidth exactly (kept in sync by
// hand -- that function is anonymous-namespace and can't be called from here).
uint8_t referenceDepthFromWidth(float fullWidthMeters) {
    constexpr float kShallowAt = 1.5f;
    constexpr float kDeepAt = 14.0f;
    constexpr float kMinDepth = 80.0f;
    float t = std::clamp((fullWidthMeters - kShallowAt) / (kDeepAt - kShallowAt), 0.0f, 1.0f);
    t = t * t * (3.0f - 2.0f * t);
    float d = kMinDepth + t * (255.0f - kMinDepth);
    return static_cast<uint8_t>(std::clamp(d, 0.0f, 255.0f));
}

} // namespace

TEST(GeneratedWorldSamplerRivers, RiverChainProducesWaterInChunk) {
    using namespace worldgen;
    auto world = makeSemiDesertWorld();

    // Land exactly on a river tile so the channel passes through the origin chunk.
    const TileId source = world->grid->fromLatLon(0.0, 0.0);
    const TileId mouth = world->grid->fromLatLon(0.0, 40.0);
    carveRiver(*world, source, mouth);

    double lat = 0.0;
    double lon = 0.0;
    world->grid->latLonOf(source, lat, lon);

    GeneratedWorldSampler sampler(world, lat, lon);
    ChunkSampleResult result = sampler.sampleChunk(ChunkCoordinate(0, 0));
    EXPECT_FALSE(result.riverSegments.empty()) << "river segments should be gathered for the origin chunk";

    auto chunk = generateChunk(ChunkCoordinate(0, 0), std::move(result), sampler.getWorldSeed());
    EXPECT_GT(countWater(*chunk), 0u) << "the river should paint water tiles in the chunk";
}

TEST(GeneratedWorldSamplerRivers, RiverWaterCarriesDepthFromWidth) {
    using namespace worldgen;
    auto world = makeSemiDesertWorld();

    const TileId source = world->grid->fromLatLon(0.0, 0.0);
    const TileId mouth = world->grid->fromLatLon(0.0, 40.0);
    carveRiver(*world, source, mouth); // flow 80, widening downstream

    double lat = 0.0;
    double lon = 0.0;
    world->grid->latLonOf(source, lat, lon);

    GeneratedWorldSampler sampler(world, lat, lon);
    ChunkSampleResult result = sampler.sampleChunk(ChunkCoordinate(0, 0));
    auto chunk = generateChunk(ChunkCoordinate(0, 0), std::move(result), sampler.getWorldSeed());

    // The carved river is wide (flow >= 80), so its water tiles must carry a
    // deeper cosmetic depth than the shallow-stream floor.
    uint8_t maxDepth = 0;
    bool anyWater = false;
    for (uint16_t y = 0; y < kChunkSize; ++y) {
        for (uint16_t x = 0; x < kChunkSize; ++x) {
            const auto& tile = chunk->getTile(x, y);
            if (tile.surface != Surface::Water) continue;
            anyWater = true;
            maxDepth = std::max(maxDepth, tile.waterDepth);
        }
    }
    ASSERT_TRUE(anyWater);
    EXPECT_GT(maxDepth, 120) << "a wide carved river should render well below the deepest, not at the shallow floor";
}

// The river source lands at local world (0,0) (GeneratedWorldSampler uses the
// landing lat/lon as the origin), the shared corner of chunks (0,0), (-1,0),
// (0,-1), and (-1,-1). Chunk (-1,-1)'s own 512x512 square is x,y in [-512, 0),
// which excludes (0,0); only its 8 m apron reaches across the corner to it. This
// is the apron-AABB-expansion this task adds to sampleChunk (terrain-polygons-
// architecture.md D4): before it, chunk (-1,-1) would gather no segments here.
TEST(GeneratedWorldSamplerRivers, ApronReachesRiverJustAcrossChunkCorner) {
    using namespace worldgen;
    auto world = makeSemiDesertWorld();

    const TileId source = world->grid->fromLatLon(0.0, 0.0);
    const TileId mouth = world->grid->fromLatLon(0.0, 40.0);
    carveRiver(*world, source, mouth);

    double lat = 0.0;
    double lon = 0.0;
    world->grid->latLonOf(source, lat, lon);

    GeneratedWorldSampler sampler(world, lat, lon);

    ChunkSampleResult originResult = sampler.sampleChunk(ChunkCoordinate(0, 0));
    ChunkSampleResult farCornerResult = sampler.sampleChunk(ChunkCoordinate(-1, -1));

    EXPECT_FALSE(originResult.riverSegments.empty())
        << "the chunk touching the river source directly must gather it";
    EXPECT_FALSE(farCornerResult.riverSegments.empty())
        << "the diagonal neighbor's 8 m apron must reach the river just across its corner";
}

TEST(GeneratedWorldSamplerRivers, NoDrainageMeansNoWater) {
    using namespace worldgen;
    auto world = makeSemiDesertWorld();
    // No river flags, downhill stays 0xFF everywhere (allocate default): a dry world.

    const TileId source = world->grid->fromLatLon(0.0, 0.0);
    double lat = 0.0;
    double lon = 0.0;
    world->grid->latLonOf(source, lat, lon);

    GeneratedWorldSampler sampler(world, lat, lon);
    ChunkSampleResult result = sampler.sampleChunk(ChunkCoordinate(0, 0));
    EXPECT_TRUE(result.riverSegments.empty());

    auto chunk = generateChunk(ChunkCoordinate(0, 0), std::move(result), sampler.getWorldSeed());
    EXPECT_EQ(countWater(*chunk), 0u) << "a SemiDesert world with no drainage must have no water";
}

// Tiles rasterize every channel at its true width. The tile grid used to hold
// each rasterized half-width at 0.8 m or more so a sub-tile stream drew as a
// contiguous 1-tile line; the river is drawn from the terrain distance field now
// (D10) and Surface::Water is data (D1: prefilter and tile-granular queries), so
// the floor is gone and a tile is water exactly when the true channel covers its
// center. The chunks below include headwater feeders narrower than the old floor,
// and the test requires at least one tile the floored rule would have made water
// and the true rule doesn't, so it proves the floor is really gone.
TEST(GeneratedWorldSamplerRivers, TileWaterMatchesTrueWidthRule) {
    using namespace worldgen;
    auto world = makeSemiDesertWorld();

    // A wide trunk (flow 80, widening downstream) with headwater feeders that
    // now taper to a true sub-tile trickle -- exactly where the moved floor
    // matters.
    const TileId source = world->grid->fromLatLon(0.0, 0.0);
    const TileId mouth = world->grid->fromLatLon(0.0, 40.0);
    carveRiver(*world, source, mouth);

    double lat = 0.0;
    double lon = 0.0;
    world->grid->latLonOf(source, lat, lon);
    GeneratedWorldSampler sampler(world, lat, lon);

    // A few chunks along the trunk and around its headwater feeders (chunk (0,0)
    // sits on the source, where the headwater springs fan out and taper).
    const std::array<ChunkCoordinate, 5> coords = {
        ChunkCoordinate(0, 0), ChunkCoordinate(1, 0), ChunkCoordinate(0, 1),
        ChunkCoordinate(-1, 0), ChunkCoordinate(0, -1)};

    bool sawSubFloorSegment = false;
    bool sawWaterTile = false;
    bool sawTileOnlyTheFloorCovered = false;
    for (size_t ci = 0; ci < coords.size(); ++ci) {
        const ChunkCoordinate coord = coords[ci];
        ChunkSampleResult result = sampler.sampleChunk(coord);
        for (const auto& s : result.riverSegments) {
            if (s.halfWidth0 < 0.8f || s.halfWidth1 < 0.8f) sawSubFloorSegment = true;
        }

        auto chunk = generateChunk(coord, result, sampler.getWorldSeed());

        const WorldPosition origin = coord.origin();

        // Around each sub-floor segment's midpoint in this chunk, look for a tile
        // the old floor would have made water that is land at true width.
        for (const auto& s : result.riverSegments) {
            if (std::max(s.halfWidth0, s.halfWidth1) >= 0.8f) continue;
            const int64_t mx = static_cast<int64_t>(std::floor(0.5 * (s.x0 + s.x1) - origin.x));
            const int64_t my = static_cast<int64_t>(std::floor(0.5 * (s.y0 + s.y1) - origin.y));
            for (int64_t y = my - 2; y <= my + 2; ++y) {
                for (int64_t x = mx - 2; x <= mx + 2; ++x) {
                    if (x < 0 || y < 0 || x >= kChunkSize || y >= kChunkSize) continue;
                    const double wx = static_cast<double>(origin.x) + static_cast<double>(x);
                    const double wy = static_cast<double>(origin.y) + static_cast<double>(y);
                    if (referenceHalfWidthAt(result.riverSegments, wx, wy, 0.0f) == 0.0f &&
                        referenceHalfWidthAt(result.riverSegments, wx, wy, 0.8f) > 0.0f &&
                        chunk->getTile(static_cast<uint16_t>(x), static_cast<uint16_t>(y)).surface != Surface::Water) {
                        sawTileOnlyTheFloorCovered = true;
                    }
                }
            }
        }

        // Full per-tile check on the chunk holding the headwater source; a coarse
        // stride elsewhere keeps the test fast while still covering the trunk and
        // any feeder reach into those chunks.
        const uint16_t stride = (ci == 0) ? 1 : 8;
        for (uint16_t y = 0; y < kChunkSize; y = static_cast<uint16_t>(y + stride)) {
            for (uint16_t x = 0; x < kChunkSize; x = static_cast<uint16_t>(x + stride)) {
                const double wx =
                    static_cast<double>(origin.x) + static_cast<double>(x) * static_cast<double>(kTileSize);
                const double wy =
                    static_cast<double>(origin.y) + static_cast<double>(y) * static_cast<double>(kTileSize);
                const float refHalf = referenceHalfWidthAt(result.riverSegments, wx, wy, 0.0f);
                const bool refIsWater = refHalf > 0.0f;

                const TileData& tile = chunk->getTile(x, y);
                const bool actualIsWater = tile.surface == Surface::Water;
                ASSERT_EQ(refIsWater, actualIsWater)
                    << "chunk (" << coord.x << "," << coord.y << ") tile (" << x << "," << y << ")";
                if (refIsWater) {
                    sawWaterTile = true;
                    EXPECT_EQ(referenceDepthFromWidth(2.0f * refHalf), tile.waterDepth)
                        << "chunk (" << coord.x << "," << coord.y << ") tile (" << x << "," << y << ")";
                }
            }
        }
    }
    EXPECT_TRUE(sawSubFloorSegment)
        << "test should exercise river geometry under the old 0.8 m render floor";
    EXPECT_TRUE(sawTileOnlyTheFloorCovered)
        << "some tile the old floor would have made water must be land at true width";
    EXPECT_TRUE(sawWaterTile);
}
