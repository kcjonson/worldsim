// TerrainPolygonBuilder channel and pond rings (terrain-polygons-architecture.md
// D7, D8, D14, section 5): hand-built segment lists and ponds, each chunk given
// the segments its own gather would produce, so the seam rule is tested the way
// the game runs it. Then a sweep over real RiverNetwork2D geometry.

#include "world/chunk/TerrainPolygonBuilder.h"

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/ChunkSampleResult.h"
#include "world/chunk/GeneratedWorldSampler.h"
#include "world/chunk/RiverTestWorld.h"
#include "world/chunk/TerrainPolygonTestSupport.h"

#include <contour/ClipRing.h>
#include <polygon/Polygon.h>
#include <predicates/Predicates.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace engine::world;
using namespace engine::world::terrain_test;

namespace {

	using Segment = TerrainPolygonBuilder::RiverSegment;
	using Pond	  = TerrainPolygonBuilder::Pond;

	constexpr uint64_t kWorldSeed = 0xC4A77E15ULL;
	constexpr double   kTwoPi	  = 2.0 * std::numbers::pi;

	struct RiverPoint {
		double x;
		double y;
		float  hw;
	};

	// Consecutive points (upstream first) cut into segments point to point.
	std::vector<Segment> segmentsOf(const std::vector<RiverPoint>& pts) {
		std::vector<Segment> out;
		for (size_t i = 0; i + 1 < pts.size(); ++i) {
			out.push_back({pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, pts[i].hw, pts[i + 1].hw});
		}
		return out;
	}

	// A river along y = y(x), x from x0 to x1 at stepM, half-width hw(x).
	std::vector<Segment> riverAlong(
		double x0, double x1, double stepM, const std::function<double(double)>& y, const std::function<double(double)>& hw
	) {
		std::vector<RiverPoint> pts;
		const auto				steps = static_cast<int>(std::lround((x1 - x0) / stepM));
		for (int i = 0; i <= steps; ++i) {
			const double x = x0 + (x1 - x0) * static_cast<double>(i) / static_cast<double>(steps);
			pts.push_back({x, y(x), static_cast<float>(hw(x))});
		}
		return segmentsOf(pts);
	}

	// What GeneratedWorldSampler hands the chunk: segments whose hw-padded bounds
	// touch the chunk square grown by kRiverGatherMarginM (RiverNetwork2D's
	// per-segment cull), and ponds whose rim bounds do.
	struct Gathered {
		std::vector<Segment> segments;
		std::vector<Pond>	 ponds;
	};

	Gathered gatherFor(ChunkCoordinate coord, const std::vector<Segment>& segments, const std::vector<Pond>& ponds) {
		const double minX = static_cast<double>(coord.x) * static_cast<double>(kChunkSize) - kRiverGatherMarginM;
		const double minY = static_cast<double>(coord.y) * static_cast<double>(kChunkSize) - kRiverGatherMarginM;
		const double maxX = minX + static_cast<double>(kChunkSize) + 2.0 * kRiverGatherMarginM;
		const double maxY = minY + static_cast<double>(kChunkSize) + 2.0 * kRiverGatherMarginM;
		Gathered	 out;
		for (const Segment& s : segments) {
			const double pad = static_cast<double>(std::max(s.halfWidth0, s.halfWidth1));
			if (std::max(s.x0, s.x1) + pad >= minX && std::min(s.x0, s.x1) - pad <= maxX && std::max(s.y0, s.y1) + pad >= minY &&
				std::min(s.y0, s.y1) - pad <= maxY) {
				out.segments.push_back(s);
			}
		}
		for (const Pond& p : ponds) {
			const double r = static_cast<double>(p.radius) * 1.5;
			if (p.cx + r >= minX && p.cx - r <= maxX && p.cy + r >= minY && p.cy - r <= maxY) {
				out.ponds.push_back(p);
			}
		}
		return out;
	}

	ChunkTerrainPolygons buildChunk(
		ChunkCoordinate coord, const HandTiles& tiles, const std::vector<Segment>& segments, const std::vector<Pond>& ponds = {}
	) {
		const Gathered g = gatherFor(coord, segments, ponds);
		return TerrainPolygonBuilder::build(coord, kWorldSeed, tiles.fn(), g.segments, g.ponds);
	}

	std::vector<const TerrainRing*> ringsOfKind(const ChunkTerrainPolygons& polys, TerrainRingKind kind) {
		std::vector<const TerrainRing*> out;
		for (const TerrainRing& r : polys.rings) {
			if (r.kind == kind) {
				out.push_back(&r);
			}
		}
		return out;
	}

