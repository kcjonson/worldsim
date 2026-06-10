// TerrainStage — M3b implementation.
//
// Algorithm overview:
//   1. Boundary classification: tiles with any differently-plated neighbor are boundary.
//      Relative plate velocity decomposed into convergence (normal) and shear (tangential)
//      components; classified into BoundaryType enum. Two smoothing passes (mode filter
//      over adjacent boundary tiles) prevent flickering classification.
//   2. Distance fields: multi-source BFS from all boundary tiles; produces boundaryDistance
//      (uint16, saturating at 65535), plus transient arrays: bndType, bndSide (overriding
//      or subducting) used only during elevation synthesis.
//      2b. Distance smoothing: three Jacobi passes average each tile's integer BFS distance
//          with its neighbors → removes stair-step terracing on steep slopes.
//          A per-tile fractional hash offset (hashNoise * 0.5 tiles) is added to further
//          break discretization without introducing non-determinism.
//      2c. Belt-end taper: for each boundary tile, count same-type boundary neighbors
//          within 4 hops along the boundary graph. Amplitude taper = smoothstep over
//          that count, preventing hard cutoffs at convergent segment ends.
//   3. Elevation synthesis: per-tile, parallel.
//      elevation = base + uplift + noise + hotspot
//      - Base: continental +400m, oceanic -4200m; continental shelf ramp near crust edges.
//      - Uplift kernels: side-aware Gaussian falloff by BoundaryType and distance.
//      - Noise: fractalNoise3 + ridgedNoise3; kept subordinate (capped at 40% of |uplift|+base).
//      - Hotspot chains: ~K/4 plumes; chain centers stored as tile ids; hotspot elevation
//        accumulated into a per-tile array via a single global BFS from all chain centers.
//      - Hard elevation cap: total elevation clamped to 9000m (Everest-style planetary max).
//   4. Sea level: histogram quantile at waterAmount (4096 bins).
//   5. Sets WorldField::Elevation | BoundaryType | BoundaryDistance.
//
// Transient memory peak: ~14 vectors of float/int over N tiles ≈ 56 bytes/tile.
// At n=1024 (N=10.5M): ~590 MB peak transient, all released before stage return.
//
// Determinism:
//   - All random numbers from Pcg32 or HashNoise; no std transcendentals (det_math only).
//   - BFS uses FIFO with tile-id-ordered seed initialization → deterministic visit order:
//     seeds enqueued in ascending tile-id order, FIFO never reorders them.
//   - Jacobi distance smoothing uses double-buffering (read one, write other) so tile
//     visit order is irrelevant → identical result at any thread count.
//   - Belt-end BFS is also FIFO with ascending tile-id seed order.
//   - parallelFor uses fixed grainSize slabs → same slab boundaries at any thread count.
//   - Euler cross products use only +-*/ → IEEE 754 basic ops, bit-identical everywhere.
//
// Kernel constants (iteration 3):
//   ConvergentCC:  A=7000m * conv * ridgeMod(0.7-1.3), sigma=280km, both sides
//   ConvergentCO overriding arc: A=4500m * conv, center=180km, sigma=100km; forearc: -600m
//   ConvergentCO subducting trench: A=-6000m, sigma=70km
//   ConvergentOO overriding arc: A=5500m * ridgeMod * conv, center=150km, sigma=60km
//   ConvergentOO subducting trench: A=-7000m, sigma=50km
//   Divergent oceanic ridge: A=2500m, sigma=150km; axial valley: -500m, sigma=20km
//   Divergent continental: rift -1500m, sigma=60km; shoulder +800m at 80km, sigma=45km
//   Transform: ±400m * noise, sigma=50km
//   Hotspot: capped at 3500m per tile.
//   Classification: convergent/divergent when |convergence| > 0.35*|v_rel|; transform otherwise.
//   Convergence normalized by 90th percentile of positive values (not global max).
//   All mountain amplitudes multiplied by ageFactor. Sigma multiplied by sigmaAgeFactor.
//   Hard elevation cap: 9000m (no stacking of base+uplift+noise+hotspot can exceed this).

#include "worldgen/stages/TerrainStage.h"

#include "worldgen/data/WorldData.h"

#include <math/DeterministicMath.h>
#include <random/HashNoise.h>
#include <random/Pcg32.h>

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

