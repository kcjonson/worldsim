#include "SeenArea.h"
#include "SightFeed.h"

#include <nav/NavMesh.h>
#include <nav/PatchableMesh.h>
#include <predicates/Predicates.h>
#include <visibility/ForestShape.h>
#include <visibility/SightPolygon.h>
#include <visibility/SightSkirt.h>
#include <visibility/Visibility.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <optional>
#include <random>
#include <set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

using geometry::Vec2i64;
using vision::SeenArea;

namespace {

	constexpr std::int64_t kSightMm = 30000;
	const vision::SightRanges kPlainRim = vision::equalRanges(kSightMm);

	using Edge = std::pair<Vec2i64, Vec2i64>;

	// The union's edge: every triangle side with no triangle across it.
	std::set<Edge> boundaryOf(const std::vector<SeenArea::Triangle>& triangles) {
		std::set<Edge> sides;
		for (const SeenArea::Triangle& triangle : triangles) {
			for (std::size_t k = 0; k < 3; ++k) {
				sides.insert({triangle.corners[k], triangle.corners[(k + 1) % 3]});
			}
		}
		std::set<Edge> out;
		for (const Edge& side : sides) {
			if (!sides.contains({side.second, side.first})) {
				out.insert(side);
			}
		}
		return out;
	}

	std::set<Vec2i64> cornersOf(const std::vector<SeenArea::Triangle>& triangles) {
		std::set<Vec2i64> out;
		for (const SeenArea::Triangle& triangle : triangles) {
			out.insert(triangle.corners.begin(), triangle.corners.end());
		}
		return out;
	}

	std::vector<SeenArea::Triangle> trianglesOf(const SeenArea& seen) {
		std::vector<SeenArea::Triangle> out;
		seen.appendTriangles(out);
		return out;
	}

	std::vector<SeenArea::Triangle> trianglesOf(const geometry::nav::PatchableMesh& mesh) {
		std::vector<SeenArea::Triangle> out;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (mesh.triangleLive(t)) {
				const geometry::nav::NavTriangle& triangle = mesh.triangles()[static_cast<std::size_t>(t)];
				out.push_back({{mesh.vertices()[triangle.v[0]], mesh.vertices()[triangle.v[1]], mesh.vertices()[triangle.v[2]]}});
			}
		}
		return out;
	}

	void drain(SeenArea& seen) {
		for (int tick = 0; tick < 1000 && (seen.shapesPending() > 0 || seen.sweepsWaiting() > 0); ++tick) {
			seen.update();
		}
		ASSERT_EQ(seen.shapesPending(), 0U);
		ASSERT_EQ(seen.sweepsWaiting(), 0U);
	}

	Vec2i64 metres(double x, double y) { return {std::llround(x * 1000.0), std::llround(y * 1000.0)}; }

	double distanceToOutline(const Vec2i64& point, const std::vector<Vec2i64>& outline) {
		double nearest = 1e300;
		for (std::size_t i = 0; i < outline.size(); ++i) {
			nearest = std::min(nearest, geometry::distanceToSegment(point, outline[i], outline[(i + 1) % outline.size()]));
		}
		return nearest;
	}

	// A square of `halfMetres` round (x, y) metres, counter-clockwise.
	std::vector<Vec2i64> square(double x, double y, double halfMetres) {
		return {metres(x - halfMetres, y - halfMetres), metres(x + halfMetres, y - halfMetres), metres(x + halfMetres, y + halfMetres),
				metres(x - halfMetres, y + halfMetres)};
	}

} // namespace

// K where registered: a diamond whose box meets the first ring's box, though the shapes are apart,
// meets one; a square far off meets none. The first ring is counted against nothing.
TEST(SeenArea, RingsMetCountsTheBoxesAlreadyRegistered) {
	SeenArea seen;
	const auto diamond = [](double x, double y, double radius) {
		return std::vector<Vec2i64>{metres(x, y - radius), metres(x + radius, y), metres(x, y + radius), metres(x - radius, y)};
	};
	seen.commitNow(diamond(0.0, 0.0, 5.0), metres(0.0, 0.0));
	seen.commitNow(diamond(9.0, 4.0, 4.0), metres(9.0, 4.0));
	seen.commitNow(square(100.0, 0.0, 5.0), metres(100.0, 0.0));
	ASSERT_EQ(seen.ringsRegistered(), 3U);
	EXPECT_EQ(seen.ringsMetMost(), 1U);
	EXPECT_DOUBLE_EQ(seen.ringsMetMean(), 0.5);
}

// The landing's polygon goes in at once, as one ring, and holds what it is inscribed in.
TEST(SeenArea, LandingIsOneRingInscribedInItsCircle) {
	SeenArea	  seen;
	const Vec2i64 drop = metres(1234.5, -987.25);
	seen.commitNow(geometry::sightPolygon(drop, 150000), drop);
	EXPECT_EQ(seen.ringsRegistered(), 1U);
	EXPECT_EQ(seen.shapesPending(), 0U);
	const double circle = std::numbers::pi * 150.0 * 150.0;
	EXPECT_LT(seen.committedSquareMeters(), circle);
	EXPECT_GT(seen.committedSquareMeters(), circle * 0.99);
	for (int k = 0; k < 16; ++k) {
		const double angle = 2.0 * std::numbers::pi * k / 16.0;
		EXPECT_TRUE(seen.isSeen(drop + metres(149.0 * std::cos(angle), 149.0 * std::sin(angle)))) << "direction " << k;
		EXPECT_FALSE(seen.isSeen(drop + metres(150.01 * std::cos(angle), 150.01 * std::sin(angle)))) << "direction " << k;
	}
}

