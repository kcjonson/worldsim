// PrecipitationStage — moisture-advection sweep + rivers.
//
// Pass 1a — base moisture Pbase (parallel, per tile):
//   Pbase = latitudeBand(lat) * noise * waterFactor * evaporation(T). This is the
//   precipitation a tile WOULD receive if a fully-charged parcel passed over it;
//   advection (pass 1c) scales it down as the parcel dries.
//   - Latitude bands: simplified Hadley pattern — ITCZ ~2000 mm/yr at the
//     equator, subtropical minimum ~200 at the hadleyEdge, recovering through
//     midlatitudes, drying again toward the poles. Edges from AtmosphereStage's
//     hadleyEdge/ferrelEdge formula so precip bands stay coupled to wind cells.
//   - Noise: seeded valueNoise3 over the tile center, factor 0.6..1.4.
//   - Evaporation: clamp(0.8 + 0.4*(T - Tmin)/(Tmax - Tmin), 0.8, 1.2) with
//     Tmin=-50 °C, Tmax=+60 °C, T from temperatureMean (0.1 °C units).
//
// Pass 1b — deterministic downwind sweep order:
//   Per tile, upwind[t] is the neighbor most opposite the wind heading (windDir +
//   the C-1 meridional tilt, as a downwind tangent vector). These links form a
//   functional graph (one parent each), so "depth along the upwind chain back to a
//   source" (ocean, an invalid link, or a broken cycle) is a valid topological key:
//   a tile always has strictly greater depth than its parent. Sorting by (depth,
//   TileId) processes every tile after the tile upwind of it, so the serial sweep
//   below is bit-identical at any thread count (same total-order trick as flowAccum
//   pass 2b). A position-projection key cannot do this: the wind is tangent to the
//   sphere, so dot(center, windVec) is ~0 everywhere.
//
// Pass 1c — moisture-budget sweep (serial, in sweep order):
//   A moisture parcel M (normalized; 1.0 = a saturated parcel that delivers full
//   Pbase) is PULLED downwind. For each tile, the upwind neighbor u is the neighbor
//   most opposite the wind heading; M = carriedOut[u] (already final, since u sorts
//   earlier by chain depth) or 0 if u is unprocessed.
//     - Ocean tile: recharge M to Mcap (warm/equatorial oceans charge wetter,
//       derived from latitudeBase*evap); precip = Pbase * kOceanPrecipFactor.
//     - Land tile, in order:
//         surface re-evaporation: lift M toward a LATITUDE-DEPENDENT plateau cap
//           (wet bands hold more interior moisture); then continentality M -=
//           loss/km — together these give the smooth wet-coast -> dry-interior
//           gradient and keep flat interiors at steppe rather than absolute desert.
//         rainHere = Pbase * f(M) * windwardBoost, f = clamp(M, kMoistureFloor, 1),
//           boost = 1 + kWindwardBoost*min((elev-windwardBase)/kOroRefM, 1).
//         orographic depletion: M -= kOroLossPerM * (new peak height gained), so the
//           total over a windward face telescopes to (peak-base) independent of tile
//           count, hill noise below the running peak costs nothing, and the lee STAYS
//           dry until the parcel descends past the belt (peak reset) or reaches the
//           sea — so the rain shadow SCALES TO ANY BELT WIDTH (the point of the rewrite).
//   This is one O(n) chain-depth pass + one O(n log n) sort + one O(n) sweep,
//   replacing the old per-tile fixed 4-hop march (O(n*4*6)) that could not fire on
//   belts wider than ~4 tiles.
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
// seeded by stageSeed; the advection sweep and flowAccum pass run serially in a
// fixed total order, so they are bit-identical at any thread count.
//
// Transient memory: tile-center cache (24 B/tile) + Pbase/oceanCharge +
// upwind ids + seenStamp + chain depth + sweep order + carriedOut/Base/Peak +
// downhill target ids, all released before return. surfCap is computed on-the-fly
// in the serial sweep to save one tileCount-sized float allocation. The center
// cache exists because the upwind lookup reads up to 6 neighbor centers per tile;
// recomputing them would dominate.

#include "worldgen/stages/PrecipitationStage.h"

