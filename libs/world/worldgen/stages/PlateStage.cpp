// PlateStage — M3a implementation.
//
// Algorithm overview:
//   1. K seeds placed at jittered spherical-Fibonacci points, snapped to tiles.
//   2. Per-plate growth rate drawn from Pcg32, range [0.6, 2.4].
//   3. Irregular Voronoi via multi-source Dijkstra on the tile graph.
//      Edge cost = (1.0 + 0.9 * (0.5 + 0.5*fractalNoise3(center*freq, seed, 3))) / growthRate.
//      freq = 3.5 chosen so boundary features are ~8-14% of planet circumference
//      (noise lattice cell ~1/3.5 of unit-sphere radius ≈ 0.29; projected arc ≈ 0.28 rad ≈ 16%
//       of circumference at equator; with 3 octaves the dominant scale is ~10%).
//   4. Continental assignment: target crust fraction = (1 - waterAmount) * 1.12.
//      The 1.12 factor gives continental shelves — crust slightly exceeds final land area.
//      Plates shuffled, flagged continental until cumulative area would overshoot target*1.35.
//   5. Craton pass: 1-3 craton seeds per continental plate, second Dijkstra constrained
//      within each plate, budgeted so global crusted fraction hits target ± 3%.
//      Oceanic margins of continental plates exist naturally where craton budget runs out.
//
// Determinism: single-threaded Dijkstra only; Pcg32/hash noise; fixed comparison order.

#include "worldgen/stages/PlateStage.h"

#include "worldgen/data/WorldData.h"

#include <math/DeterministicMath.h>
#include <random/HashNoise.h>
#include <random/Pcg32.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <queue>
#include <vector>

namespace worldgen {

namespace {

// Noise frequency for Dijkstra edge-cost perturbation.
// Unit-sphere coords are in [-1,1]^3; freq=3.5 puts the dominant noise scale at
// ~1/3.5 ≈ 0.29 of sphere radius, which projects to ~10-14% of circumference
// — wide enough to give organic boundary wander without fragmenting regions.
constexpr float kBoundaryNoiseFreq = 3.5f;

// Craton-pass noise frequency: finer scale inside plates for realistic craton edges.
constexpr float kCratonNoiseFreq = 6.0f;

// ---- Spherical Fibonacci rotation ----
// Produce K evenly-spaced points on the unit sphere using the spherical Fibonacci lattice,
// then apply a random rotation matrix from rng to break symmetry.

struct Vec3f { float x, y, z; };

// Build a random 3x3 rotation matrix from rng (columns are orthonormal).
// Uses two random unit vectors to build an orthonormal frame.
void randomRotation(foundation::Pcg32& rng, float R[9]) {
    // Random unit vector u via rejection sampling
    float ux{}, uy{}, uz{};
    for (;;) {
        ux = rng.nextFloat() * 2.0f - 1.0f;
        uy = rng.nextFloat() * 2.0f - 1.0f;
        uz = rng.nextFloat() * 2.0f - 1.0f;
        float r2 = ux*ux + uy*uy + uz*uz;
        if (r2 > 0.0001f && r2 <= 1.0f) {
            float inv = 1.0f / static_cast<float>(foundation::det_math::sqrt(static_cast<double>(r2)));
            ux *= inv; uy *= inv; uz *= inv;
            break;
        }
    }
    // Random unit vector v perpendicular to u
    float vx{}, vy{}, vz{};
    for (;;) {
        float ax = rng.nextFloat() * 2.0f - 1.0f;
        float ay = rng.nextFloat() * 2.0f - 1.0f;
        float az = rng.nextFloat() * 2.0f - 1.0f;
        float r2 = ax*ax + ay*ay + az*az;
        if (r2 < 0.0001f || r2 > 1.0f) continue;
        float inv = 1.0f / static_cast<float>(foundation::det_math::sqrt(static_cast<double>(r2)));
        ax *= inv; ay *= inv; az *= inv;
        // Gram-Schmidt: remove u component
        float dot = ax*ux + ay*uy + az*uz;
        vx = ax - dot*ux;
        vy = ay - dot*uy;
        vz = az - dot*uz;
        float vr2 = vx*vx + vy*vy + vz*vz;
        if (vr2 > 0.01f) {
            float vinv = 1.0f / static_cast<float>(foundation::det_math::sqrt(static_cast<double>(vr2)));
            vx *= vinv; vy *= vinv; vz *= vinv;
            break;
        }
    }
    // w = u cross v
    float wx = uy*vz - uz*vy;
    float wy = uz*vx - ux*vz;
    float wz = ux*vy - uy*vx;
    // Columns: u, v, w
    R[0]=ux; R[3]=vx; R[6]=wx;
    R[1]=uy; R[4]=vy; R[7]=wy;
    R[2]=uz; R[5]=vz; R[8]=wz;
}

Vec3f applyRotation(const float R[9], float x, float y, float z) {
    return { R[0]*x + R[3]*y + R[6]*z,
             R[1]*x + R[4]*y + R[7]*z,
             R[2]*x + R[5]*y + R[8]*z };
}

// Spherical Fibonacci point i of K, returned as unit vector.
// Uses the standard formula with golden angle.
Vec3f fibonacciPoint(int i, int K) {
    constexpr double kGoldenAngle = 2.3999632297286531; // 2*pi*(1 - 1/phi) = 2*pi*(2 - phi)
    double t = (static_cast<double>(i) + 0.5) / static_cast<double>(K);
    // z = 1 - 2t (uniform in [-1,1])
    double z = 1.0 - 2.0 * t;
    double sinTheta = foundation::det_math::sqrt(1.0 - z*z);
    double phi = kGoldenAngle * static_cast<double>(i);
    // Remove integer part of phi/(2pi) for argument reduction
    constexpr double kTwoPiRecip = 0.15915494309189534; // 1/(2*pi)
    double phiReduced = phi - static_cast<double>(static_cast<int64_t>(phi * kTwoPiRecip)) * 6.2831853071795865;
    double cx = sinTheta * foundation::det_math::cos(phiReduced);
    double cy = sinTheta * foundation::det_math::sin(phiReduced);
    return { static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(z) };
}

// ---- Dijkstra entry ----
// Pack plate id (8 bits) and tile id (24 bits) into a 32-bit key for tie-breaking.
// Both are stored separately in the priority queue entry.
struct HeapEntry {
    float    cost;
    uint32_t plate; // plate index
    uint32_t tile;  // tile id

