// GlacierStage (pipeline phase P10): permanent LAND ice — glaciers and ice
// sheets — from real glaciology, not a heuristic. Two physical pieces:
//
//   1. Surface mass balance (positive-degree-day model). Annual accumulation
//      (snowfall) minus ablation (melt proportional to positive degree-days)
//      decides where perennial ice can exist: the accumulation zone is land
//      where SMB > 0. PDD is computed from the annual mean temperature and the
//      seasonal amplitude we already have (temperatureMean, temperatureRange)
//      via the Calov-Greve seasonal integral. Degree-day factor and sigma are
//      standard values (antarcticglaciers.org; Calov & Greve 2005).
//
//   2. Perfect-plastic ice-sheet profile (Nye 1951). Ice deforms until its
//      gravitational driving stress equals a yield stress tau0; for a flat bed
//      this gives H = sqrt(2*tau0*d/(rho*g)), where d is the distance to the ice
//      margin. tau0 ~ 100 kPa reproduces Greenland's ~3.2 km dome to ~50 m
//      (Cuffey & Paterson; Vialov 1958). d is a deterministic geodesic distance
//      transform inward from the margin.
//
// This is the analytical steady-state equilibrium ice sheet, the right tool for a
// one-shot snapshot generator. NOT modeled (honest simplifications): time-dependent
// flow (the full Shallow-Ice-Approximation PDE), marine/floating shelves, isostatic
// bed depression, basal sliding, and the ablation-zone overhang past the margin
// (the margin is taken at the equilibrium line, the SMB=0 boundary). The coupling is
// one-way within a pass: SMB uses the temperature AtmosphereStage produced (lapse on
// bedrock), so ice does not yet cool its own surface — the ice->temperature feedbacks
// (ice-surface lapse + albedo) are the separate two-pass regeneration.
//
// Sea ice (ocean tiles) is owned by SnowStage and left untouched here. GlacierStage
// is the LAST writer of flags (adds kFlagGlacier) and of iceThickness (land ice,
// after SnowStage's sea ice), so it owns the Flags, IceThickness, and IceFlow valid
// bits. The parallel passes are per-tile or read a completed snapshot of a prior pass,
// and the distance transform is single-threaded, so the stage is deterministic.

#include "worldgen/stages/GlacierStage.h"

#include <math/DeterministicMath.h>

#include <array>
#include <limits>
#include <queue>
#include <vector>

