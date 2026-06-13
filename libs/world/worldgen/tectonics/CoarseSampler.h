#pragma once

// CoarseSampler — domain-warped lookup from a full-res tile into the coarse
// TectonicHistory grid, plus the smooth-field sampling the upsampler needs.
//
// Used by CrustStage (M-T3) and reused by TerrainStage rewrite (M-T4) so all
// full-res tectonic fields (crustAge, thicknessKm, orogenyAge, orogenyIntensity,
// volcanism, convergence) stay spatially coherent — they all warp through the same
// displacement (warpedCoarseDir) and interpolate through the same C0 sampler
// (smoothSampleAt) before use.
//
// Warp design:
//   Three independent fractalNoise3 octaves (3 octaves each, distinct seed offsets)
//   produce a Vec3 displacement; the warped direction is normalized and clamped to
//   the sphere. Amplitude ~0.6 * coarse cell angular diameter so the coastline
//   deviation at full-res always exceeds one coarse cell, guaranteeing that the
//   hex-aligned straight segments are broken up everywhere.
//
// Crust-type upsampling (M-T3.6): a warped binary crustType nearest-sample dithers
// the coast into confetti, so CrustStage instead thresholds a SMOOTH signed-distance
// field (buildSignedDistanceField) at 0, sampled via smoothSampleAt + crenulation.
// Age/plate values then come from nearestMatchingCrustTile so a flipped tile inherits
// values of its decided crust type, not the cell it warped into.
//
// Determinism: every method is a pure function of its inputs (tile id, coarseGrid,
// crustType, noises, seeds). No mutable state; the per-tile samplers are safe to call
// from parallelFor slabs (buildSignedDistanceField runs once, single-threaded).

#include "worldgen/tectonics/TectonicHistory.h"
#include "worldgen/tectonics/TectonicParams.h"
#include "worldgen/grid/SphereGrid.h"

#include <math/DeterministicMath.h>
#include <random/HashNoise.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

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

// Compute the warped unit direction AND its located coarse tile in one shot.
// The warp is a per-axis fractalNoise3 displacement on the unit-sphere coords,
// scaled by warpAmp; the returned point lets callers do inverse-distance smooth
// sampling against the warped position. The located tile `tile` is the cell
// containing the warped point (never kInvalidTile post-finalize — the coarse grid
// has no gaps; falls back to the unwarped lookup if the warp lands off-grid).
//
// warpAmp: pass warpAmplitude(coarseGrid) or a precomputed value.
// seedX/Y/Z: three distinct seed offsets, e.g. seed+0, seed+1, seed+2.
struct WarpResult {
    Vec3d  point;  // warped unit direction
    TileId tile;   // coarse tile containing `point`
};

inline WarpResult warpedCoarseDir(Vec3d tileCenter,
                                  const SphereGrid& coarseGrid,
                                  float warpAmp,
                                  uint32_t seedX, uint32_t seedY, uint32_t seedZ) {
    const float cx = static_cast<float>(tileCenter.x);
    const float cy = static_cast<float>(tileCenter.y);
    const float cz = static_cast<float>(tileCenter.z);

    constexpr int   kOctaves    = 3;
    constexpr float kFreq       = 6.0f;
    constexpr float kLacunarity = 2.0f;
    constexpr float kGain       = 0.5f;

    const float dx = foundation::fractalNoise3(cx * kFreq, cy * kFreq, cz * kFreq,
                                                seedX, kOctaves, kLacunarity, kGain) * warpAmp;
    const float dy = foundation::fractalNoise3(cx * kFreq, cy * kFreq, cz * kFreq,
                                                seedY, kOctaves, kLacunarity, kGain) * warpAmp;
    const float dz = foundation::fractalNoise3(cx * kFreq, cy * kFreq, cz * kFreq,
                                                seedZ, kOctaves, kLacunarity, kGain) * warpAmp;

    double wx = tileCenter.x + dx;
    double wy = tileCenter.y + dy;
    double wz = tileCenter.z + dz;
    double len = foundation::det_math::sqrt(wx*wx + wy*wy + wz*wz);
    if (len < 1e-12) { wx = tileCenter.x; wy = tileCenter.y; wz = tileCenter.z; len = 1.0; }
    wx /= len; wy /= len; wz /= len;

    Vec3d p{wx, wy, wz};
    TileId s = coarseGrid.fromUnitVector(p);
    if (s == kInvalidTile) s = coarseGrid.fromUnitVector(tileCenter);
    return {p, s};
}

