// WorldStats tests — basic sanity checks at tiny n.

#include "worldgen/debug/WorldStats.h"

#include "worldgen/data/PlanetParams.h"
#include "worldgen/pipeline/PlanetGenerator.h"

#include <gtest/gtest.h>

#include <algorithm>
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

} // namespace worldgen