// A queued shape is seen, and covers what it holds, before the budget commits it; the budget tests
// up to kSeenRingsPerTick shapes a tick, first in, first out, stops at the first that adds anything,
// and registers kSeenPiecesRegisteredPerTick of its pieces.
TEST(SeenArea, PendingShapesAnswerBeforeTheyAreCommitted) {
	SeenArea seen;
	for (int i = 0; i < 6; ++i) {
		const Vec2i64 centre = metres(100.0 * i, 0.0);
		seen.queue(geometry::sightPolygon(centre, kSightMm), centre);
	}
	EXPECT_EQ(seen.shapesPending(), 6U);
	EXPECT_EQ(seen.ringsRegistered(), 0U);
	EXPECT_TRUE(seen.isSeen(metres(500.0, 20.0)));
	EXPECT_TRUE(seen.covers(square(500.0, 0.0, 10.0)));
	EXPECT_FALSE(seen.isSeen(metres(50.0, 0.0)));

	const std::uint64_t before = seen.epoch();
	seen.update();
	EXPECT_EQ(seen.ringsRegistered(), vision::kSeenPiecesRegisteredPerTick);
	EXPECT_EQ(seen.shapesPending(), 6U - vision::kSeenPiecesRegisteredPerTick);
	EXPECT_NE(seen.epoch(), before);
	EXPECT_EQ(seen.registeredRing(0).front(), geometry::sightPolygon(metres(0.0, 0.0), kSightMm).front()) << "first in, first out";
	EXPECT_TRUE(seen.isSeen(metres(500.0, 20.0))) << "still pending, still seen";
	drain(seen);
	EXPECT_EQ(seen.ringsRegistered(), 6U);
	EXPECT_TRUE(seen.isSeen(metres(500.0, 20.0)));
}

// A shape the record already holds is dropped, not registered.
TEST(SeenArea, CoveredShapeIsDropped) {
	SeenArea	  seen;
	const Vec2i64 centre = metres(10.0, 10.0);
	seen.commitNow(geometry::sightPolygon(centre, 150000), centre);
	seen.queue(geometry::sightPolygon(metres(30.0, 15.0), kSightMm - vision::kSeenSlackMm), metres(30.0, 15.0));
	const std::uint64_t before = seen.epoch();
	drain(seen);
	EXPECT_EQ(seen.ringsRegistered(), 1U);
	EXPECT_EQ(seen.shapesDropped(), 1U);
	EXPECT_EQ(seen.epoch(), before);
}

// An outline across a committed ring and a pending one is covered; one reaching past both is not.
TEST(SeenArea, CoversAcrossACommittedRingAndAPendingOne) {
	SeenArea	  seen;
	const Vec2i64 west = metres(0.0, 0.0);
	const Vec2i64 east = metres(40.0, 0.0);
	seen.commitNow(geometry::sightPolygon(west, kSightMm), west);
	ASSERT_TRUE(seen.setOpenLeg(1, east, east, vision::equalRanges(kSightMm), std::nullopt));
	const std::vector<Vec2i64> across = square(20.0, 0.0, 10.0);
	EXPECT_TRUE(seen.covers(across)) << "half in the record, half in the open leg";
	EXPECT_FALSE(seen.covers(square(20.0, 25.0, 10.0))) << "reaches past both";
	EXPECT_FALSE(seen.covers(square(-25.0, 0.0, 10.0))) << "reaches past the record alone";

	seen.closeLeg(1);
	EXPECT_TRUE(seen.covers(across)) << "queued";
	drain(seen);
	EXPECT_GE(seen.ringsRegistered(), 2U);
	EXPECT_TRUE(seen.covers(across)) << "committed";
	EXPECT_FALSE(seen.covers(square(20.0, 25.0, 10.0)));
}

// lastSeenToward: `to` when it is seen; the far end of the last seen stretch on a segment that leaves
// the record, re-enters it (here through a pending shape), and leaves again; the same on one that
// starts outside; none when nothing on it is seen.
TEST(SeenArea, LastSeenTowardTheGoal) {
	SeenArea	  seen;
	const Vec2i64 west = metres(0.0, 0.0);
	seen.commitNow(geometry::sightPolygon(west, 10000), west);
	const Vec2i64 east = metres(40.0, 0.0);
	seen.queue(geometry::sightPolygon(east, 10000), east);

	const auto near = [](const std::optional<Vec2i64>& point, const Vec2i64& expected) {
		return point.has_value() && std::abs(point->x - expected.x) <= 2 && std::abs(point->y - expected.y) <= 2;
	};

	EXPECT_EQ(seen.lastSeenToward(west, metres(5.0, 0.0)), std::optional(metres(5.0, 0.0))) << "seen goal";
	EXPECT_EQ(seen.lastSeenToward(west, metres(45.0, 0.0)), std::optional(metres(45.0, 0.0))) << "seen in a pending shape";

	const std::optional<Vec2i64> twice = seen.lastSeenToward(west, metres(80.0, 0.0));
	EXPECT_TRUE(near(twice, metres(50.0, 0.0))) << "leaves, re-enters, leaves: " << (twice ? twice->x : -1);
	EXPECT_TRUE(twice.has_value() && seen.isSeen(*twice));

	const std::optional<Vec2i64> between = seen.lastSeenToward(west, metres(20.0, 0.0));
	EXPECT_TRUE(near(between, metres(10.0, 0.0))) << (between ? between->x : -1);

	const std::optional<Vec2i64> outside = seen.lastSeenToward(metres(-30.0, 0.0), metres(25.0, 0.0));
	EXPECT_TRUE(near(outside, metres(10.0, 0.0))) << "starts outside: " << (outside ? outside->x : -1);
	EXPECT_TRUE(outside.has_value() && seen.isSeen(*outside));

	const std::optional<Vec2i64> slanted = seen.lastSeenToward(metres(-3.0, -1.0), metres(17.0, 13.0));
	ASSERT_TRUE(slanted.has_value());
	EXPECT_TRUE(seen.isSeen(*slanted));
	EXPECT_FALSE(seen.isSeen(*slanted + Vec2i64{20, 14})) << "a few millimetres on toward the goal is not seen";

	EXPECT_FALSE(seen.lastSeenToward(metres(0.0, 50.0), metres(40.0, 50.0)).has_value()) << "nothing seen on it";

	drain(seen);
	EXPECT_TRUE(near(seen.lastSeenToward(west, metres(80.0, 0.0)), metres(50.0, 0.0))) << "the same once committed";
}

