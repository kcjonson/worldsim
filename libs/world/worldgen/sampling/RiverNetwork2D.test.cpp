// RiverNetwork2D tests — 2D channel synthesis from the coarse drainage graph.
//
// Worlds are built synthetically: an all-land disk around (0,0) with a small
// ocean "mouth", and a river chain walked greedily tile-to-tile toward the
// mouth (downhill + kFlagRiver + increasing flowAccum set by hand). The full
// PlanetGenerator pipeline is never run here.

#include "worldgen/sampling/RiverNetwork2D.h"

#include "worldgen/data/PlanetParams.h"
#include "worldgen/data/WorldData.h"
#include "worldgen/sampling/SphericalProjection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <unordered_set>
#include <vector>

namespace worldgen {

namespace {

constexpr uint32_t kSubdivision = 24;  // 5762 tiles, ~300 km tile width
constexpr float kLand = 500.0f;        // meters above sea level
constexpr float kOcean = -3000.0f;

double dot(const Vec3d& a, const Vec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

std::shared_ptr<GeneratedWorld> makeLandWorld() {
    auto world = std::make_shared<GeneratedWorld>();
    world->params.gridSubdivision = kSubdivision;
    world->params.seed = 0x5151ABCDULL;
    world->derived = derive(world->params);
    world->grid = std::make_shared<SphereGrid>(kSubdivision);
    world->data.allocate(world->grid->tileCount());
    world->seaLevelMeters = 0.0f;
    world->validFields = static_cast<uint32_t>(WorldField::Elevation) |
                         static_cast<uint32_t>(WorldField::Flags) |
                         static_cast<uint32_t>(WorldField::FlowAccum) |
                         static_cast<uint32_t>(WorldField::Downhill);
    for (TileId t = 0; t < world->grid->tileCount(); ++t) {
        world->data.elevation[t] = kLand;
        world->data.biome[t] = static_cast<uint8_t>(Biome::TemperateGrassland);
    }
    return world;
}

// Greedily walk neighbor-to-neighbor from source toward mouth, flagging each step
// as a river with increasing flow. Returns the ordered chain of river tiles
// (mouth excluded). The mouth tile is set to ocean.
std::vector<TileId> buildRiver(GeneratedWorld& world, double srcLat, double srcLon,
                               double mouthLat, double mouthLon,
                               float startFlow = 6.0f, float flowStep = 4.0f) {
    const SphereGrid& grid = *world.grid;
    const TileId mouth = grid.fromLatLon(mouthLat, mouthLon);
    world.data.elevation[mouth] = kOcean;
    world.data.flags[mouth] = kFlagOcean;
    world.data.biome[mouth] = static_cast<uint8_t>(Biome::Ocean);

    const Vec3d target = grid.tileCenter(mouth);
    std::vector<TileId> chain;
    std::unordered_set<TileId> visited;

    TileId cur = grid.fromLatLon(srcLat, srcLon);
    float flow = startFlow;  // >= ~7.4 makes a channel wide enough (>3 m) to grow feeders
    for (int guard = 0; guard < 10000; ++guard) {
        if (cur == mouth || !visited.insert(cur).second) break;

        std::array<TileId, 6> nbrs{};
        const uint32_t count = grid.neighbors(cur, nbrs);

        // Closest neighbor to the mouth (max dot with the target direction).
        int bestIdx = -1;
        double bestDot = -2.0;
        for (uint32_t i = 0; i < count; ++i) {
            const double d = dot(grid.tileCenter(nbrs[i]), target);
            if (d > bestDot) { bestDot = d; bestIdx = static_cast<int>(i); }
        }
        if (bestIdx < 0) break;

        world.data.flags[cur] |= kFlagRiver;
        world.data.flowAccum[cur] = flow;
        world.data.downhill[cur] = static_cast<uint8_t>(bestIdx);
        chain.push_back(cur);
        flow += flowStep;

        cur = nbrs[bestIdx];
    }
    return chain;
}

WorldPos2d tilePos(const GeneratedWorld& world, const SphericalProjection& proj, TileId t) {
    double lat = 0.0;
    double lon = 0.0;
    world.grid->latLonOf(t, lat, lon);
    return proj.latLonToWorld({lat, lon});
}

} // namespace

TEST(RiverNetwork2DWidth, MonotonicAndClamped) {
    EXPECT_FLOAT_EQ(RiverNetwork2D::channelWidthMeters(0.0f), 0.6f);   // clamped to min (trickle)
    EXPECT_GE(RiverNetwork2D::channelWidthMeters(10.0f), 0.6f);
    EXPECT_FLOAT_EQ(RiverNetwork2D::channelWidthMeters(1e9f), 110.0f); // clamped to max (kMaxWidth)
    // Strictly increasing in the unclamped band.
    EXPECT_LT(RiverNetwork2D::channelWidthMeters(10.0f),
              RiverNetwork2D::channelWidthMeters(100.0f));
    EXPECT_LT(RiverNetwork2D::channelWidthMeters(100.0f),
              RiverNetwork2D::channelWidthMeters(1000.0f));
}

TEST(RiverNetwork2D, RiverTileCentersSampleAsRiver) {
    auto world = makeLandWorld();
    auto chain = buildRiver(*world, 0.0, -30.0, 0.0, 30.0);
    ASSERT_GE(chain.size(), 5u);

    RiverNetwork2D net(world, 0.0, 0.0);
    SphericalProjection proj(world->derived.planetRadiusMeters, 0.0, 0.0);

    for (TileId t : chain) {
        WorldPos2d p = tilePos(*world, proj, t);
        auto s = net.sampleAt(p.x, p.y);
        EXPECT_TRUE(s.isRiver) << "tile " << t << " center should be inside its channel";
        EXPECT_GT(s.widthMeters, 0.0f);
    }
}

TEST(RiverNetwork2D, WidthGrowsDownstream) {
    auto world = makeLandWorld();
    auto chain = buildRiver(*world, 0.0, -30.0, 0.0, 30.0);
    ASSERT_GE(chain.size(), 6u);

    RiverNetwork2D net(world, 0.0, 0.0);
    SphericalProjection proj(world->derived.planetRadiusMeters, 0.0, 0.0);

    // Sampled channel width at the headwater vs near the mouth (flow increases
    // monotonically along the chain).
    WorldPos2d head = tilePos(*world, proj, chain.front());
    WorldPos2d foot = tilePos(*world, proj, chain.back());
    float headW = net.sampleAt(head.x, head.y).widthMeters;
    float footW = net.sampleAt(foot.x, foot.y).widthMeters;
    EXPECT_GT(footW, headW);
}

TEST(RiverNetwork2D, ChannelIsContinuousAcrossASplitBox) {
    // A river point sampled from a tiny box (a chunk) must still be a river: the
    // channel does not vanish when only a local window is gathered. Sampling on
    // both sides of an arbitrary boundary line proves cross-chunk continuity.
    auto world = makeLandWorld();
    auto chain = buildRiver(*world, 0.0, -30.0, 0.0, 30.0);
    ASSERT_GE(chain.size(), 5u);

    RiverNetwork2D net(world, 0.0, 0.0);
    SphericalProjection proj(world->derived.planetRadiusMeters, 0.0, 0.0);

    TileId mid = chain[chain.size() / 2];
    WorldPos2d p = tilePos(*world, proj, mid);
    // The center is covered; so are points a meter to either side along x.
    EXPECT_TRUE(net.sampleAt(p.x, p.y).isRiver);
    EXPECT_TRUE(net.sampleAt(p.x - 1.0, p.y).isRiver);
    EXPECT_TRUE(net.sampleAt(p.x + 1.0, p.y).isRiver);
}

TEST(RiverNetwork2D, DryGroundFarFromRiverIsNotRiver) {
    auto world = makeLandWorld();
    auto chain = buildRiver(*world, 0.0, -30.0, 0.0, 30.0);
    ASSERT_GE(chain.size(), 5u);

    RiverNetwork2D net(world, 0.0, 0.0);
    SphericalProjection proj(world->derived.planetRadiusMeters, 0.0, 0.0);

    // A point ~100 km off the equatorial chain, well outside any channel.
    WorldPos2d p = tilePos(*world, proj, chain[chain.size() / 2]);
    auto s = net.sampleAt(p.x, p.y + 100000.0);
    EXPECT_FALSE(s.isRiver);
}

// The headline seamlessness invariant: geometry is identical where two different
// query boxes overlap, so adjacent chunks render a continuous river. Every segment
// lying fully inside the overlap of two offset boxes must appear bit-for-bit in
// both gathers (box-culling may only DROP segments, never alter their geometry).
TEST(RiverNetwork2D, OverlappingBoxesAgreeOnGeometry) {
    auto world = makeLandWorld();
    auto chain = buildRiver(*world, 0.0, -30.0, 0.0, 30.0, 400.0f, 0.0f); // wide -> feeders too
    ASSERT_GE(chain.size(), 5u);
    RiverNetwork2D net(world, 0.0, 0.0);
    SphericalProjection proj(world->derived.planetRadiusMeters, 0.0, 0.0);
    const WorldPos2d c = tilePos(*world, proj, chain[chain.size() / 2]);

    std::vector<RiverNetwork2D::Segment> a;
    std::vector<RiverNetwork2D::Segment> b;
    net.gatherSegments(c.x - 3000.0, c.y - 3000.0, c.x + 1000.0, c.y + 1000.0, a);
    net.gatherSegments(c.x - 1000.0, c.y - 1000.0, c.x + 3000.0, c.y + 3000.0, b);
    ASSERT_FALSE(a.empty());
    ASSERT_FALSE(b.empty());

    // Region common to both boxes.
    const double oMinX = c.x - 1000.0;
    const double oMinY = c.y - 1000.0;
    const double oMaxX = c.x + 1000.0;
    const double oMaxY = c.y + 1000.0;
    auto fullyInsideOverlap = [&](const RiverNetwork2D::Segment& s) {
        return std::min(s.x0, s.x1) >= oMinX && std::max(s.x0, s.x1) <= oMaxX &&
               std::min(s.y0, s.y1) >= oMinY && std::max(s.y0, s.y1) <= oMaxY;
    };
    auto sameSeg = [](const RiverNetwork2D::Segment& p, const RiverNetwork2D::Segment& q) {
        return p.x0 == q.x0 && p.y0 == q.y0 && p.x1 == q.x1 && p.y1 == q.y1 &&
               p.halfWidth0 == q.halfWidth0 && p.halfWidth1 == q.halfWidth1;
    };

    int matched = 0;
    for (const auto& s : a) {
        if (!fullyInsideOverlap(s)) continue;
        bool found = false;
        for (const auto& t : b) {
            if (sameSeg(s, t)) { found = true; break; }
        }
        EXPECT_TRUE(found) << "a segment inside the overlap is missing or differs in the other box's gather";
        if (found) ++matched;
    }
    EXPECT_GT(matched, 0) << "expected shared channel/feeder geometry in the overlap region";
}

TEST(RiverNetwork2D, GatherIsDeterministic) {
    auto worldA = makeLandWorld();
    auto chainA = buildRiver(*worldA, 0.0, -30.0, 0.0, 30.0);
    auto worldB = makeLandWorld();
    auto chainB = buildRiver(*worldB, 0.0, -30.0, 0.0, 30.0);
    ASSERT_EQ(chainA.size(), chainB.size());

    RiverNetwork2D netA(worldA, 12.5, -40.0);
    RiverNetwork2D netB(worldB, 12.5, -40.0);

    std::vector<RiverNetwork2D::Segment> a;
    std::vector<RiverNetwork2D::Segment> b;
    netA.gatherSegments(-2.0e6, -2.0e6, 2.0e6, 2.0e6, a);
    netB.gatherSegments(-2.0e6, -2.0e6, 2.0e6, 2.0e6, b);

    ASSERT_FALSE(a.empty());
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].x0, b[i].x0);
        EXPECT_EQ(a[i].y0, b[i].y0);
        EXPECT_EQ(a[i].x1, b[i].x1);
        EXPECT_EQ(a[i].y1, b[i].y1);
        EXPECT_EQ(a[i].halfWidth0, b[i].halfWidth0);
        EXPECT_EQ(a[i].halfWidth1, b[i].halfWidth1);
    }
}

