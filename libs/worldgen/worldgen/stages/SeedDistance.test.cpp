#include "worldgen/stages/SeedDistance.h"

#include "worldgen/pipeline/GenerationStage.h" // CancelledException

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <vector>

namespace worldgen {

namespace {

double spacingOf(const SphereGrid& grid) {
    const double N = static_cast<double>(grid.tileCount());
    return std::sqrt(8.0 * 3.14159265358979323846 / (std::sqrt(3.0) * N));
}

std::vector<TileCenterF> centersOf(const SphereGrid& grid) {
    foundation::TaskPool pool(2);
    return tileCentersF(grid, pool);
}

double straightTiles(const SphereGrid& grid, TileId a, TileId b) {
    const Vec3d  ca = grid.tileCenter(a);
    const Vec3d  cb = grid.tileCenter(b);
    const double dx = ca.x - cb.x, dy = ca.y - cb.y, dz = ca.z - cb.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz) / spacingOf(grid);
}

} // namespace

// Two seeds, capped: the field is the straight-line distance to the nearer seed (round
// contours), each tile's owner is that seed, and past the cap a tile reads the cap with
// no owner.
TEST(SeedDistance, StraightLineDistanceAndNearestSeed) {
    const SphereGrid          grid(64);
    const std::vector<TileCenterF> centers = centersOf(grid);
    const std::vector<TileId> seeds{grid.fromLatLon(20.0, 40.0), grid.fromLatLon(22.0, 45.0)};
    const std::atomic<bool>   cancel{false};
    constexpr float           kMaxTiles = 12.0f;

    const SeedDistanceResult result = seedDistance({
        .grid     = grid,
        .centers  = centers,
        .seeds    = seeds,
        .cancel   = cancel,
        .maxTiles = kMaxTiles,
    });

    for (TileId t = 0; t < grid.tileCount(); ++t) {
        const double d0 = straightTiles(grid, t, seeds[0]);
        const double d1 = straightTiles(grid, t, seeds[1]);
        const double nearest = std::min(d0, d1);
        ASSERT_NEAR(result.distTiles[t], static_cast<float>(std::min(nearest, static_cast<double>(kMaxTiles))), 1e-4f)
            << "tile " << t;
        if (nearest >= kMaxTiles) {
            ASSERT_EQ(result.nearestSeed[t], kNoSeed) << "tile " << t;
        } else if (std::abs(d0 - d1) > 1e-3) {
            ASSERT_EQ(result.nearestSeed[t], d0 < d1 ? 0u : 1u) << "tile " << t;
        }
    }
}

// The way TerrainStage uses it: a dense row of seeds (a boundary), no cap. Every tile
// gets an owner and its distance is to that owner. Where two seeds' waves meet, a tile can
// keep the seed whose wave reached its neighbours first rather than the nearest one, so
// the distance runs a fraction of a spacing long on a few tiles.
TEST(SeedDistance, DenseRowUncapped) {
    const SphereGrid    grid(64);
    const std::vector<TileCenterF> centers = centersOf(grid);
    std::vector<TileId> seeds;
    for (double lon = 0.0; lon <= 60.0; lon += 0.5) seeds.push_back(grid.fromLatLon(10.0 + lon * 0.2, lon));
    std::sort(seeds.begin(), seeds.end());
    seeds.erase(std::unique(seeds.begin(), seeds.end()), seeds.end());
    const std::atomic<bool> cancel{false};

    const SeedDistanceResult result = seedDistance({.grid = grid, .centers = centers, .seeds = seeds, .cancel = cancel});

    double   maxExcess = 0.0;
    uint32_t inexact   = 0u;
    for (TileId t = 0; t < grid.tileCount(); ++t) {
        ASSERT_NE(result.nearestSeed[t], kNoSeed) << "tile " << t;
        double brute = std::numeric_limits<double>::max();
        for (const TileId s : seeds) brute = std::min(brute, straightTiles(grid, t, s));
        ASSERT_NEAR(result.distTiles[t], straightTiles(grid, t, seeds[result.nearestSeed[t]]), 1e-4) << "tile " << t;
        const double excess = result.distTiles[t] - brute;
        ASSERT_GT(excess, -1e-4) << "tile " << t;
        maxExcess = std::max(maxExcess, excess);
        if (excess > 1e-3) ++inexact;
    }
    // Measured 0.16 spacings at worst, on 67 of 40962 tiles.
    EXPECT_LT(maxExcess, 0.35);
    EXPECT_LT(inexact, grid.tileCount() / 200u);
}

// A seed listed twice keeps its first index (queued once).
TEST(SeedDistance, Duplicates) {
    const SphereGrid          grid(16);
    const std::vector<TileCenterF> centers = centersOf(grid);
    const std::atomic<bool>   cancel{false};

    const std::vector<TileId> twice{grid.fromLatLon(0.0, 0.0), grid.fromLatLon(30.0, 30.0), grid.fromLatLon(0.0, 0.0)};
    const SeedDistanceResult dup = seedDistance({.grid = grid, .centers = centers, .seeds = twice, .cancel = cancel});
    for (TileId t = 0; t < grid.tileCount(); ++t) ASSERT_NE(dup.nearestSeed[t], 2u) << "tile " << t;
    EXPECT_EQ(dup.nearestSeed[twice[0]], 0u);
}

TEST(SeedDistance, NoSeedsAndCancel) {
    const SphereGrid          grid(16);
    const std::vector<TileCenterF> centers = centersOf(grid);
    const std::vector<TileId> none;
    std::atomic<bool>         cancel{false};

    const SeedDistanceResult capped = seedDistance({.grid = grid, .centers = centers, .seeds = none, .cancel = cancel, .maxTiles = 5.0f});
    const SeedDistanceResult open   = seedDistance({.grid = grid, .centers = centers, .seeds = none, .cancel = cancel});
    for (TileId t = 0; t < grid.tileCount(); ++t) {
        ASSERT_EQ(capped.distTiles[t], 5.0f);
        ASSERT_EQ(capped.nearestSeed[t], kNoSeed);
        ASSERT_TRUE(std::isinf(open.distTiles[t]));
    }

    cancel = true;
    const std::vector<TileId> one{0};
    EXPECT_THROW(seedDistance({.grid = grid, .centers = centers, .seeds = one, .cancel = cancel}), CancelledException);
}

} // namespace worldgen
