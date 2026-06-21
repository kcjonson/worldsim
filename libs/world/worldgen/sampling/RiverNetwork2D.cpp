#include "worldgen/sampling/RiverNetwork2D.h"

#include "worldgen/data/WorldData.h"
#include "worldgen/grid/SphereGrid.h"

#include <math/DeterministicMath.h>
#include <utils/WorldHash.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>  // std::floor, std::ceil
#include <limits>
#include <unordered_set>

namespace worldgen {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Channel meander: a smooth, wandering centerline driven by a 1D value-noise
// lateral offset. Value noise has a bounded gradient (quintic interpolation, C2),
// so the path drifts in gentle curves and never forms the hard zig-zag a
// high-amplitude sum of sines produces. The feature length is the dominant bend
// spacing (scales with width so big rivers swing wider); the amplitude is a
// fraction of it, small enough that the bounded slope stays gentle. The offset is
// tapered to zero within a short arc length of each coarse-tile joint so segments
// meet at the tile centers (cross-segment continuity).
constexpr double kMeanderFeatureMult = 9.0;
constexpr double kMeanderFeatureMin  = 380.0;
constexpr double kMeanderFeatureMax  = 2600.0;
constexpr double kMeanderAmpFrac     = 0.30;   // lateral excursion as a fraction of feature length
constexpr double kMeanderAmpCap      = 1000.0; // meters

// Channel width varies along its length: quick riffles plus occasional pools
// (local widenings where the current slows). Pools are asymmetric -- short crests
// over long calm stretches -- so the multiplier mostly sits near 1 and bulges now
// and then, the way a real stream pinches and pools as it runs.
constexpr double kWidthWavelen   = 220.0;
constexpr double kWidthVariation = 0.28; // +/- riffle fraction of the hydraulic width
constexpr double kPoolWavelen    = 560.0;
constexpr double kPoolStrength   = 0.60; // extra width at a pool crest

// Render contiguity floor. Tiles are 1 m, so a channel rasterized narrower than
// ~1.5 m breaks into a dotted, non-contiguous line on the grid. Hold every
// emitted half-width at or above this; a stream's thinness is shown by its
// (shallow) depth -- derived downstream from width -- not by a sub-tile channel.
// 0.8 m half-width guarantees a 4-connected ribbon at any orientation.
constexpr float kRenderMinHalf = 0.8f;

// Polyline density near the query box: ~20 m straight pieces resolve the wander.
// Sampled on a GLOBAL arc-length grid (multiples of kStepMeters from the segment
// source) so adjacent chunks emit identical points where they overlap -> seamless.
constexpr double kStepMeters = 20.0;

// Hydraulic geometry: channel width ~ sqrt(discharge), clamped. Headwater streams
// start narrow and broaden downstream as flow accumulates. flowAccum is rain-
// seeded upstream drainage (precip/1000 per tile). The min is a sub-metre trickle
// so springs and the narrowest reaches read as a thread of water.
constexpr float kWidthCoef = 1.1f;
constexpr float kMinWidth  = 0.6f;
constexpr float kMaxWidth  = 110.0f;

// Procedural short feeders (springs/streams) branching off rendered river
// channels. SHORT (under ~0.5 km, never the ~50 km coarse-tile span): the coarse
// 3D graph supplies real rivers; sub-river streams are synthesized here. Each
// leaves the bank almost perpendicular to the channel, tilted a little downstream
// (a natural acute confluence), and climbs the valley side to a spring, tapering
// from the parent down to a trickle.
constexpr double kFeederMinParentWidth = 3.0;   // along-channel feeders need a real channel
constexpr double kFeederSpacing        = 440.0; // confluence spacing along the parent
constexpr double kFeederSpawnProb      = 0.55;
constexpr double kFeederLenMin         = 90.0;
constexpr double kFeederLenMax         = 480.0;
constexpr double kFeederPerpTiltMinDeg = 18.0;  // tilt from perpendicular, opening downstream
constexpr double kFeederPerpTiltMaxDeg = 42.0;
constexpr double kFeederMouthFrac      = 0.45;  // mouth half-width vs parent half-width
constexpr double kFeederMouthMaxHalf   = 3.0;
constexpr double kFeederStepMeters     = 11.0;  // fine enough to resolve the tight wiggle
constexpr double kFeederBankInset      = 0.6;   // start inside the bank so the mouth meets parent water
constexpr double kFeederMeanderFeature = 60.0;  // bend spacing -> windy but smooth
constexpr double kFeederMeanderAmp     = 14.0;  // lateral wander (meters)
constexpr int    kHeadwaterFeederCount = 2;     // a source is fed by two trickles, one per bank
constexpr double kHeadwaterFanDeg      = 55.0;  // springs spread +/- this around upstream so they diverge

// How far upstream of the query box a tile can sit and still have its downstream
// segment cross it: one tile step plus the meander, feeder, and channel headroom.
constexpr double kReachTileFactor = 1.25;

// FNV hash -> double in [0,1) using the top 53 bits (the double mantissa).
double hashUnit(uint64_t h) {
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}

// Smooth 1D value noise in [-1,1]: hashed lattice values interpolated with a
// quintic (C2), so the curve and its slope are continuous -- no kinks. floor is
// exact in IEEE, so this is deterministic and cross-platform like the det_math
// trig used elsewhere.
double valueNoise1D(double x, uint64_t salt) {
    const double xf = std::floor(x);
    const auto   i0 = static_cast<int64_t>(xf);
    const double f = x - xf;
    const double u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0); // quintic smoothstep
    auto lat = [&](int64_t i) {
        return 2.0 * hashUnit(foundation::hashCombine(salt, static_cast<uint64_t>(i))) - 1.0;
    };
    const double a = lat(i0);
    const double b = lat(i0 + 1);
    return a + (b - a) * u;
}

