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

// Two meander scales keep the channel from reading as a straight line at any zoom:
//
//   - A basin-scale sweep, amplitude a fraction of the (multi-km) coarse segment,
//     as a sum of half-period sines that vanish at the segment ends so the path
//     curves broadly across the whole reach. This is what bends the river when you
//     zoom out past a single chunk.
//   - A channel-scale meander whose geometry scales with width (wavelength ~10x,
//     amplitude ~3x width): a broad river snakes in big loops, a stream wiggles
//     tightly. Amplitude well above the width so even a wide river visibly bends.
//
// The channel-scale term is tapered to zero within a short arc-length of each
// coarse-tile joint (continuity); the basin sweep vanishes there on its own.
constexpr double kMeanderWavelenMult = 10.0;
constexpr double kMeanderWavelenMin  = 120.0;
constexpr double kMeanderWavelenMax  = 6000.0;
constexpr double kMeanderAmpMult     = 3.0;
constexpr double kMaxMeanderMeters   = 800.0; // channel-scale amplitude cap

constexpr double kBasinSweepFrac = 0.08;   // sweep amplitude as a fraction of segment length
constexpr double kBasinSweepCap  = 3000.0; // meters

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
constexpr double kFeederSpacing        = 220.0; // confluence spacing along the parent
constexpr double kFeederSpawnProb      = 0.55;
constexpr double kFeederLenMin         = 90.0;
constexpr double kFeederLenMax         = 480.0;
constexpr double kFeederPerpTiltMinDeg = 18.0;  // tilt from perpendicular, opening downstream
constexpr double kFeederPerpTiltMaxDeg = 42.0;
constexpr double kFeederMouthFrac      = 0.45;  // mouth half-width vs parent half-width
constexpr double kFeederMouthMaxHalf   = 3.0;
constexpr double kFeederMeanderFrac    = 0.10;
constexpr double kFeederStepMeters     = 22.0;
constexpr double kFeederBankInset      = 0.6;   // start inside the bank so the mouth meets parent water
constexpr int    kHeadwaterFeederMin   = 2;     // a source is a convergence of 2-3 trickles
constexpr int    kHeadwaterFeederMax   = 3;

// How far upstream of the query box a tile can sit and still have its downstream
// segment cross it: one tile step plus the meander, feeder, and channel headroom.
constexpr double kReachTileFactor = 1.25;

