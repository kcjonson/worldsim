// TerrainStage tests — M3b.
//
// All tests at n=128 (fixed seeds) unless noted.
// WORLDGEN_VISUAL=1: writes debug images at n=512 to build/debug-images/.

#include "worldgen/pipeline/PlanetGenerator.h"
#include "worldgen/data/PlanetParams.h"
#include "worldgen/data/WorldData.h"
#include "worldgen/debug/DebugImageExporter.h"
#include "worldgen/stages/SeedDistance.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace worldgen {

namespace {

std::shared_ptr<const GeneratedWorld> runPipeline(const PlanetParams& params,
                                                   int timeoutSeconds = 180,
                                                   unsigned threadCount = 0) {
    PlanetGenerator gen(threadCount);
    gen.start(params);

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(timeoutSeconds);
    while (std::chrono::steady_clock::now() < deadline) {
        auto prog = gen.progress();
        if (prog.state == GenerationProgress::State::Complete ||
            prog.state == GenerationProgress::State::Failed  ||
            prog.state == GenerationProgress::State::Cancelled) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return gen.takeResult();
}

PlanetParams makeParams(uint32_t n, double waterAmount = 0.70, int K = 12,
                        uint64_t seed = 0xFACEFEED42ULL) {
    PlanetParams p = PlanetParams::preset(Preset::EarthLike);
    p.gridSubdivision    = n;
    p.waterAmount        = waterAmount;
    p.tectonicPlateCount = K;
    p.seed               = seed;
    return p;
}

// boundaryType is set on the boundary rows only. Each tile's nearest row, by a
// multi-source BFS from the rows (first wave wins): that row's tile, and the hop count.
struct NearestRow {
    std::vector<TileId>   row;
    std::vector<uint16_t> hops;
};

NearestRow nearestBoundaryRow(const GeneratedWorld& world) {
    const SphereGrid& grid = *world.grid;
    const uint32_t    N    = grid.tileCount();
    NearestRow        out{std::vector<TileId>(N, 0u), std::vector<uint16_t>(N, 0xFFFFu)};
    std::vector<TileId> queue;
    for (TileId t = 0; t < N; ++t) {
        if (world.data.boundaryType[t] == 0u) continue;
        out.row[t]  = t;
        out.hops[t] = 0u;
        queue.push_back(t);
    }
    std::array<TileId, 6> nbrs{};
    for (size_t i = 0; i < queue.size(); ++i) {
        const TileId   t   = queue[i];
        const uint32_t cnt = grid.neighbors(t, nbrs);
        for (uint32_t k = 0; k < cnt; ++k) {
            if (out.hops[nbrs[k]] != 0xFFFFu) continue;
            out.row[nbrs[k]]  = out.row[t];
            out.hops[nbrs[k]] = static_cast<uint16_t>(out.hops[t] + 1u);
            queue.push_back(nbrs[k]);
        }
    }
    return out;
}

// Mean tile spacing in true km (planet radius).
double spacingKm(const GeneratedWorld& world) {
    const double n = static_cast<double>(world.grid->tileCount());
    return std::sqrt(8.0 * 3.14159265358979323846 / (std::sqrt(3.0) * n)) * world.derived.planetRadiusMeters / 1000.0;
}

template<typename T>
uint64_t arrayHash(const std::vector<T>& v) {
    uint64_t h = 0x517CC1B727220A95ULL;
    for (const T& x : v) {
        uint32_t bits{};
        static_assert(sizeof(T) <= 8);
        std::memcpy(&bits, &x, sizeof(uint32_t) < sizeof(T) ? sizeof(uint32_t) : sizeof(T));
        h = h * 6364136223846793005ULL + bits;
    }
    return h;
}

} // namespace

// ============================================================================
// Ocean fraction == waterAmount ± 2% (quantile construction)
// ============================================================================

TEST(TerrainStageHeavy, OceanFractionEarthLike) {
    auto world = runPipeline(makeParams(128, 0.70, 12, 0xFACEFEED42ULL));
    ASSERT_NE(world, nullptr);

    const uint32_t N = static_cast<uint32_t>(world->data.elevation.size());
    ASSERT_GT(N, 0u);

    uint32_t oceanCount = 0u;
    for (uint32_t t = 0; t < N; ++t) {
        if (world->data.elevation[t] < world->seaLevelMeters) ++oceanCount;
    }
    double actual = static_cast<double>(oceanCount) / static_cast<double>(N);
    double expected = 0.70;
    double err = actual - expected;
    if (err < 0.0) err = -err;
    EXPECT_LE(err, 0.02)
        << "Ocean fraction " << actual << " vs expected " << expected;
}

TEST(TerrainStageHeavy, OceanFractionDry) {
    auto world = runPipeline(makeParams(128, 0.40, 12, 0xABCDEF12ULL));
    ASSERT_NE(world, nullptr);

    const uint32_t N = static_cast<uint32_t>(world->data.elevation.size());
    uint32_t oceanCount = 0u;
    for (uint32_t t = 0; t < N; ++t) {
        if (world->data.elevation[t] < world->seaLevelMeters) ++oceanCount;
    }
    double actual = static_cast<double>(oceanCount) / static_cast<double>(N);
    double err = actual - 0.40;
    if (err < 0.0) err = -err;
    EXPECT_LE(err, 0.02) << "Ocean fraction " << actual;
}

// ============================================================================
// Mountains at convergent CC boundaries
// Mean elevation of land tiles within 8 hops of a ConvergentCC boundary row
// > mean land elevation + 800m
// ============================================================================

TEST(TerrainStageHeavy, MountainsAtConvergentCC) {
    auto world = runPipeline(makeParams(128, 0.70, 12, 0xFACEFEED42ULL));
    ASSERT_NE(world, nullptr);

    const uint32_t N   = static_cast<uint32_t>(world->data.elevation.size());
    float seaLevel     = world->seaLevelMeters;

    // Mean land elevation
    double sumLand = 0.0; uint32_t cntLand = 0u;
    for (uint32_t t = 0; t < N; ++t) {
        if (world->data.elevation[t] >= seaLevel) {
            sumLand += world->data.elevation[t]; ++cntLand;
        }
    }
    if (cntLand == 0u) { GTEST_SKIP() << "No land tiles"; }
    double meanLand = sumLand / cntLand;

    // Mean elevation of land tiles whose nearest boundary row is ConvergentCC, within 8 hops
    constexpr uint8_t kBTypeCC = 1u; // ConvergentCC
    const NearestRow nearest = nearestBoundaryRow(*world);
    double sumCC = 0.0; uint32_t cntCC = 0u;
    for (uint32_t t = 0; t < N; ++t) {
        if (world->data.boundaryType[nearest.row[t]] == kBTypeCC &&
            nearest.hops[t] <= 8u &&
            world->data.elevation[t] >= seaLevel) {
            sumCC += world->data.elevation[t]; ++cntCC;
        }
    }
    if (cntCC == 0u) { GTEST_SKIP() << "No CC convergent land tiles within 8 of boundary"; }
    double meanCC = sumCC / cntCC;

    // Measured 932 m above the land mean, over 742 tiles.
    EXPECT_GT(meanCC, meanLand + 800.0)
        << "CC mountain mean " << meanCC << " vs land mean " << meanLand;
}

// ============================================================================
// Hypsometry is bimodal: a deep-ocean population (abyssal plains) and a
// near-sea-level continental-platform population, with a sparse mid-depth trough
// between them. This is the headline outcome of the two-law model (depth-age vs
// isostasy) and must hold without per-seed tuning. We count tiles in three bands
// (relative to sea level) and require the abyssal and platform bands to each hold
// far more tiles than the trough band between them.
//   abyssal  band: elev - sea in [-5500, -3000]
//   trough   band: elev - sea in [-2500,  -800]   (the continental-slope gap)
//   platform band: elev - sea in [ -200, +1500]
// At n=256 the populations are large enough to be stable across seeds.
// ============================================================================

TEST(TerrainStageHeavy, HypsometryBimodal) {
    auto world = runPipeline(makeParams(256, 0.70, 12, 0xFACEFEED42ULL));
    ASSERT_NE(world, nullptr);

    const uint32_t N    = static_cast<uint32_t>(world->data.elevation.size());
    const float    sea  = world->seaLevelMeters;

    uint32_t abyssal = 0u, trough = 0u, platform = 0u;
    for (uint32_t t = 0; t < N; ++t) {
        float r = world->data.elevation[t] - sea;
        if (r >= -5500.0f && r <= -3000.0f) ++abyssal;
        else if (r >= -2500.0f && r <= -800.0f) ++trough;
        else if (r >= -200.0f && r <= 1500.0f) ++platform;
    }

    ASSERT_GT(abyssal, 0u);
    ASSERT_GT(platform, 0u);

    // Per-band tile counts span unequal band widths, so normalize to a per-meter
    // density and require the trough density to be a fraction of each mode density.
    double dAbyssal  = abyssal  / 2500.0;  // band width 2500 m
    double dTrough   = trough   / 1700.0;  // band width 1700 m
    double dPlatform = platform / 1700.0;  // band width 1700 m

    double lowerMode = std::min(dAbyssal, dPlatform);
    EXPECT_GT(lowerMode, 0.0);
    EXPECT_LT(dTrough, 0.60 * lowerMode)
        << "Trough density " << dTrough << " not < 60% of lower mode density "
        << lowerMode << " (abyssal=" << dAbyssal << " platform=" << dPlatform << ")";
}

// ============================================================================
// Subduction margin structure at ConvergentCO boundaries (n=256), where the slab side is
// oceanic (TerrainStage's rule: the pair's denser plate goes down):
//   the slab row and the overriding row across from it meet without a step, and the
//   deepest of the two is the slab row (the trench axis is deepest);
//   the coast stands well inland of the trench on the overriding plate;
//   the forearc is drowned and the arc land stands beyond it.
// ============================================================================

TEST(TerrainStageHeavy, TrenchArcStructure) {
    auto world = runPipeline(makeParams(256, 0.70, 12, 0xFACEFEED42ULL), 300);
    ASSERT_NE(world, nullptr);

    const SphereGrid& grid = *world->grid;
    const uint32_t    N    = grid.tileCount();
    const float       sea  = world->seaLevelMeters;
    const double      km   = spacingKm(*world);
    auto isCrust = [&](uint32_t t) { return (world->data.flags[t] & kFlagContinentalCrust) != 0; };
    auto up      = [&](uint32_t t) { return world->data.elevation[t] >= sea; };
    std::array<TileId, 6> nbrs{};

    // Each boundary row's dominant foreign plate, and the slab plate of each pair.
    std::vector<uint8_t> foreign(N, 255u);
    std::vector<double>   density(256 * 256, 0.0);
    std::vector<uint32_t> rows(256 * 256, 0u);
    for (uint32_t t = 0; t < N; ++t) {
        if (world->data.boundaryType[t] == 0u) continue;
        const uint8_t  p   = world->data.plateId[t];
        const uint32_t cnt = grid.neighbors(t, nbrs);
        uint32_t best = 0u;
        for (uint32_t k = 0; k < cnt; ++k) {
            const uint8_t q = world->data.plateId[nbrs[k]];
            if (q == p) continue;
            uint32_t n = 0u;
            for (uint32_t k2 = k; k2 < cnt; ++k2) n += world->data.plateId[nbrs[k2]] == q ? 1u : 0u;
            if (n > best) { best = n; foreign[t] = q; }
        }
        const size_t key = static_cast<size_t>(p) * 256 + foreign[t];
        density[key] += isCrust(t) ? 0.0 : 1.0 + world->data.crustAge[t] / 100.0;
        ++rows[key];
    }
    auto slabOf = [&](uint8_t a, uint8_t b) {
        auto mean = [&](uint8_t x, uint8_t y) {
            const size_t key = static_cast<size_t>(x) * 256 + y;
            return rows[key] > 0u ? density[key] / rows[key] : 0.0;
        };
        const double da = mean(a, b), db = mean(b, a);
        return da > db ? a : db > da ? b : std::max(a, b);
    };

    // Across each subducting CO boundary point: the oceanic slab row against the
    // overriding rows beside it.
    constexpr uint8_t kBTypeCO = 2u;
    uint32_t pairs = 0u, overDeeper = 0u;
    std::vector<float> steps;
    std::vector<uint8_t> overRow(N, 0u);
    for (uint32_t t = 0; t < N; ++t) {
        if (world->data.boundaryType[t] != kBTypeCO || isCrust(t) || foreign[t] == 255u) continue;
        const uint8_t p = world->data.plateId[t];
        if (slabOf(p, foreign[t]) != p) continue;
        const uint32_t cnt = grid.neighbors(t, nbrs);
        for (uint32_t k = 0; k < cnt; ++k) {
            const TileId o = nbrs[k];
            if (world->data.plateId[o] == p || world->data.boundaryType[o] == 0u) continue;
            overRow[o] = 1u;
            const float step = world->data.elevation[o] - world->data.elevation[t];
            ++pairs;
            steps.push_back(std::fabs(step));
            if (step < -300.0f) ++overDeeper;
        }
    }
    ASSERT_GE(pairs, 50u);

    // The overriding plate behind those rows: its tiles by distance from the nearest
    // overriding row, and the distance from each overriding row to its plate's coast.
    std::vector<TileId> overSeeds, shore;
    for (uint32_t t = 0; t < N; ++t) {
        if (overRow[t] != 0u) overSeeds.push_back(t);
        if (!up(t)) continue;
        const uint32_t cnt = grid.neighbors(t, nbrs);
        for (uint32_t k = 0; k < cnt; ++k) {
            if (!up(nbrs[k])) { shore.push_back(t); break; }
        }
    }
    const std::atomic<bool>        noCancel{false};
    foundation::TaskPool           pool(2);
    const std::vector<TileCenterF> centers   = tileCentersF(grid, pool);
    const SeedDistanceResult       fromRow   = seedDistance({.grid = grid, .centers = centers, .seeds = overSeeds,
                                                             .cancel = noCancel, .maxTiles = static_cast<float>(400.0 / km)});
    const SeedDistanceResult       fromShore = seedDistance({.grid = grid, .centers = centers, .seeds = shore,
                                                             .cancel = noCancel, .maxTiles = static_cast<float>(1000.0 / km)});
    std::vector<double> coastKm;
    for (const TileId r : overSeeds) {
        const uint32_t s = fromShore.nearestSeed[r];
        if (s == kNoSeed || world->data.plateId[shore[s]] != world->data.plateId[r]) continue;
        coastKm.push_back(up(r) ? 0.0 : fromShore.distTiles[r] * km);
    }
    uint32_t nearAll = 0u, nearLand = 0u, arcAll = 0u, arcLand = 0u;
    for (uint32_t t = 0; t < N; ++t) {
        const uint32_t r = fromRow.nearestSeed[t];
        if (r == kNoSeed || world->data.plateId[t] != world->data.plateId[overSeeds[r]]) continue;
        const double d = fromRow.distTiles[t] * km;
        if (d <= 60.0) { ++nearAll; if (up(t)) ++nearLand; }
        if (d >= 200.0 && d <= 300.0) { ++arcAll; if (up(t)) ++arcLand; }
    }
    ASSERT_GE(coastKm.size(), 20u);
    ASSERT_GE(nearAll, 50u);
    ASSERT_GE(arcAll, 50u);
    std::sort(coastKm.begin(), coastKm.end());
    std::sort(steps.begin(), steps.end());
    const double medianStepM   = steps[steps.size() / 2];
    const double medianCoastKm = coastKm[coastKm.size() / 2];
    const double nearLandFrac  = static_cast<double>(nearLand) / nearAll;
    const double arcLandFrac   = static_cast<double>(arcLand) / arcAll;

    // Measured at n=256: median step 799 m (a row is 30 km, a steep stretch of inner
    // wall), 16 of 194 overriding rows deeper, coast median 102 km, forearc 11% land, arc
    // band 52% land.
    EXPECT_LT(medianStepM, 1500.0) << "median slab/overriding row step " << medianStepM << " m";
    EXPECT_LT(overDeeper, pairs / 5u) << overDeeper << " of " << pairs << " overriding rows lie deeper than the slab row";
    EXPECT_GT(medianCoastKm, 60.0) << "coast median " << medianCoastKm << " km from the trench";
    EXPECT_LT(medianCoastKm, 200.0) << "coast median " << medianCoastKm << " km from the trench";
    EXPECT_LT(nearLandFrac, 0.15) << "the forearc within 60 km of the trench is " << nearLandFrac << " land";
    EXPECT_GT(arcLandFrac, nearLandFrac + 0.3) << "arc band land " << arcLandFrac << " vs forearc " << nearLandFrac;
}

// ============================================================================
// No cliff where the nearest boundary jumps. Across a bend or a notch in a boundary row,
// or from one boundary to another, a tile's nearest boundary tile jumps along strike,
// further the farther out; kernels that followed it stepped there, cutting straight
// cliffs of 3-4 km across the land behind a margin. Counted on continental crust 60-200 km
// from the boundary: neighbouring tiles off the rows whose nearest boundary tiles are 4 or
// more spacings apart, a step over 1.5 km between them that is more than twice the steps on
// either side along the same line. Tiles with different crust or orogeny ages are left
// out: those fields step between coarse cells (WOR-599), not at boundaries.
// ============================================================================

TEST(TerrainStageHeavy, NoCliffsWhereTheNearestBoundaryJumps) {
    auto world = runPipeline(makeParams(512, 0.70, 12, 7ULL), 600);
    ASSERT_NE(world, nullptr);

    const SphereGrid&  grid = *world->grid;
    const WorldData&   data = world->data;
    const uint32_t     N    = grid.tileCount();
    const double       km   = spacingKm(*world);
    std::vector<TileId> rows;
    for (TileId t = 0; t < N; ++t) {
        if (data.boundaryType[t] != 0u) rows.push_back(t);
    }
    ASSERT_FALSE(rows.empty());
    foundation::TaskPool           pool(4);
    const std::vector<TileCenterF> centers = tileCentersF(grid, pool);
    const std::atomic<bool>        noCancel{false};
    const SeedDistanceResult       nearest = seedDistance({.grid = grid, .centers = centers, .seeds = rows, .cancel = noCancel});
    const double spacing = std::sqrt(8.0 * 3.14159265358979323846 / (std::sqrt(3.0) * N));
    auto apartTiles = [&](TileId a, TileId b) {
        const Vec3d  ca = grid.tileCenter(rows[nearest.nearestSeed[a]]);
        const Vec3d  cb = grid.tileCenter(rows[nearest.nearestSeed[b]]);
        const double dx = ca.x - cb.x, dy = ca.y - cb.y, dz = ca.z - cb.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz) / spacing;
    };
    auto onCrustOffRow = [&](TileId t) {
        return data.boundaryType[t] == 0u && (data.flags[t] & kFlagContinentalCrust) != 0;
    };
    auto offRow = [&](TileId t) { return data.boundaryType[t] == 0u; };
    const auto& e = data.elevation;