// A wide river sprouts procedural feeder streams; a thin one (below the feeder
// width gate) does not, so the wide gather has many more segments.
TEST(RiverNetwork2D, WideRiverGrowsFeeders) {
    auto wide = makeLandWorld();
    auto wchain = buildRiver(*wide, 0.0, -30.0, 0.0, 30.0, 400.0f, 0.0f);
    auto thin = makeLandWorld();
    auto tchain = buildRiver(*thin, 0.0, -30.0, 0.0, 30.0, 6.0f, 0.0f);
    ASSERT_GE(wchain.size(), 5u);
    ASSERT_EQ(wchain.size(), tchain.size());

    RiverNetwork2D wnet(wide, 0.0, 0.0);
    RiverNetwork2D tnet(thin, 0.0, 0.0);
    SphericalProjection proj(wide->derived.planetRadiusMeters, 0.0, 0.0);
    const WorldPos2d c = tilePos(*wide, proj, wchain[wchain.size() / 2]);
    const double r = 2000.0;

    std::vector<RiverNetwork2D::Segment> ws;
    std::vector<RiverNetwork2D::Segment> ts;
    wnet.gatherSegments(c.x - r, c.y - r, c.x + r, c.y + r, ws);
    tnet.gatherSegments(c.x - r, c.y - r, c.x + r, c.y + r, ts);

    EXPECT_GT(ts.size(), 0u) << "the river channel itself should render";
    EXPECT_GT(ws.size(), ts.size() + 50)
        << "a wide river should sprout many feeder segments a thin one does not";
}