namespace worldgen {

namespace {
constexpr size_t kGrainSize = 4096;

// --- Surface mass balance (positive-degree-day model) ---
constexpr float  kSigmaC      = 5.0f;   // sub-monthly temperature std dev, C (Calov-Greve)
constexpr float  kDDFsnow     = 3.0f;   // degree-day factor, mm w.e./(C*day) (snow)
constexpr float  kSnowThreshC = 1.0f;   // rain/snow partition temperature, C
constexpr float  kRhoWOverI   = 1.091f; // water/ice density ratio (1000/917)
constexpr int    kMonths      = 12;
constexpr double kDaysPerYear = 365.25;

// --- Perfect-plastic (Nye) ice-sheet profile ---
constexpr double kYieldStressPa    = 1.0e5;  // tau0 = 100 kPa, validated to Greenland
constexpr double kRhoIce           = 917.0;  // kg/m^3
constexpr float  kMaxIceM          = 4000.0f;
constexpr float  kGlacierMinThickM = 10.0f;

constexpr double kPi       = 3.14159265358979323846;
constexpr double kSqrtTwoPi = 2.50662827463100050242; // sqrt(2*pi)

// Standard-normal CDF via Zelen & Severo (Abramowitz & Stegun 26.2.17), built on
// the deterministic exp so the whole stage is cross-platform bit-identical. |err| < 7.5e-8.
double normalCdf(double x) {
    constexpr double b0 = 0.2316419, b1 = 0.319381530, b2 = -0.356563782,
                     b3 = 1.781477937, b4 = -1.821255978, b5 = 1.330274429;
    const double ax = x < 0.0 ? -x : x;
    const double t   = 1.0 / (1.0 + b0 * ax);
    const double pdf = foundation::det_math::exp(-0.5 * ax * ax) / kSqrtTwoPi;
    const double poly = t * (b1 + t * (b2 + t * (b3 + t * (b4 + t * b5))));
    const double phiPos = 1.0 - pdf * poly; // Phi(ax), ax >= 0
    return x >= 0.0 ? phiPos : 1.0 - phiPos;
}

// Expected positive part of Normal(mu, sigma): E[max(0,T)] = sigma*phi(z) + mu*Phi(z).
double expectedPositive(double mu, double sigma) {
    if (sigma <= 0.0) return mu > 0.0 ? mu : 0.0;
    const double z = mu / sigma;
    const double pdf = foundation::det_math::exp(-0.5 * z * z) / kSqrtTwoPi;
    return sigma * pdf + mu * normalCdf(z);
}

// acos via the deterministic asin (det_math has no acos): acos(c) = pi/2 - asin(c).
double detAcos(double c) {
    if (c > 1.0) c = 1.0; else if (c < -1.0) c = -1.0;
    return kPi * 0.5 - foundation::det_math::asin(c);
}

// Chord distance between two unit-sphere directions, scaled to meters. For adjacent
// tiles the chord ~ arc to O(angle^2/24); accurate enough for a flow-distance weight
// and uses only the (bit-exact) sqrt.
double neighborDistanceM(const Vec3d& a, const Vec3d& b, double radiusM) {
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return foundation::det_math::sqrt(dx * dx + dy * dy + dz * dz) * radiusM;
}
} // namespace

void GlacierStage::run(StageContext& ctx) {
    const uint32_t totalTiles = ctx.grid.tileCount();
    const double radiusM = ctx.derived.planetRadiusMeters;
    const double g = ctx.derived.gravity;
    const float  kInf = std::numeric_limits<float>::infinity();

    // ---- Phase 1: surface mass balance (PDD) -> accumulation-zone ice mask ----
    std::vector<float> smb(totalTiles, 0.0f);
    ctx.pool.parallelFor(0, totalTiles, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        for (size_t t = begin; t < end; ++t) {
            ctx.data.iceFlow[t] = 0xFFu; // reset; recomputed for glacier tiles below

            // Ocean carries SnowStage's sea ice — leave it untouched.
            if ((ctx.data.flags[t] & kFlagOcean) != 0) continue;

            // Recompute land ice from scratch so the stage is idempotent.
            ctx.data.flags[t] &= static_cast<uint8_t>(~kFlagGlacier);
            ctx.data.iceThickness[t] = 0;

            const double Tma = static_cast<double>(ctx.data.temperatureMean[t]) * 0.1;
            const double A   = static_cast<double>(ctx.data.temperatureRange[t]) * 0.1;
            const double P   = static_cast<double>(ctx.data.precipitation[t]); // mm/yr

            // Annual positive-degree-days from the seasonal cycle (Calov-Greve).
            double pddSum = 0.0;
            for (int m = 0; m < kMonths; ++m) {
                const double mu =
                    Tma + A * foundation::det_math::cos(2.0 * kPi * m / kMonths);
                pddSum += expectedPositive(mu, kSigmaC);
            }
            const double pdd = (kDaysPerYear / kMonths) * pddSum; // C*day

            // Snow fraction of precip = fraction of the cycle below the snow threshold.
            double snowFrac;
            if (A < 1e-3) {
                snowFrac = (Tma < kSnowThreshC) ? 1.0 : 0.0;
            } else {
                const double c = (kSnowThreshC - Tma) / A; // clamped inside detAcos
                snowFrac = 1.0 - detAcos(c) / kPi;
            }

            const double accWe  = P * snowFrac;   // mm w.e./yr
            const double meltWe = kDDFsnow * pdd; // mm w.e./yr
            smb[t] = static_cast<float>((accWe - meltWe) / 1000.0 * kRhoWOverI); // m ice/yr
        }
        ctx.reportProgress(0.5f * static_cast<float>(end) / static_cast<float>(totalTiles));
    });

    // Ice mask: land tiles with positive surface mass balance.
    auto isIce = [&](TileId t) {
        return (ctx.data.flags[t] & kFlagOcean) == 0 && smb[t] > 0.0f;
    };