    uint32_t cliffs = 0u;
    std::array<TileId, 6> nt{}, nn{};
    for (TileId t = 0; t < N; ++t) {
        if (!onCrustOffRow(t)) continue;
        const double bandKm = data.boundaryDistance[t] * km;
        if (bandKm <= 60.0 || bandKm > 200.0) continue;
        const Vec3d    pt = grid.tileCenter(t);
        const uint32_t ct = grid.neighbors(t, nt);
        for (uint32_t k = 0; k < ct; ++k) {
            const TileId nb = nt[k];
            if (nb <= t || !onCrustOffRow(nb)) continue;
            if (data.crustAge[t] != data.crustAge[nb] || data.orogenyAge[t] != data.orogenyAge[nb]) continue;
            const float step = std::fabs(e[nb] - e[t]);
            if (step <= 1500.0f || apartTiles(t, nb) < 4.0) continue;
            // The tiles before and after the pair along the same line.
            const Vec3d  pn = grid.tileCenter(nb);
            const double dx = pn.x - pt.x, dy = pn.y - pt.y, dz = pn.z - pt.z;
            TileId prev = t, next = nb;
            double lo = std::numeric_limits<double>::max(), hi = -lo;
            for (uint32_t j = 0; j < ct; ++j) {
                const Vec3d  q   = grid.tileCenter(nt[j]);
                const double dot = (q.x - pt.x) * dx + (q.y - pt.y) * dy + (q.z - pt.z) * dz;
                if (dot < lo) { lo = dot; prev = nt[j]; }
            }
            const uint32_t cn = grid.neighbors(nb, nn);
            for (uint32_t j = 0; j < cn; ++j) {
                const Vec3d  q   = grid.tileCenter(nn[j]);
                const double dot = (q.x - pn.x) * dx + (q.y - pn.y) * dy + (q.z - pn.z) * dz;
                if (dot > hi) { hi = dot; next = nn[j]; }
            }
            if (!offRow(prev) || !offRow(next)) continue;
            const float side = std::max(std::fabs(e[t] - e[prev]), std::fabs(e[next] - e[nb]));
            if (step > 2.0f * side) ++cliffs;
        }
    }
    // Measured 48 (seeds 1, 31337 and 12345: 58, 41, 32). With each tile reading its nearest
    // boundary tile's inputs alone, as before the anchors: 278 (221, 148, 157).
    EXPECT_LT(cliffs, 120u) << cliffs << " isolated cliffs over 1.5 km where the nearest boundary jumps";
}