// Two octaves of value noise (~[-1,1]): one dominant smooth bend plus a little
// finer wander. The detail octave stays small so the gradient -- and therefore the
// meander's sharpness -- stays gentle.
double meanderNoise(double x, uint64_t salt) {
    const double o0 = valueNoise1D(x, salt);
    const double o1 = valueNoise1D(x * 2.6 + 17.0, foundation::hashCombine(salt, 0xA1u));
    return (o0 + 0.4 * o1) / 1.4;
}

// Width multiplier at arc length s: gentle riffles plus rare wider pools. The
// pool term is a cubed, one-sided sine, so it spends most of its length near zero
// (riffle) and bulges only at crests.
double widthVariation(double s, double phaseRiffle, double phasePool) {
    const double riffle =
        kWidthVariation * foundation::det_math::sin(2.0 * kPi * s / kWidthWavelen + phaseRiffle);
    double crest = foundation::det_math::sin(2.0 * kPi * s / kPoolWavelen + phasePool);
    crest = crest > 0.0 ? crest : 0.0;
    const double pool = kPoolStrength * crest * crest * crest;
    return 1.0 + riffle + pool;
}

// Distance from point p to segment [a,b]; outT is the clamped projection
// parameter in [0,1] of the closest point.
double distPointSegment(double px, double py, double ax, double ay,
                        double bx, double by, double& outT) {
    const double dx = bx - ax;
    const double dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    double t = 0.0;
    if (len2 > 0.0) {
        t = ((px - ax) * dx + (py - ay) * dy) / len2;
        t = std::clamp(t, 0.0, 1.0);
    }
    const double cx = ax + dx * t;
    const double cy = ay + dy * t;
    outT = t;
    const double ex = px - cx;
    const double ey = py - cy;
    return foundation::det_math::sqrt(ex * ex + ey * ey);
}

} // namespace

float RiverNetwork2D::channelWidthMeters(float flowAccum) {
    const double f = flowAccum > 0.0f ? static_cast<double>(flowAccum) : 0.0;
    const float w = kWidthCoef * static_cast<float>(foundation::det_math::sqrt(f));
    return std::clamp(w, kMinWidth, kMaxWidth);
}

RiverNetwork2D::RiverNetwork2D(std::shared_ptr<const GeneratedWorld> generatedWorld,
                               double landingLatDeg, double landingLonDeg)
    : world(std::move(generatedWorld)),
      projection(world->derived.planetRadiusMeters, landingLatDeg, landingLonDeg),
      seaLevelMeters(world->seaLevelMeters) {
    constexpr uint32_t kRequiredFields =
        static_cast<uint32_t>(WorldField::Elevation) |
        static_cast<uint32_t>(WorldField::Flags) |
        static_cast<uint32_t>(WorldField::FlowAccum) |
        static_cast<uint32_t>(WorldField::Downhill);
    assert(world->grid != nullptr);
    assert((world->validFields & kRequiredFields) == kRequiredFields);
}

