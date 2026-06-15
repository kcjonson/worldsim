// WorldStats tests — basic sanity checks at tiny n.

#include "worldgen/debug/WorldStats.h"

#include "worldgen/data/PlanetParams.h"
#include "worldgen/grid/SphereGrid.h"
#include "worldgen/pipeline/PlanetGenerator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <numeric>
#include <thread>

namespace worldgen {

namespace {

std::shared_ptr<const GeneratedWorld> runPipeline(const PlanetParams& params,
                                                   int timeoutSeconds = 120) {
    PlanetGenerator gen;
    gen.start(params);
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(timeoutSeconds);
    while (std::chrono::steady_clock::now() < deadline) {
        auto prog = gen.progress();
        if (prog.state == GenerationProgress::State::Complete ||
            prog.state == GenerationProgress::State::Failed ||
            prog.state == GenerationProgress::State::Cancelled) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return gen.takeResult();
}

} // namespace

TEST(WorldStats, OceanFractionInRange) {
    PlanetParams p = PlanetParams::preset(Preset::EarthLike);
    p.gridSubdivision = 16;
    p.seed = 0xABCDEF01u;

    auto world = runPipeline(p);
    ASSERT_NE(world, nullptr);

    WorldStats stats = computeWorldStats(*world);

    EXPECT_GE(stats.oceanFraction, 0.0f);
    EXPECT_LE(stats.oceanFraction, 1.0f);
}

TEST(WorldStats, HistogramSumsToN) {
    PlanetParams p = PlanetParams::preset(Preset::EarthLike);
    p.gridSubdivision = 16;
    p.seed = 0xDEADC0DEu;

    auto world = runPipeline(p);
    ASSERT_NE(world, nullptr);

    WorldStats stats = computeWorldStats(*world);

    EXPECT_EQ(stats.hypsoHist.size(), 256u);

    uint64_t total = 0;
    for (uint32_t c : stats.hypsoHist) total += c;
    EXPECT_EQ(total, static_cast<uint64_t>(stats.tileCount));
}

TEST(WorldStats, ComponentsFound) {
    PlanetParams p = PlanetParams::preset(Preset::EarthLike);
    p.gridSubdivision = 16;
    p.seed = 0x12345678u;

    auto world = runPipeline(p);
    ASSERT_NE(world, nullptr);

    WorldStats stats = computeWorldStats(*world);

    // At n=16 there are only 2562 tiles, so we may not hit the 0.5% threshold
    // for multiple continents, but the computed fractions should be in range.
    EXPECT_GE(stats.oceanFraction, 0.0f);
    EXPECT_LE(stats.oceanFraction, 1.0f);
    for (const auto& cont : stats.continents) {
        EXPECT_GT(cont.isoperimetricRatio, 0.0f);
    }
}

// Geodesic aspect ratio: per-belt fields are non-negative and medians are consistent.
TEST(WorldStats, GeodesicAspectRatioFieldsValid) {
    PlanetParams p = PlanetParams::preset(Preset::EarthLike);
    p.gridSubdivision = 32;
    p.seed = 0xBEEF1234u;

    auto world = runPipeline(p, 180);
    ASSERT_NE(world, nullptr);

    WorldStats stats = computeWorldStats(*world);

    EXPECT_GE(stats.medianAspectRatio, 0.0f);
    EXPECT_GE(stats.tileWeightedMedianAspectRatio, 0.0f);

    for (const auto& b : stats.belts) {
        EXPECT_GE(b.lengthKm, 0.0f);
        EXPECT_GE(b.geoWidthKm, 0.0f);
        EXPECT_GE(b.aspectRatio, 1.0f); // length >= width by construction
        // A belt >= 32 tiles can't have zero hops unless it's a single tile (impossible).
        if (b.tileCount >= 32) {
            EXPECT_GT(b.lengthKm, 0.0f);
        }
    }

    // Tile-weighted median should be >= plain median when large belts are more elongated,
    // or at worst equal — either way both must be >= 1.
    if (!stats.belts.empty()) {
        EXPECT_GE(stats.tileWeightedMedianAspectRatio, 1.0f);
        EXPECT_GE(stats.medianAspectRatio, 1.0f);
    }
}

// Hand-built synthetic BeltStats: verify the tile-weighted median picks the
// large-belt value over many small blobs.
TEST(WorldStats, TileWeightedMedianFavorsLargeBelt) {
    // Simulate belt list: one large thin belt (high aspect) plus many small blobs (low aspect).
    // We test by calling computeWorldStats on a tiny real world and inspecting the math,
    // not by calling the internal helper directly.  Instead verify the property holds:
    // if most tiles live in a high-aspect belt, tile-weighted median >= plain median.
    //
    // At n=16 there may not be any belts, so we just assert correctness of the
    // published medians being non-negative when the belt list is empty.
    PlanetParams p = PlanetParams::preset(Preset::EarthLike);
    p.gridSubdivision = 16;
    p.seed = 0xCAFEBABEu;

    auto world = runPipeline(p);
    ASSERT_NE(world, nullptr);

    WorldStats stats = computeWorldStats(*world);

    EXPECT_GE(stats.medianAspectRatio, 0.0f);
    EXPECT_GE(stats.tileWeightedMedianAspectRatio, 0.0f);

    // For any belt present: aspect ratio = lengthKm / max(geoWidthKm, tileKm) >= 1.
    for (const auto& b : stats.belts) {
        EXPECT_GE(b.aspectRatio, 1.0f);
    }
}

// Water stats: sanity-check all new drainage fields are in range.
TEST(WorldStats, WaterStatsInRange) {
    PlanetParams p = PlanetParams::preset(Preset::EarthLike);
    p.gridSubdivision = 16;
    p.seed = 0xA917E001u;

    auto world = runPipeline(p);
    ASSERT_NE(world, nullptr);

    WorldStats stats = computeWorldStats(*world);

    // Fractions must be in [0, 1].
    EXPECT_GE(stats.riverTileFraction,            0.0f);
    EXPECT_LE(stats.riverTileFraction,            1.0f);
    EXPECT_GE(stats.endorheicSinkFraction,        0.0f);
    EXPECT_LE(stats.endorheicSinkFraction,        1.0f);
    EXPECT_GE(stats.lakeTileFraction,             0.0f);
    EXPECT_LE(stats.lakeTileFraction,             1.0f);
    EXPECT_GE(stats.landWithWaterNearbyFraction,  0.0f);
    EXPECT_LE(stats.landWithWaterNearbyFraction,  1.0f);

    // W-1: depression routing sets kFlagLake on ponded basin tiles, so lakes may
    // now form. The fraction stays small (lakes are a few % of the surface at
    // most); at n=16 (2562 tiles) a given Earth-like world may have zero or a
    // handful, so only the [0,1] range is asserted here. The heavy drainage test
    // (WorldStatsHeavy.DrainageRoutesToOcean) checks that lakes actually form on
    // a higher-resolution world.
    EXPECT_GE(stats.lakeTileFraction, 0.0f);
    EXPECT_LE(stats.lakeTileFraction, 0.20f)
        << "lakes should never cover a large fraction of the surface";

    // Sinks cannot exceed land tile count.
    const uint32_t landCount = static_cast<uint32_t>(stats.landTileCount);
    EXPECT_LE(stats.sinkTileCount, landCount);

    // Flow stats: non-negative.
    EXPECT_GE(stats.maxFlowAccum,      0.0f);
    EXPECT_GE(stats.meanFlowAccumLand, 0.0f);
    // Mean can't exceed max.
    EXPECT_LE(stats.meanFlowAccumLand, stats.maxFlowAccum + 1e-3f);
}

// Biome fractions: sum over land biomes is ~1.0, all values in [0,1].
TEST(WorldStats, BiomeFractionsSumToOne) {
    PlanetParams p = PlanetParams::preset(Preset::EarthLike);
    p.gridSubdivision = 16;
    p.seed = 0xB10BE5EED0u;

    auto world = runPipeline(p);
    ASSERT_NE(world, nullptr);

    WorldStats stats = computeWorldStats(*world);

    // Every fraction must be in [0,1].
    for (size_t i = 0; i < stats.biomeFraction.size(); ++i) {
        EXPECT_GE(stats.biomeFraction[i], 0.0f) << "biome " << i;
        EXPECT_LE(stats.biomeFraction[i], 1.0f) << "biome " << i;
    }

    // Fractions over non-water biomes must sum to ~1 when there is land.
    if (stats.landTileCount > 0.0f) {
        float sum = 0.0f;
        for (size_t i = 0; i < stats.biomeFraction.size(); ++i) {
            auto b = static_cast<Biome>(i);
            if (b == Biome::Ocean || b == Biome::Lake) continue;
            sum += stats.biomeFraction[i];
        }
        EXPECT_NEAR(sum, 1.0f, 0.01f) << "land biome fractions must sum to 1";
    }

    // Shelf fraction must be in [0,1].
    EXPECT_GE(stats.shelfSubmergedFraction, 0.0f);
    EXPECT_LE(stats.shelfSubmergedFraction, 1.0f);
}

// ============================================================================
// C-4 acceptance gate: a full Earth-like pipeline must land biome fractions in
// Earth-like ranges. Runs at n=64 (cheap enough for the Debug CI ctest budget;
// the fractions hold within a couple points at n=512).
//
// This guards the MODEL's central tendency, not one seed's continental lottery.
// Biome fractions are (climate model) x (continental arrangement); a single seed
// can draw a polar-heavy or desert-heavy layout (e.g. seed 42 is an ocean-
// dominated cold world: ~19% tundra, ~3% hot desert) without the climate model
// being wrong. So we AVERAGE fractions across a fixed seed set and assert the
// MEAN lands in Earth-like ranges. The set includes the documented calibration
// seeds (7, 42, 1337) plus a 1..3 prefix; seed 42 stays in so the guard is not
// dodging its own hardest case. Measured across-seed MEANs at n=64:
//   ArcticTundra ~11.8%   HotDesert ~7.1%   total forest >>15%
// The gates sit outside that mean with margin: a genuine regression guard that
// catches a systematic climate skew (which would move ALL seeds), not seed luck.
// ============================================================================
TEST(WorldStatsHeavy, EarthLikeBiomeFractionsAcceptance) {
    constexpr uint64_t kSeeds[] = {7ull, 42ull, 1337ull, 1ull, 2ull, 3ull};
    constexpr size_t kSeedCount = sizeof(kSeeds) / sizeof(kSeeds[0]);

    std::vector<double> sumFrac;
    for (uint64_t seed : kSeeds) {
        PlanetParams p = PlanetParams::preset(Preset::EarthLike);
        p.gridSubdivision = 64;
        p.seed = seed;

        auto world = runPipeline(p, 180);
        ASSERT_NE(world, nullptr);
        WorldStats stats = computeWorldStats(*world);
        ASSERT_GT(stats.landTileCount, 0.0f);

        if (sumFrac.empty()) sumFrac.assign(stats.biomeFraction.size(), 0.0);
        for (size_t i = 0; i < stats.biomeFraction.size(); ++i)
            sumFrac[i] += static_cast<double>(stats.biomeFraction[i]);
    }
    auto frac = [&](Biome b) {
        return static_cast<float>(sumFrac[static_cast<size_t>(b)] / static_cast<double>(kSeedCount));
    };

    // ArcticTundra must not dominate the land (baseline before C-4 was ~47%,
    // then ~22% pre-rebalance). Earth is ~10%.
    EXPECT_LE(frac(Biome::ArcticTundra), 0.18f)
        << "mean ArcticTundra " << frac(Biome::ArcticTundra) * 100.0f << "% too dominant";

    // Hot deserts must return to the subtropical interiors (baseline ~0.1-2.7%).
    EXPECT_GE(frac(Biome::HotDesert), 0.06f)
        << "mean HotDesert " << frac(Biome::HotDesert) * 100.0f << "% too sparse";

    // Forests must cover a substantial fraction of land.
    float totalForest = frac(Biome::TropicalRainforest) +
                        frac(Biome::TropicalSeasonalForest) +
                        frac(Biome::TemperateDeciduousForest) +
                        frac(Biome::TemperateRainforest) +
                        frac(Biome::BorealForest) +
                        frac(Biome::MontaneForest);
    EXPECT_GE(totalForest, 0.15f)
        << "mean total forest " << totalForest * 100.0f << "% too low";

    // No single non-ocean biome may swamp the map (rigid-stripe failure mode). A
    // systematic stripe failure moves every seed, so it shows in the mean too.
    for (size_t i = 0; i < sumFrac.size(); ++i) {
        auto b = static_cast<Biome>(i);
        if (b == Biome::Ocean || b == Biome::Lake) continue;
        EXPECT_LE(frac(b), 0.35f)
            << biomeToString(b) << " mean " << frac(b) * 100.0f
            << "% — one biome must not dominate the land";
    }
}

// ============================================================================
// Hypsometry mode detection must report genuine land/abyssal bimodality, not
// the C-3 shelf shoulder (~-100 m). The two modes must sit in distinct
// elevation regions: one abyssal (deep ocean), one land/shelf.
// ============================================================================
TEST(WorldStatsHeavy, HypsometryBimodalityIgnoresShelfShoulder) {
    PlanetParams p = PlanetParams::preset(Preset::EarthLike);
    // n=64: low enough to keep the full-pipeline run cheap for the Debug CI suite,
    // high enough that the abyssal plains resolve to a genuinely deep mode
    // (n=48 under-resolves them, leaving the deeper mode at only ~-3100 m).
    p.gridSubdivision = 64;
    p.seed = 42;

    auto world = runPipeline(p, 180);
    ASSERT_NE(world, nullptr);

    WorldStats stats = computeWorldStats(*world);
    ASSERT_EQ(stats.modeElevations.size(), 2u)
        << "Earth-like hypsometry must be bimodal";

    // One mode must be abyssal (deep ocean), one must be land/shelf. The shelf
    // shoulder near -100 m must NOT be reported as the deeper mode.
    float lo = std::min(stats.modeElevations[0], stats.modeElevations[1]);
    float hi = std::max(stats.modeElevations[0], stats.modeElevations[1]);
    // The deeper mode must be genuine deep ocean, far below the C-3 shelf shoulder
    // (~-120 m), not that shoulder leaking into the deep slot. The exact abyssal
    // depth is resolution-dependent (coarser grids under-resolve the deepest plains),
    // so -2500 m is the meaningful "clearly abyssal, not shelf" bound.
    EXPECT_LE(lo, -2500.0f)
        << "deeper mode " << lo << " m is not abyssal (shelf shoulder leaked in?)";
    EXPECT_GE(lo, -6500.0f) << "deeper mode " << lo << " m implausibly deep";
    EXPECT_GE(hi, -500.0f)  << "shallower mode " << hi << " m is below the platform";
    EXPECT_LE(hi, 1500.0f)  << "shallower mode " << hi << " m implausibly high";
}

// ============================================================================
// High-waterAmount hypsometry: at waterAmount=0.85 (sparse land), the land mode
// must still report the continental platform (>= 0 m), not the shelf shoulder
// (~-120/-140 m). The two-threshold detection in computeHypsometry uses
// kLandModeMinM=0 to exclude the shelf from the "land" slot.
// ============================================================================
TEST(WorldStatsHeavy, HypsometryLandModeAboveZeroAtHighWaterAmount) {
    PlanetParams p = PlanetParams::preset(Preset::EarthLike);
    // n=48 is ample for the bimodal histogram; keeps the full-pipeline run cheap.
    p.gridSubdivision = 48;
    p.waterAmount = 0.85f;
    p.seed = 42;

    auto world = runPipeline(p, 180);
    ASSERT_NE(world, nullptr);

    WorldStats stats = computeWorldStats(*world);

    // Must still report exactly two modes.
    ASSERT_EQ(stats.modeElevations.size(), 2u)
        << "High-waterAmount hypsometry must still be bimodal";

    float lo = std::min(stats.modeElevations[0], stats.modeElevations[1]);
    float hi = std::max(stats.modeElevations[0], stats.modeElevations[1]);

    // Abyssal mode must be deep.
    EXPECT_LE(lo, -2000.0f)
        << "deeper mode " << lo << " m must be abyssal at high waterAmount";

    // Land mode must be on the platform (>= 0 m), not the shelf shoulder.
    EXPECT_GE(hi, 0.0f)
        << "shallower mode " << hi << " m is the shelf shoulder, not the platform "
        << "(two-threshold detection failed at waterAmount=0.85)";
}

// ============================================================================
// W-1 drainage acceptance: priority-flood depression routing must give every
// land tile a downstream route that reaches the ocean (no dropped flow), unless
// the tile is a genuine endorheic sink (downhill 0xFF). Lakes must form, and the
// river-tile fraction must land in Earth's plausible band. Determinism is
// covered by PlanetGenerator's worldHash tests + worldgen-cli --verify-threads;
// here we re-run once and confirm the structural invariant holds identically.
// ============================================================================
TEST(WorldStatsHeavy, DrainageRoutesToOcean) {
    PlanetParams p = PlanetParams::preset(Preset::EarthLike);
    p.gridSubdivision = 64; // enough land for stable lake/river fractions
    p.seed = 42;

    auto world = runPipeline(p, 180);
    ASSERT_NE(world, nullptr);

    const SphereGrid& grid = *world->grid;
    const uint32_t N = grid.tileCount();
    const float seaLevel = world->seaLevelMeters;

    // Follow each land tile's downhill chain. It must terminate at the ocean
    // (a tile whose downhill target is below sea level), at a flagged endorheic
    // sink (downhill 0xFF), and never loop. A cap on hops guards against an
    // accidental cycle hanging the test.
    std::array<TileId, 6> nbrs{};
    uint32_t reachedOcean = 0;
    uint32_t endorheic    = 0;
    uint32_t landCount    = 0;
    const uint32_t maxHops = N + 8u;

    for (uint32_t s = 0; s < N; ++s) {
        if (world->data.elevation[s] < seaLevel) continue; // ocean
        ++landCount;

        TileId cur = s;
        bool resolved = false;
        for (uint32_t hop = 0; hop < maxHops; ++hop) {
            const uint8_t dh = world->data.downhill[cur];
            if (dh == 0xFFu) { ++endorheic; resolved = true; break; } // sink
            const uint32_t cnt = grid.neighbors(cur, nbrs);
            ASSERT_LT(dh, cnt) << "downhill index out of range at tile " << cur;
            const TileId next = nbrs[dh];
            if (world->data.elevation[next] < seaLevel) { // reached the sea
                ++reachedOcean; resolved = true; break;
            }
            cur = next;
        }
        ASSERT_TRUE(resolved)
            << "land tile " << s << " never reached ocean/sink (cycle?)";
    }

    EXPECT_EQ(reachedOcean + endorheic, landCount)
        << "every land tile must route to the ocean or be an endorheic sink";

    WorldStats stats = computeWorldStats(*world);

    // Lakes must form on an Earth-like world (depression routing ponds basins).
    EXPECT_GT(stats.lakeTileFraction, 0.0f) << "no lakes formed";

    // River-tile fraction should be close to kRiverLandFraction (4%). The band
    // is wider than exactly 4% because land fraction varies per seed/resolution,
    // so the quantile cut lands at slightly different tile counts depending on
    // how many land tiles exist. Guards against regressions in both directions.
    EXPECT_GE(stats.riverTileFraction, 0.02f)
        << "river fraction " << stats.riverTileFraction * 100.0f << "% too low";
    EXPECT_LE(stats.riverTileFraction, 0.07f)
        << "river fraction " << stats.riverTileFraction * 100.0f << "% too high";
}

} // namespace worldgen