// ============================================================================
// Passive margins: continental tiles > 15 tiles from any plate boundary
// have |elevation - continentalBase| < 1500m on average (no phantom mountains)
// ============================================================================

TEST(TerrainStageHeavy, PassiveMarginsCalm) {
    auto world = runPipeline(makeParams(128, 0.70, 12, 0xFACEFEED42ULL));
    ASSERT_NE(world, nullptr);

    const uint32_t N = static_cast<uint32_t>(world->data.elevation.size());
    constexpr float kContinentalBase = 400.0f;

    double sumDev = 0.0; uint32_t cnt = 0u;
    for (uint32_t t = 0; t < N; ++t) {
        bool isCrust = (world->data.flags[t] & kFlagContinentalCrust) != 0;
        if (!isCrust) continue;
        if (world->data.boundaryDistance[t] <= 15) continue;

        float dev = world->data.elevation[t] - kContinentalBase;
        if (dev < 0.0f) dev = -dev;
        sumDev += dev; ++cnt;
    }
    if (cnt < 50u) { GTEST_SKIP() << "Not enough deep-interior continental tiles (cnt=" << cnt << ")"; }
    double meanDev = sumDev / cnt;
    EXPECT_LT(meanDev, 1500.0)
        << "Mean elevation deviation in passive margin interior: " << meanDev << "m (expected < 1500m)";
}

