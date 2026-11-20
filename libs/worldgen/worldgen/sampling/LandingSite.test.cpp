// The bar (assessLandingTile) and the pick resolution (resolveLandingSite), on synthetic worlds.

#include "worldgen/sampling/LandingSite.h"

#include "worldgen/data/PlanetParams.h"
#include "worldgen/data/WorldData.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <memory>
#include <unordered_set>

namespace worldgen {

namespace {

constexpr uint32_t kRiverSubdivision = 24;
// The lake anchor stands on the great-circle midpoint of two tile centres, which the grid's Voronoi edge
// (the 2D shore) leaves by under 15 m at n=256 but by kilometres at n=24.
constexpr uint32_t kLakeSubdivision = 256;

double dot(const Vec3d& a, const Vec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Grassland with |lon| < 25 and |lat| < 60, ocean elsewhere; a mild, wet climate everywhere.
std::shared_ptr<GeneratedWorld> makeWorld(uint32_t subdivision) {
    auto world = std::make_shared<GeneratedWorld>();
    world->params.gridSubdivision = subdivision;
    world->params.seed = 0x1A4D1ULL;
    world->derived = derive(world->params);
    world->grid = std::make_shared<SphereGrid>(subdivision);
    world->data.allocate(world->grid->tileCount());
    world->seaLevelMeters = 0.0f;
    world->validFields = static_cast<uint32_t>(WorldField::Elevation) | static_cast<uint32_t>(WorldField::Biome) |
                         static_cast<uint32_t>(WorldField::Flags) | static_cast<uint32_t>(WorldField::FlowAccum) |
                         static_cast<uint32_t>(WorldField::Downhill) |
                         static_cast<uint32_t>(WorldField::TemperatureMean) |
                         static_cast<uint32_t>(WorldField::TemperatureRange) |
                         static_cast<uint32_t>(WorldField::Precipitation);
    for (TileId t = 0; t < world->grid->tileCount(); ++t) {
        double lat = 0.0;
        double lon = 0.0;
        world->grid->latLonOf(t, lat, lon);
        const bool land = std::abs(lat) < 60.0 && std::abs(lon) < 25.0;
        world->data.elevation[t] = land ? 500.0f : -3000.0f;
        world->data.biome[t] = static_cast<uint8_t>(land ? Biome::TemperateGrassland : Biome::Ocean);
        world->data.flags[t] = land ? 0 : kFlagOcean;
        world->data.temperatureMean[t] = 150;
        world->data.temperatureRange[t] = 100;
        world->data.precipitation[t] = 900;
    }
    return world;
}

BiomeProvisionTable grasslandProvisions() {
    BiomeProvisionTable provisions{};
    provisions[static_cast<std::size_t>(Biome::TemperateGrassland)] = {300.0f, 300.0f};
    return provisions;
}

// A river from source toward mouth, one tile a step, its flow growing downstream.
void carveRiver(GeneratedWorld& world, TileId source, TileId mouth) {
    const SphereGrid& grid = *world.grid;
    const Vec3d target = grid.tileCenter(mouth);
    std::unordered_set<TileId> visited;
    TileId current = source;
    float flow = 90.0f;
    for (int guard = 0; guard < 1000; ++guard) {
        if (current == mouth || !visited.insert(current).second) break;
        std::array<TileId, 6> neighbours{};
        const uint32_t count = grid.neighbors(current, neighbours);
        int best = -1;
        double bestDot = -2.0;
        for (uint32_t i = 0; i < count; ++i) {
            const double d = dot(grid.tileCenter(neighbours[i]), target);
            if (d > bestDot) {
                bestDot = d;
                best = static_cast<int>(i);
            }
        }
        if (best < 0) break;
        world.data.flags[current] |= kFlagRiver;
        world.data.flowAccum[current] = flow;
        world.data.downhill[current] = static_cast<uint8_t>(best);
        flow += 15.0f;
        current = neighbours[static_cast<uint32_t>(best)];
    }
}

LatLon centreOf(const GeneratedWorld& world, TileId tile) {
    LatLon centre;
    world.grid->latLonOf(tile, centre.latDeg, centre.lonDeg);
    return centre;
}

void expectSameLanding(const LandingSite& a, const LandingSite& b) {
    EXPECT_EQ(a.origin.latDeg, b.origin.latDeg);
    EXPECT_EQ(a.origin.lonDeg, b.origin.lonDeg);
    EXPECT_EQ(a.tile, b.tile);
    EXPECT_EQ(a.feature, b.feature);
    EXPECT_EQ(a.spawn.xMeters, b.spawn.xMeters);
    EXPECT_EQ(a.spawn.yMeters, b.spawn.yMeters);
    EXPECT_EQ(a.spawn.water, b.spawn.water);
    EXPECT_EQ(a.assessment.water, b.assessment.water);
}

// A river carved from (lat, -10) eastward, and the resolution of a pick off its source tile's centre.
struct RiverFixture {
    std::shared_ptr<GeneratedWorld> world;
    TileId                          source{kInvalidTile};
};

RiverFixture riverAtLatitude(double latDeg) {
    RiverFixture fixture{makeWorld(kRiverSubdivision)};
    fixture.source = fixture.world->grid->fromLatLon(latDeg, -10.0);
    carveRiver(*fixture.world, fixture.source, fixture.world->grid->fromLatLon(latDeg, 20.0));
    return fixture;
}

} // namespace

TEST(LandingSiteBar, RiverTileWithFoodWoodAndLivableSeasonsMeetsIt) {
    auto world = makeWorld(kRiverSubdivision);
    const BiomeProvisionTable provisions = grasslandProvisions();
    const TileId tile = world->grid->fromLatLon(0.0, 0.0);

    const TileAssessment good = assessLandingTile(*world, provisions, tile, WaterClass::River);
    EXPECT_TRUE(good.meetsBar());
    EXPECT_EQ(good.habitability, Habitability::Easy);

    EXPECT_FALSE(assessLandingTile(*world, provisions, tile, WaterClass::RainFed).meetsBar()) << "no fresh water";
    EXPECT_FALSE(assessLandingTile(*world, provisions, tile, WaterClass::Coastal).meetsBar()) << "salt water";

    BiomeProvisionTable noWood = provisions;
    noWood[static_cast<std::size_t>(Biome::TemperateGrassland)].woodSourcesPerSquareKilometer = 0.0f;
    const TileAssessment withoutWood = assessLandingTile(*world, noWood, tile, WaterClass::River);
    EXPECT_FALSE(withoutWood.hasWood);
    EXPECT_FALSE(withoutWood.meetsBar());

    BiomeProvisionTable noFood = provisions;
    noFood[static_cast<std::size_t>(Biome::TemperateGrassland)].foodSourcesPerSquareKilometer = 0.0f;
    const TileAssessment withoutFood = assessLandingTile(*world, noFood, tile, WaterClass::River);
    EXPECT_FALSE(withoutFood.hasFood);
    EXPECT_FALSE(withoutFood.meetsBar());

    // A mild mean with a coldest season below -15 C.
    world->data.temperatureMean[tile] = 100;
    world->data.temperatureRange[tile] = 300;
    const TileAssessment bitterWinter = assessLandingTile(*world, provisions, tile, WaterClass::River);
    EXPECT_FALSE(bitterWinter.livableTemperature);
    EXPECT_FALSE(bitterWinter.meetsBar());
}

TEST(LandingSiteBar, DryColdRainFedTileRatesHarsh) {
    auto world = makeWorld(kRiverSubdivision);
    const TileId tile = world->grid->fromLatLon(0.0, 0.0);
    world->data.temperatureMean[tile] = -200;
    world->data.precipitation[tile] = 40;

    EXPECT_EQ(assessLandingTile(*world, BiomeProvisionTable{}, tile, WaterClass::RainFed).habitability,
              Habitability::Harsh);
}

TEST(LandingSiteResolution, PickOnRiverTileResolvesToItsCentre) {
    const RiverFixture fixture = riverAtLatitude(0.0);
    const LatLon centre = centreOf(*fixture.world, fixture.source);
    const LatLon pick{centre.latDeg + 0.3, centre.lonDeg - 0.2};
    ASSERT_EQ(fixture.world->grid->fromLatLon(pick.latDeg, pick.lonDeg), fixture.source);

    const LandingSite site = resolveLandingSite(fixture.world, grasslandProvisions(), pick);
    EXPECT_EQ(site.origin.latDeg, centre.latDeg);
    EXPECT_EQ(site.origin.lonDeg, centre.lonDeg);
    EXPECT_EQ(site.tile, fixture.source);
    EXPECT_EQ(site.feature, FreshWaterFeature::River);
    EXPECT_EQ(site.spawn.water, SpawnWater::River);
    EXPECT_LE(site.spawn.waterDistanceMeters, 6.0f);
    EXPECT_TRUE(site.recommended());

    expectSameLanding(resolveLandingSite(fixture.world, grasslandProvisions(), site.origin), site);
}

TEST(LandingSiteResolution, PickOneTileFromRiverResolvesToTheRiverTile) {
    const RiverFixture fixture = riverAtLatitude(0.0);
    const GeneratedWorld& world = *fixture.world;

    // A dry neighbour of the source, off the river.
    std::array<TileId, 6> neighbours{};
    const uint32_t count = world.grid->neighbors(fixture.source, neighbours);
    TileId dry = kInvalidTile;
    for (uint32_t i = 0; i < count && dry == kInvalidTile; ++i) {
        if ((world.data.flags[neighbours[i]] & kFlagRiver) == 0) dry = neighbours[i];
    }
    ASSERT_NE(dry, kInvalidTile);

    const LandingSite site = resolveLandingSite(fixture.world, grasslandProvisions(), centreOf(world, dry));
    ASSERT_EQ(site.feature, FreshWaterFeature::River);
    EXPECT_NE(world.data.flags[site.tile] & kFlagRiver, 0) << "the landing tile is the river tile";
    const LatLon riverCentre = centreOf(world, site.tile);
    EXPECT_EQ(site.origin.latDeg, riverCentre.latDeg);
    EXPECT_EQ(site.origin.lonDeg, riverCentre.lonDeg);

    expectSameLanding(resolveLandingSite(fixture.world, grasslandProvisions(), site.origin), site);
}

TEST(LandingSiteResolution, RiverTilePastFortyFiveDegreesThatMeetsTheBarIsRecommended) {
    const RiverFixture fixture = riverAtLatitude(52.0);
    const LandingSite site =
        resolveLandingSite(fixture.world, grasslandProvisions(), centreOf(*fixture.world, fixture.source));
    EXPECT_GT(std::abs(site.origin.latDeg), 45.0);
    EXPECT_TRUE(site.recommended());
}

TEST(LandingSiteResolution, DryPickStaysAtThePick) {
    auto world = makeWorld(kRiverSubdivision);
    const LatLon pick{1.3, 2.1};

    const LandingSite site = resolveLandingSite(world, grasslandProvisions(), pick);
    EXPECT_EQ(site.origin.latDeg, pick.latDeg);
    EXPECT_EQ(site.origin.lonDeg, pick.lonDeg);
    EXPECT_EQ(site.tile, world->grid->fromLatLon(pick.latDeg, pick.lonDeg));
    EXPECT_EQ(site.feature, FreshWaterFeature::None);
    EXPECT_EQ(site.assessment.water, WaterClass::RainFed);
    EXPECT_FALSE(site.recommended());

    expectSameLanding(resolveLandingSite(world, grasslandProvisions(), site.origin), site);
}

TEST(LandingSiteResolution, PickBesideLakeResolvesToItsShore) {
    auto world = makeWorld(kLakeSubdivision);
    const TileId land = world->grid->fromLatLon(0.0, 0.0);
    std::array<TileId, 6> neighbours{};
    world->grid->neighbors(land, neighbours);
    const TileId lake = neighbours[0];
    world->data.flags[lake] |= kFlagLake;
    world->data.biome[lake] = static_cast<uint8_t>(Biome::Lake);

    const LandingSite site = resolveLandingSite(world, grasslandProvisions(), centreOf(*world, land));
    ASSERT_EQ(site.feature, FreshWaterFeature::Lake);
    EXPECT_EQ(site.tile, land);
    EXPECT_EQ(site.spawn.water, SpawnWater::Lake);
    EXPECT_GT(site.spawn.waterDistanceMeters, 0.0f);
    EXPECT_LE(site.spawn.waterDistanceMeters, 6.0f);
    EXPECT_EQ(site.assessment.water, WaterClass::Lake);
    EXPECT_TRUE(site.recommended());

    expectSameLanding(resolveLandingSite(world, grasslandProvisions(), site.origin), site);
}

} // namespace worldgen