// Smooth-sample an arbitrary coarse float field at a warped point.
//
// Inverse-distance blend over the located cell `s` and its hex neighbors, weighting
// each by 1/(d^2 + eps) where d is the angular chord distance from the warped point
// `at` to that cell's center. Unlike blendSmoothField (which weights by inter-cell
// distance and restricts to same crust type), this is a true bilinear-style
// interpolation against the sample position, so the result is C0-continuous as the
// warped point crosses cell boundaries — no quantization steps.
//
// This is the shared smooth-scalar API M-T4 reuses: thicknessKm, orogenyIntensity,
// volcanism, and convergence all warp through warpedCoarseDir() then sample here, so
// every full-res tectonic field stays spatially coherent under the same displacement.
//
// getField: TileId -> float. eps guards the center (d=0) weight from blowing up.
template <typename FieldAccessor>
float smoothSampleAt(Vec3d at,
                     TileId s,
                     const SphereGrid& coarseGrid,
                     FieldAccessor getField,
                     float eps = 1e-4f) {
    auto chord2 = [&](TileId t) -> float {
        Vec3d c = coarseGrid.tileCenter(t);
        double dx = at.x - c.x, dy = at.y - c.y, dz = at.z - c.z;
        return static_cast<float>(dx*dx + dy*dy + dz*dz);
    };

    float wSum = 1.0f / (chord2(s) + eps);
    float fSum = wSum * getField(s);

    std::array<TileId, 6> nbrs{};
    uint32_t nCount = coarseGrid.neighbors(s, nbrs);
    for (uint32_t k = 0; k < nCount; ++k) {
        TileId nb = nbrs[k];
        float w = 1.0f / (chord2(nb) + eps);
        wSum += w;
        fSum += w * getField(nb);
    }
    return fSum / wSum;
}

// Signed distance (in coarse-cell units) to the crust-type boundary, one value per
// coarse tile. Multi-source BFS from every coast cell (a cell with >= 1 neighbor of
// a different crustType) gives an unsigned ring distance; signed + inside continental,
// - inside oceanic. Then kCoastSdfSmoothPasses light Jacobi passes soften the hex-ring
// quantization so the eventual 0 level set is organic, not stepped.
//
// Determinism: BFS frontier processed in ascending TileId order each ring; neighbor
// order is the fixed SphereGrid order. Pure function of (crustType, grid).
inline std::vector<float> buildSignedDistanceField(const SphereGrid& grid,
                                                   const std::vector<uint8_t>& crustType) {
    const uint32_t N = grid.tileCount();
    constexpr int32_t kUnset = 0x7FFFFFFF;

    // ringDist[t] = BFS distance (rings) from the nearest coast cell. Coast cells = 0.
    std::vector<int32_t> ringDist(N, kUnset);

    // Seed: every cell adjacent to a different crust type is a coast cell (ring 0).
    // Collect into the current frontier in ascending TileId order.
    std::vector<TileId> frontier;
    std::array<TileId, 6> nbrs{};
    for (TileId t = 0; t < N; ++t) {
        uint8_t ct = crustType[t];
        uint32_t nc = grid.neighbors(t, nbrs);
        bool isCoast = false;
        for (uint32_t k = 0; k < nc; ++k) {
            if (crustType[nbrs[k]] != ct) { isCoast = true; break; }
        }
        if (isCoast) { ringDist[t] = 0; frontier.push_back(t); }
    }

    // Multi-source BFS, ring by ring. frontier is already ascending; each next ring
    // is built ascending because we scan the (ascending) frontier and push neighbors
    // in fixed order, then sort the next frontier to restore ascending order.
    int32_t ring = 0;
    while (!frontier.empty()) {
        std::vector<TileId> next;
        for (TileId t : frontier) {
            uint32_t nc = grid.neighbors(t, nbrs);
            for (uint32_t k = 0; k < nc; ++k) {
                TileId nb = nbrs[k];
                if (ringDist[nb] == kUnset) {
                    ringDist[nb] = ring + 1;
                    next.push_back(nb);
                }
            }
        }
        std::sort(next.begin(), next.end());
        frontier.swap(next);
        ++ring;
    }

    // Sign: + for continental, - for oceanic. Coast cells (ring 0) keep their side's
    // sign so the 0 crossing falls between a continental and an oceanic coast cell.
    std::vector<float> sdf(N, 0.0f);
    for (TileId t = 0; t < N; ++t) {
        float d = static_cast<float>(ringDist[t] == kUnset ? 0 : ringDist[t]);
        bool cont = crustType[t] == static_cast<uint8_t>(CrustType::Continental);
        sdf[t] = cont ? d : -d;
    }

    // Light Jacobi smoothing: blend each cell toward its neighbor mean. Softens the
    // discrete ring steps without moving the 0 level set far (symmetric blur).
    std::vector<float> tmp(N);
    for (int pass = 0; pass < kCoastSdfSmoothPasses; ++pass) {
        for (TileId t = 0; t < N; ++t) {
            uint32_t nc = grid.neighbors(t, nbrs);
            float sum = 0.0f;
            for (uint32_t k = 0; k < nc; ++k) sum += sdf[nbrs[k]];
            float mean = nc > 0 ? sum / static_cast<float>(nc) : sdf[t];
            tmp[t] = sdf[t] + kCoastSdfSmoothWeight * (mean - sdf[t]);
        }
        sdf.swap(tmp);
    }
    return sdf;
}