// Feeders taper from the parent width at the confluence to a thin trickle, but
// never below the render contiguity floor (so thin water is not broken on the
// 1 m tile grid). The wide trunk stays wide.
TEST(RiverNetwork2D, FeedersTaperToTrickle) {
    auto world = makeLandWorld();
    auto chain = buildRiver(*world, 0.0, -30.0, 0.0, 30.0, 400.0f, 0.0f);
    ASSERT_GE(chain.size(), 5u);
    RiverNetwork2D net(world, 0.0, 0.0);
    SphericalProjection proj(world->derived.planetRadiusMeters, 0.0, 0.0);
    const WorldPos2d c = tilePos(*world, proj, chain[chain.size() / 2]);
    const double r = 2000.0;

    std::vector<RiverNetwork2D::Segment> segs;
    net.gatherSegments(c.x - r, c.y - r, c.x + r, c.y + r, segs);
    ASSERT_FALSE(segs.empty());

    float minHalf = 1e9f;
    float maxHalf = 0.0f;
    for (const auto& s : segs) {
        minHalf = std::min({minHalf, s.halfWidth0, s.halfWidth1});
        maxHalf = std::max({maxHalf, s.halfWidth0, s.halfWidth1});
    }
    EXPECT_GE(minHalf, 0.75f) << "no emitted channel may drop below the contiguity floor (~0.8 m half)";
    EXPECT_LE(minHalf, 1.2f) << "feeder spring ends should still taper to a thin trickle";
    EXPECT_GE(maxHalf, 5.0f) << "the wide trunk should remain wide";
}

