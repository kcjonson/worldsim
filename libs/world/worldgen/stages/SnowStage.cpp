// SnowStage — M3e implementation (pipeline phase P8): permanent snow from
// effective temperature (lapse rate already applied by AtmosphereStage, so
// summits read cold here without any elevation term).
//
// Permanent-snow threshold:
//   thresholdC = -10 - 3 * (sqrt(atmosphereStrength) - 1), clamped [-16, -6]
// Thin atmospheres refreeze easily (threshold warms toward -6 C); thick
// greenhouse atmospheres hold heat through winter, so snowpack needs a colder
// mean to persist (toward -16 C). Earth (1 atm) lands at -10 C.
//
// Coverage ramps linearly below the threshold, saturating 50 C under it.
// Ocean tiles keep snowCover = 0 (sea ice is future work).

#include "worldgen/stages/SnowStage.h"

#include <math/DeterministicMath.h>

namespace worldgen {

namespace {
constexpr size_t kGrainSize = 4096;
constexpr double kBaseThresholdC  = -10.0;
constexpr double kAtmThresholdC   = 3.0;   // per unit of (sqrt(atm) - 1)
constexpr double kThresholdMinC   = -16.0;
constexpr double kThresholdMaxC   = -6.0;
constexpr float  kFullCoverDeltaC = 50.0f; // full pack this far below threshold
} // namespace

void SnowStage::run(StageContext& ctx) {
    const uint32_t totalTiles = ctx.grid.tileCount();

    const double atm = ctx.params.atmosphereStrength;
    const double sqrtAtm = foundation::det_math::sqrt(atm < 0.0 ? 0.0 : atm);
    double thresholdC = kBaseThresholdC - kAtmThresholdC * (sqrtAtm - 1.0);
    if (thresholdC < kThresholdMinC) thresholdC = kThresholdMinC;
    if (thresholdC > kThresholdMaxC) thresholdC = kThresholdMaxC;
    const float threshold = static_cast<float>(thresholdC);

    ctx.pool.parallelFor(0, totalTiles, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        for (size_t t = begin; t < end; ++t) {
            // Recompute from scratch so the stage is idempotent: stale snow
            // state never survives a warmer or ocean outcome
            ctx.data.flags[t] &= static_cast<uint8_t>(~kFlagPermanentSnow);
            ctx.data.snowCover[t] = 0;

            if ((ctx.data.flags[t] & kFlagOcean) != 0) continue;

            const float tempC =
                static_cast<float>(ctx.data.temperatureMean[t]) * 0.1f;
            if (tempC < threshold) {
                float coldness = (threshold - tempC) / kFullCoverDeltaC;
                if (coldness > 1.0f) coldness = 1.0f;
                // Round up so any tile below threshold has nonzero cover;
                // the flag and the coverage can never disagree
                auto cover = static_cast<uint8_t>(coldness * 255.0f);
                if (cover == 0) cover = 1;
                ctx.data.snowCover[t] = cover;
                ctx.data.flags[t] |= kFlagPermanentSnow;
            }
        }
        ctx.reportProgress(static_cast<float>(end) /
                           static_cast<float>(totalTiles));
    });

    // SnowStage is the last writer of flags (adds kFlagPermanentSnow after
    // OceanStage's kFlagOcean). It owns the Flags valid bit; no stage may
    // write flags afterward.
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::Flags);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::SnowCover);
}

} // namespace worldgen
