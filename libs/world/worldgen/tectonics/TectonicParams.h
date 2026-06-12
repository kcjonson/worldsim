#pragma once

#include <cstdint>

namespace worldgen::tectonics {

// All constants for the coarse tectonic-history simulation, Earth-anchored.
// One-line citation per constant. M-T1 scope: motion, subduction, ridge
// creation, aging, boundary scan. M-T2 adds Wilson-cycle events: collisions,
// sutures, merging, rifting, terrane accretion, hotspots, erosion proxy, slow
// Euler-pole evolution. M-T2.5 adds arc crust production (island-arc maturation,
// continental-margin progradation, a continental-area feedback controller, and
// volcanism decay) so subduction-zone magmatism balances collisional shortening and
// continental area stays near its target (Reymer & Schubert 1984).
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
// Volcanism e-folding decay time (Myr). Arc/hotspot volcanism relaxes toward 0 with
// this tau so extinct arcs go magmatically quiet once subduction moves on; active arcs
// stay hot because they are re-fed each step. ~60 Myr lets a saturated arc (volcanism
// ~1) fall below the terrane-docking gate (~0.15) over ~110 Myr, matching arc-to-quiet
// terrane timescales. (Arc magmatism ceases within ~10-20 Myr of subduction shutdown;
// the longer tau here keeps recently-active belts legible for M-T4.)
inline constexpr double kVolcanismTauMyr = 60.0;

// --- Arc crust production (M-T2.5) ---
// Earth keeps continental area roughly constant despite collisional shortening
// because subduction-zone magmatism manufactures juvenile continental crust at
// ~1 km^3/yr (Reymer & Schubert 1984). The collisional thickening in
// collisionProcessing only stacks existing continent (footprint shrinks); without a
// production term the continental cell count bleeds 18-25% over a default run. These
// constants add the two real producers: intra-oceanic island-arc maturation (the
// dominant juvenile-crust factory, later accreted as exotic terranes) and
// continental-margin arc progradation (Andean-type margins grow seaward slowly).
//
// Island-arc maturation eligibility floor: an oceanic cell is a maturation candidate
// once its accumulated arc `volcanism` exceeds this, i.e. it has been a magmatic arc
// long enough to have built andesitic, low-density crust (intra-oceanic arc -> juvenile
// continental crust, Izu-Bonin-Mariana, Stern 2010). With kArcVolcanismPerStep=0.15 per
// step of subduction, 0.85 needs ~6 deposition steps; because arc bands migrate, that
// is ~50-150 Myr of sustained nearby subduction. Eligibility is necessary but not
// sufficient: the actual flip is deficit-gated and probabilistic (see below).
inline constexpr float kArcMatureVolcThreshold = 0.85f;
// Per-step maturation probability for an eligible arc-band cell, at FULL deficit
// (controller factor 2.0 -> factor-1 = 1.0). The realized rate is this times
// (factor - 1), so it is 0 at/above the area target and ramps up smoothly with the
// shortfall. Probabilistic (not a batch flip) so an irreversible conversion drains a
// deficit gradually and settles near target instead of overshooting. Evaluated only
// inside the subduction arc band (collisionProcessing), so maturation tracks real
// subduction zones, never mid-plate hotspots. 0.20 drains a typical deficit over a few
// tens of Myr without a single-step lurch.
inline constexpr double kArcMatureProbPerStep = 0.20;
// Juvenile continental crust is thin and immature (island arcs ~20-25 km, far below
// cratonic 38; Suyehiro et al. 1996 IBM arc crust ~22 km). Newly matured arc cells
// get this thickness so they read as thin continental margins, not Tibet.
inline constexpr float kJuvenileArcThicknessKm = 24.0f;
// Continental-margin progradation: at a CO arc the overriding continent's margin
// grows seaward by occasionally converting an immediately-adjacent oceanic cell of
// the OVERRIDING plate to thin continental crust (forearc accretion + magmatic
// addition). Deficit-gated and scaled by (controllerFactor - 1), so the per-cell rate
// runs 0 at/above target up to this cap at full deficit; island-arc maturation stays
// the dominant producer. Kept modest so margins creep, not sprint, seaward.
inline constexpr double kMarginAccretionProb = 0.08;
// Margin progradation only acts on oceanic cells in the innermost arc rings (right at
// the trench-adjacent forearc), not the full arc band.
inline constexpr int kMarginAccretionMaxRing = 2;

// --- Continental-area feedback controller (M-T2.5) ---
// Same role as the plate-count controller: a small deterministic nudge that keeps the
// stochastic dynamics near the physical set-point without hard clamps. Each step we
// compare resolved continental cells against the area target
// (target = (1-water) * kCrustAreaFactor * N) and scale the arc volcanism accumulation
// rate by a smooth bounded factor. Deficit (below set-point) -> factor > 1 (arcs mature
// faster, more juvenile crust); surplus -> factor < 1 (production throttles). The factor
// is linear in the fractional error and clamped to [min, max]:
//   setPoint = target * (1 - kAreaControllerSetpointBias)
//   error    = (setPoint - resolved) / target          // + when below set-point
//   factor   = clamp(1 + kAreaControllerGain * error, kAreaControllerFactorMin,
//                    kAreaControllerFactorMax)
// Set-point bias: production is irreversible (matured crust cannot un-convert), so a
// purely proportional controller halts production exactly at target and then settles a
// little ABOVE it (the last deficit-driven batch overshoots, consumption near target is
// slow). Aiming the controller ~6% below the true target halts production early and lets
// ongoing subduction/collision consumption pull the resolved area back onto target,
// centering the 5-seed distribution inside the +/-10% acceptance band. This is the
// standard offset correction for proportional control of a one-sided actuator.
inline constexpr double kAreaControllerSetpointBias = 0.06;
// Gain 3.0: a ~10% shortfall below the set-point pushes the factor to ~1.3, enough to
// close the gap without a single-step lurch; the clamp bounds prevent runaway.
inline constexpr double kAreaControllerGain      = 3.0;
inline constexpr double kAreaControllerFactorMin = 0.5;
inline constexpr double kAreaControllerFactorMax = 2.0;

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
// the costliest event pass; terranes accrete over many Myr so a stride is fine). Now
// that M-T2.5 arc production actually feeds dockable fragments the pass does real
// transfer work, so an 8-step (~40 Myr) stride keeps it cheap without changing the slow
// accretion dynamics.
inline constexpr int kTerraneStride = 8;
// A fragment docks only once it has gone magmatically quiet: its mean `volcanism` must
// be below this for the block to count as a drifted exotic terrane rather than a live
// arc still building in place. Without this gate the M-T2.5 island arcs would re-dock
// the moment they mature (they form right at a trench), firing accretion continuously
// instead of occasionally. (Exotic terranes are old amalgamated blocks by suturing time,
// Coney et al. 1980.)
inline constexpr double kTerraneMaxMeanVolcanism = 0.15;

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
