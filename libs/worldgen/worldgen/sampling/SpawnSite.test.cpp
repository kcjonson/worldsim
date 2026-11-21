// findWatersideSpawn: the drop is dry land kLandingWaterDistanceMeters from the nearest water of any kind.

#include "worldgen/sampling/SpawnSite.h"

#include "worldgen/sampling/PlanetSampler.h"
#include "worldgen/sampling/RiverNetwork2D.h"
#include "worldgen/sampling/RiverWorld.test.h"
#include "worldgen/sampling/SphericalProjection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>

namespace worldgen {

namespace {

using test_support::addLake;
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

// The east coast of the grassland on the equator: its easternmost land tile, that tile's easternmost ocean
// neighbour, and the edge between them as the grid's own lookup draws it.
struct Coast {
    std::shared_ptr<GeneratedWorld> world;
    TileId                          land{kInvalidTile};
    TileId                          across{kInvalidTile};
    Vec3d                           edge{};   // on the edge, on the line between the two centres
    Vec3d                           inward{}; // unit, tangent, toward the land tile's centre
    Vec3d                           along{};  // unit, tangent, along the edge

    // The point inwardMeters toward the land tile and alongMeters along the edge from `edge`, on the sphere.
    [[nodiscard]] Vec3d offset(double inwardMeters, double alongMeters) const {
        const double radius = world->derived.planetRadiusMeters;
        const Vec3d  moved{edge.x + (inward.x * inwardMeters + along.x * alongMeters) / radius,
                          edge.y + (inward.y * inwardMeters + along.y * alongMeters) / radius,
                          edge.z + (inward.z * inwardMeters + along.z * alongMeters) / radius};
        const double length = std::sqrt(dotProduct(moved, moved));
        return Vec3d{moved.x / length, moved.y / length, moved.z / length};
    }

    // Where the land tile's boundary lies alongMeters along, as meters inward of the straight line through
    // `edge`: the edge bends away from that line, so it is found by bisection across it, to a centimetre.
    [[nodiscard]] double boundaryAt(double alongMeters) const {
        constexpr double kAcrossMeters = 20000.0; // past how far the edge strays from that line on this grid
        double           outside = -kAcrossMeters;
        double           inside = kAcrossMeters;
        while (inside - outside > 0.01) {
            const double middle = 0.5 * (inside + outside);
            if (world->grid->fromUnitVector(offset(middle, alongMeters)) == land) {
                inside = middle;
            } else {
                outside = middle;
            }
        }
        return inside;
    }

    // The tile just outside the land tile's boundary alongMeters along: `across` while on this edge.
    [[nodiscard]] TileId outsideAt(double alongMeters) const {
        return world->grid->fromUnitVector(offset(boundaryAt(alongMeters) - 1.0, alongMeters));
    }

    // A landing origin inlandMeters back from the edge where it lies alongMeters along.
    [[nodiscard]] Landing at(double inlandMeters, double alongMeters) const {
        Landing landing{world};
        SphereGrid::latLonFromUnitVector(offset(boundaryAt(alongMeters) + inlandMeters, alongMeters), landing.latDeg,
                                         landing.lonDeg);
        return landing;
    }

