// PrecipitationStage — M3d implementation.
//
// Pass 1 — precipitation (parallel, per tile):
//   precip = latitudeBand(lat) * noise * waterFactor * evaporation(T) * orographic
//   - Latitude bands: simplified Hadley pattern — ITCZ ~2000 mm/yr at the
//     equator, subtropical minimum ~200 at 30°, recovering through midlatitudes,
//     drying again toward the poles.
//   - Noise: seeded valueNoise3 over the tile center, factor 0.6..1.4.
//   - Evaporation: clamp(0.8 + 0.4*(T - Tmin)/(Tmax - Tmin), 0.8, 1.2) with
//     Tmin=-50 °C, Tmax=+60 °C, T from temperatureMean (0.1 °C units).
//     Cold air carries less moisture.
//   - Orographic (land only): march up to 4 neighbor hops UPWIND. windDir is the
//     direction the wind blows toward (0=N, 64=E), so upwind = windDir + 128.
//     Each hop picks the neighbor whose offset best aligns with the upwind
//     heading (normalized dot product; ties broken by lower TileId), with the
//     heading re-projected onto the local tangent plane each step so the march
//     follows a great circle.
//       windward: ocean found upwind and this tile rises above the upwind low
//         point -> boost 1 + 0.6*min(rise/1500 m, 1)  (up to 1.6x)
//       rain shadow: a higher ridge upwind -> cut 1 - 0.75*min((ridge-e)/1500, 1)
//         (down to 0.25x)
//       continental interior: no ocean within the march -> 0.8x
//   - Ocean tiles (elevation < seaLevelMeters — kFlagOcean is not set until
//     OceanStage runs): evaporation-scaled base * 1.15, no march.
//   - Final clamp to [0, 8000] mm/yr before the uint16 store.
//
// Pass 2 — rivers (this stage introduces FlowAccum and Downhill):
//   - downhill[t]: index into grid.neighbors(t, ..) order of the strictly
//     lowest neighbor; 0xFF for ocean tiles and local minima (sink/lake
//     candidates). Ties broken by lowest neighbor TileId. Slab-parallel.
//   - flowAccum[t]: land tiles seeded with precipitation/1000 (wet regions
//     drain more), then one serial pass over land tiles sorted by elevation
//     descending (ties by ascending TileId) adds each tile's accumulation into
//     its downhill target. Flow into an ocean tile exits the network (ocean
//     stays 0). The sort gives a total order, so float addition order — and
//     therefore the result — is identical at any thread count.
//
// Determinism: det_math for transcendentals (std::sqrt is correctly rounded per
// IEEE 754 and wrapped by det_math::sqrt); all randomness from valueNoise3
// seeded by stageSeed; parallel passes are pure per-tile functions over fixed
// grain slabs.
//
// Transient memory: tile-center cache (24 B/tile) + downhill target ids
// (4 B/tile) ≈ 295 MB at n=1024, released before return. The center cache
// exists because the upwind march reads up to 32 neighbor centers per land
// tile; recomputing them would dominate the stage.

#include "worldgen/stages/PrecipitationStage.h"

#include <math/DeterministicMath.h>
#include <random/HashNoise.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace worldgen {

namespace {

constexpr size_t kGrainSize = 4096;
constexpr double kPi        = 3.14159265358979323846;
constexpr double kPiOver180 = kPi / 180.0;
constexpr double kRadPerWindUnit = 2.0 * kPi / 256.0;

constexpr int    kUpwindSteps         = 4;
constexpr double kOrographicRefMeters = 1500.0; // full windward/shadow effect at this relief
constexpr double kWindwardMaxBoost    = 0.6;    // multiplier reaches 1.6x
constexpr double kShadowMaxCut        = 0.75;   // multiplier floors at 0.25x
constexpr double kInteriorDryFactor   = 0.8;    // no ocean within the march
constexpr double kOceanPrecipFactor   = 1.15;   // oceans are moisture sources
constexpr double kEvapTminC           = -50.0;
constexpr double kEvapTmaxC           = 60.0;
constexpr double kMaxPrecipMmYr       = 8000.0;

// Latitude-band base precipitation (mm/yr): simplified Hadley cell pattern.
// Kept in sync with PrecipitationStage.test.cpp (expectedNoOrographic).
double latitudeBase(double absLat) {
    if (absLat < 15.0) return 2000.0 - absLat * 20.0;          // ITCZ
    if (absLat < 30.0) return 2000.0 - absLat * 60.0 + 200.0;  // subtropical dry
    if (absLat < 60.0) return 200.0 + (absLat - 30.0) * 18.3;  // midlatitude
    return 750.0 - (absLat - 60.0) * 7.5;                      // polar
}

inline double dot3(const Vec3d& a, const Vec3d& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

} // namespace

void PrecipitationStage::run(StageContext& ctx) {
    const uint32_t totalTiles = ctx.grid.tileCount();
    const auto seed32 = static_cast<uint32_t>(ctx.stageSeed ^ (ctx.stageSeed >> 32));
    const float  seaLevel  = ctx.world.seaLevelMeters;
    const double dSeaLevel = static_cast<double>(seaLevel);
    const float  invTotal  = 1.0f / static_cast<float>(totalTiles);

    std::vector<Vec3d> centers(totalTiles);
    ctx.pool.parallelFor(0, totalTiles, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        for (size_t t = begin; t < end; ++t) {
            centers[t] = ctx.grid.tileCenter(static_cast<TileId>(t));
        }
        ctx.reportProgress(0.10f * static_cast<float>(end) * invTotal);
    });

