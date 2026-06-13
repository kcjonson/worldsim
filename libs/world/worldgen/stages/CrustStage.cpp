// CrustStage — M-T3 implementation.
//
// Upsamples the coarse TectonicHistory (produced by TectonicHistoryStage) to the
// full-resolution grid via domain-warped nearest sampling, writing:
//   data.plateId          — coarse plate id propagated to full-res tile
//   data.flags            — kFlagContinentalCrust for continental tiles
//   data.crustAge         — u16 Myr, inverse-distance blended (same crust type)
//   data.orogenyAge       — u16 Myr, suture-guarded blend (65535 = never)
//
// Also builds world.plates from TectonicHistory::plates so TerrainStage can
// compute Euler-pole relative velocities exactly as before.
//
// Determinism: parallelFor with fixed grain; every computation is a pure function
// of (tileId, params, seeds) using foundation det_math + fractalNoise3 only.
//
// Domain warp: three independent fractalNoise3 channels (3 octaves, freq 6.0) are
// scaled by kWarpAmp ≈ 0.6 * coarse-cell angular diameter, which at kCoarseN=128
// is ~0.00297 rad on the unit sphere (~19 km at Earth radius). The warped unit
// direction is looked up on the coarse grid. This breaks the hex-aligned straight
// edges of Voronoi coarse cells, giving organic coastlines.

#include "worldgen/stages/CrustStage.h"

#include "worldgen/data/WorldData.h"
#include "worldgen/tectonics/CoarseSampler.h"
#include "worldgen/tectonics/TectonicHistory.h"
#include "worldgen/tectonics/TectonicParams.h"

#include <math/DeterministicMath.h>
#include <random/HashNoise.h>

#include <cassert>
#include <cstdint>

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

    ctx.reportProgress(0.05f);
    throwIfCancelled(ctx);

    // ----- Per-tile parallel upsample -----
    constexpr size_t kGrainSize = 4096;

    ctx.pool.parallelFor(0, N, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);

        for (size_t t = begin; t < end; ++t) {
            const Vec3d center = fullGrid.tileCenter(static_cast<uint32_t>(t));

            // Domain-warped coarse lookup
            const TileId s = tectonics::warpedCoarseTile(
                center, coarseGrid, kWarpAmp, seedWX, seedWY, seedWZ);

            // plateId — clamp + debug assert
            uint8_t pid = hist->plateId[s];
#ifdef NDEBUG
            if (pid >= static_cast<uint8_t>(plateCount)) pid = 0;
#else
            assert(pid < static_cast<uint8_t>(plateCount) && "plateId 255 in finalized TectonicHistory");
            if (pid >= static_cast<uint8_t>(plateCount)) pid = 0;
#endif
            ctx.data.plateId[t] = pid;

            // Continental crust flag
            const bool isCont = (hist->crustType[s] ==
                                  static_cast<uint8_t>(tectonics::CrustType::Continental));
            if (isCont) {
                ctx.data.flags[t] |= kFlagContinentalCrust;
            } else {
                ctx.data.flags[t] &= static_cast<uint8_t>(~kFlagContinentalCrust);
            }

            // crustAge: inverse-distance blend of same-crust-type neighbors
            auto getCrustAge = [&](TileId tile) -> float {
                return static_cast<float>(hist->crustAge[tile]);
            };
            tectonics::BlendResult ageBlend =
                tectonics::blendSmoothField(s, coarseGrid, *hist, getCrustAge);
            ctx.data.crustAge[t] = clampCrustAge(ageBlend.value);

            // orogenyAge: suture-guarded blend
            const float blendedOrogenic = tectonics::blendOrogenyAge(s, coarseGrid, *hist);
            ctx.data.orogenyAge[t] = toOrogenyAgeU16(blendedOrogenic);
        }

        ctx.reportProgress(0.05f + static_cast<float>(end) / static_cast<float>(N) * 0.90f);
    });

    ctx.world.validFields |= static_cast<uint32_t>(WorldField::PlateId);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::Flags);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::CrustAge);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::OrogenyAge);
    // BoundaryType and BoundaryDistance are written by TerrainStage, not here.
    // TerrainStage also inherits the plates list we built above.

    ctx.reportProgress(1.0f);
}

} // namespace worldgen