	// Where the ring's edges cross the line x = xMm (vertical) or y = xMm, sorted.
	std::vector<double> crossings(const Ring& ring, bool vertical, double lineMm) {
		std::vector<double> out;
		for (size_t i = 0; i < ring.size(); ++i) {
			const Vec2i64& a  = ring[i];
			const Vec2i64& b  = ring[(i + 1) % ring.size()];
			const double   a0 = static_cast<double>(vertical ? a.x : a.y);
			const double   b0 = static_cast<double>(vertical ? b.x : b.y);
			if ((a0 < lineMm) == (b0 < lineMm)) {
				continue;
			}
			const double t	= (lineMm - a0) / (b0 - a0);
			const double a1 = static_cast<double>(vertical ? a.y : a.x);
			const double b1 = static_cast<double>(vertical ? b.y : b.x);
			out.push_back(a1 + (b1 - a1) * t);
		}
		std::sort(out.begin(), out.end());
		return out;
	}

	// Cross-channel extent (mm) of a roughly west-east ribbon at x = xM.
	double extentAt(const Ring& ring, double xM) {
		const std::vector<double> c = crossings(ring, true, xM * 1000.0);
		return c.size() < 2 ? 0.0 : c.back() - c.front();
	}

	int64_t maxX(const Ring& ring) {
		int64_t m = std::numeric_limits<int64_t>::min();
		for (const Vec2i64& v : ring) {
			m = std::max(m, v.x);
		}
		return m;
	}

	double distanceToSegmentMm(const Vec2i64& p, const Vec2i64& a, const Vec2i64& b) {
		const double abx = static_cast<double>(b.x - a.x);
		const double aby = static_cast<double>(b.y - a.y);
		const double apx = static_cast<double>(p.x - a.x);
		const double apy = static_cast<double>(p.y - a.y);
		const double len = abx * abx + aby * aby;
		const double t	 = len > 0.0 ? std::clamp((apx * abx + apy * aby) / len, 0.0, 1.0) : 0.0;
		return std::hypot(apx - abx * t, apy - aby * t);
	}

	double distanceToRingMm(const Ring& ring, const Vec2i64& p) {
		double best = std::numeric_limits<double>::max();
		for (size_t i = 0; i < ring.size(); ++i) {
			best = std::min(best, distanceToSegmentMm(p, ring[i], ring[(i + 1) % ring.size()]));
		}
		return best;
	}

	std::vector<Vec2i64> flaggedVertices(const TerrainRing& ring, uint8_t flag) {
		std::vector<Vec2i64> out;
		for (size_t i = 0; i < ring.ring.size(); ++i) {
			if ((ring.profiles[i].flags & flag) != 0) {
				out.push_back(ring.ring[i]);
			}
		}
		std::sort(out.begin(), out.end());
		return out;
	}

	// ---- The two-chunk world the seam and determinism tests share ----
	//
	// Chunks (0,0) and (1,0), border x = 512 m. A lake 20 m east of the border.
	// River A meanders west to east across the border and ends in the lake (its
	// mouth and flare lie in chunk 1, near the border but not across it). Rivers
	// B and C taper through the fordable width 6 m and 14 m west of the border,
	// so the cut is inside both extended regions (B) or only chunk 0's (C).

	Biome seamWorldBiome(int64_t tx, int64_t ty) {
		return (tx >= 532 && tx < 600 && ty >= 200 && ty < 320) ? Biome::Lake : Biome::TemperateGrassland;
	}

	std::vector<Segment> seamWorldRivers() {
		std::vector<Segment> all = riverAlong(
			-300.0,
			545.0,
			15.0,
			[](double x) { return 256.0 + 25.0 * std::sin(kTwoPi * x / 140.0); },
			[](double x) { return 3.0 + 0.5 * std::sin(kTwoPi * x / 90.0); }
		);
		auto taper = [](double atX) {
			return [atX](double x) { return std::clamp(1.0 - 0.4 * (x - 300.0) / (atX - 300.0), 0.3, 1.0); };
		};
		for (const Segment& s : riverAlong(
				 250.0, 800.0, 15.0, [](double x) { return 420.0 + 8.0 * std::sin(kTwoPi * x / 60.0); }, taper(506.0)
			 )) {
			all.push_back(s);
		}
		for (const Segment& s : riverAlong(
				 250.0, 800.0, 15.0, [](double x) { return 100.0 + 6.0 * std::sin(kTwoPi * x / 70.0); }, taper(498.0)
			 )) {
			all.push_back(s);
		}
		return all;
	}

	// Border vertices on x = line with each one's ring blocking flag, sorted.
	std::vector<std::pair<Vec2i64, bool>> borderVerticesWithFlags(const ChunkTerrainPolygons& polys, int64_t line) {
		std::vector<std::pair<Vec2i64, bool>> out;
		for (const TerrainRing& ring : polys.navRings) {
			for (const Vec2i64& v : ring.ring) {
				if (v.x == line) {
					out.emplace_back(v, ring.blocksMovement);
				}
			}
		}
		std::sort(out.begin(), out.end());
		return out;
	}

} // namespace

// ============================================================================
// Seams (D4, section 5)
// ============================================================================

