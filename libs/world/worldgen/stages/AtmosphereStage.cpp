// Atmosphere stage (pipeline phase P4): surface temperature, seasonal range,
// and prevailing winds. Writes temperatureMean, temperatureRange, windDir,
// windSpeed only.
//
// Temperature model:
//   T_global = T_equilibrium + greenhouse(atm)
//     equilibriumTemperatureK is the pre-greenhouse Stefan-Boltzmann value
//     (Earth: ~255 K / -18 C); greenhouse(atm) = 33 * sqrt(atm), capped +120 C.
//     Exponent 0.5 because absorption bands saturate as column mass grows, so
//     warming is sublinear in pressure; sqrt hits the anchors: +33 C at 1 atm
//     (Earth), ~+10 C at 0.1 atm (thin Mars-like), ~+104 C at 10 atm (Venus-ward).
//   T(lat) = T_global + A * (cos^2(lat) - 2/3)
//     The area-weighted sphere average of cos^2(lat) is exactly 2/3, so the
//     global mean is preserved by construction. A is the equator-pole contrast
//     (~44 C Earth-like), nudged up by fast rotation (weaker meridional heat
//     transport) and down by thick atmosphere (stronger transport).
//   Land above sea level loses lapseRateCPerKm per km; ocean and below-sea-level
//   depressions are clamped at 0 km.
//
// Continentality (land/sea contrast):
//   Distance-to-ocean (tile hops, shared with PrecipitationStage) drives two
//   corrections on land:
//     - mean temperature: a ZERO-SUM nudge contDelta = kContinentality *
//       (distNorm - meanLandDistNorm). The land sum of contDelta is exactly 0
//       (meanLandDistNorm is the land-area mean of distNorm), so the global mean
//       is preserved by construction — interiors warm, coasts cool, sum unchanged.
//     - seasonal range: interiors get a much larger half-amplitude than coasts at
//       the same latitude (Earth: maritime west-coast ~5 C, deep-continental
//       ~30 C+). This is the dominant continentality effect and it is NOT zero-sum
//       (range has no global-mean constraint).

#include "worldgen/stages/AtmosphereStage.h"

#include "worldgen/stages/ClimateField.h"

#include <math/DeterministicMath.h>
#include <random/HashNoise.h>

#include <vector>

