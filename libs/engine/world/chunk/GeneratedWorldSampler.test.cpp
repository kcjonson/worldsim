// GeneratedWorldSampler integration tests — verifies that 3D drainage data
// becomes 2D river water through the full chunk-generation path
// (sampleChunk -> ChunkSampleResult.riverSegments -> Chunk::computeTile).
//
// The world is built as SemiDesert everywhere: DesertGenerator never produces
// water, so any Surface::Water tile in a generated chunk must come from the
// river override. A control world with no drainage flags must yield zero water.

#include "GeneratedWorldSampler.h"

#include "world/chunk/Chunk.h"

#include <worldgen/data/PlanetParams.h>
#include <worldgen/data/WorldData.h>
#include <worldgen/grid/SphereGrid.h>

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <unordered_set>

using namespace engine::world;

namespace {

constexpr uint32_t kSubdivision = 24;  // ~300 km tiles
constexpr float kLand = 500.0f;

double dot(const worldgen::Vec3d& a, const worldgen::Vec3d& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

std::shared_ptr<worldgen::GeneratedWorld> makeSemiDesertWorld() {
    using namespace worldgen;
    auto world = std::make_shared<GeneratedWorld>();
    world->params.gridSubdivision = kSubdivision;
    world->params.seed = 0xDEADBEEFULL;
    world->derived = derive(world->params);
    world->grid = std::make_shared<SphereGrid>(kSubdivision);
    world->data.allocate(world->grid->tileCount());
    world->seaLevelMeters = 0.0f;
    world->validFields = static_cast<uint32_t>(WorldField::Elevation) |
                         static_cast<uint32_t>(WorldField::Biome) |
                         static_cast<uint32_t>(WorldField::Flags) |
                         static_cast<uint32_t>(WorldField::FlowAccum) |
                         static_cast<uint32_t>(WorldField::Downhill);
    for (TileId t = 0; t < world->grid->tileCount(); ++t) {
        world->data.elevation[t] = kLand;
        world->data.biome[t] = static_cast<uint8_t>(Biome::SemiDesert);
    }
    return world;
}

// Flag a river chain from `source` toward `mouth`, returning the source tile.
worldgen::TileId carveRiver(worldgen::GeneratedWorld& world, worldgen::TileId source,
                            worldgen::TileId mouth) {
    using namespace worldgen;
    const SphereGrid& grid = *world.grid;
    const Vec3d target = grid.tileCenter(mouth);
    std::unordered_set<TileId> visited;
    TileId cur = source;
    float flow = 80.0f;  // wide enough channel to clearly rasterize
    for (int guard = 0; guard < 1000; ++guard) {
        if (cur == mouth || !visited.insert(cur).second) break;
        std::array<TileId, 6> nbrs{};
        const uint32_t count = grid.neighbors(cur, nbrs);
        int bestIdx = -1;
        double bestDot = -2.0;
        for (uint32_t i = 0; i < count; ++i) {
            const double d = dot(grid.tileCenter(nbrs[i]), target);
            if (d > bestDot) { bestDot = d; bestIdx = static_cast<int>(i); }
        }
        if (bestIdx < 0) break;
        world.data.flags[cur] |= kFlagRiver;
        world.data.flowAccum[cur] = flow;
        world.data.downhill[cur] = static_cast<uint8_t>(bestIdx);
        flow += 20.0f;
        cur = nbrs[static_cast<uint32_t>(bestIdx)];
    }
    return source;
}

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
