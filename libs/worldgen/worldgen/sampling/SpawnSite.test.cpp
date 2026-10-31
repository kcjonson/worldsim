// findRiverbankSpawn tests — the colonist drop point lands on dry ground beside
// clean water when a river runs through the landing tile.

#include "worldgen/sampling/SpawnSite.h"

#include "worldgen/data/PlanetParams.h"
#include "worldgen/data/WorldData.h"
#include "worldgen/sampling/PlanetSampler.h"
#include "worldgen/sampling/RiverNetwork2D.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <memory>
#include <unordered_set>

namespace worldgen {

namespace {

constexpr uint32_t kSubdivision = 24;
constexpr float kLand = 500.0f;
constexpr float kOcean = -3000.0f;

double dot(const Vec3d& a, const Vec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

std::shared_ptr<GeneratedWorld> makeWorld() {
    auto world = std::make_shared<GeneratedWorld>();
    world->params.gridSubdivision = kSubdivision;
    world->params.seed = 0x7777ULL;
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
        double lat = 0.0, lon = 0.0;
        world->grid->latLonOf(t, lat, lon);
        const bool land = std::abs(lat) < 25.0 && std::abs(lon) < 25.0;
        world->data.elevation[t] = land ? kLand : kOcean;
        world->data.biome[t] = static_cast<uint8_t>(land ? Biome::TemperateGrassland : Biome::Ocean);
        if (!land) world->data.flags[t] = kFlagOcean;
    }
    return world;
}

void carveRiver(GeneratedWorld& world, TileId source, TileId mouth) {
    const SphereGrid& grid = *world.grid;
    const Vec3d target = grid.tileCenter(mouth);
    std::unordered_set<TileId> visited;
    TileId cur = source;
    float flow = 90.0f;
    for (int guard = 0; guard < 1000; ++guard) {
        if (cur == mouth || !visited.insert(cur).second) break;
        std::array<TileId, 6> nbrs{};
        const uint32_t count = grid.neighbors(cur, nbrs);
        int best = -1;
        double bestDot = -2.0;
        for (uint32_t i = 0; i < count; ++i) {
            const double d = dot(grid.tileCenter(nbrs[i]), target);
            if (d > bestDot) { bestDot = d; best = static_cast<int>(i); }
        }
        if (best < 0) break;
        world.data.flags[cur] |= kFlagRiver;
        world.data.flowAccum[cur] = flow;
        world.data.downhill[cur] = static_cast<uint8_t>(best);
        flow += 15.0f;
        cur = nbrs[static_cast<uint32_t>(best)];
    }
}

} // namespace

TEST(SpawnSite, RiverThroughOriginSpawnsOnDryBank) {
    auto world = makeWorld();
    const TileId source = world->grid->fromLatLon(0.0, 0.0);
    const TileId mouth = world->grid->fromLatLon(0.0, 20.0);
    carveRiver(*world, source, mouth);

    double lat = 0.0, lon = 0.0;
    world->grid->latLonOf(source, lat, lon);

    SpawnSite site = findRiverbankSpawn(world, lat, lon);
    EXPECT_TRUE(site.nearWater);
    EXPECT_TRUE(site.freshWater);

    // The spawn must be dry land (not in the channel, not biome water)...
    RiverNetwork2D rn(world, lat, lon);
    PlanetSampler ps(world, lat, lon);
    EXPECT_FALSE(rn.sampleAt(site.xMeters, site.yMeters).isRiver) << "spawn must not be in the river";
    EXPECT_FALSE(ps.sampleAt(site.xMeters, site.yMeters).water) << "spawn must be on land";

    // ...and close to the channel (a riverbank), within a few meters of the bank.
    bool channelWithin = false;
    for (double r = 1.0; r <= 12.0 && !channelWithin; r += 1.0) {
        const double offs[4][2] = {{r, 0}, {-r, 0}, {0, r}, {0, -r}};
        for (auto& o : offs)
            if (rn.sampleAt(site.xMeters + o[0], site.yMeters + o[1]).isRiver) channelWithin = true;
    }
    EXPECT_TRUE(channelWithin) << "spawn should sit on the riverbank";
}

TEST(SpawnSite, DryWorldFallsBackToOrigin) {
    auto world = makeWorld();  // no rivers carved
    double lat = 0.0, lon = 0.0;
    world->grid->latLonOf(world->grid->fromLatLon(0.0, 0.0), lat, lon);

    SpawnSite site = findRiverbankSpawn(world, lat, lon);
    EXPECT_FALSE(site.nearWater);
    EXPECT_DOUBLE_EQ(site.xMeters, 0.0);
    EXPECT_DOUBLE_EQ(site.yMeters, 0.0);
}

} // namespace worldgen