namespace worldgen {

namespace {

constexpr size_t kGrainSize = 4096;
constexpr double kPiOver180 = 3.14159265358979323846 / 180.0;
constexpr double kPi        = 3.14159265358979323846;

// --- Continentality (land/sea contrast) ---
// distNorm in [0,1] = (distToOcean - meanLand) re-centered then scaled, see below.
// kContinentalityMeanC sets the half-spread of the ZERO-SUM mean nudge: a tile at
// the deepest interior (distNorm=1) ends up ~kContinentalityMeanC*(1-mean) warmer
// than the global land mean, a coast (distNorm=0) ~kContinentalityMeanC*mean
// cooler. Small (a few C) so the equator-pole structure still dominates and the
// EarthLikeGlobalMean gate holds. Earth's annual-mean continentality is modest;
// the seasonal swing below is where continentality really shows.
constexpr double kContinentalityMeanC = 8.0; // full distNorm span -> ~8 C warmer interior
// Seasonal-range continentality: interiors get a far larger half-amplitude than
// coasts. kContinentalityRangeC is the extra half-amplitude added to a deep
// interior (distNorm=1) over a coast (distNorm=0) at the same latitude. Earth:
// maritime ~5 C half-amp, deep-continental ~30 C+, so ~20 C of spread.
constexpr double kContinentalityRangeC = 20.0;
// distToOcean saturates: beyond this many hops a tile is "fully interior" for the
// purpose of normalization, so a single huge supercontinent doesn't compress every
// other land mass to distNorm~0. ~14 hops at n=512 is roughly a 2500-3000 km
// traverse (the scale at which interiors fully dry / decouple from the sea).
constexpr double kInteriorSaturationHops = 14.0;

double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

} // namespace

void AtmosphereStage::run(StageContext& ctx) {
    const uint32_t totalTiles = ctx.grid.tileCount();
    const auto seed32 = static_cast<uint32_t>(ctx.stageSeed ^ (ctx.stageSeed >> 32));

    const double atm      = ctx.params.atmosphereStrength;
    const double rot      = ctx.params.rotationRate;
    // Clamp to the documented model range; the UI clamps too, but direct
    // callers may not
    const double ecc      = clampd(ctx.params.eccentricity, 0.0, 0.95);
    const double sqrtAtm  = foundation::det_math::sqrt(atm < 0.0 ? 0.0 : atm);
    const double sqrtRot  = foundation::det_math::sqrt(rot < 0.0 ? 0.0 : rot);
    const double seaLevel = static_cast<double>(ctx.world.seaLevelMeters);
    const double lapseRate = ctx.derived.lapseRateCPerKm;

    // Global mean surface temperature = equilibrium + greenhouse.
    const double equilTempC  = ctx.derived.equilibriumTemperatureK - 273.15;
    const double greenhouseC = clampd(33.0 * sqrtAtm, 0.0, 120.0);
    const double globalMeanC = equilTempC + greenhouseC;

    // Equator-pole contrast A. Faster rotation (rot < 1) raises it, thicker
    // atmosphere lowers it; both modest and clamped.
    const double rotContrast = clampd(1.0 + 0.25 * (1.0 - sqrtRot), 0.8, 1.2);
    const double atmContrast = clampd(1.0 + 0.15 * (1.0 - sqrtAtm), 0.75, 1.25);
    // Base 44 (was 50): a 50 C contrast pushed the -5 C arctic isotherm to ~57
    // deg, drowning the mid-latitudes in tundra. 44 warms 50-70 deg into the
    // taiga/subtropical-dry range while keeping the equator-pole spread inside
    // the [40,65] EquatorHotterThanPoles gate.
    const double contrastA   = clampd(44.0 * rotContrast * atmContrast, 25.0, 75.0);

    // Seasonal range modifiers: eccentricity widens it (quadratic, +15 C at
    // e=0.95), thick atmosphere damps it.
    const double eccBoost = 15.0 * (ecc * ecc) / (0.95 * 0.95);
    const double atmRangeDamp = clampd(1.0 - 0.2 * (sqrtAtm - 1.0), 0.5, 1.2);

    // Circulation cell boundaries: faster rotation -> narrower cells.
    const double hadleyEdge = clampd(30.0 + 5.0 * (sqrtRot - 1.0), 25.0, 35.0);
    const double ferrelEdge = hadleyEdge + 30.0;

    // Wind speed scaling: slower rotation sustains stronger winds; thicker
    // atmosphere carries more momentum.
    const double rotWindFactor = clampd(1.0 + 0.15 * (sqrtRot - 1.0), 0.85, 1.5);
    const double atmWindFactor = clampd(0.8 + 0.2 * sqrtAtm, 0.7, 1.5);
    const double windScale = rotWindFactor * atmWindFactor;

    // --- Continentality pass 1: distance-to-ocean + land-mean of distNorm ---
    // distNorm[t] = min(distToOcean / saturation, 1) on land, 0 on ocean. The
    // mean is reduced SERIALLY over land tiles in ascending order — a parallel
    // float reduce would not be bit-reproducible. The mean drives the zero-sum
    // mean-temperature nudge below.
    const std::vector<float> distToOcean =
        computeDistanceToOcean(ctx.grid, ctx.data.elevation, seaLevel);
    std::vector<float> distNorm(totalTiles, 0.0f);
    double landDistSum = 0.0;
    uint32_t landCount = 0;
    for (uint32_t t = 0; t < totalTiles; ++t) {
        if (static_cast<double>(ctx.data.elevation[t]) < seaLevel) continue; // ocean
        double dn = static_cast<double>(distToOcean[t]) / kInteriorSaturationHops;
        if (dn > 1.0) dn = 1.0;
        distNorm[t] = static_cast<float>(dn);
        landDistSum += dn;
        ++landCount;
    }
    const double meanLandDistNorm = landCount > 0
        ? landDistSum / static_cast<double>(landCount) : 0.0;

    ctx.pool.parallelFor(0, totalTiles, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        for (size_t t = begin; t < end; ++t) {
            Vec3d center = ctx.grid.tileCenter(static_cast<uint32_t>(t));
            // Unit vector: z = sin(lat), so cos^2(lat) = 1 - z^2 with no trig.
            const double sin2Lat = center.z * center.z;
            const double cos2Lat = 1.0 - sin2Lat;
            const double lat = foundation::det_math::asin(
                clampd(center.z, -1.0, 1.0)) / kPiOver180;
            const double absLat = lat < 0.0 ? -lat : lat;

            const double elev = static_cast<double>(ctx.data.elevation[t]);
            // kFlagOcean is set later by OceanStage; detect ocean by elevation.
            const bool isOcean = elev < seaLevel;

            // --- Mean temperature ---
            double tempC = globalMeanC + contrastA * (cos2Lat - 2.0 / 3.0);

            // Lapse rate on land above sea level only; depressions clamp at 0 km.
            if (!isOcean && elev > seaLevel) {
                tempC -= lapseRate * (elev - seaLevel) / 1000.0;
            }

            // Continentality mean nudge (land only, zero-sum). distNorm is 0 on
            // ocean, so this is a no-op there; over land the (distNorm - mean)
            // factor sums to exactly 0, preserving the global mean.
            if (!isOcean) {
                tempC += kContinentalityMeanC *
                         (static_cast<double>(distNorm[t]) - meanLandDistNorm);
            }

            // Small seeded perturbation (+/-1.5 C) for texture.
            const float noise = foundation::valueNoise3(
                static_cast<float>(center.x) * 4.0f,
                static_cast<float>(center.y) * 4.0f,
                static_cast<float>(center.z) * 4.0f,
                seed32);
            tempC += (static_cast<double>(noise) - 0.5) * 3.0;

            ctx.data.temperatureMean[t] =
                static_cast<int16_t>(clampd(tempC, -327.0, 327.0) * 10.0);

            // --- Seasonal range (half-amplitude) ---
            double range = (4.0 + 30.0 * sin2Lat + eccBoost) * atmRangeDamp;
            if (isOcean) {
                range *= 0.5; // maritime moderation
            } else {
                // Continentality: interiors swing far more than coasts at the
                // same latitude. NOT zero-sum (range has no global-mean budget).
                range += kContinentalityRangeC * static_cast<double>(distNorm[t]);
            }
            ctx.data.temperatureRange[t] =
                static_cast<int16_t>(clampd(range, 1.0, 80.0) * 10.0);

            // --- Wind direction ---
            // Three-cell structure with ~30 degree meridional tilt.
            // Encoding: 0=N, 64=E, 128=S, 192=W (256 units = 360 degrees).
            // kMeridionalUnits = round(30/360*256) = 21.
            // Trades: blow toward west + toward equator (equatorward in
            //   each hemisphere). Westerlies: east + poleward. Polar
            //   easterlies: west + equatorward (same sense as trades).
            // All arithmetic is integer and wraps mod 256 — deterministic.
            static constexpr uint32_t kMeridionalUnits = 21u;
            uint32_t wdir;
            if (absLat < hadleyEdge) {
                // Trades: zonal base west (192). NH tilts toward S (+64 units
                // moves from W toward S in the 0=N,64=E,128=S,192=W encoding);
                // SH tilts toward N (-64 units, i.e. +192).
                wdir = lat >= 0.0
                    ? (192u + kMeridionalUnits)   // NH: WNW->SW direction
                    : (192u - kMeridionalUnits + 256u); // SH: WSW->NW direction
            } else if (absLat < ferrelEdge) {
                // Westerlies: zonal base east (64). NH tilts toward N (-64
                // moves toward N); SH tilts toward S (+64).
                wdir = lat >= 0.0
                    ? (64u - kMeridionalUnits + 256u) // NH: ENE direction
                    : (64u + kMeridionalUnits);        // SH: ESE direction
            } else {
                // Polar easterlies: same sense as trades (equatorward tilt).
                wdir = lat >= 0.0
                    ? (192u + kMeridionalUnits)
                    : (192u - kMeridionalUnits + 256u);
            }
            ctx.data.windDir[t] = static_cast<uint8_t>(wdir & 0xFFu);

            // --- Wind speed ---
            // Sinusoidal bump per cell: calm (~4 m/s) at the doldrums and cell
            // boundaries, peaks mid-cell (trades ~8, westerlies ~12, polar ~6.5).
            double speed;
            if (absLat < hadleyEdge) {
                speed = 4.0 + 4.0 * foundation::det_math::sin(
                    kPi * absLat / hadleyEdge);
            } else if (absLat < ferrelEdge) {
                speed = 4.0 + 8.0 * foundation::det_math::sin(
                    kPi * (absLat - hadleyEdge) / (ferrelEdge - hadleyEdge));
            } else {
                speed = 4.0 + 2.5 * foundation::det_math::sin(
                    kPi * (absLat - ferrelEdge) / (90.0 - ferrelEdge));
            }
            speed = clampd(speed * windScale, 1.0, 60.0);
            ctx.data.windSpeed[t] = static_cast<uint8_t>(speed + 0.5);
        }
        ctx.reportProgress(static_cast<float>(end) / static_cast<float>(totalTiles));
    });

    ctx.world.validFields |= static_cast<uint32_t>(WorldField::TemperatureMean);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::TemperatureRange);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::WindDir);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::WindSpeed);
}

} // namespace worldgen
