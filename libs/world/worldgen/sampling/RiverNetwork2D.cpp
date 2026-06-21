#include "worldgen/sampling/RiverNetwork2D.h"

#include "worldgen/data/WorldData.h"
#include "worldgen/grid/SphereGrid.h"

#include <math/DeterministicMath.h>
#include <utils/WorldHash.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>  // std::floor, std::round on integer-valued doubles only
#include <unordered_set>

namespace worldgen {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Meander: lateral wander is a fraction of segment length, capped so it stays in
// the valley. The displacement is a sum of half-period sines, each of which
// vanishes at t=0 and t=1, so consecutive segments meet exactly at shared tile
// centers — the channel is continuous across chunk seams with no stitching.
constexpr double kMeanderFrac      = 0.18;
constexpr double kMaxMeanderMeters = 600.0;

// Polyline density: aim for ~150 m straight pieces, bounded so tiny and huge
// tiles both stay sane.
constexpr double kTargetSubLen = 150.0;
constexpr int    kMinSub       = 4;
constexpr int    kMaxSub       = 32;

// Hydraulic geometry: channel width ~ sqrt(discharge), clamped to a realistic
// band. flowAccum is rain-seeded upstream drainage (precip/1000 per tile).
constexpr float kWidthCoef = 2.0f;
constexpr float kMinWidth  = 3.0f;
constexpr float kMaxWidth  = 140.0f;

// How far upstream of the query box a tile can sit and still have its downstream
// segment cross it: one tile step plus the meander and channel headroom.
constexpr double kReachTileFactor = 1.25;

// FNV hash -> double in [0,1) using the top 53 bits (the double mantissa).
double hashUnit(uint64_t h) {
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}
double hashSigned(uint64_t base, uint64_t salt) {
    return 2.0 * hashUnit(foundation::hashCombine(base, salt)) - 1.0;
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

    const double reach =
        grid.tileWidthMeters(seed, world->derived.planetRadiusMeters) * kReachTileFactor +
        kMaxMeanderMeters + static_cast<double>(kMaxWidth);
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

    const double perpx = -dy / length;
    const double perpy = dx / length;

    const float flowA = world->data.flowAccum[tile];
    // Do not taper to zero at the mouth: ocean tiles carry no flow.
    const float flowB = isOceanTile(down) ? flowA : world->data.flowAccum[down];
    const float hwA = 0.5f * channelWidthMeters(flowA);
    const float hwB = 0.5f * channelWidthMeters(flowB);

    const uint64_t base = foundation::hashCombine(
        foundation::hashCombine(world->params.seed, tile), down);
    const double c1 = hashSigned(base, 0x11);
    const double c2 = hashSigned(base, 0x22);
    const double c3 = hashSigned(base, 0x33);
    const double amp = std::min(kMeanderFrac * length, kMaxMeanderMeters);

    int sub = static_cast<int>(std::lround(length / kTargetSubLen));
    sub = std::clamp(sub, kMinSub, kMaxSub);

    double prevX = pa.x;
    double prevY = pa.y;
    float  prevHW = hwA;
    for (int k = 1; k <= sub; ++k) {
        const double t = static_cast<double>(k) / static_cast<double>(sub);
        const double off = amp * (c1 * foundation::det_math::sin(kPi * t) +
                                  c2 * foundation::det_math::sin(2.0 * kPi * t) +
                                  c3 * foundation::det_math::sin(3.0 * kPi * t));
        const double tx = pa.x + dx * t + perpx * off;
        const double ty = pa.y + dy * t + perpy * off;
        const float  hwHere = hwA + (hwB - hwA) * static_cast<float>(t);

        // Cull sub-segments whose footprint cannot touch the query box.
        const double pad = static_cast<double>(std::max(prevHW, hwHere));
        const double sMinX = std::min(prevX, tx) - pad;
        const double sMaxX = std::max(prevX, tx) + pad;
        const double sMinY = std::min(prevY, ty) - pad;
        const double sMaxY = std::max(prevY, ty) + pad;
        if (sMaxX >= minX && sMinX <= maxX && sMaxY >= minY && sMinY <= maxY) {
            out.push_back({prevX, prevY, tx, ty, prevHW, hwHere});
        }

        prevX = tx;
        prevY = ty;
        prevHW = hwHere;
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
