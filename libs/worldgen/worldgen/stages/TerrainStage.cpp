// TerrainStage — M-T4 elevation synthesis.
//
// Elevation derives from the simulated tectonic state, not from uplift kernels
// stacked on a flat base. Two laws carry the bimodal hypsometry for free:
//   - Continental: Airy isostasy on crustal thickness gives the platform height;
//     orogeny-aged RIDGED belts give the linear mountain texture (sharp crests,
//     V-valleys) along collision margins; a continental-shelf ramp handles coasts.
//   - Oceanic: the GDH1 plate-cooling depth-age law
//       depth = floorDepth - (floorDepth - ridgeDepth) * exp(-age / tau)
//     turns the simulated seafloor age field into a ridge-to-abyssal gradient that
//     saturates near the plate-equilibrium depth (~-5750 m) rather than subsiding
//     without bound (unlike the half-space sqrt law).
// Narrow active-boundary kernels add trenches, volcanic arcs, rifts, and transform
// roughness only where the full-res boundary classification puts them.
//
// All tectonic inputs (thicknessKm, orogenyIntensity, volcanism, convergence) are
// sampled from the coarse TectonicHistory through the SAME domain warp CrustStage
// used (CoarseSampler::warpedCoarseDir + smoothSampleAt), so they stay aligned with
// the coastline. crustAge and orogenyAge are already full-res fields (CrustStage
// wrote them) and are read directly.
//
// Pipeline:
//   1. Boundary classification: relative Euler velocity from world.plates against the
//      boundary normal averaged along the boundary -> BoundaryType; slab plate per pair.
//   2. Kernel blend: each kernel family's share, convergence, and the oceanic share of the
//      slab side, along each plate pair's boundary, pooled into anchors.
//   3. Straight-line crust-edge distance (continental shelf ramp).
//   4. Straight-line distance to the nearest boundary tile.
//   5. Elevation synthesis: each tile weighs the anchors around it whose boundaries hold its
//      plate (one partition of unity, so the kernels blend without a step along a boundary
//      or between boundaries), runs each boundary's kernels at its own distance and takes
//      their weighted mean; the slab-plate boundary tiles go first, for the trench floors.
//   6. Sea level: histogram quantile at waterAmount.
//
// Determinism: every per-tile value is a pure function of (tile id, params, seeds).
// parallelFor uses fixed grain slabs; all transcendentals go through det_math; the
// coarse samplers are pure functions of the coarse grid + fields. Distance passes are
// serial FIFOs seeded in ascending tile-id order, so results are identical at any
// thread count.

#include "worldgen/stages/TerrainStage.h"

#include "worldgen/data/WorldData.h"
#include "worldgen/pipeline/GenerationError.h"
#include "worldgen/stages/SeedDistance.h"
#include "worldgen/tectonics/CoarseSampler.h"
#include "worldgen/tectonics/TectonicHistory.h"
#include "worldgen/tectonics/TectonicParams.h"

#include <math/DeterministicMath.h>
#include <random/HashNoise.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace worldgen {

