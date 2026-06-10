#pragma once

#include <cstdint>

namespace worldgen {

enum class Preset {
    EarthLike,
    DesertWorld,
    OceanWorld,
    FrozenWorld,
    VolcanicWorld,
    AncientGarden,
};

// All input parameters that define a planet and its star system.
// Units and defaults documented per field.
struct PlanetParams {
    // --- Star ---
    double starMass{1.0};          // solar masses
    double starRadius{1.0};        // solar radii
    double starTemperature{5778.0};// Kelvin
    double starAge{4.6e9};         // years

    // --- Planet ---
    double planetRadius{1.0};      // Earth radii
    double planetMass{1.0};        // Earth masses
    double rotationRate{1.0};      // Earth days per rotation
    int    tectonicPlateCount{12};
    double waterAmount{0.70};      // 0..1 fraction of surface
    double atmosphereStrength{1.0};// Earth atmospheres
    double planetAge{4.5e9};       // years

    // --- Orbit ---
    double semiMajorAxis{1.0};     // AU
    double eccentricity{0.017};

    // --- Generation ---
    uint64_t seed{12345678901234567ULL};
    uint32_t gridSubdivision{1024}; // n; total tiles = 10*n*n

    // Return a preset configuration.
    static PlanetParams preset(Preset p);
};

// Values derived from PlanetParams — computed by derive().
// Refined in M3c; these are useful approximations for stubs.
struct DerivedPlanetValues {
    double planetRadiusMeters{};     // planetRadius * 6.371e6
    double gravity{};                // m/s^2, g = (M/R^2) * 9.81
    double solarConstant{};          // W/m^2 at planet
    double equilibriumTemperatureK{};// assuming albedo = 0.3
    double rotationPeriodSeconds{};  // rotationRate * 86400
    double lapseRateCPerKm{6.5};     // standard lapse rate (constant)
};

DerivedPlanetValues derive(const PlanetParams& params);

} // namespace worldgen