// A river's headwater is a convergence of springs: even a thin head (below the
// along-channel feeder gate) sprouts streams that run into the upstream hemisphere
// and fan to both banks. The channel flows *downstream* from the source, so any
// water upstream of it can only be a spring -- a clean discriminator independent
// of the channel's own meander.
TEST(RiverNetwork2D, HeadwaterSproutsConvergingSprings) {
    auto world = makeLandWorld();
    auto chain = buildRiver(*world, 0.0, -30.0, 0.0, 30.0); // default thin flow
    ASSERT_GE(chain.size(), 5u);
    RiverNetwork2D net(world, 0.0, 0.0);
    SphericalProjection proj(world->derived.planetRadiusMeters, 0.0, 0.0);

    const WorldPos2d src = tilePos(*world, proj, chain.front());
    const WorldPos2d nxt = tilePos(*world, proj, chain[1]);
    double ux = nxt.x - src.x;
    double uy = nxt.y - src.y;
    const double ulen = std::sqrt(ux * ux + uy * uy);
    ASSERT_GT(ulen, 0.0);
    ux /= ulen;
    uy /= ulen;
    const double perpx = -uy;
    const double perpy = ux;

    const double r = 1500.0;
    std::vector<RiverNetwork2D::Segment> segs;
    net.gatherSegments(src.x - r, src.y - r, src.x + r, src.y + r, segs);
    ASSERT_FALSE(segs.empty());

    bool leftBank = false;
    bool rightBank = false;
    for (const auto& s : segs) {
        const double xs[2] = {s.x0, s.x1};
        const double ys[2] = {s.y0, s.y1};
        for (int i = 0; i < 2; ++i) {
            const double along = (xs[i] - src.x) * (-ux) + (ys[i] - src.y) * (-uy);
            const double lateral = (xs[i] - src.x) * perpx + (ys[i] - src.y) * perpy;
            if (along > 8.0) { // upstream of the source: only a spring can be here
                if (lateral > 12.0) leftBank = true;
                if (lateral < -12.0) rightBank = true;
            }
        }
    }
    EXPECT_TRUE(leftBank && rightBank)
        << "a headwater should be fed by springs from both banks, upstream of the source";
}