/// Visibility polygons from viewpoints 0.5 m apart walking past and through a doorway, each queued as
// a sight polygon with the one before it: the record equals a fresh build of the rings it
// registered; it holds every point of every polygon deeper than the cover tolerance, and nothing
// farther than the skirt from the polygons and the sweep's; and the same walk again registers
// nothing and sweeps nothing.
TEST(SeenArea, DoorwayPolygonsEqualAFreshBuildUpToTheSkirt) {
	const std::vector<geometry::OccluderSegment> walls = {
		{metres(-20.0, 0.0), metres(-0.5, 0.0)},
		{metres(0.5, 0.0), metres(20.0, 0.0)},
		{metres(-0.5, 0.0), metres(-0.5, 0.2)},
		{metres(0.5, 0.0), metres(0.5, 0.2)},
		{metres(-20.0, 0.2), metres(-0.5, 0.2)},
		{metres(0.5, 0.2), metres(20.0, 0.2)},
	};
	std::vector<std::vector<Vec2i64>> walks(3);
	for (int step = 0; step <= 24; ++step) {
		walks[0].push_back(metres(0.0, -6.0 + 0.5 * step));
	}
	for (int step = 0; step <= 20; ++step) {
		walks[1].push_back(metres(-6.0 + 0.5 * step, -0.4));
	}
	for (int step = 0; step <= 16; ++step) {
		walks[2].push_back(metres(-4.0 + 0.5 * step, -4.0 + 0.5 * step));
	}

	SeenArea						  seen;
	std::vector<std::vector<Vec2i64>> polygons; // every polygon that could go in: the walks' and the sweep's
	seen.setSightBuilder([&](const Vec2i64& centre, std::int64_t /*sightRadiusMm*/, const vision::SightRanges& rimRanges) {
		const std::int64_t reachMm = rimRanges.front();
		polygons.push_back(geometry::computeVisibilityPolygon(centre, reachMm, walls));
		return SeenArea::BuiltSight{polygons.back(), rimRanges};
	});
	int	 notSimple = 0;
	auto walkAll   = [&](bool keep) {
		  for (const std::vector<Vec2i64>& walk : walks) {
			  std::vector<Vec2i64> previous;
			  for (const Vec2i64& viewpoint : walk) {
				  const geometry::Ring polygon = geometry::computeVisibilityPolygon(viewpoint, kSightMm, walls);
				  if (!geometry::simpleAround(polygon, viewpoint)) {
					  ++notSimple;
					  previous.clear();
					  continue;
				  }
				  if (keep) {
					  polygons.push_back(polygon);
				  }
				  seen.queueSightPolygon(polygon, viewpoint, kSightMm, kPlainRim, previous, kPlainRim);
				  seen.update();
				  previous.assign(1, viewpoint);
			  }
		  }
		  drain(seen);
	};
	walkAll(true);
	EXPECT_EQ(notSimple, 0);
	EXPECT_GT(seen.ringsRegistered(), 10U);
	EXPECT_GT(seen.shapesDropped(), 0U);
	EXPECT_EQ(seen.shapesSkipped(), 0U);
	EXPECT_GT(seen.sweepsBuilt(), 0U);

	std::mt19937 random(5);
	int			 deepSamples = 0;
	for (const std::vector<Vec2i64>& polygon : polygons) {
		for (int attempt = 0; attempt < 30; ++attempt) {
			const std::int64_t x = static_cast<std::int64_t>(random() % 60001) - 30000;
			const std::int64_t y = static_cast<std::int64_t>(random() % 60001) - 30000;
			const Vec2i64	   point{x, y};
			if (geometry::pointInPolygon(point, polygon) != geometry::PointInPolygon::Inside || distanceToOutline(point, polygon) <= 6.0) {
				continue;
			}
			++deepSamples;
			EXPECT_TRUE(seen.isSeen(point)) << "(" << point.x << ", " << point.y << ") lost";
		}
	}
	EXPECT_GT(deepSamples, 1000);
	for (const SeenArea::Triangle& triangle : trianglesOf(seen)) {
		const Vec2i64 middle{(triangle.corners[0].x + triangle.corners[1].x + triangle.corners[2].x) / 3,
							 (triangle.corners[0].y + triangle.corners[1].y + triangle.corners[2].y) / 3};
		double nearest = 1e300;
		for (const std::vector<Vec2i64>& polygon : polygons) {
			nearest = geometry::pointInPolygon(middle, polygon) != geometry::PointInPolygon::Outside ? 0.0 : std::min(nearest, distanceToOutline(middle, polygon));
		}
		EXPECT_LE(nearest, static_cast<double>(geometry::kSeenSkirtMm + 1)) << "(" << middle.x << ", " << middle.y << ") seen past the skirt";
	}

	const std::size_t ringsBefore  = seen.ringsRegistered();
	const std::size_t sweepsBefore = seen.sweepsBuilt();
	walkAll(false);
	EXPECT_EQ(seen.ringsRegistered(), ringsBefore) << "the same walk again";
	EXPECT_EQ(seen.sweepsBuilt(), sweepsBefore) << "covered polygons sweep nothing";

	geometry::nav::PatchableMesh fresh;
	geometry::nav::PatchEdits	 edits;
	for (std::size_t i = 0; i < seen.ringsRegistered(); ++i) {
		edits.adds.push_back(geometry::nav::NavInputPolygon{.ring = seen.registeredRing(i)});
	}
	fresh.applyPatch(edits);
	EXPECT_EQ(cornersOf(trianglesOf(seen)), cornersOf(trianglesOf(fresh)));
	EXPECT_EQ(boundaryOf(trianglesOf(seen)), boundaryOf(trianglesOf(fresh)));
	for (const std::vector<Vec2i64>& walk : walks) {
		for (const Vec2i64& viewpoint : walk) {
			EXPECT_TRUE(seen.isSeen(viewpoint));
		}
	}
	EXPECT_FALSE(seen.isSeen(metres(-25.0, 25.0))) << "north of the wall, beyond every viewpoint north of it and out of line through the door";
}