#include "worldgen/stages/ClimateField.h"

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

constexpr double kEvapTminC           = -50.0;
constexpr double kEvapTmaxC           = 60.0;
constexpr double kMaxPrecipMmYr       = 8000.0;
constexpr double kOceanPrecipFactor   = 1.15;   // oceans are moisture sources

// --- Moisture-advection sweep ---
// Moisture is normalized: M = 1.0 is a saturated parcel delivering full Pbase;
// M = kMoistureFloor delivers the residual (lee/desert) precip. f(M) = clamp(M,
// floor, 1) scales Pbase by how charged the parcel still is.
constexpr double kMoistureFloor = 0.06; // lee/desert tiles still get ~6% of base

// Ocean recharge cap. Warm oceans (high evap) charge a wetter parcel; the parcel
// can carry slightly above 1.0 so the first few coastal land hops still rain at
// full Pbase (f caps at 1) before continentality bites. Derived from evap so it
// tracks the ocean's warmth/latitude (latitudeBase enters Pbase, evap enters here).
constexpr double kOceanChargeBase = 0.95; // cold/polar ocean charge
constexpr double kOceanChargeWarm = 0.45; // extra charge at full evap (warm ocean)
constexpr double kOceanChargeMax  = 1.40; // saturation buffer above f's cap of 1

// Windward orographic boost: climbing into the wind wrings out extra rain (up to
// 1 + kWindwardBoost at kOroRefM of relief above the windward base) and depletes
// the parcel by kOroLossPerM per meter of NEW peak height gained (see carriedPeak
// in the sweep). Depleting on new-peak increments — not per-hop rise — makes the
// total windward-face depletion telescope to (peak - base) regardless of how many
// tiles subdivide the slope, so it is resolution-independent AND hill-noise bumps
// that don't set a new peak cost nothing. A tall belt drains the parcel near 0, so
// the whole lee behind it stays dry across its full width; once the parcel descends
// well past the belt (kPeakResetDropM into lowland) the peak resets so a second
// belt can cast its own shadow.
constexpr double kWindwardBoost = 0.6;
constexpr double kOroRefM       = 1500.0;
constexpr double kOroLossPerM   = 1.0 / 2200.0; // ~2200 m of peak gain fully drains a parcel
constexpr double kPeakResetDropM = 800.0;       // descend this far below peak -> shadow resets

// Continentality: a parcel marching inland loses this much moisture per km, so
// interiors dry SMOOTHLY (REPLACES the old binary interior 0.8x). Keyed to
// physical distance, not hops, so the gradient is resolution-independent.
constexpr double kContinentalLossPerKm = 0.45 / 2500.0; // ~0.00018 per km

