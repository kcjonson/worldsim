// The bar (assessLandingTile), the pick resolution (resolveLandingSite), and the suggested-site finder
// (findSuggestedLandingSites), on synthetic worlds and one small generated planet.

#include "worldgen/sampling/LandingSite.h"

#include "worldgen/data/PlanetParams.h"
#include "worldgen/data/WorldData.h"
#include "worldgen/pipeline/PlanetGenerator.test.h"
#include "worldgen/sampling/PlanetSampler.h"
#include "worldgen/sampling/RiverNetwork2D.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <stop_token>
#include <unordered_set>
#include <vector>

namespace worldgen {

namespace {

constexpr uint32_t kSubdivision = 24;

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
    RiverFixture fixture{makeWorld(kSubdivision)};
    fixture.source = fixture.world->grid->fromLatLon(latDeg, -10.0);
    carveRiver(*fixture.world, fixture.source, fixture.world->grid->fromLatLon(latDeg, 20.0));
    return fixture;
}

void addLake(GeneratedWorld& world, double latDeg, double lonDeg) {
    const TileId lake = world.grid->fromLatLon(latDeg, lonDeg);
    world.data.flags[lake] |= kFlagLake;
    world.data.biome[lake] = static_cast<uint8_t>(Biome::Lake);
}

// Grassland south of the equator and deciduous forest north of it, each with a river and a lake far
// enough from the others to be a site of its own.
std::shared_ptr<GeneratedWorld> twoBiomeWorld() {
    auto world = makeWorld(kSubdivision);
    for (TileId t = 0; t < world->grid->tileCount(); ++t) {
        double lat = 0.0;
        double lon = 0.0;
        world->grid->latLonOf(t, lat, lon);
        if (lat > 0.0 && world->data.biome[t] == static_cast<uint8_t>(Biome::TemperateGrassland)) {
            world->data.biome[t] = static_cast<uint8_t>(Biome::TemperateDeciduousForest);
        }
    }
    carveRiver(*world, world->grid->fromLatLon(-30.0, -10.0), world->grid->fromLatLon(-30.0, 20.0));
    carveRiver(*world, world->grid->fromLatLon(28.0, -10.0), world->grid->fromLatLon(28.0, 20.0));
    addLake(*world, -6.0, -16.0);
    addLake(*world, 6.0, 16.0);
    return world;
}

BiomeProvisionTable twoBiomeProvisions() {
    BiomeProvisionTable provisions = grasslandProvisions();
    provisions[static_cast<std::size_t>(Biome::TemperateDeciduousForest)] = {300.0f, 300.0f};
    return provisions;
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
        const double length = std::sqrt(dot(offset, offset));
        const double along = dot(offset, direction) / length;
        if (along > alignment) {
            alignment = along;
            best = neighbours[i];
        }
    }
    return best;
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
    NarrowLandFixture fixture{makeWorld(kSubdivision)};
    GeneratedWorld&   world = *fixture.world;
    const SphereGrid& grid = *world.grid;

    // The tile whose river runs most nearly due east over two tiles, so the open land stays outside its
    // search square.
    double bestAlignment = -2.0;
    TileId beyond = kInvalidTile;
    for (TileId tile = 0; tile < grid.tileCount(); ++tile) {
        const LatLon centre = centreOf(world, tile);
        if (std::abs(centre.latDeg) > 10.0 || std::abs(centre.lonDeg) > 10.0) continue;
        const double lonRadians = centre.lonDeg * std::numbers::pi / 180.0;
        const Vec3d  east{-std::sin(lonRadians), std::cos(lonRadians), 0.0};
        double       firstAlignment = 0.0;
        double       secondAlignment = 0.0;
        const TileId next = neighbourToward(grid, tile, east, firstAlignment);
        const TileId after = neighbourToward(grid, next, east, secondAlignment);
        if (std::min(firstAlignment, secondAlignment) > bestAlignment) {
            bestAlignment = std::min(firstAlignment, secondAlignment);
            fixture.narrow = tile;
            fixture.riverNeighbour = next;
            beyond = after;
        }
    }

