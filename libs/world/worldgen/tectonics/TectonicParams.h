#pragma once

#include <cstdint>

namespace worldgen::tectonics {

// All constants for the coarse tectonic-history simulation, Earth-anchored.
// One-line citation per constant. M-T1 scope only: motion, subduction, ridge
// creation, aging, boundary scan. Event constants (merge/rift/collision/
// terranes/hotspots/erosion) are intentionally absent until M-T2 needs them.
//
// Geometry anchor: Earth mean radius 6371 km (IUGG). Used to convert plate
// surface speeds (cm/yr) into angular rates (rad/Myr).
inline constexpr double kEarthRadiusKm = 6371.0; // IUGG mean radius

// Coarse grid subdivision: SphereGrid(128) = 163,842 tiles (~56 km/tile).
// Resolves plate boundaries and ocean age stripes without per-step cost blowup.
inline constexpr uint32_t kCoarseN = 128;

// Time step. 5 Myr keeps per-step plate motion under one coarse tile width
// (fastest ocean ~10 cm/yr * 5 Myr = 500 km < a few tiles), so forward
// rasterization never skips cells. (PlaTec/Tectonics.js use comparable dt.)
inline constexpr double kDtMyr = 5.0;

// History length: base 800 Myr (~2 Wilson cycles at Earth rates) scaled by
// planet age. Factor = clamp(planetAge / 4.5e9, 0.4, 1.3): younger planets get
// shorter recorded history, older planets a bit more. (Wilson-cycle period ~300-500 Myr.)
inline constexpr double kHistoryBaseMyr   = 800.0;
inline constexpr double kHistoryAgeRefYrs = 4.5e9; // Earth age reference
inline constexpr double kHistoryAgeMin    = 0.4;
inline constexpr double kHistoryAgeMax    = 1.3;

// Plate surface speeds (cm/yr). Oceanic plates move faster than continental
// (slab pull dominates ridge push). (Bird 2003 plate catalog; DeMets et al. NUVEL-1A.)
inline constexpr double kOceanicSpeedMinCmYr     = 4.0;
inline constexpr double kOceanicSpeedMaxCmYr     = 10.0;
inline constexpr double kContinentalSpeedMinCmYr = 1.0;
inline constexpr double kContinentalSpeedMaxCmYr = 4.0;

// Planet-age speed multiplier on plate rates: clamp(2.0 - age/4.5e9, 0.5, 2.0).
// Younger = hotter mantle = faster convection. (Ports PlateMovementStage scaling.)
inline constexpr double kSpeedAgeMin = 0.5;
inline constexpr double kSpeedAgeMax = 2.0;

// Oceanic crust at sim start is pre-aged up to this cap so the initial seafloor
// is not uniformly age 0. (Oldest in-situ ocean floor on Earth ~180-200 Myr; the
// Parsons-Sclater / GDH1 depth-age law saturates past ~180 Myr.)
inline constexpr int32_t kOceanInitMaxAgeMyr = 180;

// Initial crustal thickness. Continental 38 +/- 6 km (global average ~39 km,
// Mooney CRUST2.0); oceanic 7 km (White et al. 1992 normal ocean crust).
inline constexpr double kContinentalThicknessMeanKm  = 38.0;
inline constexpr double kContinentalThicknessSpreadKm = 6.0;
inline constexpr double kOceanicThicknessKm           = 7.0;

// Continental crust area target as a fraction of all coarse tiles. The 1.12
// factor gives submerged continental shelves (crust slightly exceeds final land
// area, sea level cuts through the shelf). Matches the old PlateStage convention.
inline constexpr double kCrustAreaFactor = 1.12;

// New oceanic crust stamped into spreading-ridge gaps. Born at the current step
// time (age 0), normal ocean thickness. (Ridge crest = freshly accreted crust.)
inline constexpr double kRidgeCrustThicknessKm = kOceanicThicknessKm;

// Boundary classification thresholds (adapted from TerrainStage). A boundary is
// convergent/divergent when |convergence| exceeds this fraction of the relative
// speed; otherwise transform (strike-slip dominates).
inline constexpr double kConvergenceFraction = 0.35;

// crustAge output cap (u16 storage in TectonicHistory): clamp to avoid overflow.
inline constexpr uint16_t kMaxStoredAgeMyr = 65534;

// Sentinel for "never" in orogeny fields and for the crust-cell orogeny stamp.
inline constexpr int32_t kOrogenyNever = 0x7FFFFFFF;

} // namespace worldgen::tectonics
