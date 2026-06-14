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
// Pipeline scaffolding REUSED from the prior stage (unchanged):
//   1. Boundary classification: relative Euler velocity from world.plates ->
//      convergence/shear -> BoundaryType + side; two mode-filter smoothing passes.
//   2c. Belt-end taper along the boundary graph.
//   2.  Multi-source boundary-distance BFS (propagates type/side/conv/taper inward).
//   2b. Jacobi+jitter distance smoothing (kills stair-step terracing).
//   2d. Crust-edge distance BFS (continental shelf ramp).
//   Sea level: histogram quantile at waterAmount.
//
// Determinism: every per-tile value is a pure function of (tile id, params, seeds).
// parallelFor uses fixed grain slabs; all transcendentals go through det_math; the
// coarse samplers are pure functions of the coarse grid + fields. BFS passes use
// ascending-tile-id seed order over a FIFO, and the Jacobi smoother is double
// buffered, so results are identical at any thread count.

#include "worldgen/stages/TerrainStage.h"

#include "worldgen/data/WorldData.h"
#include "worldgen/tectonics/CoarseSampler.h"
#include "worldgen/tectonics/TectonicHistory.h"
#include "worldgen/tectonics/TectonicParams.h"

#include <math/DeterministicMath.h>
#include <random/HashNoise.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

namespace worldgen {

namespace {

// Local alias for brevity within this file.
using BType = BoundaryType;

constexpr uint8_t kSideOverriding = 1;
constexpr uint8_t kSideSubducting = 2;
constexpr uint8_t kSideSymmetric  = 0;

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
// orogeny line stands continuously above the mountain threshold (one connected belt, not a
// string of beads severed by the recentered ridged texture); the rest stays as the recentered
// ridged crest/valley texture so the belt reads as a sharp range, not a smooth welt.
// kBeltLiftFrac is the positive-lift fraction; (1 - kBeltLiftFrac) is the +/- ridged texture.
constexpr float kBeltLiftFrac = 0.55f;
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
//   0 < signedKm <= kShelfWidthKm → flat shelf rising from kShelfBreakDepthM to ~-60m
//   signedKm ≈ 0                  → kShelfBreakDepthM
//   −kSlopeWidthKm < signedKm < 0 → continental slope: steep descent to abyssalElev
//   signedKm <= −kSlopeWidthKm    → abyssalElev (depth-age, unchanged)
constexpr float kShelfBreakDepthM = -140.0f; // depth at the shelf break (signedKm=0)
constexpr float kShelfDepthM      = -120.0f; // representative mid-shelf depth
// kShelfWidthKm: Earth passive-margin shelves span 50-300+ km (mean ~75 km, wide
// Atlantic-type > 200 km). Use 200 km to ensure 3-4 tiles at n=256 (tile ~50 km)
// fall in the submerged shelf band; the quantile-based sea level self-corrects
// oceanFraction, so widening the shelf doesn't push more land underwater.
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

// --- Active-boundary kernels (applied only near the classified boundary) ---
// ConvergentCO subducting (oceanic) side: deep narrow trench. M-T4.5: deepened (-5500 ->
// -6600) and slightly widened (60 -> 80 km sigma). The M-T4.5 sim changes shifted the
// ocean-age / boundary geometry so the basin's abyssal-mean sits a little deeper; a deeper
// trench keeps it unambiguously below the abyssal mean (trenches ARE the deepest ocean
// features), which the TrenchArcStructure invariant checks.
constexpr float kTrenchCOAmpM   = -7200.0f;
constexpr float kTrenchCOSigKm  = 110.0f;
// ConvergentCO overriding (continental) side: volcanic arc inland + forearc basin.
constexpr float kArcCOAmpM      = 2500.0f; // scaled by convergence
constexpr float kArcCODistKm    = 220.0f;  // arc sits this far inland of the trench
constexpr float kArcCOSigKm     = 90.0f;
constexpr float kForearcAmpM    = -400.0f;
constexpr float kForearcSigKm   = 28.0f;
// ConvergentOO: trench + ridged island-arc peaks behind it.
constexpr float kTrenchOOAmpM   = -6000.0f;
constexpr float kTrenchOOSigKm  = 50.0f;
constexpr float kArcOOAmpM      = 3000.0f; // scaled by convergence, ridged so only some breach
constexpr float kArcOODistKm    = 150.0f;
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

// --- Hotspot / arc volcanism cones ---
// volcanism * ridged cone noise (high frequency) so hotspot chains read as
// seamount/island lines. Capped so a saturated plume can't make a Hawaii the size of Tibet.
constexpr float kVolcanismConeAmpM  = 4500.0f; // pre-cap amplitude at volcanism=1
constexpr float kVolcanismConeFreq  = 14.0f;
constexpr float kVolcanismCapM      = 3500.0f;

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

// --- OO convergent arc mix coefficients ---
// arcAmp = kArcOOAmpM * (kOOConvBase + kOOConvAmp * convN) * (kOORidgeBase + kOORidgeAmp * ridgeN)
constexpr float kOOConvBase  = 0.5f;
constexpr float kOOConvAmp   = 0.5f;
constexpr float kOORidgeBase = 0.3f;
constexpr float kOORidgeAmp  = 0.7f;

// --- Transform shear noise frequency ---
constexpr float kTransformShearFreq    = 6.0f;
constexpr int   kTransformShearOctaves = 3;

// Gaussian falloff via det_math::exp for cross-platform determinism.
inline float gaussianFalloff(float d, float sigma) {
    if (sigma <= 0.0f) return 0.0f;
    double x = static_cast<double>(d) / static_cast<double>(sigma);
    return static_cast<float>(foundation::det_math::exp(-0.5 * x * x));
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
            float t = (signedKm - outerEdge) / kShelfInnerBlendKm;
            float sm = t * t * (3.0f - 2.0f * t); // smoothstep
            return kShelfDepthM + sm * (platformElev - kShelfDepthM);
        }
    } else {
        // On the continental slope: steep descent from break to abyssal floor.
        float t = -signedKm / kSlopeWidthKm; // 0 at break, 1 at slope foot
        float sm = t * t * (3.0f - 2.0f * t); // smoothstep → concave-up profile
        return kShelfBreakDepthM + sm * (abyssalElev - kShelfBreakDepthM);
    }
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
    assert(hist && "TerrainStage requires TectonicHistory (run after CrustStage)");
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
    // =========================================================================

