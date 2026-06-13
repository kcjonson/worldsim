// PlanetSampler / LandingSite tests — M4.
//
// Worlds are built synthetically (arrays filled by hand, validFields set
// directly); the full PlanetGenerator pipeline is never run here.

#include "worldgen/sampling/PlanetSampler.h"

#include "worldgen/data/PlanetParams.h"
#include "worldgen/data/WorldData.h"
#include "worldgen/sampling/LandingSite.h"
#include "worldgen/sampling/SphericalProjection.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <memory>

namespace worldgen {

namespace {

constexpr uint32_t kSubdivision = 12;  // 1440 tiles, ~600 km tile width
constexpr float kLandElevationAboveSea = 500.0f;
constexpr float kOceanDepthBelowSea = 3000.0f;

// All-ocean world with a rectangular land region around lat/lon (0,0):
// tiles with |lat| < 25 and |lon| < 25 are TemperateGrassland at
// seaLevel + 500 m; everything else is ocean-flagged at seaLevel - 3000 m.
std::shared_ptr<GeneratedWorld> makeWorld(float seaLevelMeters) {
    auto world = std::make_shared<GeneratedWorld>();
    world->params.gridSubdivision = kSubdivision;
    world->params.seed = 0xABCDEF12345ULL;
    world->derived = derive(world->params);
    world->grid = std::make_shared<SphereGrid>(kSubdivision);
    world->data.allocate(world->grid->tileCount());
    world->seaLevelMeters = seaLevelMeters;
    world->validFields = static_cast<uint32_t>(WorldField::Elevation) |
                         static_cast<uint32_t>(WorldField::Biome) |
                         static_cast<uint32_t>(WorldField::Flags);

    for (TileId t = 0; t < world->grid->tileCount(); ++t) {
        double latDeg = 0.0;
        double lonDeg = 0.0;
        world->grid->latLonOf(t, latDeg, lonDeg);
        if (std::abs(latDeg) < 25.0 && std::abs(lonDeg) < 25.0) {
            world->data.biome[t] = static_cast<uint8_t>(Biome::TemperateGrassland);
            world->data.flags[t] = 0;
            world->data.elevation[t] = seaLevelMeters + kLandElevationAboveSea;
        } else {
            world->data.biome[t] = static_cast<uint8_t>(Biome::Ocean);
            world->data.flags[t] = kFlagOcean;
            world->data.elevation[t] = seaLevelMeters - kOceanDepthBelowSea;
        }
    }
    return world;
}

// World position of a lat/lon for a sampler landed at (0, 0).
WorldPos2d worldPosOf(const GeneratedWorld& world, double latDeg, double lonDeg) {
    SphericalProjection projection(world.derived.planetRadiusMeters, 0.0, 0.0);
    return projection.latLonToWorld({latDeg, lonDeg});
}

WorldPos2d tileCenterPos(const GeneratedWorld& world, TileId t) {
    double latDeg = 0.0;
    double lonDeg = 0.0;
    world.grid->latLonOf(t, latDeg, lonDeg);
    return worldPosOf(world, latDeg, lonDeg);
}

} // namespace

TEST(PlanetSampler, LandSamplesAsGrasslandAboveSeaLevel) {
    auto world = makeWorld(0.0f);
    PlanetSampler sampler(world, 0.0, 0.0);

    auto sample = sampler.sampleAt(0.0, 0.0);
    EXPECT_FALSE(sample.water);
    ASSERT_GE(sample.weightCount, 1u);
    EXPECT_EQ(sample.weights[0].biome, Biome::TemperateGrassland);
    // Any blend partner here is also grassland at the same elevation.
    EXPECT_FLOAT_EQ(sample.elevationMeters, kLandElevationAboveSea);
}

TEST(PlanetSampler, ElevationIsRelativeToSeaLevel) {
    // Same world shape at two sea levels: gameplay elevation must not change,
    // because tile elevations are authored relative to seaLevelMeters.
    auto low = makeWorld(0.0f);
    auto high = makeWorld(250.0f);
    PlanetSampler lowSampler(low, 0.0, 0.0);
    PlanetSampler highSampler(high, 0.0, 0.0);

    EXPECT_FLOAT_EQ(lowSampler.elevationAt(0.0, 0.0), kLandElevationAboveSea);
    EXPECT_FLOAT_EQ(highSampler.elevationAt(0.0, 0.0), kLandElevationAboveSea);

    WorldPos2d deepOcean = worldPosOf(*low, 0.0, 120.0);
    EXPECT_FLOAT_EQ(lowSampler.elevationAt(deepOcean.x, deepOcean.y), -kOceanDepthBelowSea);
    EXPECT_FLOAT_EQ(highSampler.elevationAt(deepOcean.x, deepOcean.y), -kOceanDepthBelowSea);
}

TEST(PlanetSampler, OceanSamplesAsWaterWithNegativeElevation) {
    auto world = makeWorld(0.0f);
    PlanetSampler sampler(world, 0.0, 0.0);

    WorldPos2d pos = worldPosOf(*world, 0.0, 120.0);
    auto sample = sampler.sampleAt(pos.x, pos.y);
    EXPECT_TRUE(sample.water);
    EXPECT_EQ(sample.weights[0].biome, Biome::Ocean);
    EXPECT_FLOAT_EQ(sample.elevationMeters, -kOceanDepthBelowSea);
}

TEST(PlanetSampler, LakeFlagSamplesAsLakeClampedToSeaLevel) {
    auto world = makeWorld(0.0f);
    TileId lakeTile = world->grid->fromLatLon(10.0, 10.0);
    world->data.flags[lakeTile] = kFlagLake;
    world->data.biome[lakeTile] = static_cast<uint8_t>(Biome::Lake);
    world->data.elevation[lakeTile] = 25.0f;  // lake surface above sea level

    PlanetSampler sampler(world, 0.0, 0.0);
    WorldPos2d pos = tileCenterPos(*world, lakeTile);
    auto sample = sampler.sampleAt(pos.x, pos.y);

    EXPECT_EQ(sample.tile, lakeTile);
    EXPECT_TRUE(sample.water);
    EXPECT_EQ(sample.weights[0].biome, Biome::Lake);
    EXPECT_LE(sample.elevationMeters, 0.0f);
}

TEST(PlanetSampler, UnflaggedLandBelowSeaLevelSamplesAsOcean) {
    auto world = makeWorld(0.0f);
    TileId sunken = world->grid->fromLatLon(-10.0, -10.0);
    ASSERT_EQ(world->data.flags[sunken], 0);  // land region, no water flags
    world->data.elevation[sunken] = -50.0f;

    PlanetSampler sampler(world, 0.0, 0.0);
    WorldPos2d pos = tileCenterPos(*world, sunken);
    auto sample = sampler.sampleAt(pos.x, pos.y);

    EXPECT_EQ(sample.tile, sunken);
    EXPECT_TRUE(sample.water);
    EXPECT_EQ(sample.weights[0].biome, Biome::Ocean);
    EXPECT_FLOAT_EQ(sample.elevationMeters, -50.0f);
}

TEST(PlanetSampler, TileCenterIsPureAndBoundaryBlendsNeighbor) {
    auto world = makeWorld(0.0f);
    TileId center = world->grid->fromLatLon(5.0, 5.0);
    world->data.elevation[center] = 400.0f;

    std::array<TileId, 6> neighborIds{};
    uint32_t neighborCount = world->grid->neighbors(center, neighborIds);
    ASSERT_GE(neighborCount, 5u);
    for (uint32_t i = 0; i < neighborCount; ++i) {
        world->data.biome[neighborIds[i]] = static_cast<uint8_t>(Biome::HotDesert);
        world->data.flags[neighborIds[i]] = 0;
        world->data.elevation[neighborIds[i]] = 600.0f;
    }

    PlanetSampler sampler(world, 0.0, 0.0);

    // Tile centers are hundreds of km from any edge at this subdivision: pure.
    WorldPos2d centerPos = tileCenterPos(*world, center);
    auto pure = sampler.sampleAt(centerPos.x, centerPos.y);
    EXPECT_EQ(pure.weightCount, 1u);
    EXPECT_EQ(pure.weights[0].biome, Biome::TemperateGrassland);
    EXPECT_FLOAT_EQ(pure.weights[0].weight, 1.0f);
    EXPECT_FLOAT_EQ(pure.elevationMeters, 400.0f);

    // Bisect along the segment toward a neighbor center to land on the edge.
    WorldPos2d neighborPos = tileCenterPos(*world, neighborIds[0]);
    ASSERT_EQ(sampler.tileAt(centerPos.x, centerPos.y), center);
    ASSERT_NE(sampler.tileAt(neighborPos.x, neighborPos.y), center);

    double lo = 0.0;
    double hi = 1.0;
    for (int i = 0; i < 60; ++i) {
        double mid = 0.5 * (lo + hi);
        double x = centerPos.x + (neighborPos.x - centerPos.x) * mid;
        double y = centerPos.y + (neighborPos.y - centerPos.y) * mid;
        if (sampler.tileAt(x, y) == center) {
            lo = mid;
        } else {
            hi = mid;
        }
    }

    double edgeX = centerPos.x + (neighborPos.x - centerPos.x) * lo;
    double edgeY = centerPos.y + (neighborPos.y - centerPos.y) * lo;
    auto blended = sampler.sampleAt(edgeX, edgeY);

    ASSERT_EQ(blended.weightCount, 2u);
    EXPECT_EQ(blended.weights[0].biome, Biome::TemperateGrassland);
    EXPECT_EQ(blended.weights[1].biome, Biome::HotDesert);
    EXPECT_NEAR(blended.weights[0].weight, 0.5f, 0.02f);
    EXPECT_NEAR(blended.weights[1].weight, 0.5f, 0.02f);
    EXPECT_NEAR(blended.weights[0].weight + blended.weights[1].weight, 1.0f, 1e-6f);
    EXPECT_NEAR(blended.elevationMeters, 500.0f, 5.0f);
}

TEST(PlanetSampler, IdenticalInputsProduceIdenticalOutputs) {
    auto worldA = makeWorld(0.0f);
    auto worldB = makeWorld(0.0f);
    PlanetSampler samplerA(worldA, 12.5, -45.0);
    PlanetSampler samplerB(worldB, 12.5, -45.0);

    for (double y = -50000.0; y <= 50000.0; y += 9100.0) {
        for (double x = -50000.0; x <= 50000.0; x += 9100.0) {
            auto a = samplerA.sampleAt(x, y);
            auto b = samplerB.sampleAt(x, y);
            ASSERT_EQ(a.tile, b.tile);
            ASSERT_EQ(a.water, b.water);
            ASSERT_EQ(a.elevationMeters, b.elevationMeters);  // bit-identical
            ASSERT_EQ(a.weightCount, b.weightCount);
            for (uint32_t i = 0; i < a.weightCount; ++i) {
                ASSERT_EQ(a.weights[i].biome, b.weights[i].biome);
                ASSERT_EQ(a.weights[i].weight, b.weights[i].weight);
            }
        }
    }
}

TEST(PlanetSampler, TileLookupMatchesSphereGrid) {
    auto world = makeWorld(0.0f);
    PlanetSampler sampler(world, 0.0, 0.0);

    EXPECT_EQ(sampler.tileAt(0.0, 0.0), world->grid->fromLatLon(0.0, 0.0));

    WorldPos2d pos = worldPosOf(*world, 20.0, -15.0);
    EXPECT_EQ(sampler.tileAt(pos.x, pos.y), world->grid->fromLatLon(20.0, -15.0));
}

TEST(LandingSite, PicksTemperateCoastalLand) {
    auto world = makeWorld(0.0f);

    // (60,0) is ocean in this fixture; turn it into a lone island, so it is
    // coastal (surrounded by ocean) but poleward of the preferred band.
    TileId highLatLand = world->grid->fromLatLon(60.0, 0.0);
    world->data.flags[highLatLand] = 0;
    world->data.biome[highLatLand] = static_cast<uint8_t>(Biome::TemperateGrassland);
    world->data.elevation[highLatLand] = kLandElevationAboveSea;

    LatLon site = findDefaultLandingSite(*world);

    // The site must be temperate-band coastal land, not the high-lat island.
    EXPECT_LE(std::abs(site.latDeg), 45.0);
    TileId siteTile = world->grid->fromLatLon(site.latDeg, site.lonDeg);
    EXPECT_NE(siteTile, highLatLand);
    EXPECT_EQ(world->data.flags[siteTile] & (kFlagOcean | kFlagLake), 0);
    EXPECT_GE(world->data.elevation[siteTile], world->seaLevelMeters);

    std::array<TileId, 6> nbrs{};
    uint32_t count = world->grid->neighbors(siteTile, nbrs);
    bool hasWaterNeighbor = false;
    for (uint32_t i = 0; i < count; ++i) {
        if ((world->data.flags[nbrs[i]] & kFlagOcean) != 0) hasWaterNeighbor = true;
    }
    EXPECT_TRUE(hasWaterNeighbor);
}

TEST(LandingSite, FallsBackToTemperateInlandWhenNoCoast) {
    auto world = makeWorld(0.0f);

    // All-land world: no water anywhere, so no tile is coastal.
    for (TileId t = 0; t < world->grid->tileCount(); ++t) {
        world->data.biome[t] = static_cast<uint8_t>(Biome::TemperateGrassland);
        world->data.flags[t] = 0;
        world->data.elevation[t] = kLandElevationAboveSea;
    }

    TileId expected = kInvalidTile;
    for (TileId t = 0; t < world->grid->tileCount(); ++t) {
        double latDeg = 0.0;
        double lonDeg = 0.0;
        world->grid->latLonOf(t, latDeg, lonDeg);
        if (std::abs(latDeg) <= 45.0) {
            expected = t;
            break;
        }
    }
    ASSERT_NE(expected, kInvalidTile);

    LatLon site = findDefaultLandingSite(*world);
    double latDeg = 0.0;
    double lonDeg = 0.0;
    world->grid->latLonOf(expected, latDeg, lonDeg);
    EXPECT_DOUBLE_EQ(site.latDeg, latDeg);
    EXPECT_DOUBLE_EQ(site.lonDeg, lonDeg);
}

TEST(LandingSite, FallsBackToAnyLandWhenNoneTemperate) {
    auto world = makeWorld(0.0f);

    // Single land tile at lat 60; everything else ocean.
    TileId onlyLand = world->grid->fromLatLon(60.0, 0.0);
    for (TileId t = 0; t < world->grid->tileCount(); ++t) {
        world->data.biome[t] = static_cast<uint8_t>(Biome::Ocean);
        world->data.flags[t] = kFlagOcean;
        world->data.elevation[t] = -kOceanDepthBelowSea;
    }
    world->data.biome[onlyLand] = static_cast<uint8_t>(Biome::ArcticTundra);
    world->data.flags[onlyLand] = 0;
    world->data.elevation[onlyLand] = kLandElevationAboveSea;

    LatLon site = findDefaultLandingSite(*world);
    double latDeg = 0.0;
    double lonDeg = 0.0;
    world->grid->latLonOf(onlyLand, latDeg, lonDeg);
    EXPECT_DOUBLE_EQ(site.latDeg, latDeg);
    EXPECT_DOUBLE_EQ(site.lonDeg, lonDeg);
}

TEST(LandingSite, AllOceanFallsBackToZeroZero) {
    auto world = makeWorld(0.0f);
    for (TileId t = 0; t < world->grid->tileCount(); ++t) {
        world->data.biome[t] = static_cast<uint8_t>(Biome::Ocean);
        world->data.flags[t] = kFlagOcean;
        world->data.elevation[t] = -kOceanDepthBelowSea;
    }

    LatLon site = findDefaultLandingSite(*world);
    EXPECT_DOUBLE_EQ(site.latDeg, 0.0);
    EXPECT_DOUBLE_EQ(site.lonDeg, 0.0);
}

} // namespace worldgen