namespace {

// Local alias for brevity within this file.
using BType = BoundaryType;

// ============================================================================
// Elevation-model constants (Earth-anchored). Centralized here so the mountain
// look is tuned in one place. Heights in meters, distances in km unless noted.
// ============================================================================

// --- Continental Airy isostasy ---
// Crust floats on the mantle: extra thickness above the reference column rises by
// (rho_mantle - rho_crust)/rho_mantle of the excess. The lumped coefficient
// 180 m per km of crust above a 35 km reference reproduces the observed platform
// heights: 35 km -> +400 m baseline craton, 50 km -> +3.1 km, 65 km -> +5.8 km
// (Tibet-class plateau). (Airy 1855; reference column from CRUST2.0 global mean.)
// Isostasy with a platform KNEE. Below the knee thickness most continental crust sits
// on a flat ~platform height, so the bulk of the continent forms one sharp hypsometric
// land mode (the bimodality gate needs a tall land peak to beat the deep-ocean peak,
// which a smooth thickness-proportional ramp smears away). Above the knee, extra
// thickness raises elevation by kIsostasyMPerKm per km — the real isostatic response
// that builds plateaus at genuine collision cores.
//   thickness <= knee  -> kPlatformM (flat shelf-of-the-continent)
//   thickness  > knee  -> kPlatformM + (thickness - knee) * kIsostasyMPerKm
// Knee = 44 km: normal cratonic crust (38-44 km) all floats to roughly the same
// platform height; only over-thickened crust stands tall. At 145 m/km above the knee:
//   46 km -> +740 m, 58 km -> +2.48 km, 70 km -> +4.22 km (Tibet-class).
// (Airy 1855; reference column from CRUST2.0 global mean.)
constexpr float kIsostasyKneeThicknessKm = 44.0f;
constexpr float kIsostasyPlatformM       = 450.0f; // platform height at/below the knee
constexpr float kIsostasyMPerKm          = 145.0f; // ramp above the knee
// Cap the thickness fed to isostasy so a runaway collision raster can't push a tile
// past plausible plateau heights (Tibetan crust tops out ~70-80 km).
constexpr float kIsostasyMaxThicknessKm  = 72.0f;

// --- Orogeny-aged ridged mountain belts ---
// THE core of the realistic-mountain fix: ridged noise gives sharp crests + V-valleys
// (a RANGE), not a smooth dome. Amplitude is gated by the simulated orogenyIntensity
// so belts only build where collision actually happened, and decays with orogenyAge so
// old orogens are low and smooth while young ones are tall and sharp.
constexpr float kBeltBaseAmpM = 2800.0f; // peak ridged relief at full intensity, fresh orogen
// M-T4.5: with the orogeny field now thin + linear (PlateSim concentrates the stamp on the
// boundary line), a fraction of the belt amplitude is added as a POSITIVE lift so the thin
// orogeny line stands above the mountain threshold; the rest stays as recentered ridged
// crest/valley texture so the belt reads as a sharp range, not a smooth welt. The lift
// fraction is the positive-lift share; (1 - fraction) is the +/- ridged texture.
//
// A CONSTANT lift fraction made the crest a flat-topped pedestal of uniform height, so a
// straight boundary read as a continuous white wall. Modulate the fraction along the belt on
// a low-frequency noise within [min,max]: where it is low, more amplitude goes to the ridged
// texture whose valleys dip below the pedestal -> real passes and saddles; where it is high, a
// continuous spine. So the crest breaks like a true range (Andes have passes, not one wall).
// The min floor keeps the belt from severing into disconnected beads (the M-T4.5 risk).
constexpr float kBeltLiftFracMin = 0.30f;
constexpr float kBeltLiftFracMax = 0.55f;
constexpr float kBeltLiftFreq    = 1.3f; // along-belt lift-fraction noise frequency (low, long-scale variation)
// Intensity sharpening (smoothstep): only orogeny intensity above kBeltIntensityKnee produces
// tall belt relief, ramping to full by kBeltIntensityFull. This narrows the belt to the high-
// intensity SPINE of the (smooth-sampled) field so faint flanks stay at hill height rather
// than the whole bump clearing the mountain threshold.
constexpr float kBeltIntensityKnee = 0.40f;
constexpr float kBeltIntensityFull = 0.85f;
// ageDecay = exp(-orogenyAgeMyr / tau) floored: young belts (active margins) sharp and
// tall, old belts (interior sutures) subdued to rolling highland. M-T4.5: tau cut 350 -> 260
// and floor 0.20 -> 0.10 so the age gradient the sim now produces (interior sutures average
// 200-400 Myr, active margins ~30 Myr) translates into a steep height contrast: an old
// interior suture drops to ~0.10-0.30 of full belt height (a modest interior ridge, not a
// second mountain wall), which is the main lever pulling the interior-mountain fraction down
// while active-margin belts stay Andes-tall.
constexpr float kBeltAgeDecayTauMyr = 260.0f;
constexpr float kBeltAgeDecayFloor  = 0.10f;
// alongBeltMod = base + amp * fractalNoise: gaps and culminations along the belt so a
// range isn't a uniform wall (the Andes have passes and high knots, not a constant crest).
// M-T4.5: base raised 0.55 -> 0.70, amp cut 0.90 -> 0.50 so the along-belt variation makes
// knots and passes WITHOUT dropping whole segments below the mountain threshold (which would
// sever the belt into disconnected beads and tank per-component elongation). The belt keeps a
// continuous high spine with height variation along it.
constexpr float kBeltAlongBase = 0.70f;
constexpr float kBeltAlongAmp  = 0.50f;
constexpr float kBeltAlongFreq = 2.5f;
// Ridged-crest spatial frequency on the unit sphere. Higher -> finer crest spacing.
// ~3.2 puts crest-to-crest at roughly the 10-100 km scale that reads as a mountain range.
constexpr float kBeltRidgeFreq    = 3.2f;
constexpr int   kBeltRidgeOctaves = 5;
// Belt amplitude ramps with crustal thickness excess: 0 at/below 46 km (re-stamped but
// un-thickened crust stays at hill height), full at 60 km (a real collision core). With the
// narrowed thicken band (kCollisionThickenRings), the thickened core is tight, so the belt
// traced from it is thin; a soft floor in the belt term (below) keeps the belt continuous
// where thickness dips a hair below the knee mid-ridge.
constexpr float kBeltThicknessMinKm  = 46.0f;
constexpr float kBeltThicknessFullKm = 60.0f;

// --- Continental hills ---
// Broad fractal relief everywhere on land, amplified near recent orogeny (foothills).
constexpr float kHillFreq        = 3.0f;
constexpr int   kHillOctaves     = 6;
constexpr float kHillBaseAmpM    = 350.0f;
// M-T4.5: cut 350 -> 180. The orogeny-proximity hill boost added broad foothill relief around
// every orogen; near interior sutures it lifted roundish foothill patches over the mountain
// threshold, inflating the interior-mountain fraction. A smaller boost keeps foothills as
// hills, not mountains, so high tiles stay on the belt spine.
constexpr float kHillOrogenyAmpM = 180.0f; // extra amplitude scaled by proximity-to-orogeny

// --- Continental shelf profile (Earth-anchored piecewise) ---
// Real passive-margin structure (Harris et al. 2014 bathymetric survey):
//   Inner shelf to shelf break: gently inclined, 0-150 m depth, 0-100 km wide
//   Shelf break: ~140 m
//   Continental slope: steep (~1-4 deg), 140 m to 3000+ m, 20-100 km wide
// We use a signed crust-edge distance: positive = inland (continental), negative = seaward.
//   signedKm > kShelfWidthKm      → platformElev (isostasy, blended)
//   0 < signedKm <= kShelfWidthKm → flat shelf rising from kShelfBreakDepthM (-140m) to
//                                   kShelfDepthM (-120m) over the outer portion, then
//                                   smoothstepping up to platformElev over the inner blend
//   signedKm ≈ 0                  → kShelfBreakDepthM (-140m)
//   −kSlopeWidthKm < signedKm < 0 → continental slope: steep descent to abyssalElev
//   signedKm <= −kSlopeWidthKm    → abyssalElev (depth-age, unchanged)
constexpr float kShelfBreakDepthM = -140.0f; // depth at the shelf break (signedKm=0)
constexpr float kShelfDepthM      = -120.0f; // representative mid-shelf depth
// kShelfWidthKm: Earth passive-margin shelves span 50-300+ km (mean ~75 km, wide
// Atlantic-type > 200 km). Use 200 km to ensure 3-4 tiles at n=256 (a stage-km tile is
// ~49 km there) fall in the submerged shelf band; the quantile-based sea level
// self-corrects oceanFraction, so widening the shelf doesn't push more land underwater.
// These widths are stage km (through tileWidthKm).
constexpr float kShelfWidthKm     =  200.0f; // flat shelf width (0 to +200 km inland)
constexpr float kSlopeWidthKm     =   60.0f; // continental slope width (0 to -60 km seaward)
// Inner shelf edge: shelf transitions into the isostatic platform over this blend.
constexpr float kShelfInnerBlendKm = 50.0f;

// --- Oceanic depth-age law (Stein & Stein 1992 GDH1 plate-cooling form) ---
// We use the exponential plate form:
//   depth = floorDepth - (floorDepth - ridgeDepth) * exp(-age / tau)
// which approximates sqrt(age) for young floor and saturates smoothly toward
// floorDepth, so the over-old tail the coarse sim leaves (a few % past 220 Myr)
// spreads across the deep abyssal band instead of piling on a hard clamp.
// Representative values: age 0 -> -2500 (ridge), 60 -> -4490, 100 -> -5060,
// 180 -> -5570, 360 -> -5690.
constexpr float kOceanRidgeDepthM = 2500.0f;
constexpr float kOceanFloorDepthM = 5750.0f; // plate-equilibrium abyssal depth
constexpr float kOceanDepthTauMyr = 65.0f;   // e-folding age of the subsidence
constexpr float kOceanMaxDepthM   = -6500.0f; // safety floor (hills can dip a bit below)
// Abyssal-hill noise + a longer-wavelength swell so the seafloor isn't glassy. M-T4.5: swell
// cut 400 -> 250 to concentrate the abyssal floor into a slightly sharper hypsometric peak
// (the M-T4.5 continental work lengthened coastlines, fattening the mid-depth continental-
// slope shoulder), without changing the mean ocean depth.
constexpr float kAbyssalHillAmpM = 300.0f;
constexpr float kAbyssalSwellAmpM = 250.0f;

// --- Boundary classification and kernel blending ---
// These radii are true km on the sphere (planet radius), unlike the stage-km constants
// elsewhere in this file, which go through tileWidthKm.
// A subduction zone keeps one character for hundreds of km along strike (Andes, Aleutians,
// Tonga-Kermadec), so the boundary normal that decides convergent, divergent, or transform
// is averaged over kBoundaryTypeSmoothKm. Spreading ridges do step through transform
// offsets, every ~50-500 km on Earth, so a regionally spreading boundary is split into
// ridge and transform segments by the normal over kRidgeSegmentKm. Where the type still
// changes, the kernels blend by each family's share within kBoundaryBlendKm.
constexpr double kBoundaryTypeSmoothKm = 300.0;
constexpr double kRidgeSegmentKm       = 50.0;
constexpr double kBoundaryBlendKm      = 150.0;
// Floor on those radii in tile spacings, so coarse grids still average over neighbours.
constexpr double kBoundaryRadiusMinSpacings = 2.5;
// A tile weighs the boundaries around it through anchors, their tiles pooled kInputAnchorKm
// across, over a band of distance kInputBandMinKm wide at the boundary that widens with the
// distance out to the bisector blend (kBoundaryBlendKm). The overriding side reads the trench
// floor from the slab plate's tiles within kFloorBandSpacings of its nearest.
constexpr double kInputAnchorKm     = 20.0;
constexpr double kInputBandMinKm    = 75.0;
constexpr float  kFloorBandSpacings = 2.0f;
// An averaged normal shorter than this fraction of its weight sum has cancelled across a
// fold in the boundary; the radius halves until it is coherent.
constexpr double kNormalCoherenceMin = 0.5;
// Convergent or divergent when |convergence| exceeds this fraction of |v_rel|.
constexpr double kConvergentFraction = 0.35;
// Slab choice: oceanic crust scores 1 + age/kSlabAgeScaleMyr (older is denser), continental 0.
constexpr double kSlabAgeScaleMyr = 100.0;

// --- Active-boundary kernels (applied only near the classified boundary; stage km) ---
// Each falls off as a Gaussian cut to exactly zero at kKernelCutSigmas.
constexpr float kKernelCutSigmas = 3.0f;
// ConvergentCO slab side: a deep trench on the depth-age floor, unambiguously below the
// abyssal mean (trenches are the deepest ocean features), which TrenchArcStructure checks.
constexpr float kTrenchCOAmpM   = -7200.0f;
constexpr float kTrenchCOSigKm  = 110.0f;
// ConvergentCO overriding side: volcanic arc behind the forearc (margin profile below).
constexpr float kArcCOAmpM      = 2500.0f; // scaled by convergence
constexpr float kArcCOSigKm     = 90.0f;
// ConvergentOO: trench + an island arc behind it.
constexpr float kTrenchOOAmpM   = -6000.0f;
constexpr float kTrenchOOSigKm  = 50.0f;
constexpr float kArcOOAmpM      = 6000.0f; // a whole island's rise off the floor, scaled by convergence
constexpr float kArcOOSigKm     = 60.0f;
// Divergent continental rift: valley + flanking shoulders.
constexpr float kRiftAmpM       = -1500.0f;
constexpr float kRiftSigKm      = 60.0f;
constexpr float kRiftShoulderM  = 800.0f;
constexpr float kRiftShoulderDistKm = 80.0f;
constexpr float kRiftShoulderSigKm  = 45.0f;
// Divergent oceanic: ridge crest comes from the depth-age law; add a small axial valley.
constexpr float kAxialValleyM   = -300.0f;
constexpr float kAxialValleySigKm = 20.0f;
// Transform: low-amplitude shear roughness.
constexpr float kTransformAmpM  = 400.0f;
constexpr float kTransformSigKm = 50.0f;

// --- Subduction margin profile (true km from the trench axis, the plate boundary) ---
// Across the overriding plate at Earth's active margins (Peru-Chile, Japan, Aleutians,
// Tonga), seaward to landward: the inner trench wall climbs from the trench floor to a
// submerged forearc (accretionary wedge and forearc basin, -1 to -3 km) whose landward edge
// is the coast, 100-200 km from the trench; the volcanic front stands behind the coast. The
// arc-trench gap is 100-300 km (Gill 1981), the front sitting where the slab is ~100 km
// deep (England et al. 2004; Syracuse & Abers 2006). Intra-oceanic forearcs stand deeper
// and their gaps run shorter, so an island arc's volcanoes are its first land.
// The overriding plate is pulled to the profile in full out to the forearc's full width and
// let go by its end; the coast lands where the plate's own surface wins back, so a shelf
// survives only past the forearc.
constexpr double kInnerWallSigKm  = 30.0;    // trench floor up to the forearc
constexpr double kForearcCODepthM = -2500.0;
constexpr double kForearcCOFullKm = 90.0;
constexpr double kForearcCOEndKm  = 180.0;
constexpr double kArcCOGapKm      = 250.0;   // trench axis to volcanic front
constexpr double kForearcOODepthM = -3000.0;
constexpr double kForearcOOFullKm = 100.0;
constexpr double kForearcOOEndKm  = 180.0;
constexpr double kArcOOGapKm      = 190.0;
// Subduction share of the boundary nearby (kBoundaryBlendKm) at and above which the forearc
// stands in full; below it the forearc hands over to the neighbouring boundary type.
constexpr float  kForearcFullShare = 0.5f;

// --- Hotspot / arc volcanism cones ---
// volcanism * ridged cone noise (high frequency) so hotspot chains read as
// seamount/island lines. Capped so a saturated plume can't make a Hawaii the size of Tibet.
constexpr float kVolcanismConeAmpM  = 4500.0f; // pre-cap amplitude at volcanism=1
constexpr float kVolcanismConeFreq  = 14.0f;
constexpr float kVolcanismCapM      = 3500.0f;
// Volcanism below this raises no cones; above it the cones ramp in from zero height.
constexpr float kVolcanismMin       = 0.02f;

// --- Global elevation clamp ---
constexpr float kElevMinM = -11000.0f; // Challenger-Deep-class floor
constexpr float kElevMaxM =   9000.0f; // Everest-class ceiling

// --- Foothill proximity-to-orogeny decay ---
// Hills near recent orogeny get a boost; this e-folding time controls how quickly
// that boost fades with orogeny age (separate from the belt age-decay tau).
constexpr double kFoothillDecayTauMyr = 600.0;

// --- Abyssal-hill noise parameters ---
constexpr float kAbyssalHillFreq    =  3.0f;
constexpr int   kAbyssalHillOctaves =  5;
constexpr float kAbyssalSwellFreq   =  1.2f;
constexpr int   kAbyssalSwellOctaves = 3;

// --- OO island arcs ---
// arcAmp = kArcOOAmpM * (kOOConvBase + kOOConvAmp * convN) * island, where island maps the
// noise band [kArcIslandNoiseLo, kArcIslandNoiseHi] to 0..1. An island arc is a chain of
// volcanoes ~50-100 km apart standing in deep water (Aleutians, Lesser Antilles, Tonga):
// kArcIslandFreq on the unit sphere gives features ~70 km across, and only the top of the
// noise rises at all, so the water between islands stays abyssal and the arc reads as a
// chain at any sea level.
constexpr float kOOConvBase       = 0.5f;
constexpr float kOOConvAmp        = 0.5f;
constexpr float kArcIslandFreq    = 90.0f;
constexpr float kArcIslandNoiseLo = -0.1f;
constexpr float kArcIslandNoiseHi = 0.3f;

// --- Transform shear noise frequency ---
constexpr float kTransformShearFreq    = 6.0f;
constexpr int   kTransformShearOctaves = 3;

// Gaussian falloff via det_math::exp for cross-platform determinism.
inline float gaussianFalloff(float d, float sigma) {
    if (sigma <= 0.0f) return 0.0f;
    double x = static_cast<double>(d) / static_cast<double>(sigma);
    return static_cast<float>(foundation::det_math::exp(-0.5 * x * x));
}

// Smoothstep of x clamped to [0, 1].
inline float smoothstep01(float x) {
    x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    return x * x * (3.0f - 2.0f * x);
}

// A kernel's falloff: the Gaussian, lowered and rescaled to reach exactly zero at
// kKernelCutSigmas so nothing it touches sits past a hard edge.
inline float cutGaussian(float d, float sigma) {
    const float reach = kKernelCutSigmas * sigma;
    if (d <= -reach || d >= reach) return 0.0f;
    const float floor = gaussianFalloff(reach, sigma);
    return (gaussianFalloff(d, sigma) - floor) / (1.0f - floor);
}

// GDH1 depth-age law as an elevation (negative).
inline float depthAgeElevationM(float crustAgeMyr) {
    const float subside = static_cast<float>(foundation::det_math::exp(
        -static_cast<double>(crustAgeMyr) / kOceanDepthTauMyr));
    return -(kOceanFloorDepthM - (kOceanFloorDepthM - kOceanRidgeDepthM) * subside);
}

// The overriding plate's margin profile: the depth it is pulled to, rising from the trench
// floor at the axis up the inner wall to the forearc, and the pull's weight, 1 out to the
// forearc's full width and smoothstepped to 0 at its end.
struct ForearcPullArgs {
    double distKm   = 0.0; // from the trench axis, true km
    double axisM    = 0.0; // trench floor at the axis nearest
    double regionM  = 0.0; // trench floor along the boundary nearby
    double forearcM = 0.0; // forearc terrace depth
    double fullKm   = 0.0;
    double endKm    = 0.0;
};
struct ForearcPull {
    float targetM = 0.0f;
    float weight  = 0.0f;
};
inline ForearcPull forearcPull(const ForearcPullArgs& args) {
    if (args.distKm >= args.endKm) return {};
    const double wall   = cutGaussian(static_cast<float>(args.distKm), static_cast<float>(kInnerWallSigKm));
    const float  weight = 1.0f - smoothstep01(static_cast<float>((args.distKm - args.fullKm) / (args.endKm - args.fullKm)));
    // The wall climbs from the floor at the axis, which hands over to the boundary's floor
    // nearby as the wall dies out. A floor above the forearc means the slab side is too
    // buoyant to go down (a continent arriving): the forearc rises with it, as at Timor or
    // Taiwan, instead of leaving a moat behind a raised axis.
    const double floorM   = args.regionM + (args.axisM - args.regionM) * wall;
    const double forearcM = std::max(args.forearcM, floorM);
    return {static_cast<float>(forearcM + (floorM - forearcM) * wall), weight};
}

// Piecewise continental shelf profile driven by signed crust-edge distance.
// signedKm > 0 = inland on continental crust; signedKm < 0 = seaward on oceanic crust.
// platformElev = isostatic elevation for this continental tile.
// abyssalElev  = depth-age elevation for this oceanic tile.
// Called by BOTH continental and oceanic synthesis blocks.
inline float shelfElevationForSignedEdge(float signedKm, float platformElev, float abyssalElev) {
    if (signedKm >= kShelfWidthKm) {
        // Deep continental interior — full platform, no shelf.
        return platformElev;
    }
    if (signedKm <= -kSlopeWidthKm) {
        // Past the slope foot — full abyssal depth.
        return abyssalElev;
    }
    if (signedKm >= 0.0f) {
        // On the shelf: rises gently from kShelfBreakDepthM at the break (0 km)
        // up to near kShelfDepthM toward mid-shelf, then blends into platformElev
        // near the inner edge (kShelfWidthKm).
        //
        // Two-segment blend:
        //   outer portion [0, kShelfWidthKm - kShelfInnerBlendKm]: nearly flat at shelf depth
        //   inner blend   [kShelfWidthKm - kShelfInnerBlendKm, kShelfWidthKm]: rise to platformElev
        float outerEdge = kShelfWidthKm - kShelfInnerBlendKm;
        if (signedKm < outerEdge) {
            // Flat shelf, very gentle rise from break to mid-shelf depth.
            float t = signedKm / outerEdge;
            return kShelfBreakDepthM + t * (kShelfDepthM - kShelfBreakDepthM);
        } else {
            // Inner blend: rise from kShelfDepthM to platformElev.
            return kShelfDepthM + smoothstep01((signedKm - outerEdge) / kShelfInnerBlendKm) * (platformElev - kShelfDepthM);
        }
    } else {
        // On the continental slope: steep descent from break to abyssal floor.
        // 0 at the break, 1 at the slope foot; the smoothstep makes it concave up.
        return kShelfBreakDepthM + smoothstep01(-signedKm / kSlopeWidthKm) * (abyssalElev - kShelfBreakDepthM);
    }
}

// Relief below the continental shelf's surface (negative hills, belt valleys), admitted
// only as the profile rises into the platform: none on the flat outer shelf, eased in over
// the inner blend with the profile's own smoothstep, in full inland. Wave-base planing at
// lowstands and sediment infill keep a shelf from holding basins below its surface; without
// this a faded-in negative hill floods a lagoon behind a dry strip at the break.
inline float shelfRelief(float signedKm, float relief) {
    if (relief >= 0.0f || signedKm >= kShelfWidthKm) return relief;
    const float outerEdge = kShelfWidthKm - kShelfInnerBlendKm;
    if (signedKm <= outerEdge) return 0.0f;
    return relief * smoothstep01((signedKm - outerEdge) / kShelfInnerBlendKm);
}

// Cross product
inline void cross3d(double ax, double ay, double az,
                    double bx, double by, double bz,
                    double& rx, double& ry, double& rz) {
    rx = ay * bz - az * by;
    ry = az * bx - ax * bz;
    rz = ax * by - ay * bx;
}

inline double dot3d(double ax, double ay, double az,
                    double bx, double by, double bz) {
    return ax*bx + ay*by + az*bz;
}

inline double length3d(double x, double y, double z) {
    return foundation::det_math::sqrt(x*x + y*y + z*z);
}

} // namespace