TEST(TerrainPolygonChannelsTest, ChannelsSeamAcrossABorder) {
	const std::vector<Segment> rivers = seamWorldRivers();
	const ChunkCoordinate	   westCoord{0, 0};
	const ChunkCoordinate	   eastCoord{1, 0};
	const ChunkTerrainPolygons west = buildChunk(westCoord, HandTiles(westCoord, seamWorldBiome), rivers);
	const ChunkTerrainPolygons east = buildChunk(eastCoord, HandTiles(eastCoord, seamWorldBiome), rivers);
	expectAllSimple(west, "west");
	expectAllSimple(east, "east");

	const SeamSide a = seamSide(westCoord, west);
	const SeamSide b = seamSide(eastCoord, east);
	expectBorderVerticesMatch(a, b, true);
	// Three channels cross the border, each at two bank vertices; B and C cross
	// as fordable pieces in both chunks.
	const std::vector<std::pair<Vec2i64, bool>> westBorder = borderVerticesWithFlags(west, kChunkMm);
	EXPECT_EQ(westBorder, borderVerticesWithFlags(east, kChunkMm));
	EXPECT_EQ(std::count_if(westBorder.begin(), westBorder.end(), [](const auto& v) { return !v.second; }), 4);

	const geometry::nav::NavMesh mesh = buildSeamMesh({a, b});
	ASSERT_FALSE(mesh.triangles.empty());
	expectSeamClassificationAgrees(mesh, a, b, true);

	// River B's cut lies in both extended regions: both chunks flag the same two
	// cut vertices on both of its pieces.
	auto cutsNear = [](const ChunkTerrainPolygons& polys, double xM) {
		std::vector<Vec2i64> out;
		for (const TerrainRing& ring : polys.rings) {
			for (const Vec2i64& v : flaggedVertices(ring, ShoreProfile::kFlagFordableCut)) {
				if (std::abs(static_cast<double>(v.x) - xM * 1000.0) < 1500.0) {
					out.push_back(v);
				}
			}
		}
		std::sort(out.begin(), out.end());
		return out;
	};
	const std::vector<Vec2i64> westCuts = cutsNear(west, 506.0);
	EXPECT_EQ(westCuts.size(), 4U); // two vertices, on each of two pieces
	EXPECT_EQ(westCuts, cutsNear(east, 506.0));

	// River A's mouth: chunk 1 flares it into the lake and the ribbon overlaps the
	// lake ring; chunk 0 never sees the lake.
	const std::vector<const TerrainRing*> lakes = ringsOfKind(east, TerrainRingKind::Waterline);
	ASSERT_EQ(lakes.size(), 1U);
	bool overlaps = false;
	for (const TerrainRing* channel : ringsOfKind(east, TerrainRingKind::Channel)) {
		for (const Vec2i64& v : channel->ring) {
			overlaps = overlaps || geometry::pointInPolygon(v, lakes[0]->ring) == geometry::PointInPolygon::Inside;
		}
	}
	EXPECT_TRUE(overlaps);
	EXPECT_TRUE(ringsOfKind(west, TerrainRingKind::Waterline).empty());
}

TEST(TerrainPolygonChannelsTest, PondStraddlingABorderSeams) {
	const std::vector<Pond> ponds = {{512.4, 300.2, 14.0F, 0.9F, 2.3F, 200}};
	const ChunkCoordinate	westCoord{0, 0};
	const ChunkCoordinate	eastCoord{1, 0};
	const HandTiles			grass(Biome::TemperateGrassland);
	const ChunkTerrainPolygons west = buildChunk(westCoord, grass, {}, ponds);
	const ChunkTerrainPolygons east = buildChunk(eastCoord, grass, {}, ponds);
	expectAllSimple(west, "west");
	expectAllSimple(east, "east");
	// Each chunk's rim reaches past its extended boundary, so each is closed
	// there with a synthetic edge.
	ASSERT_EQ(west.rings.size(), 1U);
	ASSERT_EQ(east.rings.size(), 1U);
	for (const ChunkTerrainPolygons* polys : {&west, &east}) {
		EXPECT_EQ(flaggedVertices(polys->rings[0], ShoreProfile::kFlagSynthetic).size(), 1U);
	}

	const SeamSide a = seamSide(westCoord, west);
	const SeamSide b = seamSide(eastCoord, east);
	expectBorderVerticesMatch(a, b, true);
	EXPECT_EQ(navVerticesOnLine(west, true, kChunkMm).size(), 2U);
	const geometry::nav::NavMesh mesh = buildSeamMesh({a, b});
	expectSeamClassificationAgrees(mesh, a, b, true);
}

// ============================================================================
// Fordability (D7 steps 5-6)
// ============================================================================

