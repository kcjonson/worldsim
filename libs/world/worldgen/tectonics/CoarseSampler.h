#pragma once

// CoarseSampler — domain-warped lookup from a full-res tile into the coarse
// TectonicHistory grid, plus neighbor-blended field sampling.
//
// Used by CrustStage (M-T3) and reused by TerrainStage rewrite (M-T4) so all
// full-res tectonic fields (crustAge, thicknessKm, orogenyAge, orogenyIntensity,
// volcanism, convergence) stay spatially coherent — they all warp through the
// same displacement before sampling.
//
// Warp design:
//   Three independent fractalNoise3 octaves (3 octaves each, distinct seed offsets)
//   produce a Vec3 displacement; the warped direction is normalized and clamped to
//   the sphere. Amplitude ~0.6 * coarse cell angular diameter so the coastline
//   deviation at full-res always exceeds one coarse cell, guaranteeing that the
//   hex-aligned straight segments are broken up everywhere.
//
// Determinism: every method is a pure function of its inputs (tile id, coarseGrid,
// noises, seeds). No mutable state; safe to call from parallelFor slabs.

#include "worldgen/tectonics/TectonicHistory.h"
#include "worldgen/tectonics/TectonicParams.h"
#include "worldgen/grid/SphereGrid.h"

#include <math/DeterministicMath.h>
#include <random/HashNoise.h>

#include <array>
#include <cstdint>