// A sight polygon that adds ground near its rim queues the sweep back along the walk from the build
// before it, points kSeenSweepSpacingMm apart strictly between, built up to four a tick whatever shapes
// wait while the sweep's own pieces drain, which register only with what the shapes' leave
// (sight-softening.md 6.6); one with no build before it, or a covered one, builds nothing.
TEST(SeenArea, APolygonThatAddsGroundIsSweptAndACoveredOneIsNot) {
	const std::vector<geometry::OccluderSegment> walls = {{metres(3.0, -10.0), metres(3.0, 10.0)}};
	SeenArea									 seen;
	std::vector<Vec2i64>						 builtAt;
	seen.setSightBuilder([&](const Vec2i64& centre, std::int64_t /*sightRadiusMm*/, const vision::SightRanges& rimRanges) {
		const std::int64_t reachMm = rimRanges.front();
		builtAt.push_back(centre);
		return SeenArea::BuiltSight{geometry::computeVisibilityPolygon(centre, reachMm, walls), rimRanges};
	});
	const Vec2i64 first	 = metres(0.0, 0.0);
	const Vec2i64 second = metres(1.0, 0.0);
	seen.queueSightPolygon(geometry::computeVisibilityPolygon(first, kSightMm, walls), first, kSightMm, kPlainRim, {}, kPlainRim);
	drain(seen);
	EXPECT_EQ(seen.sweepsBuilt(), 0U) << "no build before it";
	seen.queueSightPolygon(geometry::computeVisibilityPolygon(second, kSightMm, walls), second, kSightMm, kPlainRim, {first}, kPlainRim);
	// Seven shapes far off, one piece each, keep shapes and pieces waiting behind the polygon's.
	for (int far = 0; far < 7; ++far) {
		seen.queue(square(200.0 + 50.0 * far, 0.0, 5.0), metres(200.0 + 50.0 * far, 0.0));
	}
	seen.update();
	EXPECT_EQ(seen.lastTickWork().piecesRegistered, vision::kSeenPiecesRegisteredPerTick);
	EXPECT_GT(seen.lastTickWork().sweepPointsBuilt, 0U) << "points between the two builds";
	EXPECT_GT(seen.pending().shapesWaiting, 0U) << "built while shapes wait";
	// The shapes' pieces register first: no sweep piece goes in before the seven far squares have.
	for (int tick = 0; tick < 100 && (seen.shapesPending() > 0 || seen.sweepsWaiting() > 0); ++tick) {
		EXPECT_LE(seen.lastTickWork().sweepPointsBuilt, vision::kSeenSweepPointsPerTick);
		EXPECT_TRUE(seen.registrations(SeenArea::Origin::Other) >= 7 || seen.registrations(SeenArea::Origin::Sweep) == 0) << "tick " << tick;
		seen.update();
	}
	drain(seen);
	EXPECT_EQ(seen.sweepsBuilt(), 9U);
	ASSERT_EQ(builtAt.size(), 9U);
	for (std::size_t point = 0; point < builtAt.size(); ++point) {
		EXPECT_EQ(builtAt[point], metres(0.1 * static_cast<double>(point + 1), 0.0));
	}

	const std::size_t ringsBefore = seen.ringsRegistered();
	seen.queueSightPolygon(geometry::computeVisibilityPolygon(second, kSightMm, walls), second, kSightMm, kPlainRim, {first}, kPlainRim);
	drain(seen);
	EXPECT_EQ(seen.ringsRegistered(), ringsBefore);
	EXPECT_EQ(builtAt.size(), 9U) << "a covered polygon builds no sweep";
}

