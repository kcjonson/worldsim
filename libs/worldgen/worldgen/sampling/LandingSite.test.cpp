// The bar (assessLandingTile), the pick resolution (resolveLandingSite), and the suggested-site finder
// (findSuggestedLandingSites), on synthetic worlds and one small generated planet.

#include "worldgen/sampling/LandingSite.h"

#include "worldgen/pipeline/PlanetGenerator.test.h"
#include "worldgen/sampling/PlanetSampler.h"
#include "worldgen/sampling/RiverNetwork2D.h"
#include "worldgen/sampling/RiverWorld.test.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <stop_token>
#include <vector>

namespace worldgen {

namespace {

using test_support::addLake;
using test_support::carveRiver;
using test_support::dotProduct;
using test_support::GrasslandBlock;
using test_support::grasslandWorld;

std::shared_ptr<GeneratedWorld> makeWorld() { return grasslandWorld(GrasslandBlock{}); }

BiomeProvisionTable grasslandProvisions() {
    BiomeProvisionTable provisions{};
    provisions[static_cast<std::size_t>(Biome::TemperateGrassland)] = {300.0f, 300.0f};
    return provisions;
}

LatLon centreOf(const GeneratedWorld& world, TileId tile) {
    LatLon centre;
    world.grid->latLonOf(tile, centre.latDeg, centre.lonDeg);
    return centre;
}

void expectSameLanding(const LandingSite& actual, const LandingSite& expected) {
    EXPECT_EQ(actual.origin.latDeg, expected.origin.latDeg);
    EXPECT_EQ(actual.origin.lonDeg, expected.origin.lonDeg);
    EXPECT_EQ(actual.tile, expected.tile);
    EXPECT_EQ(actual.feature, expected.feature);
    EXPECT_EQ(actual.spawn.xMeters, expected.spawn.xMeters);
    EXPECT_EQ(actual.spawn.yMeters, expected.spawn.yMeters);
    EXPECT_EQ(actual.spawn.water, expected.spawn.water);
    EXPECT_EQ(actual.assessment.water, expected.assessment.water);
}

// A river carved from (lat, -10) eastward, and the resolution of a pick off its source tile's centre.
struct RiverFixture {
    std::shared_ptr<GeneratedWorld> world;
    TileId                          source{kInvalidTile};
};

RiverFixture riverAtLatitude(double latDeg) {
    RiverFixture fixture{makeWorld()};
    fixture.source = fixture.world->grid->fromLatLon(latDeg, -10.0);
    carveRiver(*fixture.world, fixture.source, fixture.world->grid->fromLatLon(latDeg, 20.0));
    return fixture;
}

// Grassland south of the equator and deciduous forest north of it, each with a river and a lake far
// enough from the others to be a site of its own.
std::shared_ptr<GeneratedWorld> twoBiomeWorld() {
    auto world = makeWorld();
    for (TileId tile = 0; tile < world->grid->tileCount(); ++tile) {
        if (centreOf(*world, tile).latDeg > 0.0 && world->data.biome[tile] == static_cast<uint8_t>(Biome::TemperateGrassland)) {
            world->data.biome[tile] = static_cast<uint8_t>(Biome::TemperateDeciduousForest);
        }
    }
    carveRiver(*world, world->grid->fromLatLon(-30.0, -10.0), world->grid->fromLatLon(-30.0, 20.0));
    carveRiver(*world, world->grid->fromLatLon(28.0, -10.0), world->grid->fromLatLon(28.0, 20.0));
    addLake(*world, world->grid->fromLatLon(-6.0, -16.0));
    addLake(*world, world->grid->fromLatLon(6.0, 16.0));
    return world;
}

BiomeProvisionTable twoBiomeProvisions() {
    BiomeProvisionTable provisions = grasslandProvisions();
    provisions[static_cast<std::size_t>(Biome::TemperateDeciduousForest)] = {300.0f, 300.0f};
    return provisions;
}

double arcDegrees(const GeneratedWorld& world, TileId from, TileId to) {
    const double cosine = std::clamp(dotProduct(world.grid->tileCenter(from), world.grid->tileCenter(to)), -1.0, 1.0);
    return std::acos(cosine) * 180.0 / std::numbers::pi;
}

// The neighbour of `tile` whose centre lies most nearly in `direction` (a tangent at the tile's centre).
TileId neighbourToward(const SphereGrid& grid, TileId tile, const Vec3d& direction, double& alignment) {
    const Vec3d centre = grid.tileCenter(tile);
    std::array<TileId, 6> neighbours{};
    const uint32_t count = grid.neighbors(tile, neighbours);
    TileId best = kInvalidTile;
    alignment = -2.0;
    for (uint32_t i = 0; i < count; ++i) {
        const Vec3d other = grid.tileCenter(neighbours[i]);
        const Vec3d offset{other.x - centre.x, other.y - centre.y, other.z - centre.z};
        const double along = dotProduct(offset, direction) / std::sqrt(dotProduct(offset, offset));
        if (along > alignment) {
            alignment = along;
            best = neighbours[i];
        }
    }
    return best;
}

Vec3d eastAt(const GeneratedWorld& world, TileId tile) {
    const double lonRadians = centreOf(world, tile).lonDeg * std::numbers::pi / 180.0;
    return {-std::sin(lonRadians), std::cos(lonRadians), 0.0};
}

// A land river tile on a planet shrunk so tiles are 140 m apart, in a lake but for its river neighbour to
// the east and the land from two tiles east on. Its own channel, 110 m wide, leaves no dry point 30 m from
// water within its search; the land opens out past the neighbour, which its river neighbour's search
// reaches and its own does not.
struct NarrowLandFixture {
    std::shared_ptr<GeneratedWorld> world;
    TileId                          narrow{kInvalidTile};
    TileId                          riverNeighbour{kInvalidTile};
};

NarrowLandFixture narrowLand() {
    constexpr double kTileSpacingMeters = 140.0;
    constexpr float  kWideRiverFlow = 20000.0f; // the widest channel, 110 m
    NarrowLandFixture fixture{makeWorld()};
    GeneratedWorld&   world = *fixture.world;
    const SphereGrid& grid = *world.grid;

    // The tile whose river runs most nearly due east over two tiles, so the open land stays outside its
    // search square.
    double bestAlignment = -2.0;
    TileId beyond = kInvalidTile;
    for (TileId tile = 0; tile < grid.tileCount(); ++tile) {
        const LatLon centre = centreOf(world, tile);
        if (std::abs(centre.latDeg) > 10.0 || std::abs(centre.lonDeg) > 10.0) continue;
        double       firstAlignment = 0.0;
        double       secondAlignment = 0.0;
        const TileId next = neighbourToward(grid, tile, eastAt(world, tile), firstAlignment);
        const TileId after = neighbourToward(grid, next, eastAt(world, tile), secondAlignment);
        if (std::min(firstAlignment, secondAlignment) > bestAlignment) {
            bestAlignment = std::min(firstAlignment, secondAlignment);
            fixture.narrow = tile;
            fixture.riverNeighbour = next;
            beyond = after;
        }
    }

    const Vec3d  narrowCentre = grid.tileCenter(fixture.narrow);
    const double spacingRadians =
        std::acos(std::clamp(dotProduct(narrowCentre, grid.tileCenter(fixture.riverNeighbour)), -1.0, 1.0));
    world.derived.planetRadiusMeters = kTileSpacingMeters / spacingRadians;

    // Lake round the narrow tile out of every search's reach, except its river neighbour and the land from
    // two tiles east on.
    const Vec3d east = eastAt(world, fixture.narrow);
    for (TileId tile = 0; tile < grid.tileCount(); ++tile) {
        if (tile == fixture.narrow || tile == fixture.riverNeighbour) continue;
        const Vec3d  centre = grid.tileCenter(tile);
        const Vec3d  offset{centre.x - narrowCentre.x, centre.y - narrowCentre.y, centre.z - narrowCentre.z};
        const double eastTiles = dotProduct(offset, east) / spacingRadians;
        const double arc = std::acos(std::clamp(dotProduct(narrowCentre, centre), -1.0, 1.0));
        if (arc < 4.0 * spacingRadians && eastTiles < 1.9) addLake(world, tile);
    }

    const LatLon beyondCentre = centreOf(world, beyond);
    carveRiver(world, beyond, grid.fromLatLon(beyondCentre.latDeg, beyondCentre.lonDeg + 20.0));
    const auto flowInto = [&](TileId from, TileId to) {
        std::array<TileId, 6> neighbours{};
        const uint32_t count = grid.neighbors(from, neighbours);
        for (uint32_t i = 0; i < count; ++i) {
            if (neighbours[i] == to) world.data.downhill[from] = static_cast<uint8_t>(i);
        }
        world.data.flags[from] |= kFlagRiver;
    };
    flowInto(fixture.narrow, fixture.riverNeighbour);
    flowInto(fixture.riverNeighbour, beyond);
    for (TileId tile = 0; tile < grid.tileCount(); ++tile) {
        if ((world.data.flags[tile] & kFlagRiver) != 0) world.data.flowAccum[tile] = kWideRiverFlow;
    }
    return fixture;
}

// Grassland from lon -70 to 70 with five rivers in its west half, every one long enough for several sites,
// and, when asked, five lakes in its east half far from all of them.
std::shared_ptr<GeneratedWorld> riversWestLakesEast(bool withLakes) {
    auto world = grasslandWorld(GrasslandBlock{.landLonDeg = 70.0});
    for (const double latDeg : {-40.0, -20.0, 0.0, 20.0, 40.0}) {
        carveRiver(*world, world->grid->fromLatLon(latDeg, -65.0), world->grid->fromLatLon(latDeg, -10.0));
    }
    if (withLakes) {
        for (const auto& [latDeg, lonDeg] : std::array<std::pair<double, double>, 5>{{{-30.0, 30.0}, {0.0, 30.0}, {30.0, 30.0}, {-15.0, 55.0}, {15.0, 55.0}}}) {
            addLake(*world, world->grid->fromLatLon(latDeg, lonDeg));
        }
    }
    return world;
}

std::size_t countOf(const std::vector<LandingSite>& sites, FreshWaterFeature feature) {
    return static_cast<std::size_t>(
        std::count_if(sites.begin(), sites.end(), [feature](const LandingSite& site) { return site.feature == feature; }));
}

} // namespace

TEST(LandingSiteBar, RiverTileWithFoodWoodAndLivableSeasonsMeetsIt) {
    auto world = makeWorld();
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
    auto world = makeWorld();
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
    EXPECT_NEAR(site.spawn.waterDistanceMeters, kLandingWaterDistanceMeters, kLandingWaterDistanceToleranceMeters);
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

TEST(LandingSiteResolution, RiverTilePastFortyFiveDegreesIsRecommendedButNeverSuggested) {
    const RiverFixture fixture = riverAtLatitude(52.0);
    const LandingSite site =
        resolveLandingSite(fixture.world, grasslandProvisions(), centreOf(*fixture.world, fixture.source));
    EXPECT_GT(std::abs(site.origin.latDeg), 45.0);
    EXPECT_TRUE(site.recommended());
    expectSameLanding(resolveLandingSite(fixture.world, grasslandProvisions(), site.origin), site);

    EXPECT_TRUE(findSuggestedLandingSites(fixture.world, grasslandProvisions(), {}).empty())
        << "the river past the latitude cut is the only fresh water";
}

TEST(LandingSiteResolution, DryPickStaysAtThePick) {
    auto world = makeWorld();
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

// The search on a narrow strip finds fresh water but no dry point 30 m from it; that drop is not a
// landing, and the resolution goes on to the river neighbour, whose search reaches open land.
TEST(LandingSiteResolution, NarrowLandIsNotALandingAndTheNextAnchorIsTried) {
    const NarrowLandFixture fixture = narrowLand();
    const LatLon            narrowCentre = centreOf(*fixture.world, fixture.narrow);

    const SpawnSite narrowDrop = findWatersideSpawn(fixture.world, narrowCentre.latDeg, narrowCentre.lonDeg);
    ASSERT_TRUE(narrowDrop.water == SpawnWater::River || narrowDrop.water == SpawnWater::Lake);
    ASSERT_FALSE(narrowDrop.clear) << "the narrow tile's best point is " << narrowDrop.waterDistanceMeters << " m from water";

    const LandingSite site = resolveLandingSite(fixture.world, grasslandProvisions(), narrowCentre);
    EXPECT_EQ(site.tile, fixture.riverNeighbour);
    const LatLon neighbourCentre = centreOf(*fixture.world, fixture.riverNeighbour);
    EXPECT_EQ(site.origin.latDeg, neighbourCentre.latDeg);
    EXPECT_EQ(site.origin.lonDeg, neighbourCentre.lonDeg);
    EXPECT_NE(site.feature, FreshWaterFeature::None);
    EXPECT_TRUE(site.spawn.clear);
    EXPECT_NEAR(site.spawn.waterDistanceMeters, kLandingWaterDistanceMeters, kLandingWaterDistanceToleranceMeters);

    expectSameLanding(resolveLandingSite(fixture.world, grasslandProvisions(), site.origin), site);
}

TEST(LandingSiteResolution, PickBesideLakeResolvesToItsShore) {
    auto world = makeWorld();
    const TileId land = world->grid->fromLatLon(0.0, 0.0);
    std::array<TileId, 6> neighbours{};
    world->grid->neighbors(land, neighbours);
    addLake(*world, neighbours[0]);

    const LandingSite site = resolveLandingSite(world, grasslandProvisions(), centreOf(*world, land));
    ASSERT_EQ(site.feature, FreshWaterFeature::Lake);
    EXPECT_EQ(site.tile, land);
    EXPECT_EQ(site.spawn.water, SpawnWater::Lake);
    EXPECT_NEAR(site.spawn.waterDistanceMeters, kLandingWaterDistanceMeters, kLandingWaterDistanceToleranceMeters);

    // The shore read independently, rings 0.5 m apart round the drop: the first water is the lake, a
    // sample step either side of the reported distance.
    const PlanetSampler sampler(world, site.origin.latDeg, site.origin.lonDeg);
    double              nearestLake = -1.0;
    for (double radius = 0.0; nearestLake < 0.0 && radius <= 40.0; radius += 0.5) {
        const int points = std::max(1, static_cast<int>(std::ceil(2.0 * std::numbers::pi * radius / 0.5)));
        for (int i = 0; i < points && nearestLake < 0.0; ++i) {
            const double angle = 2.0 * std::numbers::pi * i / points;
            const auto   sample = sampler.sampleAt(site.spawn.xMeters + radius * std::cos(angle),
                                                   site.spawn.yMeters + radius * std::sin(angle));
            if (!sample.water) continue;
            EXPECT_EQ(sample.weights[0].biome, Biome::Lake);
            nearestLake = radius;
        }
    }
    EXPECT_NEAR(nearestLake, site.spawn.waterDistanceMeters, 3.0);
    EXPECT_EQ(site.assessment.water, WaterClass::Lake);
    EXPECT_TRUE(site.recommended());

    expectSameLanding(resolveLandingSite(world, grasslandProvisions(), site.origin), site);
}

TEST(LandingSiteFinder, SitesMeetTheBarStandApartAndVaryByKindAndBiome) {
    const auto world = twoBiomeWorld();
    const std::vector<LandingSite> sites = findSuggestedLandingSites(world, twoBiomeProvisions(), {});
    ASSERT_FALSE(sites.empty());
    EXPECT_LE(sites.size(), kSuggestedSiteCount);

    bool grassland = false;
    bool forest = false;
    for (std::size_t i = 0; i < sites.size(); ++i) {
        const LandingSite& site = sites[i];
        EXPECT_TRUE(site.recommended()) << "site " << i;
        EXPECT_TRUE(site.spawn.clear) << "site " << i;
        EXPECT_LE(std::abs(centreOf(*world, site.tile).latDeg), kMaximumSuggestedLatitudeDegrees) << "site " << i;
        for (std::size_t j = 0; j < i; ++j) {
            EXPECT_GE(arcDegrees(*world, site.tile, sites[j].tile), kMinimumSiteSeparationDegrees) << i << " and " << j;
        }
        const auto biome = static_cast<Biome>(world->data.biome[site.tile]);
        grassland = grassland || biome == Biome::TemperateGrassland;
        forest = forest || biome == Biome::TemperateDeciduousForest;
    }
    EXPECT_GT(countOf(sites, FreshWaterFeature::River), 0u);
    EXPECT_GT(countOf(sites, FreshWaterFeature::Lake), 0u);
    EXPECT_TRUE(grassland);
    EXPECT_TRUE(forest);

    // Every river candidate here rates the same, so the first site is the lowest TileId, the river in the
    // north. With the biomes swapped it is a grassland river, and ample provisions in the forest lift the
    // southern forest river above it.
    ASSERT_EQ(sites[0].feature, FreshWaterFeature::River);
    ASSERT_EQ(static_cast<Biome>(world->data.biome[sites[0].tile]), Biome::TemperateDeciduousForest);
    auto swapped = std::make_shared<GeneratedWorld>(*world);
    for (uint8_t& biome : swapped->data.biome) {
        if (biome == static_cast<uint8_t>(Biome::TemperateGrassland)) {
            biome = static_cast<uint8_t>(Biome::TemperateDeciduousForest);
        } else if (biome == static_cast<uint8_t>(Biome::TemperateDeciduousForest)) {
            biome = static_cast<uint8_t>(Biome::TemperateGrassland);
        }
    }
    BiomeProvisionTable ampleForest = twoBiomeProvisions();
    ampleForest[static_cast<std::size_t>(Biome::TemperateDeciduousForest)] = {kAmpleFoodSourcesPerSquareKilometer,
                                                                             kAmpleWoodSourcesPerSquareKilometer};
    SuggestedSiteQuery first;
    first.maximumSites = 1;
    const std::vector<LandingSite> ranked = findSuggestedLandingSites(swapped, ampleForest, first);
    ASSERT_EQ(ranked.size(), 1u);
    EXPECT_EQ(ranked[0].feature, FreshWaterFeature::River);
    EXPECT_EQ(static_cast<Biome>(swapped->data.biome[ranked[0].tile]), Biome::TemperateDeciduousForest);
    EXPECT_GT(ranked[0].tile, sites[0].tile) << "the grassland river has the lower TileId";
    EXPECT_EQ(ranked[0].assessment.habitabilityScore, sites[0].assessment.habitabilityScore + 1);
}

// Rivers outrank lakes, so without the cap every site would be a river; with lake sites to be had, the rivers
// stop at kMaximumSitesOfOneKind and the rest are lakes.
TEST(LandingSiteFinder, OneKindStopsAtItsCapWhileTheOtherHasSites) {
    const std::vector<LandingSite> riversOnly = findSuggestedLandingSites(riversWestLakesEast(false), grasslandProvisions(), {});
    ASSERT_GT(countOf(riversOnly, FreshWaterFeature::River), kMaximumSitesOfOneKind) << "more river sites than the cap exist";

    const std::vector<LandingSite> sites = findSuggestedLandingSites(riversWestLakesEast(true), grasslandProvisions(), {});
    EXPECT_EQ(countOf(sites, FreshWaterFeature::River), kMaximumSitesOfOneKind);
    EXPECT_EQ(countOf(sites, FreshWaterFeature::Lake), kSuggestedSiteCount - kMaximumSitesOfOneKind);
}

TEST(LandingSiteFinder, SameWorldGivesTheSameSites) {
    const auto world = twoBiomeWorld();
    const std::vector<LandingSite> first = findSuggestedLandingSites(world, twoBiomeProvisions(), {});
    const std::vector<LandingSite> second = findSuggestedLandingSites(world, twoBiomeProvisions(), {});
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) expectSameLanding(first[i], second[i]);
}

TEST(LandingSiteFinder, DesertAndOceanWorldsGiveNoSites) {
    auto desert = twoBiomeWorld();
    for (TileId tile = 0; tile < desert->grid->tileCount(); ++tile) {
        if ((desert->data.flags[tile] & (kFlagOcean | kFlagLake)) == 0) {
            desert->data.biome[tile] = static_cast<uint8_t>(Biome::HotDesert);
        }
    }
    BiomeProvisionTable desertProvisions = twoBiomeProvisions();
    desertProvisions[static_cast<std::size_t>(Biome::HotDesert)] = {300.0f, 0.0f};
    EXPECT_TRUE(findSuggestedLandingSites(desert, desertProvisions, {}).empty());

    auto ocean = makeWorld();
    for (TileId tile = 0; tile < ocean->grid->tileCount(); ++tile) {
        ocean->data.elevation[tile] = -3000.0f;
        ocean->data.biome[tile] = static_cast<uint8_t>(Biome::Ocean);
        ocean->data.flags[tile] = kFlagOcean;
    }
    EXPECT_TRUE(findSuggestedLandingSites(ocean, twoBiomeProvisions(), {}).empty());
}

TEST(LandingSiteFinder, StopDuringTheTileScanReturnsNothing) {
    const auto       world = twoBiomeWorld();
    std::stop_source stop;
    int              checkpoints = 0;
    SuggestedSiteQuery query;
    query.stopToken = stop.get_token();
    query.checkpoint = [&] {
        ++checkpoints;
        stop.request_stop();
    };
    EXPECT_TRUE(findSuggestedLandingSites(world, twoBiomeProvisions(), query).empty());
    EXPECT_EQ(checkpoints, 1);
}

// This world's 5,762 tiles take one checkpoint in the scan; the next ones come before each resolution of the
// selection. Stopped before the second resolution, the finder returns the first site.
TEST(LandingSiteFinder, StopDuringSelectionReturnsTheSitesTakenSoFar) {
    const auto                     world = twoBiomeWorld();
    const std::vector<LandingSite> everySite = findSuggestedLandingSites(world, twoBiomeProvisions(), {});
    ASSERT_GE(everySite.size(), 2u);

    constexpr int    kCheckpointBeforeSecondResolution = 3;
    std::stop_source stop;
    int              checkpoints = 0;
    SuggestedSiteQuery query;
    query.stopToken = stop.get_token();
    query.checkpoint = [&] {
        if (++checkpoints == kCheckpointBeforeSecondResolution) stop.request_stop();
    };
    const std::vector<LandingSite> taken = findSuggestedLandingSites(world, twoBiomeProvisions(), query);
    ASSERT_EQ(taken.size(), 1u);
    expectSameLanding(taken[0], everySite[0]);
}

// Every suggested site's drop is clear: 30 m from water within the tolerance, the distance read independently
// round it (the channels its RiverNetwork2D gathers for a river site, the lake's samples for a lake site).
TEST(LandingSiteFinderHeavy, GeneratedPlanetSitesStandClearOfTheirWater) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision = 64;
    params.seed = 42;
    const std::shared_ptr<const GeneratedWorld> world = runToCompletion(params);
    ASSERT_NE(world, nullptr);

    // The shipped wood biomes, every one with food.
    BiomeProvisionTable provisions{};
    for (const Biome biome : {Biome::TropicalRainforest, Biome::TropicalSeasonalForest, Biome::TemperateDeciduousForest,
                              Biome::TemperateRainforest, Biome::BorealForest, Biome::MontaneForest,
                              Biome::TemperateGrassland, Biome::Beach}) {
        provisions[static_cast<std::size_t>(biome)] = {300.0f, 300.0f};
    }

    const std::vector<LandingSite> sites = findSuggestedLandingSites(world, provisions, {});
    ASSERT_FALSE(sites.empty());
    constexpr double kReachMeters = kLandingWaterDistanceMeters + kLandingWaterDistanceToleranceMeters + 3.0;
    for (std::size_t i = 0; i < sites.size(); ++i) {
        const LandingSite& site = sites[i];
        const double       dropX = site.spawn.xMeters;
        const double       dropY = site.spawn.yMeters;
        EXPECT_TRUE(site.spawn.clear) << "site " << i;
        EXPECT_GE(site.spawn.waterDistanceMeters, kLandingWaterDistanceMeters - kLandingWaterDistanceToleranceMeters) << "site " << i;
        EXPECT_LE(site.spawn.waterDistanceMeters, kLandingWaterDistanceMeters + kLandingWaterDistanceToleranceMeters) << "site " << i;

        double nearest = -1.0;
        if (site.feature == FreshWaterFeature::River) {
            const RiverNetwork2D rivers(world, site.origin.latDeg, site.origin.lonDeg);
            std::vector<RiverNetwork2D::Segment> segments;
            rivers.gatherSegments(dropX - kReachMeters, dropY - kReachMeters, dropX + kReachMeters, dropY + kReachMeters, segments);
            for (const RiverNetwork2D::Segment& segment : segments) {
                const double spanX = segment.x1 - segment.x0;
                const double spanY = segment.y1 - segment.y0;
                const double lengthSquared = spanX * spanX + spanY * spanY;
                const double along = lengthSquared > 0.0
                    ? std::clamp(((dropX - segment.x0) * spanX + (dropY - segment.y0) * spanY) / lengthSquared, 0.0, 1.0)
                    : 0.0;
                const double halfWidth = segment.halfWidth0 + (segment.halfWidth1 - segment.halfWidth0) * along;
                const double edge = std::hypot(dropX - (segment.x0 + spanX * along), dropY - (segment.y0 + spanY * along)) - halfWidth;
                if (nearest < 0.0 || edge < nearest) nearest = edge;
            }
            EXPECT_NEAR(nearest, site.spawn.waterDistanceMeters, 0.5) << "site " << i;
        } else {
            ASSERT_EQ(site.feature, FreshWaterFeature::Lake) << "site " << i;
            const PlanetSampler sampler(world, site.origin.latDeg, site.origin.lonDeg);
            for (double radius = 0.0; nearest < 0.0 && radius <= kReachMeters; radius += 0.5) {
                const int points = std::max(1, static_cast<int>(std::ceil(2.0 * std::numbers::pi * radius / 0.5)));
                for (int j = 0; j < points && nearest < 0.0; ++j) {
                    const double angle = 2.0 * std::numbers::pi * j / points;
                    const auto   sample = sampler.sampleAt(dropX + radius * std::cos(angle), dropY + radius * std::sin(angle));
                    if (sample.water && sample.weights[0].biome == Biome::Lake) nearest = radius;
                }
            }
            EXPECT_NEAR(nearest, site.spawn.waterDistanceMeters, 3.0) << "site " << i;
        }
    }
}

} // namespace worldgen
