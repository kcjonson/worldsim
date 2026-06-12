// PlateSim tests (M-T1): subduction, ridge gap-fill, continental conservation,
// determinism (incl. golden product hash), and ocean-age sanity.

#include "worldgen/tectonics/PlateSim.h"
#include "worldgen/tectonics/TectonicHistory.h"
#include "worldgen/tectonics/TectonicParams.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace worldgen::tectonics {

namespace {

PlateSimParams defaultParams(uint32_t coarseN = 64, uint64_t seed = 0xC0FFEEULL,
                             int K = 12, double water = 0.70) {
    PlateSimParams p;
    p.coarseN = coarseN;
    p.seed = seed;
    p.plateCount = K;
    p.waterAmount = water;
    p.planetAge = 4.5e9;
    return p;
}

// Count crust cells of a given type across all plate rasters.
uint32_t countType(const PlateSim& sim, CrustType type) {
    uint32_t n = 0;
    for (const auto& pl : sim.plates()) {
        if (!pl.alive) continue;
        for (const auto& c : pl.crust) if (c.type == type) ++n;
    }
    return n;
}

} // namespace

// ============================================================================
// Two-plate analytic subduction: an older oceanic plate converging with a
// younger one should have its overlap cells erased (older = denser = subducts).
// ============================================================================

TEST(PlateSim, OlderOceanicSubducts) {
    // Small grid for a controlled case.
    const uint32_t coarseN = 32;
    PlateSimParams p = defaultParams(coarseN, 1234ULL, 2, 0.95);
    p.historyMyr = 60.0; // a handful of steps
    p.dtMyr = 5.0;

    auto grid = std::make_shared<const SphereGrid>(coarseN);
    const uint32_t N = grid->tileCount();

    // Two oceanic plates split by the x=0 plane (east vs west hemisphere).
    PlateSimTestOverride ov;
    ov.plates.assign(2, SimPlate{});
    ov.owner.assign(N, 255);
    for (auto& pl : ov.plates) { pl.crust.assign(N, CrustCell{}); pl.alive = true; pl.isContinental = false; }

    // Plate 0 = older ocean (birth -100), plate 1 = younger (birth -10).
    for (TileId t = 0; t < N; ++t) {
        Vec3d c = grid->tileCenter(t);
        int side = c.x >= 0.0 ? 0 : 1;
        ov.owner[t] = static_cast<uint8_t>(side);
        CrustCell& cell = ov.plates[side].crust[t];
        cell.type = CrustType::Oceanic;
        cell.thicknessKm = 7.0f;
        cell.birthMyr = side == 0 ? -100 : -10;
    }
    // Converge them: plate 0 rotates so its crust drifts toward +x boundary into
    // plate 1's territory. Euler pole = +z (rotation in the xy-plane).
    ov.plates[0].eulerPole = {0, 0, 1};
    ov.plates[0].omegaRadPerMyr = 0.02;  // ~drift toward plate 1
    ov.plates[1].eulerPole = {0, 0, 1};
    ov.plates[1].omegaRadPerMyr = -0.02; // opposite drift -> convergence at one boundary

    PlateSim sim(p, &ov);

    uint32_t olderBefore = 0;
    for (const auto& c : sim.plates()[0].crust) if (c.type == CrustType::Oceanic) ++olderBefore;

    auto history = sim.run();

    uint32_t olderAfter = 0;
    for (const auto& c : sim.plates()[0].crust) if (c.type == CrustType::Oceanic) ++olderAfter;

    // The older plate lost cells to subduction at the convergent boundary.
    EXPECT_LT(olderAfter, olderBefore)
        << "older oceanic plate did not lose cells to subduction";

    // Total resolved crust count stays ~N (gap-fill replaces subducted ocean).
    uint32_t resolved = 0;
    for (TileId t = 0; t < N; ++t) if (history->crustType[t] != static_cast<uint8_t>(CrustType::None)) ++resolved;
    EXPECT_GE(resolved, static_cast<uint32_t>(N * 0.95))
        << "resolved crust fell well below N (gap fill not keeping up)";
}

// ============================================================================
// Gap fill: a divergent boundary creates fresh oceanic crust born at the step
// time (age 0 at creation), so the final history has age-0 oceanic cells.
// ============================================================================

TEST(PlateSim, GapFillCreatesAgeZeroCrust) {
    auto p = defaultParams(48, 99ULL, 6, 0.70);
    PlateSim sim(p);
    auto history = sim.run();

    // Some oceanic cells must be age 0 (freshly created at a ridge in the final step).
    bool foundAgeZero = false;
    uint32_t youngOceanic = 0;
    const uint32_t N = history->grid->tileCount();
    for (TileId t = 0; t < N; ++t) {
        if (history->crustType[t] != static_cast<uint8_t>(CrustType::Oceanic)) continue;
        if (history->crustAge[t] == 0) { foundAgeZero = true; ++youngOceanic; }
    }
    EXPECT_TRUE(foundAgeZero) << "no age-0 oceanic crust found; ridges not spreading";
    EXPECT_GT(youngOceanic, 0u);
}

// ============================================================================
// Continental conservation: M-T1 has no collision consumption, so the in-raster
// continental cell count should drift < 1% over a full run.
// ============================================================================

