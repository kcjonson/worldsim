// findWatersideSpawn: the drop is dry land kLandingWaterDistanceMeters from the nearest water of any kind.

#include "worldgen/sampling/SpawnSite.h"

#include "worldgen/sampling/PlanetSampler.h"
#include "worldgen/sampling/RiverNetwork2D.h"
#include "worldgen/sampling/RiverWorld.test.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

namespace worldgen {

namespace {

using test_support::carveRiver;
using test_support::dotProduct;
using test_support::GrasslandBlock;
using test_support::grasslandWorld;

constexpr double kSearchRadiusMeters = 200.0; // findWatersideSpawn's default
constexpr double kStepMeters = 3.0;           // its grid

std::shared_ptr<GeneratedWorld> makeWorld() {
    return grasslandWorld(GrasslandBlock{.landLatDeg = 25.0, .landLonDeg = 25.0, .seed = 0x7777ULL});
}

// A landing origin on a world: the lat/lon the search runs at.
struct Landing {
    std::shared_ptr<GeneratedWorld> world;
    double                          latDeg{};
    double                          lonDeg{};
};

// A river whose source tile's centre is the landing origin: the trunk and the source's two springs meet
// there, three channels a few meters apart.
Landing riverSource() {
    Landing      landing{makeWorld()};
    const TileId source = landing.world->grid->fromLatLon(0.0, 0.0);
    carveRiver(*landing.world, source, landing.world->grid->fromLatLon(0.0, 20.0));
    landing.world->grid->latLonOf(source, landing.latDeg, landing.lonDeg);
    return landing;
}

// A point on the east coast of the grassland: `inlandMeters` back from the edge between a land tile and its
// ocean neighbour, found by bisection on the grid's own lookup, and `alongCoastMeters` along that edge.
// With `riverToSea`, a river runs from the land tile into that ocean neighbour, across the edge there.
Landing eastCoast(double inlandMeters, double alongCoastMeters, bool riverToSea) {
    Landing           landing{makeWorld()};
    GeneratedWorld&   world = *landing.world;
    const SphereGrid& grid = *world.grid;
    // The easternmost land tile on the equator and its easternmost ocean neighbour.
    TileId land = kInvalidTile;
    double landLonDeg = -180.0;
    for (TileId tile = 0; tile < grid.tileCount(); ++tile) {
        double latDeg = 0.0;
        double lonDeg = 0.0;
        grid.latLonOf(tile, latDeg, lonDeg);
        if (std::abs(latDeg) < 2.0 && (world.data.flags[tile] & kFlagOcean) == 0 && lonDeg > landLonDeg) {
            land = tile;
            landLonDeg = lonDeg;
        }
    }
    std::array<TileId, 6> neighbours{};
    const uint32_t        count = grid.neighbors(land, neighbours);
    TileId                ocean = kInvalidTile;
    double                oceanLonDeg = -180.0;
    for (uint32_t i = 0; i < count; ++i) {
        double latDeg = 0.0;
        double lonDeg = 0.0;
        grid.latLonOf(neighbours[i], latDeg, lonDeg);
        if ((world.data.flags[neighbours[i]] & kFlagOcean) != 0 && lonDeg > oceanLonDeg) {
            ocean = neighbours[i];
            oceanLonDeg = lonDeg;
        }
    }
    if (riverToSea) carveRiver(world, land, ocean);

    const Vec3d from = grid.tileCenter(land);
    const Vec3d to = grid.tileCenter(ocean);
    const auto  between = [&](double share) {
        const Vec3d  mixed{from.x + (to.x - from.x) * share, from.y + (to.y - from.y) * share, from.z + (to.z - from.z) * share};
        const double length = std::sqrt(dotProduct(mixed, mixed));
        return Vec3d{mixed.x / length, mixed.y / length, mixed.z / length};
    };
    double inside = 0.0;
    double outside = 1.0;
    for (int halving = 0; halving < 60; ++halving) {
        const double middle = 0.5 * (inside + outside);
        if (grid.fromUnitVector(between(middle)) == land) {
            inside = middle;
        } else {
            outside = middle;
        }
    }
    const Vec3d  edge = between(inside);
    const Vec3d  inwardRaw{from.x - edge.x * dotProduct(edge, from), from.y - edge.y * dotProduct(edge, from),
                          from.z - edge.z * dotProduct(edge, from)};
    const double inwardLength = std::sqrt(dotProduct(inwardRaw, inwardRaw));
    const Vec3d  inward{inwardRaw.x / inwardLength, inwardRaw.y / inwardLength, inwardRaw.z / inwardLength};
    const Vec3d  along{edge.y * inward.z - edge.z * inward.y, edge.z * inward.x - edge.x * inward.z,
                      edge.x * inward.y - edge.y * inward.x};
    const double radius = world.derived.planetRadiusMeters;
    const Vec3d  moved{edge.x + (inward.x * inlandMeters + along.x * alongCoastMeters) / radius,
                      edge.y + (inward.y * inlandMeters + along.y * alongCoastMeters) / radius,
                      edge.z + (inward.z * inlandMeters + along.z * alongCoastMeters) / radius};
    SphereGrid::latLonFromUnitVector(moved, landing.latDeg, landing.lonDeg);
    return landing;
}

struct NearestWater {
    double radius{};
    bool   river{};
};

// The first ring round (x, y), rings 0.5 m apart sampled every 0.5 m of arc, that touches a river channel or
// biome water: an independent reading of the distance to the nearest water of any kind, and its kind.
NearestWater nearestWater(const Landing& landing, double x, double y, double limitMeters) {
    const RiverNetwork2D rivers(landing.world, landing.latDeg, landing.lonDeg);
    const PlanetSampler  sampler(landing.world, landing.latDeg, landing.lonDeg);
    for (double radius = 0.0; radius <= limitMeters; radius += 0.5) {
        const int points = std::max(1, static_cast<int>(std::ceil(2.0 * std::numbers::pi * radius / 0.5)));
        for (int i = 0; i < points; ++i) {
            const double angle = 2.0 * std::numbers::pi * i / points;
            const double pointX = x + radius * std::cos(angle);
            const double pointY = y + radius * std::sin(angle);
            if (rivers.sampleAt(pointX, pointY).isRiver) return {radius, true};
            if (sampler.sampleAt(pointX, pointY).water) return {radius, false};
        }
    }
    return {limitMeters, false};
}

} // namespace

// Three channels meet at the origin, so the dry ground nearest it is close to two of them at once; the drop
// is the nearest point 30 m clear of every one.
TEST(SpawnSite, DropIsThirtyMetersFromTheNearestChannelAndNearestTheOrigin) {
    const Landing   landing = riverSource();
    const SpawnSite site = findWatersideSpawn(landing.world, landing.latDeg, landing.lonDeg);
    EXPECT_EQ(site.water, SpawnWater::River);
    EXPECT_TRUE(site.clear);
    EXPECT_NEAR(site.waterDistanceMeters, kLandingWaterDistanceMeters, kLandingWaterDistanceToleranceMeters);

    const PlanetSampler sampler(landing.world, landing.latDeg, landing.lonDeg);
    EXPECT_FALSE(sampler.sampleAt(site.xMeters, site.yMeters).water) << "the drop is on land";
    const NearestWater nearest = nearestWater(landing, site.xMeters, site.yMeters, 40.0);
    EXPECT_TRUE(nearest.river);
    EXPECT_NEAR(nearest.radius, site.waterDistanceMeters, 1.0) << "the reported distance is to the nearest channel";

    // No point of the same grid nearer the origin is dry and in the band from the channels, measured here from
    // the segments the network gathers; this world has no lake or ocean in reach.
    const RiverNetwork2D rivers(landing.world, landing.latDeg, landing.lonDeg);
    std::vector<RiverNetwork2D::Segment> channels;
    const double extent = kSearchRadiusMeters + kLandingWaterDistanceMeters + kLandingWaterDistanceToleranceMeters;
    rivers.gatherSegments(-extent, -extent, extent, extent, channels);
    const double dropRadius = std::hypot(site.xMeters, site.yMeters);
    for (double y = -kSearchRadiusMeters; y <= kSearchRadiusMeters; y += kStepMeters) {
        for (double x = -kSearchRadiusMeters; x <= kSearchRadiusMeters; x += kStepMeters) {
            if (std::hypot(x, y) >= dropRadius || sampler.sampleAt(x, y).water) continue;
            double channelEdge = 1e9;
            for (const RiverNetwork2D::Segment& segment : channels) {
                const double spanX = segment.x1 - segment.x0;
                const double spanY = segment.y1 - segment.y0;
                const double lengthSquared = spanX * spanX + spanY * spanY;
                const double share =
                    lengthSquared > 0.0 ? std::clamp(((x - segment.x0) * spanX + (y - segment.y0) * spanY) / lengthSquared, 0.0, 1.0) : 0.0;
                const double halfWidth = segment.halfWidth0 + (segment.halfWidth1 - segment.halfWidth0) * share;
                channelEdge = std::min(channelEdge, std::hypot(x - (segment.x0 + spanX * share), y - (segment.y0 + spanY * share)) - halfWidth);
            }
            const bool inBand = channelEdge >= kLandingWaterDistanceMeters - kLandingWaterDistanceToleranceMeters &&
                                channelEdge <= kLandingWaterDistanceMeters + kLandingWaterDistanceToleranceMeters;
            EXPECT_FALSE(inBand) << "(" << x << ", " << y << ") is " << channelEdge << " m from a channel and nearer the origin";
        }
    }
}

TEST(SpawnSite, NoPointThirtyMetersFromWaterTakesTheFarthest) {
    const Landing    landing = riverSource();
    constexpr double kNarrowSearchMeters = 15.0; // every dry point in it is under 27 m from a channel
    const SpawnSite  site = findWatersideSpawn(landing.world, landing.latDeg, landing.lonDeg, kNarrowSearchMeters);
    ASSERT_EQ(site.water, SpawnWater::River);
    EXPECT_FALSE(site.clear);
    EXPECT_GT(site.waterDistanceMeters, 0.0f);
    EXPECT_LT(site.waterDistanceMeters, kLandingWaterDistanceMeters - kLandingWaterDistanceToleranceMeters);

    // The point farthest from channels that meet at the centre is on the search square's edge.
    EXPECT_DOUBLE_EQ(std::max(std::abs(site.xMeters), std::abs(site.yMeters)), kNarrowSearchMeters);
    EXPECT_NEAR(nearestWater(landing, site.xMeters, site.yMeters, 40.0).radius, site.waterDistanceMeters, 1.0);
}

TEST(SpawnSite, OceanShoreGivesAnOceanDropThirtyMetersBack) {
    const Landing   landing = eastCoast(20.0, 0.0, false);
    const SpawnSite site = findWatersideSpawn(landing.world, landing.latDeg, landing.lonDeg);
    EXPECT_EQ(site.water, SpawnWater::Ocean);
    EXPECT_TRUE(site.clear);
    EXPECT_NEAR(site.waterDistanceMeters, kLandingWaterDistanceMeters, kLandingWaterDistanceToleranceMeters);

    const NearestWater nearest = nearestWater(landing, site.xMeters, site.yMeters, 40.0);
    EXPECT_FALSE(nearest.river);
    EXPECT_NEAR(nearest.radius, site.waterDistanceMeters, kStepMeters) << "the shore read independently";
}

// The ocean is the water nearest the origin, a river a little farther along the coast: the drop still takes
// the river.
TEST(SpawnSite, RiverComesBeforeANearerOcean) {
    const Landing landing = eastCoast(20.0, 120.0, true);
    ASSERT_FALSE(nearestWater(landing, 0.0, 0.0, 60.0).river) << "the ocean is nearer the origin than the river";

    const SpawnSite site = findWatersideSpawn(landing.world, landing.latDeg, landing.lonDeg);
    EXPECT_EQ(site.water, SpawnWater::River);
    EXPECT_TRUE(site.clear);
    EXPECT_TRUE(nearestWater(landing, site.xMeters, site.yMeters, 40.0).river);
}

TEST(SpawnSite, DryWorldFallsBackToOrigin) {
    auto   world = makeWorld(); // no rivers carved
    double latDeg = 0.0;
    double lonDeg = 0.0;
    world->grid->latLonOf(world->grid->fromLatLon(0.0, 0.0), latDeg, lonDeg);

    const SpawnSite site = findWatersideSpawn(world, latDeg, lonDeg);
    EXPECT_EQ(site.water, SpawnWater::None);
    EXPECT_FALSE(site.clear);
    EXPECT_FLOAT_EQ(site.waterDistanceMeters, 0.0f);
    EXPECT_DOUBLE_EQ(site.xMeters, 0.0);
    EXPECT_DOUBLE_EQ(site.yMeters, 0.0);
}

} // namespace worldgen