TEST(TerrainPolygonChannelsTest, FordableSplitSharesItsCutVertices) {
	const ChunkCoordinate	   coord{0, 0};
	const std::vector<Segment> river = riverAlong(
		100.0, 400.0, 15.0, [](double x) { return 256.0 + 0.02 * (x - 100.0); }, [](double x) { return 1.0 - 0.7 * (x - 100.0) / 300.0; }
	);
	const ChunkTerrainPolygons polys = buildChunk(coord, HandTiles(Biome::TemperateGrassland), river);
	expectAllSimple(polys, "split");

	const std::vector<const TerrainRing*> channels = ringsOfKind(polys, TerrainRingKind::Channel);
	ASSERT_EQ(channels.size(), 2U);
	const TerrainRing* wide	  = channels[0]->blocksMovement ? channels[0] : channels[1];
	const TerrainRing* narrow = channels[0]->blocksMovement ? channels[1] : channels[0];
	ASSERT_TRUE(wide->blocksMovement);
	ASSERT_FALSE(narrow->blocksMovement);
	EXPECT_GT(wide->meanHalfWidthM, 0.6F);
	EXPECT_LT(narrow->meanHalfWidthM, 0.6F);
	EXPECT_EQ(wide->water, WaterKind::River);
	EXPECT_FALSE(wide->holeCapable);

	const std::vector<Vec2i64> wideCuts	  = flaggedVertices(*wide, ShoreProfile::kFlagFordableCut);
	const std::vector<Vec2i64> narrowCuts = flaggedVertices(*narrow, ShoreProfile::kFlagFordableCut);
	ASSERT_EQ(wideCuts.size(), 2U);
	EXPECT_EQ(wideCuts, narrowCuts);
	// The cut sits where 2 hw = 1.2 m, x = 100 + 300 * 0.4 / 0.7, and spans the
	// channel there: 1.2 m give or take the bank noise (at most half of hw per bank).
	const double cutX = 100.0 + 300.0 * 0.4 / 0.7;
	for (const Vec2i64& v : wideCuts) {
		EXPECT_NEAR(static_cast<double>(v.x) / 1000.0, cutX, 0.2);
	}
	const double cutWidth = std::hypot(static_cast<double>(wideCuts[1].x - wideCuts[0].x), static_cast<double>(wideCuts[1].y - wideCuts[0].y));
	EXPECT_GT(cutWidth, 600.0);
	EXPECT_LT(cutWidth, 1800.0);
	// The two vertices are adjacent in each ring: the butt edge.
	for (const TerrainRing* ring : {wide, narrow}) {
		const auto it0 = std::find(ring->ring.begin(), ring->ring.end(), wideCuts[0]);
		const auto it1 = std::find(ring->ring.begin(), ring->ring.end(), wideCuts[1]);
		const auto i0  = static_cast<size_t>(it0 - ring->ring.begin());
		const auto i1  = static_cast<size_t>(it1 - ring->ring.begin());
		const size_t n = ring->ring.size();
		EXPECT_TRUE((i0 + 1) % n == i1 || (i1 + 1) % n == i0);
	}

	// Nav skips the fordable piece (D9): only the wide ring blocks.
	int blockingNav = 0;
	for (const TerrainRing& r : polys.navRings) {
		blockingNav += r.blocksMovement ? 1 : 0;
	}
	EXPECT_EQ(blockingNav, 1);
}

// ============================================================================
// Ends: round caps at true ends, nothing at a gather cut (D4, D7 step 6)
// ============================================================================

TEST(TerrainPolygonChannelsTest, RoundCapsAtTrueEndsAndNoCapAtACut) {
	const ChunkCoordinate coord{0, 0};
	// D: both ends inside the chunk. E: runs out of the gather box on both sides.
	std::vector<Segment> rivers = riverAlong(100.0, 300.0, 15.0, [](double) { return 256.0; }, [](double) { return 2.0; });
	for (const Segment& s : riverAlong(
			 -200.0, 800.0, 15.0, [](double x) { return 400.0 + 15.0 * std::sin(kTwoPi * x / 120.0); }, [](double) { return 2.5; }
		 )) {
		rivers.push_back(s);
	}
	const ChunkTerrainPolygons polys = buildChunk(coord, HandTiles(Biome::TemperateGrassland), rivers);
	expectAllSimple(polys, "caps");
	const std::vector<const TerrainRing*> channels = ringsOfKind(polys, TerrainRingKind::Channel);
	ASSERT_EQ(channels.size(), 2U);

	const Vec2i64 extMin{-kApronMm, -kApronMm};
	const Vec2i64 extMax{kChunkMm + kApronMm, kChunkMm + kApronMm};
	for (const TerrainRing* ring : channels) {
		const bool crossing = maxX(ring->ring) > 400000;
		size_t	   flagged	= 0;
		for (size_t i = 0; i < ring->ring.size(); ++i) {
			const Vec2i64& a	  = ring->ring[i];
			const Vec2i64& b	  = ring->ring[(i + 1) % ring->ring.size()];
			const bool	   onSide = (a.x == extMin.x && b.x == extMin.x) || (a.x == extMax.x && b.x == extMax.x) ||
								(a.y == extMin.y && b.y == extMin.y) || (a.y == extMax.y && b.y == extMax.y);
			const bool synthetic = (ring->profiles[i].flags & ShoreProfile::kFlagSynthetic) != 0;
			EXPECT_EQ(onSide, synthetic) << "edge " << i;
			flagged += synthetic ? 1 : 0;
			EXPECT_TRUE(a.x >= extMin.x && a.x <= extMax.x && a.y >= extMin.y && a.y <= extMax.y);
		}
		if (crossing) {
			// Cut by the gather on both sides: closed exactly along the extended
			// boundary, each end two bank vertices and the synthetic edge between.
			EXPECT_EQ(flagged, 2U);
			for (const int64_t line : {extMin.x, extMax.x}) {
				EXPECT_EQ(std::count_if(ring->ring.begin(), ring->ring.end(), [line](const Vec2i64& v) { return v.x == line; }), 2);
			}
		} else {
			// True ends: a round cap reaching about hw past each end point, with
			// vertices all the way round it.
			EXPECT_EQ(flagged, 0U);
			int beyondStart = 0;
			int beyondEnd	= 0;
			for (const Vec2i64& v : ring->ring) {
				beyondStart += v.x < 99500 ? 1 : 0;
				beyondEnd += v.x > 300500 ? 1 : 0;
				EXPECT_GT(v.x, 100000 - 3500);
				EXPECT_LT(v.x, 300000 + 3500);
			}
			EXPECT_GE(beyondStart, 3);
			EXPECT_GE(beyondEnd, 3);
		}
	}
}