namespace worldgen::tectonics {

// Angular diameter of one coarse cell (radians).
// Coarse cell ~= 2/sqrt(N_coarse) where N_coarse = grid->tileCount().
// At kCoarseN=128 this is ~2/sqrt(163842) ≈ 0.00494 rad.
inline float coarseCellAngularSize(const SphereGrid& coarseGrid) {
    const double n = static_cast<double>(coarseGrid.tileCount());
    return static_cast<float>(2.0 / foundation::det_math::sqrt(n));
}

// Warp amplitude: 0.6 * coarse angular size (ensures > 0.5 cell displacement,
// breaking coarse-hex coastline aliasing while staying sub-cell for smooth fields).
inline float warpAmplitude(const SphereGrid& coarseGrid) {
    return 0.6f * coarseCellAngularSize(coarseGrid);
}

// Compute the warped coarse-grid tile for a full-res tile center.
// The warp is a per-axis fractalNoise3 displacement on the unit-sphere coords,
// scaled by warpAmp. Result is a coarse TileId (never kInvalidTile post-finalize
// because the coarse grid has no gaps).
//
// warpAmp: pass warpAmplitude(coarseGrid) or a precomputed value.
// seedX/Y/Z: three distinct seed offsets, e.g. seed+0, seed+1, seed+2.
inline TileId warpedCoarseTile(Vec3d tileCenter,
                                const SphereGrid& coarseGrid,
                                float warpAmp,
                                uint32_t seedX, uint32_t seedY, uint32_t seedZ) {
    const float cx = static_cast<float>(tileCenter.x);
    const float cy = static_cast<float>(tileCenter.y);
    const float cz = static_cast<float>(tileCenter.z);

    // Three independent noise channels, 3 octaves each.
    constexpr int   kOctaves   = 3;
    constexpr float kFreq      = 6.0f;   // ~0.6 rad^-1 on unit sphere, detail ~10 deg
    constexpr float kLacunarity = 2.0f;
    constexpr float kGain      = 0.5f;

    const float dx = foundation::fractalNoise3(cx * kFreq, cy * kFreq, cz * kFreq,
                                                seedX, kOctaves, kLacunarity, kGain)
                     * warpAmp;
    const float dy = foundation::fractalNoise3(cx * kFreq, cy * kFreq, cz * kFreq,
                                                seedY, kOctaves, kLacunarity, kGain)
                     * warpAmp;
    const float dz = foundation::fractalNoise3(cx * kFreq, cy * kFreq, cz * kFreq,
                                                seedZ, kOctaves, kLacunarity, kGain)
                     * warpAmp;

    double wx = tileCenter.x + dx;
    double wy = tileCenter.y + dy;
    double wz = tileCenter.z + dz;

    double len = foundation::det_math::sqrt(wx*wx + wy*wy + wz*wz);
    if (len < 1e-12) { wx = tileCenter.x; wy = tileCenter.y; wz = tileCenter.z; len = 1.0; }
    wx /= len; wy /= len; wz /= len;

    TileId s = coarseGrid.fromUnitVector({wx, wy, wz});
    if (s == kInvalidTile) {
        // Fallback: unwarped lookup (should never happen on a valid grid)
        s = coarseGrid.fromUnitVector(tileCenter);
    }
    return s;
}

// Blend result for smooth scalar fields. Carries the weighted value and the
// sum of weights so callers can normalize multiple blends together if needed.
struct BlendResult {
    float value{};
    float totalWeight{};
};

// Inverse-distance blend of a float field from the coarse tile s and its
// neighbors, RESTRICTED to neighbors with the same crustType as s (no
// cross-coast bleed into continental for oceanic tiles and vice versa).
//
// distWeightExp: exponent on 1/distance weighting (2 = inv-sq; 1 = inv-linear).
// A tile at distance 0 (the center) gets weight 1; each neighbor at distance d
// gets weight 1/(d^distWeightExp). The coarse tile center and each neighbor are
// at ~1 coarse cell apart, so distance ~1.0 is nominal.
template <typename FieldAccessor>
BlendResult blendSmoothField(TileId s,
                              const SphereGrid& coarseGrid,
                              const TectonicHistory& hist,
                              FieldAccessor getField,
                              float distWeightExp = 2.0f) {
    const uint8_t ownCrustType = hist.crustType[s];
    float weightedSum = getField(s);
    float totalWeight = 1.0f;

    std::array<TileId, 6> nbrs{};
    uint32_t nCount = coarseGrid.neighbors(s, nbrs);
    for (uint32_t k = 0; k < nCount; ++k) {
        TileId nb = nbrs[k];
        if (hist.crustType[nb] != ownCrustType) continue;

        Vec3d cs = coarseGrid.tileCenter(s);
        Vec3d cn = coarseGrid.tileCenter(nb);
        double dx = cs.x - cn.x, dy = cs.y - cn.y, dz = cs.z - cn.z;
        double dist = foundation::det_math::sqrt(dx*dx + dy*dy + dz*dz);
        if (dist < 1e-12) continue;

        float w = 1.0f / static_cast<float>(
            distWeightExp > 1.5f ? dist * dist : dist);
        weightedSum += w * getField(nb);
        totalWeight  += w;
    }
    return {weightedSum / totalWeight, totalWeight};
}

// Nearest-value sampling for discrete/discontinuous fields (like orogenyAge
// when the age difference between neighbors exceeds a suture cutoff). Returns
// the value from the center tile s unchanged.
template <typename FieldAccessor>
auto nearestField(TileId s, FieldAccessor getField) -> decltype(getField(s)) {
    return getField(s);
}

// Blend orogenyAge with a suture-jump guard: use inverse-distance blend across
// neighbors with the SAME crustType UNLESS the neighbor orogenyAge differs by
// more than kOrogenyBlendCutoffMyr from the center, in which case that neighbor
// contributes the center value (nearest) to keep suture discontinuities sharp.
inline float blendOrogenyAge(TileId s,
                              const SphereGrid& coarseGrid,
                              const TectonicHistory& hist,
                              float kOrogenyBlendCutoffMyr = 80.0f) {
    const uint8_t ownCrustType = hist.crustType[s];

    const auto rawAge = [&](TileId t) -> float {
        int32_t v = hist.orogenyAge[t];
        return (v == tectonics::kOrogenyNever) ? 1e9f : static_cast<float>(v);
    };

    float centerAge = rawAge(s);
    float weightedSum = centerAge;
    float totalWeight  = 1.0f;

    std::array<TileId, 6> nbrs{};
    uint32_t nCount = coarseGrid.neighbors(s, nbrs);
    for (uint32_t k = 0; k < nCount; ++k) {
        TileId nb = nbrs[k];
        if (hist.crustType[nb] != ownCrustType) continue;

        Vec3d cs = coarseGrid.tileCenter(s);
        Vec3d cn = coarseGrid.tileCenter(nb);
        double dx = cs.x - cn.x, dy = cs.y - cn.y, dz = cs.z - cn.z;
        double dist = foundation::det_math::sqrt(dx*dx + dy*dy + dz*dz);
        if (dist < 1e-12) continue;

        float w = 1.0f / static_cast<float>(dist * dist);
        float nbAge = rawAge(nb);
        // Suture jump guard: large age difference -> use nearest (center value)
        float contrib = (std::abs(nbAge - centerAge) > kOrogenyBlendCutoffMyr)
                        ? centerAge
                        : nbAge;
        weightedSum += w * contrib;
        totalWeight  += w;
    }
    return weightedSum / totalWeight;
}

} // namespace worldgen::tectonics
