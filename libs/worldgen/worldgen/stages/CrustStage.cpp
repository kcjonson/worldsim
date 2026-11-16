// CrustStage — M-T3 implementation (M-T3.6 crust-type upsampling).
//
// Upsamples the coarse TectonicHistory (produced by TectonicHistoryStage) to the
// full-resolution grid, writing:
//   data.plateId          — coarse plate id propagated to full-res tile
//   data.flags            — kFlagContinentalCrust for continental tiles
//   data.crustAge         — u16 Myr, inverse-distance blended (same crust type)
//   data.orogenyAge       — u16 Myr, suture-guarded blend (65535 = never)
//
// Also builds world.plates from TectonicHistory::plates so TerrainStage can
// compute Euler-pole relative velocities exactly as before.
//
// Determinism: parallelFor with fixed grain; every computation is a pure function
// of (tileId, params, seeds) using foundation det_math + fractalNoise3 only. The
// signed-distance field is precomputed once (single-threaded) before the loop.
//
// Crust-type decision (M-T3.6): the type is NOT a nearest-sample of the coarse
// binary crustType — warping a binary field per-sample dithers a ~1-cell band
// around every coast into salt-and-pepper confetti. Instead we threshold a SMOOTH
// signed "continentalness" field at 0:
//   1. Precompute, on the coarse grid, the signed distance to the crust-type
//      boundary (cell units, + continental / - oceanic), via multi-source BFS +
//      light Jacobi smoothing (CoarseSampler::buildSignedDistanceField).
//   2. Per full-res tile: warp the center (same domain warp as before, which breaks
//      the coarse-hex straight edges), smooth-interpolate the signed field at the
//      warped point, add fine crenulation noise, and threshold at 0. This gives a
//      crisp, organic, 1-tile-wide coast with no confetti.
//
// Domain warp: three independent fractalNoise3 channels (3 octaves, freq 6.0) are
// scaled by kWarpAmp ≈ 0.6 * coarse-cell angular diameter, which at kCoarseN=128
// is ~0.00297 rad on the unit sphere (~19 km at Earth radius).

#include "worldgen/stages/CrustStage.h"

#include "worldgen/data/WorldData.h"
#include "worldgen/tectonics/CoarseSampler.h"
#include "worldgen/tectonics/TectonicHistory.h"
#include "worldgen/tectonics/TectonicParams.h"

#include <math/DeterministicMath.h>
#include <random/HashNoise.h>

#include <cassert>
#include <cstdint>
#include <vector>