// Find the nearest coarse cell with crustType == wantType, searching the located
// cell `s`, then its hex neighbors, then an outward BFS ring. Used so a tile whose
// SDF threshold pushed it to a crust type different from the cell it warped into can
// still inherit age/plate values from a cell of the MATCHING type (never age-0
// oceanic values on a continental tile, nor stale continental ages on an ocean tile).
//
// Returns s itself if no matching cell is found within maxRings (deep inside a
// flipped region — rare); the caller documents that fallback. Deterministic:
// neighbor scan in fixed order, nearest-by-chord tie-broken by smaller TileId.
inline TileId nearestMatchingCrustTile(Vec3d at,
                                       TileId s,
                                       const SphereGrid& grid,
                                       const std::vector<uint8_t>& crustType,
                                       uint8_t wantType,
                                       int maxRings = 3) {
    if (crustType[s] == wantType) return s;

    auto chord2 = [&](TileId t) -> double {
        Vec3d c = grid.tileCenter(t);
        double dx = at.x - c.x, dy = at.y - c.y, dz = at.z - c.z;
        return dx*dx + dy*dy + dz*dz;
    };

    // Outward BFS over <= maxRings, picking the nearest matching cell on the first
    // ring that has one. A 3-ring hex BFS visits ~37 cells, so the visited set is a
    // small local list — never an O(N) allocation (this is called per coast-band
    // full-res tile, millions of times at n=1024).
    // A hex BFS ring r has ~6r cells; over maxRings (3 by default) the visited set
    // and any single frontier stay well under these caps. Sized with headroom; the
    // bounded buffers never truncate a real ring, so the BFS is complete.
    constexpr size_t kCap = 96;
    std::array<TileId, kCap> visited{};
    size_t visitedCount = 0;
    auto isVisited = [&](TileId t) {
        for (size_t i = 0; i < visitedCount; ++i) if (visited[i] == t) return true;
        return false;
    };
    auto markVisited = [&](TileId t) {
        if (visitedCount < kCap) visited[visitedCount++] = t;
    };

    std::array<TileId, kCap> frontier{};
    std::array<TileId, kCap> nextBuf{};
    frontier[0] = s; size_t frontierCount = 1;
    markVisited(s);

    std::array<TileId, 6> nbrs{};
    for (int ring = 0; ring < maxRings; ++ring) {
        size_t nextCount = 0;
        TileId best = kInvalidTile;
        double bestD = 0.0;
        for (size_t fi = 0; fi < frontierCount; ++fi) {
            uint32_t nc = grid.neighbors(frontier[fi], nbrs);
            for (uint32_t k = 0; k < nc; ++k) {
                TileId nb = nbrs[k];
                if (isVisited(nb)) continue;
                markVisited(nb);
                if (nextCount < kCap) nextBuf[nextCount++] = nb;
                if (crustType[nb] == wantType) {
                    double d = chord2(nb);
                    if (best == kInvalidTile || d < bestD ||
                        (d == bestD && nb < best)) {
                        best = nb; bestD = d;
                    }
                }
            }
        }
        if (best != kInvalidTile) return best;
        if (nextCount == 0) break;
        // Carry the next ring forward (deterministic: ascending order).
        std::sort(nextBuf.begin(), nextBuf.begin() + nextCount);
        frontierCount = nextCount;
        for (size_t i = 0; i < nextCount; ++i) frontier[i] = nextBuf[i];
    }
    return s; // fallback: no matching cell nearby; caller keeps s's values
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