// Gaussian falloff via det_math::exp for cross-platform determinism.
inline float gaussianFalloff(float d, float sigma) {
    if (sigma <= 0.0f) return 0.0f;
    double x = static_cast<double>(d) / static_cast<double>(sigma);
    return static_cast<float>(foundation::det_math::exp(-0.5 * x * x));
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

    // Sub-seeds for each independent RNG stream.
    const auto stageSeed32 = static_cast<uint32_t>(ctx.stageSeed ^ (ctx.stageSeed >> 32));
    const uint32_t seedFractal = stageSeed32 ^ 0xA3C5E7F1u;
    const uint32_t seedRidged  = stageSeed32 ^ 0x1B2D4E6Fu;
    const uint32_t seedHotspot = stageSeed32 ^ 0xDEADBEEFu;

    // tile width in km (equatorial approximation: circumference / sqrt(N))
    const double kPlanetCircumference = 2.0 * 3.14159265358979323846 *
                                        ctx.derived.planetRadiusMeters;
    const double tileWidthM   = kPlanetCircumference / foundation::det_math::sqrt(static_cast<double>(N));
    const float  tileWidthKm  = static_cast<float>(tileWidthM / 1000.0);

    auto kmToTiles = [&](float km) -> float { return km / tileWidthKm; };

    // Age modulation
    double ageFactor = 4.5e9 / ctx.params.planetAge;
    if (ageFactor < 0.55) ageFactor = 0.55;
    if (ageFactor > 1.60) ageFactor = 1.60;
    double sigmaAgeFactor = 1.0 / foundation::det_math::sqrt(ageFactor);

    // =========================================================================
    // 1. Boundary classification
    // =========================================================================

    std::vector<BType>   bndTypeRaw(N, BType::None);
    std::vector<uint8_t> bndSideRaw(N, kSideSymmetric);
    std::vector<uint8_t> bndPlate(N, 255u);
    std::vector<float>   bndConvRaw(N, 0.0f);
    std::vector<bool>    isBoundary(N, false);

    {
        std::array<TileId, 8> nbrs{};
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
            std::array<TileId, 8> nbrs{};
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
    //     BFS is seeded in ascending tile-id order → deterministic.
    //     Interior tiles receive the propagated taper value from their nearest
    //     boundary tile (via the multi-source BFS in section 2).
    // =========================================================================

    std::vector<float> beltTaper(N, 1.0f);
    {
        constexpr int32_t kTaperHops = 4;
        constexpr float   kTaperMax  = 12.0f;

        std::vector<int32_t> epoch(N, -1);
        std::vector<uint32_t> bfsQ;
        bfsQ.reserve(256);
        std::array<TileId, 8> tbNbrs{};

        int32_t baseEpoch = 0;
        for (uint32_t t = 0; t < N; ++t) {
            // Use bndTypeRaw (already smoothed at this point) rather than
            // ctx.data.boundaryType, which isn't written until after this block.
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
    //    bfsBndTaper propagated from the seed boundary tile to every interior
    //    tile in its BFS region (same mechanism as bndType/bndSide/bndConv).
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
        std::array<TileId, 8> nbrs{};
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
    //     terracing caused by integer BFS tile distances on steep slopes.
    //     Double-buffered: reads distA, writes distB, then swaps.
    //     Result is float to preserve sub-tile precision; a fractional hash
    //     offset is added to further break discretization bands.
    // =========================================================================

    std::vector<float> smoothDist(N, 0.0f);
    {
        // Jitter FIRST, then Jacobi-smooth.  Adding noise to the integer BFS
        // distances before averaging means each iso-distance shell gets blurred
        // into its neighbors, eliminating the stair-step bands that appear with
        // tight-sigma kernels (trench sigma ~1.75 tiles: one tile step = 25% drop).
        // Jitter amplitude 1.2 tiles: large enough to span adjacent shells on the
        // steepest kernels; smoothing then blends them continuously.
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
        std::array<TileId, 8> jNbrs{};

        // Three Jacobi passes blend jittered shells into a continuous float field.
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

    // Free no-longer-needed data
    isBoundary.clear();  isBoundary.shrink_to_fit();
    bndTypeRaw.clear();  bndTypeRaw.shrink_to_fit();
    bndSideRaw.clear();  bndSideRaw.shrink_to_fit();
    bndConvNorm.clear(); bndConvNorm.shrink_to_fit();
    beltTaper.clear();   beltTaper.shrink_to_fit();

    ctx.reportProgress(0.25f);
    throwIfCancelled(ctx);

    // =========================================================================
    // 2d. Crust-edge distance BFS (for continental shelf ramp).
    // =========================================================================

    std::vector<int32_t> crustEdgeDist(N, -1);
    {
        std::vector<uint32_t> bfsQueue;
        bfsQueue.reserve(N / 4);
        std::array<TileId, 8> nbrs{};
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
            std::array<TileId, 8> nbrs2{};
            uint32_t cnt = ctx.grid.neighbors(t, nbrs2);
            for (uint32_t k = 0; k < cnt; ++k) {
                TileId nb = nbrs2[k];
                if (crustEdgeDist[nb] < 0) { crustEdgeDist[nb] = nd; bfsQueue.push_back(nb); }
            }
        }
    }

    ctx.reportProgress(0.30f);
    throwIfCancelled(ctx);

    // =========================================================================
    // 3. Hotspot chains — per-chain-point bounded BFS with accumulation:
    //    For each plume chain, store (centerTileId, amplitude) pairs.
    //    For each chain point, run a bounded BFS limited to kHotspotMaxTiles hops;
    //    accumulate Gaussian elevation contributions into hotspotElev[] per tile.
    //    Tiles within range of multiple chain points accumulate all contributions.
    //
    //    Each bounded BFS touches O(kHotspotMaxTiles^2) tiles; with ~nHotspots*kChainLen
    //    centers this is much less than O(N) total, so it is efficient.
    //    BFS terminates when distance > kHotspotMaxTiles.
    // =========================================================================

    const int  nHotspots         = std::max(2, K / 4);
    const int  kChainLen         = 12;
    const float kChainSpacingKm  = 200.0f;
    const float chainSpacingTiles = kmToTiles(kChainSpacingKm);
    const float hotspotSigmaTiles = kmToTiles(55.0f);
    const int   kHotspotMaxTiles  = static_cast<int>(hotspotSigmaTiles * 3.5f) + 2;

    // Per-tile hotspot elevation accumulator
    std::vector<float> hotspotElev(N, 0.0f);

    {
        // Collect all (tileId, amplitude) chain points
        struct ChainPoint { TileId tile; float amplitude; };
        std::vector<ChainPoint> chainPts;
        chainPts.reserve(static_cast<size_t>(nHotspots * kChainLen));

        foundation::Pcg32 pcgHot(static_cast<uint64_t>(seedHotspot));

        for (int h = 0; h < nHotspots; ++h) {
            // Random point on unit sphere (rejection sampling)
            double px{}, py{}, pz{};
            for (;;) {
                px = pcgHot.nextDouble() * 2.0 - 1.0;
                py = pcgHot.nextDouble() * 2.0 - 1.0;
                pz = pcgHot.nextDouble() * 2.0 - 1.0;
                double r2 = px*px + py*py + pz*pz;
                if (r2 > 0.01 && r2 <= 1.0) {
                    double inv = 1.0 / foundation::det_math::sqrt(r2);
                    px *= inv; py *= inv; pz *= inv; break;
                }
            }
            TileId plumeTile = ctx.grid.fromUnitVector({px, py, pz});
            if (plumeTile == kInvalidTile) continue;
            uint8_t plumeplate = ctx.data.plateId[plumeTile];
            if (plumeplate >= static_cast<uint8_t>(K)) continue;

            const auto& pl = ctx.world.plates[static_cast<size_t>(plumeplate)];
            double omegaX = pl.eulerPole.x * static_cast<double>(pl.angularSpeed);
            double omegaY = pl.eulerPole.y * static_cast<double>(pl.angularSpeed);
            double omegaZ = pl.eulerPole.z * static_cast<double>(pl.angularSpeed);

            const double twoPI = 6.283185307179586;
            double sqrtN       = foundation::det_math::sqrt(static_cast<double>(N));
            double angleStepRad = twoPI * (static_cast<double>(chainSpacingTiles) / sqrtN);

            double cx = px, cy = py, cz = pz;
            for (int s = 0; s < kChainLen; ++s) {
                float amplitude = static_cast<float>(5500.0 - s * 380.0);
                if (amplitude < 150.0f) amplitude = 150.0f;

                TileId centerTile = ctx.grid.fromUnitVector({cx, cy, cz});
                if (centerTile != kInvalidTile) {
                    chainPts.push_back({centerTile, amplitude});
                }

                // Rotate backward along plate motion
                double theta    = -angleStepRad;
                double cosTheta = foundation::det_math::cos(theta);
                double sinTheta = foundation::det_math::sin(theta);
                double omLen    = length3d(omegaX, omegaY, omegaZ);
                double ex{}, ey{}, ez{};
                if (omLen > 1e-12) {
                    ex = omegaX/omLen; ey = omegaY/omLen; ez = omegaZ/omLen;
                } else {
                    ex = 0.0; ey = 0.0; ez = 1.0;
                }
                double edotP = dot3d(ex, ey, ez, cx, cy, cz);
                double crossX{}, crossY{}, crossZ{};
                cross3d(ex, ey, ez, cx, cy, cz, crossX, crossY, crossZ);
                double nx = cx * cosTheta + crossX * sinTheta + ex * edotP * (1.0 - cosTheta);
                double ny = cy * cosTheta + crossY * sinTheta + ey * edotP * (1.0 - cosTheta);
                double nz = cz * cosTheta + crossZ * sinTheta + ez * edotP * (1.0 - cosTheta);
                double nlen = length3d(nx, ny, nz);
                if (nlen > 1e-12) { cx = nx/nlen; cy = ny/nlen; cz = nz/nlen; }
            }
        }

        // Global BFS from all chain centers, limited to kHotspotMaxTiles steps.
        // For each tile, track (closest center index, bfs distance).
        // We don't need to track which center owns a tile — just accumulate Gaussian
        // contributions from ALL centers within range.
        //
        // To keep this O(N) instead of O(N * nChainPts): do a multi-source BFS
        // seeded by all chain centers, and for each tile record the distance from
        // its nearest chain center. Then apply the Gaussian using that distance.
        // But multiple nearby chain centers all contribute independently — a tile
        // near two chain points should get contributions from both.
        //
        // Pragmatic approach: for each chain point do a BOUNDED BFS limited to
        // kHotspotMaxTiles. At n=512, kHotspotMaxTiles ≈ 10 tiles, so the BFS
        // touches only ~pi*10^2 ≈ 314 tiles per center. With nHotspots*kChainLen ≈ 36
        // centers, total BFS work ≈ 11000 tiles << N=2.6M. Efficient enough.
        //
        // We allocate a single int32 "tag" array to avoid re-allocating per center;
        // use the chain-point index + 1 as the tag, reset via a "visited this round"
        // epoch counter instead of zeroing the full array.

        std::vector<int32_t>  hotDist(N, -1);
        std::vector<uint32_t> hotQueue;
        hotQueue.reserve(kHotspotMaxTiles * kHotspotMaxTiles * 8);

        for (const auto& cp : chainPts) {
            if (cp.tile == kInvalidTile) continue;

            // BFS from this center, up to kHotspotMaxTiles hops
            hotQueue.clear();
            // We need a local visited array — use hotDist with a sentinel trick:
            // set hotDist[t] = d during this BFS, but reset with the collected queue afterward.
            hotQueue.push_back(cp.tile);
            hotDist[cp.tile] = 0;

            std::array<TileId, 8> hnbrs{};
            for (size_t qi = 0; qi < hotQueue.size(); ++qi) {
                uint32_t t  = hotQueue[qi];
                int32_t  d  = hotDist[t];
                if (d >= kHotspotMaxTiles) continue;

                uint32_t cnt = ctx.grid.neighbors(t, hnbrs);
                for (uint32_t k = 0; k < cnt; ++k) {
                    TileId nb = hnbrs[k];
                    if (hotDist[nb] < 0) {
                        hotDist[nb] = d + 1;
                        hotQueue.push_back(nb);
                    }
                }
            }

            // Apply Gaussian and accumulate; then reset hotDist for reuse
            for (uint32_t t2 : hotQueue) {
                float falloff = gaussianFalloff(static_cast<float>(hotDist[t2]),
                                                hotspotSigmaTiles);
                hotspotElev[t2] += cp.amplitude * falloff;
                hotDist[t2] = -1; // reset for next chain point
            }
        }
    }

    ctx.reportProgress(0.38f);
    throwIfCancelled(ctx);

    // =========================================================================
    // 4. Elevation synthesis — parallel per tile.
    // =========================================================================

    // Kernel sigma values in tiles (age-modulated)
    const float sigCC       = static_cast<float>(sigmaAgeFactor) * kmToTiles(280.0f);
    const float sigCO_arc   = static_cast<float>(sigmaAgeFactor) * kmToTiles(100.0f);
    const float sigCO_trnc  = static_cast<float>(sigmaAgeFactor) * kmToTiles(70.0f);
    const float dCO_arc     = kmToTiles(180.0f);
    const float sigOO_arc   = static_cast<float>(sigmaAgeFactor) * kmToTiles(60.0f);
    const float sigOO_trnc  = static_cast<float>(sigmaAgeFactor) * kmToTiles(50.0f);
    const float dOO_arc     = kmToTiles(150.0f);
    const float sigDiv_rdg  = static_cast<float>(sigmaAgeFactor) * kmToTiles(150.0f);
    const float sigDiv_axv  = kmToTiles(20.0f);
    const float sigDiv_rft  = static_cast<float>(sigmaAgeFactor) * kmToTiles(60.0f);
    const float sigDiv_shl  = static_cast<float>(sigmaAgeFactor) * kmToTiles(45.0f);
    const float dDiv_shl    = kmToTiles(80.0f);
    const float sigTrn      = kmToTiles(50.0f);
    const float shelfBlend  = kmToTiles(60.0f);

    const float ageAmp = static_cast<float>(ageFactor);
    constexpr size_t kGrainSize = 4096;

    ctx.pool.parallelFor(0, N, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        for (size_t t = begin; t < end; ++t) {
            const bool    isCrust  = (ctx.data.flags[t] & kFlagContinentalCrust) != 0;
            const BType   bt       = static_cast<BType>(bfsBndType[t]);
            const uint8_t side     = bfsBndSide[t];
            const float   distT    = smoothDist[t]; // smoothed float distance (Jacobi + jitter)
            const float   conv     = bfsBndConv[t];
            const float   taperAmp = bfsBndTaper[t]; // along-boundary end taper, propagated to interior tiles

            // ---- Base ----
            float base;
            {
                float cedge = static_cast<float>(crustEdgeDist[t] >= 0 ? crustEdgeDist[t] : 9999);
                if (isCrust) {
                    // Smoothstep from shelf (-2500m) to continental (+400m)
                    float tt = cedge / shelfBlend;
                    if (tt > 1.0f) tt = 1.0f;
                    float sm = tt * tt * (3.0f - 2.0f * tt);
                    base = -2500.0f + sm * 2900.0f;
                } else {
                    // Oceanic: shelf blend from -2500 to -4200 away from crust edge
                    float tt = cedge / (shelfBlend * 1.5f);
                    if (tt > 1.0f) tt = 1.0f;
                    float sm = tt * tt * (3.0f - 2.0f * tt);
                    base = -2500.0f - sm * 1700.0f;
                }
            }

            // Tile center needed by both uplift and noise kernels
            Vec3d ctr = ctx.grid.tileCenter(static_cast<uint32_t>(t));
            float cx  = static_cast<float>(ctr.x);
            float cy  = static_cast<float>(ctr.y);
            float cz  = static_cast<float>(ctr.z);

            // ---- Uplift ----
            float uplift = 0.0f;
            switch (bt) {
                case BType::ConvergentCC: {
                    // Both-sides uplift, amplitude scales with convergence.
                    // Ridged noise (0.7..1.3x) gives crest variation so ranges aren't flat-topped.
                    // taperAmp fades to 0 at segment ends → no hard belt cutoffs.
                    float amp = ageAmp * (5000.0f + 2000.0f * conv);
                    float ridgeMod = 0.7f + 0.6f * foundation::ridgedNoise3(
                        cx * 4.0f, cy * 4.0f, cz * 4.0f,
                        seedRidged + 3u, 3, 2.0f, 0.5f);
                    uplift = amp * ridgeMod * gaussianFalloff(distT, sigCC) * taperAmp;
                    break;
                }
                case BType::ConvergentCO: {
                    if (side == kSideSubducting) {
                        // Oceanic trench depression at boundary
                        uplift = -6000.0f * gaussianFalloff(distT, sigCO_trnc) * taperAmp;
                    } else {
                        // Continental volcanic arc: peak at dCO_arc tiles inland
                        float arcAmp = ageAmp * (3500.0f + 1000.0f * conv);
                        uplift = arcAmp * gaussianFalloff(distT - dCO_arc, sigCO_arc) * taperAmp;
                        // Forearc basin (slight depression between trench and arc)
                        uplift += -600.0f * gaussianFalloff(distT, sigCO_trnc * 0.4f) * taperAmp;
                    }
                    break;
                }
                case BType::ConvergentOO: {
                    if (side == kSideSubducting) {
                        uplift = -7000.0f * gaussianFalloff(distT, sigOO_trnc) * taperAmp;
                    } else {
                        // Island arc with ridged-noise variation so only some peaks breach surface
                        float ridgeN = foundation::ridgedNoise3(
                            cx * 8.0f, cy * 8.0f, cz * 8.0f,
                            seedRidged + 1u, 3, 2.0f, 0.5f);
                        float arcAmp = ageAmp * (4000.0f + 1500.0f * conv) * (0.3f + 0.7f * ridgeN);
                        uplift = arcAmp * gaussianFalloff(distT - dOO_arc, sigOO_arc) * taperAmp;
                    }
                    break;
                }
                case BType::Divergent: {
                    if (!isCrust) {
                        // Oceanic mid-ocean ridge + axial valley
                        uplift  = 2500.0f * gaussianFalloff(distT, sigDiv_rdg);
                        uplift += -500.0f * gaussianFalloff(distT, sigDiv_axv);
                    } else {
                        // Continental rift valley + shoulder uplift
                        uplift  = -1500.0f * gaussianFalloff(distT, sigDiv_rft);
                        uplift += 800.0f   * gaussianFalloff(distT - dDiv_shl, sigDiv_shl);
                    }
                    break;
                }
                case BType::Transform: {
                    float shearN = foundation::fractalNoise3(
                        cx * 6.0f, cy * 6.0f, cz * 6.0f,
                        seedFractal + 7u, 3, 2.0f, 0.5f);
                    uplift = 400.0f * shearN * gaussianFalloff(distT, sigTrn);
                    break;
                }
                case BType::None:
                    break;
            }

            float noiseTotal;
            if (isCrust) {
                float baseNoise = foundation::fractalNoise3(cx*3.0f, cy*3.0f, cz*3.0f,
                                                            seedFractal, 6, 2.0f, 0.5f)
                                  * 700.0f;
                // Mountain mask: bias ridged noise toward high-uplift zones
                float upliftAbs  = uplift < 0.0f ? -uplift : uplift;
                float mMask      = upliftAbs / (upliftAbs + 1800.0f);
                float ridgedN    = foundation::ridgedNoise3(cx*1.5f, cy*1.5f, cz*1.5f,
                                                            seedRidged, 4, 2.0f, 0.5f)
                                   * 400.0f * mMask;
                noiseTotal = baseNoise + ridgedN;
            } else {
                // Oceanic abyssal hills + longer-wavelength variation
                float baseN  = foundation::fractalNoise3(cx*3.0f, cy*3.0f, cz*3.0f,
                                                         seedFractal + 3u, 5, 2.0f, 0.5f)
                               * 300.0f;
                float longN  = foundation::fractalNoise3(cx*1.2f, cy*1.2f, cz*1.2f,
                                                         seedFractal + 5u, 3, 2.0f, 0.5f)
                               * 400.0f;
                noiseTotal = baseN + longN;
            }

            // Cap: noise stays subordinate to tectonic signal
            float upliftAbs2 = uplift < 0.0f ? -uplift : uplift;
            float baseVar    = isCrust ? 2900.0f : 1950.0f;
            float noiseCap   = 0.40f * (upliftAbs2 + baseVar);
            if (noiseTotal >  noiseCap) noiseTotal =  noiseCap;
            if (noiseTotal < -noiseCap) noiseTotal = -noiseCap;

            // ---- Hotspot ----
            float hotspot = hotspotElev[t];
            if (hotspot > 3500.0f) hotspot = 3500.0f;

            // Hard cap: no real planet has terrain above ~9000m; prevents runaway
            // stacking from high-convergence CC belts + ridgeMod + noise + hotspot.
            float total = base + uplift + noiseTotal + hotspot;
            if (total > 9000.0f) total = 9000.0f;
            ctx.data.elevation[t] = total;
        }
        ctx.reportProgress(0.38f + static_cast<float>(end) / static_cast<float>(N) * 0.50f);
    });

    // Free transient arrays
    bfsBndType.clear();   bfsBndType.shrink_to_fit();
    bfsBndSide.clear();   bfsBndSide.shrink_to_fit();
    bfsBndConv.clear();   bfsBndConv.shrink_to_fit();
    bfsBndTaper.clear();  bfsBndTaper.shrink_to_fit();
    bfsDist.clear();      bfsDist.shrink_to_fit();
    smoothDist.clear();   smoothDist.shrink_to_fit();
    crustEdgeDist.clear(); crustEdgeDist.shrink_to_fit();
    hotspotElev.clear();  hotspotElev.shrink_to_fit();

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