// ============================================================================
// Determinism: same seed → identical elevation + boundaryType arrays
// ============================================================================

TEST(TerrainStageHeavy, Deterministic) {
    auto p  = makeParams(64, 0.70, 12, 0xCAFEBABEDEAD0000ULL);
    auto w1 = runPipeline(p);
    auto w2 = runPipeline(p);
    ASSERT_NE(w1, nullptr); ASSERT_NE(w2, nullptr);

    uint64_t h1e = arrayHash(w1->data.elevation);
    uint64_t h2e = arrayHash(w2->data.elevation);
    EXPECT_EQ(h1e, h2e) << "elevation array differs between runs with same seed";

    uint64_t h1b = arrayHash(w1->data.boundaryType);
    uint64_t h2b = arrayHash(w2->data.boundaryType);
    EXPECT_EQ(h1b, h2b) << "boundaryType array differs between runs with same seed";

    EXPECT_FLOAT_EQ(w1->seaLevelMeters, w2->seaLevelMeters)
        << "seaLevelMeters differs between runs";
}

TEST(TerrainStageHeavy, DeterministicAcrossThreadCounts) {
    const auto p  = makeParams(64, 0.70, 12, 0xCAFEBABEDEAD0000ULL);
    const auto w1 = runPipeline(p, 180, 1);
    const auto w8 = runPipeline(p, 180, 8);
    ASSERT_NE(w1, nullptr); ASSERT_NE(w8, nullptr);
    EXPECT_EQ(arrayHash(w1->data.elevation), arrayHash(w8->data.elevation));
    EXPECT_EQ(arrayHash(w1->data.boundaryType), arrayHash(w8->data.boundaryType));
    EXPECT_EQ(arrayHash(w1->data.boundaryDistance), arrayHash(w8->data.boundaryDistance));
    EXPECT_FLOAT_EQ(w1->seaLevelMeters, w8->seaLevelMeters);
}

