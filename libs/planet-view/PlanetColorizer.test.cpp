// Regression guard for the washed-out far-zoom globe: the base-tier bake of a
// fully generated world must produce saturated terrain colors, never a sheet of
// neutral gray. The original bug fed the colorizer an early progressive snapshot
// (plate fields only, no Elevation), so every Terrain texel fell back to gray and
// the mipmapped far view read as a pale veil. All CPU — no GL context.

#include "PlanetColorizer.h"

#include <world/worldgen/data/GeneratedWorld.h>
#include <world/worldgen/data/PlanetParams.h>
#include <world/worldgen/data/WorldData.h>
#include <world/worldgen/pipeline/PlanetGenerator.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
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
