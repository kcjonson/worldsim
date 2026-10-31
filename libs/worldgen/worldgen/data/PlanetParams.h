#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace worldgen {

// Grid subdivision bounds. n drives tile count = 10*n*n + 2 (Goldberg).
// kMaxGridSubdivision is the hard ceiling enforced by both PlanetIO (file
// loads) and PlanetParams::validate() — the single source of truth for the cap.
inline constexpr uint32_t kMinGridSubdivision = 1;
inline constexpr uint32_t kMaxGridSubdivision = 2048;

// Default n for a freshly generated planet, calibrated from a full-pipeline
// gen-time sweep on the reference dev machine (RelWithDebInfo, seed 424242):
// n=512 -> 10.8s, n=768 -> 15.6s, n=1024 -> 25.2s, n=1449 -> 44.2s. 1024
// (~10.5M tiles, ~5.6km/tile at Earth scale) is the chosen quality/time point.
// Re-run the sweep (worldgen-cli --n <n>) and re-pick if the pipeline cost
// shifts. Quick Start bypasses this with a cached planet; this default governs
// the custom WorldCreator path.
inline constexpr uint32_t kDefaultGridSubdivision = 1024;

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
    uint32_t gridSubdivision{kDefaultGridSubdivision}; // n; total tiles = 10*n*n + 2

    // Return a preset configuration.
    static PlanetParams preset(Preset p);

    // Reject out-of-range inputs before generation. Returns a human-readable
    // reason on the first violation, or std::nullopt when every field is in
    // range. Callers (PlanetGenerator::start) fail loud instead of relying on
    // scattered debug asserts that compile out in release.
    std::optional<std::string> validate() const;
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