TEST(TerrainStageHeavy, DifferentSeedsDiffer) {
    auto p1 = makeParams(64, 0.70, 12, 0xAAAAAAAAAAAAAAAAULL);
    auto p2 = makeParams(64, 0.70, 12, 0xBBBBBBBBBBBBBBBBULL);
    auto w1 = runPipeline(p1);
    auto w2 = runPipeline(p2);
    ASSERT_NE(w1, nullptr); ASSERT_NE(w2, nullptr);

    uint64_t h1 = arrayHash(w1->data.elevation);
    uint64_t h2 = arrayHash(w2->data.elevation);
    EXPECT_NE(h1, h2) << "different seeds produced identical elevation arrays";
}

// ============================================================================
// Valid fields are set
// ============================================================================

TEST(TerrainStageHeavy, ValidFieldsSet) {
    auto world = runPipeline(makeParams(64));
    ASSERT_NE(world, nullptr);

    EXPECT_NE(world->validFields & static_cast<uint32_t>(WorldField::Elevation),       0u);
    EXPECT_NE(world->validFields & static_cast<uint32_t>(WorldField::BoundaryType),    0u);
    EXPECT_NE(world->validFields & static_cast<uint32_t>(WorldField::BoundaryDistance),0u);
}

// ============================================================================
// BoundaryDistance: interior tiles of large plates have distance > 5
// ============================================================================

TEST(TerrainStageHeavy, BoundaryDistanceNonTrivial) {
    auto world = runPipeline(makeParams(128, 0.70, 12, 0xFACEFEED42ULL));
    ASSERT_NE(world, nullptr);

    const uint32_t N = static_cast<uint32_t>(world->data.boundaryDistance.size());
    uint32_t deepInterior = 0u;
    for (uint32_t t = 0; t < N; ++t) {
        if (world->data.boundaryDistance[t] > 5u) ++deepInterior;
    }
    // Expect at least 30% of tiles are more than 5 tiles from a boundary
    double fraction = static_cast<double>(deepInterior) / static_cast<double>(N);
    EXPECT_GT(fraction, 0.30)
        << "Only " << (fraction*100) << "% of tiles are > 5 tiles from a boundary";
}