    std::vector<BType>   bndTypeRaw(N, BType::None);
    std::vector<uint8_t> bndSideRaw(N, kSideSymmetric);
    std::vector<uint8_t> bndPlate(N, 255u);
    std::vector<float>   bndConvRaw(N, 0.0f);
    std::vector<bool>    isBoundary(N, false);

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
            isBoundary[t] = true;
            bndPlate[t]   = domPlate;

            Vec3d ctr = ctx.grid.tileCenter(t);

            // omega vectors for own and foreign plate
            double oaX{}, oaY{}, oaZ{};
            double obX{}, obY{}, obZ{};
            if (pid < static_cast<uint8_t>(K)) {
                const auto& pl = ctx.world.plates[static_cast<size_t>(pid)];
                double sp = static_cast<double>(pl.angularSpeed);
                oaX = pl.eulerPole.x * sp;
                oaY = pl.eulerPole.y * sp;
                oaZ = pl.eulerPole.z * sp;
            }
            if (domPlate < static_cast<uint8_t>(K)) {
                const auto& pl = ctx.world.plates[static_cast<size_t>(domPlate)];
                double sp = static_cast<double>(pl.angularSpeed);
                obX = pl.eulerPole.x * sp;
                obY = pl.eulerPole.y * sp;
                obZ = pl.eulerPole.z * sp;
            }

            // v_rel = cross(omega_a, r) - cross(omega_b, r)
            double vaX{}, vaY{}, vaZ{}, vbX{}, vbY{}, vbZ{};
            cross3d(oaX, oaY, oaZ, ctr.x, ctr.y, ctr.z, vaX, vaY, vaZ);
            cross3d(obX, obY, obZ, ctr.x, ctr.y, ctr.z, vbX, vbY, vbZ);
            double vrX = vaX - vbX, vrY = vaY - vbY, vrZ = vaZ - vbZ;