// ============================================================================
// Bends (D7 steps 3-4)
// ============================================================================

// A 120 degree bend whose curvature peaks at 1 / 5.08 m, stroked at hw 4 m. The
// inner bank's unclamped offset at the apex is hw + 0.4 a = 4.4 m plus up to
// 0.68 m of damped noise, past 0.9 R: without the clamp this bend folds for part
// of the noise range. With it the inner bank never reaches past 0.9 R and the
// ring stays simple. (A bend with hw well over R, or a hairpin whose legs come
// back within the ribbon's width, cannot be saved by a local clamp; see the
// builder's finishVectorRing.)
TEST(TerrainPolygonChannelsTest, TightBendInnerBankStaysUnderTheRadius) {
	constexpr double kSigmaM = 6.0;
	constexpr double kTurn	 = 120.0 * std::numbers::pi / 180.0;
	const double	 kappaMax = kTurn / (kSigmaM * std::sqrt(std::numbers::pi)); // Gaussian profile integrates to kTurn
	constexpr double kStepM	  = 2.0;

	std::vector<RiverPoint> pts;
	double					x		= 180.0;
	double					y		= 200.0;
	double					heading = 0.0;
	std::optional<Vec2i64>	apex;
	for (int i = -40; i <= 40; ++i) {
		const double s = static_cast<double>(i) * kStepM;
		pts.push_back({x, y, 4.0F});
		if (i == 0) {
			apex = Vec2i64{std::llround(x * 1000.0), std::llround(y * 1000.0)};
		}
		// Midpoint rule on the Gaussian curvature profile.
		const double mid = s + kStepM / 2.0;
		heading += kappaMax * std::exp(-(mid / kSigmaM) * (mid / kSigmaM)) * kStepM;
		x += kStepM * std::cos(heading);
		y += kStepM * std::sin(heading);
	}
	const ChunkTerrainPolygons polys = buildChunk({0, 0}, HandTiles(Biome::TemperateGrassland), segmentsOf(pts));
	expectAllSimple(polys, "bend");
	const std::vector<const TerrainRing*> channels = ringsOfKind(polys, TerrainRingKind::Channel);
	ASSERT_EQ(channels.size(), 1U);
	// The closest bank to the apex is the inner one, clamped to 0.9 R (plus the
	// 100 mm simplification tolerance and the resample chord).
	ASSERT_TRUE(apex.has_value());
	EXPECT_LT(distanceToRingMm(channels[0]->ring, *apex), 0.9 * 1000.0 / kappaMax + 200.0);
}

// ============================================================================
// River mouths (D7)
// ============================================================================

namespace {

	// A straight river along y = 256 m from x = 50 m to x = endX, hw 3 m. Built
	// with and without a receiving body at the same world position: the bank
	// noise and (on a straight river) the zero asymmetry are identical, so the
	// ratio of widths at an x is exactly the flare there.
	struct MouthPair {
		const TerrainRing* mouth	 = nullptr;
		const TerrainRing* control	 = nullptr;
		double			   shoreXM	 = 0.0;
		const TerrainRing* receiving = nullptr;
	};

	std::vector<Segment> straightRiver(double endX) {
		return riverAlong(50.0, endX, 15.0, [](double) { return 256.0; }, [](double) { return 3.0; });
	}

