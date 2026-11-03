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
    const std::vector<TileId> seeds{grid.fromLatLon(20.0, 40.0), grid.fromLatLon(22.0, 45.0)};
    const std::atomic<bool>   cancel{false};
    constexpr float           kMaxTiles = 12.0f;

    const SeedDistanceResult result = seedDistance({
        .grid     = grid,
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
    std::vector<TileId> seeds;
    for (double lon = 0.0; lon <= 60.0; lon += 0.5) seeds.push_back(grid.fromLatLon(10.0 + lon * 0.2, lon));
    std::sort(seeds.begin(), seeds.end());
    seeds.erase(std::unique(seeds.begin(), seeds.end()), seeds.end());
    const std::atomic<bool> cancel{false};

    const SeedDistanceResult result = seedDistance({.grid = grid, .seeds = seeds, .cancel = cancel});

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
    // Measured 0.29 spacings at worst, on 68 of 40962 tiles.
    EXPECT_LT(maxExcess, 0.35);
    EXPECT_LT(inexact, grid.tileCount() / 200u);
}

// The second seed against brute force. Three rows of seeds (labels 1, 2, 3, packed with
// key 0) and a row of label 4 folded into two arms facing each other across a channel: a
// tile's second is its nearest seed of another label within the band and the reach, so
// the channel between the arms has none.
TEST(SeedDistance, SecondSeed) {
    const SphereGrid      grid(64);
    std::vector<TileId>   seeds;
    std::vector<uint32_t> labels;
    auto add = [&](double lat, double lon, uint32_t label) {
        const TileId t = grid.fromLatLon(lat, lon);
        if (std::find(seeds.begin(), seeds.end(), t) != seeds.end()) return;
        seeds.push_back(t);
        labels.push_back(label);
    };
    for (double lon = 0.0; lon <= 40.0; lon += 0.5) {
        add(10.0 + lon * 0.1, lon, 1u);
        add(25.0 + lon * 0.1, lon, 2u);
        add(17.5 + lon * 0.1, lon, 3u);
    }
    for (double t = 0.0; t <= 30.0; t += 0.5) {
        add(-20.0 + t, 100.0, 4u); // one arm
        add(-20.0 + t, 110.0, 4u); // the other, ~9 spacings across
    }
    const std::atomic<bool> cancel{false};
    constexpr float         kBand  = 4.0f;
    constexpr float         kReach = 12.0f;

    const SeedDistanceResult result = seedDistance({
        .grid             = grid,
        .seeds            = seeds,
        .cancel           = cancel,
        .labels           = &labels,
        .secondBandTiles  = kBand,
        .secondReachTiles = kReach,
    });

    double   maxExcess = 0.0;
    uint32_t inexact = 0u, withSecond = 0u, missed = 0u, inside = 0u;
    for (TileId t = 0; t < grid.tileCount(); ++t) {
        const uint32_t first = result.nearestSeed[t];
        ASSERT_NE(first, kNoSeed);
        double brute = std::numeric_limits<double>::max();
        for (size_t s = 0; s < seeds.size(); ++s) {
            if (labels[s] != labels[first]) brute = std::min(brute, straightTiles(grid, t, seeds[s]));
        }
        const double   gap    = brute - result.distTiles[t];
        const uint32_t second = result.secondSeed[t];
        const bool     inReach = result.distTiles[t] < kReach - 0.5f;
        if (gap < kBand - 0.5 && inReach) {
            ++inside;
            if (second == kNoSeed) ++missed;
        }
        if (second == kNoSeed) {
            ASSERT_TRUE(std::isinf(result.secondDistTiles[t])) << "tile " << t;
            continue;
        }
        ++withSecond;
        ASSERT_LT(result.distTiles[t], kReach + 1e-3) << "tile " << t;
        ASSERT_NE(labels[second], labels[first]) << "tile " << t;
        ASSERT_NEAR(result.secondDistTiles[t], straightTiles(grid, t, seeds[second]), 1e-4) << "tile " << t;
        ASSERT_LT(result.secondDistTiles[t] - result.distTiles[t], kBand + 1e-3) << "tile " << t;
        const double excess = result.secondDistTiles[t] - brute;
        ASSERT_GT(excess, -1e-4) << "tile " << t;
        maxExcess = std::max(maxExcess, excess);
        if (excess > 1e-3) ++inexact;
    }
    // Measured: none of 691 tiles inside the band missed, 3 of 811 seconds long by 0.15
    // spacings at worst.
    EXPECT_GT(inside, 300u);
    EXPECT_LT(maxExcess, 0.35);
    EXPECT_LT(inexact, withSecond / 20u);
    EXPECT_LT(missed, inside / 50u);
}

// Tile keys: a seed whose label holds neither of a tile's keys is never its second.
TEST(SeedDistance, SecondSeedHoldsTheTileKey) {
    const SphereGrid      grid(32);
    std::vector<TileId>   seeds{grid.fromLatLon(0.0, 0.0), grid.fromLatLon(0.0, 10.0), grid.fromLatLon(0.0, 20.0)};
    std::vector<uint32_t> labels{1u | (2u << 8), 1u | (3u << 8), 2u | (3u << 8)};
    std::vector<uint8_t>  keys(grid.tileCount(), 1u);
    const std::atomic<bool> cancel{false};

    const SeedDistanceResult result = seedDistance({
        .grid            = grid,
        .seeds           = seeds,
        .cancel          = cancel,
        .labels          = &labels,
        .tileKeys        = &keys,
        .secondBandTiles = 100.0f,
    });
    uint32_t withSecond = 0u;
    for (TileId t = 0; t < grid.tileCount(); ++t) {
        if (result.secondSeed[t] == kNoSeed) continue;
        ++withSecond;
        ASSERT_NE(result.secondSeed[t], 2u) << "tile " << t; // seed 2 holds keys 2 and 3 only
    }
    EXPECT_GT(withSecond, 0u);
}

// No seeds, labels or not, and a seed listed twice (it keeps its first index, queued once).
TEST(SeedDistance, NoSeedsAndDuplicates) {
    const SphereGrid          grid(16);
    const std::vector<TileId> none;
    const std::vector<uint32_t> noLabels;
    const std::atomic<bool>   cancel{false};

    const SeedDistanceResult empty = seedDistance({.grid = grid, .seeds = none, .cancel = cancel,
                                                   .labels = &noLabels, .secondBandTiles = 4.0f});
    for (TileId t = 0; t < grid.tileCount(); ++t) {
        ASSERT_TRUE(std::isinf(empty.distTiles[t]));
        ASSERT_EQ(empty.nearestSeed[t], kNoSeed);
        ASSERT_EQ(empty.secondSeed[t], kNoSeed);
    }

    const std::vector<TileId> twice{grid.fromLatLon(0.0, 0.0), grid.fromLatLon(30.0, 30.0), grid.fromLatLon(0.0, 0.0)};
    const SeedDistanceResult dup = seedDistance({.grid = grid, .seeds = twice, .cancel = cancel});
    for (TileId t = 0; t < grid.tileCount(); ++t) ASSERT_NE(dup.nearestSeed[t], 2u) << "tile " << t;
    EXPECT_EQ(dup.nearestSeed[twice[0]], 0u);
}

TEST(SeedDistance, NoSeedsAndCancel) {
    const SphereGrid          grid(16);
    const std::vector<TileId> none;
    std::atomic<bool>         cancel{false};

    const SeedDistanceResult capped = seedDistance({.grid = grid, .seeds = none, .cancel = cancel, .maxTiles = 5.0f});
    const SeedDistanceResult open   = seedDistance({.grid = grid, .seeds = none, .cancel = cancel});
    for (TileId t = 0; t < grid.tileCount(); ++t) {
        ASSERT_EQ(capped.distTiles[t], 5.0f);
        ASSERT_EQ(capped.nearestSeed[t], kNoSeed);
        ASSERT_TRUE(std::isinf(open.distTiles[t]));
    }

    cancel = true;
    const std::vector<TileId> one{0};
    EXPECT_THROW(seedDistance({.grid = grid, .seeds = one, .cancel = cancel}), CancelledException);
}

} // namespace worldgen