    const Vec3d narrowCentre = grid.tileCenter(fixture.narrow);
    const double spacingRadians = std::acos(std::clamp(dot(narrowCentre, grid.tileCenter(fixture.riverNeighbour)), -1.0, 1.0));
    world.derived.planetRadiusMeters = kTileSpacingMeters / spacingRadians;

    // Lake round the narrow tile out of every search's reach, except its river neighbour and the land from
    // two tiles east on.
    const double lonRadians = centreOf(world, fixture.narrow).lonDeg * std::numbers::pi / 180.0;
    const Vec3d  east{-std::sin(lonRadians), std::cos(lonRadians), 0.0};
    for (TileId tile = 0; tile < grid.tileCount(); ++tile) {
        if (tile == fixture.narrow || tile == fixture.riverNeighbour) continue;
        const Vec3d  centre = grid.tileCenter(tile);
        const Vec3d  offset{centre.x - narrowCentre.x, centre.y - narrowCentre.y, centre.z - narrowCentre.z};
        const double eastTiles = dot(offset, east) / spacingRadians;
        const double arc = std::acos(std::clamp(dot(narrowCentre, centre), -1.0, 1.0));
        if (arc < 4.0 * spacingRadians && eastTiles < 1.9) {
            world.data.flags[tile] |= kFlagLake;
            world.data.biome[tile] = static_cast<uint8_t>(Biome::Lake);
        }
    }

    const LatLon beyondLatLon = centreOf(world, beyond);
    carveRiver(world, beyond, grid.fromLatLon(beyondLatLon.latDeg, beyondLatLon.lonDeg + 20.0));
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

double arcDegrees(const GeneratedWorld& world, TileId a, TileId b) {
    const double cosine = std::clamp(dot(world.grid->tileCenter(a), world.grid->tileCenter(b)), -1.0, 1.0);
    return std::acos(cosine) * 180.0 / 3.14159265358979323846;
}

} // namespace

TEST(LandingSiteBar, RiverTileWithFoodWoodAndLivableSeasonsMeetsIt) {
    auto world = makeWorld(kSubdivision);
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
    auto world = makeWorld(kSubdivision);
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

    EXPECT_TRUE(findSuggestedLandingSites(fixture.world, grasslandProvisions(), {}).empty())
        << "the river past the latitude cut is the only fresh water";
}

TEST(LandingSiteResolution, DryPickStaysAtThePick) {
    auto world = makeWorld(kSubdivision);
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
}