	void expectFlare(const MouthPair& pair) {
		constexpr double kW = 6.0;
		ASSERT_NE(pair.mouth, nullptr);
		ASSERT_NE(pair.control, nullptr);
		auto ratio = [&pair](double xM) { return extentAt(pair.mouth->ring, xM) / extentAt(pair.control->ring, xM); };

		EXPECT_NEAR(ratio(150.0), 1.0, 0.03);
		EXPECT_NEAR(ratio(pair.shoreXM - 2.5 * kW), 1.0, 0.03);
		// Half way up the 2 w flare: 1 + 0.6 smoothstep(0.5) = 1.3.
		EXPECT_NEAR(ratio(pair.shoreXM - kW), 1.3, 0.08);
		// Past the mouth, inside the water body: the full 1.6.
		for (const double dx : {1.0, 2.5, 4.0}) {
			EXPECT_NEAR(ratio(pair.shoreXM + dx), 1.6, 0.06) << "shore + " << dx;
		}
		// The ribbon runs on about 1 w into the water body (plus its cap), then stops.
		const double endM = static_cast<double>(maxX(pair.mouth->ring)) / 1000.0;
		EXPECT_GT(endM, pair.shoreXM + kW);
		EXPECT_LT(endM, pair.shoreXM + kW + 1.6 * 3.0 * 1.6 + 1.0);
		EXPECT_GT(static_cast<double>(maxX(pair.control->ring)) / 1000.0, 330.0);

		bool overlaps = false;
		for (const Vec2i64& v : pair.mouth->ring) {
			overlaps = overlaps || geometry::pointInPolygon(v, pair.receiving->ring) == geometry::PointInPolygon::Inside;
		}
		EXPECT_TRUE(overlaps);
	}

} // namespace

TEST(TerrainPolygonChannelsTest, RiverFlaresIntoALake) {
	const ChunkCoordinate	   coord{0, 0};
	const std::vector<Segment> river = straightRiver(330.0);
	const ChunkTerrainPolygons withLake =
		buildChunk(coord, HandTiles(coord, [](int64_t tx, int64_t) { return tx >= 300 ? Biome::Lake : Biome::TemperateGrassland; }), river);
	const ChunkTerrainPolygons control = buildChunk(coord, HandTiles(Biome::TemperateGrassland), river);
	expectAllSimple(withLake, "lake");

	MouthPair pair;
	const std::vector<const TerrainRing*> lakes = ringsOfKind(withLake, TerrainRingKind::Waterline);
	ASSERT_EQ(lakes.size(), 1U);
	pair.receiving = lakes[0];
	pair.shoreXM   = crossings(lakes[0]->ring, false, 256000.0).front() / 1000.0;
	ASSERT_EQ(ringsOfKind(withLake, TerrainRingKind::Channel).size(), 1U);
	ASSERT_EQ(ringsOfKind(control, TerrainRingKind::Channel).size(), 1U);
	pair.mouth	 = ringsOfKind(withLake, TerrainRingKind::Channel)[0];
	pair.control = ringsOfKind(control, TerrainRingKind::Channel)[0];
	expectFlare(pair);
}

TEST(TerrainPolygonChannelsTest, RiverFlaresIntoAPond) {
	const ChunkCoordinate	   coord{0, 0};
	const std::vector<Segment> river = straightRiver(335.0);
	const std::vector<Pond>	   ponds = {{330.0, 256.0, 12.0F, 0.4F, 1.7F, 180}};
	const ChunkTerrainPolygons withPond = buildChunk(coord, HandTiles(Biome::TemperateGrassland), river, ponds);
	const ChunkTerrainPolygons control	= buildChunk(coord, HandTiles(Biome::TemperateGrassland), river);
	expectAllSimple(withPond, "pond");

	MouthPair pair;
	const std::vector<const TerrainRing*> pondRings = ringsOfKind(withPond, TerrainRingKind::Pond);
	ASSERT_EQ(pondRings.size(), 1U);
	pair.receiving = pondRings[0];
	pair.shoreXM   = crossings(pondRings[0]->ring, false, 256000.0).front() / 1000.0;
	ASSERT_EQ(ringsOfKind(withPond, TerrainRingKind::Channel).size(), 1U);
	pair.mouth	 = ringsOfKind(withPond, TerrainRingKind::Channel)[0];
	pair.control = ringsOfKind(control, TerrainRingKind::Channel)[0];
	expectFlare(pair);
}

// ============================================================================
// Ponds (D8)
// ============================================================================