            // Boundary normal: direction toward foreign plate neighbors
            double normX = 0.0, normY = 0.0, normZ = 0.0;
            uint32_t dfCnt = 0u;
            for (uint32_t k = 0; k < cnt; ++k) {
                if (ctx.data.plateId[nbrs[k]] == domPlate) {
                    Vec3d nc = ctx.grid.tileCenter(nbrs[k]);
                    normX += nc.x - ctr.x;
                    normY += nc.y - ctr.y;
                    normZ += nc.z - ctr.z;
                    ++dfCnt;
                }
            }
            if (dfCnt == 0u) {
                // Fallback: look one hop further
                for (uint32_t k = 0; k < cnt; ++k) {
                    Vec3d nc = ctx.grid.tileCenter(nbrs[k]);
                    normX += nc.x - ctr.x;
                    normY += nc.y - ctr.y;
                    normZ += nc.z - ctr.z;
                    dfCnt++;
                }
            }
            double nLen = length3d(normX, normY, normZ);
            if (nLen > 1e-12) { normX /= nLen; normY /= nLen; normZ /= nLen; }

            // convergence = -dot(v_rel, normal): positive → plates approaching
            double convergence = -dot3d(vrX, vrY, vrZ, normX, normY, normZ);

            bndConvRaw[t] = static_cast<float>(convergence);

            double vRelMag = length3d(vrX, vrY, vrZ);
            double absConv = convergence < 0.0 ? -convergence : convergence;
            // Convergent/divergent when |convergence| > 0.35*|v_rel|; transform otherwise.
            bool isConvergent  = absConv > 0.35 * vRelMag;
            bool isApproaching = convergence > 0.0;

            bool tileIsCont    = (ctx.data.flags[t] & kFlagContinentalCrust) != 0;
            // Derive foreign crust type from the actual neighboring tiles of domPlate
            // (majority vote of kFlagContinentalCrust), since crust is decoupled from
            // plate identity — mixed-crust plates exist at continental margins.
            // Fall back to plate.isContinental only when no domPlate neighbors are visible.
            bool foreignIsCont = false;
            if (domPlate < static_cast<uint8_t>(K)) {
                uint32_t domCrustCount = 0u, domTileCount = 0u;
                for (uint32_t k = 0; k < cnt; ++k) {
                    if (ctx.data.plateId[nbrs[k]] == domPlate) {
                        ++domTileCount;
                        if ((ctx.data.flags[nbrs[k]] & kFlagContinentalCrust) != 0) ++domCrustCount;
                    }
                }
                if (domTileCount > 0u) {
                    foreignIsCont = (domCrustCount * 2u > domTileCount); // majority
                } else {
                    foreignIsCont = ctx.world.plates[static_cast<size_t>(domPlate)].isContinental;
                }
            }