    bool operator>(const HeapEntry& o) const {
        if (cost != o.cost) return cost > o.cost;
        if (plate != o.plate) return plate > o.plate;
        return tile > o.tile;
    }
};

// ---- fractalNoise3 convenience wrapper ----
// 3-octave fBm, lacunarity=2, gain=0.5 → normalized to [0,1] via (val+1)/2.
// gradientNoise3 output is ~[-1,1]; after normalization, edge cost varies smoothly.
inline float boundaryNoise(float x, float y, float z, uint32_t seed) {
    float n = foundation::fractalNoise3(x, y, z, seed, 3, 2.0f, 0.5f);
    // clamp to [-1,1] before mapping
    if (n < -1.0f) n = -1.0f;
    if (n >  1.0f) n =  1.0f;
    return 0.5f + 0.5f * n; // map to [0,1]
}

} // namespace

void PlateStage::run(StageContext& ctx) {
    const uint32_t N   = ctx.grid.tileCount();
    const int      K   = ctx.params.tectonicPlateCount;
    const double   waterAmount = ctx.params.waterAmount;
    assert(K >= 2 && K <= 30 && "tectonicPlateCount out of supported range (2..30)");
    assert(K <= 254 && "uint8 plateId can hold 0..254; 255 is unassigned sentinel");

    // Derive two sub-seeds from stageSeed so noise and rng are independent.
    // Using SplitMix-style mixing: multiply by a large odd constant, xor-fold.
    auto stageSeed32 = static_cast<uint32_t>(ctx.stageSeed ^ (ctx.stageSeed >> 32));
    uint32_t noiseSeed  = stageSeed32 ^ 0x9E3779B9u;
    uint32_t cratonSeed = stageSeed32 ^ 0x6C62272Eu;

    foundation::Pcg32 rng(ctx.stageSeed);

    // ---- Step 1: Spherical Fibonacci seeds with random rotation + jitter ----
    float R[9];
    randomRotation(rng, R);

    std::vector<TileId> seedTile(static_cast<size_t>(K), kInvalidTile);
    {
        // Track which tiles are already used as seeds to avoid collisions.
        std::vector<bool> usedAsSeed(N, false);

        for (int p = 0; p < K; ++p) {
            Vec3f base = fibonacciPoint(p, K);
            Vec3f rot  = applyRotation(R, base.x, base.y, base.z);

            // Normalize (rotation is orthonormal so length stays 1, but rounding may drift)
            double rx = rot.x, ry = rot.y, rz = rot.z;
            double rlen = foundation::det_math::sqrt(rx*rx + ry*ry + rz*rz);
            if (rlen > 1e-12) { rx /= rlen; ry /= rlen; rz /= rlen; }

            TileId t = ctx.grid.fromUnitVector({rx, ry, rz});

            // If collision, jitter deterministically until distinct.
            // Jitter: small perturbation along a hash-derived direction.
            uint32_t jitterIter = 0;
            while (t == kInvalidTile || usedAsSeed[t]) {
                ++jitterIter;
                // Hash gives a pseudo-random offset direction.
                uint32_t h = foundation::hash3(p, static_cast<int32_t>(jitterIter), 0, stageSeed32);
                // Convert h to a small displacement on the sphere.
                float jx = static_cast<float>(static_cast<int32_t>(h))        * (1.0f / 2147483648.0f) * 0.05f;
                float jy = static_cast<float>(static_cast<int32_t>(h >> 1))   * (1.0f / 2147483648.0f) * 0.05f;
                float jz = static_cast<float>(static_cast<int32_t>(h >> 2))   * (1.0f / 2147483648.0f) * 0.05f;
                double nx = rx + jx, ny = ry + jy, nz = rz + jz;
                double nlen = foundation::det_math::sqrt(nx*nx + ny*ny + nz*nz);
                if (nlen < 1e-12) continue;
                nx /= nlen; ny /= nlen; nz /= nlen;
                t = ctx.grid.fromUnitVector({nx, ny, nz});
            }
            seedTile[static_cast<size_t>(p)] = t;
            usedAsSeed[t] = true;
        }
    }

    // ---- Step 2: Per-plate growth rates ----
    std::vector<float> growthRate(static_cast<size_t>(K));
    for (int p = 0; p < K; ++p) {
        // Uniform in [0.6, 2.4]: gives Pacific-huge / Cocos-tiny size distribution.
        growthRate[static_cast<size_t>(p)] = 0.6f + rng.nextFloat() * 1.8f;
    }

    // ---- Step 3: Multi-source Dijkstra for Voronoi plate assignment ----
    // bestCost[t] = min cost so far; visited[t] = fully settled.
    std::vector<float>  bestCost(N, 1e30f);
    std::vector<bool>   visited(N, false);

    // plateId[] starts at 255 (unassigned)
    std::fill(ctx.data.plateId.begin(), ctx.data.plateId.end(), 255u);

    std::priority_queue<HeapEntry, std::vector<HeapEntry>, std::greater<HeapEntry>> pq;

    // Initialize with seed tiles at cost 0
    for (int p = 0; p < K; ++p) {
        TileId t = seedTile[static_cast<size_t>(p)];
        bestCost[t] = 0.0f;
        ctx.data.plateId[t] = static_cast<uint8_t>(p);
        pq.push({0.0f, static_cast<uint32_t>(p), t});
    }

    uint32_t poppedTiles = 0;
    std::array<TileId, 8> nbrs{};

    while (!pq.empty()) {
        HeapEntry cur = pq.top();
        pq.pop();

        if (visited[cur.tile]) continue;
        visited[cur.tile] = true;
        ctx.data.plateId[cur.tile] = static_cast<uint8_t>(cur.plate);

        ++poppedTiles;
        // Check for cancellation every 4096 pops (~0.5ms at n=256 in Debug).
        // The priority_queue pop is expensive enough that 4096 iterations is well under
        // any cancel latency budget (100ms window means we need checks every ~few ms).
        if ((poppedTiles & 0x0FFFu) == 0) {
            throwIfCancelled(ctx);
            ctx.reportProgress(static_cast<float>(poppedTiles) / static_cast<float>(N) * 0.65f);
        }

        uint32_t nCount = ctx.grid.neighbors(cur.tile, nbrs);
        for (uint32_t k = 0; k < nCount; ++k) {
            TileId nb = nbrs[k];
            if (visited[nb]) continue;

            Vec3d center = ctx.grid.tileCenter(nb);
            auto cx = static_cast<float>(center.x);
            auto cy = static_cast<float>(center.y);
            auto cz = static_cast<float>(center.z);

            // Edge step cost: noise-perturbed / growthRate
            float noise = boundaryNoise(cx * kBoundaryNoiseFreq,
                                        cy * kBoundaryNoiseFreq,
                                        cz * kBoundaryNoiseFreq,
                                        noiseSeed);
            float stepCost = (1.0f + 0.9f * noise) / growthRate[cur.plate];
            float newCost  = cur.cost + stepCost;

            if (newCost < bestCost[nb]) {
                bestCost[nb] = newCost;
                pq.push({newCost, cur.plate, nb});
            }
        }
    }

    ctx.reportProgress(0.65f);

    // ---- Step 4: Continental plate assignment ----
    // Count area (tile count) per plate.
    std::vector<uint32_t> plateArea(static_cast<size_t>(K), 0u);
    for (uint32_t t = 0; t < N; ++t) {
        uint8_t pid = ctx.data.plateId[t];
        if (pid < static_cast<uint8_t>(K)) {
            plateArea[static_cast<size_t>(pid)]++;
        }
    }

    // Shuffle plate indices via Pcg32 (Fisher-Yates on index array).
    std::vector<int> shuffled(static_cast<size_t>(K));
    for (int i = 0; i < K; ++i) shuffled[static_cast<size_t>(i)] = i;
    for (int i = K - 1; i > 0; --i) {
        uint32_t j = rng.nextRange(static_cast<uint32_t>(i + 1));
        std::swap(shuffled[static_cast<size_t>(i)], shuffled[j]);
    }

    // Target crust fraction: 1.12× (1-waterAmount).
    // The 1.12 accounts for continental shelves: crust slightly exceeds the final land area
    // because sea level cuts through the shelf, leaving submerged continental rock.
    const double crustTarget = (1.0 - waterAmount) * 1.12;

    // Flag plates continental until cumulative area would overshoot target * 1.35.
    // The overshoot budget (1.35×) exists because not all continental plate area becomes
    // crust — the craton pass trims it; we just need enough plates to source the crust.
    const double maxCrustArea = crustTarget * 1.35 * static_cast<double>(N);

    std::vector<bool> isContinentalPlate(static_cast<size_t>(K), false);
    double accArea = 0.0;
    for (int idx = 0; idx < K; ++idx) {
        int p = shuffled[static_cast<size_t>(idx)];
        if (accArea >= maxCrustArea) break;
        isContinentalPlate[static_cast<size_t>(p)] = true;
        accArea += static_cast<double>(plateArea[static_cast<size_t>(p)]);
    }

    ctx.reportProgress(0.68f);

    // ---- Step 5: Craton pass — second Dijkstra constrained within continental plates ----
    // Target: global crusted tile count = crustTarget * N (±3%)
    const uint32_t crustBudgetGlobal = static_cast<uint32_t>(crustTarget * static_cast<double>(N));

    // Per-plate craton budget: proportional to plate area, scaled so sum = crustBudgetGlobal.
    uint32_t totalContinentalArea = 0;
    for (int p = 0; p < K; ++p) {
        if (isContinentalPlate[static_cast<size_t>(p)]) {
            totalContinentalArea += plateArea[static_cast<size_t>(p)];
        }
    }

    std::vector<uint32_t> cratonBudget(static_cast<size_t>(K), 0u);
    if (totalContinentalArea > 0) {
        for (int p = 0; p < K; ++p) {
            if (isContinentalPlate[static_cast<size_t>(p)]) {
                // Proportional share of global budget
                double share = static_cast<double>(plateArea[static_cast<size_t>(p)])
                             / static_cast<double>(totalContinentalArea);
                // Small plates get fully crusted (microcontinents): use at least their full area
                // but cap at budget.
                uint32_t budget = static_cast<uint32_t>(share * static_cast<double>(crustBudgetGlobal));
                // Guarantee small plates are fully crusted: if budget < plate area, use plate area
                // only if that plate's full area is <= 3% of N (microcontinent threshold).
                if (budget < plateArea[static_cast<size_t>(p)] &&
                    plateArea[static_cast<size_t>(p)] <= N / 33u) {
                    budget = plateArea[static_cast<size_t>(p)];
                }
                cratonBudget[static_cast<size_t>(p)] = budget;
            }
        }
    }

    // Craton Dijkstra: seeds on continental plates (1-3 per plate, hash-picked).
    foundation::Pcg32 cratonRng(cratonSeed);

    std::vector<float>  cratonCost(N, 1e30f);
    std::vector<bool>   cratonVisited(N, false);
    std::vector<uint32_t> cratonAccepted(static_cast<size_t>(K), 0u);

    // Re-use HeapEntry; 'plate' field carries plate id for budget tracking.
    std::priority_queue<HeapEntry, std::vector<HeapEntry>, std::greater<HeapEntry>> cq;

    for (int p = 0; p < K; ++p) {
        if (!isContinentalPlate[static_cast<size_t>(p)]) continue;

        // 1-3 craton centers, hash-determined
        uint32_t nCratons = 1 + cratonRng.nextRange(3); // [1, 3]

        // Collect tiles belonging to this plate, then pick random ones as craton seeds.
        // To avoid building a per-plate tile list (O(N) each = O(K*N) total), use a
        // reservoir-sampling-style approach: scan once, pick nCratons via Pcg32.
        // We only scan tiles of this plate using the plateId array we just wrote.
        // Accepted approach: simple random picks with retry on wrong plate.
        // At small K, worst case is fine. At K=30 with ~N/30 tiles per plate, hit rate ~1/30.
        //
        // For robustness, use the seed tile as a guaranteed craton center, plus up to 2 more.
        std::array<TileId, 3> cratonSeeds{};
        uint32_t seedCount = 0;

        // First craton seed = plate seed tile (guaranteed in-plate)
        cratonSeeds[seedCount++] = seedTile[static_cast<size_t>(p)];

        // Additional seeds: random tiles of this plate
        for (uint32_t sc = 1; sc < nCratons; ++sc) {
            uint32_t attempts = 0;
            while (attempts < 64) {
                TileId candidate = cratonRng.nextRange(N);
                if (ctx.data.plateId[candidate] == static_cast<uint8_t>(p)) {
                    // Check not already a seed
                    bool dup = false;
                    for (uint32_t s2 = 0; s2 < seedCount; ++s2) {
                        if (cratonSeeds[s2] == candidate) { dup = true; break; }
                    }
                    if (!dup) { cratonSeeds[seedCount++] = candidate; break; }
                }
                ++attempts;
            }
            // If we fail to find a second/third distinct tile, just use fewer seeds
        }

        for (uint32_t sc = 0; sc < seedCount; ++sc) {
            TileId t = cratonSeeds[sc];
            if (cratonCost[t] > 0.0f) {
                cratonCost[t] = 0.0f;
                cq.push({0.0f, static_cast<uint32_t>(p), t});
            }
        }
    }

    uint32_t cratonPopped = 0;
    std::array<TileId, 8> cnbrs{};

    while (!cq.empty()) {
        HeapEntry cur = cq.top();
        cq.pop();

        if (cratonVisited[cur.tile]) continue;
        uint8_t tilePlate = ctx.data.plateId[cur.tile];
        // Only expand within the same plate as the craton source
        if (tilePlate != static_cast<uint8_t>(cur.plate)) continue;

        // Check budget for this plate
        uint32_t pid = cur.plate;
        if (cratonAccepted[pid] >= cratonBudget[pid]) continue;

        cratonVisited[cur.tile] = true;
        ctx.data.flags[cur.tile] |= kFlagContinentalCrust;
        cratonAccepted[pid]++;

        ++cratonPopped;
        if ((cratonPopped & 0x0FFFu) == 0) {
            throwIfCancelled(ctx);
            ctx.reportProgress(0.68f + static_cast<float>(cratonPopped) / static_cast<float>(N) * 0.22f);
        }

        uint32_t nCount = ctx.grid.neighbors(cur.tile, cnbrs);
        for (uint32_t k = 0; k < nCount; ++k) {
            TileId nb = cnbrs[k];
            if (cratonVisited[nb]) continue;
            if (ctx.data.plateId[nb] != static_cast<uint8_t>(cur.plate)) continue;
            if (cratonAccepted[pid] >= cratonBudget[pid]) break;

            Vec3d center = ctx.grid.tileCenter(nb);
            auto cx = static_cast<float>(center.x);
            auto cy = static_cast<float>(center.y);
            auto cz = static_cast<float>(center.z);

            float noise = boundaryNoise(cx * kCratonNoiseFreq,
                                        cy * kCratonNoiseFreq,
                                        cz * kCratonNoiseFreq,
                                        cratonSeed);
            float stepCost = 1.0f + 0.6f * noise;
            float newCost  = cur.cost + stepCost;

            if (newCost < cratonCost[nb]) {
                cratonCost[nb] = newCost;
                cq.push({newCost, cur.plate, nb});
            }
        }
    }

    ctx.reportProgress(0.90f);

    // ---- Step 6: Populate world.plates ----
    ctx.world.plates.resize(static_cast<size_t>(K));
    for (int p = 0; p < K; ++p) {
        ctx.world.plates[static_cast<size_t>(p)].isContinental = isContinentalPlate[static_cast<size_t>(p)];
        // eulerPole and angularSpeed are filled by PlateMovementStage.
        ctx.world.plates[static_cast<size_t>(p)].eulerPole    = {0.0, 0.0, 1.0};
        ctx.world.plates[static_cast<size_t>(p)].angularSpeed = 0.0f;
    }

    ctx.world.validFields |= static_cast<uint32_t>(WorldField::PlateId);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::Flags);

    ctx.reportProgress(1.0f);
}

} // namespace worldgen