WorldPos2d RiverNetwork2D::projectTile(TileId t) const {
    double latDeg = 0.0;
    double lonDeg = 0.0;
    world->grid->latLonOf(t, latDeg, lonDeg);
    return projection.latLonToWorld({latDeg, lonDeg});
}

bool RiverNetwork2D::isOceanTile(TileId t) const {
    return (world->data.flags[t] & kFlagOcean) != 0 ||
           world->data.elevation[t] < seaLevelMeters;
}

bool RiverNetwork2D::isRiverTile(TileId t) const {
    return (world->data.flags[t] & kFlagRiver) != 0;
}

bool RiverNetwork2D::isHeadwaterRiverTile(TileId t) const {
    if (!isRiverTile(t) || isOceanTile(t)) return false;
    std::array<TileId, 6> nbrs{};
    const uint32_t count = world->grid->neighbors(t, nbrs);
    for (uint32_t i = 0; i < count; ++i) {
        const TileId u = nbrs[i];
        if (isRiverTile(u) && !isOceanTile(u) && downstreamTile(u) == t) return false;
    }
    return true;
}

TileId RiverNetwork2D::downstreamTile(TileId t) const {
    const uint8_t d = world->data.downhill[t];
    if (d == 0xFFu) return kInvalidTile;
    std::array<TileId, 6> nbrs{};
    const uint32_t count = world->grid->neighbors(t, nbrs);
    if (d >= count) return kInvalidTile;
    return nbrs[d];
}

void RiverNetwork2D::collectRiverTiles(double minX, double minY, double maxX, double maxY,
                                       std::vector<TileId>& out) const {
    const SphereGrid& grid = *world->grid;
    const double cx = 0.5 * (minX + maxX);
    const double cy = 0.5 * (minY + maxY);
    const TileId seed = grid.fromUnitVector(projection.worldToUnitVector(cx, cy));

    // A segment whose straight axis lies outside the box can still sweep/meander
    // into it; include the worst-case lateral excursion in the reach.
    const double reach =
        grid.tileWidthMeters(seed, world->derived.planetRadiusMeters) * kReachTileFactor +
        kMeanderAmpCap + kFeederLenMax + static_cast<double>(kMaxWidth);
    const double exMinX = minX - reach;
    const double exMinY = minY - reach;
    const double exMaxX = maxX + reach;
    const double exMaxY = maxY + reach;

    std::unordered_set<TileId> visited;
    std::vector<TileId> stack;
    stack.push_back(seed);
    visited.insert(seed);

    while (!stack.empty()) {
        const TileId t = stack.back();
        stack.pop_back();

        const WorldPos2d p = projectTile(t);
        const bool inside = p.x >= exMinX && p.x <= exMaxX && p.y >= exMinY && p.y <= exMaxY;
        if (!inside) continue;  // visited (so never re-queued) but do not expand past the box

        if (isRiverTile(t) && !isOceanTile(t)) out.push_back(t);

        std::array<TileId, 6> nbrs{};
        const uint32_t count = grid.neighbors(t, nbrs);
        for (uint32_t i = 0; i < count; ++i) {
            if (visited.insert(nbrs[i]).second) stack.push_back(nbrs[i]);
        }
    }

    // Deterministic emission order regardless of BFS traversal.
    std::sort(out.begin(), out.end());
}

