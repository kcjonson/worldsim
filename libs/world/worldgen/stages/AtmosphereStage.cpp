// STUB stage: replaced in M3c — produces placeholder data so downstream tracks can build.

#include "worldgen/stages/AtmosphereStage.h"

#include <math/DeterministicMath.h>

namespace worldgen {

namespace {
constexpr size_t kGrainSize = 4096;
constexpr double kPiOver180 = 3.14159265358979323846 / 180.0;
} // namespace

void AtmosphereStage::run(StageContext& ctx) {
    const uint32_t totalTiles = ctx.grid.tileCount();

    // Temperature by latitude + lapse rate from elevation.
    // equilibriumTemperatureK gives the global baseline; lapse rate modifies by elevation.
    const double equilTempC = ctx.derived.equilibriumTemperatureK - 273.15;
    const double lapseRate  = ctx.derived.lapseRateCPerKm; // C per 1000m
    const double seaLevel   = static_cast<double>(ctx.world.seaLevelMeters);

    ctx.pool.parallelFor(0, totalTiles, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        for (size_t t = begin; t < end; ++t) {
            Vec3d center = ctx.grid.tileCenter(static_cast<uint32_t>(t));
            // Latitude from z component (asin)
            double lat = foundation::det_math::asin(center.z) / kPiOver180;

            // Base temperature: equatorial hot, polar cold
            // Approximation: T = equilTemp + 30*cos(lat) - 30
            double cosLat = foundation::det_math::cos(lat * kPiOver180);
            double baseT = equilTempC + 30.0 * cosLat - 30.0;

            // Lapse rate adjustment for land above sea level
            double elev = static_cast<double>(ctx.data.elevation[t]);
            double effectiveElev = elev > seaLevel ? (elev - seaLevel) : 0.0;
            double lapseAdj = -(effectiveElev / 1000.0) * lapseRate;

            double tempC = baseT + lapseAdj;

            // Store as int16 in 0.1 C units
            double clamped = tempC < -327.0 ? -327.0 : (tempC > 327.0 ? 327.0 : tempC);
            ctx.data.temperatureMean[t] = static_cast<int16_t>(clamped * 10.0);

            // Seasonal range: bigger at higher latitudes, smaller near equator
            double absLat = lat < 0.0 ? -lat : lat;
            double range = 5.0 + absLat * 0.3;
            ctx.data.temperatureRange[t] = static_cast<int16_t>(range * 10.0);
        }
        ctx.reportProgress(static_cast<float>(end) / static_cast<float>(totalTiles));
    });

    ctx.world.validFields |= static_cast<uint32_t>(WorldField::TemperatureMean);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::TemperatureRange);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::WindDir);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::WindSpeed);
}

} // namespace worldgen
