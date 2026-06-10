// STUB stage: replaced in M3b — produces placeholder data so downstream tracks can build.

#include "worldgen/stages/TerrainStage.h"

#include <random/HashNoise.h>

#include <algorithm>
#include <vector>
#include <cmath>

namespace worldgen {

namespace {
constexpr size_t kGrainSize = 4096;
} // namespace

void TerrainStage::run(StageContext& ctx) {
    const uint32_t totalTiles = ctx.grid.tileCount();
    const auto seed32 = static_cast<uint32_t>(ctx.stageSeed ^ (ctx.stageSeed >> 32));

    // Generate fractal elevation using fractalNoise3 over tile center
    ctx.pool.parallelFor(0, totalTiles, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        for (size_t t = begin; t < end; ++t) {
            Vec3d center = ctx.grid.tileCenter(static_cast<uint32_t>(t));
            // Scale position to noise space; low-frequency for continent-scale features
            float fx = static_cast<float>(center.x) * 2.5f;
            float fy = static_cast<float>(center.y) * 2.5f;
            float fz = static_cast<float>(center.z) * 2.5f;
            float n = foundation::fractalNoise3(fx, fy, fz, seed32, 6, 2.0f, 0.5f);
            // Map [-1,1] -> [-5000m, 5000m]
            ctx.data.elevation[t] = n * 5000.0f;
        }
        ctx.reportProgress(static_cast<float>(end) / static_cast<float>(totalTiles) * 0.8f);
    });

    throwIfCancelled(ctx);

    // Compute sea level via elevation-histogram quantile at waterAmount.
    // This is the real quantile implementation (reused in M3b).
    {
        constexpr int kBins = 1024;
        constexpr float kMinElev = -6000.0f;
        constexpr float kMaxElev =  6000.0f;
        constexpr float kBinWidth = (kMaxElev - kMinElev) / kBins;

        std::vector<uint32_t> hist(kBins, 0);
        for (uint32_t t = 0; t < totalTiles; ++t) {
            float e = ctx.data.elevation[t];
            int bin = static_cast<int>((e - kMinElev) / kBinWidth);
            if (bin < 0) bin = 0;
            if (bin >= kBins) bin = kBins - 1;
            hist[static_cast<size_t>(bin)]++;
        }

        // Find bin whose cumulative count reaches (1 - waterAmount) of total
        double target = (1.0 - ctx.params.waterAmount) * static_cast<double>(totalTiles);
        double cumul = 0.0;
        int seaBin = 0;
        for (int b = 0; b < kBins; ++b) {
            cumul += hist[static_cast<size_t>(b)];
            if (cumul >= target) { seaBin = b; break; }
        }
        ctx.world.seaLevelMeters = kMinElev + (seaBin + 0.5f) * kBinWidth;
    }

    ctx.world.validFields |= static_cast<uint32_t>(WorldField::Elevation);
    ctx.reportProgress(1.0f);
}

} // namespace worldgen