// The seen boxes hold every seen point: the landing's, cut at the cells it spans, a pending shape's
// before it is committed and the committed one's after, across negative coordinates; a query that
// meets a box returns it, one that meets none returns nothing.
TEST(SeenArea, SeenBoxesHoldEverySeenPoint) {
	SeenArea	  seen;
	const Vec2i64 drop = metres(1234.5, -987.25);
	seen.commitNow(geometry::sightPolygon(drop, 150000), drop);

	const auto boxesIn = [&seen](const Vec2i64& low, const Vec2i64& high) {
		std::vector<SeenArea::Box> boxes;
		seen.appendSeenBoxes(low, high, boxes);
		return boxes;
	};
	const auto held = [](const std::vector<SeenArea::Box>& boxes, const Vec2i64& point) {
		return std::any_of(boxes.begin(), boxes.end(), [&point](const SeenArea::Box& box) {
			return point.x >= box.low.x && point.x <= box.high.x && point.y >= box.low.y && point.y <= box.high.y;
		});
	};

	// The landing spans the cells at y -1536..-1024 and -1024..-512 m, so it is two boxes, each cut to its cell.
	const std::vector<SeenArea::Box> everywhere = boxesIn(metres(-10000.0, -10000.0), metres(10000.0, 10000.0));
	ASSERT_EQ(everywhere.size(), 2U);
	for (const SeenArea::Box& box : everywhere) {
		EXPECT_GE(box.low.x, metres(1234.5 - 150.0, 0.0).x);
		EXPECT_LE(box.high.x, metres(1234.5 + 150.0, 0.0).x);
		EXPECT_TRUE(box.high.y <= -1024000 || box.low.y >= -1024000) << "cut to its cell";
	}
	std::mt19937 random(7);
	std::uniform_real_distribution<double> offset(-160.0, 160.0);
	for (int sample = 0; sample < 2000; ++sample) {
		const double  x		= offset(random);
		const double  y		= offset(random);
		const Vec2i64 point = drop + metres(x, y);
		if (seen.isSeen(point)) {
			EXPECT_TRUE(held(everywhere, point)) << x << ", " << y;
		}
	}

	// Half over the landing's east edge, the box comes back; wholly east of it, nothing does.
	EXPECT_FALSE(boxesIn(metres(1300.0, -1000.0), metres(1600.0, -900.0)).empty());
	EXPECT_TRUE(boxesIn(metres(1400.0, -1000.0), metres(1700.0, -900.0)).empty());

	// A pending shape is held while it waits, and after it is committed.
	const Vec2i64 far = metres(-3000.0, 2000.0);
	seen.queue(geometry::sightPolygon(far, kSightMm), far);
	const std::vector<SeenArea::Box> pending = boxesIn(far, far);
	ASSERT_EQ(pending.size(), 1U);
	EXPECT_TRUE(held(pending, far + metres(29.0, 0.0)));
	drain(seen);
	const std::vector<SeenArea::Box> committed = boxesIn(far - metres(40.0, 40.0), far + metres(40.0, 40.0));
	ASSERT_EQ(committed.size(), 1U);
	EXPECT_TRUE(held(committed, far + metres(29.0, 0.0)));
	EXPECT_TRUE(held(committed, far - metres(29.0, 0.0)));
	EXPECT_TRUE(boxesIn(metres(0.0, 0.0), metres(100.0, 100.0)).empty());
}

namespace {

	// The open leg's capsule as the record built it before forest legs: the sight polygon round the
	// start, each side facing back along the leg kept there and the rest moved to the end, the two
	// kinds bridged along the leg, and points in line dropped.
	std::vector<Vec2i64> capsuleBeforeForestLegs(const Vec2i64& start, const Vec2i64& end, std::int64_t radiusMm) {
		const std::vector<Vec2i64> atStart = geometry::sightPolygon(start, radiusMm);
		const Vec2i64			   leg	   = end - start;
		const std::size_t		   sides   = atStart.size();
		std::vector<Vec2i64>	   out;
		for (std::size_t k = 0; k < sides; ++k) {
			const bool before = geometry::cross(leg, atStart[k] - atStart[(k + sides - 1) % sides]).sign() < 0;
			const bool after  = geometry::cross(leg, atStart[(k + 1) % sides] - atStart[k]).sign() < 0;
			if (before && after) {
				out.push_back(atStart[k]);
			} else if (!before && !after) {
				out.push_back(atStart[k] + leg);
			} else if (before) {
				out.push_back(atStart[k]);
				out.push_back(atStart[k] + leg);
			} else {
				out.push_back(atStart[k] + leg);
				out.push_back(atStart[k]);
			}
		}
		for (bool changed = true; changed;) {
			changed = false;
			for (std::size_t i = 0; i < out.size() && out.size() >= 3; ++i) {
				const Vec2i64& previous = out[(i + out.size() - 1) % out.size()];
				const Vec2i64& next		= out[(i + 1) % out.size()];
				if (out[i] == previous || geometry::cross(out[i] - previous, next - previous).sign() == 0) {
					out.erase(out.begin() + static_cast<std::ptrdiff_t>(i));
					changed = true;
					--i;
				}
			}
		}
		return out;
	}

	std::vector<Vec2i64> openLegRing(const SeenArea& seen) {
		std::vector<const std::vector<Vec2i64>*> pending;
		seen.appendPendingShapes(pending);
		return pending.empty() ? std::vector<Vec2i64>{} : *pending.front();
	}

	std::set<Vec2i64> asSet(const std::vector<Vec2i64>& ring) { return {ring.begin(), ring.end()}; }

	bool holdsRing(const std::vector<Vec2i64>& outer, const std::vector<Vec2i64>& inner) {
		return std::all_of(inner.begin(), inner.end(), [&outer](const Vec2i64& point) {
			return geometry::pointInPolygon(point, outer) != geometry::PointInPolygon::Outside;
		});
	}

	// A forest shape's ranges: `radiusMm` less `deepMm` on the `count` sides from `first`, `radiusMm`
	// elsewhere.
	vision::SightRanges rangesWithDip(std::int64_t radiusMm, std::size_t first, std::size_t count, std::int64_t deepMm) {
		vision::SightRanges ranges = vision::equalRanges(radiusMm);
		for (std::size_t side = first; side < first + count; ++side) {
			ranges[side % geometry::kSightSides] = radiusMm - deepMm;
		}
		return ranges;
	}

	// The landing disc of 5 m round the origin and six spokes out from it, 8 m wide, every 60 degrees: a
	// sight polygon round the origin then adds the six sectors between them.
	void commitSpokes(SeenArea& seen) {
		seen.commitNow(geometry::sightPolygon({0, 0}, 5000), {0, 0});
		for (int spoke = 0; spoke < 6; ++spoke) {
			const double angle	= spoke * std::numbers::pi / 3.0;
			const double alongX = std::cos(angle);
			const double alongY = std::sin(angle);
			const auto	 corner = [&](double along, double across) {
				  return Vec2i64{std::llround((alongX * along - alongY * across) * 1000.0), std::llround((alongY * along + alongX * across) * 1000.0)};
			};
			const std::vector<Vec2i64> ring{corner(3.0, -4.0), corner(45.0, -4.0), corner(45.0, 4.0), corner(3.0, 4.0)};
			seen.commitNow(ring, corner(20.0, 0.0));
		}
	}

} // namespace