            if (!isConvergent) {
                bndTypeRaw[t] = BType::Transform;
                bndSideRaw[t] = kSideSymmetric;
            } else if (!isApproaching) {
                bndTypeRaw[t] = BType::Divergent;
                bndSideRaw[t] = kSideSymmetric;
            } else if (tileIsCont && foreignIsCont) {
                bndTypeRaw[t] = BType::ConvergentCC;
                bndSideRaw[t] = kSideSymmetric;
            } else if (tileIsCont && !foreignIsCont) {
                bndTypeRaw[t] = BType::ConvergentCO;
                bndSideRaw[t] = kSideOverriding;
            } else if (!tileIsCont && foreignIsCont) {
                bndTypeRaw[t] = BType::ConvergentCO;
                bndSideRaw[t] = kSideSubducting;
            } else {
                bndTypeRaw[t] = BType::ConvergentOO;
                bndSideRaw[t] = (pid < domPlate) ? kSideOverriding : kSideSubducting;
            }
        }
    }

    ctx.reportProgress(0.08f);
    throwIfCancelled(ctx);

    // ---- Smooth boundary classification (2 passes, mode filter) ----
    // True double-buffered: each pass reads one buffer and writes the other,
    // so the result is order-independent regardless of tile scan order.
    {
        // Pass 0: raw → smooth.  Pass 1: smooth → smooth2.  Final result in smooth2.
        std::vector<BType>   smooth(N, BType::None);
        std::vector<uint8_t> smoothSide(N, kSideSymmetric);
        std::vector<BType>   smooth2(N, BType::None);
        std::vector<uint8_t> smoothSide2(N, kSideSymmetric);

        auto modePass = [&](const std::vector<BType>&   srcT,
                            const std::vector<uint8_t>& srcS,
                            std::vector<BType>&         dstT,
                            std::vector<uint8_t>&       dstS) {
            std::array<TileId, 6> nbrs{};
            for (uint32_t t = 0; t < N; ++t) {
                if (!isBoundary[t]) { dstT[t] = BType::None; dstS[t] = kSideSymmetric; continue; }
                uint32_t cnt = ctx.grid.neighbors(t, nbrs);
                uint8_t freq[6] = {};
                freq[static_cast<uint8_t>(srcT[t])]++;
                for (uint32_t k = 0; k < cnt; ++k) {
                    if (isBoundary[nbrs[k]]) freq[static_cast<uint8_t>(srcT[nbrs[k]])]++;
                }
                uint8_t best = static_cast<uint8_t>(srcT[t]);
                uint8_t bestF = freq[best];
                for (uint8_t b = 1; b <= 5; ++b) {
                    if (freq[b] > bestF) { bestF = freq[b]; best = b; }
                }
                dstT[t] = static_cast<BType>(best);
                dstS[t] = srcS[t];
            }
        };

        modePass(bndTypeRaw, bndSideRaw, smooth,  smoothSide);   // pass 0
        modePass(smooth,     smoothSide,  smooth2, smoothSide2);  // pass 1

        bndTypeRaw = std::move(smooth2);
        bndSideRaw = std::move(smoothSide2);
    }

    // Write to data.boundaryType
    for (uint32_t t = 0; t < N; ++t) {
        ctx.data.boundaryType[t] = static_cast<uint8_t>(bndTypeRaw[t]);
    }

    // Normalize convergence to [0,1] using the 90th-percentile magnitude of positive
    // convergence values so outlier plate pairs don't collapse all others near zero.
    std::vector<float> posConvSamples;
    posConvSamples.reserve(N / 4);
    for (uint32_t t = 0; t < N; ++t) {
        if (bndConvRaw[t] > 0.0f) posConvSamples.push_back(bndConvRaw[t]);
    }
    float p90Conv = 1e-12f;
    if (!posConvSamples.empty()) {
        std::sort(posConvSamples.begin(), posConvSamples.end());
        size_t idx = static_cast<size_t>(posConvSamples.size() * 9 / 10);
        if (idx >= posConvSamples.size()) idx = posConvSamples.size() - 1;
        p90Conv = posConvSamples[idx];
        if (p90Conv < 1e-12f) p90Conv = 1e-12f;
    }
    posConvSamples.clear(); posConvSamples.shrink_to_fit();
    std::vector<float> bndConvNorm(N, 0.0f);
    for (uint32_t t = 0; t < N; ++t) {
        float v = bndConvRaw[t] / p90Conv;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        bndConvNorm[t] = v;
    }
    bndConvRaw.clear(); bndConvRaw.shrink_to_fit();

    ctx.reportProgress(0.13f);
    throwIfCancelled(ctx);

    // =========================================================================
    // 2c. Belt-end taper — compute per-boundary-tile amplitude taper BEFORE the
    //     distance BFS so we can propagate it to interior tiles in section 2.
    //     For each boundary tile, count same-type boundary neighbors within 4 hops
    //     along the boundary graph; taper = smoothstep(count / kTaperMax).
    //     Isolated segment ends taper to 0; interior belt tiles stay at 1.
    // =========================================================================

    std::vector<float> beltTaper(N, 1.0f);
    {
        constexpr int32_t kTaperHops = 4;
        constexpr float   kTaperMax  = 12.0f;

        std::vector<int32_t> epoch(N, -1);
        std::vector<uint32_t> bfsQ;
        bfsQ.reserve(256);
        std::array<TileId, 6> tbNbrs{};

        int32_t baseEpoch = 0;
        for (uint32_t t = 0; t < N; ++t) {
            BType myType = bndTypeRaw[t];
            if (myType == BType::None) continue;

            bfsQ.clear();
            epoch[t] = baseEpoch;
            bfsQ.push_back(t);
            int32_t count = 0;

            for (size_t qi = 0; qi < bfsQ.size(); ++qi) {
                uint32_t cur = bfsQ[qi];
                int32_t  hop = epoch[cur] - baseEpoch;
                if (hop >= kTaperHops) continue;
                ++count;

                uint32_t cnt = ctx.grid.neighbors(cur, tbNbrs);
                for (uint32_t k = 0; k < cnt; ++k) {
                    TileId nb = tbNbrs[k];
                    if (epoch[nb] >= baseEpoch) continue;
                    if (bndTypeRaw[nb] != myType) continue;
                    epoch[nb] = baseEpoch + hop + 1;
                    bfsQ.push_back(nb);
                }
            }

            float c = static_cast<float>(count) / kTaperMax;
            if (c > 1.0f) c = 1.0f;
            beltTaper[t] = c * c * (3.0f - 2.0f * c); // smoothstep

            baseEpoch += kTaperHops + 1;
        }
    }

    // =========================================================================
    // 2. Distance BFS — from all boundary tiles simultaneously.
    //    Seeds enqueued in ascending tile-id order → FIFO → deterministic.
    // =========================================================================

    std::vector<BType>   bfsBndType(N, BType::None);
    std::vector<uint8_t> bfsBndSide(N, kSideSymmetric);
    std::vector<float>   bfsBndConv(N, 0.0f);
    std::vector<float>   bfsBndTaper(N, 1.0f);
    std::vector<int32_t> bfsDist(N, -1);

    {
        std::vector<uint32_t> bfsQueue;
        bfsQueue.reserve(N);
        for (uint32_t t = 0; t < N; ++t) {
            if (isBoundary[t]) {
                bfsDist[t]      = 0;
                bfsBndType[t]   = bndTypeRaw[t];
                bfsBndSide[t]   = bndSideRaw[t];
                bfsBndConv[t]   = bndConvNorm[t];
                bfsBndTaper[t]  = beltTaper[t];
                bfsQueue.push_back(t);
            }
        }
        std::array<TileId, 6> nbrs{};
        for (size_t qi = 0; qi < bfsQueue.size(); ++qi) {
            uint32_t t  = bfsQueue[qi];
            int32_t  nd = bfsDist[t] + 1;
            uint32_t cnt = ctx.grid.neighbors(t, nbrs);
            for (uint32_t k = 0; k < cnt; ++k) {
                TileId nb = nbrs[k];
                if (bfsDist[nb] < 0) {
                    bfsDist[nb]     = nd;
                    bfsBndType[nb]  = bfsBndType[t];
                    bfsBndSide[nb]  = bfsBndSide[t];
                    bfsBndConv[nb]  = bfsBndConv[t];
                    bfsBndTaper[nb] = bfsBndTaper[t];
                    bfsQueue.push_back(nb);
                }
            }
            if ((qi & 0xFFFFu) == 0) throwIfCancelled(ctx);
        }
        for (uint32_t t = 0; t < N; ++t) {
            int32_t d = (bfsDist[t] < 0) ? 0 : bfsDist[t];
            ctx.data.boundaryDistance[t] = (d > 65535) ? uint16_t(65535) : static_cast<uint16_t>(d);
        }
    }

    // =========================================================================
    // 2b. Distance smoothing — three Jacobi passes to eliminate stair-step
    //     terracing from integer BFS distances. Jitter first, then smooth.
    // =========================================================================

    std::vector<float> smoothDist(N, 0.0f);
    {
        const uint32_t seedDistJitter = stageSeed32 ^ 0xF1E2D3C4u;
        std::vector<float> distA(N);
        for (uint32_t t = 0; t < N; ++t) {
            float base = static_cast<float>(bfsDist[t] >= 0 ? bfsDist[t] : 0);
            uint32_t h = foundation::hash3(static_cast<int32_t>(t), 0, 0, seedDistJitter);
            float jitter = static_cast<float>(h >> 8) * (1.0f / 16777216.0f) - 0.5f; // [-0.5,0.5)
            float d = base + jitter * 2.4f; // ±1.2 tile amplitude
            distA[t] = d < 0.0f ? 0.0f : d;
        }

        std::vector<float> distB(N);
        std::array<TileId, 6> jNbrs{};

        for (int pass = 0; pass < 3; ++pass) {
            for (uint32_t t = 0; t < N; ++t) {
                uint32_t cnt = ctx.grid.neighbors(t, jNbrs);
                float sum = distA[t];
                for (uint32_t k = 0; k < cnt; ++k) sum += distA[jNbrs[k]];
                distB[t] = sum / static_cast<float>(cnt + 1u);
            }
            distA.swap(distB);
            throwIfCancelled(ctx);
        }

        for (uint32_t t = 0; t < N; ++t) {
            smoothDist[t] = distA[t] < 0.0f ? 0.0f : distA[t];
        }
    }

    // Free no-longer-needed boundary-classification scratch.
    isBoundary.clear();  isBoundary.shrink_to_fit();
    bndTypeRaw.clear();  bndTypeRaw.shrink_to_fit();
    bndSideRaw.clear();  bndSideRaw.shrink_to_fit();
    bndConvNorm.clear(); bndConvNorm.shrink_to_fit();
    beltTaper.clear();   beltTaper.shrink_to_fit();
    bndPlate.clear();    bndPlate.shrink_to_fit();

    ctx.reportProgress(0.25f);
    throwIfCancelled(ctx);

    // =========================================================================
    // 2d. Crust-edge distance BFS (for continental shelf ramp).
    // =========================================================================

    std::vector<int32_t> crustEdgeDist(N, -1);
    {
        std::vector<uint32_t> bfsQueue;
        bfsQueue.reserve(N / 4);
        std::array<TileId, 6> nbrs{};
        for (uint32_t t = 0; t < N; ++t) {
            bool isCrust = (ctx.data.flags[t] & kFlagContinentalCrust) != 0;
            uint32_t cnt = ctx.grid.neighbors(t, nbrs);
            for (uint32_t k = 0; k < cnt; ++k) {
                bool nbCrust = (ctx.data.flags[nbrs[k]] & kFlagContinentalCrust) != 0;
                if (nbCrust != isCrust) { crustEdgeDist[t] = 0; bfsQueue.push_back(t); break; }
            }
        }
        for (size_t qi = 0; qi < bfsQueue.size(); ++qi) {
            uint32_t t   = bfsQueue[qi];
            int32_t  nd  = crustEdgeDist[t] + 1;
            std::array<TileId, 6> nbrs2{};
            uint32_t cnt = ctx.grid.neighbors(t, nbrs2);
            for (uint32_t k = 0; k < cnt; ++k) {
                TileId nb = nbrs2[k];
                if (crustEdgeDist[nb] < 0) { crustEdgeDist[nb] = nd; bfsQueue.push_back(nb); }
            }
        }
    }

    ctx.reportProgress(0.32f);
    throwIfCancelled(ctx);

    // =========================================================================
    // 3. Elevation synthesis — parallel per tile.
    //    Pure function of (tile id, coarse fields, full-res fields, seeds).
    // =========================================================================

    // Kernel sigma values in tiles. A floor of ~1.3 tiles keeps the narrow features
    // (trench, axial valley, forearc) at least one tile wide at coarse n, where a
    // physical 50-60 km sigma would otherwise be sub-tile and the feature would vanish
    // between samples. At full res these kilometre sigmas dominate the floor, so the
    // floor only bites on small test grids (n=128) — the features stay geologically
    // narrow where it matters.
    constexpr float kSigFloorTiles = 1.3f;
    auto sigTiles = [&](float km) { float s = kmToTiles(km); return s < kSigFloorTiles ? kSigFloorTiles : s; };
    const float sigCO_trnc  = sigTiles(kTrenchCOSigKm);
    const float sigCO_arc   = sigTiles(kArcCOSigKm);
    const float dCO_arc     = kmToTiles(kArcCODistKm);
    const float sigForearc  = sigTiles(kForearcSigKm);
    const float sigOO_trnc  = sigTiles(kTrenchOOSigKm);
    const float sigOO_arc   = sigTiles(kArcOOSigKm);
    const float dOO_arc     = kmToTiles(kArcOODistKm);
    const float sigRift     = sigTiles(kRiftSigKm);
    const float sigRiftShl  = sigTiles(kRiftShoulderSigKm);
    const float dRiftShl    = kmToTiles(kRiftShoulderDistKm);
    const float sigAxial    = sigTiles(kAxialValleySigKm);
    const float sigTrn      = sigTiles(kTransformSigKm);

    constexpr size_t kGrainSize = 4096;

    ctx.pool.parallelFor(0, N, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        for (size_t t = begin; t < end; ++t) {
            const bool    isCont   = (ctx.data.flags[t] & kFlagContinentalCrust) != 0;
            const BType   bt       = static_cast<BType>(bfsBndType[t]);
            const uint8_t side     = bfsBndSide[t];
            const float   distT    = smoothDist[t];
            const float   convN    = bfsBndConv[t];
            const float   taperAmp = bfsBndTaper[t];

            Vec3d  ctr = ctx.grid.tileCenter(static_cast<uint32_t>(t));
            float  cx  = static_cast<float>(ctr.x);
            float  cy  = static_cast<float>(ctr.y);
            float  cz  = static_cast<float>(ctr.z);

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

                // -- Signed crust-edge distance (positive = inland) --
                float cedge = static_cast<float>(crustEdgeDist[t] >= 0 ? crustEdgeDist[t] : 9999);
                float signedKm = cedge * tileWidthKm; // continental side is always +

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
                float intensitySharp = 0.0f;
                if (orogenyIntensity > kBeltIntensityKnee) {
                    float u = (orogenyIntensity - kBeltIntensityKnee) /
                              (kBeltIntensityFull - kBeltIntensityKnee);
                    if (u > 1.0f) u = 1.0f;
                    intensitySharp = u * u * (3.0f - 2.0f * u); // smoothstep
                }

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
                    float lift    = kBeltLiftFrac * beltAmp;
                    float texture = (1.0f - kBeltLiftFrac) * beltAmp * (2.0f * ridged - 1.0f);
                    beltDetail = lift + texture;
                }

                // -- Hills (broad relief, amplified near recent orogeny) --
                // Suppressed on the shallow shelf so fractal noise can't push
                // -120m shelf tiles above sea level. Hill amplitude ramps from
                // 0 at the crust edge to full at the inner shelf edge, then
                // stays full across the continental interior.
                float hillShelfFade = signedKm >= kShelfWidthKm
                    ? 1.0f
                    : (signedKm <= 0.0f
                        ? 0.0f
                        : signedKm / kShelfWidthKm);
                hillShelfFade = hillShelfFade * hillShelfFade * (3.0f - 2.0f * hillShelfFade); // smoothstep

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

                elev = platform + beltDetail + hills;
            } else {
                // -- Oceanic depth-age law (GDH1 plate-cooling form) --
                float subside = static_cast<float>(foundation::det_math::exp(
                    -static_cast<double>(crustAgeMyr) / kOceanDepthTauMyr));
                float depth = kOceanFloorDepthM -
                              (kOceanFloorDepthM - kOceanRidgeDepthM) * subside;
                float z = -depth;

                // Continental shelf slope on passive margins — but NOT on active subducting
                // margins where the trench kernel already carries the deep trough.
                bool subductingTrench =
                    side == kSideSubducting &&
                    (bt == BType::ConvergentCO || bt == BType::ConvergentOO);
                if (!subductingTrench) {
                    // Signed distance: oceanic tiles have negative sign (seaward of edge).
                    float cedge = static_cast<float>(crustEdgeDist[t] >= 0 ? crustEdgeDist[t] : 9999);
                    float signedKm = -(cedge * tileWidthKm); // oceanic side is always −

                    // shelfElevationForSignedEdge with platformElev=kShelfBreakDepthM:
                    // for signedKm in (−kSlopeWidthKm, 0) it gives the slope profile;
                    // for signedKm <= −kSlopeWidthKm it gives z (the depth-age abyssal floor).
                    // We pass z as abyssalElev so the slope foot connects exactly to the
                    // depth-age depth of this tile.
                    z = shelfElevationForSignedEdge(signedKm, kShelfBreakDepthM, z);
                }

                // Abyssal hills + a longer swell.
                float hills = foundation::fractalNoise3(
                                  cx * kAbyssalHillFreq, cy * kAbyssalHillFreq, cz * kAbyssalHillFreq,
                                  seedFractal + 3u, kAbyssalHillOctaves, 2.0f, 0.5f) * kAbyssalHillAmpM;
                float swell = foundation::fractalNoise3(
                                  cx * kAbyssalSwellFreq, cy * kAbyssalSwellFreq, cz * kAbyssalSwellFreq,
                                  seedFractal + 5u, kAbyssalSwellOctaves, 2.0f, 0.5f) * kAbyssalSwellAmpM;
                elev = z + hills + swell;
                if (elev < kOceanMaxDepthM) elev = kOceanMaxDepthM;
            }

            // ---- Active-boundary kernels (narrow, near the classified boundary) ----
            float kernel = 0.0f;
            switch (bt) {
                case BType::ConvergentCC:
                    // NO Gaussian dome — collision relief is the isostasy platform +
                    // ridged belts above. Nothing added here.
                    break;
                case BType::ConvergentCO: {
                    if (side == kSideSubducting) {
                        kernel = kTrenchCOAmpM * gaussianFalloff(distT, sigCO_trnc) * taperAmp;
                    } else {
                        float arc = kArcCOAmpM * convN *
                                    gaussianFalloff(distT - dCO_arc, sigCO_arc) * taperAmp;
                        float forearc = kForearcAmpM * gaussianFalloff(distT, sigForearc) * taperAmp;
                        kernel = arc + forearc;
                    }
                    break;
                }
                case BType::ConvergentOO: {
                    if (side == kSideSubducting) {
                        kernel = kTrenchOOAmpM * gaussianFalloff(distT, sigOO_trnc) * taperAmp;
                    } else {
                        float ridgeN = foundation::ridgedNoise3(
                            cx * 8.0f, cy * 8.0f, cz * 8.0f,
                            seedRidged + 1u, 3, 2.0f, 0.5f);
                        float arcAmp = kArcOOAmpM * (kOOConvBase + kOOConvAmp * convN) *
                                       (kOORidgeBase + kOORidgeAmp * ridgeN);
                        kernel = arcAmp * gaussianFalloff(distT - dOO_arc, sigOO_arc) * taperAmp;
                    }
                    break;
                }
                case BType::Divergent: {
                    if (!isCont) {
                        // Oceanic ridge crest is already in the depth-age law (age 0 at
                        // the spreading center); add only a small axial valley.
                        kernel = kAxialValleyM * gaussianFalloff(distT, sigAxial);
                    } else {
                        kernel  = kRiftAmpM * gaussianFalloff(distT, sigRift);
                        kernel += kRiftShoulderM * gaussianFalloff(distT - dRiftShl, sigRiftShl);
                    }
                    break;
                }
                case BType::Transform: {
                    float shearN = foundation::fractalNoise3(
                        cx * kTransformShearFreq, cy * kTransformShearFreq, cz * kTransformShearFreq,
                        seedFractal + 7u, kTransformShearOctaves, 2.0f, 0.5f);
                    kernel = kTransformAmpM * shearN * gaussianFalloff(distT, sigTrn);
                    break;
                }
                case BType::None:
                    break;
            }
            elev += kernel;

            // ---- Hotspot / arc volcanism cones (seamount/island lines) ----
            if (volcanism > 0.02f) {
                float cone = foundation::ridgedNoise3(
                    cx * kVolcanismConeFreq, cy * kVolcanismConeFreq, cz * kVolcanismConeFreq,
                    seedRidged + 5u, 4, 2.0f, 0.5f);
                // Sharpen so cones are isolated peaks, not a continuous mat.
                cone = cone * cone;
                float volc = volcanism * cone * kVolcanismConeAmpM;
                if (volc > kVolcanismCapM) volc = kVolcanismCapM;
                elev += volc;
            }

            if (elev < kElevMinM) elev = kElevMinM;
            if (elev > kElevMaxM) elev = kElevMaxM;
            ctx.data.elevation[t] = elev;
        }
        ctx.reportProgress(0.32f + static_cast<float>(end) / static_cast<float>(N) * 0.56f);
    });

    // Free transient arrays
    bfsBndType.clear();   bfsBndType.shrink_to_fit();
    bfsBndSide.clear();   bfsBndSide.shrink_to_fit();
    bfsBndConv.clear();   bfsBndConv.shrink_to_fit();
    bfsBndTaper.clear();  bfsBndTaper.shrink_to_fit();
    bfsDist.clear();      bfsDist.shrink_to_fit();
    smoothDist.clear();   smoothDist.shrink_to_fit();
    crustEdgeDist.clear(); crustEdgeDist.shrink_to_fit();

    ctx.reportProgress(0.90f);
    throwIfCancelled(ctx);

    // =========================================================================
    // 4. Sea level — histogram quantile at waterAmount.
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