    // Orographic multiplier for a land tile: march upwind, then combine
    // windward boost, rain shadow, and continental-interior drying.
    auto orographicMul = [&](TileId t, const Vec3d& c, double e0) -> double {
        const uint32_t upwindUnits =
            (static_cast<uint32_t>(ctx.data.windDir[t]) + 128u) & 0xFFu;
        const double theta = static_cast<double>(upwindUnits) * kRadPerWindUnit;

        // Tangent basis at c: east toward +longitude (ẑ × c), north = c × east.
        const double eastX = -c.y;
        const double eastY = c.x;
        const double eastLen2 = eastX * eastX + eastY * eastY;
        if (eastLen2 < 1e-12) return 1.0; // tile centered on a pole: no heading
        const double invEast = 1.0 / foundation::det_math::sqrt(eastLen2);
        const Vec3d east{eastX * invEast, eastY * invEast, 0.0};
        const Vec3d north{-c.z * east.y, c.z * east.x, c.x * east.y - c.y * east.x};

        const double sinT = foundation::det_math::sin(theta);
        const double cosT = foundation::det_math::cos(theta);
        Vec3d up{north.x * cosT + east.x * sinT,
                 north.y * cosT + east.y * sinT,
                 north.z * cosT + east.z * sinT};

        TileId cur  = t;
        Vec3d  curC = c;
        double minUp = e0;
        double maxUp = e0;
        bool   oceanFound = false;

        for (int step = 0; step < kUpwindSteps; ++step) {
            // Re-project the heading onto the current tangent plane
            // (approximate parallel transport along the marched path).
            const double radial = dot3(up, curC);
            Vec3d proj{up.x - radial * curC.x,
                       up.y - radial * curC.y,
                       up.z - radial * curC.z};
            const double projLen2 = dot3(proj, proj);
            if (projLen2 < 1e-12) break;
            const double invProj = 1.0 / foundation::det_math::sqrt(projLen2);
            up = Vec3d{proj.x * invProj, proj.y * invProj, proj.z * invProj};

            std::array<TileId, 8> nbs{};
            const uint32_t cnt = ctx.grid.neighbors(cur, nbs);
            double bestScore = 0.0; // require positive upwind alignment
            TileId best = kInvalidTile;
            for (uint32_t k = 0; k < cnt; ++k) {
                const Vec3d& nc = centers[nbs[k]];
                const Vec3d d{nc.x - curC.x, nc.y - curC.y, nc.z - curC.z};
                const double dl2 = dot3(d, d);
                if (dl2 <= 0.0) continue;
                const double score = dot3(d, up) / foundation::det_math::sqrt(dl2);
                if (score > bestScore ||
                    (score == bestScore && best != kInvalidTile && nbs[k] < best)) {
                    bestScore = score;
                    best = nbs[k];
                }
            }
            if (best == kInvalidTile) break;

            cur  = best;
            curC = centers[cur];
            const double e = static_cast<double>(ctx.data.elevation[cur]);
            if (e < minUp) minUp = e;
            if (e > maxUp) maxUp = e;
            if (e < dSeaLevel) {
                oceanFound = true; // moisture source reached; ridges beyond it don't matter
                break;
            }
        }

        double mul = 1.0;
        if (oceanFound) {
            // Rise above the upwind low point, measured from the water surface.
            const double low  = minUp > dSeaLevel ? minUp : dSeaLevel;
            const double rise = e0 - low;
            if (rise > 0.0) {
                double s = rise / kOrographicRefMeters;
                if (s > 1.0) s = 1.0;
                mul *= 1.0 + kWindwardMaxBoost * s;
            }
        } else {
            mul *= kInteriorDryFactor;
        }
        const double ridge = maxUp - e0;
        if (ridge > 0.0) {
            double s = ridge / kOrographicRefMeters;
            if (s > 1.0) s = 1.0;
            mul *= 1.0 - kShadowMaxCut * s;
        }
        return mul;
    };

