#include "worldgen/stages/ErosionStage.h"

#include "worldgen/data/WorldData.h"
#include "worldgen/stages/DrainageRouting.h"

#include <math/DeterministicMath.h>

#include <algorithm>
#include <vector>

namespace worldgen {

namespace {

constexpr double kPi = 3.14159265358979323846;

// --- Stream-power erosion constants (tuned for the "subtle" default) ---
// Detachment-limited incision dh/dt = -K * A^m * S, solved implicitly (unconditionally
// stable). A = upstream drainage area in km^2 (resolution-invariant), S = slope to the
// receiver, m = 0.5 so A^m = sqrt(A) (det_math evaluates it exactly). Large-A channels
// incise toward their receiver; ridges and divides (small A) barely move, so valleys
// deepen while the orogenic belt crests are preserved -- dissection, not flattening.
constexpr float kErosionK            = 0.020f;  // base erodibility (dt folded in)
constexpr float kErosionStrength     = 1.0f;    // tunable knob; default subtle
constexpr int   kErosionSweeps       = 20;      // implicit incision passes on the fixed drainage
                                                // (the sweeps are cheap; the priority-flood dominates)
constexpr float kErosionMaxIncisionM = 1500.0f; // safety cap on cumulative drop per tile (* strength)

} // namespace

void ErosionStage::run(StageContext& ctx) {
    const TileId totalTiles = static_cast<TileId>(ctx.data.elevation.size());
    const float  seaLevel   = ctx.world.seaLevelMeters;

    // Physical tile size for resolution-invariant erosion: mean tile area + spacing (km).
    const double Rkm           = ctx.derived.planetRadiusMeters / 1000.0;
    const double sphereAreaKm2 = 4.0 * kPi * Rkm * Rkm;
    const float  tileAreaKm2   = static_cast<float>(sphereAreaKm2 / static_cast<double>(totalTiles));
    const float  dxKm          = static_cast<float>(
        foundation::det_math::sqrt(static_cast<double>(tileAreaKm2)));

    // 1. Provisional depression-routed drainage on the current terrain (shared helper).
    std::vector<float>  filled;
    std::vector<TileId> receiver;
    routeDepressions(ctx.grid, ctx.data.elevation, seaLevel, filled, receiver, ctx.cancelRequested);
    ctx.reportProgress(0.40f);

    // 2. Drainage-stack order: land tiles by (filled ascending, TileId ascending). A tile's
    //    receiver was popped earlier in the flood, so it always precedes the tile in this
    //    order -- exactly the order the implicit solve needs (receiver updated first).
    std::vector<TileId> order;
    order.reserve(totalTiles);
    for (TileId t = 0; t < totalTiles; ++t) {
        if (ctx.data.elevation[t] >= seaLevel) order.push_back(t);
    }
    std::sort(order.begin(), order.end(), [&](TileId a, TileId b) {
        if (filled[a] != filled[b]) return filled[a] < filled[b];
        return a < b;
    });

    // 3. Upstream drainage area A (km^2): each land tile contributes its own cell area;
    //    accumulate downstream by iterating the stack in reverse (upstream -> downstream).
    std::vector<float> area(totalTiles, 0.0f);
    for (const TileId t : order) area[t] = tileAreaKm2;
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const TileId t = *it;
        const TileId r = receiver[t];
        if (r != kInvalidTile && ctx.data.elevation[r] >= seaLevel) area[r] += area[t];
    }
    ctx.reportProgress(0.55f);

    // 4. Implicit stream-power incision (Braun & Willett 2013). Process the stack forward
    //    (receiver before tile) so h_r is already this-sweep-updated:
    //        h_i = (h_i + f*h_r) / (1 + f),   f = K * sqrt(A_i) / dx
    //    a weighted pull toward the receiver, strongest where A is large. The original
    //    terrain is the base state (uplift already came from tectonics); incision only
    //    lowers. Clamps keep channels monotone downhill, never below sea level (no new
    //    ocean), and bound cumulative incision so a belt-crossing channel can't gut a range.
    const float K       = kErosionK * kErosionStrength;
    const float maxDrop = kErosionMaxIncisionM * kErosionStrength;
    const std::vector<float> original(ctx.data.elevation.begin(), ctx.data.elevation.end());

    for (int sweep = 0; sweep < kErosionSweeps; ++sweep) {
        throwIfCancelled(ctx);
        for (const TileId t : order) {
            // Skip basin/lake floors (filled above the ORIGINAL terrain): depositional
            // base levels, not eroding. Keyed to `original` so the mask is stable as
            // incision lowers elevation across sweeps.
            if (filled[t] > original[t] + 0.5f) continue;
            const TileId r = receiver[t];
            if (r == kInvalidTile) continue; // endorheic sink: local base level
            float hr = ctx.data.elevation[r];
            if (hr < seaLevel) hr = seaLevel; // outlet to ocean -> base level is sea level
            const float hi = ctx.data.elevation[t];
            if (hi <= hr) continue;           // already at/below the receiver
            const float f = K *
                static_cast<float>(foundation::det_math::sqrt(static_cast<double>(area[t]))) / dxKm;
            float hnew = (hi + f * hr) / (1.0f + f);
            const float floorElev = original[t] - maxDrop;
            if (hnew < floorElev) hnew = floorElev; // cumulative-incision cap
            if (hnew < hr)        hnew = hr;        // stay monotone downhill
            if (hnew < seaLevel)  hnew = seaLevel;  // never create ocean
            ctx.data.elevation[t] = hnew;
        }
        ctx.reportProgress(0.55f + 0.40f * static_cast<float>(sweep + 1) /
                                          static_cast<float>(kErosionSweeps));
    }
}

} // namespace worldgen