// The local per-point query (which gathers only a small box, as a chunk does)
// must agree with a single large gather, proving feeders are not dropped by
// chunk-local gathering -- i.e. they are continuous across chunk seams.
TEST(RiverNetwork2D, FeederGatherIsLocallyConsistent) {
    auto world = makeLandWorld();
    auto chain = buildRiver(*world, 0.0, -30.0, 0.0, 30.0, 400.0f, 0.0f);
    ASSERT_GE(chain.size(), 5u);
    RiverNetwork2D net(world, 0.0, 0.0);
    SphericalProjection proj(world->derived.planetRadiusMeters, 0.0, 0.0);
    const WorldPos2d c = tilePos(*world, proj, chain[chain.size() / 2]);

    std::vector<RiverNetwork2D::Segment> big;
    net.gatherSegments(c.x - 3000.0, c.y - 3000.0, c.x + 3000.0, c.y + 3000.0, big);
    auto coveredByBig = [&](double x, double y) {
        for (const auto& s : big) {
            const double dx = s.x1 - s.x0;
            const double dy = s.y1 - s.y0;
            const double len2 = dx * dx + dy * dy;
            double t = 0.0;
            if (len2 > 0.0) t = std::clamp(((x - s.x0) * dx + (y - s.y0) * dy) / len2, 0.0, 1.0);
            const double ex = x - (s.x0 + dx * t);
            const double ey = y - (s.y0 + dy * t);
            const float hw = s.halfWidth0 + (s.halfWidth1 - s.halfWidth0) * static_cast<float>(t);
            if (ex * ex + ey * ey <= static_cast<double>(hw) * static_cast<double>(hw)) return true;
        }
        return false;
    };

    for (double dy = -500.0; dy <= 500.0; dy += 60.0) {
        for (double dx = -500.0; dx <= 500.0; dx += 60.0) {
            const double x = c.x + dx;
            const double y = c.y + dy;
            EXPECT_EQ(net.sampleAt(x, y).isRiver, coveredByBig(x, y))
                << "mismatch at offset (" << dx << ", " << dy << ")";
        }
    }
}

} // namespace worldgen