TEST(LandingSiteResolution, PickBesideLakeResolvesToItsShore) {
    auto world = makeWorld(kSubdivision);
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
    EXPECT_NEAR(site.spawn.waterDistanceMeters, kLandingWaterDistanceMeters, kLandingWaterDistanceToleranceMeters);

    // The shore read independently, rings 0.5 m apart round the drop: the first water is the lake, a
    // sample step either side of the reported distance.
    const PlanetSampler sampler(world, site.origin.latDeg, site.origin.lonDeg);
    double              nearestLake = -1.0;
    for (double radius = 0.0; nearestLake < 0.0 && radius <= 40.0; radius += 0.5) {
        const int points = std::max(1, static_cast<int>(std::ceil(2.0 * 3.14159265358979323846 * radius / 0.5)));
        for (int i = 0; i < points && nearestLake < 0.0; ++i) {
            const double angle = 2.0 * 3.14159265358979323846 * i / points;
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

    bool river = false;
    bool lake = false;
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
        river = river || site.feature == FreshWaterFeature::River;
        lake = lake || site.feature == FreshWaterFeature::Lake;
        const auto biome = static_cast<Biome>(world->data.biome[site.tile]);
        grassland = grassland || biome == Biome::TemperateGrassland;
        forest = forest || biome == Biome::TemperateDeciduousForest;
    }
    EXPECT_TRUE(river);
    EXPECT_TRUE(lake);
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

TEST(LandingSiteFinder, SameWorldGivesTheSameSites) {
    const auto world = twoBiomeWorld();
    const std::vector<LandingSite> first = findSuggestedLandingSites(world, twoBiomeProvisions(), {});
    const std::vector<LandingSite> second = findSuggestedLandingSites(world, twoBiomeProvisions(), {});
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) expectSameLanding(first[i], second[i]);
}

TEST(LandingSiteFinder, DesertAndOceanWorldsGiveNoSites) {
    auto desert = twoBiomeWorld();
    for (TileId t = 0; t < desert->grid->tileCount(); ++t) {
        if ((desert->data.flags[t] & (kFlagOcean | kFlagLake)) == 0) {
            desert->data.biome[t] = static_cast<uint8_t>(Biome::HotDesert);
        }
    }
    BiomeProvisionTable desertProvisions = twoBiomeProvisions();
    desertProvisions[static_cast<std::size_t>(Biome::HotDesert)] = {300.0f, 0.0f};
    EXPECT_TRUE(findSuggestedLandingSites(desert, desertProvisions, {}).empty());

    auto ocean = makeWorld(kSubdivision);
    for (TileId t = 0; t < ocean->grid->tileCount(); ++t) {
        ocean->data.elevation[t] = -3000.0f;
        ocean->data.biome[t] = static_cast<uint8_t>(Biome::Ocean);
        ocean->data.flags[t] = kFlagOcean;
    }
    EXPECT_TRUE(findSuggestedLandingSites(ocean, twoBiomeProvisions(), {}).empty());
}

TEST(LandingSiteFinder, StopRequestEndsTheScanEarly) {
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

TEST(LandingSiteFinder, GeneratedPlanetSitesHaveFreshWaterInSight) {
    // NavigationSystem::kLandingSightMeters, the radius revealed at landing; worldgen can't see sim.
    constexpr double kLandingSightMeters = 150.0;
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
    for (std::size_t i = 0; i < sites.size(); ++i) {
        const LandingSite& site = sites[i];
        const double       dropX = site.spawn.xMeters;
        const double       dropY = site.spawn.yMeters;
        bool               freshWater = false;
        if (site.feature == FreshWaterFeature::River) {
            const RiverNetwork2D rivers(world, site.origin.latDeg, site.origin.lonDeg);
            std::vector<RiverNetwork2D::Segment> segments;
            rivers.gatherSegments(dropX - kLandingSightMeters, dropY - kLandingSightMeters, dropX + kLandingSightMeters,
                                  dropY + kLandingSightMeters, segments);
            for (const RiverNetwork2D::Segment& segment : segments) {
                for (double t = 0.0; t <= 1.0 && !freshWater; t += 0.05) {
                    const double x = segment.x0 + (segment.x1 - segment.x0) * t;
                    const double y = segment.y0 + (segment.y1 - segment.y0) * t;
                    freshWater = std::hypot(x - dropX, y - dropY) <= kLandingSightMeters;
                }
            }
        } else if (site.feature == FreshWaterFeature::Lake) {
            const PlanetSampler sampler(world, site.origin.latDeg, site.origin.lonDeg);
            for (double y = -kLandingSightMeters; y <= kLandingSightMeters && !freshWater; y += 3.0) {
                for (double x = -kLandingSightMeters; x <= kLandingSightMeters && !freshWater; x += 3.0) {
                    if (std::hypot(x, y) > kLandingSightMeters) continue;
                    const auto sample = sampler.sampleAt(dropX + x, dropY + y);
                    freshWater = sample.water && sample.weights[0].biome == Biome::Lake;
                }
            }
        }
        EXPECT_TRUE(freshWater) << "site " << i << " at " << site.origin.latDeg << ", " << site.origin.lonDeg;
    }
}

} // namespace worldgen