TEST(TerrainPolygonChannelsTest, PondRingMatchesItsRim) {
	const Pond				   pond{200.3, 310.7, 9.5F, 0.7F, 2.1F, 150};
	const ChunkTerrainPolygons polys = buildChunk({0, 0}, HandTiles(Biome::TemperateGrassland), {}, {pond});
	expectAllSimple(polys, "pond");
	ASSERT_EQ(polys.rings.size(), 1U);
	const TerrainRing& ring = polys.rings[0];
	EXPECT_EQ(ring.kind, TerrainRingKind::Pond);
	EXPECT_EQ(ring.water, WaterKind::Pond);
	EXPECT_TRUE(ring.blocksMovement);
	EXPECT_FALSE(ring.holeCapable);
	EXPECT_EQ(geometry::windingOrder(ring.ring), geometry::Winding::CounterClockwise);
	EXPECT_EQ(
		geometry::pointInPolygon(Vec2i64{std::llround(pond.cx * 1000.0), std::llround(pond.cy * 1000.0)}, ring.ring),
		geometry::PointInPolygon::Inside
	);

	// Area of the unperturbed rim, 1/2 integral of r(theta)^2; the 125 mm radial
	// noise moves it by well under 2%.
	constexpr int kSteps = 20000;
	double		  area	 = 0.0;
	for (int k = 0; k < kSteps; ++k) {
		const double r = worldgen::PondNetwork2D::rimRadiusAt(pond, kTwoPi * (static_cast<double>(k) + 0.5) / kSteps);
		area += 0.5 * r * r * kTwoPi / kSteps;
	}
	const double ringArea = static_cast<double>(absArea2(ring.ring)) / 2.0 / 1.0e6;
	EXPECT_NEAR(ringArea / area, 1.0, 0.02);
	ASSERT_EQ(polys.navRings.size(), 1U);
	EXPECT_EQ(polys.navRings[0].ring, ring.ring);
}

// ============================================================================
// Thalweg (D2, D7 step 3)
// ============================================================================

TEST(TerrainPolygonChannelsTest, ThalwegHugsTheOuterBankOfABend) {
	// A left-turning arc of radius 40 m around (256, 150), hw 4 m: the outer bank
	// is away from the center, and the asymmetry there is
	// min(0.25, 2 hw / R) hw = 0.8 m.
	constexpr double		kRadiusM = 40.0;
	const Vec2i64			center{256000, 150000};
	std::vector<RiverPoint> pts;
	for (int deg = -90; deg <= 30; deg += 6) {
		const double a = static_cast<double>(deg) * std::numbers::pi / 180.0;
		pts.push_back({256.0 + kRadiusM * std::cos(a), 150.0 + kRadiusM * std::sin(a), 4.0F});
	}
	const ChunkTerrainPolygons polys = buildChunk({0, 0}, HandTiles(Biome::TemperateGrassland), segmentsOf(pts));
	expectAllSimple(polys, "thalweg");
	ASSERT_EQ(polys.thalwegs.size(), 1U);
	const ThalwegPath& path = polys.thalwegs[0];
	ASSERT_GE(path.points.size(), 100U);
	EXPECT_EQ(path.halfWidthM.size(), path.points.size());
	EXPECT_EQ(path.widthRatio.size(), path.points.size());
	EXPECT_EQ(path.curvature.size(), path.points.size());

	// The middle of the arc, well away from the ends.
	const size_t mid = path.points.size() / 2;
	const double r	 = std::hypot(static_cast<double>(path.points[mid].x - center.x), static_cast<double>(path.points[mid].y - center.y));
	EXPECT_NEAR(r / 1000.0, kRadiusM + 0.8, 0.15);
	EXPECT_NEAR(path.curvature[mid], 1.0 / kRadiusM, 0.1 / kRadiusM);
	EXPECT_NEAR(path.halfWidthM[mid], 4.0F, 1e-4F);
	EXPECT_NEAR(path.widthRatio[mid], 1.0F, 1e-4F);
}

// ============================================================================
// Determinism (D14)
// ============================================================================

TEST(TerrainPolygonChannelsTest, IdenticalAcrossThreadCountsWithRiversAndPonds) {
	const std::vector<Segment>		   rivers = seamWorldRivers();
	const std::vector<Pond>			   ponds  = {{512.4, 300.2, 14.0F, 0.9F, 2.3F, 200}, {760.0, 620.0, 8.0F, 1.1F, 0.2F, 90}};
	const std::vector<ChunkCoordinate> coords = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};

	auto generate = [&rivers, &ponds](ChunkCoordinate c) {
		ChunkSampleResult sample = makeUniformChunkSampleResult(BiomeWeights::single(Biome::TemperateGrassland), 10.0F);
		Gathered		  g		 = gatherFor(c, rivers, ponds);
		sample.riverSegments	 = std::move(g.segments);
		sample.pondBlobs		 = std::move(g.ponds);
		auto chunk				 = std::make_unique<Chunk>(c, std::move(sample), kWorldSeed);
		chunk->generate();
		return chunk;
	};

	std::vector<std::unique_ptr<Chunk>> serial;
	for (const ChunkCoordinate& c : coords) {
		serial.push_back(generate(c));
	}
	std::vector<std::future<std::unique_ptr<Chunk>>> futures;
	for (auto it = coords.rbegin(); it != coords.rend(); ++it) {
		const ChunkCoordinate c = *it;
		futures.push_back(std::async(std::launch::async, [&generate, c] { return generate(c); }));
	}
	std::vector<std::unique_ptr<Chunk>> concurrent(coords.size());
	for (size_t i = 0; i < futures.size(); ++i) {
		concurrent[coords.size() - 1 - i] = futures[i].get();
	}

	int channelRings = 0;
	for (size_t i = 0; i < coords.size(); ++i) {
		const ChunkTerrainPolygons& a = serial[i]->terrainPolygons();
		const ChunkTerrainPolygons& b = concurrent[i]->terrainPolygons();
		expectAllSimple(a, "determinism " + std::to_string(i));
		EXPECT_TRUE(sameRings(a.rings, b.rings)) << "chunk " << i;
		EXPECT_TRUE(sameRings(a.navRings, b.navRings)) << "chunk " << i;
		EXPECT_TRUE(sameThalwegs(a.thalwegs, b.thalwegs)) << "chunk " << i;
		channelRings += static_cast<int>(ringsOfKind(a, TerrainRingKind::Channel).size());
	}
	EXPECT_GT(channelRings, 0);
}

