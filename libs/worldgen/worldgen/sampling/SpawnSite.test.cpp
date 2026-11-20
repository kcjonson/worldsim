// findWatersideSpawn: the drop is dry land kLandingWaterDistanceMeters from the nearest water of any kind.

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

// A river whose source tile's centre is the landing origin: the trunk and the source's two springs meet
// there, three channels a few meters apart.
struct SourceFixture {
    std::shared_ptr<GeneratedWorld> world;
    double                          latDeg{};
    double                          lonDeg{};
};

SourceFixture riverSource() {
    SourceFixture fixture{makeWorld()};
    const TileId source = fixture.world->grid->fromLatLon(0.0, 0.0);
    carveRiver(*fixture.world, source, fixture.world->grid->fromLatLon(0.0, 20.0));
    fixture.world->grid->latLonOf(source, fixture.latDeg, fixture.lonDeg);
    return fixture;
}

// Radius of the first ring round (x, y), 0.5 m apart and sampled every 0.5 m of arc, that touches a river
// channel or biome water: an independent reading of the distance to the nearest water of any kind.
double nearestWaterMeters(const SourceFixture& fixture, double x, double y, double limitMeters) {
    const RiverNetwork2D rivers(fixture.world, fixture.latDeg, fixture.lonDeg);
    const PlanetSampler  sampler(fixture.world, fixture.latDeg, fixture.lonDeg);
    for (double radius = 0.0; radius <= limitMeters; radius += 0.5) {
        const int points = std::max(1, static_cast<int>(std::ceil(2.0 * 3.14159265358979323846 * radius / 0.5)));
        for (int i = 0; i < points; ++i) {
            const double angle = 2.0 * 3.14159265358979323846 * i / points;
            const double px = x + radius * std::cos(angle);
            const double py = y + radius * std::sin(angle);
            if (rivers.sampleAt(px, py).isRiver || sampler.sampleAt(px, py).water) return radius;
        }
    }
    return limitMeters;
}

} // namespace

// Three channels meet at the origin, so the dry ground nearest it is close to two of them at once; the drop
// is the nearest point 30 m clear of every one.
TEST(SpawnSite, DropIsThirtyMetersFromTheNearestChannel) {
    const SourceFixture fixture = riverSource();
    const SpawnSite     site = findWatersideSpawn(fixture.world, fixture.latDeg, fixture.lonDeg);
    EXPECT_EQ(site.water, SpawnWater::River);
    EXPECT_NEAR(site.waterDistanceMeters, kLandingWaterDistanceMeters, kLandingWaterDistanceToleranceMeters);

    const PlanetSampler sampler(fixture.world, fixture.latDeg, fixture.lonDeg);
    EXPECT_FALSE(sampler.sampleAt(site.xMeters, site.yMeters).water) << "the drop is on land";
    const double nearest = nearestWaterMeters(fixture, site.xMeters, site.yMeters, 40.0);
    EXPECT_NEAR(nearest, site.waterDistanceMeters, 1.0) << "the reported distance is to the nearest channel";
}

TEST(SpawnSite, NoPointThirtyMetersFromWaterTakesTheFarthest) {
    const SourceFixture fixture = riverSource();
    constexpr double    kNarrowSearchMeters = 15.0; // every dry point in it is under 27 m from a channel
    const SpawnSite     site = findWatersideSpawn(fixture.world, fixture.latDeg, fixture.lonDeg, kNarrowSearchMeters);
    ASSERT_EQ(site.water, SpawnWater::River);
    EXPECT_GT(site.waterDistanceMeters, 0.0f);
    EXPECT_LT(site.waterDistanceMeters, kLandingWaterDistanceMeters - kLandingWaterDistanceToleranceMeters);

    // The point farthest from channels that meet at the centre is on the search square's edge.
    EXPECT_DOUBLE_EQ(std::max(std::abs(site.xMeters), std::abs(site.yMeters)), kNarrowSearchMeters);
    EXPECT_NEAR(nearestWaterMeters(fixture, site.xMeters, site.yMeters, 40.0), site.waterDistanceMeters, 1.0);
}

TEST(SpawnSite, DryWorldFallsBackToOrigin) {
    auto world = makeWorld();  // no rivers carved
    double lat = 0.0, lon = 0.0;
    world->grid->latLonOf(world->grid->fromLatLon(0.0, 0.0), lat, lon);

    SpawnSite site = findWatersideSpawn(world, lat, lon);
    EXPECT_EQ(site.water, SpawnWater::None);
    EXPECT_FLOAT_EQ(site.waterDistanceMeters, 0.0f);
    EXPECT_DOUBLE_EQ(site.xMeters, 0.0);
    EXPECT_DOUBLE_EQ(site.yMeters, 0.0);
}

} // namespace worldgen