// FNV hash -> double in [0,1) using the top 53 bits (the double mantissa).
double hashUnit(uint64_t h) {
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}
double hashSigned(uint64_t base, uint64_t salt) {
    return 2.0 * hashUnit(foundation::hashCombine(base, salt)) - 1.0;
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
        kBasinSweepCap + kMaxMeanderMeters + kFeederLenMax + static_cast<double>(kMaxWidth);
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

    // Deterministic meander phases and basin-sweep coefficients for this segment.
    const uint64_t base = foundation::hashCombine(
        foundation::hashCombine(world->params.seed, tile), down);
    const double phaseA = 2.0 * kPi * hashUnit(foundation::hashCombine(base, 0x41));
    const double phaseB = 2.0 * kPi * hashUnit(foundation::hashCombine(base, 0x42));
    const double phaseW = 2.0 * kPi * hashUnit(foundation::hashCombine(base, 0x43));
    const double phaseP = 2.0 * kPi * hashUnit(foundation::hashCombine(base, 0x44));
    const double c1 = hashSigned(base, 0x11);
    const double c2 = hashSigned(base, 0x22);
    const double c3 = hashSigned(base, 0x33);

    // Channel-scale meander geometry scales with the segment's representative width.
    const double segWidth = static_cast<double>(channelWidthMeters(0.5f * (flowA + flowB)));
    const double meanderWavelen =
        std::clamp(kMeanderWavelenMult * segWidth, kMeanderWavelenMin, kMeanderWavelenMax);
    const double meanderAmp =
        std::min({kMeanderAmpMult * segWidth, 0.3 * length, kMaxMeanderMeters});
    // Taper the channel-scale meander to zero only within a very short arc-length of
    // each tile-center joint -- just enough for continuity there. The player lands at
    // a joint, so keeping this short is what lets the river wander right at the colony
    // instead of arriving as a long straight reach.
    const double meanderTaper = std::clamp(meanderWavelen * 0.08, 40.0, 120.0);
    // Basin-scale sweep amplitude (fraction of the coarse segment length).
    const double basinAmp = std::min(kBasinSweepFrac * length, kBasinSweepCap);

    // Centerline point + channel half-width at arc length s along the segment.
    auto eval = [&](double s, double& ox, double& oy, float& ohw) {
        const double t = s / length;
        const double distEnd = std::min(s, length - s);
        double e = distEnd / meanderTaper;
        e = e <= 0.0 ? 0.0 : (e >= 1.0 ? 1.0 : e * e * (3.0 - 2.0 * e)); // smoothstep
        const double env = e;
        // Basin sweep: half-period sines that vanish at the segment ends (t=0,1).
        const double sweep = c1 * foundation::det_math::sin(kPi * t) +
                             0.6 * c2 * foundation::det_math::sin(2.0 * kPi * t) +
                             0.4 * c3 * foundation::det_math::sin(3.0 * kPi * t);
        const double wander =
            0.7 * foundation::det_math::sin(2.0 * kPi * s / meanderWavelen + phaseA) +
            0.3 * foundation::det_math::sin(4.0 * kPi * s / meanderWavelen + phaseB);
        const double offset = basinAmp * sweep + env * meanderAmp * wander;
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
    // seams). A feeder leaves the bank near-perpendicular, tilted downstream so the
    // confluence opens like a real tributary, and climbs to a spring.
    auto emitFeeder = [&](double cx0, double cy0, double parentHalfHere, double side,
                          double tilt, double len, double meanderC, uint64_t fh) {
        const double cosT = foundation::det_math::cos(tilt);
        const double sinT = foundation::det_math::sin(tilt);
        // Out from the chosen bank (perp), leaning upstream (-u) so flow enters the
        // parent heading downstream -- the natural acute confluence.
        const double dux = side * perpx * cosT + (-ux) * sinT;
        const double duy = side * perpy * cosT + (-uy) * sinT;
        const double fpx = -duy;
        const double fpy = dux;
        // Anchor a little inside the bank so the mouth overlaps the parent water and
        // the feeder never reaches across to the far bank.
        const double sx = cx0 + side * perpx * (parentHalfHere * kFeederBankInset);
        const double sy = cy0 + side * perpy * (parentHalfHere * kFeederBankInset);
        const double mouthHalf = std::clamp(parentHalfHere * kFeederMouthFrac,
                                            static_cast<double>(kRenderMinHalf), kFeederMouthMaxHalf);
        const double fAmp = kFeederMeanderFrac * len;
        const double phaseFP = 2.0 * kPi * hashUnit(foundation::hashCombine(fh, 0x7));
        const int fSteps = std::clamp(static_cast<int>(std::lround(len / kFeederStepMeters)), 3, 40);

        double pfx = sx;
        double pfy = sy;
        float  pfh = static_cast<float>(mouthHalf);
        for (int i = 1; i <= fSteps; ++i) {
            const double f = static_cast<double>(i) / static_cast<double>(fSteps);
            const double along = len * f;
            const double moff = fAmp * meanderC * foundation::det_math::sin(kPi * f); // 0 at both ends
            const double fx = sx + dux * along + fpx * moff;
            const double fy = sy + duy * along + fpy * moff;
            // Taper mouth -> trickle (render floor), with the same riffle/pool play.
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

    auto feederGeometry = [&](uint64_t fh, double& tilt, double& len, double& meanderC) {
        tilt = (kFeederPerpTiltMinDeg + (kFeederPerpTiltMaxDeg - kFeederPerpTiltMinDeg) *
                                            hashUnit(foundation::hashCombine(fh, 0x3))) *
               (kPi / 180.0);
        len = kFeederLenMin +
              (kFeederLenMax - kFeederLenMin) * hashUnit(foundation::hashCombine(fh, 0x4));
        meanderC = 2.0 * hashUnit(foundation::hashCombine(fh, 0x5)) - 1.0;
    };

    // Headwater source: 2-3 trickles converge at the river's birth (s ~ 0),
    // regardless of how thin the head is -- this is what makes a source read as a
    // gathering of springs rather than an abrupt start. Only when the source is
    // within feeder reach of the query box.
    if (isHeadwaterRiverTile(tile)) {
        double scx = 0.0;
        double scy = 0.0;
        float  srcHalf = 0.0f;
        eval(0.0, scx, scy, srcHalf);
        const double reach = kFeederLenMax + margin;
        if (scx + reach >= minX && scx - reach <= maxX &&
            scy + reach >= minY && scy - reach <= maxY) {
            const uint64_t hh = foundation::hashCombine(base, 0x6000u);
            const int count = kHeadwaterFeederMin +
                              static_cast<int>(hashUnit(foundation::hashCombine(hh, 0x0)) *
                                               (kHeadwaterFeederMax - kHeadwaterFeederMin + 1));
            for (int j = 0; j < count; ++j) {
                const uint64_t fh = foundation::hashCombine(hh, static_cast<uint64_t>(j + 1));
                const double side = (j % 2 == 0) ? -1.0 : 1.0; // fan to alternating banks
                double tilt = 0.0;
                double len = 0.0;
                double meanderC = 0.0;
                feederGeometry(fh, tilt, len, meanderC);
                // At a head the parent is barely a trickle; give the source streams a
                // small but real mouth so they read as converging tributaries.
                const double parentHalfHere = std::max(static_cast<double>(srcHalf), 1.2);
                emitFeeder(scx, scy, parentHalfHere, side, tilt, len, meanderC, fh);
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

        const double side = hashUnit(foundation::hashCombine(fh, 0x2)) < 0.5 ? -1.0 : 1.0;
        double tilt = 0.0;
        double len = 0.0;
        double meanderC = 0.0;
        feederGeometry(fh, tilt, len, meanderC);
        emitFeeder(ccx, ccy, static_cast<double>(parentHalf), side, tilt, len, meanderC, fh);
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