// Surface re-evaporation: each land hop the parcel picks moisture back up off the
// surface (soil, vegetation, lakes) and, in the wet convective bands, from local
// ITCZ/frontal rainfall the advection model doesn't carry. This offsets
// continentality so FLAT interiors plateau at a level rather than collapsing to
// absolute desert, and it lets a lee parcel recover over ~15-20 hops downwind of a
// belt (a rain shadow of realistic width, not an infinite one). It only lifts M
// toward the cap (never tops up an already-wetter parcel), so the wet-coast ->
// dry-interior gradient holds: near the coast M starts above the cap and DECAYS to it.
constexpr double kSurfaceRechargePerKm = 1.0 / 2500.0; // ~2.7x the continentality loss rate
// The plateau cap is LATITUDE-DEPENDENT: deep-tropical and mid-latitude interiors
// (high latitudeBase) hold more moisture because local convective/frontal rain
// recharges the surface, so equatorial interiors stay forested (Congo/Amazon) while
// subtropical interiors (low latitudeBase) dry to steppe/desert. cap = base *
// (lo + (hi-lo)*latBaseNorm), latBaseNorm = latitudeBase / 2000 in [0,1].
constexpr double kSurfaceRechargeCapBase = 0.40;
constexpr double kSurfaceRechargeCapLo   = 0.55; // multiplier in the dry subtropics
constexpr double kSurfaceRechargeCapHi   = 1.45; // multiplier in the wet ITCZ/midlat bands

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

    // Circulation cell boundaries — shared formula via ClimateField.h so that
    // precipitation bands stay physically coupled to wind cells and can't drift.
    const double rot     = ctx.params.rotationRate;
    const double sqrtRot = foundation::det_math::sqrt(rot < 0.0 ? 0.0 : rot);
    const CirculationCellEdges cellEdges = circulationCellEdges(sqrtRot);
    const double hadleyEdge = cellEdges.hadleyEdge;
    const double ferrelEdge = cellEdges.ferrelEdge;

    // Tile width in km (equatorial approximation: circumference / sqrt(N)). The
    // continentality moisture loss is keyed to physical distance via this, so the
    // interior-drying gradient is independent of grid resolution.
    const double circumferenceKm =
        2.0 * kPi * ctx.derived.planetRadiusMeters / 1000.0;
    const double tileWidthKm =
        circumferenceKm / foundation::det_math::sqrt(static_cast<double>(totalTiles));

    std::vector<Vec3d> centers(totalTiles);
    ctx.pool.parallelFor(0, totalTiles, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        for (size_t t = begin; t < end; ++t) {
            centers[t] = ctx.grid.tileCenter(static_cast<TileId>(t));
        }
        ctx.reportProgress(0.10f * static_cast<float>(end) * invTotal);
    });

    // Downwind tangent unit vector for tile t's wind heading. windDir is the
    // direction the wind blows TOWARD (0=N, 64=E), so this is the advection
    // direction. Returns false for a degenerate (polar) tile with no heading.
    auto downwindDir = [&](TileId t, const Vec3d& c, Vec3d& outDir) -> bool {
        const double theta =
            static_cast<double>(ctx.data.windDir[t]) * kRadPerWindUnit;
        // Tangent basis at c: east toward +longitude (ẑ × c), north = c × east.
        const double eastX = -c.y;
        const double eastY = c.x;
        const double eastLen2 = eastX * eastX + eastY * eastY;
        if (eastLen2 < 1e-12) return false; // pole: no heading
        const double invEast = 1.0 / foundation::det_math::sqrt(eastLen2);
        const Vec3d east{eastX * invEast, eastY * invEast, 0.0};
        const Vec3d north{-c.z * east.y, c.z * east.x,
                          c.x * east.y - c.y * east.x};
        const double sinT = foundation::det_math::sin(theta);
        const double cosT = foundation::det_math::cos(theta);
        outDir = Vec3d{north.x * cosT + east.x * sinT,
                       north.y * cosT + east.y * sinT,
                       north.z * cosT + east.z * sinT};
        return true;
    };

    // -------- Pass 1a: base moisture Pbase + upwind links (parallel) --------
    // Pbase is the pre-advection precip (everything except the moisture parcel).
    // upwind[t] is the neighbor the parcel arrives FROM (the neighbor most opposite
    // the downwind heading).
    std::vector<float>  pbase(totalTiles, 0.0f);
    std::vector<float>  oceanCharge(totalTiles, 0.0f); // Mcap for ocean tiles
    // surfCap (latitude-dependent plateau cap) is computed on-the-fly in the serial
    // sweep (pass 1c) to avoid a tileCount-sized allocation here. The cap formula
    // needs only latBase and the named constants, all of which are in scope there.
    std::vector<TileId> upwind(totalTiles, kInvalidTile);
    ctx.pool.parallelFor(0, totalTiles, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        std::array<TileId, 6> nbs{};
        for (size_t t = begin; t < end; ++t) {
            const TileId tile = static_cast<TileId>(t);
            const Vec3d c = centers[t];
            const double lat = foundation::det_math::asin(c.z) / kPiOver180;
            const double absLat = lat < 0.0 ? -lat : lat;

            const double latBase = latitudePrecipBase(absLat, hadleyEdge, ferrelEdge);
            const float noise = foundation::valueNoise3(
                static_cast<float>(c.x) * 3.0f,
                static_cast<float>(c.y) * 3.0f,
                static_cast<float>(c.z) * 3.0f,
                seed32);
            const double tempC =
                static_cast<double>(ctx.data.temperatureMean[t]) * 0.1;
            double evap = 0.8 + 0.4 * (tempC - kEvapTminC) / (kEvapTmaxC - kEvapTminC);
            if (evap < 0.8) evap = 0.8;
            if (evap > 1.2) evap = 1.2;

            // Pbase = latitude * noise * waterFactor * evap (the old pre-orographic
            // precip). Advection scales this by f(M) on land below.
            double pb = latBase * (0.6 + 0.8 * static_cast<double>(noise));
            pb *= 0.5 + ctx.params.waterAmount;
            pb *= evap;
            if (pb < 0.0) pb = 0.0;
            pbase[t] = static_cast<float>(pb);

            // Ocean recharge cap: warmer (higher evap) oceans charge a wetter
            // parcel. Capped above f's ceiling so coastal land starts at full base.
            double charge = kOceanChargeBase + kOceanChargeWarm * (evap - 0.8) / 0.4;
            if (charge > kOceanChargeMax) charge = kOceanChargeMax;
            oceanCharge[t] = static_cast<float>(charge);

            // Upwind link: the neighbor most OPPOSITE the downwind heading (the
            // direction the moisture parcel arrives from).
            // bestUp starts below any achievable alignment (-2 < any dot product
            // of unit vectors divided by distance) so a neighbor with alignment
            // exactly 0.0 is correctly selected rather than rejected.
            Vec3d dir{};
            if (downwindDir(tile, c, dir)) {
                const uint32_t cnt = ctx.grid.neighbors(tile, nbs);
                double bestUp = -2.0;
                TileId up = kInvalidTile;
                for (uint32_t k = 0; k < cnt; ++k) {
                    const Vec3d& nc = centers[nbs[k]];
                    const Vec3d d{nc.x - c.x, nc.y - c.y, nc.z - c.z};
                    const double dl2 = dot3(d, d);
                    if (dl2 <= 0.0) continue;
                    const double align = -dot3(d, dir) / foundation::det_math::sqrt(dl2);
                    if (align > bestUp ||
                        (align == bestUp && nbs[k] < up)) {
                        bestUp = align; up = nbs[k];
                    }
                }
                upwind[t] = up;
            }
        }
        ctx.reportProgress(0.10f + 0.20f * static_cast<float>(end) * invTotal);
    });

    // -------- Pass 1b: deterministic sweep order (upwind-chain depth) --------
    // The sweep must process every tile AFTER the tile upwind of it so the carried
    // moisture is final when read. The upwind links form a functional graph (each
    // tile has exactly one upwind parent), so "depth along the upwind chain back to
    // a source" is a valid topological key: a tile always has strictly greater depth
    // than its upwind parent, so sorting by (depth, TileId) processes parents first.
    // Sources are ocean tiles and tiles whose upwind link is invalid; cycles (two
    // tiles pointing at each other across a convergence line) are broken at the FIRST
    // tile that is detected to already appear in the current chain walk (the repeated
    // tile itself becomes depth 0), which is deterministic because the chain walk
    // always starts from the same start tile and follows the same upwind links.
    // Computed by an iterative walk (no recursion -> no stack overflow at millions of
    // tiles). Pure function of the upwind array, so it is thread-count independent.
    std::vector<int32_t> depth(totalTiles, -1);
    {
        // seenStamp[t] == start means tile t was pushed onto the chain for the
        // walk that started at `start`. O(1) membership test per hop, replacing
        // the previous O(chain-length) linear scan. The break point is identical
        // to the old scan: the first tile whose seenStamp already equals start
        // is the first repeated tile in the current chain, and it becomes depth 0.
        std::vector<TileId> seenStamp(totalTiles, kInvalidTile);
        std::vector<TileId> chain;
        chain.reserve(64);
        for (TileId start = 0; start < totalTiles; ++start) {
            if (depth[start] >= 0) continue;
            chain.clear();
            TileId cur = start;
            bool cycle = false;
            // Walk upwind until we reach a resolved tile, a source, or a cycle.
            while (true) {
                if (depth[cur] >= 0) break;            // parent already resolved (not in chain)
                const bool isOcean = ctx.data.elevation[cur] < seaLevel;
                const TileId up = upwind[cur];
                if (isOcean || up == kInvalidTile) {   // source: depth 0 (not in chain)
                    depth[cur] = 0;
                    break;
                }
                // Detect a cycle: cur already appears earlier in this chain.
                if (seenStamp[cur] == start) {
                    depth[cur] = 0; cycle = true; break;  // break the cycle here; cur IS in chain
                }
                seenStamp[cur] = start;                // mark as seen for this walk
                depth[cur] = -2;                       // mark in-progress
                chain.push_back(cur);
                cur = up;
            }
            // Unwind, assigning depth = parent depth + 1. When the walk closed a cycle,
            // `cur` is the break tile and is already in `chain` with depth 0: leave it at
            // 0 and re-base from it so tiles downwind of the break measure depth from the
            // break, not from inside the cycle (no inflation by the cycle length).
            int32_t base = depth[cur];
            for (size_t k = chain.size(); k-- > 0;) {
                if (cycle && chain[k] == cur) {
                    base = depth[cur];                 // = 0; keep the break tile's depth
                    continue;
                }
                base += 1;
                depth[chain[k]] = base;
            }
        }
    }

    std::vector<TileId> sweepOrder(totalTiles);
    for (TileId t = 0; t < totalTiles; ++t) sweepOrder[t] = t;
    std::sort(sweepOrder.begin(), sweepOrder.end(), [&](TileId a, TileId b) {
        if (depth[a] != depth[b]) return depth[a] < depth[b];
        return a < b;
    });
    ctx.reportProgress(0.45f);

    // -------- Pass 1c: moisture-budget sweep (serial, in sweep order) --------
    // PULL model: each land tile reads the moisture its UPWIND neighbor carries
    // out (carriedOut[upwind]); under the prevailing onshore flow that neighbor is
    // closer to the sea, so it sorts earlier and its value is final. A tile whose
    // upwind neighbor sorts LATER (offshore-flow coast, or a parcel coming from
    // deeper interior) reads the sentinel and starts dry — physically correct: such
    // a tile is fed by dried-out continental air, not the sea. carriedOut = -1 =
    // not yet processed.
    const double contLossPerHop     = kContinentalLossPerKm * tileWidthKm;
    const double surfRechargePerHop = kSurfaceRechargePerKm * tileWidthKm;
    // carriedOut[t] = moisture the parcel carries downwind out of t (-1 = unprocessed).
    // carriedBase[t] = the running LOW the parcel last descended to (windward-boost
    //   reference). carriedPeak[t] = the running HIGH (orographic-depletion ratchet),
    //   so depletion telescopes to peak-gained and is resolution-independent.
    std::vector<float> carriedOut(totalTiles, -1.0f);
    std::vector<float> carriedBase(totalTiles, 0.0f);
    std::vector<float> carriedPeak(totalTiles, 0.0f);
    for (size_t i = 0; i < sweepOrder.size(); ++i) {
        if ((i & 0xFFFFFu) == 0u) throwIfCancelled(ctx);
        const TileId t = sweepOrder[i];
        const double e0 = static_cast<double>(ctx.data.elevation[t]);

        if (e0 < dSeaLevel) {
            // Ocean: saturate the parcel; precip is the base ocean source term.
            carriedOut[t]  = oceanCharge[t];
            carriedBase[t] = static_cast<float>(dSeaLevel);
            carriedPeak[t] = static_cast<float>(dSeaLevel);
            double precip = static_cast<double>(pbase[t]) * kOceanPrecipFactor;
            if (precip > kMaxPrecipMmYr) precip = kMaxPrecipMmYr;
            ctx.data.precipitation[t] = static_cast<uint16_t>(precip);
            continue;
        }

        // Land: incoming moisture + windward base/peak from the upwind neighbor
        // (final if it sorted earlier; otherwise the parcel starts dry from here).
        const TileId u = upwind[t];
        double m, baseElev, peakElev;
        if (u != kInvalidTile && carriedOut[u] >= 0.0f) {
            m        = static_cast<double>(carriedOut[u]);
            baseElev = static_cast<double>(carriedBase[u]);
            peakElev = static_cast<double>(carriedPeak[u]);
        } else {
            m        = 0.0;  // no upwind moisture source yet
            baseElev = e0;   // start the windward base/peak here
            peakElev = e0;
        }

        // Surface re-evaporation FIRST (applied before rainout) so a parcel that
        // arrived dry still rains at the level its band can re-evaporate to, and
        // flat interiors plateau there rather than at the desert floor. The surface
        // is a weak source: it only lifts M toward the (latitude-dependent) cap,
        // never tops up an already-wetter parcel, so wet coasts and lee shadows hold.
        // Cap is computed on-the-fly from the tile center (centers[] is still live)
        // to avoid carrying a full surfCap[] array through the sweep.
        const Vec3d& tc = centers[t];
        const double tAbsLat = [&] {
            const double tlat = foundation::det_math::asin(tc.z) / kPiOver180;
            return tlat < 0.0 ? -tlat : tlat;
        }();
        const double tLatBase = latitudePrecipBase(tAbsLat, hadleyEdge, ferrelEdge);
        double tLatBaseNorm = tLatBase / 2000.0;
        if (tLatBaseNorm < 0.0) tLatBaseNorm = 0.0;
        if (tLatBaseNorm > 1.0) tLatBaseNorm = 1.0;
        const double cap = kSurfaceRechargeCapBase *
            (kSurfaceRechargeCapLo +
             (kSurfaceRechargeCapHi - kSurfaceRechargeCapLo) * tLatBaseNorm);
        if (m < cap) {
            m += surfRechargePerHop;
            if (m > cap) m = cap;
        }
        // Continentality: every land hop costs moisture (smooth interior drying).
        m -= contLossPerHop;
        if (m < 0.0) m = 0.0;

        // Rain falls FIRST, using the moisture the parcel arrived with, boosted on
        // the windward slope (precip rises with height above the windward base — the
        // low the parcel last descended to). The rain that falls is then what
        // depletes the parcel below.
        double f = m;
        if (f < kMoistureFloor) f = kMoistureFloor;
        if (f > 1.0) f = 1.0;
        double rainHere = static_cast<double>(pbase[t]) * f;
        const double aboveBase = e0 - baseElev;
        if (aboveBase > 0.0) {
            double s = aboveBase / kOroRefM;
            if (s > 1.0) s = 1.0;
            rainHere *= 1.0 + kWindwardBoost * s;
        }

        // Orographic depletion: only NEW peak height gained wrings moisture out, so
        // crossing a windward face depletes by ~(peak-base)*kOroLossPerM total no
        // matter how finely it is tiled (resolution-independent), and sitting at
        // altitude or on small bumps below the running peak costs nothing. The lee
        // therefore stays dry across the whole belt width.
        if (e0 > peakElev) {
            m -= kOroLossPerM * (e0 - peakElev);
            if (m < 0.0) m = 0.0;
            peakElev = e0;
        }

        // Track the running low (windward-boost reference) and reset the peak once
        // the parcel has descended well past the high ground into lowland, so a
        // later belt can cast its own shadow.
        if (e0 < baseElev) baseElev = e0;
        if (e0 < peakElev - kPeakResetDropM) { peakElev = e0; baseElev = e0; }
        carriedBase[t] = static_cast<float>(baseElev);
        carriedPeak[t] = static_cast<float>(peakElev);
        carriedOut[t]  = static_cast<float>(m);

        if (rainHere < 0.0) rainHere = 0.0;
        if (rainHere > kMaxPrecipMmYr) rainHere = kMaxPrecipMmYr;
        ctx.data.precipitation[t] = static_cast<uint16_t>(rainHere);
    }
    ctx.reportProgress(0.60f);

    pbase.clear();       pbase.shrink_to_fit();
    oceanCharge.clear(); oceanCharge.shrink_to_fit();
    upwind.clear();      upwind.shrink_to_fit();
    depth.clear();       depth.shrink_to_fit();
    carriedOut.clear();  carriedOut.shrink_to_fit();
    carriedBase.clear(); carriedBase.shrink_to_fit();
    carriedPeak.clear(); carriedPeak.shrink_to_fit();
    sweepOrder.clear();  sweepOrder.shrink_to_fit();

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
            std::array<TileId, 6> nbs{};
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