    // How far along the edge it ends, to a metre by bisection, and the third tile at that corner: of the two
    // tiles bordering both, the one whose centre is nearer the corner.
    [[nodiscard]] std::pair<double, TileId> corner() const {
        constexpr double kFarPastAnyCornerMeters = 500000.0;
        double           onEdge = 0.0;
        double           pastEnd = kFarPastAnyCornerMeters;
        while (pastEnd - onEdge > 1.0) {
            const double middle = 0.5 * (onEdge + pastEnd);
            if (outsideAt(middle) == across) {
                onEdge = middle;
            } else {
                pastEnd = middle;
            }
        }
        const SphereGrid&     grid = *world->grid;
        const Vec3d           cornerPoint = offset(boundaryAt(onEdge), onEdge);
        std::array<TileId, 6> landNeighbours{};
        std::array<TileId, 6> acrossNeighbours{};
        const uint32_t        landCount = grid.neighbors(land, landNeighbours);
        const uint32_t        acrossCount = grid.neighbors(across, acrossNeighbours);
        TileId                third = kInvalidTile;
        for (uint32_t i = 0; i < landCount; ++i) {
            const TileId candidate = landNeighbours[i];
            const bool   bordersAcross = std::find(acrossNeighbours.begin(), acrossNeighbours.begin() + acrossCount, candidate) !=
                                       acrossNeighbours.begin() + acrossCount;
            if (bordersAcross &&
                (third == kInvalidTile || dotProduct(grid.tileCenter(candidate), cornerPoint) > dotProduct(grid.tileCenter(third), cornerPoint))) {
                third = candidate;
            }
        }
        return {onEdge, third};
    }
};

Coast eastCoast() {
    Coast             coast{makeWorld()};
    GeneratedWorld&   world = *coast.world;
    const SphereGrid& grid = *world.grid;
    double            landLonDeg = -180.0;
    for (TileId tile = 0; tile < grid.tileCount(); ++tile) {
        double latDeg = 0.0;
        double lonDeg = 0.0;
        grid.latLonOf(tile, latDeg, lonDeg);
        if (std::abs(latDeg) < 2.0 && (world.data.flags[tile] & kFlagOcean) == 0 && lonDeg > landLonDeg) {
            coast.land = tile;
            landLonDeg = lonDeg;
        }
    }
    std::array<TileId, 6> neighbours{};
    const uint32_t        count = grid.neighbors(coast.land, neighbours);
    double                oceanLonDeg = -180.0;
    for (uint32_t i = 0; i < count; ++i) {
        double latDeg = 0.0;
        double lonDeg = 0.0;
        grid.latLonOf(neighbours[i], latDeg, lonDeg);
        if ((world.data.flags[neighbours[i]] & kFlagOcean) != 0 && lonDeg > oceanLonDeg) {
            coast.across = neighbours[i];
            oceanLonDeg = lonDeg;
        }
    }

    const Vec3d from = grid.tileCenter(coast.land);
    const Vec3d to = grid.tileCenter(coast.across);
    const auto  between = [&](double share) {
        const Vec3d  mixed{from.x + (to.x - from.x) * share, from.y + (to.y - from.y) * share, from.z + (to.z - from.z) * share};
        const double length = std::sqrt(dotProduct(mixed, mixed));
        return Vec3d{mixed.x / length, mixed.y / length, mixed.z / length};
    };
    double inside = 0.0;
    double outside = 1.0;
    for (int halving = 0; halving < 60; ++halving) {
        const double middle = 0.5 * (inside + outside);
        if (grid.fromUnitVector(between(middle)) == coast.land) {
            inside = middle;
        } else {
            outside = middle;
        }
    }
    const Vec3d edge = between(inside);
    const Vec3d inwardRaw{from.x - edge.x * dotProduct(edge, from), from.y - edge.y * dotProduct(edge, from),
                          from.z - edge.z * dotProduct(edge, from)};
    const double inwardLength = std::sqrt(dotProduct(inwardRaw, inwardRaw));
    const Vec3d  inward{inwardRaw.x / inwardLength, inwardRaw.y / inwardLength, inwardRaw.z / inwardLength};
    coast.edge = edge;
    coast.inward = inward;
    coast.along = Vec3d{edge.y * inward.z - edge.z * inward.y, edge.z * inward.x - edge.x * inward.z, edge.x * inward.y - edge.y * inward.x};
    return coast;
}

// A tile, land or ocean, turned into a lake above sea level.
void makeLake(GeneratedWorld& world, TileId tile) {
    world.data.flags[tile] = 0;
    addLake(world, tile);
    world.data.elevation[tile] = 500.0F;
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
    const Landing   landing = eastCoast().at(20.0, 0.0);
    const SpawnSite site = findWatersideSpawn(landing.world, landing.latDeg, landing.lonDeg);
    EXPECT_EQ(site.water, SpawnWater::Ocean);
    EXPECT_TRUE(site.clear);
    EXPECT_NEAR(site.waterDistanceMeters, kLandingWaterDistanceMeters, kLandingWaterDistanceToleranceMeters);

    const NearestWater nearest = nearestWater(landing, site.xMeters, site.yMeters, 40.0);
    EXPECT_FALSE(nearest.river);
    EXPECT_NEAR(nearest.radius, site.waterDistanceMeters, kStepMeters) << "the shore read independently";
}

// The origin is 20 m in from the shore at the coast's tile across; the point 30 m in, straight inland of it,
// is a clear drop beside that shore. Checked independently: dry, its nearest water that shore 30 m off, and
// nearer the origin than the drop the search took.
void expectClearShorePointNearerThan(const Coast& coast, const Landing& landing, const SpawnSite& drop, double alongMeters) {
    const SphericalProjection projection(landing.world->derived.planetRadiusMeters, landing.latDeg, landing.lonDeg);
    const Landing             point = coast.at(kLandingWaterDistanceMeters, alongMeters);
    const WorldPos2d          position = projection.latLonToWorld({point.latDeg, point.lonDeg});

    const PlanetSampler sampler(landing.world, landing.latDeg, landing.lonDeg);
    EXPECT_FALSE(sampler.sampleAt(position.x, position.y).water);
    const NearestWater nearest = nearestWater(landing, position.x, position.y, 40.0);
    EXPECT_FALSE(nearest.river);
    EXPECT_NEAR(nearest.radius, kLandingWaterDistanceMeters, kLandingWaterDistanceToleranceMeters);
    EXPECT_LT(std::hypot(position.x, position.y), std::hypot(drop.xMeters, drop.yMeters))
        << "the shore's clear point is nearer the origin than the drop";
}

// A clear ocean drop lies nearer the origin than the river's, which the drop still takes.
TEST(SpawnSite, RiverComesBeforeANearerOcean) {
    const Coast coast = eastCoast();
    carveRiver(*coast.world, coast.land, coast.across);
    const Landing   landing = coast.at(20.0, 120.0);
    const SpawnSite site = findWatersideSpawn(landing.world, landing.latDeg, landing.lonDeg);
    expectClearShorePointNearerThan(coast, landing, site, 120.0);

    EXPECT_EQ(site.water, SpawnWater::River);
    EXPECT_TRUE(site.clear);
    EXPECT_TRUE(nearestWater(landing, site.xMeters, site.yMeters, 40.0).river);
}

// The same with a lake across the shore: a clear lake drop lies nearer, and the drop takes the river.
TEST(SpawnSite, RiverComesBeforeANearerLake) {
    const Coast coast = eastCoast();
    makeLake(*coast.world, coast.across);
    carveRiver(*coast.world, coast.land, coast.across);
    const Landing   landing = coast.at(20.0, 120.0);
    const SpawnSite site = findWatersideSpawn(landing.world, landing.latDeg, landing.lonDeg);
    expectClearShorePointNearerThan(coast, landing, site, 120.0);

    EXPECT_EQ(site.water, SpawnWater::River);
    EXPECT_TRUE(site.clear);
    EXPECT_TRUE(nearestWater(landing, site.xMeters, site.yMeters, 40.0).river);
}

// The ocean 20 m off, and the tile at the end of the coast's edge, 100 m along it, made a lake: a clear ocean
// drop lies nearer, and the drop takes the lake.
TEST(SpawnSite, LakeComesBeforeANearerOcean) {
    const Coast coast = eastCoast();
    const auto [cornerMeters, beyond] = coast.corner();
    makeLake(*coast.world, beyond);
    const double    alongMeters = cornerMeters - 100.0;
    const Landing   landing = coast.at(20.0, alongMeters);
    const SpawnSite site = findWatersideSpawn(landing.world, landing.latDeg, landing.lonDeg);
    expectClearShorePointNearerThan(coast, landing, site, alongMeters);

    EXPECT_EQ(site.water, SpawnWater::Lake);
    EXPECT_TRUE(site.clear);
    const PlanetSampler sampler(landing.world, landing.latDeg, landing.lonDeg);
    EXPECT_FALSE(sampler.sampleAt(site.xMeters, site.yMeters).water);
    EXPECT_NEAR(site.waterDistanceMeters, kLandingWaterDistanceMeters, kLandingWaterDistanceToleranceMeters);
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