// With every range equal a leg's ring is the open leg's capsule as it was before forest legs, vertex
// for vertex, wherever it runs.
TEST(SeenArea, AnEqualRangesLegIsTheOpenLegsCapsule) {
	const std::int64_t							radiusMm = kSightMm - vision::kSeenSlackMm;
	std::mt19937								random(11);
	std::uniform_int_distribution<std::int64_t> coordinate(-80000, 80000);
	for (int leg = 0; leg < 60; ++leg) {
		SeenArea	  seen;
		const Vec2i64 start{coordinate(random), coordinate(random)};
		const Vec2i64 end = start + Vec2i64{coordinate(random) / 4, coordinate(random) / 4};
		ASSERT_TRUE(seen.setOpenLeg(1, start, end, vision::equalRanges(radiusMm), std::nullopt));
		EXPECT_EQ(asSet(openLegRing(seen)), asSet(capsuleBeforeForestLegs(start, end, radiusMm))) << "leg " << leg;
	}
}

// A leg's ring holds every shape he walked with: while the ranges hold it is the hull of the ring as it
// stood when they began and their shape where he stands; when they change, the ring so far is kept and
// the new shape joins it where he is. The inherited shape, read only on the call that opens the leg, is
// in the ring too.
TEST(SeenArea, ALegKeepsItsRingWhenItsRangesChange) {
	SeenArea				  seen;
	const vision::SightRanges first		= rangesWithDip(29950, 4, 6, 3000);
	const vision::SightRanges second	= rangesWithDip(29950, 36, 8, 4000);
	const Vec2i64			  start		= metres(0.0, 0.0);
	const Vec2i64			  inherited = metres(-0.3, 0.1);
	const vision::SightRanges drawn		= vision::equalRanges(29000);
	ASSERT_TRUE(seen.setOpenLeg(7, start, start, first, SeenArea::InheritedShape{inherited, drawn}));
	EXPECT_TRUE(holdsRing(openLegRing(seen), geometry::forestShape(inherited, drawn)));
	ASSERT_TRUE(seen.setOpenLeg(7, start, metres(2.0, 0.0), first, std::nullopt));
	ASSERT_TRUE(seen.setOpenLeg(7, start, metres(3.0, 0.0), second, std::nullopt));
	ASSERT_TRUE(seen.setOpenLeg(7, start, metres(6.0, 0.0), second, std::nullopt));
	const std::vector<Vec2i64> ring = openLegRing(seen);
	EXPECT_TRUE(holdsRing(ring, geometry::forestShape(inherited, drawn))) << "the inherited shape";
	for (const double x : {0.0, 1.0, 2.0}) {
		EXPECT_TRUE(holdsRing(ring, geometry::forestShape(metres(x, 0.0), first))) << "the first ranges at " << x << " m";
	}
	for (const double x : {3.0, 4.5, 6.0}) {
		EXPECT_TRUE(holdsRing(ring, geometry::forestShape(metres(x, 0.0), second))) << "the second ranges at " << x << " m";
	}
	EXPECT_EQ(geometry::pointInPolygon(metres(36.1, 0.0), ring), geometry::PointInPolygon::Outside) << "no farther than the farthest shape reaches";
	EXPECT_EQ(geometry::pointInPolygon(metres(-30.4, 0.0), ring), geometry::PointInPolygon::Outside);
}

// A leg whose ring would pass kSoftLegMostVertices is refused and left as it was.
TEST(SeenArea, ALegRingPastItsMostVerticesIsRefused) {
	SeenArea	  seen;
	const Vec2i64 start = metres(40.0, 0.0);
	ASSERT_TRUE(seen.setOpenLeg(3, start, start, rangesWithDip(29950, 0, 3, 2000), std::nullopt));
	bool				 refused = false;
	std::vector<Vec2i64> before;
	for (int step = 1; step < 400 && !refused; ++step) {
		before = openLegRing(seen);
		// Each step round a circle with new ranges, so every shape joins the ring and adds corners.
		const double  angle = 0.05 * step;
		const Vec2i64 at	= metres(40.0 * std::cos(angle), 40.0 * std::sin(angle));
		refused			 = !seen.setOpenLeg(3, start, at, rangesWithDip(29950, static_cast<std::size_t>(step) * 5, 3, 2000 + 37 * (step % 11)), std::nullopt);
		EXPECT_LE(openLegRing(seen).size(), vision::kSoftLegMostVertices);
	}
	ASSERT_TRUE(refused) << "the ring never reached its most vertices";
	EXPECT_EQ(openLegRing(seen), before) << "a refused join leaves the ring as it was";
}

// Four pieces register a tick (sight-softening.md 6.6): a shape that adds six pieces registers four,
// then two, and while four wait no shape is cut. Covered tests go on while pieces wait: a queue of
// covered shapes behind the waiting pieces is dropped within a tick, and the pieces still register.
TEST(SeenArea, FourPiecesATickAndCoveredTestsWhilePiecesWait) {
	SeenArea seen;
	commitSpokes(seen);
	seen.update();
	const std::size_t landed = seen.ringsRegistered();
	seen.queue(geometry::sightPolygon({0, 0}, kSightMm), {0, 0});
	const Vec2i64 elsewhere = metres(100.0, 0.0);
	seen.queue(geometry::sightPolygon(elsewhere, 5000), elsewhere);
	seen.update();
	EXPECT_EQ(seen.lastTickWork().shapesTested, 2U);
	EXPECT_EQ(seen.lastTickWork().shapesCut, 1U) << "six pieces wait, four or more, so the second shape is not cut";
	EXPECT_EQ(seen.lastTickWork().piecesRegistered, 4U);
	EXPECT_EQ(seen.ringsRegistered(), landed + 4);
	EXPECT_EQ(seen.pending().piecesWaiting, 2U);
	EXPECT_EQ(seen.pending().shapesWaiting, 1U);

	// Ten shapes the record holds, queued behind two waiting pieces and a waiting shape.
	const std::size_t droppedBefore = seen.shapesDropped();
	for (int covered = 0; covered < 10; ++covered) {
		const Vec2i64 centre = metres(-2.0 + 0.4 * covered, 0.0);
		seen.queue(geometry::sightPolygon(centre, 1500), centre);
	}
	seen.update();
	EXPECT_EQ(seen.lastTickWork().shapesTested, 10U);
	EXPECT_EQ(seen.shapesDropped(), droppedBefore + 10) << "dropped within the tick";
	EXPECT_EQ(seen.lastTickWork().shapesCut, 1U) << "the waiting shape is cut once fewer than four pieces wait";
	EXPECT_EQ(seen.ringsRegistered(), landed + 7) << "the two pieces, then the second shape's one";
	EXPECT_EQ(seen.shapesPending(), 0U);
	for (const double angle : {30.0, 90.0, 150.0, 210.0, 270.0, 330.0}) {
		const double radians = angle * std::numbers::pi / 180.0;
		EXPECT_TRUE(seen.isSeen(metres(25.0 * std::cos(radians), 25.0 * std::sin(radians)))) << angle << " degrees";
	}
}