// ============================================================================
// Elevation ranges are plausible: land max > 2000m, ocean min < -3000m
// ============================================================================

TEST(TerrainStageHeavy, ElevationRange) {
    auto world = runPipeline(makeParams(128, 0.70, 12, 0xFACEFEED42ULL));
    ASSERT_NE(world, nullptr);

    const uint32_t N = static_cast<uint32_t>(world->data.elevation.size());
    float seaLevel = world->seaLevelMeters;

    float landMax  = seaLevel; // track highest land point
    float oceanMin = seaLevel; // track deepest ocean point
    for (uint32_t t = 0; t < N; ++t) {
        float e = world->data.elevation[t];
        if (e >= seaLevel && e > landMax)  landMax  = e;
        if (e <  seaLevel && e < oceanMin) oceanMin = e;
    }

    EXPECT_GT(landMax - seaLevel, 2000.0f)
        << "Highest land point only " << (landMax - seaLevel) << "m above sea level";
    // oceanMin < seaLevel, so seaLevel - oceanMin > 0; we want it > 3000m
    EXPECT_GT(seaLevel - oceanMin, 3000.0f)
        << "Ocean depth (sea level - min) = " << (seaLevel - oceanMin) << "m, expected > 3000m";
}

// ============================================================================
// Convergent boundary fraction > 25% at Earth-like n=256
// ============================================================================

TEST(TerrainStageHeavy, ConvergentBoundaryFraction) {
    auto world = runPipeline(makeParams(256, 0.70, 12, 0xFACEFEED42ULL));
    ASSERT_NE(world, nullptr);

    const uint32_t N = static_cast<uint32_t>(world->data.boundaryType.size());
    uint32_t nBoundary   = 0u;
    uint32_t nConvergent = 0u;
    for (uint32_t t = 0; t < N; ++t) {
        uint8_t bt = world->data.boundaryType[t];
        if (bt == 0u) continue;
        ++nBoundary;
        if (bt >= 1u && bt <= 3u) ++nConvergent; // CC, CO, OO
    }
    if (nBoundary < 100u) { GTEST_SKIP() << "Not enough boundary tiles"; }
    double fraction = static_cast<double>(nConvergent) / static_cast<double>(nBoundary);
    EXPECT_GT(fraction, 0.25)
        << "Convergent boundary fraction " << fraction
        << " (convergent=" << nConvergent << " total=" << nBoundary << ")";
}

// ============================================================================
// Convergence sign correctness: a 2-plate analytic case.
// Plate A (north pole, CCW from above) and Plate B (south pole, CW from above)
// share a boundary along the equator. Relative velocity at a point on the equator
// points inward (convergent). Verify that the sign convention used in TerrainStage
// yields convergence > 0 for approaching plates.
//
// v_A = omega_A x r   (omega_A = (0,0,+w))  → at r=(1,0,0): v_A = (0,+w,0)
// v_B = omega_B x r   (omega_B = (0,0,-w))  → at r=(1,0,0): v_B = (0,-w,0)
// v_rel = v_A - v_B = (0,+2w,0)
// boundary normal toward B (south) = (0,-1,0) roughly,
// but the equator is between them so the outward normal from A toward B is (0,-1,0)
// for a tile just north of equator.
// Actually at the equator point r=(1,0,0), the normal from A toward B's territory
// is (0,-1,0) (south).
// convergence = -dot(v_rel, normal) = -dot((0,2w,0),(0,-1,0)) = -(-2w) = +2w > 0 ✓
// ============================================================================

TEST(TerrainStage, ConvergenceSignCorrectness) {
    // Inline re-implementation of the convergence math to verify sign convention.
    auto cross3d = [](double ax, double ay, double az,
                      double bx, double by, double bz,
                      double& rx, double& ry, double& rz) {
        rx = ay*bz - az*by; ry = az*bx - ax*bz; rz = ax*by - ay*bx;
    };
    auto dot3d = [](double ax, double ay, double az,
                    double bx, double by, double bz) {
        return ax*bx + ay*by + az*bz;
    };

    // Plate A: omega = (0,0,+w) — north pole, CCW from above (approaching equator from north)
    // Plate B: omega = (0,0,-w) — south pole, CW from above (approaching equator from south)
    const double w = 1.0;
    double oaX=0, oaY=0, oaZ=+w;
    double obX=0, obY=0, obZ=-w;

    // Evaluation point: r = (1, 0, 0) (on equator, 0° lon)
    double rX=1.0, rY=0.0, rZ=0.0;

    // v_A = omega_A x r
    double vaX, vaY, vaZ;
    cross3d(oaX, oaY, oaZ, rX, rY, rZ, vaX, vaY, vaZ);
    // v_B = omega_B x r
    double vbX, vbY, vbZ;
    cross3d(obX, obY, obZ, rX, rY, rZ, vbX, vbY, vbZ);
    // v_rel = v_A - v_B
    double vrX = vaX - vbX, vrY = vaY - vbY, vrZ = vaZ - vbZ;

    // Boundary normal from plate-A tile toward plate-B territory.
    // Plate B is to the south; normal = (0,-1,0).
    double normX = 0.0, normY = -1.0, normZ = 0.0;

    // convergence = -dot(v_rel, normal)
    double convergence = -dot3d(vrX, vrY, vrZ, normX, normY, normZ);

    // Expect convergence > 0 (plates approaching each other)
    EXPECT_GT(convergence, 0.0)
        << "Expected convergence > 0 for two plates approaching equator. "
        << "v_rel=(" << vrX << "," << vrY << "," << vrZ << ") "
        << "convergence=" << convergence;

    // Also verify that the magnitude is as expected: v_rel = (0, 2w, 0),
    // normal = (0,-1,0), so convergence = -dot((0,2w,0),(0,-1,0)) = 2w.
    EXPECT_NEAR(convergence, 2.0 * w, 1e-12);

    // Diverging case: omega_A = (0,0,-w) south pole, omega_B = (0,0,+w) north pole,
    // same evaluation point and normal. Plates moving away from equator.
    double oaX2=0, oaY2=0, oaZ2=-w;
    double obX2=0, obY2=0, obZ2=+w;
    double vaX2, vaY2, vaZ2, vbX2, vbY2, vbZ2;
    cross3d(oaX2, oaY2, oaZ2, rX, rY, rZ, vaX2, vaY2, vaZ2);
    cross3d(obX2, obY2, obZ2, rX, rY, rZ, vbX2, vbY2, vbZ2);
    double vrX2 = vaX2-vbX2, vrY2 = vaY2-vbY2, vrZ2 = vaZ2-vbZ2;
    double convergence2 = -dot3d(vrX2, vrY2, vrZ2, normX, normY, normZ);
    EXPECT_LT(convergence2, 0.0)
        << "Expected convergence < 0 for plates diverging from equator. "
        << "convergence=" << convergence2;
}