    // ---- Phase 2a: geodesic distance to the ice margin (deterministic Dijkstra) ----
    // Multi-source from the margin (ice/non-ice boundary): an ice tile touching a
    // non-ice tile seeds at half the edge (the margin sits ~midway between centers);
    // interior tiles accumulate full edge lengths inward. Min-heap ordered (dist,
    // TileId) for a deterministic relaxation order (same discipline as DrainageRouting).
    std::vector<float> dist(totalTiles, kInf);
    struct HeapItem { float d; TileId t; };
    auto cmp = [](const HeapItem& a, const HeapItem& b) {
        if (a.d != b.d) return a.d > b.d;
        return a.t > b.t;
    };
    std::priority_queue<HeapItem, std::vector<HeapItem>, decltype(cmp)> heap(cmp);

    std::array<TileId, 6> nbs{};
    for (TileId t = 0; t < totalTiles; ++t) {
        if (!isIce(t)) continue;
        const Vec3d ct = ctx.grid.tileCenter(t);
        const uint32_t cnt = ctx.grid.neighbors(t, nbs);
        float seed = kInf;
        for (uint32_t k = 0; k < cnt; ++k) {
            if (isIce(nbs[k])) continue; // margin neighbor (ocean or below-ELA land)
            const float half =
                static_cast<float>(0.5 * neighborDistanceM(ct, ctx.grid.tileCenter(nbs[k]), radiusM));
            if (half < seed) seed = half;
        }
        if (seed < dist[t]) { dist[t] = seed; heap.push({seed, t}); }
    }

    while (!heap.empty()) {
        const HeapItem it = heap.top();
        heap.pop();
        if (it.d > dist[it.t]) continue; // stale entry
        const Vec3d ct = ctx.grid.tileCenter(it.t);
        const uint32_t cnt = ctx.grid.neighbors(it.t, nbs);
        for (uint32_t k = 0; k < cnt; ++k) {
            const TileId nb = nbs[k];
            if (!isIce(nb)) continue;
            const float nd = it.d +
                static_cast<float>(neighborDistanceM(ct, ctx.grid.tileCenter(nb), radiusM));
            if (nd < dist[nb]) { dist[nb] = nd; heap.push({nd, nb}); }
        }
    }

    // ---- Phase 2b: perfect-plastic thickness from distance to margin (parallel) ----
    ctx.pool.parallelFor(0, totalTiles, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        for (size_t t = begin; t < end; ++t) {
            if (!isIce(static_cast<TileId>(t))) continue;
            const float d = dist[t];
            double H;
            if (d < kInf) {
                H = foundation::det_math::sqrt(
                    2.0 * kYieldStressPa * static_cast<double>(d) / (kRhoIce * g));
                if (H > kMaxIceM) H = kMaxIceM;
            } else {
                // No reachable margin: the ice region has no edge at all (a fully
                // glaciated world with no ocean and no below-ELA land). It is the
                // unbounded interior of an ice sheet, so cap it at the maximum
                // thickness rather than leaving it bare (the perfect-plastic profile
                // only thins toward a margin, and here there is none).
                H = kMaxIceM;
            }
            if (H >= kGlacierMinThickM) {
                ctx.data.iceThickness[t] = static_cast<uint16_t>(H);
                ctx.data.flags[t] |= kFlagGlacier;
            }
        }
        ctx.reportProgress(0.5f + 0.4f * static_cast<float>(end) /
                                  static_cast<float>(totalTiles));
    });

    // ---- Phase 2c: ice-surface flow direction (steepest descent of bed + ice) ----
    // Reads the completed thickness from 2b and writes only iceFlow, so no tile reads
    // a value another thread is writing — deterministic at any thread count.
    ctx.pool.parallelFor(0, totalTiles, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        std::array<TileId, 6> local{};
        for (size_t t = begin; t < end; ++t) {
            if ((ctx.data.flags[t] & kFlagGlacier) == 0) continue;
            const float surf = ctx.data.elevation[t] +
                               static_cast<float>(ctx.data.iceThickness[t]);
            const uint32_t cnt = ctx.grid.neighbors(static_cast<TileId>(t), local);
            float   bestSurf = surf;
            uint8_t bestDir  = 0xFFu;
            for (uint32_t k = 0; k < cnt; ++k) {
                const float ns = ctx.data.elevation[local[k]] +
                                 static_cast<float>(ctx.data.iceThickness[local[k]]);
                if (ns < bestSurf) { bestSurf = ns; bestDir = static_cast<uint8_t>(k); }
            }
            ctx.data.iceFlow[t] = bestDir;
        }
    });

    ctx.world.validFields |= static_cast<uint32_t>(WorldField::Flags);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::IceThickness);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::IceFlow);
}

} // namespace worldgen