namespace {

	struct WallWalk {
		std::size_t	 pointsDropped = 0;
		std::size_t	 runsDroppedWhole = 0;
		std::size_t	 mostWaiting = 0;
		std::int64_t widestGapMm = 0; // along any walk, between consecutive swept points or build eyes
	};

	// Walkers side by side along a wall at `speed` times 2 m/s and 60 frames a second, each fed through
	// vision::SightFeed as PlayerVisibilitySystem feeds a colonist, vision building every 5th frame for a
	// walker who moved past the rebuild step: the polygon path with an opaque wall in reach, so every
	// build sweeps its walk. The gap is read from the walk itself: the build eyes and the points the
	// sweep built, each walker's sorted along his line.
	WallWalk walkBesideAWall(int walkers, double speed) {
		const std::vector<geometry::OccluderSegment> wall = {
			{metres(-200.0, 6.0), metres(600.0, 6.0)}, {metres(600.0, 6.2), metres(-200.0, 6.2)},
			{metres(600.0, 6.0), metres(600.0, 6.2)},  {metres(-200.0, 6.2), metres(-200.0, 6.0)},
		};
		const vision::SightRanges			ranges = vision::equalRanges(kSightMm);
		SeenArea							seen;
		std::vector<std::vector<std::int64_t>> swept(static_cast<std::size_t>(walkers));
		const auto walkerAt = [walkers](std::int64_t yMm) {
			return static_cast<std::size_t>(std::clamp<std::int64_t>((-1000 - yMm + 25) / 50, 0, walkers - 1));
		};
		seen.setSightBuilder([&](const Vec2i64& centre, std::int64_t, const vision::SightRanges& rim) {
			swept[walkerAt(centre.y)].push_back(centre.x);
			return SeenArea::BuiltSight{geometry::computeVisibilityPolygon(centre, rim.front(), wall), rim};
		});
		struct Walker {
			vision::SightFeed	 feed;
			Vec2i64				 at;
			Vec2i64				 eye;
			std::uint64_t		 revision = 0;
			std::vector<Vec2i64> polygon;
		};
		std::vector<Walker> walking(static_cast<std::size_t>(walkers));
		for (int index = 0; index < walkers; ++index) {
			walking[static_cast<std::size_t>(index)].at = {-4000 * index, -1000 - 50 * index};
		}
		WallWalk		   result;
		const std::int64_t stepMm = std::llround(2000.0 * speed / 60.0);
		for (int frame = 0; frame < 480; ++frame) {
			for (std::size_t index = 0; index < walking.size(); ++index) {
				Walker& walker = walking[index];
				walker.at.x += stepMm;
				const Vec2i64 moved = walker.at - walker.eye;
				if (frame % 5 == 0 && (walker.revision == 0 || geometry::dot(moved, moved) > geometry::Int128::product(500, 500))) {
					walker.eye	   = walker.at;
					walker.polygon = geometry::computeVisibilityPolygon(walker.eye, kSightMm + 2300, wall);
					++walker.revision;
					swept[index].push_back(walker.eye.x);
				}
				if (walker.revision == 0) {
					continue;
				}
				const vision::SightBuild build{
					.buildRevision	   = walker.revision,
					.polygonRevision   = walker.revision,
					.eye			   = walker.eye,
					.sightMm		   = kSightMm,
					.ranges			   = &ranges,
					.polygon		   = &walker.polygon,
					.opaqueWallInReach = true,
				};
				walker.feed.follow(seen, index, walker.at, build, vision::SightFeed::legToleranceMm(500, speed), 400);
			}
			seen.update();
			result.mostWaiting = std::max(result.mostWaiting, seen.sweepsWaiting());
		}
		for (std::size_t index = 0; index < walking.size(); ++index) {
			walking[index].feed.leave(seen, index);
		}
		drain(seen);
		result.pointsDropped	= seen.sweepPointsDropped();
		result.runsDroppedWhole = seen.sweepRunsDroppedWhole();
		for (std::vector<std::int64_t>& line : swept) {
			std::sort(line.begin(), line.end());
			for (std::size_t point = 1; point < line.size(); ++point) {
				result.widestGapMm = std::max(result.widestGapMm, line[point] - line[point - 1]);
			}
		}
		return result;
	}

} // namespace