// ============================================================================
// Real river geometry (RiverNetwork2D through GeneratedWorldSampler)
// ============================================================================

// The carved-river world the GeneratedWorldSampler river tests use: a flow-80
// trunk from world (0, 0) eastward, meandering about y = 0 (the border between
// chunk rows -1 and 0), with two headwater feeders at its source and
// along-channel feeders. Six chunks around the source, every ring simple, every
// adjacent pair seamed.
TEST(TerrainPolygonChannelsTest, RealRiverWithFeedersStaysSimpleAndSeamed) {
	const river_test::CarvedRiverWorld world = river_test::makeCarvedRiverWorld();
	const GeneratedWorldSampler		   sampler(world.world, world.landingLat, world.landingLon);

	constexpr int32_t kMinX = -1;
	constexpr int32_t kMaxX = 1;
	constexpr int32_t kMinY = -1;
	constexpr int32_t kMaxY = 0;
	std::map<std::pair<int32_t, int32_t>, std::unique_ptr<Chunk>> chunks;
	const auto start = std::chrono::steady_clock::now();
	for (int32_t y = kMinY; y <= kMaxY; ++y) {
		for (int32_t x = kMinX; x <= kMaxX; ++x) {
			const ChunkCoordinate c{x, y};
			auto				  chunk = std::make_unique<Chunk>(c, sampler.sampleChunk(c), sampler.getWorldSeed());
			chunk->generate();
			chunks[{x, y}] = std::move(chunk);
		}
	}
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

	size_t rings	   = 0;
	size_t channels	   = 0;
	size_t fordable	   = 0;
	size_t ringVerts   = 0;
	size_t navRings	   = 0;
	size_t navVerts	   = 0;
	size_t maxRingSize = 0;
	for (const auto& [key, chunk] : chunks) {
		const ChunkTerrainPolygons& polys = chunk->terrainPolygons();
		expectAllSimple(polys, "real (" + std::to_string(key.first) + ", " + std::to_string(key.second) + ")");
		for (const TerrainRing& r : polys.rings) {
			++rings;
			channels += r.kind == TerrainRingKind::Channel ? 1 : 0;
			fordable += r.blocksMovement ? 0 : 1;
			ringVerts += r.ring.size();
			maxRingSize = std::max(maxRingSize, r.ring.size());
		}
		for (const TerrainRing& r : polys.navRings) {
			++navRings;
			navVerts += r.ring.size();
		}
	}
	std::cout << "[ real river ] " << chunks.size() << " chunks in " << seconds << " s: " << rings << " rings (" << channels
			  << " channel, " << fordable << " fordable), " << ringVerts << " ring vertices (max " << maxRingSize << "), "
			  << navRings << " nav rings, " << navVerts << " nav vertices\n";
	EXPECT_GT(channels, 3U);
	EXPECT_GT(fordable, 0U);

	int seamsCrossed = 0;
	for (const auto& [key, chunk] : chunks) {
		for (const bool vertical : {true, false}) {
			const std::pair<int32_t, int32_t> nextKey = vertical ? std::pair{key.first + 1, key.second} : std::pair{key.first, key.second + 1};
			const auto						  next	  = chunks.find(nextKey);
			if (next == chunks.end()) {
				continue;
			}
			const SeamSide			   a	= seamChunk(*chunk);
			const SeamSide			   b	= seamChunk(*next->second);
			const int64_t			   line = vertical ? b.min.x : b.min.y;
			const std::vector<Vec2i64> va	= navVerticesOnLine(*a.polys, vertical, line);
			EXPECT_EQ(va, navVerticesOnLine(*b.polys, vertical, line)) << key.first << ", " << key.second << (vertical ? " east" : " north");
			if (va.empty()) {
				continue;
			}
			++seamsCrossed;
			const geometry::nav::NavMesh mesh = buildSeamMesh({a, b});
			expectSeamClassificationAgrees(mesh, a, b, vertical);
		}
	}
	EXPECT_GE(seamsCrossed, 3);
}