    // -------- Pass 1: precipitation --------
    ctx.pool.parallelFor(0, totalTiles, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        for (size_t t = begin; t < end; ++t) {
            const TileId tile = static_cast<TileId>(t);
            const Vec3d c = centers[t];
            const double lat = foundation::det_math::asin(c.z) / kPiOver180;
            const double absLat = lat < 0.0 ? -lat : lat;

            double precip = latitudeBase(absLat);

            const float noise = foundation::valueNoise3(
                static_cast<float>(c.x) * 3.0f,
                static_cast<float>(c.y) * 3.0f,
                static_cast<float>(c.z) * 3.0f,
                seed32);
            precip *= 0.6 + 0.8 * static_cast<double>(noise);

            // More ocean = more precipitation globally.
            precip *= 0.5 + ctx.params.waterAmount;

            // Temperature-dependent evaporation / moisture capacity.
            const double tempC = static_cast<double>(ctx.data.temperatureMean[t]) * 0.1;
            double evap = 0.8 + 0.4 * (tempC - kEvapTminC) / (kEvapTmaxC - kEvapTminC);
            if (evap < 0.8) evap = 0.8;
            if (evap > 1.2) evap = 1.2;
            precip *= evap;

            const double e0 = static_cast<double>(ctx.data.elevation[t]);
            if (e0 < dSeaLevel) {
                precip *= kOceanPrecipFactor;
            } else {
                precip *= orographicMul(tile, c, e0);
            }

            if (precip < 0.0) precip = 0.0;
            if (precip > kMaxPrecipMmYr) precip = kMaxPrecipMmYr;
            ctx.data.precipitation[t] = static_cast<uint16_t>(precip);
        }
        ctx.reportProgress(0.10f + 0.50f * static_cast<float>(end) * invTotal);
    });

    // -------- Pass 2a: downhill pointers (slab-parallel) --------
    std::vector<TileId> downTarget(totalTiles, kInvalidTile);
    ctx.pool.parallelFor(0, totalTiles, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        for (size_t t = begin; t < end; ++t) {
            if (ctx.data.elevation[t] < seaLevel) {
                ctx.data.downhill[t]  = 0xFF;
                ctx.data.flowAccum[t] = 0.0f;
                continue;
            }
            std::array<TileId, 8> nbs{};
            const uint32_t cnt = ctx.grid.neighbors(static_cast<TileId>(t), nbs);
            float    bestE   = ctx.data.elevation[t]; // strictly lower required
            uint32_t bestIdx = 0xFFu;
            TileId   bestId  = kInvalidTile;
            for (uint32_t k = 0; k < cnt; ++k) {
                const float e = ctx.data.elevation[nbs[k]];
                if (e < bestE ||
                    (bestIdx != 0xFFu && e == bestE && nbs[k] < bestId)) {
                    bestE   = e;
                    bestIdx = k;
                    bestId  = nbs[k];
                }
            }
            ctx.data.downhill[t]  = static_cast<uint8_t>(bestIdx);
            downTarget[t]         = bestIdx == 0xFFu ? kInvalidTile : bestId;
            ctx.data.flowAccum[t] =
                static_cast<float>(ctx.data.precipitation[t]) * (1.0f / 1000.0f);
        }
        ctx.reportProgress(0.60f + 0.20f * static_cast<float>(end) * invTotal);
    });

    // -------- Pass 2b: flow accumulation (serial, deterministic order) --------
    std::vector<TileId> order;
    order.reserve(totalTiles);
    for (TileId t = 0; t < totalTiles; ++t) {
        if (ctx.data.elevation[t] >= seaLevel) order.push_back(t);
    }
    std::sort(order.begin(), order.end(), [&](TileId a, TileId b) {
        const float ea = ctx.data.elevation[a];
        const float eb = ctx.data.elevation[b];
        if (ea != eb) return ea > eb;
        return a < b;
    });
    ctx.reportProgress(0.90f);

    for (size_t i = 0; i < order.size(); ++i) {
        if ((i & 0xFFFFFu) == 0u) throwIfCancelled(ctx);
        const TileId t   = order[i];
        const TileId tgt = downTarget[t];
        if (tgt != kInvalidTile && ctx.data.elevation[tgt] >= seaLevel) {
            ctx.data.flowAccum[tgt] += ctx.data.flowAccum[t];
        }
    }

    ctx.world.validFields |= static_cast<uint32_t>(WorldField::Precipitation);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::FlowAccum);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::Downhill);
    ctx.reportProgress(1.0f);
}

} // namespace worldgen
