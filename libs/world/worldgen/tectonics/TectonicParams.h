#pragma once

#include <cstdint>

namespace worldgen::tectonics {

// All constants for the coarse tectonic-history simulation, Earth-anchored.
// One-line citation per constant. M-T1 scope: motion, subduction, ridge
// creation, aging, boundary scan. M-T2 adds Wilson-cycle events: collisions,
// sutures, merging, rifting, terrane accretion, hotspots, erosion proxy, slow
// Euler-pole evolution.
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

// ============================================================================
// M-T2 Wilson-cycle event constants
// ============================================================================

// --- Continental collision (CC) ---
// Crustal-thickening rate per unit convergence-time. convergence is the inward
// component of relative plate velocity (rad/Myr-scaled, ~surface rate). A
// sustained convergence of order 1 (fast) over ~100 Myr should add ~30 km of
// crust (38 -> 68, Tibet/Himalaya scale). conv values run ~1e-3 rad/Myr at
// these rates, so the coefficient is large; tuned against the default sim so a
// collision matures over the merge timescale. (Himalayan crust ~70 km, England
// & Molnar 1991; collision-thickening doubling time ~50 Myr.)
inline constexpr double kCcThickenPerConvMyr = 9000.0;
// Cap on crustal thickness from collision (km). (Tibetan plateau crust ~70-80 km.)
inline constexpr double kMaxCrustThicknessKm = 75.0;
// CC thickening / orogeny band half-width in coarse-cell rings from the boundary.
inline constexpr int kCollisionBandRings = 3;

// Continental-collision motion damping: a plate's rotation is scaled by
// (1 - block), where block rises with its CC contact extent and saturates at
// kMaxCollisionBlock. This makes buoyant continents resist subduction and suture
// edge-to-edge instead of interpenetrating, conserving continental area. The
// reference tile count sets how much contact reaches full block. (Continental crust
// is too buoyant to subduct; collision slows convergence, Molnar & Tapponnier 1975.)
inline constexpr double kMaxCollisionBlock = 0.70;
inline constexpr uint32_t kCollisionBlockRefTiles = 45;

// Per-step orogeny-intensity increment at the collision front (added, clamped to
// 1). A boundary in sustained collision saturates intensity over a few tens of
// Myr. (Intensity is a unitless 0..1 "how recently/hard did this orogen build"
// accumulator, read by TerrainStage for ridged-belt amplitude in M-T4.)
inline constexpr float kOrogenyIntensityPerStep = 0.20f;

// --- Arc volcanism (CO / OO convergent) ---
// Volcanism deposited per step on the overriding side within the arc band.
// Volcanism is a unitless 0..1 accumulator (cap 1). (Andean/Cascade arcs build
// over ~10 Myr of subduction.)
inline constexpr float kArcVolcanismPerStep = 0.15f;
// Arc band: inland rings from the trench where volcanism + modest thickening land
// (volcanic front sits ~150-250 km inland; ~3-5 coarse cells at ~56 km/cell).
inline constexpr int kArcBandRingMin = 1;
inline constexpr int kArcBandRingMax = 4;
// Modest overriding-plate thickening per step at a CO/OO arc (km). Arc magmatism
// thickens the upper plate but far slower than continental collision.
inline constexpr double kArcThickenKmPerStep = 0.05;

// --- Plate merge ---
// Per-pair collision score: each step a colliding pair adds sum(convergence*dt)
// over its shared CC contact tiles; a pair NOT colliding this step decays by
// kCollisionScoreDecay. So only SUSTAINED collision accumulates toward the
// threshold. convergence runs ~1e-2 rad/Myr at full plate speed, so a ~15-tile
// contact adds ~1/step; the threshold then takes ~30 sustained steps (~150 Myr) to
// merge. (Wilson-cycle continent-continent suturing completes ~50-100 Myr after
// contact; we require sustained convergence so grazing/transient touches don't fuse
// plates.)
inline constexpr double kMergeScoreThreshold = 1.0;
inline constexpr double kCollisionScoreDecay = 0.75; // per step, untouched pairs
// On merge, re-stamp orogeny intensity along the suture at this level.
inline constexpr float kSutureOrogenyIntensity = 0.85f;

// --- Rifting ---
// Per-step base probability of a rift firing when the alive plate count is below
// the controller set-point K. Scaled up by how far below K we are. (At dt=5 Myr,
// p~0.02/step gives a rift roughly every ~250 Myr per missing plate; the deficit
// scaling makes recovery faster the more plates are missing.)
inline constexpr double kRiftBaseProb = 0.040;
inline constexpr double kRiftDeficitProb = 0.035; // added per missing plate
// A plate is only riftable if its area exceeds this multiple of the mean alive
// plate area (only big plates split; avoids shattering small fragments).
inline constexpr double kRiftMinAreaFactor = 1.5;
// Rift split-path cost: base + noise - sutureBias * recentOrogeny. Higher bias =
// rifts re-open old sutures more strongly. (Wilson: rifts preferentially reactivate
// inherited weaknesses, Buiter & Torsvik 2014.)
inline constexpr float kRiftSutureBias = 4.0f;
inline constexpr float kRiftPathNoise  = 0.6f; // noise amplitude on path cost
// Orogeny age (Myr) under which a suture counts as "recent" for rift bias.
inline constexpr int32_t kRiftSutureRecentMyr = 600;

// --- Terrane accretion ---
// A continental fragment smaller than this fraction of total coarse tiles, riding
// on a plate whose ocean is subducting at a CO boundary, plasters onto the
// overriding plate when it reaches the trench. (Microcontinents / exotic terranes,
// Coney et al. 1980.)
inline constexpr double kTerraneMaxAreaFraction = 0.02;
// Run the terrane flood-fill every Nth step (the full continental component scan is
// the costliest event pass; terranes accrete over many Myr so a stride is fine).
inline constexpr int kTerraneStride = 4;

// --- Hotspots ---
// Plume count H = kHotspotBase + K/kHotspotPerPlateDiv, capped at kHotspotCap.
// (Earth has ~40-50 hotspots but only ~10-15 are strong/persistent; Courtillot
// et al. 2003 count ~7 deep plumes.)
inline constexpr int kHotspotBase = 5;
inline constexpr int kHotspotPerPlateDiv = 3;
inline constexpr int kHotspotCap = 15;
// Volcanism deposited per step at the cell over a plume; +1 ring at half rate.
inline constexpr float kHotspotVolcPerStep = 0.30f;

// --- Erosion proxy ---
// Continental thickness relaxes toward this equilibrium (km) with an e-folding
// time tau. thickness = eq + (thickness-eq)*exp(-dt/tau). Older orogens erode
// toward the stable cratonic ~35-40 km. (Continental denudation timescale
// ~10s-100s Myr; we use the slow end so young orogens stay high through collision.)
inline constexpr double kErosionEqThicknessKm = 35.0;
inline constexpr double kErosionTauMyr = 300.0;

// --- Slow Euler-pole evolution ---
// Fixed poles for the whole 800 Myr history strand some ocean basins (they never
// reach a trench), so seafloor ages past Earth's ~190 Myr ceiling. Each plate
// re-draws its pole + speed every kPoleEvolutionPeriodMyr with continuity (the new
// pole is a bounded random rotation of the old one, the speed a bounded random
// walk), physically motivated by slowly changing slab-pull/ridge-push balance.
// (Plate-motion reorganizations on Earth recur every ~50-100 Myr; absolute-plate-
// motion changes, e.g. the Hawaiian-Emperor bend ~47 Ma.)
inline constexpr double kPoleEvolutionPeriodMyr = 90.0;
// Max angular nudge applied to the pole each evolution (radians). ~1.2 rad ~ 69 deg,
// enough to swing a stranded basin toward a trench within the history.
inline constexpr double kPoleEvolutionMaxNudgeRad = 1.2;
// Fractional speed random-walk bound each evolution (+/-).
inline constexpr double kPoleEvolutionSpeedJitter = 0.45;

} // namespace worldgen::tectonics
