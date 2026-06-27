#include "worldgen/data/PlanetParams.h"

#include <math/DeterministicMath.h>

#include <string>

namespace worldgen {

std::optional<std::string> PlanetParams::validate() const {
    if (gridSubdivision < kMinGridSubdivision || gridSubdivision > kMaxGridSubdivision)
        return "gridSubdivision " + std::to_string(gridSubdivision) + " out of range ["
             + std::to_string(kMinGridSubdivision) + ", "
             + std::to_string(kMaxGridSubdivision) + "]";
    if (tectonicPlateCount < 2 || tectonicPlateCount > 30)
        return "tectonicPlateCount " + std::to_string(tectonicPlateCount)
             + " out of range [2, 30]";
    if (waterAmount < 0.0 || waterAmount > 1.0)
        return "waterAmount " + std::to_string(waterAmount) + " out of range [0, 1]";
    if (eccentricity < 0.0 || eccentricity > 0.95)
        return "eccentricity " + std::to_string(eccentricity) + " out of range [0, 0.95]";
    if (starMass <= 0.0)        return "starMass must be > 0";
    if (starRadius <= 0.0)      return "starRadius must be > 0";
    if (starTemperature <= 0.0) return "starTemperature must be > 0";
    if (planetRadius <= 0.0)    return "planetRadius must be > 0";
    if (planetMass <= 0.0)      return "planetMass must be > 0";
    if (rotationRate <= 0.0)    return "rotationRate must be > 0";
    if (semiMajorAxis <= 0.0)   return "semiMajorAxis must be > 0";
    if (atmosphereStrength < 0.0) return "atmosphereStrength must be >= 0";
    return std::nullopt;
}

PlanetParams PlanetParams::preset(Preset p) {
    PlanetParams params;
    switch (p) {
        case Preset::EarthLike:
            // Default values already Earth-like
            break;

        case Preset::DesertWorld:
            params.planetRadius = 0.7;
            params.planetMass   = 0.5;
            params.semiMajorAxis = 1.2;
            params.waterAmount  = 0.15;
            params.atmosphereStrength = 0.5;
            params.planetAge    = 3.0e9;
            params.tectonicPlateCount = 8;
            break;

        case Preset::OceanWorld:
            params.planetRadius = 1.2;
            params.planetMass   = 1.5;
            params.semiMajorAxis = 1.0;
            params.waterAmount  = 0.90;
            params.atmosphereStrength = 1.2;
            params.tectonicPlateCount = 30;
            break;

        case Preset::FrozenWorld:
            params.starMass        = 0.3;
            params.starRadius      = 0.35;
            params.starTemperature = 3200.0;
            params.planetRadius    = 0.8;
            params.planetMass      = 0.6;
            params.semiMajorAxis   = 1.5;
            params.waterAmount     = 0.40;
            params.atmosphereStrength = 0.7;
            params.planetAge       = 6.0e9;
            params.tectonicPlateCount = 8;
            break;

        case Preset::VolcanicWorld:
            params.starMass        = 1.5;
            params.starRadius      = 1.4;
            params.starTemperature = 7000.0;
            params.planetAge       = 200.0e6;
            params.semiMajorAxis   = 0.8;
            params.waterAmount     = 0.30;
            params.atmosphereStrength = 1.5;
            params.tectonicPlateCount = 20;
            break;

        case Preset::AncientGarden:
            params.starAge         = 8.0e9;
            params.planetAge       = 8.0e9;
            params.eccentricity    = 0.0;
            params.waterAmount     = 0.60;
            params.tectonicPlateCount = 6;
            params.atmosphereStrength = 1.0;
            break;
    }
    return params;
}

DerivedPlanetValues derive(const PlanetParams& params) {
    // Physical constants
    constexpr double kEarthRadiusM   = 6.371e6;     // meters
    constexpr double kEarthGravity   = 9.81;         // m/s^2
    constexpr double kSolarLuminosity= 3.828e26;     // W
    constexpr double kSunRadiusM     = 6.957e8;      // m
    constexpr double kSunTempK       = 5778.0;       // K
    constexpr double kAuM            = 1.496e11;     // m/AU
    constexpr double kSecondsPerDay  = 86400.0;
    constexpr double kStefanBoltzmann= 5.67e-8;      // W/(m^2 K^4)
    constexpr double kPi             = 3.14159265358979323846;

    DerivedPlanetValues d;
    d.planetRadiusMeters    = params.planetRadius * kEarthRadiusM;
    d.gravity               = (params.planetMass / (params.planetRadius * params.planetRadius))
                               * kEarthGravity;
    d.rotationPeriodSeconds = params.rotationRate * kSecondsPerDay;

    // Stellar luminosity via Stefan-Boltzmann: L ~ (Rs/Rsun)^2 * (Ts/Tsun)^4 * Lsun
    double rsRatio = params.starRadius;              // already in solar radii
    double tsRatio = params.starTemperature / kSunTempK;
    double lStar   = kSolarLuminosity * rsRatio * rsRatio
                     * tsRatio * tsRatio * tsRatio * tsRatio;

    // Solar constant at planet: S = L / (4*pi*d^2)
    double distM = params.semiMajorAxis * kAuM;
    d.solarConstant = lStar / (4.0 * kPi * distM * distM);

    // Equilibrium temperature: T_eq = (S*(1-A) / (4*sigma))^(1/4), albedo A=0.3
    constexpr double kAlbedo = 0.3;
    double flux = d.solarConstant * (1.0 - kAlbedo) / (4.0 * kStefanBoltzmann);
    // T_eq = flux^0.25 — use sqrt twice
    d.equilibriumTemperatureK = foundation::det_math::sqrt(
        foundation::det_math::sqrt(flux));

    d.lapseRateCPerKm = 6.5;

    return d;
}

} // namespace worldgen