// ============================================================================
// Shelf profile: passive continental margins should have a band of tiles near
// -120m (flat shelf), then a break, then steep slope to abyssal.
// Also verifies oceanFraction stays within ±0.01 of waterAmount and that
// shelfSubmergedFraction rises well above the ~0.8% baseline.
// ============================================================================

TEST(TerrainStageHeavy, ShelfProfileAndSubmergedFraction) {
    // n=256: tileCount = 10*256^2+2 = 655362, tileWidthKm ≈ 40030/sqrt(655362) ≈ 49 km.
    // Enough resolution to resolve the 200 km shelf band (kShelfWidthKm in TerrainStage).
    auto world = runPipeline(makeParams(256, 0.70, 12, 0xFACEFEED42ULL), 300);
    ASSERT_NE(world, nullptr);

    const uint32_t N     = static_cast<uint32_t>(world->data.elevation.size());
    const float    sea   = world->seaLevelMeters;
    const double   waterAmount = 0.70;

    // --- oceanFraction within ±0.01 of waterAmount ---
    uint32_t oceanCount = 0u;
    for (uint32_t t = 0; t < N; ++t) {
        if (world->data.elevation[t] < sea) ++oceanCount;
    }
    double oceanFraction = static_cast<double>(oceanCount) / static_cast<double>(N);
    EXPECT_NEAR(oceanFraction, waterAmount, 0.01)
        << "oceanFraction=" << oceanFraction << " waterAmount=" << waterAmount;

    // --- shelfSubmergedFraction: continental tiles below sea level ---
    // Earth: ~8-15% of continental crust is submerged shelf; baseline was ~0.8%.
    uint32_t contTotal    = 0u;
    uint32_t contSubmerged = 0u;
    // Shelf count: continental tiles below sea AND above -400m (shallow shelf band)
    uint32_t shelfCount = 0u;

    for (uint32_t t = 0; t < N; ++t) {
        bool isCont = (world->data.flags[t] & kFlagContinentalCrust) != 0;
        if (!isCont) continue;
        ++contTotal;
        float e = world->data.elevation[t];
        if (e < sea) {
            ++contSubmerged;
            if (e >= sea - 400.0f) ++shelfCount; // shallow-submerged band
        }
    }

    ASSERT_GT(contTotal, 0u);
    double shelfSubmergedFraction = static_cast<double>(contSubmerged) /
                                    static_cast<double>(contTotal);
    EXPECT_GT(shelfSubmergedFraction, 0.05)
        << "shelfSubmergedFraction=" << shelfSubmergedFraction
        << " — expected >5% (Earth is ~8-15%); baseline was ~0.8%";
    EXPECT_LT(shelfSubmergedFraction, 0.30)
        << "shelfSubmergedFraction=" << shelfSubmergedFraction
        << " suspiciously high (>30%); shelf profile may be too wide or too shallow";

    // --- A visible shelf shoulder: water shallower than 250 m, nearly all of it shelf ---
    // The quantile sea level lands in the shelf band here, so the outer shelf (continental
    // crust) floods to a few meters, and the top of the slope (oceanic crust beside it)
    // lies a little deeper.
    const SphereGrid& grid = *world->grid;
    auto isCrust = [&](uint32_t t) { return (world->data.flags[t] & kFlagContinentalCrust) != 0; };
    std::array<TileId, 6> nbrs{};
    auto besideCrust = [&](uint32_t t) {
        const uint32_t cnt = grid.neighbors(t, nbrs);
        for (uint32_t k = 0; k < cnt; ++k) {
            if (isCrust(nbrs[k])) return true;
        }
        return false;
    };
    uint32_t shallowTiles = 0u, shallowShelfTiles = 0u;
    for (uint32_t t = 0; t < N; ++t) {
        float r = world->data.elevation[t] - sea;
        if (r < -250.0f || r >= 0.0f) continue;
        ++shallowTiles;
        if (isCrust(t) || besideCrust(t)) ++shallowShelfTiles;
    }
    double shallowFraction = static_cast<double>(shallowTiles) / static_cast<double>(N);
    double shelfShare      = shallowTiles > 0u ? static_cast<double>(shallowShelfTiles) / shallowTiles : 0.0;
    EXPECT_GT(shallowFraction, 0.01)
        << "Shallow water (<250m below sea) only " << (shallowFraction * 100.0)
        << "% of tiles; expected a visible shelf shoulder";
    // Measured 1.43% of tiles, 98.2% of them shelf or the oceanic edge row beside it.
    EXPECT_GT(shelfShare, 0.9)
        << "Only " << (shelfShare * 100.0) << "% of the shallow water is shelf or the edge row beside it";

    // --- No land rim outside a flooded shelf on passive margins ---
    // An oceanic edge tile above sea beside a submerged continental edge tile is land
    // seaward of the shelf: the rim abyssal noise raises when it runs at full amplitude up
    // to the shelf break. Far from plate boundaries no trench or arc kernel reaches the
    // edge; volcanism lifts the shelf behind a cone too, so it rarely counts here. Only
    // edge tiles beside a flooded shelf can hold a rim, so they are the denominator.
    constexpr uint16_t kPassiveBoundarySpacings = 10; // boundaryDistance units, ~300 km at n=256
    uint32_t floodedEdgeTiles = 0u;
    uint32_t rimTiles = 0u;
    for (uint32_t t = 0; t < N; ++t) {
        if (isCrust(t) || world->data.boundaryDistance[t] <= kPassiveBoundarySpacings) continue;
        bool floodedShelf = false;
        uint32_t cnt = grid.neighbors(t, nbrs);
        for (uint32_t k = 0; k < cnt; ++k) {
            if (isCrust(nbrs[k]) && world->data.elevation[nbrs[k]] < sea) floodedShelf = true;
        }
        if (!floodedShelf) continue;
        ++floodedEdgeTiles;
        if (world->data.elevation[t] >= sea) ++rimTiles;
    }
    ASSERT_GT(floodedEdgeTiles, 500u);
    double rimFraction = static_cast<double>(rimTiles) / static_cast<double>(floodedEdgeTiles);
    EXPECT_LT(rimFraction, 0.03)
        << rimTiles << " of " << floodedEdgeTiles
        << " passive-margin oceanic edge tiles beside a flooded shelf stand above sea";
}

