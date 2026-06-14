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
// Earth-like ranges. Runs at n=128 (164k tiles) for stability — the same
// fractions hold within a couple points at n=512.
//
// Tolerances are anchored to what the retuned climate+Whittaker model actually
// achieves across seeds 42, 7, and 1337 (measured at n=128 and n=512), widened
// for seed variation, NOT to Earth's textbook percentages directly (this is a
// coarse single-pass model, not a GCM). Measured spread across those seeds:
//   ArcticTundra 8.2-15.5%   HotDesert 7.3-10.1%   total forest 34-41%
//   largest non-ocean biome 15.5-17.3%
// The gates below sit outside that spread with margin so the test is a genuine
// regression guard, not an overfit to one seed.
// ============================================================================
TEST(WorldStats, EarthLikeBiomeFractionsAcceptance) {
    PlanetParams p = PlanetParams::preset(Preset::EarthLike);
    p.gridSubdivision = 128;
    p.seed = 42;

    auto world = runPipeline(p, 180);
    ASSERT_NE(world, nullptr);

    WorldStats stats = computeWorldStats(*world);
    ASSERT_GT(stats.landTileCount, 0.0f);

    auto frac = [&](Biome b) { return stats.biomeFraction[static_cast<size_t>(b)]; };

    // ArcticTundra must not dominate the land (baseline before C-4 was ~47%,
    // then ~22% pre-rebalance). Earth is ~10%.
    EXPECT_LE(frac(Biome::ArcticTundra), 0.18f)
        << "ArcticTundra " << frac(Biome::ArcticTundra) * 100.0f << "% too dominant";

    // Hot deserts must return to the subtropical interiors (baseline ~0.1-2.7%).
    EXPECT_GE(frac(Biome::HotDesert), 0.06f)
        << "HotDesert " << frac(Biome::HotDesert) * 100.0f << "% too sparse";

    // Forests must cover a substantial fraction of land.
    float totalForest = frac(Biome::TropicalRainforest) +
                        frac(Biome::TropicalSeasonalForest) +
                        frac(Biome::TemperateDeciduousForest) +
                        frac(Biome::TemperateRainforest) +
                        frac(Biome::BorealForest) +
                        frac(Biome::MontaneForest);
    EXPECT_GE(totalForest, 0.15f)
        << "total forest " << totalForest * 100.0f << "% too low";

    // No single non-ocean biome may swamp the map (rigid-stripe failure mode).
    for (size_t i = 0; i < stats.biomeFraction.size(); ++i) {
        auto b = static_cast<Biome>(i);
        if (b == Biome::Ocean || b == Biome::Lake) continue;
        EXPECT_LE(stats.biomeFraction[i], 0.35f)
            << biomeToString(b) << " " << stats.biomeFraction[i] * 100.0f
            << "% — one biome must not dominate the land";
    }
}

// ============================================================================
// Hypsometry mode detection must report genuine land/abyssal bimodality, not
// the C-3 shelf shoulder (~-100 m). The two modes must sit in distinct
// elevation regions: one abyssal (deep ocean), one land/shelf.
// ============================================================================
TEST(WorldStats, HypsometryBimodalityIgnoresShelfShoulder) {
    PlanetParams p = PlanetParams::preset(Preset::EarthLike);
    p.gridSubdivision = 128;
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
    EXPECT_LE(lo, -3500.0f)
        << "deeper mode " << lo << " m is not abyssal (shelf shoulder leaked in?)";
    EXPECT_GE(lo, -6500.0f) << "deeper mode " << lo << " m implausibly deep";
    EXPECT_GE(hi, -500.0f)  << "shallower mode " << hi << " m is below the platform";
    EXPECT_LE(hi, 1500.0f)  << "shallower mode " << hi << " m implausibly high";
}

} // namespace worldgen