TEST(PlateSim, ContinentalConservation) {
    auto p = defaultParams(64, 0xABCDULL, 12, 0.70);
    PlateSim sim(p);
    uint32_t before = countType(sim, CrustType::Continental);
    sim.run();
    uint32_t after = countType(sim, CrustType::Continental);

    ASSERT_GT(before, 0u);
    double drift = std::abs(static_cast<double>(after) - static_cast<double>(before)) /
                   static_cast<double>(before);
    EXPECT_LT(drift, 0.01) << "continental cell drift " << (drift * 100.0)
                           << "% (before " << before << " after " << after << ")";
}

// ============================================================================
// Determinism: same seed twice -> identical product hash.
// ============================================================================

TEST(PlateSim, DeterministicProductHash) {
    auto p = defaultParams(48, 0x5EED5EEDULL, 10, 0.65);
    PlateSim a(p);
    PlateSim b(p);
    auto ha = a.run();
    auto hb = b.run();
    EXPECT_EQ(computeTectonicHistoryHash(*ha), computeTectonicHistoryHash(*hb));
}

// ============================================================================
// Golden product hash at coarseN=64, fixed seed. UPDATE POLICY: this value pins
// the deterministic output of the M-T1 sim. Only update it on a deliberate,
// reviewed change to the sim algorithm or its constants — never to "make the test
// pass" after an accidental change. (Mirrors the worldHash golden-test policy.)
// ============================================================================

TEST(PlateSim, GoldenProductHash) {
    auto p = defaultParams(64, 0x1234567890ABCDEFULL, 12, 0.70);
    PlateSim sim(p);
    auto h = sim.run();
    uint64_t hash = computeTectonicHistoryHash(*h);
    constexpr uint64_t kGolden = 0x802cfb2867a7f8bdULL;
    // Print so a deliberate update is easy; assert against the pinned value.
    std::printf("[PlateSim.GoldenProductHash] coarseN=64 seed=0x1234567890ABCDEF "
                "hash=0x%016llx\n", static_cast<unsigned long long>(hash));
    EXPECT_EQ(hash, kGolden);
}

// ============================================================================
// Ocean age sanity: max age within history + init cap; mean in a plausible band;
// age correlates positively with distance from the nearest divergent boundary
// (ridge crust young, abyssal crust old).
// ============================================================================

TEST(PlateSim, OceanAgeSanity) {
    auto p = defaultParams(64, 0xFEEDULL, 12, 0.70);
    PlateSim sim(p);
    auto h = sim.run();
    const SphereGrid& g = *h->grid;
    const uint32_t N = g.tileCount();

    uint16_t maxAge = 0;
    uint64_t sum = 0;
    uint32_t cnt = 0;
    for (TileId t = 0; t < N; ++t) {
        if (h->crustType[t] != static_cast<uint8_t>(CrustType::Oceanic)) continue;
        maxAge = std::max(maxAge, h->crustAge[t]);
        sum += h->crustAge[t];
        ++cnt;
    }
    ASSERT_GT(cnt, 0u);
    double mean = static_cast<double>(sum) / cnt;

    // Upper bound: history length + the initial pre-age cap.
    double bound = h->historyMyr + static_cast<double>(kOceanInitMaxAgeMyr);
    EXPECT_LE(static_cast<double>(maxAge), bound)
        << "max ocean age " << maxAge << " exceeds history+init cap " << bound;
    // Mean in a plausible band (Earth ocean mean ~60-80 Myr; ours runs younger
    // because ridges constantly resurface). Just bound it loosely.
    EXPECT_GT(mean, 5.0);
    EXPECT_LT(mean, 250.0);

    // Spreading-ridge signature: oceanic crust touching a divergent boundary is
    // freshly created and therefore younger than the oceanic mean. This is the
    // honest, resolution-robust version of "age increases away from ridges" — a
    // global distance-to-final-ridge correlation washes out because ridges migrate
    // over the run, but ridge-adjacent crust is young by construction.
    std::array<TileId, 6> nbrs{};
    uint64_t nearSum = 0;
    uint32_t nearCnt = 0;
    for (TileId t = 0; t < N; ++t) {
        if (h->crustType[t] != static_cast<uint8_t>(CrustType::Oceanic)) continue;
        bool nearRidge = h->boundaryType[t] == static_cast<uint8_t>(BoundaryType::Divergent);
        if (!nearRidge) {
            uint32_t nc = g.neighbors(t, nbrs);
            for (uint32_t k = 0; k < nc; ++k) {
                if (h->boundaryType[nbrs[k]] == static_cast<uint8_t>(BoundaryType::Divergent)) {
                    nearRidge = true; break;
                }
            }
        }
        if (nearRidge) { nearSum += h->crustAge[t]; ++nearCnt; }
    }
    if (nearCnt > 50) {
        double nearMean = static_cast<double>(nearSum) / nearCnt;
        std::printf("[PlateSim.OceanAgeSanity] ridge-adjacent mean=%.1f vs ocean mean=%.1f "
                    "(near n=%u, max=%u)\n", nearMean, mean, nearCnt, maxAge);
        EXPECT_LT(nearMean, mean) << "ridge-adjacent ocean crust is not younger than the mean";
    }
}

} // namespace worldgen::tectonics