void RiverNetwork2D::emitSegment(TileId tile, double minX, double minY, double maxX, double maxY,
                                 std::vector<Segment>& out) const {
    const TileId down = downstreamTile(tile);
    if (down == kInvalidTile) return;  // endorheic sink; basin fill (lake biome) covers it

    const WorldPos2d pa = projectTile(tile);
    const WorldPos2d pb = projectTile(down);
    const double dx = pb.x - pa.x;
    const double dy = pb.y - pa.y;
    const double length = foundation::det_math::sqrt(dx * dx + dy * dy);
    if (length < 1e-6) return;

    const double ux = dx / length;
    const double uy = dy / length;
    const double perpx = -uy;
    const double perpy = ux;

    const float flowA = world->data.flowAccum[tile];
    // Do not taper to zero at the mouth: ocean tiles carry no flow.
    const float flowB = isOceanTile(down) ? flowA : world->data.flowAccum[down];

    // Deterministic per-segment hash; phases for the width riffles/pools.
    const uint64_t base = foundation::hashCombine(
        foundation::hashCombine(world->params.seed, tile), down);
    const double phaseW = 2.0 * kPi * hashUnit(foundation::hashCombine(base, 0x43));
    const double phaseP = 2.0 * kPi * hashUnit(foundation::hashCombine(base, 0x44));

    // Meander geometry scales with the segment's representative width.
    const double segWidth = static_cast<double>(channelWidthMeters(0.5f * (flowA + flowB)));
    const double meanderFeature =
        std::clamp(kMeanderFeatureMult * segWidth, kMeanderFeatureMin, kMeanderFeatureMax);
    const double meanderAmp =
        std::min({kMeanderAmpFrac * meanderFeature, 0.22 * length, kMeanderAmpCap});
    // Taper the meander to zero within a short arc-length of each tile-center joint
    // so adjacent segments meet exactly at the tile centers (continuity).
    const double meanderTaper = std::clamp(meanderFeature * 0.15, 80.0, 250.0);

    // Centerline point + channel half-width at arc length s along the segment.
    auto eval = [&](double s, double& ox, double& oy, float& ohw) {
        const double t = s / length;
        const double distEnd = std::min(s, length - s);
        double e = distEnd / meanderTaper;
        e = e <= 0.0 ? 0.0 : (e >= 1.0 ? 1.0 : e * e * (3.0 - 2.0 * e)); // smoothstep
        const double env = e;
        // Smooth value-noise offset in global arc length: gentle wandering curves,
        // never a hard zig-zag. Keyed on s (not t) so neighbouring chunks emit
        // identical points where they overlap.
        const double offset = env * meanderAmp * meanderNoise(s / meanderFeature, base);
        ox = pa.x + ux * s + perpx * offset;
        oy = pa.y + uy * s + perpy * offset;

        const float flow = flowA + (flowB - flowA) * static_cast<float>(t);
        const double baseHalf = 0.5 * static_cast<double>(channelWidthMeters(flow));
        const double wv = widthVariation(s, phaseW, phaseP);
        ohw = std::max(kRenderMinHalf, static_cast<float>(baseHalf * wv));
    };

    // Restrict to the stretch of the segment near the query box: project the box
    // onto the segment axis, pad for the channel half-width. Lateral wander does
    // not move a point along the axis, so the box's axis-projection bounds it.
    const double margin = 0.5 * static_cast<double>(kMaxWidth) + kStepMeters;
    double sBoxLo = std::numeric_limits<double>::max();
    double sBoxHi = std::numeric_limits<double>::lowest();
    const double cornersX[4] = {minX, maxX, minX, maxX};
    const double cornersY[4] = {minY, minY, maxY, maxY};
    for (int i = 0; i < 4; ++i) {
        const double s = (cornersX[i] - pa.x) * ux + (cornersY[i] - pa.y) * uy;
        sBoxLo = std::min(sBoxLo, s);
        sBoxHi = std::max(sBoxHi, s);
    }
    const double sLo = std::clamp(sBoxLo - margin, 0.0, length);
    const double sHi = std::clamp(sBoxHi + margin, 0.0, length);

    // Channel: sample on a global arc-length grid so neighbouring chunks share points.
    if (sHi > sLo) {
        const long kFirst = static_cast<long>(std::floor(sLo / kStepMeters));
        const long kLast = static_cast<long>(std::ceil(sHi / kStepMeters));
        double prevX = 0.0;
        double prevY = 0.0;
        float  prevHW = 0.0f;
        bool   havePrev = false;
        for (long k = kFirst; k <= kLast; ++k) {
            const double s = std::clamp(static_cast<double>(k) * kStepMeters, 0.0, length);
            double cx = 0.0;
            double cy = 0.0;
            float  hw = 0.0f;
            eval(s, cx, cy, hw);
            if (havePrev) {
                const double pad = static_cast<double>(std::max(prevHW, hw));
                if (std::max(prevX, cx) + pad >= minX && std::min(prevX, cx) - pad <= maxX &&
                    std::max(prevY, cy) + pad >= minY && std::min(prevY, cy) - pad <= maxY) {
                    out.push_back({prevX, prevY, cx, cy, prevHW, hw});
                }
            }
            prevX = cx;
            prevY = cy;
            prevHW = hw;
            havePrev = true;
        }
    }

    // --- Procedural short feeders: springs and streams flowing into this channel.
    // Each is fully determined by the parent tile-pair + a confluence key, so any
    // chunk that gathers this parent emits identical feeders (seamless across
    // seams). A feeder runs out along an explicit direction to a spring, winding
    // tightly along the way (windy, not one broad arc).
    auto emitFeeder = [&](double cx0, double cy0, double parentHalfHere,
                          double dirx, double diry, double len, uint64_t fh) {
        const double dlen = foundation::det_math::sqrt(dirx * dirx + diry * diry);
        if (dlen < 1e-9) return;
        const double dux = dirx / dlen;
        const double duy = diry / dlen;
        const double fpx = -duy; // wiggle axis, perpendicular to the feeder's run
        const double fpy = dux;
        // Anchor a little inside the bank (toward the side the feeder leaves on) so
        // the mouth overlaps the parent water and never reaches the far bank.
        double bankSign = dux * perpx + duy * perpy;
        bankSign = bankSign >= 0.0 ? 1.0 : -1.0;
        const double sx = cx0 + bankSign * perpx * (parentHalfHere * kFeederBankInset);
        const double sy = cy0 + bankSign * perpy * (parentHalfHere * kFeederBankInset);
        const double mouthHalf = std::clamp(parentHalfHere * kFeederMouthFrac,
                                            static_cast<double>(kRenderMinHalf), kFeederMouthMaxHalf);
        const double phaseFP = 2.0 * kPi * hashUnit(foundation::hashCombine(fh, 0x9));
        const int fSteps = std::clamp(static_cast<int>(std::lround(len / kFeederStepMeters)), 3, 60);

        double pfx = sx;
        double pfy = sy;
        float  pfh = static_cast<float>(mouthHalf);
        for (int i = 1; i <= fSteps; ++i) {
            const double f = static_cast<double>(i) / static_cast<double>(fSteps);
            const double along = len * f;
            // Smooth value-noise wander, tapered to zero at both ends (so the mouth
            // sits on the bank and the spring is a point) -- windy but never kinked.
            const double env = foundation::det_math::sin(kPi * f);
            const double moff = kFeederMeanderAmp * env * meanderNoise(along / kFeederMeanderFeature, fh);
            const double fx = sx + dux * along + fpx * moff;
            const double fy = sy + duy * along + fpy * moff;
            // Taper mouth -> trickle (render floor), with riffle/pool play.
            const double taper = mouthHalf + (static_cast<double>(kRenderMinHalf) - mouthHalf) * f;
            const float fhw = std::max(kRenderMinHalf,
                                       static_cast<float>(taper * widthVariation(along, phaseFP, phaseFP * 1.7)));
            const double pad = static_cast<double>(std::max(pfh, fhw));
            if (std::max(pfx, fx) + pad >= minX && std::min(pfx, fx) - pad <= maxX &&
                std::max(pfy, fy) + pad >= minY && std::min(pfy, fy) - pad <= maxY) {
                out.push_back({pfx, pfy, fx, fy, pfh, fhw});
            }
            pfx = fx;
            pfy = fy;
            pfh = fhw;
        }
    };

    auto feederLen = [&](uint64_t fh) {
        return kFeederLenMin +
               (kFeederLenMax - kFeederLenMin) * hashUnit(foundation::hashCombine(fh, 0x4));
    };

    // Headwater source: two trickles converge at the river's birth (s ~ 0),
    // regardless of how thin the head is -- this is what makes a source read as a
    // gathering of springs rather than an abrupt start. The springs fan across the
    // upstream semicircle (one per bank) so they diverge and stay apart. Only when
    // the source is within feeder reach of the query box.
    if (isHeadwaterRiverTile(tile)) {
        double scx = 0.0;
        double scy = 0.0;
        float  srcHalf = 0.0f;
        eval(0.0, scx, scy, srcHalf);
        const double reach = kFeederLenMax + margin;
        if (scx + reach >= minX && scx - reach <= maxX &&
            scy + reach >= minY && scy - reach <= maxY) {
            const uint64_t hh = foundation::hashCombine(base, 0x6000u);
            const int count = kHeadwaterFeederCount;
            const double parentHalfHere = std::max(static_cast<double>(srcHalf), 1.2);
            for (int j = 0; j < count; ++j) {
                const uint64_t fh = foundation::hashCombine(hh, static_cast<uint64_t>(j + 1));
                // Spread evenly across [-fan, +fan] around straight-upstream (-u),
                // plus a little jitter, so springs point in clearly distinct directions.
                const double spread = count > 1 ? (-1.0 + 2.0 * j / (count - 1)) : 0.0;
                const double jitter = 0.2 * (2.0 * hashUnit(foundation::hashCombine(fh, 0x6)) - 1.0);
                const double theta = (spread + jitter) * kHeadwaterFanDeg * (kPi / 180.0);
                const double cosA = foundation::det_math::cos(theta);
                const double sinA = foundation::det_math::sin(theta);
                const double dux = (-ux) * cosA - (-uy) * sinA;
                const double duy = (-ux) * sinA + (-uy) * cosA;
                emitFeeder(scx, scy, parentHalfHere, dux, duy, feederLen(fh), fh);
            }
        }
    }

    // Along-channel feeders: only off channels wide enough to be worth feeding.
    if (segWidth < kFeederMinParentWidth) return;
    const double sFeedLo = std::clamp(sBoxLo - (kFeederLenMax + margin), 0.0, length);
    const double sFeedHi = std::clamp(sBoxHi + (kFeederLenMax + margin), 0.0, length);
    if (sFeedHi <= sFeedLo) return;

    const long fFirst = static_cast<long>(std::floor(sFeedLo / kFeederSpacing));
    const long fLast = static_cast<long>(std::ceil(sFeedHi / kFeederSpacing));
    for (long kf = fFirst; kf <= fLast; ++kf) {
        if (kf <= 0) continue; // s ~ 0 belongs to the headwater source, not a mid-channel feeder
        const uint64_t fh = foundation::hashCombine(base, 0x5000u + static_cast<uint64_t>(kf));
        if (hashUnit(fh) > kFeederSpawnProb) continue;

        double sc = static_cast<double>(kf) * kFeederSpacing +
                    (2.0 * hashUnit(foundation::hashCombine(fh, 0x1)) - 1.0) * 0.4 * kFeederSpacing;
        sc = std::clamp(sc, 0.0, length);

        double ccx = 0.0;
        double ccy = 0.0;
        float  parentHalf = 0.0f;
        eval(sc, ccx, ccy, parentHalf);

        // Alternate banks by index so consecutive feeders sit on opposite sides and
        // stay clear of one another, leaving the bank near-perpendicular with a
        // downstream tilt (an acute, natural confluence).
        const double side = (kf % 2 == 0) ? -1.0 : 1.0;
        const double tilt = (kFeederPerpTiltMinDeg + (kFeederPerpTiltMaxDeg - kFeederPerpTiltMinDeg) *
                                                          hashUnit(foundation::hashCombine(fh, 0x3))) *
                            (kPi / 180.0);
        const double cosT = foundation::det_math::cos(tilt);
        const double sinT = foundation::det_math::sin(tilt);
        const double dux = side * perpx * cosT + (-ux) * sinT;
        const double duy = side * perpy * cosT + (-uy) * sinT;
        emitFeeder(ccx, ccy, static_cast<double>(parentHalf), dux, duy, feederLen(fh), fh);
    }
}

void RiverNetwork2D::gatherSegments(double minX, double minY, double maxX, double maxY,
                                    std::vector<Segment>& out) const {
    std::vector<TileId> tiles;
    collectRiverTiles(minX, minY, maxX, maxY, tiles);
    for (TileId t : tiles) emitSegment(t, minX, minY, maxX, maxY, out);
}

RiverNetwork2D::PointSample RiverNetwork2D::sampleAt(double xMeters, double yMeters) const {
    const double margin = 0.5 * static_cast<double>(kMaxWidth) + 1.0;
    std::vector<Segment> segs;
    gatherSegments(xMeters - margin, yMeters - margin, xMeters + margin, yMeters + margin, segs);

    PointSample best;
    for (const Segment& s : segs) {
        double t = 0.0;
        const double d = distPointSegment(xMeters, yMeters, s.x0, s.y0, s.x1, s.y1, t);
        const float hw = s.halfWidth0 + (s.halfWidth1 - s.halfWidth0) * static_cast<float>(t);
        if (d <= static_cast<double>(hw)) {
            const float width = 2.0f * hw;
            if (!best.isRiver || width > best.widthMeters) {
                best.isRiver = true;
                best.widthMeters = width;
            }
        }
    }
    return best;
}

} // namespace worldgen