// ============================================================================

void TerrainStage::run(StageContext& ctx) {
    const uint32_t N = ctx.grid.tileCount();
    const int      K = static_cast<int>(ctx.world.plates.size());

    // The coarse tectonic history is the source of thickness/orogeny/volcanism/
    // convergence. CrustStage runs before this stage and never clears it.
    const auto& hist = ctx.world.tectonicHistory;
    if (!hist)
        throw GenerationError("TerrainStage requires TectonicHistory (run after CrustStage)");
    const SphereGrid& coarseGrid = *hist->grid;
    const float kWarpAmp = tectonics::warpAmplitude(coarseGrid);

    // Sub-seeds for each independent RNG stream.
    const auto stageSeed32 = static_cast<uint32_t>(ctx.stageSeed ^ (ctx.stageSeed >> 32));
    const uint32_t seedFractal = stageSeed32 ^ 0xA3C5E7F1u;
    const uint32_t seedRidged  = stageSeed32 ^ 0x1B2D4E6Fu;
    // Same three warp channels CrustStage used, so coarse fields warp coherently
    // with the coastline (CrustStage seeds: seed32 ^ {0xA3C5E701, 0x1B2D4E02, 0xDEADBE03}).
    const uint32_t seedWX = stageSeed32 ^ 0xA3C5E701u;
    const uint32_t seedWY = stageSeed32 ^ 0x1B2D4E02u;
    const uint32_t seedWZ = stageSeed32 ^ 0xDEADBE03u;

    // tile width in km (equatorial approximation: circumference / sqrt(N))
    const double kPlanetCircumference = 2.0 * 3.14159265358979323846 *
                                        ctx.derived.planetRadiusMeters;
    const double tileWidthM   = kPlanetCircumference / foundation::det_math::sqrt(static_cast<double>(N));
    const float  tileWidthKm  = static_cast<float>(tileWidthM / 1000.0);

    auto kmToTiles = [&](float km) -> float { return km / tileWidthKm; };

    constexpr size_t kGrainSize = 4096;

    // =========================================================================
    // 1. Boundary classification
    //
    // Full-res boundary type is reclassified here from full-res plateId + Euler poles
    // rather than upsampled from the coarse TectonicHistory boundary fields. This is
    // deliberate: the full-res reclassification is more accurate (it reflects the actual
    // sub-cell plate geometry after CrustStage's domain-warp upsampling, not an
    // approximation interpolated from coarse cells). The coarse boundaryType and
    // boundaryDistance fields in TectonicHistory are sim-internal diagnostics consumed
    // by worldgen-cli --sim-only for debugging; they are not the authoritative values
    // for the full pipeline.
    //
    // A tile's own normal (toward the foreign plate, from its six neighbours) follows
    // every jag of the warped boundary, so the type is decided on the normal averaged
    // along the boundary instead (kBoundaryTypeSmoothKm, kRidgeSegmentKm).
    // =========================================================================

    struct BoundaryTile {
        TileId  tile;
        uint8_t plate;
        uint8_t foreign;     // dominant foreign plate
        bool    cont;
        bool    foreignCont; // majority crust of the foreign plate's tiles beside it
        Vec3d   center;
        Vec3d   normal;      // unit, toward the foreign plate
    };
    std::vector<BoundaryTile> boundaryTiles; // ascending tile id

    {
        std::array<TileId, 6> nbrs{};
        for (uint32_t t = 0; t < N; ++t) {
            uint8_t pid = ctx.data.plateId[t];
            uint32_t cnt = ctx.grid.neighbors(t, nbrs);

            // Find dominant foreign plate
            uint8_t domPlate  = 255u;
            uint32_t domCount = 0u;
            uint32_t foreignCount = 0u;
            for (uint32_t k = 0; k < cnt; ++k) {
                uint8_t npid = ctx.data.plateId[nbrs[k]];
                if (npid != pid && npid != 255u) {
                    ++foreignCount;
                    uint32_t thisCount = 0u;
                    for (uint32_t k2 = k; k2 < cnt; ++k2) {
                        if (ctx.data.plateId[nbrs[k2]] == npid) ++thisCount;
                    }
                    if (thisCount > domCount) { domCount = thisCount; domPlate = npid; }
                }
            }
            if (foreignCount == 0u) continue;

            Vec3d ctr = ctx.grid.tileCenter(t);

            // Boundary normal: direction toward foreign plate neighbors
            double normX = 0.0, normY = 0.0, normZ = 0.0;
            for (uint32_t k = 0; k < cnt; ++k) {
                if (ctx.data.plateId[nbrs[k]] == domPlate) {
                    Vec3d nc = ctx.grid.tileCenter(nbrs[k]);
                    normX += nc.x - ctr.x;
                    normY += nc.y - ctr.y;
                    normZ += nc.z - ctr.z;
                }
            }
            double nLen = length3d(normX, normY, normZ);
            if (nLen > 1e-12) { normX /= nLen; normY /= nLen; normZ /= nLen; }

            bool tileIsCont    = (ctx.data.flags[t] & kFlagContinentalCrust) != 0;
            // Derive foreign crust type from the actual neighboring tiles of domPlate
            // (majority vote of kFlagContinentalCrust), since crust is decoupled from
            // plate identity — mixed-crust plates exist at continental margins.
            bool foreignIsCont = false;
            if (domPlate < static_cast<uint8_t>(K)) {
                uint32_t domCrustCount = 0u, domTileCount = 0u;
                for (uint32_t k = 0; k < cnt; ++k) {
                    if (ctx.data.plateId[nbrs[k]] == domPlate) {
                        ++domTileCount;
                        if ((ctx.data.flags[nbrs[k]] & kFlagContinentalCrust) != 0) ++domCrustCount;
                    }
                }
                foreignIsCont = domCrustCount * 2u > domTileCount; // majority
            }

            boundaryTiles.push_back({t, pid, domPlate, tileIsCont, foreignIsCont, ctr, {normX, normY, normZ}});
        }
    }
    const uint32_t boundaryCount = static_cast<uint32_t>(boundaryTiles.size());

    ctx.reportProgress(0.08f);
    throwIfCancelled(ctx);

    // Neighbourhood sums among boundary tiles within chord radius R, weighted
    // (1 - d^2/R^2)^2, found through a 3D cell hash; the group key limits which tiles
    // count. Summed in cell order, then tile order, so independent of thread count.
    using CellIndex = std::vector<std::pair<uint64_t, uint32_t>>;
    auto cellOf = [](double v, double cell) { return static_cast<int64_t>(std::floor(v / cell)); };
    auto cellKey = [](uint32_t group, int64_t x, int64_t y, int64_t z) {
        auto q = [](int64_t c) { return static_cast<uint64_t>(c + 32768) & 0xFFFFu; };
        return (static_cast<uint64_t>(group) << 48) | (q(x) << 32) | (q(y) << 16) | q(z);
    };
    auto buildCellIndex = [&](double cell, auto groupOf) {
        CellIndex index(boundaryCount);
        for (uint32_t i = 0; i < boundaryCount; ++i) {
            const Vec3d& p = boundaryTiles[i].center;
            index[i] = {cellKey(groupOf(i), cellOf(p.x, cell), cellOf(p.y, cell), cellOf(p.z, cell)), i};
        }
        std::sort(index.begin(), index.end());
        return index;
    };
    auto forEachWithin = [&](const CellIndex& index, double radius, uint32_t group, const Vec3d& p, auto&& fn) {
        const int64_t x = cellOf(p.x, radius), y = cellOf(p.y, radius), z = cellOf(p.z, radius);
        const double  r2 = radius * radius;
        for (int64_t dx = -1; dx <= 1; ++dx)
            for (int64_t dy = -1; dy <= 1; ++dy)
                for (int64_t dz = -1; dz <= 1; ++dz) {
                    const uint64_t key = cellKey(group, x + dx, y + dy, z + dz);
                    auto it = std::lower_bound(index.begin(), index.end(), std::make_pair(key, 0u));
                    for (; it != index.end() && it->first == key; ++it) {
                        const Vec3d& c = boundaryTiles[it->second].center;
                        const double d2 = (c.x - p.x) * (c.x - p.x) + (c.y - p.y) * (c.y - p.y) +
                                          (c.z - p.z) * (c.z - p.z);
                        if (d2 < r2) fn(it->second, d2);
                    }
                }
    };
    auto weight = [](double d2, double r) {
        const double w = 1.0 - d2 / (r * r);
        return w > 0.0 ? w * w : 0.0;
    };
    // Radii in chord units on the unit sphere, floored at a few tile spacings so the
    // averages still span neighbours on coarse grids.
    const double planetRadiusKm = ctx.derived.planetRadiusMeters / 1000.0;
    const double spacingChord   = meanSpacingChord(ctx.grid);
    auto boundaryRadius = [&](double km) {
        return std::max(km / planetRadiusKm, kBoundaryRadiusMinSpacings * spacingChord);
    };

    // Normals averaged along the boundary, among tiles of the same plate pair, projected
    // back onto the tangent plane. Where the pair's boundary folds back within the radius
    // (a narrow plate tongue), opposing normals cancel; the radius halves until the
    // average is coherent again.
    std::vector<Vec3d> regionalNormal(boundaryCount), segmentNormal(boundaryCount);
    {
        const double rRegion  = boundaryRadius(kBoundaryTypeSmoothKm);
        const double rSegment = boundaryRadius(kRidgeSegmentKm);
        auto pairOf = [&](uint32_t i) {
            return (static_cast<uint32_t>(boundaryTiles[i].plate) << 8) | boundaryTiles[i].foreign;
        };
        const CellIndex index = buildCellIndex(rRegion, pairOf);
        auto tangentUnit = [](Vec3d v, const Vec3d& p) {
            const double d = dot3d(v.x, v.y, v.z, p.x, p.y, p.z);
            v = {v.x - d * p.x, v.y - d * p.y, v.z - d * p.z};
            const double len = length3d(v.x, v.y, v.z);
            return len > 1e-12 ? Vec3d{v.x / len, v.y / len, v.z / len} : v;
        };
        constexpr int kRadiusSteps = 3; // rRegion, rRegion/2, rRegion/4
        ctx.pool.parallelFor(0, boundaryCount, kGrainSize, [&](size_t begin, size_t end) {
            throwIfCancelled(ctx);
            for (size_t i = begin; i < end; ++i) {
                const BoundaryTile& b = boundaryTiles[i];
                std::array<Vec3d, kRadiusSteps>  sum{};
                std::array<double, kRadiusSteps> wsum{};
                Vec3d segment{};
                forEachWithin(index, rRegion, pairOf(static_cast<uint32_t>(i)), b.center, [&](uint32_t j, double d2) {
                    const Vec3d& n = boundaryTiles[j].normal;
                    double r = rRegion;
                    for (int s = 0; s < kRadiusSteps; ++s, r *= 0.5) {
                        const double w = weight(d2, std::max(r, rSegment));
                        sum[s]  = {sum[s].x + w * n.x, sum[s].y + w * n.y, sum[s].z + w * n.z};
                        wsum[s] += w;
                    }
                    const double ws = weight(d2, rSegment);
                    segment = {segment.x + ws * n.x, segment.y + ws * n.y, segment.z + ws * n.z};
                });
                int pick = kRadiusSteps - 1;
                for (int s = 0; s < kRadiusSteps; ++s) {
                    if (length3d(sum[s].x, sum[s].y, sum[s].z) >= kNormalCoherenceMin * wsum[s]) { pick = s; break; }
                }
                regionalNormal[i] = tangentUnit(sum[static_cast<size_t>(pick)], b.center);
                segmentNormal[i]  = tangentUnit(segment, b.center);
            }
        });
    }

    // Classify from the regional normal: |convergence| above kConvergentFraction of
    // |v_rel| is convergent or divergent, anything else transform. A regionally spreading
    // boundary is split into ridge segments and transform offsets by the shorter normal.
    std::vector<BType> boundaryType(boundaryCount);
    std::vector<float> boundaryConv(boundaryCount); // regional convergence, positive = approaching
    std::fill(ctx.data.boundaryType.begin(), ctx.data.boundaryType.end(), static_cast<uint8_t>(BType::None));
    for (uint32_t i = 0; i < boundaryCount; ++i) {
        const BoundaryTile& b = boundaryTiles[i];
        double oaX{}, oaY{}, oaZ{};
        double obX{}, obY{}, obZ{};
        if (b.plate < static_cast<uint8_t>(K)) {
            const auto& pl = ctx.world.plates[static_cast<size_t>(b.plate)];
            double sp = static_cast<double>(pl.angularSpeed);
            oaX = pl.eulerPole.x * sp; oaY = pl.eulerPole.y * sp; oaZ = pl.eulerPole.z * sp;
        }
        if (b.foreign < static_cast<uint8_t>(K)) {
            const auto& pl = ctx.world.plates[static_cast<size_t>(b.foreign)];
            double sp = static_cast<double>(pl.angularSpeed);
            obX = pl.eulerPole.x * sp; obY = pl.eulerPole.y * sp; obZ = pl.eulerPole.z * sp;
        }
        // v_rel = cross(omega_a, r) - cross(omega_b, r)
        double vaX{}, vaY{}, vaZ{}, vbX{}, vbY{}, vbZ{};
        cross3d(oaX, oaY, oaZ, b.center.x, b.center.y, b.center.z, vaX, vaY, vaZ);
        cross3d(obX, obY, obZ, b.center.x, b.center.y, b.center.z, vbX, vbY, vbZ);
        const double vrX = vaX - vbX, vrY = vaY - vbY, vrZ = vaZ - vbZ;
        const double vRelMag = length3d(vrX, vrY, vrZ);

        // convergence = -dot(v_rel, normal): positive → plates approaching
        const Vec3d& nr = regionalNormal[i];
        const double conv = -dot3d(vrX, vrY, vrZ, nr.x, nr.y, nr.z);
        BType type;
        if ((conv < 0.0 ? -conv : conv) <= kConvergentFraction * vRelMag) {
            type = BType::Transform;
        } else if (conv < 0.0) {
            const Vec3d& ns = segmentNormal[i];
            const double segConv = -dot3d(vrX, vrY, vrZ, ns.x, ns.y, ns.z);
            type = -segConv > kConvergentFraction * vRelMag ? BType::Divergent : BType::Transform;
        } else if (b.cont && b.foreignCont) {
            type = BType::ConvergentCC;
        } else if (b.cont != b.foreignCont) {
            type = BType::ConvergentCO;
        } else {
            type = BType::ConvergentOO;
        }
        boundaryType[i] = type;
        boundaryConv[i] = static_cast<float>(conv);
        ctx.data.boundaryType[b.tile] = static_cast<uint8_t>(type);
    }
    regionalNormal.clear(); regionalNormal.shrink_to_fit();
    segmentNormal.clear();  segmentNormal.shrink_to_fit();

    // Normalize convergence to [0,1] using the 90th-percentile magnitude of positive
    // convergence values so outlier plate pairs don't collapse all others near zero.
    float p90Conv = 1e-12f;
    {
        std::vector<float> posConv;
        posConv.reserve(boundaryCount);
        for (const float c : boundaryConv) {
            if (c > 0.0f) posConv.push_back(c);
        }
        if (!posConv.empty()) {
            std::sort(posConv.begin(), posConv.end());
            size_t idx = posConv.size() * 9 / 10;
            if (idx >= posConv.size()) idx = posConv.size() - 1;
            if (posConv[idx] > p90Conv) p90Conv = posConv[idx];
        }
    }

    // Which plate of each pair goes down: the denser along their shared boundary, so
    // oceanic crust under continental, older oceanic crust under younger. Decided once
    // per pair, so the slab side doesn't flip where a coastline crosses the boundary.
    std::vector<uint8_t> slabPlate(boundaryCount);
    {
        std::vector<double> densitySum(256 * 256, 0.0);
        std::vector<uint32_t> densityCount(256 * 256, 0u);
        for (const BoundaryTile& b : boundaryTiles) {
            const size_t key = static_cast<size_t>(b.plate) * 256 + b.foreign;
            densitySum[key] += b.cont ? 0.0 : 1.0 + ctx.data.crustAge[b.tile] / kSlabAgeScaleMyr;
            ++densityCount[key];
        }
        auto meanDensity = [&](uint8_t from, uint8_t to) {
            const size_t key = static_cast<size_t>(from) * 256 + to;
            return densityCount[key] > 0 ? densitySum[key] / densityCount[key] : 0.0;
        };
        for (uint32_t i = 0; i < boundaryCount; ++i) {
            const BoundaryTile& b = boundaryTiles[i];
            const double own = meanDensity(b.plate, b.foreign), other = meanDensity(b.foreign, b.plate);
            slabPlate[i] = own > other ? b.plate : other > own ? b.foreign : std::max(b.plate, b.foreign);
        }
    }

    ctx.reportProgress(0.13f);
    throwIfCancelled(ctx);

    // =========================================================================
    // 2. Kernel blend: each kernel family's share among the same plate pair's boundary
    //    tiles within kBoundaryBlendKm, and their convergence averaged the same way, so
    //    where the type changes along a boundary or a segment ends the kernels hand over
    //    smoothly. Between pairs, the kernels blend in step 5's gather.
    // =========================================================================

    struct BoundaryKernelInput {
        float   wCO        = 0.0f;
        float   wOO        = 0.0f;
        float   wDiv       = 0.0f;
        float   wTr        = 0.0f;
        float   conv       = 0.0f; // normalized convergence, [0,1]
        float   subducting = 0.0f; // share of the slab side here that is oceanic, so goes down
        float   axisM      = 0.0f; // trench floor at the axis: the slab row's elevation (step 5)
        uint8_t slabPlate  = 255u; // the pair's down-going plate
        uint8_t overPlate  = 255u; // the pair's other plate
    };
    // Unordered plate pair: the low byte one plate, the next the other.
    auto pairKey = [&](uint32_t i) {
        const BoundaryTile& b = boundaryTiles[i];
        return (static_cast<uint32_t>(std::min(b.plate, b.foreign)) << 8) | std::max(b.plate, b.foreign);
    };
    std::vector<BoundaryKernelInput> boundaryInput(boundaryCount); // in tile order
    {
        const double    rBlend = boundaryRadius(kBoundaryBlendKm);
        const CellIndex index  = buildCellIndex(rBlend, pairKey);
        // Whether the slab side at each boundary tile is oceanic: the tile itself on the
        // slab plate, else most of its slab-plate neighbours. Continental crust there is
        // arriving at the trench, too buoyant to go down: a collision, with no trench,
        // forearc or arc.
        std::vector<uint8_t> slabOceanic(boundaryCount, 0u);
        ctx.pool.parallelFor(0, boundaryCount, kGrainSize, [&](size_t begin, size_t end) {
            std::array<TileId, 6> nbrs{};
            auto oceanic = [&](TileId u) { return (ctx.data.flags[u] & kFlagContinentalCrust) == 0; };
            for (size_t i = begin; i < end; ++i) {
                const BoundaryTile& b = boundaryTiles[i];
                if (slabPlate[i] == b.plate) { slabOceanic[i] = oceanic(b.tile) ? 1u : 0u; continue; }
                uint32_t on = 0, all = 0;
                const uint32_t cnt = ctx.grid.neighbors(b.tile, nbrs);
                for (uint32_t k = 0; k < cnt; ++k) {
                    if (ctx.data.plateId[nbrs[k]] != slabPlate[i]) continue;
                    ++all;
                    if (oceanic(nbrs[k])) ++on;
                }
                slabOceanic[i] = on * 2u > all ? 1u : 0u;
            }
        });
        // A first estimate of the trench floor at the axis, from the slab side's floor at
        // the boundary tile (the tile itself when it is on the slab plate, else its
        // slab-plate neighbours across the boundary) and the trench; step 5 replaces it
        // with the slab row's own elevation.
        auto floorOf = [&](TileId u) {
            return (ctx.data.flags[u] & kFlagContinentalCrust) != 0
                ? kShelfBreakDepthM
                : depthAgeElevationM(static_cast<float>(ctx.data.crustAge[u]));
        };
        ctx.pool.parallelFor(0, boundaryCount, kGrainSize, [&](size_t begin, size_t end) {
            throwIfCancelled(ctx);
            std::array<TileId, 6> nbrs{};
            for (size_t i = begin; i < end; ++i) {
                double share[6]   = {};
                double conv       = 0.0;
                double subducting = 0.0;
                double total      = 0.0;
                forEachWithin(index, rBlend, pairKey(static_cast<uint32_t>(i)), boundaryTiles[i].center, [&](uint32_t j, double d2) {
                    const double w = weight(d2, rBlend);
                    double c = boundaryConv[j] / p90Conv;
                    c = c < 0.0 ? 0.0 : (c > 1.0 ? 1.0 : c);
                    share[static_cast<uint8_t>(boundaryType[j])] += w;
                    conv       += w * c;
                    subducting += w * slabOceanic[j];
                    total      += w;
                });
                BoundaryKernelInput& in = boundaryInput[i];
                in.wCO       = static_cast<float>(share[static_cast<uint8_t>(BType::ConvergentCO)] / total);
                in.wOO       = static_cast<float>(share[static_cast<uint8_t>(BType::ConvergentOO)] / total);
                in.wDiv      = static_cast<float>(share[static_cast<uint8_t>(BType::Divergent)] / total);
                in.wTr       = static_cast<float>(share[static_cast<uint8_t>(BType::Transform)] / total);
                in.conv       = static_cast<float>(conv / total);
                in.subducting = static_cast<float>(subducting / total);
                in.slabPlate = slabPlate[i];
                in.overPlate = slabPlate[i] == boundaryTiles[i].plate ? boundaryTiles[i].foreign : boundaryTiles[i].plate;
                const TileId tile = boundaryTiles[i].tile;
                float slabFloor = floorOf(tile);
                if (in.slabPlate != boundaryTiles[i].plate) {
                    float sum = 0.0f;
                    uint32_t n = 0;
                    const uint32_t cnt = ctx.grid.neighbors(tile, nbrs);
                    for (uint32_t k = 0; k < cnt; ++k) {
                        if (ctx.data.plateId[nbrs[k]] == in.slabPlate) { sum += floorOf(nbrs[k]); ++n; }
                    }
                    if (n > 0) slabFloor = sum / static_cast<float>(n);
                }
                in.axisM = std::max(kElevMinM, slabFloor + in.subducting * (in.wCO * kTrenchCOAmpM + in.wOO * kTrenchOOAmpM));
            }
        });
    }
    boundaryType.clear(); boundaryType.shrink_to_fit();
    boundaryConv.clear(); boundaryConv.shrink_to_fit();
    slabPlate.clear();    slabPlate.shrink_to_fit();

    // Kernel sigma values in tiles. A floor of ~1.3 tiles keeps the narrow features
    // (trench, axial valley) at least one tile wide at coarse n, where a physical
    // 50-60 km sigma would otherwise be sub-tile and the feature would vanish between
    // samples. It bites wherever a stage-km sigma is under 1.3 tiles (the 20 km axial
    // valley below n~1250, the 50 km transform and OO trench below n~500); at n=2048 every
    // sigma is above it.
    constexpr float kSigFloorTiles = 1.3f;
    auto sigTiles = [&](float km) { float s = kmToTiles(km); return s < kSigFloorTiles ? kSigFloorTiles : s; };
    const double spacingKm       = spacingChord * planetRadiusKm; // true km per tile spacing
    const float  sigTrenchCO     = sigTiles(kTrenchCOSigKm);
    const float  sigArcCO        = sigTiles(kArcCOSigKm);
    const float  arcGapCO        = static_cast<float>(kArcCOGapKm / spacingKm);
    const float  sigTrenchOO     = sigTiles(kTrenchOOSigKm);
    const float  sigArcOO        = sigTiles(kArcOOSigKm);
    const float  arcGapOO        = static_cast<float>(kArcOOGapKm / spacingKm);
    // Without the floor, for the extent of the island arc's cone suppression: on a coarse
    // grid a floored sigma would clear cones far beyond the arc.
    const float  arcSigOONoFloor = kmToTiles(kArcOOSigKm);
    const float  sigRift         = sigTiles(kRiftSigKm);
    const float  sigRiftShoulder = sigTiles(kRiftShoulderSigKm);
    const float  riftShoulderGap = kmToTiles(kRiftShoulderDistKm);
    const float  sigAxial        = sigTiles(kAxialValleySigKm);
    const float  sigTransform    = sigTiles(kTransformSigKm);
    // Past this distance from its boundary every kernel is exactly zero.
    const float kernelReachTiles = std::max({
        arcGapCO + kKernelCutSigmas * sigArcCO, arcGapOO + kKernelCutSigmas * sigArcOO,
        arcGapOO + 3.0f * arcSigOONoFloor, kKernelCutSigmas * sigTrenchCO, kKernelCutSigmas * sigTrenchOO,
        static_cast<float>(std::max(kForearcCOEndKm, kForearcOOEndKm) / spacingKm),
        riftShoulderGap + kKernelCutSigmas * sigRiftShoulder, kKernelCutSigmas * sigRift,
        kKernelCutSigmas * sigAxial, kKernelCutSigmas * sigTransform});
    // Across the bisector between two boundaries, their kernels blend over this much
    // difference in distance, floored like the radii so coarse grids still span tiles.
    const float bisectorBlendTiles = static_cast<float>(
        std::max(kBoundaryBlendKm / spacingKm, kBoundaryRadiusMinSpacings));

    // A tile's boundaries reach it through anchors: the boundary tiles pooled per plate pair
    // into cells kInputAnchorKm across, each with its tiles' mean position and inputs. Step
    // 5 weighs every anchor around a tile whose pair holds the tile's plate (gatherAt).
    // A weighted sum of inputs, for an anchor's mean over its tiles and a tile's over its
    // anchors.
    struct InputSum {
        double wCO = 0.0, wOO = 0.0, wDiv = 0.0, wTr = 0.0, conv = 0.0, subducting = 0.0, axisM = 0.0;
        double weight = 0.0;
        void add(const BoundaryKernelInput& in, double w) {
            wCO += w * in.wCO; wOO += w * in.wOO; wDiv += w * in.wDiv; wTr += w * in.wTr;
            conv += w * in.conv; subducting += w * in.subducting; axisM += w * in.axisM;
            weight += w;
        }
        // The weighted mean, on `pair`'s plates.
        BoundaryKernelInput mean(const BoundaryKernelInput& pair) const {
            BoundaryKernelInput out = pair;
            out.wCO        = static_cast<float>(wCO / weight);
            out.wOO        = static_cast<float>(wOO / weight);
            out.wDiv       = static_cast<float>(wDiv / weight);
            out.wTr        = static_cast<float>(wTr / weight);
            out.conv       = static_cast<float>(conv / weight);
            out.subducting = static_cast<float>(subducting / weight);
            out.axisM      = static_cast<float>(axisM / weight);
            return out;
        }
    };
    struct InputAnchor {
        Vec3d               center;   // the mean of its tiles' centres, on the sphere
        BoundaryKernelInput in;       // the mean of its tiles' inputs
        uint32_t            pair;     // pairKey
        uint32_t            rowBegin; // its tiles: [rowBegin, rowEnd) of the members
        uint32_t            rowEnd;
    };
    std::vector<InputAnchor> anchors;
    // The anchors' tiles, grouped by anchor: centre, and whether on the pair's slab plate.
    std::vector<Vec3d>       memberCenter(boundaryCount);
    std::vector<uint8_t>     memberOnSlab(boundaryCount);
    std::vector<uint32_t>    rowMember(boundaryCount); // boundary tile -> its member slot
    std::vector<uint32_t>    rowAnchor(boundaryCount); // boundary tile -> its anchor
    float                    anchorExtent = 0.0f; // farthest a member sits from its anchor, tile spacings
    // A tile's gather weighs anchors over a band of distance past the nearest, kInputBandMinKm
    // at the boundary and widening with the distance up to the bisector blend.
    const float bandMinTiles = static_cast<float>(std::max(kInputBandMinKm / spacingKm, kBoundaryRadiusMinSpacings));
    auto bandAt = [&](float d) { return std::min(bisectorBlendTiles, bandMinTiles + d); };
    // How far from a tile its gather looks, given its nearest boundary tile at d1 (spacings):
    // its own plate's boundary is within two spacings more, the band past that, the
    // anchors' extent either side, and the trench floor's band and a row beyond.
    auto gatherReachAt = [&](float d1) {
        return d1 + 2.0f + bandAt(d1 + 2.0f) + 3.0f * anchorExtent + kFloorBandSpacings + 2.0f;
    };
    // Per anchor, every anchor a gather starting from it can need, nearest first:
    // (distance in tile spacings, anchor).
    std::vector<uint32_t>                   neighbourStart;
    std::vector<std::pair<float, uint32_t>> neighbours;
    {
        const double    cell  = std::max(kInputAnchorKm / planetRadiusKm, spacingChord);
        const CellIndex cells = buildCellIndex(cell, pairKey);
        for (size_t k = 0; k < cells.size();) {
            size_t   end = k;
            Vec3d    sum{};
            InputSum in;
            for (; end < cells.size() && cells[end].first == cells[k].first; ++end) {
                const uint32_t i = cells[end].second;
                const Vec3d&   c = boundaryTiles[i].center;
                sum = {sum.x + c.x, sum.y + c.y, sum.z + c.z};
                in.add(boundaryInput[i], 1.0);
                memberCenter[end] = c;
                memberOnSlab[end] = boundaryTiles[i].plate == boundaryInput[i].slabPlate ? 1u : 0u;
                rowMember[i]      = static_cast<uint32_t>(end);
                rowAnchor[i]      = static_cast<uint32_t>(anchors.size());
            }
            const double len = length3d(sum.x, sum.y, sum.z);
            anchors.push_back({{sum.x / len, sum.y / len, sum.z / len},
                               in.mean(boundaryInput[cells[k].second]),
                               static_cast<uint32_t>(cells[k].first >> 48),
                               static_cast<uint32_t>(k), static_cast<uint32_t>(end)});
            k = end;
        }
        auto chordTo = [](const Vec3d& a, const Vec3d& b) {
            const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            return foundation::det_math::sqrt(dx * dx + dy * dy + dz * dz);
        };
        double extent = 0.0;
        for (const InputAnchor& a : anchors) {
            for (uint32_t m = a.rowBegin; m < a.rowEnd; ++m) extent = std::max(extent, chordTo(memberCenter[m], a.center));
        }
        anchorExtent = static_cast<float>(extent / spacingChord);
        // A gather starts from the anchor of the tile's nearest boundary tile, at most kernel
        // reach and the extent from the tile, and looks gatherReachAt(reach) around the tile.
        const double radius = (kernelReachTiles + anchorExtent + gatherReachAt(kernelReachTiles)) * spacingChord;
        const uint32_t anchorCount = static_cast<uint32_t>(anchors.size());
        CellIndex anchorCells(anchorCount);
        for (uint32_t a = 0; a < anchorCount; ++a) {
            const Vec3d& p = anchors[a].center;
            anchorCells[a] = {cellKey(0u, cellOf(p.x, radius), cellOf(p.y, radius), cellOf(p.z, radius)), a};
        }
        std::sort(anchorCells.begin(), anchorCells.end());
        std::vector<std::vector<std::pair<float, uint32_t>>> lists(anchorCount);
        ctx.pool.parallelFor(0, anchorCount, kGrainSize, [&](size_t begin, size_t end) {
            throwIfCancelled(ctx);
            for (size_t a = begin; a < end; ++a) {
                const Vec3d&  p = anchors[a].center;
                const int64_t x = cellOf(p.x, radius), y = cellOf(p.y, radius), z = cellOf(p.z, radius);
                auto& list = lists[a];
                for (int64_t dx = -1; dx <= 1; ++dx)
                    for (int64_t dy = -1; dy <= 1; ++dy)
                        for (int64_t dz = -1; dz <= 1; ++dz) {
                            const uint64_t key = cellKey(0u, x + dx, y + dy, z + dz);
                            auto it = std::lower_bound(anchorCells.begin(), anchorCells.end(), std::make_pair(key, 0u));
                            for (; it != anchorCells.end() && it->first == key; ++it) {
                                if (it->second == a) continue;
                                const double d = chordTo(p, anchors[it->second].center);
                                if (d < radius) list.push_back({static_cast<float>(d / spacingChord), it->second});
                            }
                        }
                std::sort(list.begin(), list.end());
            }
        });
        neighbourStart.resize(static_cast<size_t>(anchorCount) + 1);
        neighbourStart[0] = 0;
        for (uint32_t a = 0; a < anchorCount; ++a) {
            neighbourStart[a + 1] = neighbourStart[a] + static_cast<uint32_t>(lists[a].size());
        }
        neighbours.reserve(neighbourStart[anchorCount]);
        for (auto& list : lists) neighbours.insert(neighbours.end(), list.begin(), list.end());
    }

    // =========================================================================
    // 3. Crust-edge distance (for continental shelf ramp), in tile spacings.
    //    Straight-line: the coastline is an iso-contour of this field, so its contours
    //    must be round.
    // =========================================================================

    // Every tile's centre, for this step's and the next's distance passes.
    std::vector<TileCenterF> centers = tileCentersF(ctx.grid, ctx.pool);
    std::vector<float> crustEdgeDist;
    {
        std::vector<uint8_t> isEdge(N, 0u);
        ctx.pool.parallelFor(0, N, kGrainSize, [&](size_t begin, size_t end) {
            std::array<TileId, 6> nbrs{};
            for (size_t t = begin; t < end; ++t) {
                bool isCrust = (ctx.data.flags[t] & kFlagContinentalCrust) != 0;
                uint32_t cnt = ctx.grid.neighbors(static_cast<TileId>(t), nbrs);
                for (uint32_t k = 0; k < cnt; ++k) {
                    bool nbCrust = (ctx.data.flags[nbrs[k]] & kFlagContinentalCrust) != 0;
                    if (nbCrust != isCrust) { isEdge[t] = 1u; break; }
                }
            }
        });
        std::vector<TileId> edgeTiles;
        edgeTiles.reserve(static_cast<size_t>(std::count(isEdge.begin(), isEdge.end(), uint8_t{1})));
        for (uint32_t t = 0; t < N; ++t) {
            if (isEdge[t] != 0u) edgeTiles.push_back(t);
        }
        // Past the wider of the shelf and the slope the profile saturates, so only that
        // band (plus a tile) needs a real distance.
        crustEdgeDist = seedDistance({
            .grid     = ctx.grid,
            .centers  = centers,
            .seeds    = edgeTiles,
            .cancel   = ctx.cancelRequested,
            .maxTiles = kmToTiles(std::max(kShelfWidthKm, kSlopeWidthKm)) + 1.0f,
        }).distTiles;
        // That is the distance to the edge rows' centres; the edge itself runs between the
        // two rows, half a spacing farther on, so neither row sits exactly on the break.
        for (float& d : crustEdgeDist) d += 0.5f;
    }

    ctx.reportProgress(0.20f);
    throwIfCancelled(ctx);

    // =========================================================================
    // 4. Boundary distance: straight-line distance (tile spacings) from every tile to its
    //    nearest boundary tile, where its gather (step 5) starts.
    // =========================================================================

    std::vector<TileId>   boundaryRows(boundaryCount); // seed index -> tile
    std::vector<uint32_t> nearestRow;
    {
        for (uint32_t i = 0; i < boundaryCount; ++i) boundaryRows[i] = boundaryTiles[i].tile;
        boundaryTiles.clear(); boundaryTiles.shrink_to_fit();

        SeedDistanceResult boundary = seedDistance({
            .grid    = ctx.grid,
            .centers = centers,
            .seeds   = boundaryRows,
            .cancel  = ctx.cancelRequested,
        });
        for (uint32_t t = 0; t < N; ++t) {
            const float d = boundary.distTiles[t];
            ctx.data.boundaryDistance[t] = d >= 65535.0f ? uint16_t(65535) : static_cast<uint16_t>(d + 0.5f);
        }
        nearestRow = std::move(boundary.nearestSeed);
    }
    centers.clear(); centers.shrink_to_fit();

    ctx.reportProgress(0.32f);
    throwIfCancelled(ctx);

    // =========================================================================
    // 5. Elevation synthesis: the slab-plate boundary tiles first, for each boundary's
    //    trench floor at the axis, then every tile in parallel. A pure function of
    //    (tile id, coarse fields, full-res fields, seeds).
    // =========================================================================

    // The trench floor at each slab-plate member: its elevation once the passes below have
    // run, the first estimate (step 2) until then.
    std::vector<float> memberFloor(boundaryCount);
    for (uint32_t i = 0; i < boundaryCount; ++i) memberFloor[rowMember[i]] = boundaryInput[i].axisM;
    // Past this distance (tile spacings) from the axis the inner wall is gone, so no tile
    // there reads the floor at the axis.
    const float wallReachTiles = static_cast<float>(kKernelCutSigmas * kInnerWallSigKm / spacingKm);

    // One plate pair around a tile: its weight in the tile's blend, its inputs there, its
    // distance (to the boundary itself, half a spacing past its nearest member), and the
    // trench floor read from its slab-plate members nearest the tile.
    struct NearPair {
        uint32_t            pair     = 0;
        uint32_t            anchor   = 0; // one of its anchors, for the pair's plates
        InputSum            sum;
        float               rowDist  = std::numeric_limits<float>::infinity(); // to its nearest member
        float               slabDist = std::numeric_limits<float>::infinity(); // to its nearest slab-plate member
        double              floorSum = 0.0, floorWeight = 0.0;
        BoundaryKernelInput in;
        float               dist   = 0.0f;
        float               floorM = 0.0f;
    };
    // The pairs around tile t (centre p) whose boundaries hold its plate, none past kernel
    // reach. Every anchor of such a pair weighs 1 - smoothstep((d - dNearest) / band), d its
    // distance from the tile and dNearest the nearest one's; a pair's weight is its anchors'
    // sum and its inputs their weighted mean, so one partition of unity over the anchors
    // gives both the inputs along a boundary and the blend between boundaries. The weights
    // are continuous in the tile's position and reach exactly 0 one band past the nearest,
    // so an anchor joins or leaves only at weight 0, and nothing steps where the nearest
    // boundary tile jumps, along one boundary or from one to another. The band sets how far
    // a hand-over spreads: at the boundary kInputBandMinKm, so where two boundaries meet a
    // forearc and a trench several km apart in height hand over without a cliff, and wider
    // with the distance, since across a bend the nearest tile jumps along strike by about the
    // distance; it stops at kBoundaryBlendKm. Each pair's kernels still run at its own exact
    // distance, so the band moves no feature off its boundary.
    auto gatherAt = [&](TileId t, const Vec3d& p, uint8_t plate) -> const std::vector<NearPair>& {
        thread_local std::vector<NearPair>                           pairs;
        thread_local std::vector<std::pair<float, uint32_t>>         nearAnchors;
        thread_local std::vector<std::pair<uint32_t, std::pair<float, uint32_t>>> floorMembers;
        pairs.clear();
        const uint32_t row = nearestRow[t];
        if (row == kNoSeed) return pairs;
        auto distTo = [&](const Vec3d& c) {
            const double dx = p.x - c.x, dy = p.y - c.y, dz = p.z - c.z;
            return static_cast<float>(foundation::det_math::sqrt(dx * dx + dy * dy + dz * dz) / spacingChord);
        };
        // Every pair's distance is at least the nearest boundary tile's, and every kernel is
        // exactly zero past reach.
        const float d1 = distTo(memberCenter[rowMember[row]]);
        if (d1 + 0.5f >= kernelReachTiles) return pairs;
        auto holds = [&](uint32_t pair) { return (pair & 0xFFu) == plate || (pair >> 8) == plate; };
        const float    reach = gatherReachAt(d1);
        const uint32_t a0    = rowAnchor[row];
        const float    d0    = distTo(anchors[a0].center);
        nearAnchors.clear();
        if (holds(anchors[a0].pair) && d0 < reach) nearAnchors.push_back({d0, a0});
        for (uint32_t k = neighbourStart[a0]; k < neighbourStart[a0 + 1]; ++k) {
            if (neighbours[k].first >= d0 + reach) break;
            const uint32_t a = neighbours[k].second;
            if (!holds(anchors[a].pair)) continue;
            const float d = distTo(anchors[a].center);
            if (d < reach) nearAnchors.push_back({d, a});
        }
        if (nearAnchors.empty()) return pairs;
        std::sort(nearAnchors.begin(), nearAnchors.end());

        // Each pair's nearest member, nearest slab-plate member and the slab-plate members
        // within the floor band of it, from its anchors nearest first: no member of an anchor
        // is nearer the tile than its distance less the extent.
        const bool wantFloor = d1 + 0.5f < wallReachTiles;
        floorMembers.clear();
        for (const auto& [d, a] : nearAnchors) {
            const uint32_t pair = anchors[a].pair;
            uint32_t pi = 0;
            while (pi < pairs.size() && pairs[pi].pair != pair) ++pi;
            if (pi == pairs.size()) {
                pairs.push_back({});
                pairs[pi].pair   = pair;
                pairs[pi].anchor = a;
            }
            NearPair&   np    = pairs[pi];
            const float lower = d - anchorExtent;
            if (lower >= np.rowDist && (!wantFloor || lower >= np.slabDist + kFloorBandSpacings)) continue;
            for (uint32_t m = anchors[a].rowBegin; m < anchors[a].rowEnd; ++m) {
                const float dm = distTo(memberCenter[m]);
                np.rowDist = std::min(np.rowDist, dm);
                if (wantFloor && memberOnSlab[m] != 0u) {
                    np.slabDist = std::min(np.slabDist, dm);
                    floorMembers.push_back({pi, {dm, m}});
                }
            }
        }
        for (const auto& [pi, member] : floorMembers) {
            NearPair&   np = pairs[pi];
            const float x  = (member.first - np.slabDist) / kFloorBandSpacings;
            if (x >= 1.0f) continue;
            const double w = 1.0 - smoothstep01(x);
            np.floorSum    += w * memberFloor[member.second];
            np.floorWeight += w;
        }

        float dNearestRow = std::numeric_limits<float>::infinity();
        for (const NearPair& np : pairs) dNearestRow = std::min(dNearestRow, np.rowDist);
        const float band     = bandAt(dNearestRow);
        const float dNearest = nearAnchors.front().first;
        for (const auto& [d, a] : nearAnchors) {
            const float x = (d - dNearest) / band;
            if (x >= 1.0f) break;
            uint32_t pi = 0;
            while (pairs[pi].pair != anchors[a].pair) ++pi;
            pairs[pi].sum.add(anchors[a].in, 1.0 - smoothstep01(x));
        }
        for (NearPair& np : pairs) {
            if (np.sum.weight <= 0.0) continue;
            np.in     = np.sum.mean(anchors[np.anchor].in);
            np.dist   = np.rowDist + 0.5f;
            np.floorM = np.floorWeight > 0.0 ? static_cast<float>(np.floorSum / np.floorWeight) : np.in.axisM;
        }
        return pairs;
    };

    // The elevation of tile t: this section's recipe, start to finish.
    auto tileElevation = [&](size_t t) {
        const bool    isCont   = (ctx.data.flags[t] & kFlagContinentalCrust) != 0;
        const uint8_t plate    = ctx.data.plateId[t];
        // Signed crust-edge distance, stage km: positive inland, negative seaward.
        const float signedEdgeKm = (isCont ? 1.0f : -1.0f) * crustEdgeDist[t] * tileWidthKm;

        Vec3d  ctr = ctx.grid.tileCenter(static_cast<uint32_t>(t));
        float  cx  = static_cast<float>(ctr.x);
        float  cy  = static_cast<float>(ctr.y);
        float  cz  = static_cast<float>(ctr.z);

        // The boundaries around the tile, each by its weight; everything they add below is
        // their weighted mean.
        const std::vector<NearPair>& nearby = gatherAt(static_cast<TileId>(t), ctr, plate);
        double nearWeight = 0.0;
        for (const NearPair& np : nearby) nearWeight += np.sum.weight;

        // ---- Sample coarse fields through the same warp CrustStage used ----
        const tectonics::WarpResult w = tectonics::warpedCoarseDir(
            ctr, coarseGrid, kWarpAmp, seedWX, seedWY, seedWZ);
        const float thicknessKm = tectonics::smoothSampleAt(
            w.point, w.tile, coarseGrid,
            [&](TileId tile) { return hist->thicknessKm[tile]; });
        const float orogenyIntensity = tectonics::smoothSampleAt(
            w.point, w.tile, coarseGrid,
            [&](TileId tile) { return hist->orogenyIntensity[tile]; });
        const float volcanism = tectonics::smoothSampleAt(
            w.point, w.tile, coarseGrid,
            [&](TileId tile) { return hist->volcanism[tile]; });

        // Full-res fields read directly (CrustStage wrote them).
        const float crustAgeMyr   = static_cast<float>(ctx.data.crustAge[t]);
        const uint16_t orogenyAgeRaw = ctx.data.orogenyAge[t];
        const bool hasOrogeny     = orogenyAgeRaw != 65535u;
        const float orogenyAgeMyr = hasOrogeny ? static_cast<float>(orogenyAgeRaw) : 1e9f;

        float elev;

        if (isCont) {
            // -- Airy isostasy platform from crustal thickness (knee model) --
            float thkClamped = thicknessKm;
            if (thkClamped > kIsostasyMaxThicknessKm) thkClamped = kIsostasyMaxThicknessKm;
            float iso = kIsostasyPlatformM;
            if (thkClamped > kIsostasyKneeThicknessKm) {
                iso += (thkClamped - kIsostasyKneeThicknessKm) * kIsostasyMPerKm;
            }

            const float signedKm = signedEdgeKm;

            // Shelf profile: flat shelf → break → steep slope → abyssal.
            // For the continental block the abyssal reference is the shelf-break
            // depth (the slope below the break is on oceanic tiles; the oceanic
            // block handles signedKm < 0). Supply kShelfBreakDepthM as abyssalElev
            // so the continental block just governs the +signedKm (shelf) region.
            float platform = shelfElevationForSignedEdge(signedKm, iso, kShelfBreakDepthM);

            // -- Orogeny-aged ridged belt detail --
            // The belt is the primary tall-relief source and it TRACKS THE OROGENY LINE.
            // beltCore gates the tall belt to the thickened CORE (thickness >
            // kBeltThicknessMinKm). It is a soft multiplier with a floor, not a hard gate:
            // a strong-intensity belt segment on only-moderately-thickened crust still rises
            // to kBeltCoreFloor of full amplitude, so the belt follows the thin linear
            // orogeny field continuously. The 2-ring thicken band keeps the core narrow; the
            // floor keeps the gate from severing a belt where thickness dips a hair below the
            // knee mid-ridge (continuity for elongation).
            float beltCoreRaw = (thicknessKm - kBeltThicknessMinKm) /
                                (kBeltThicknessFullKm - kBeltThicknessMinKm);
            if (beltCoreRaw < 0.0f) beltCoreRaw = 0.0f;
            if (beltCoreRaw > 1.0f) beltCoreRaw = 1.0f;
            constexpr float kBeltCoreFloor = 0.35f;
            float beltCore = kBeltCoreFloor + (1.0f - kBeltCoreFloor) * beltCoreRaw;

            // Sharpen the (smooth-sampled) intensity to its high spine so the belt stays a
            // thin line instead of lifting the whole bump above the mountain threshold.
            const float intensitySharp = smoothstep01((orogenyIntensity - kBeltIntensityKnee) /
                                                      (kBeltIntensityFull - kBeltIntensityKnee));

            float beltDetail = 0.0f;
            if (intensitySharp > 0.0f && beltCore > 0.0f) {
                float ageDecay = static_cast<float>(
                    foundation::det_math::exp(-static_cast<double>(orogenyAgeMyr) /
                                              kBeltAgeDecayTauMyr));
                if (ageDecay < kBeltAgeDecayFloor) ageDecay = kBeltAgeDecayFloor;

                float along = kBeltAlongBase + kBeltAlongAmp *
                    foundation::fractalNoise3(cx * kBeltAlongFreq, cy * kBeltAlongFreq,
                                              cz * kBeltAlongFreq, seedFractal + 11u,
                                              4, 2.0f, 0.5f);
                if (along < 0.0f) along = 0.0f;

                float beltAmp = kBeltBaseAmpM * intensitySharp * beltCore * ageDecay * along;
                float ridged = foundation::ridgedNoise3(
                    cx * kBeltRidgeFreq, cy * kBeltRidgeFreq, cz * kBeltRidgeFreq,
                    seedRidged, kBeltRidgeOctaves, 2.0f, 0.5f);
                // Split the belt into a positive LIFT (the orogeny line rises as the high-
                // tile set, lifting PCA elongation) and a recentered ridged crest/valley
                // TEXTURE on top (sharp range, not a smooth welt). The lift is what makes
                // the thin orogeny line read as a mountain range; the texture gives it
                // crests and passes. Ridged noise is ~[0,1].
                float liftN = foundation::fractalNoise3(
                    cx * kBeltLiftFreq, cy * kBeltLiftFreq, cz * kBeltLiftFreq,
                    seedFractal + 23u, 3, 2.0f, 0.5f);
                float liftT = 0.5f + 0.5f * liftN;
                if (liftT < 0.0f) liftT = 0.0f;
                if (liftT > 1.0f) liftT = 1.0f;
                float liftFrac = kBeltLiftFracMin + (kBeltLiftFracMax - kBeltLiftFracMin) * liftT;
                float lift    = liftFrac * beltAmp;
                float texture = (1.0f - liftFrac) * beltAmp * (2.0f * ridged - 1.0f);
                beltDetail = lift + texture;
            }

            // -- Hills (broad relief, amplified near recent orogeny) --
            // Suppressed on the shallow shelf so fractal noise can't push
            // -120m shelf tiles above sea level. Hill amplitude ramps from
            // 0 at the crust edge to full at the inner shelf edge, then
            // stays full across the continental interior.
            const float hillShelfFade = smoothstep01(signedKm / kShelfWidthKm);

            float proximityToOrogeny = 0.0f;
            if (hasOrogeny) {
                // closer/younger orogeny -> more foothill relief
                float ad = static_cast<float>(
                    foundation::det_math::exp(-static_cast<double>(orogenyAgeMyr) /
                                              kFoothillDecayTauMyr));
                proximityToOrogeny = orogenyIntensity * ad;
            }
            float hills = foundation::fractalNoise3(
                              cx * kHillFreq, cy * kHillFreq, cz * kHillFreq,
                              seedFractal, kHillOctaves, 2.0f, 0.5f)
                          * (kHillBaseAmpM + kHillOrogenyAmpM * proximityToOrogeny)
                          * hillShelfFade;

            elev = platform + shelfRelief(signedKm, beltDetail + hills);
        } else {
            // -- Oceanic depth-age law (GDH1 plate-cooling form) --
            const float zDepthAge = depthAgeElevationM(crustAgeMyr);

            // Continental slope profile from the crust edge; its foot meets this tile's
            // depth-age depth. Abyssal relief fades up the slope as the shelf hills fade
            // toward the break: at full amplitude it lifts the break tiles above sea
            // level, a land rim outside the flooded shelf.
            float z = shelfElevationForSignedEdge(signedEdgeKm, kShelfBreakDepthM, zDepthAge);
            float reliefSlopeFade = smoothstep01(-signedEdgeKm / kSlopeWidthKm);

            // Neither a rifted margin nor a slab going down a trench stands at the shelf
            // break: the floor returns to its depth-age depth by the rift and trench
            // kernels' own falloff, each by its share.
            double riftSum = 0.0, slabTrenchSum = 0.0;
            for (const NearPair& np : nearby) {
                if (np.sum.weight <= 0.0) continue;
                const BoundaryKernelInput& bi = np.in;
                riftSum += np.sum.weight * bi.wDiv * cutGaussian(np.dist, sigRift);
                if (plate == bi.slabPlate) {
                    slabTrenchSum += np.sum.weight * bi.subducting *
                                     (bi.wCO * cutGaussian(np.dist, sigTrenchCO) + bi.wOO * cutGaussian(np.dist, sigTrenchOO));
                }
            }
            const float riftW       = nearWeight > 0.0 ? static_cast<float>(riftSum / nearWeight) : 0.0f;
            const float slabTrenchW = nearWeight > 0.0 ? static_cast<float>(slabTrenchSum / nearWeight) : 0.0f;
            const float depthAgeW   = 1.0f - (1.0f - riftW) * (1.0f - slabTrenchW);
            z += depthAgeW * (zDepthAge - z);
            reliefSlopeFade += depthAgeW * (1.0f - reliefSlopeFade);

            // Abyssal hills + a longer swell.
            float hills = foundation::fractalNoise3(
                              cx * kAbyssalHillFreq, cy * kAbyssalHillFreq, cz * kAbyssalHillFreq,
                              seedFractal + 3u, kAbyssalHillOctaves, 2.0f, 0.5f) * kAbyssalHillAmpM;
            float swell = foundation::fractalNoise3(
                              cx * kAbyssalSwellFreq, cy * kAbyssalSwellFreq, cz * kAbyssalSwellFreq,
                              seedFractal + 5u, kAbyssalSwellOctaves, 2.0f, 0.5f) * kAbyssalSwellAmpM;
            elev = z + (hills + swell) * reliefSlopeFade;
            if (elev < kOceanMaxDepthM) elev = kOceanMaxDepthM;
        }

        // ---- Active-boundary kernels (narrow, near the classified boundary) ----
        // Each family scaled by its share along the boundary (step 2), at the distance
        // from the boundary itself. The side is the tile's own plate: the slab plate of
        // the boundary's pair carries the trench, the other the forearc and arc, so the
        // side only changes at the plate boundary.
        // Continental crust on the slab plate is too buoyant to follow it down: the
        // trench fades out across its slope (stage km) inland of the crust edge, and where
        // the slab side at the boundary is continental, subduction gives way to collision.
        // The forearc pulls the overriding plate toward the margin profile rather than
        // adding to it, so the coast lands at the same distance whatever the platform;
        // its inner wall starts from the slab side's own floor nearest the tile (floorM),
        // so the deepest point is the axis.
        const float trenchCrustFade = 1.0f - smoothstep01(signedEdgeKm / kSlopeWidthKm);
        // Rift or axial valley by crust, eased across the crust edge over the slope's width.
        const float riftCrust = smoothstep01(0.5f + 0.5f * signedEdgeKm / kSlopeWidthKm);
        // Per-tile noises, drawn once whichever boundaries ask.
        float islandNoise = 0.0f, shearNoise = 0.0f;
        bool  haveIsland = false, haveShear = false;
        auto island = [&] {
            if (!haveIsland) {
                islandNoise = smoothstep01((foundation::fractalNoise3(cx * kArcIslandFreq, cy * kArcIslandFreq,
                                                                      cz * kArcIslandFreq, seedRidged + 1u, 3, 2.0f, 0.5f) -
                                            kArcIslandNoiseLo) / (kArcIslandNoiseHi - kArcIslandNoiseLo));
                haveIsland = true;
            }
            return islandNoise;
        };
        auto shear = [&] {
            if (!haveShear) {
                shearNoise = foundation::fractalNoise3(
                    cx * kTransformShearFreq, cy * kTransformShearFreq, cz * kTransformShearFreq,
                    seedFractal + 7u, kTransformShearOctaves, 2.0f, 0.5f);
                haveShear = true;
            }
            return shearNoise;
        };
        struct KernelOut {
            float kernel       = 0.0f;
            float coneSuppress = 0.0f; // share of the volcanic cones taken off (below)
        };
        auto boundaryKernel = [&](const BoundaryKernelInput& bi, float distT, float floorM) {
            KernelOut out;
            const bool onSlab = plate == bi.slabPlate;
            const double distKm = static_cast<double>(distT) * spacingKm;
            // Subduction only where the slab side is oceanic; the rest is collision.
            const float wCO = bi.wCO * bi.subducting;
            const float wOO = bi.wOO * bi.subducting;
            // The forearc pull, summed over the subduction families by share: the
            // overriding share, the share-weighted pull weight and pull, and the
            // share-weighted reach of the cone suppression.
            float pullShare = 0.0f, pullWeight = 0.0f, pull = 0.0f, coneWeight = 0.0f;
            if (wCO > 0.0f) {
                if (onSlab) {
                    out.kernel += wCO * kTrenchCOAmpM * cutGaussian(distT, sigTrenchCO) * trenchCrustFade;
                } else {
                    const ForearcPull fa = forearcPull({.distKm = distKm, .axisM = floorM, .regionM = bi.axisM,
                                                        .forearcM = kForearcCODepthM, .fullKm = kForearcCOFullKm,
                                                        .endKm = kForearcCOEndKm});
                    out.kernel += wCO * kArcCOAmpM * bi.conv * cutGaussian(distT - arcGapCO, sigArcCO);
                    pullShare  += wCO;
                    pullWeight += wCO * fa.weight;
                    pull       += wCO * fa.weight * (fa.targetM - elev);
                    coneWeight += wCO * fa.weight;
                }
            }
            if (wOO > 0.0f) {
                if (onSlab) {
                    out.kernel += wOO * kTrenchOOAmpM * cutGaussian(distT, sigTrenchOO) * trenchCrustFade;
                } else {
                    const ForearcPull fa = forearcPull({.distKm = distKm, .axisM = floorM, .regionM = bi.axisM,
                                                        .forearcM = kForearcOODepthM, .fullKm = kForearcOOFullKm,
                                                        .endKm = kForearcOOEndKm});
                    const float arcAmp = kArcOOAmpM * (kOOConvBase + kOOConvAmp * bi.conv) * island();
                    out.kernel += wOO * arcAmp * cutGaussian(distT - arcGapOO, sigArcOO);
                    pullShare  += wOO;
                    pullWeight += wOO * fa.weight;
                    pull       += wOO * fa.weight * (fa.targetM - elev);
                    // An island arc's volcanoes are its islands, so the cones stand down
                    // across the arc as well, out to two of its sigmas past the axis.
                    coneWeight += wOO * (1.0f - smoothstep01((distT - arcGapOO - 2.0f * arcSigOONoFloor) / arcSigOONoFloor));
                }
            }
            // The forearc stands in full wherever subduction holds kForearcFullShare of
            // the boundary nearby, and hands over only as that share runs out along strike.
            if (pullShare > 0.0f) {
                const float gate = smoothstep01(pullShare / kForearcFullShare) / pullShare; // per unit share
                out.kernel      += gate * pull;
                out.coneSuppress = gate * coneWeight;
            }
            if (bi.wDiv > 0.0f) {
                // On oceanic crust the ridge crest is already in the depth-age law (age 0
                // at the spreading center), so only a small axial valley; on continental
                // crust a rift valley with shoulders.
                const float axial = kAxialValleyM * cutGaussian(distT, sigAxial);
                const float rift  = kRiftAmpM * cutGaussian(distT, sigRift) +
                                    kRiftShoulderM * cutGaussian(distT - riftShoulderGap, sigRiftShoulder);
                out.kernel += bi.wDiv * (axial + riftCrust * (rift - axial));
            }
            if (bi.wTr > 0.0f) {
                out.kernel += bi.wTr * kTransformAmpM * shear() * cutGaussian(distT, sigTransform);
            }
            return out;
        };
        KernelOut kernel;
        for (const NearPair& np : nearby) {
            if (np.sum.weight <= 0.0) continue;
            const KernelOut k = boundaryKernel(np.in, np.dist, np.floorM);
            const float     w = static_cast<float>(np.sum.weight / nearWeight);
            kernel.kernel       += w * k.kernel;
            kernel.coneSuppress += w * k.coneSuppress;
        }
        // Continental collision (CC) adds no kernel: its relief is the isostatic
        // platform and the ridged belts.
        elev += kernel.kernel;

        // ---- Hotspot / arc volcanism cones (seamount/island lines) ----
        if (volcanism > kVolcanismMin) {
            float cone = foundation::ridgedNoise3(
                cx * kVolcanismConeFreq, cy * kVolcanismConeFreq, cz * kVolcanismConeFreq,
                seedRidged + 5u, 4, 2.0f, 0.5f);
            // Sharpen so cones are isolated peaks, not a continuous mat.
            cone = cone * cone;
            float volc = (volcanism - kVolcanismMin) * cone * kVolcanismConeAmpM;
            if (volc > kVolcanismCapM) volc = kVolcanismCapM;
            // None on the forearc, where the slab is still too shallow to melt the mantle
            // above it, nor on an island arc, whose volcanoes are its islands.
            volc *= 1.0f - kernel.coneSuppress;
            elev += volc;
        }

        if (elev < kElevMinM) elev = kElevMinM;
        if (elev > kElevMaxM) elev = kElevMaxM;
        return elev;
    };

    // Each boundary's trench floor at the axis, where the overriding plate's inner wall
    // starts, is the slab plate's own elevation there, so the two sides meet with the axis
    // deepest: the slab-plate members' elevations, read by the overriding side's gathers
    // (above), and their mean in each anchor for the floor along the boundary. A slab-plate
    // member reads no floor of its own pair, but at a triple junction it can be overriding
    // for another pair and read that one's, which can read its own in turn. The passes
    // repeat until those settle: after three, a fourth would move under 0.1% of the
    // slab-plate members, by tens of metres at most.
    {
        constexpr int kFloorPasses = 3;
        std::vector<uint32_t> slabRows;
        for (uint32_t i = 0; i < boundaryCount; ++i) {
            if (memberOnSlab[rowMember[i]] != 0u) slabRows.push_back(i);
        }
        std::vector<float> settled(slabRows.size());
        for (int pass = 0; pass < kFloorPasses; ++pass) {
            ctx.pool.parallelFor(0, slabRows.size(), kGrainSize, [&](size_t begin, size_t end) {
                throwIfCancelled(ctx);
                for (size_t k = begin; k < end; ++k) settled[k] = tileElevation(boundaryRows[slabRows[k]]);
            });
            for (size_t k = 0; k < slabRows.size(); ++k) memberFloor[rowMember[slabRows[k]]] = settled[k];
            for (InputAnchor& a : anchors) {
                double   sum = 0.0;
                uint32_t n   = 0;
                for (uint32_t m = a.rowBegin; m < a.rowEnd; ++m) {
                    if (memberOnSlab[m] == 0u) continue;
                    sum += memberFloor[m];
                    ++n;
                }
                if (n > 0) a.in.axisM = static_cast<float>(sum / n);
            }
        }
    }
    boundaryInput.clear(); boundaryInput.shrink_to_fit();

    ctx.pool.parallelFor(0, N, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        for (size_t t = begin; t < end; ++t) ctx.data.elevation[t] = tileElevation(t);
        ctx.reportProgress(0.32f + static_cast<float>(end) / static_cast<float>(N) * 0.56f);
    });

    // Free transient arrays
    nearestRow.clear();     nearestRow.shrink_to_fit();
    anchors.clear();        anchors.shrink_to_fit();
    memberCenter.clear();   memberCenter.shrink_to_fit();
    memberOnSlab.clear();   memberOnSlab.shrink_to_fit();
    memberFloor.clear();    memberFloor.shrink_to_fit();
    rowMember.clear();      rowMember.shrink_to_fit();
    rowAnchor.clear();      rowAnchor.shrink_to_fit();
    neighbourStart.clear(); neighbourStart.shrink_to_fit();
    neighbours.clear();     neighbours.shrink_to_fit();
    crustEdgeDist.clear();  crustEdgeDist.shrink_to_fit();

    ctx.reportProgress(0.90f);
    throwIfCancelled(ctx);

    // =========================================================================
    // 6. Sea level — histogram quantile at waterAmount.
    //    waterAmount fraction of tiles should lie BELOW sea level.
    // =========================================================================

    {
        constexpr int   kBins    = 4096;
        constexpr float kMinElev = -14000.0f;
        constexpr float kMaxElev =  12000.0f;
        constexpr float kBinWidth = (kMaxElev - kMinElev) / kBins;

        std::vector<uint32_t> hist(static_cast<size_t>(kBins), 0u);
        for (uint32_t t = 0; t < N; ++t) {
            float e   = ctx.data.elevation[t];
            int   bin = static_cast<int>((e - kMinElev) / kBinWidth);
            if (bin < 0)      bin = 0;
            if (bin >= kBins) bin = kBins - 1;
            hist[static_cast<size_t>(bin)]++;
        }

        double target = ctx.params.waterAmount * static_cast<double>(N);
        double cumul  = 0.0;
        int seaBin    = kBins - 1;
        for (int b = 0; b < kBins; ++b) {
            cumul += static_cast<double>(hist[static_cast<size_t>(b)]);
            if (cumul >= target) { seaBin = b; break; }
        }
        ctx.world.seaLevelMeters = kMinElev + (static_cast<float>(seaBin) + 0.5f) * kBinWidth;
    }

    ctx.world.validFields |= static_cast<uint32_t>(WorldField::Elevation);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::BoundaryType);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::BoundaryDistance);

    ctx.reportProgress(1.0f);
}

} // namespace worldgen