namespace worldgen {

namespace {

// Sentinel cap for WorldData crustAge storage (u16, matches TectonicHistory).
constexpr uint16_t kCrustAgeCap    = 65534u;
// "Never" sentinel for WorldData orogenyAge (u16).
constexpr uint16_t kOrogenyNeverU16 = 65535u;

// Clamp a float Myr age to the u16 crustAge field.
inline uint16_t clampCrustAge(float ageMyr) {
    if (ageMyr < 0.0f)                       return 0u;
    if (ageMyr > static_cast<float>(kCrustAgeCap)) return kCrustAgeCap;
    return static_cast<uint16_t>(ageMyr);
}

// Convert the coarse orogenyAge (int32 with kOrogenyNever sentinel) to u16.
inline uint16_t toOrogenyAgeU16(float blendedMyr) {
    if (blendedMyr >= 1e8f) return kOrogenyNeverU16;
    if (blendedMyr < 0.0f)  return 0u;
    uint32_t v = static_cast<uint32_t>(blendedMyr);
    return (v > 65534u) ? uint16_t{65534u} : static_cast<uint16_t>(v);
}

} // namespace

void CrustStage::run(StageContext& ctx) {
    const auto& hist = ctx.world.tectonicHistory;
    if (!hist) {
        // Should never happen post-TectonicHistoryStage, but guard gracefully.
        ctx.reportProgress(1.0f);
        return;
    }

    const SphereGrid& coarseGrid = *hist->grid;
    const SphereGrid& fullGrid   = ctx.grid;
    const uint32_t    N          = fullGrid.tileCount();

    // Warp amplitude: ~0.6 * coarse cell angular diameter, precomputed once.
    const float kWarpAmp = tectonics::warpAmplitude(coarseGrid);

    // Three distinct seed offsets for X/Y/Z warp channels.
    const auto seed32 = static_cast<uint32_t>(ctx.stageSeed ^ (ctx.stageSeed >> 32));
    const uint32_t seedWX = seed32 ^ 0xA3C5E701u;
    const uint32_t seedWY = seed32 ^ 0x1B2D4E02u;
    const uint32_t seedWZ = seed32 ^ 0xDEADBE03u;

    // ----- Build world.plates from TectonicHistory -----
    // TerrainStage reads plates[pid].eulerPole and .angularSpeed to compute Euler
    // velocities. TectonicPlate stores omega as (eulerPole, omegaRadPerMyr).
    const uint32_t plateCount = static_cast<uint32_t>(hist->plates.size());
    ctx.world.plates.resize(plateCount);
    for (uint32_t p = 0; p < plateCount; ++p) {
        const tectonics::TectonicPlate& tp = hist->plates[p];
        PlateInfo& pi = ctx.world.plates[p];
        pi.eulerPole     = tp.eulerPole;
        // TerrainStage uses angularSpeed * eulerPole as omega; omegaRadPerMyr
        // is already the signed angular speed about the pole.
        pi.angularSpeed  = static_cast<float>(tp.omegaRadPerMyr);
        pi.isContinental = tp.isContinental;
    }

    // ----- Precompute the coarse signed-distance ("continentalness") field -----
    // Single-source-of-truth for the crust-type decision: thresholded at 0 per
    // full-res tile. Built once, single-threaded, before the parallel upsample.
    const std::vector<float> coarseSdf =
        tectonics::buildSignedDistanceField(coarseGrid, hist->crustType);

    // Crenulation noise seed (distinct from the three warp channels).
    const uint32_t seedCoast = seed32 ^ 0x5EACAA04u;

    ctx.reportProgress(0.10f);
    throwIfCancelled(ctx);

    // ----- Per-tile parallel upsample -----
    constexpr size_t kGrainSize = 4096;

    const uint8_t kContinentalU8 =
        static_cast<uint8_t>(tectonics::CrustType::Continental);
    const uint8_t kOceanicU8 =
        static_cast<uint8_t>(tectonics::CrustType::Oceanic);

    ctx.pool.parallelFor(0, N, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);

        for (size_t t = begin; t < end; ++t) {
            const Vec3d center = fullGrid.tileCenter(static_cast<uint32_t>(t));

            // Domain-warped coarse lookup: warped point + the cell containing it.
            const tectonics::WarpResult w = tectonics::warpedCoarseDir(
                center, coarseGrid, kWarpAmp, seedWX, seedWY, seedWZ);

            // ---- Crust-type decision: threshold the smooth signed field at 0 ----
            // Smooth-interpolate the continentalness SDF at the warped point so the
            // field is continuous (no per-cell quantization steps), then add fine
            // crenulation so the coastline meanders at the full-res scale.
            float d = tectonics::smoothSampleAt(
                w.point, w.tile, coarseGrid,
                [&](TileId tile) { return coarseSdf[tile]; });

            const float cx = static_cast<float>(center.x);
            const float cy = static_cast<float>(center.y);
            const float cz = static_cast<float>(center.z);
            d += foundation::fractalNoise3(
                     cx * tectonics::kCoastDetailFreq,
                     cy * tectonics::kCoastDetailFreq,
                     cz * tectonics::kCoastDetailFreq,
                     seedCoast, tectonics::kCoastDetailOctaves, 2.0f, 0.5f)
                 * tectonics::kCoastDetailAmp;

            const bool isCont = (d > 0.0f);
            if (isCont) {
                ctx.data.flags[t] |= kFlagContinentalCrust;
            } else {
                ctx.data.flags[t] &= static_cast<uint8_t>(~kFlagContinentalCrust);
            }

            // ---- Age/plate consistency ----
            // Sample plate + age fields from the nearest coarse cell whose crust type
            // MATCHES the decided type, so a tile pushed continental by the threshold
            // never inherits age-0 oceanic values (and vice versa). When the threshold
            // agrees with the warped cell (the common case, away from coasts) sm == s,
            // so plate-boundary coherence and the existing age blends are unchanged.
            const uint8_t wantType = isCont ? kContinentalU8 : kOceanicU8;
            const TileId sm = tectonics::nearestMatchingCrustTile(
                w.point, w.tile, coarseGrid, hist->crustType, wantType);
            // Fallback (no matching cell within the search rings, deep in a flipped
            // region): sm == w.tile and the blends below use its values clamped — rare.

            // plateId — clamp + debug assert
            uint8_t pid = hist->plateId[sm];
#ifdef NDEBUG
            if (pid >= static_cast<uint8_t>(plateCount)) pid = 0;
#else
            assert(pid < static_cast<uint8_t>(plateCount) && "plateId 255 in finalized TectonicHistory");
            if (pid >= static_cast<uint8_t>(plateCount)) pid = 0;
#endif
            ctx.data.plateId[t] = pid;

            // crustAge: inverse-distance blend of same-crust-type neighbors of sm.
            // Guard: if nearestMatchingCrustTile fell back to returning a wrong-type
            // tile (sm's crustType != wantType), writing its age into a tile of the
            // opposite crust type would be semantically wrong (ocean age on a continent,
            // or vice versa). In that rare case use 0 — a neutral, type-appropriate
            // default — rather than inheriting a cross-type value.
            auto getCrustAge = [&](TileId tile) -> float {
                return static_cast<float>(hist->crustAge[tile]);
            };
            if (hist->crustType[sm] != wantType) {
                ctx.data.crustAge[t] = 0u;
            } else {
                tectonics::BlendResult ageBlend =
                    tectonics::blendSmoothField(sm, coarseGrid, *hist, getCrustAge);
                ctx.data.crustAge[t] = clampCrustAge(ageBlend.value);
            }

            // orogenyAge: suture-guarded blend anchored at sm. Same fallback guard as
            // crustAge: if sm is the wrong crust type, use a neutral value (kOrogenyNeverU16
            // for continental — no orogeny yet; 0 for oceanic — though orogeny on ocean is
            // unexpected). In practice continental is the only type that carries orogenyAge.
            if (hist->crustType[sm] != wantType) {
                ctx.data.orogenyAge[t] = isCont ? kOrogenyNeverU16 : 0u;
            } else {
                const float blendedOrogenic = tectonics::blendOrogenyAge(sm, coarseGrid, *hist);
                ctx.data.orogenyAge[t] = toOrogenyAgeU16(blendedOrogenic);
            }
        }

        ctx.reportProgress(0.10f + static_cast<float>(end) / static_cast<float>(N) * 0.85f);
    });

    ctx.world.validFields |= static_cast<uint32_t>(WorldField::PlateId);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::Flags);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::CrustAge);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::OrogenyAge);
    // BoundaryType and BoundaryDistance are written by TerrainStage, not here.
    // TerrainStage reclassifies boundaries at full resolution from plateId + Euler poles
    // (more accurate than upsampling the coarse TectonicHistory boundary fields, which
    // are sim-internal diagnostics used only by worldgen-cli --sim-only).
    // TerrainStage also inherits the plates list we built above.

    ctx.reportProgress(1.0f);
}

} // namespace worldgen
