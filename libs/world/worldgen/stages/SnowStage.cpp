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
//
// Sea ice: ocean tiles freeze below a colder air-temp threshold (kSeaIceThresholdC;
// seawater resists freezing and temperatureMean is air temp). Frozen ocean is solid
// ice, not snow (snow does not accumulate on water), so it records a thickness in
// iceThickness and is flagged kFlagSeaIce; snowCover stays land-only. The hard
// temperature threshold gives a hard ice/water edge. A cold-enough world trips the
// threshold at every latitude, so the whole ocean freezes; an Earth-like world
// freezes only a high-latitude cap.

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

constexpr float  kSeaIceThresholdC      = -9.0f;  // air-temp mean at which sea ice persists
constexpr float  kSeaIceFullThickDeltaC = 20.0f;  // full-thickness pack this far below threshold
constexpr float  kSeaIceMaxThicknessM   = 3.0f;   // perennial pack ice ~1-3 m
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
            // Recompute from scratch so the stage is idempotent: stale snow/ice
            // state never survives a warmer or re-flagged outcome.
            ctx.data.flags[t] &=
                static_cast<uint8_t>(~(kFlagPermanentSnow | kFlagSeaIce));
            ctx.data.snowCover[t] = 0;
            ctx.data.iceThickness[t] = 0;

            const float tempC =
                static_cast<float>(ctx.data.temperatureMean[t]) * 0.1f;

            if ((ctx.data.flags[t] & kFlagOcean) != 0) {
                // Sea ice: frozen ocean is solid ice, not snow (snow does not
                // accumulate on water). Record its thickness, thicker the colder;
                // the hard threshold gives a hard ice/water edge. snowCover stays 0.
                if (tempC < kSeaIceThresholdC) {
                    float coldness =
                        (kSeaIceThresholdC - tempC) / kSeaIceFullThickDeltaC;
                    if (coldness > 1.0f) coldness = 1.0f;
                    auto thick = static_cast<uint16_t>(
                        coldness * kSeaIceMaxThicknessM + 0.5f);
                    if (thick == 0) thick = 1;
                    ctx.data.iceThickness[t] = thick;
                    ctx.data.flags[t] |= kFlagSeaIce;
                }
                continue;
            }

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

    // SnowStage writes land snow (snowCover + kFlagPermanentSnow) and sea ice
    // (iceThickness + kFlagSeaIce), but GlacierStage runs after and is the last
    // writer of both flags and iceThickness, so it owns those valid bits.
    // SnowStage claims only SnowCover.
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::SnowCover);
}

} // namespace worldgen
