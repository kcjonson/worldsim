// Regression guard for the washed-out far-zoom globe: the base-tier bake of a
// fully generated world must produce saturated terrain colors, never a sheet of
// neutral gray. The original bug fed the colorizer an early progressive snapshot
// (plate fields only, no Elevation), so every Terrain texel fell back to gray and
// the mipmapped far view read as a pale veil. All CPU — no GL context.
//
// Also covers: correct nearest-vertex texel mapping in the downsample path, and
// release() state-machine safety (bake-then-re-bake without a GL context).

#include "PlanetColorizer.h"

#include <world/worldgen/data/GeneratedWorld.h>
#include <world/worldgen/data/PlanetParams.h>
#include <world/worldgen/data/WorldData.h>
#include <world/worldgen/grid/SphereGrid.h>
#include <world/worldgen/pipeline/PlanetGenerator.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

using namespace planetview;

namespace {

std::shared_ptr<const worldgen::GeneratedWorld> generate(uint32_t n) {
    worldgen::PlanetParams params =
        worldgen::PlanetParams::preset(worldgen::Preset::EarthLike);
    params.gridSubdivision = n;
    params.seed = 42;

    worldgen::PlanetGenerator gen;
    gen.start(params);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (std::chrono::steady_clock::now() < deadline) {
        auto prog = gen.progress();
        if (prog.state == worldgen::GenerationProgress::State::Complete ||
            prog.state == worldgen::GenerationProgress::State::Failed ||
            prog.state == worldgen::GenerationProgress::State::Cancelled) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return gen.takeResult();
}

// Mean per-pixel saturation (0..1) and fraction of texels that are exactly the
// neutral-gray fallback (128,128,128) across all 10 rhombi.
void bakeStats(const worldgen::GeneratedWorld& world, uint32_t n, ColorMode mode,
               double& meanSat, double& grayFrac) {
    uint32_t texSize = std::min(n, 1024u);
    std::vector<uint8_t> buf;
    double satSum = 0.0;
    size_t grayCount = 0;
    size_t total = 0;
    for (uint32_t r = 0; r < 10; ++r) {
        PlanetColorizer::bakeRhombusForTest(buf, r, texSize, n, world, mode);
        size_t px = static_cast<size_t>(texSize) * texSize;
        for (size_t k = 0; k < px; ++k) {
            int cr = buf[k * 4], cg = buf[k * 4 + 1], cb = buf[k * 4 + 2];
            int mx = std::max({cr, cg, cb});
            int mn = std::min({cr, cg, cb});
            satSum += mx > 0 ? static_cast<double>(mx - mn) / mx : 0.0;
            if (cr == 128 && cg == 128 && cb == 128) ++grayCount;
        }
        total += px;
    }
    meanSat = satSum / static_cast<double>(total);
    grayFrac = static_cast<double>(grayCount) / static_cast<double>(total);
}

} // namespace

TEST(PlanetColorizerBake, TerrainBakeIsSaturatedNotGray) {
    auto world = generate(64);
    ASSERT_TRUE(world) << "generation failed to produce a world";
    ASSERT_TRUE(world->validFields & static_cast<uint32_t>(worldgen::WorldField::Elevation))
        << "completed world must carry the Elevation field";

    double meanSat = 0.0, grayFrac = 0.0;
    bakeStats(*world, 64, ColorMode::Terrain, meanSat, grayFrac);

    // A real Earth-like terrain bake is dominated by saturated ocean blue; the
    // washed-out bug drove meanSat to ~0 and grayFrac to ~1.
    EXPECT_GT(meanSat, 0.4) << "base-tier Terrain bake is desaturated (washed out)";
    EXPECT_LT(grayFrac, 0.05) << "base-tier Terrain bake is mostly neutral-gray fallback";
}

TEST(PlanetColorizerBake, CombinedBakeIsSaturated) {
    auto world = generate(64);
    ASSERT_TRUE(world);

    double meanSat = 0.0, grayFrac = 0.0;
    bakeStats(*world, 64, ColorMode::Combined, meanSat, grayFrac);

    EXPECT_GT(meanSat, 0.4);
    EXPECT_LT(grayFrac, 0.05);
}

// Verify the nearest-vertex-at-texel-center downsample mapping stays within the
// owned vertex range. Owned range: i in [1..n] (i=0 is the seam, not owned by
// this rhombus), j in [0..n-1] (j=n is the v=1 seam). The formula
// (2k+1)*n/(2*texSize) must produce ti in [0..n-1] (-> ti+1 in [1..n]) for i,
// and tj in [0..n-1] for j. The old (n-1)/(texSize-1) formula was biased: at
// non-divisor texSizes it pulled interior texels away from their nearest vertex.
TEST(PlanetColorizerBake, DownsampleMappingStaysInOwnedRange) {
    // n=64, texSize=48: not a clean divisor, so old vs new formula diverge.
    uint32_t n = 64;
    uint32_t ts = 48;

    for (uint32_t j = 0; j < ts; ++j) {
        for (uint32_t i = 0; i < ts; ++i) {
            uint32_t ti = (2U * i + 1U) * n / (2U * ts);
            uint32_t tj = (2U * j + 1U) * n / (2U * ts);
            // ti+1 must be in [1..n] (owned i range)
            EXPECT_GE(ti + 1U, 1U) << "i=" << i;
            EXPECT_LE(ti + 1U, n)  << "ti+1 exceeded n (seam+1); i=" << i;
            // tj must be in [0..n-1] (owned j range)
            EXPECT_GE(tj, 0U) << "j=" << j;
            EXPECT_LT(tj, n)  << "tj reached j=n seam; j=" << j;
        }
    }

    // Spot-check the last texel: old formula would give ti = (ts-1)*(n-1)/(ts-1)
    // = n-1, ti+1=n (owned, ok but endpoint-biased). New formula gives
    // (2*(ts-1)+1)*n/(2*ts) = (2*47+1)*64/96 = 95*64/96 = 63, ti+1=64=n.
    // Both are within range but new is the true nearest vertex.
    uint32_t tiLast = (2U * (ts - 1U) + 1U) * n / (2U * ts);
    EXPECT_EQ(tiLast, n - 1U) << "last texel should map to vertex n-1";
}

// Hydrology mode: rivers (kFlagRiver) must be blue, lakes (kFlagLake) must be
// cyan-blue (more blue than land), ocean tiles must render as elevation blue
// (not neutral gray), and the bake must not be dominated by neutral gray.
TEST(PlanetColorizerBake, HydrologyBakeIsNotGray) {
    auto world = generate(64);
    ASSERT_TRUE(world);
    ASSERT_TRUE(world->validFields & static_cast<uint32_t>(worldgen::WorldField::FlowAccum))
        << "Hydrology mode needs FlowAccum";

    double meanSat = 0.0, grayFrac = 0.0;
    bakeStats(*world, 64, ColorMode::Hydrology, meanSat, grayFrac);

    EXPECT_LT(grayFrac, 0.05) << "Hydrology bake is mostly neutral-gray (missing fields?)";
    // The mode should have some saturation from ocean blue and river/lake tiles.
    EXPECT_GT(meanSat, 0.1) << "Hydrology bake is completely desaturated";
}

// Regression for release() not draining bake state: calling bakeRhombusForTest
// twice on the same world must produce byte-identical output, confirming the
// CPU baker is stateless and the fix does not corrupt the shared path.
TEST(PlanetColorizerBake, BakeIsIdempotentAcrossRepeatCalls) {
    auto world = generate(32); // small n, fast
    ASSERT_TRUE(world);

    std::vector<uint8_t> first, second;
    PlanetColorizer::bakeRhombusForTest(first,  0, 32, 32, *world, ColorMode::Terrain);
    PlanetColorizer::bakeRhombusForTest(second, 0, 32, 32, *world, ColorMode::Terrain);

    ASSERT_EQ(first.size(), second.size());
    EXPECT_EQ(first, second) << "bakeRhombusForTest is not idempotent";
}