// A full sweep queue thins each run, a walk's points between two builds' shapes, on its own, keeping
// its ends, and when no run has a point between its ends left the oldest run goes whole
// (sight-softening.md 6.6). Twelve walkers beside a wall at 1x thin nothing; at 10x, with 12 and 50
// walkers each building every pass, the points waiting never pass kSeenSweepPointsMost and runs go
// whole, so the widest gap along any walk between swept points and build eyes is the 1,667 mm between
// two builds, never wider.
TEST(SeenArea, AFullSweepQueueThinsEachRunKeepingItsEnds) {
	const WallWalk slow = walkBesideAWall(12, 1.0);
	EXPECT_EQ(slow.pointsDropped, 0U);
	EXPECT_LE(slow.widestGapMm, vision::kSeenSweepSpacingMm);
	for (const int walkers : {12, 50}) {
		const WallWalk fast = walkBesideAWall(walkers, 10.0);
		std::printf(
			"[ WallWalk ] %d walkers at 10x: %zu sweep points dropped, %zu runs whole, at most %zu waiting, the widest gap along a walk %lld mm\n",
			walkers,
			fast.pointsDropped,
			fast.runsDroppedWhole,
			fast.mostWaiting,
			static_cast<long long>(fast.widestGapMm)
		);
		EXPECT_LE(fast.mostWaiting, vision::kSeenSweepPointsMost);
		EXPECT_GT(fast.pointsDropped, 0U);
		EXPECT_LE(fast.widestGapMm, 1667) << "no wider than two builds apart";
	}
}

// The record's edge as the cover's feather reads it (sight-softening.md 6.3): every side of the
// committed union's boundary once, seen ground on its left, and nothing of a pending shape; the
// committed record alone holds a point that only a pending shape holds besides.
TEST(SeenArea, TheRecordsEdgeAndInRecordReadTheCommittedUnionAlone) {
	SeenArea seen;
	seen.commitNow(square(0.0, 0.0, 10.0), metres(0.0, 0.0));
	seen.commitNow(square(12.0, 4.0, 6.0), metres(12.0, 4.0));
	seen.queue(square(40.0, 0.0, 5.0), metres(40.0, 0.0));

	std::vector<Edge> edges;
	seen.appendEdges(metres(-100.0, -100.0), metres(100.0, 100.0), edges);
	const std::set<Edge> unique(edges.begin(), edges.end());
	EXPECT_EQ(unique.size(), edges.size()) << "each edge once";
	EXPECT_EQ(unique, boundaryOf(trianglesOf(seen)));
	for (const auto& [start, end] : edges) {
		const Vec2i64 middle{(start.x + end.x) / 2, (start.y + end.y) / 2};
		const Vec2i64 along	 = end - start;
		const double  length = std::hypot(static_cast<double>(along.x), static_cast<double>(along.y));
		const Vec2i64 left{middle.x - std::llround(static_cast<double>(along.y) * 5.0 / length), middle.y + std::llround(static_cast<double>(along.x) * 5.0 / length)};
		const Vec2i64 right{middle.x + std::llround(static_cast<double>(along.y) * 5.0 / length), middle.y - std::llround(static_cast<double>(along.x) * 5.0 / length)};
		EXPECT_TRUE(seen.inRecord(left)) << "seen ground on the left";
		EXPECT_FALSE(seen.inRecord(right));
		EXPECT_LT(std::max(start.x, end.x), metres(30.0, 0.0).x) << "no side of the pending square";
	}

	std::vector<Edge> near;
	seen.appendEdges(metres(-11.0, -1.0), metres(-9.0, 1.0), near);
	EXPECT_TRUE(std::ranges::any_of(near, [](const Edge& edge) { return edge.first.x == -10000 && edge.second.x == -10000; }))
		<< "a box meeting the west side gives it";

	EXPECT_TRUE(seen.inRecord(metres(0.0, 0.0)));
	EXPECT_TRUE(seen.inRecord(metres(10.0, 0.0))) << "on the edge";
	EXPECT_TRUE(seen.isSeen(metres(40.0, 0.0)));
	EXPECT_FALSE(seen.inRecord(metres(40.0, 0.0))) << "pending, not committed";
}

// Each registered ring's box comes out once, to the one reader that takes them, and a reader that
// never takes them leaves the record holding a bounded number of boxes that still hold every ring.
TEST(SeenArea, ChangedBoxesAreTheRegisteredRingsAndStayBounded) {
	SeenArea				   seen;
	std::vector<SeenArea::Box> boxes;
	seen.takeChangedBoxes(boxes);
	EXPECT_TRUE(boxes.empty());

	seen.commitNow(square(0.0, 0.0, 10.0), metres(0.0, 0.0));
	seen.queue(square(30.0, 0.0, 5.0), metres(30.0, 0.0));
	drain(seen);
	seen.takeChangedBoxes(boxes);
	ASSERT_EQ(boxes.size(), 2U);
	EXPECT_EQ(boxes[0].low, metres(-10.0, -10.0));
	EXPECT_EQ(boxes[0].high, metres(10.0, 10.0));
	EXPECT_EQ(boxes[1].low, metres(25.0, -5.0));
	EXPECT_EQ(boxes[1].high, metres(35.0, 5.0));
	boxes.clear();
	seen.takeChangedBoxes(boxes);
	EXPECT_TRUE(boxes.empty()) << "taken once";

	const std::size_t rings = vision::kSeenChangedBoxesMost + 20;
	for (std::size_t ring = 0; ring < rings; ++ring) {
		const double x = 100.0 + 3.0 * static_cast<double>(ring);
		seen.commitNow(square(x, 0.0, 1.0), metres(x, 0.0));
	}
	seen.takeChangedBoxes(boxes);
	EXPECT_LE(boxes.size(), vision::kSeenChangedBoxesMost);
	for (std::size_t ring = 0; ring < rings; ++ring) {
		const Vec2i64 centre = metres(100.0 + 3.0 * static_cast<double>(ring), 0.0);
		EXPECT_TRUE(std::ranges::any_of(boxes, [&centre](const SeenArea::Box& box) {
			return box.low.x <= centre.x - 1000 && box.high.x >= centre.x + 1000 && box.low.y <= -1000 && box.high.y >= 1000;
		})) << "ring " << ring;
	}
}
