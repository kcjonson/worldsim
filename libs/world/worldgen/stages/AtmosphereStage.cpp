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

            // Wind direction: latitude-band Hadley/Ferrel/polar cell approximation.
            // 0=N, 64=E, 128=S, 192=W in 256-unit circle.
            // Tropics (|lat|<30): easterlies (trade winds) -> from east, blowing west = 192
            // Midlats (30-60): westerlies -> from west, blowing east = 64
            // Polar (>60): polar easterlies -> from east = 192
            uint8_t wdir;
            if (absLat < 30.0) {
                wdir = 192; // trade winds (easterly)
            } else if (absLat < 60.0) {
                wdir = 64;  // westerlies
            } else {
                wdir = 192; // polar easterlies
            }
            // Flip direction in southern hemisphere
            if (lat < 0.0) wdir = static_cast<uint8_t>((wdir + 128u) & 0xFFu);
            ctx.data.windDir[t]   = wdir;
            ctx.data.windSpeed[t] = 5; // stub: 5 m/s everywhere
        }
        ctx.reportProgress(static_cast<float>(end) / static_cast<float>(totalTiles));
    });

    ctx.world.validFields |= static_cast<uint32_t>(WorldField::TemperatureMean);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::TemperatureRange);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::WindDir);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::WindSpeed);
}

} // namespace worldgen