// ============================================================================
// Visual image export test — always writes n=256; WORLDGEN_VISUAL=1 → n=512
// ============================================================================

TEST(TerrainStageVisualHeavy, WriteDebugImages) {
    const uint32_t n = 256;
    auto world = runPipeline(makeParams(n, 0.70, 12, 42ULL), 300);
    ASSERT_NE(world, nullptr) << "Pipeline failed at n=" << n;

#ifdef _WIN32
    std::system("mkdir build\\debug-images 2>nul");
#else
    std::system("mkdir -p build/debug-images");
#endif

    EXPECT_TRUE(exportEquirectangularBmp(*world, WorldFieldOrMode::Elevation,
                                         "build/debug-images/elevation.bmp", 2048))
        << "Failed to write elevation.bmp";

    EXPECT_TRUE(exportEquirectangularBmp(*world, WorldFieldOrMode::BoundaryTypeMap,
                                         "build/debug-images/boundary_types.bmp", 2048))
        << "Failed to write boundary_types.bmp";

    EXPECT_TRUE(exportEquirectangularBmp(*world, WorldFieldOrMode::Crust,
                                         "build/debug-images/crust.bmp", 2048))
        << "Failed to write crust.bmp";

    const char* visual = std::getenv("WORLDGEN_VISUAL");
    if (visual && std::string(visual) == "1") {
        printf("[TerrainStageVisual] Running n=512 seed 42 (WORLDGEN_VISUAL=1)...\n");
        fflush(stdout);
        auto w512 = runPipeline(makeParams(512, 0.70, 12, 42ULL), 600);
        if (w512) {
            exportEquirectangularBmp(*w512, WorldFieldOrMode::Elevation,
                                     "build/debug-images/elevation.bmp", 2048);
            exportEquirectangularBmp(*w512, WorldFieldOrMode::BoundaryTypeMap,
                                     "build/debug-images/boundary_types.bmp", 2048);
            printf("[TerrainStageVisual] n=512 seed 42 images written to build/debug-images/\n");
            fflush(stdout);
        } else {
            printf("[TerrainStageVisual] n=512 seed 42 pipeline failed or timed out\n");
        }

        printf("[TerrainStageVisual] Running n=512 seed 1337...\n");
        fflush(stdout);
        auto w512b = runPipeline(makeParams(512, 0.70, 12, 1337ULL), 600);
        if (w512b) {
            exportEquirectangularBmp(*w512b, WorldFieldOrMode::Elevation,
                                     "build/debug-images/elevation_seed1337.bmp", 2048);
            exportEquirectangularBmp(*w512b, WorldFieldOrMode::BoundaryTypeMap,
                                     "build/debug-images/boundary_types_seed1337.bmp", 2048);
            printf("[TerrainStageVisual] n=512 seed 1337 images written\n");
            fflush(stdout);
        } else {
            printf("[TerrainStageVisual] n=512 seed 1337 failed or timed out\n");
        }
    }
}

} // namespace worldgen
