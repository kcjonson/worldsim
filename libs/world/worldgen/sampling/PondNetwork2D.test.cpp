// PondNetwork2D tests - sparse hydrology/biome-driven ponds + desert oases.
//
// Worlds are built synthetically (no PlanetGenerator): a uniform-biome land sphere,
// optionally with an ocean surround for the distance-to-water cases. Some tests
// shrink the planet radius so the coarse grid's per-tile distance is small enough
// (~km) for the oasis threshold (km-scale) to be meaningfully crossed.

#include "worldgen/sampling/PondNetwork2D.h"

#include "worldgen/data/Biome.h"
#include "worldgen/data/PlanetParams.h"
#include "worldgen/data/WorldData.h"
#include "worldgen/grid/SphereGrid.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

namespace worldgen {

namespace {

constexpr uint32_t kSubdivision = 24; // ~5762 tiles
constexpr float    kLand = 500.0f;
constexpr float    kOcean = -3000.0f;

std::shared_ptr<GeneratedWorld> makeWorld(Biome biome, uint16_t precip, bool withOcean,
                                          bool smallPlanet = false, double landHalfDeg = 6.0) {
    auto world = std::make_shared<GeneratedWorld>();
    world->params.gridSubdivision = kSubdivision;
    world->params.seed = 0xA17C0DEull;
    world->derived = derive(world->params);
    if (smallPlanet) world->derived.planetRadiusMeters = 20000.0; // ~0.8 km tiles
    world->grid = std::make_shared<SphereGrid>(kSubdivision);
    world->data.allocate(world->grid->tileCount());
    world->seaLevelMeters = 0.0f;
    world->validFields = static_cast<uint32_t>(WorldField::Elevation) |
                         static_cast<uint32_t>(WorldField::Flags) |
                         static_cast<uint32_t>(WorldField::FlowAccum) |
                         static_cast<uint32_t>(WorldField::Downhill) |
                         static_cast<uint32_t>(WorldField::Precipitation) |
                         static_cast<uint32_t>(WorldField::Biome);
    for (TileId t = 0; t < world->grid->tileCount(); ++t) {
        bool ocean = false;
        if (withOcean) {
            double lat = 0.0, lon = 0.0;
            world->grid->latLonOf(t, lat, lon);
            ocean = !(std::abs(lat) < landHalfDeg && std::abs(lon) < landHalfDeg);
        }
        if (ocean) {
            world->data.elevation[t] = kOcean;
            world->data.flags[t] = kFlagOcean;
            world->data.biome[t] = static_cast<uint8_t>(Biome::Ocean);
        } else {
            world->data.elevation[t] = kLand;
            world->data.biome[t] = static_cast<uint8_t>(biome);
            world->data.precipitation[t] = precip;
        }
    }
    return world;
}

size_t countPonds(const PondNetwork2D& net, double half) {
    std::vector<PondNetwork2D::Pond> ponds;
    net.gatherPonds(-half, -half, half, half, ponds);
    return ponds.size();
}

} // namespace

TEST(PondNetwork2D, GatherIsDeterministic) {
    auto world = makeWorld(Biome::TemperateWetland, 1500, /*withOcean=*/false);
    PondNetwork2D a(world, 0.0, 0.0);
    PondNetwork2D b(world, 0.0, 0.0);
    std::vector<PondNetwork2D::Pond> pa;
    std::vector<PondNetwork2D::Pond> pb;
    a.gatherPonds(-3000.0, -3000.0, 3000.0, 3000.0, pa);
    b.gatherPonds(-3000.0, -3000.0, 3000.0, 3000.0, pb);
    ASSERT_EQ(pa.size(), pb.size());
    ASSERT_FALSE(pa.empty());
    for (size_t i = 0; i < pa.size(); ++i) {
        EXPECT_EQ(pa[i].cx, pb[i].cx);
        EXPECT_EQ(pa[i].cy, pb[i].cy);
        EXPECT_EQ(pa[i].radius, pb[i].radius);
        EXPECT_EQ(pa[i].depth, pb[i].depth);
    }
}

TEST(PondNetwork2D, WetlandProducesPonds) {
    auto world = makeWorld(Biome::TemperateWetland, 1500, /*withOcean=*/false);
    PondNetwork2D net(world, 0.0, 0.0);
    EXPECT_GT(countPonds(net, 3000.0), 0u) << "a wet biome should grow natural ponds";
}

// Natural-path biome gating, using two biomes that never spawn oases (threshold < 0)
// so only the precipitation/biome-weighted natural path is exercised: wetland (weight
// 1.0) must out-pond rainforest (weight 0.7) over the same region.
TEST(PondNetwork2D, WetterBiomeHasMorePonds) {
    auto wet = makeWorld(Biome::TemperateWetland, 1500, false);
    auto rain = makeWorld(Biome::TropicalRainforest, 1500, false);
    PondNetwork2D wetNet(wet, 0.0, 0.0);
    PondNetwork2D rainNet(rain, 0.0, 0.0);
    EXPECT_GT(countPonds(wetNet, 4000.0), countPonds(rainNet, 4000.0));
}

TEST(PondNetwork2D, DesertFarFromWaterGetsOasis) {
    // All-land desert, no water anywhere -> every dry cell is "far" -> oases appear.
    auto world = makeWorld(Biome::HotDesert, 80, /*withOcean=*/false);
    PondNetwork2D net(world, 0.0, 0.0);
    EXPECT_GT(countPonds(net, 5000.0), 0u) << "a waterless desert expanse should sprout oases";
}

TEST(PondNetwork2D, DesertNearWaterHasFarFewerPonds) {
    // Small planet (~0.8 km tiles) so distance-to-water is meaningful at the km
    // oasis threshold. A desert ringed by nearby ocean stays (almost) pond-free,
    // whereas the same desert with no water nearby sprouts oases.
    auto nearW = makeWorld(Biome::HotDesert, 80, /*withOcean=*/true, /*smallPlanet=*/true, 6.0);
    auto farW = makeWorld(Biome::HotDesert, 80, /*withOcean=*/false, /*smallPlanet=*/true);
    PondNetwork2D nearNet(nearW, 0.0, 0.0);
    PondNetwork2D farNet(farW, 0.0, 0.0);
    const size_t nearCount = countPonds(nearNet, 1500.0);
    const size_t farCount = countPonds(farNet, 1500.0);
    EXPECT_LT(nearCount, farCount) << "ponds near water should be far fewer than in a dry interior";
    EXPECT_LE(nearCount, 1u) << "a desert hugging the coast should be essentially pond-free";
}

TEST(PondNetwork2D, CountIsBoundedPerChunk) {
    // Over a single 512 m chunk, even the wettest biome stays in the low single
    // digits -- nothing like the old noise flood.
    auto world = makeWorld(Biome::TemperateWetland, 1800, false);
    PondNetwork2D net(world, 0.0, 0.0);
    std::vector<PondNetwork2D::Pond> ponds;
    net.gatherPonds(0.0, 0.0, 512.0, 512.0, ponds);
    EXPECT_LE(ponds.size(), 6u) << "ponds per chunk must be sparse, not a flood";
}

TEST(PondNetwork2D, SeamStraddlingPondIsConsistent) {
    // Ponds fully inside the overlap of two offset boxes must match bit-for-bit, so
    // adjacent chunks render the same pond with no seam.
    auto world = makeWorld(Biome::TemperateWetland, 1500, false);
    PondNetwork2D net(world, 0.0, 0.0);
    std::vector<PondNetwork2D::Pond> a;
    std::vector<PondNetwork2D::Pond> b;
    net.gatherPonds(-4000.0, -4000.0, 0.0, 0.0, a);
    net.gatherPonds(-2000.0, -2000.0, 2000.0, 2000.0, b);
    const double oMinX = -2000.0, oMinY = -2000.0, oMaxX = 0.0, oMaxY = 0.0;
    int matched = 0;
    for (const auto& p : a) {
        const double pr = static_cast<double>(p.radius) * 1.5;
        if (p.cx - pr < oMinX || p.cx + pr > oMaxX || p.cy - pr < oMinY || p.cy + pr > oMaxY) continue;
        bool found = false;
        for (const auto& q : b) {
            if (p.cx == q.cx && p.cy == q.cy && p.radius == q.radius && p.depth == q.depth) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "a pond inside the overlap is missing from the other box's gather";
        if (found) ++matched;
    }
    EXPECT_GT(matched, 0) << "expected shared ponds in the overlap region";
}

TEST(PondNetwork2D, DepthInsidePondAndDryElsewhere) {
    auto world = makeWorld(Biome::TemperateWetland, 1600, false);
    PondNetwork2D net(world, 0.0, 0.0);
    std::vector<PondNetwork2D::Pond> ponds;
    net.gatherPonds(-3000.0, -3000.0, 3000.0, 3000.0, ponds);
    ASSERT_FALSE(ponds.empty());
    const PondNetwork2D::Pond& p = ponds.front();
    EXPECT_GT(net.depthAt(p.cx, p.cy), 0) << "the pond center should read as water";
    EXPECT_EQ(net.depthAt(p.cx + 100000.0, p.cy + 100000.0), 0) << "far from any pond should be dry";
}

} // namespace worldgen
