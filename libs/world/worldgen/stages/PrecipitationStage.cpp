// STUB stage: replaced in M3d — produces placeholder data so downstream tracks can build.

#include "worldgen/stages/PrecipitationStage.h"

#include <math/DeterministicMath.h>
#include <random/HashNoise.h>

namespace worldgen {

namespace {
constexpr size_t kGrainSize = 4096;
constexpr double kPiOver180 = 3.14159265358979323846 / 180.0;
} // namespace

void PrecipitationStage::run(StageContext& ctx) {
    const uint32_t totalTiles = ctx.grid.tileCount();
    const auto seed32 = static_cast<uint32_t>(ctx.stageSeed ^ (ctx.stageSeed >> 32));

    ctx.pool.parallelFor(0, totalTiles, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        for (size_t t = begin; t < end; ++t) {
            Vec3d center = ctx.grid.tileCenter(static_cast<uint32_t>(t));
            double lat = foundation::det_math::asin(center.z) / kPiOver180;
            double absLat = lat < 0.0 ? -lat : lat;

            // Latitude band precipitation (simplified Hadley cell pattern):
            //   equatorial (0-15 deg): wet 2000mm
            //   subtropical (15-30 deg): dry 200mm
            //   midlatitude (30-60 deg): moderate 750mm
            //   polar (60-90 deg): dry 300mm
            double basePrecip = 0.0;
            if (absLat < 15.0) {
                basePrecip = 2000.0 - absLat * 20.0;
            } else if (absLat < 30.0) {
                basePrecip = 2000.0 - absLat * 60.0 + 200.0;
            } else if (absLat < 60.0) {
                basePrecip = 200.0 + (absLat - 30.0) * 18.3;
            } else {
                basePrecip = 750.0 - (absLat - 60.0) * 7.5;
            }

            // Noise variation
            float noise = foundation::valueNoise3(
                static_cast<float>(center.x) * 3.0f,
                static_cast<float>(center.y) * 3.0f,
                static_cast<float>(center.z) * 3.0f,
                seed32);
            basePrecip = basePrecip * (0.6 + 0.8 * static_cast<double>(noise));

            // Scale by waterAmount (more ocean = more precipitation globally)
            basePrecip *= (0.5 + ctx.params.waterAmount);

            if (basePrecip < 0.0) basePrecip = 0.0;
            if (basePrecip > 65535.0) basePrecip = 65535.0;
            ctx.data.precipitation[t] = static_cast<uint16_t>(basePrecip);
        }
        ctx.reportProgress(static_cast<float>(end) / static_cast<float>(totalTiles));
    });

    ctx.world.validFields |= static_cast<uint32_t>(WorldField::Precipitation);
}

} // namespace worldgen
