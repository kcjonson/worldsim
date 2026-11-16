// ClimateField tests — the shared distance-to-ocean BFS used by AtmosphereStage
// (continentality) and PrecipitationStage (sweep order).

#include "worldgen/stages/ClimateField.h"

#include "worldgen/grid/SphereGrid.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

namespace worldgen {

namespace {

constexpr uint32_t kN = 24;

} // namespace

TEST(ClimateField, OceanTilesAreZeroDistance) {
    SphereGrid grid(kN);
    const uint32_t N = grid.tileCount();
    // Western hemisphere ocean, eastern hemisphere land.
    std::vector<float> elev(N, 0.0f);
    for (TileId t = 0; t < N; ++t) {
        double lat{}, lon{};
        grid.latLonOf(t, lat, lon);
        elev[t] = lon < 0.0 ? -1000.0f : 500.0f;
    }
    const std::vector<float> dist = computeDistanceToOcean(grid, elev, 0.0f);

    ASSERT_EQ(dist.size(), N);
    for (TileId t = 0; t < N; ++t) {
        if (elev[t] < 0.0f) {
            EXPECT_EQ(dist[t], 0.0f) << "ocean tile " << t << " must be distance 0";
        } else {
            EXPECT_GT(dist[t], 0.0f) << "land tile " << t << " must be > 0";
        }
    }
}

TEST(ClimateField, MonotonicNeighborStep) {
    // BFS property: every land tile is at most one hop more than its nearest
    // neighbor, and at least one neighbor is exactly one hop closer (it lies on a
    // shortest path back to the sea).
    SphereGrid grid(kN);
    const uint32_t N = grid.tileCount();
    std::vector<float> elev(N, 500.0f);
    for (TileId t = 0; t < N; ++t) {
        double lat{}, lon{};
        grid.latLonOf(t, lat, lon);
        if (lon < -90.0) elev[t] = -1000.0f; // a single ocean basin
    }
    const std::vector<float> dist = computeDistanceToOcean(grid, elev, 0.0f);

    std::array<TileId, 6> nbs{};
    for (TileId t = 0; t < N; ++t) {
        if (dist[t] <= 0.0f) continue;
        const uint32_t cnt = grid.neighbors(t, nbs);
        float minNb = 1e30f;
        for (uint32_t k = 0; k < cnt; ++k) minNb = std::min(minNb, dist[nbs[k]]);
        EXPECT_NEAR(dist[t], minNb + 1.0f, 1e-4f)
            << "tile " << t << " dist " << dist[t] << " minNb " << minNb;
    }
}

TEST(ClimateField, AllLandDegenerateIsZero) {
    // No ocean -> every distance is 0 (a no-op for the continentality terms).
    SphereGrid grid(kN);
    const uint32_t N = grid.tileCount();
    std::vector<float> elev(N, 800.0f);
    const std::vector<float> dist = computeDistanceToOcean(grid, elev, 0.0f);
    for (TileId t = 0; t < N; ++t) EXPECT_EQ(dist[t], 0.0f);
}

TEST(ClimateField, DeterministicRegardlessOfThreads) {
    // The BFS is serial and seed-ordered, so it is trivially thread-independent;
    // this guards against an accidental reordering by re-running and comparing.
    SphereGrid grid(kN);
    const uint32_t N = grid.tileCount();
    std::vector<float> elev(N, 300.0f);
    for (TileId t = 0; t < N; ++t) {
        double lat{}, lon{};
        grid.latLonOf(t, lat, lon);
        if (lat < -20.0) elev[t] = -500.0f;
    }
    const std::vector<float> a = computeDistanceToOcean(grid, elev, 0.0f);
    const std::vector<float> b = computeDistanceToOcean(grid, elev, 0.0f);
    EXPECT_EQ(a, b);
}

} // namespace worldgen
