#include "Visibility.h"
#include "SightPolygon.h"
#include "Buildings.test.h"
#include "../core/Vec2i64.h"
#include "../polygon/Polygon.h"
#include "../predicates/Predicates.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <iterator>
#include <numbers>
#include <optional>
#include <random>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;

namespace {

	// Distance from the observer to a ring vertex, as a double (exact at the small
	// coordinates these tests use).
	double radius(const Vec2i64& observer, const Vec2i64& p) {
		const double dx = static_cast<double>(p.x - observer.x);
		const double dy = static_cast<double>(p.y - observer.y);
		return std::sqrt(dx * dx + dy * dy);
	}

	// Ground-truth visible distance along a unit ray from `observer` in direction
	// (cx, cy): the nearest occluder intersection within the sight radius, else the
	// radius itself. Double-precision ray/segment solve; exact at |coord| <= ~200.
	double bruteForceVisibleDistance(const Vec2i64& observer, double cx, double cy, double radiusMm,
									 const std::vector<OccluderSegment>& occluders) {
		double best = radiusMm;
		const double ox = static_cast<double>(observer.x);
		const double oy = static_cast<double>(observer.y);
		for (const OccluderSegment& occ : occluders) {
			// Solve observer + t*(c) = a + u*(b - a), t >= 0, u in [0,1].
			const double ax = static_cast<double>(occ.a.x);
			const double ay = static_cast<double>(occ.a.y);
			const double bx = static_cast<double>(occ.b.x);
			const double by = static_cast<double>(occ.b.y);
			const double ex = bx - ax;
			const double ey = by - ay;
			const double denom = cx * ey - cy * ex; // cross(ray dir, edge dir)
			if (std::abs(denom) < 1e-12) {
				continue; // parallel (collinear graze handled loosely by tolerance)
			}
			const double rx = ax - ox;
			const double ry = ay - oy;
			const double t = (rx * ey - ry * ex) / denom; // distance along the ray
			const double u = (rx * cy - ry * cx) / denom; // param along the edge
			if (t >= 0.0 && u >= 0.0 && u <= 1.0 && t < best) {
				best = t;
			}
		}
		return best;
	}

	// Boundary distance of the computed polygon along a unit ray from `observer`:
	// the nearest ring-edge intersection with the ray, in double. The ring is
	// star-shaped about the observer so exactly one edge is hit per direction
	// (modulo grazing); we take the smallest positive t over all edges.
	std::optional<double> polygonBoundaryDistance(const Ring& ring, const Vec2i64& observer, double cx, double cy) {
		if (ring.size() < 3) {
			return std::nullopt;
		}
		const double ox = static_cast<double>(observer.x);
		const double oy = static_cast<double>(observer.y);
		std::optional<double> best;
		const std::size_t n = ring.size();
		for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
			const double ax = static_cast<double>(ring[j].x);
			const double ay = static_cast<double>(ring[j].y);
			const double bx = static_cast<double>(ring[i].x);
			const double by = static_cast<double>(ring[i].y);
			const double ex = bx - ax;
			const double ey = by - ay;
			const double denom = cx * ey - cy * ex;
			if (std::abs(denom) < 1e-12) {
				continue;
			}
			const double rx = ax - ox;
			const double ry = ay - oy;
			const double t = (rx * ey - ry * ex) / denom;
			const double u = (rx * cy - ry * cx) / denom;
			if (t >= 0.0 && u >= -1e-9 && u <= 1.0 + 1e-9) {
				if (!best || t < *best) {
					best = t;
				}
			}
		}
		return best;
	}

	bool isCcw(const Ring& ring) { return windingOrder(ring) == Winding::CounterClockwise; }

	// A scene made from integers alone, so every toolchain makes the same one: a building of 2 to
	// 4 by 1 to 3 rooms of 3 to 9 m, walls 0.2 m thick meeting at junctions with a 1 m gap in some,
	// turned and moved up to 40 km out; loose segments that cross, share ends, run along each other
	// and pass through the observer; the observer on open floor, on or by a wall corner, or in a gap;
	// and a sight radius of 2 to 42 m.
	struct PinnedScene {
		std::vector<OccluderSegment> occluders;
		Vec2i64						 observer;
		std::int64_t				 radius = 0;
	};

	PinnedScene pinnedScene(std::mt19937& rng) {
		PinnedScene		   scene;
		const std::int64_t originX = static_cast<std::int64_t>(rng() % 80000001) - 40000000;
		const std::int64_t originY = static_cast<std::int64_t>(rng() % 80000001) - 40000000;
		const std::int64_t cosine  = static_cast<std::int64_t>(rng() % 2049) - 1024;
		std::int64_t	   sine	   = 0;
		while ((sine + 1) * (sine + 1) + cosine * cosine <= 1024 * 1024) {
			++sine;
		}
		sine = rng() % 2 == 0 ? sine : -sine;
		auto place = [&](std::int64_t x, std::int64_t y) { return Vec2i64{originX + (cosine * x - sine * y) / 1024, originY + (sine * x + cosine * y) / 1024}; };

		std::vector<std::int64_t> columnAt{0};
		std::vector<std::int64_t> rowAt{0};
		const std::size_t		  columns = 2 + rng() % 3;
		const std::size_t		  rows	  = 1 + rng() % 3;
		for (std::size_t column = 0; column < columns; ++column) {
			columnAt.push_back(columnAt.back() + 3000 + static_cast<std::int64_t>(rng() % 6001));
		}
		for (std::size_t row = 0; row < rows; ++row) {
			rowAt.push_back(rowAt.back() + 3000 + static_cast<std::int64_t>(rng() % 6001));
		}
		std::vector<Vec2i64> corners;
		std::vector<Vec2i64> gaps;
		auto addPiece = [&](std::int64_t lowX, std::int64_t lowY, std::int64_t highX, std::int64_t highY) {
			const Vec2i64 pieceCorners[] = {place(lowX, lowY), place(highX, lowY), place(highX, highY), place(lowX, highY)};
			for (std::size_t i = 0; i < 4; ++i) {
				scene.occluders.push_back({pieceCorners[i], pieceCorners[(i + 1) % 4]});
				corners.push_back(pieceCorners[i]);
			}
		};
		// A wall along x = `at` (or y = `at` when `across`) from `from` to `to`, with a 1 m gap one time
		// in two.
		auto addWall = [&](bool across, std::int64_t at, std::int64_t from, std::int64_t to) {
			const bool		   gap	  = rng() % 2 == 0;
			const std::int64_t middle = from + 600 + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(to - from - 1200));
			auto piece = [&](std::int64_t start, std::int64_t end) {
				if (across) {
					addPiece(start, at - 100, end, at + 100);
				} else {
					addPiece(at - 100, start, at + 100, end);
				}
			};
			if (!gap) {
				piece(from - 100, to + 100);
				return;
			}
			piece(from - 100, middle - 500);
			piece(middle + 500, to + 100);
			gaps.push_back(across ? place(middle, at) : place(at, middle));
		};
		for (std::size_t line = 0; line <= columns; ++line) {
			for (std::size_t row = 0; row < rows; ++row) {
				addWall(false, columnAt[line], rowAt[row], rowAt[row + 1]);
			}
		}
		for (std::size_t line = 0; line <= rows; ++line) {
			for (std::size_t column = 0; column < columns; ++column) {
				addWall(true, rowAt[line], columnAt[column], columnAt[column + 1]);
			}
		}

		const std::uint32_t kind = rng() % 4;
		if (kind == 0) {
			const std::int64_t x = static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(columnAt.back() + 20001)) - 10000;
			const std::int64_t y = static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(rowAt.back() + 20001)) - 10000;
			scene.observer		 = place(x, y);
		} else if (kind == 1) {
			scene.observer = corners[rng() % corners.size()];
		} else if (kind == 2) {
			const Vec2i64&	   corner = corners[rng() % corners.size()];
			const std::int64_t x	  = static_cast<std::int64_t>(rng() % 601) - 300;
			const std::int64_t y	  = static_cast<std::int64_t>(rng() % 601) - 300;
			scene.observer			  = corner + Vec2i64{x, y};
		} else {
			scene.observer = gaps.empty() ? place(0, 0) : gaps[rng() % gaps.size()];
		}

		const std::size_t loose = rng() % 13;
		for (std::size_t i = 0; i < loose; ++i) {
			const std::uint32_t looseKind = rng() % 4;
			const std::int64_t	x		  = static_cast<std::int64_t>(rng() % 12001) - 6000;
			const std::int64_t	y		  = static_cast<std::int64_t>(rng() % 12001) - 6000;
			const Vec2i64		start	  = scene.observer + Vec2i64{x, y};
			if (looseKind == 0) {
				scene.occluders.push_back({start, scene.observer + Vec2i64{-x, -y}});
			} else if (looseKind == 1) {
				scene.occluders.push_back({start, scene.occluders[rng() % scene.occluders.size()].b});
			} else if (looseKind == 2) {
				const OccluderSegment& along = scene.occluders[rng() % scene.occluders.size()];
				scene.occluders.push_back({along.a, along.a + (along.b - along.a) * 2});
			} else {
				const std::int64_t endX = static_cast<std::int64_t>(rng() % 12001) - 6000;
				const std::int64_t endY = static_cast<std::int64_t>(rng() % 12001) - 6000;
				scene.occluders.push_back({start, scene.observer + Vec2i64{endX, endY}});
			}
		}
		scene.radius = 2000 + static_cast<std::int64_t>(rng() % 40001);
		return scene;
	}

} // namespace

TEST(Visibility, NoOccludersIsTheSightPolygon) {
	for (const Vec2i64 observer : {Vec2i64{0, 0}, Vec2i64{37123456, -18456789}}) {
		for (const std::int64_t radiusMm : {std::int64_t{1000}, std::int64_t{30000}}) {
			const Ring poly = computeVisibilityPolygon(observer, radiusMm, {});
			EXPECT_EQ(poly, sightPolygon(observer, radiusMm));
			EXPECT_TRUE(isCcw(poly));
			EXPECT_TRUE(simpleAround(poly, observer));
		}
	}
}

// Where a wall runs out past the rim, the polygon turns at the crossing instead of cutting
// across to the next rim vertex: the wall face is seen to within a millimetre of the rim.
TEST(Visibility, WallFaceIsSeenOutToTheRim) {
	const Vec2i64					   observer{0, 0};
	const std::vector<OccluderSegment> occ{{{1000, -40000}, {1000, 40000}}};
	const Ring						   poly = computeVisibilityPolygon(observer, 30000, occ);
	const Ring						   rim	= sightPolygon(observer, 30000);
	// Where the rim side from vertex 15 to vertex 16 (84.4 to 90 degrees) crosses x = 1000.
	const Vec2i64&	   from	   = rim[15];
	const Vec2i64&	   to	   = rim[16];
	const std::int64_t rimAtFace = from.y + (to.y - from.y) * (1000 - from.x) / (to.x - from.x);
	EXPECT_NE(pointInPolygon(Vec2i64{999, rimAtFace - 5}, poly), PointInPolygon::Outside);
	EXPECT_NE(pointInPolygon(Vec2i64{999, -(rimAtFace - 5)}, poly), PointInPolygon::Outside);
	EXPECT_EQ(pointInPolygon(Vec2i64{1001, 0}, poly), PointInPolygon::Outside);
	EXPECT_TRUE(simpleAround(poly, observer));
}

TEST(Visibility, SimpleAroundRejectsBadRings) {
	const Ring square{{-10, -10}, {10, -10}, {10, 10}, {-10, 10}};
	EXPECT_TRUE(simpleAround(square, {0, 0}));
	EXPECT_TRUE(simpleAround(square, {9, 9}));
	EXPECT_FALSE(simpleAround(square, {10, 0})) << "centre on an edge";
	EXPECT_FALSE(simpleAround(square, {20, 0})) << "centre outside";
	EXPECT_FALSE(simpleAround(Ring{{-10, 10}, {10, 10}, {10, -10}, {-10, -10}}, {0, 0})) << "clockwise";
	EXPECT_FALSE(simpleAround(Ring{{-10, -10}, {10, -10}, {-10, 10}, {10, 10}}, {0, 0})) << "a bow tie";
	EXPECT_FALSE(simpleAround(Ring{{-10, -10}, {10, -10}, {10, 10}, {-10, 10}, {-10, -10}, {10, -10}, {10, 10}, {-10, 10}}, {0, 0})) << "twice round";
	// A shadow edge: a radial step from a near corner out to the far field is allowed once.
	EXPECT_TRUE(simpleAround(Ring{{-10, -10}, {10, -10}, {10, 5}, {20, 10}, {-10, 10}}, {0, 0}));
	EXPECT_TRUE(simpleAround(Ring{{-10, -10}, {10, -10}, {4, 2}, {10, 5}, {-10, 10}}, {0, 0}));
	// A vertex a step behind its neighbour's angle, as a snapped hit can fall: still simple.
	EXPECT_TRUE(simpleAround(Ring{{-10, -10}, {10, -10}, {10, 4}, {20, 2}, {-10, 10}}, {0, 0}));
	// The same step back far enough that the next edge crosses an earlier one.
	EXPECT_FALSE(simpleAround(Ring{{-10, -10}, {10, -10}, {10, 4}, {20, -2}, {-10, 10}}, {0, 0}));
	// Simple but touching itself at a vertex.
	EXPECT_FALSE(simpleAround(Ring{{-10, -10}, {10, -10}, {0, 5}, {10, 10}, {-10, 10}, {0, 5}}, {0, -5}));
}

TEST(Visibility, SingleWallCastsShadow) {
	// Observer at origin, a wall across the +y field at y=100 spanning x in
	// [-100,100]. Points behind the wall (further in +y) are not visible.
	const Vec2i64 observer{0, 0};
	const std::int64_t radiusMm = 1000;
	std::vector<OccluderSegment> occ = {{{-100, 100}, {100, 100}}};

	Ring poly = computeVisibilityPolygon(observer, radiusMm, occ);
	ASSERT_GE(poly.size(), 3u);
	EXPECT_TRUE(isCcw(poly));

	// A point directly behind the wall is shadowed (not inside the polygon).
	EXPECT_EQ(pointInPolygon(Vec2i64{0, 500}, poly), PointInPolygon::Outside);
	// A point in front of the wall (between observer and wall) is visible.
	EXPECT_EQ(pointInPolygon(Vec2i64{0, 50}, poly), PointInPolygon::Inside);
	// A point off to the side, past the wall's extent, is still visible.
	EXPECT_EQ(pointInPolygon(Vec2i64{500, 50}, poly), PointInPolygon::Inside);

	// The near face of the wall bounds the polygon: a point just in front of the
	// wall centre is inside, the wall line itself is the boundary.
	EXPECT_EQ(pointInPolygon(Vec2i64{0, 99}, poly), PointInPolygon::Inside);
}

TEST(Visibility, ClosedRoomConfinesVisibility) {
	// A square room around the observer, walls as four segments. Everything inside
	// is visible; everything outside a wall is shadowed.
	const Vec2i64 observer{0, 0};
	const std::int64_t radiusMm = 5000;
	std::vector<OccluderSegment> occ = {
		{{-200, -200}, {200, -200}},
		{{200, -200}, {200, 200}},
		{{200, 200}, {-200, 200}},
		{{-200, 200}, {-200, -200}},
	};

	Ring poly = computeVisibilityPolygon(observer, radiusMm, occ);
	ASSERT_GE(poly.size(), 3u);
	EXPECT_TRUE(isCcw(poly));

	// Polygon stays within the room: a point well outside a wall is not visible.
	EXPECT_EQ(pointInPolygon(Vec2i64{1000, 0}, poly), PointInPolygon::Outside);
	EXPECT_EQ(pointInPolygon(Vec2i64{0, 1000}, poly), PointInPolygon::Outside);
	// A point inside the room is visible.
	EXPECT_EQ(pointInPolygon(Vec2i64{100, 100}, poly), PointInPolygon::Inside);
	EXPECT_EQ(pointInPolygon(Vec2i64{-150, 50}, poly), PointInPolygon::Inside);

	// No ring vertex escapes the room (all within the [-200,200] box, a few mm).
	for (const Vec2i64& v : poly) {
		EXPECT_LE(std::abs(v.x), 203);
		EXPECT_LE(std::abs(v.y), 203);
	}
}

TEST(Visibility, LShapedRoom) {
	// L-shaped room: a point tucked behind the inner corner is occluded, a point
	// in the open leg is visible.
	const Vec2i64 observer{50, 50};
	const std::int64_t radiusMm = 5000;
	std::vector<OccluderSegment> occ = {
		{{0, 0}, {300, 0}},
		{{300, 0}, {300, 100}},
		{{300, 100}, {100, 100}},
		{{100, 100}, {100, 300}},
		{{100, 300}, {0, 300}},
		{{0, 300}, {0, 0}},
	};

	Ring poly = computeVisibilityPolygon(observer, radiusMm, occ);
	ASSERT_GE(poly.size(), 3u);
	EXPECT_TRUE(isCcw(poly));

	// A point in the long arm, around the inner corner from the observer, is
	// occluded by the reflex wall at (100,100).
	EXPECT_EQ(pointInPolygon(Vec2i64{250, 250}, poly), PointInPolygon::Outside);
	// A point in the lower arm, in direct line of sight, is visible.
	EXPECT_EQ(pointInPolygon(Vec2i64{250, 50}, poly), PointInPolygon::Inside);
	// A point outside the room entirely is not visible.
	EXPECT_EQ(pointInPolygon(Vec2i64{400, 50}, poly), PointInPolygon::Outside);
}

TEST(Visibility, DoorwayGapLetsSightThrough) {
	// Two collinear wall segments at y=100 with a gap (doorway) over x in [-20,20].
	// A point beyond the gap, in line with it, is visible; a point beyond the solid
	// part is shadowed.
	const Vec2i64 observer{0, 0};
	const std::int64_t radiusMm = 2000;
	std::vector<OccluderSegment> occ = {
		{{-300, 100}, {-20, 100}},
		{{20, 100}, {300, 100}},
	};

	Ring poly = computeVisibilityPolygon(observer, radiusMm, occ);
	ASSERT_GE(poly.size(), 3u);
	EXPECT_TRUE(isCcw(poly));

	// Straight through the gap: a point far in +y on the centreline is visible.
	EXPECT_EQ(pointInPolygon(Vec2i64{0, 500}, poly), PointInPolygon::Inside);
	// Behind a solid part of the wall: shadowed.
	EXPECT_EQ(pointInPolygon(Vec2i64{200, 500}, poly), PointInPolygon::Outside);
	EXPECT_EQ(pointInPolygon(Vec2i64{-200, 500}, poly), PointInPolygon::Outside);
}

TEST(Visibility, ObserverOnEndpointDoesNotCrash) {
	// Observer sitting exactly on an occluder endpoint: must not crash and must
	// produce a sane (CCW, non-empty) polygon.
	const Vec2i64 observer{100, 100};
	const std::int64_t radiusMm = 1000;
	std::vector<OccluderSegment> occ = {
		{{100, 100}, {300, 100}},
		{{100, 100}, {100, 300}},
	};

	Ring poly = computeVisibilityPolygon(observer, radiusMm, occ);
	ASSERT_GE(poly.size(), 3u);
	EXPECT_TRUE(isCcw(poly));
}

TEST(Visibility, OccluderCrossingTheCircleIsClipped) {
	// A wall that runs out past the sight radius is clipped to the circle: no ring
	// vertex exceeds the radius by more than a couple mm.
	const Vec2i64 observer{0, 0};
	const std::int64_t radiusMm = 500;
	std::vector<OccluderSegment> occ = {
		{{-2000, 200}, {2000, 200}}, // far longer than the circle
	};

	Ring poly = computeVisibilityPolygon(observer, radiusMm, occ);
	ASSERT_GE(poly.size(), 3u);
	EXPECT_TRUE(isCcw(poly));
	for (const Vec2i64& v : poly) {
		EXPECT_LE(radius(observer, v), static_cast<double>(radiusMm) + 2.0);
	}
	// The far field beyond the circle is not visible.
	EXPECT_EQ(pointInPolygon(Vec2i64{0, 600}, poly), PointInPolygon::Outside);
}

TEST(Visibility, LineOfSightClearWhenUnobstructed) {
	const Vec2i64 observer{0, 0};
	std::vector<OccluderSegment> occ = {{{-100, 200}, {100, 200}}};
	// Target in front of the wall, clear.
	EXPECT_TRUE(hasLineOfSight(observer, Vec2i64{0, 100}, 1000, occ));
	// Target off to the side, clear.
	EXPECT_TRUE(hasLineOfSight(observer, Vec2i64{300, 0}, 1000, occ));
}

TEST(Visibility, LineOfSightBlockedByWall) {
	const Vec2i64 observer{0, 0};
	std::vector<OccluderSegment> occ = {{{-100, 200}, {100, 200}}};
	// Target directly behind the wall.
	EXPECT_FALSE(hasLineOfSight(observer, Vec2i64{0, 400}, 1000, occ));
}

TEST(Visibility, LineOfSightFalseBeyondRadius) {
	const Vec2i64 observer{0, 0};
	// Nothing blocks, but the target is past the sight radius.
	EXPECT_FALSE(hasLineOfSight(observer, Vec2i64{0, 1500}, 1000, {}));
	// Just within the radius: clear.
	EXPECT_TRUE(hasLineOfSight(observer, Vec2i64{0, 900}, 1000, {}));
}

TEST(Visibility, Deterministic) {
	const Vec2i64 observer{10, -5};
	const std::int64_t radiusMm = 800;
	std::vector<OccluderSegment> occ = {
		{{-100, 50}, {100, 50}},
		{{40, -80}, {40, 80}},
	};
	Ring a = computeVisibilityPolygon(observer, radiusMm, occ);
	Ring b = computeVisibilityPolygon(observer, radiusMm, occ);
	EXPECT_EQ(a, b);
	// And independent of occluder input order.
	std::vector<OccluderSegment> reordered = {occ[1], occ[0]};
	Ring c = computeVisibilityPolygon(observer, radiusMm, reordered);
	EXPECT_EQ(a, c);
}

// The ring is built from a compile-time table and integer predicates, so it is the same on
// every toolchain by construction; this pins it so a change to the construction is seen. Two
// doorway scenes, one at the origin and one 41 km out, are pinned by their vertex count and a
// hash of their vertices; CI runs this on MSVC and on clang.
TEST(Visibility, DoorwaySceneIsTheSameOnEveryToolchain) {
	struct Scene {
		Vec2i64		  offset;
		Vec2i64		  observer;
		std::size_t	  vertices;
		std::uint64_t hash;
	};
	const Scene scenes[] = {
		{{0, 0}, {-1300, -2700}, 47, 10607929458999170820ULL},
		{{37123456, -18456789}, {37122156, -18459489}, 47, 6333417564983028845ULL},
	};
	for (const Scene& scene : scenes) {
		std::vector<OccluderSegment> walls;
		for (const auto& [x0, x1] : {std::pair<std::int64_t, std::int64_t>{-20000, -500}, std::pair<std::int64_t, std::int64_t>{500, 20000}}) {
			const Vec2i64 bottomStart = scene.offset + Vec2i64{x0, 0};
			const Vec2i64 bottomEnd   = scene.offset + Vec2i64{x1, 0};
			const Vec2i64 topEnd	   = scene.offset + Vec2i64{x1, 200};
			const Vec2i64 topStart	   = scene.offset + Vec2i64{x0, 200};
			walls.push_back({bottomStart, bottomEnd});
			walls.push_back({bottomEnd, topEnd});
			walls.push_back({topEnd, topStart});
			walls.push_back({topStart, bottomStart});
		}
		const Ring	  ring = computeVisibilityPolygon(scene.observer, 30000, walls);
		std::uint64_t hash = 0xCBF29CE484222325ULL;
		for (const Vec2i64& vertex : ring) {
			hash = (hash ^ static_cast<std::uint64_t>(vertex.x)) * 0x100000001B3ULL;
			hash = (hash ^ static_cast<std::uint64_t>(vertex.y)) * 0x100000001B3ULL;
		}
		EXPECT_EQ(ring.size(), scene.vertices) << "observer (" << scene.observer.x << ", " << scene.observer.y << ")";
		EXPECT_EQ(hash, scene.hash) << "observer (" << scene.observer.x << ", " << scene.observer.y << ")";
	}
}

// The rings of 64 integer scenes (pinnedScene), pinned by vertex count and a hash of their
// vertices as the construction gave them before it read only the occluders a ray can meet
// (player-visibility.md 6.8): the cheaper construction gives the same rings, vertex for vertex.
TEST(Visibility, PinnedScenesKeepTheirRings) {
	struct Pin {
		std::size_t	  vertices;
		std::uint64_t hash;
	};
	const Pin pins[] = {
		{241, 13631086932445771888ULL},
		{123, 3708665231633123521ULL},
		{139, 1090735646773467490ULL},
		{109, 18263433503592362450ULL},
		{113, 13528743472672024613ULL},
		{174, 221463779720458841ULL},
		{165, 5723680177161265206ULL},
		{191, 4775356844391346868ULL},
		{142, 16949482533480782901ULL},
		{205, 10171609479135931951ULL},
		{74, 9750883291138837760ULL},
		{97, 17297999335735903400ULL},
		{111, 148186311004549816ULL},
		{104, 13531695761320440399ULL},
		{232, 3521690141311948356ULL},
		{173, 16396897520917093681ULL},
		{104, 14251771751548344270ULL},
		{130, 12314231805923909304ULL},
		{145, 1557636115698853981ULL},
		{129, 12757091429503954568ULL},
		{142, 8334904414526905816ULL},
		{136, 5654745975539213023ULL},
		{107, 2299930250589128827ULL},
		{158, 12558452668971398221ULL},
		{71, 17462866498189416073ULL},
		{69, 14474410487512572977ULL},
		{83, 11878891070589718066ULL},
		{111, 737211124792915998ULL},
		{131, 14928224993666910385ULL},
		{144, 5524364065757765962ULL},
		{190, 11059126147831899115ULL},
		{121, 13648960008430355664ULL},
		{135, 18431692302742704218ULL},
		{169, 12516637805617586254ULL},
		{65, 14955638052394774488ULL},
		{84, 13667381120194837796ULL},
		{125, 13899488911971121318ULL},
		{82, 7008901119070285867ULL},
		{169, 11655088787184149473ULL},
		{91, 9387644236641271013ULL},
		{147, 4181742503229211889ULL},
		{69, 4055284405286340694ULL},
		{111, 13111217984858764082ULL},
		{162, 5763959758659463511ULL},
		{97, 15661718671368433642ULL},
		{124, 8485352340819262523ULL},
		{122, 4672347361065797162ULL},
		{75, 3492194608616789320ULL},
		{118, 11937416901194380713ULL},
		{119, 5998880095165736254ULL},
		{119, 15955541804769954870ULL},
		{191, 1872746083837911356ULL},
		{157, 10639505937207113196ULL},
		{151, 14287611418616377925ULL},
		{120, 12242759394057257181ULL},
		{155, 13734247776890292415ULL},
		{115, 4680439631142568605ULL},
		{185, 14683903735811786061ULL},
		{168, 17909174517024057809ULL},
		{137, 12305483726743140464ULL},
		{172, 17092601931932926793ULL},
		{111, 1915787372665956169ULL},
		{160, 11456612267387732509ULL},
		{163, 13540172896637287516ULL},
	};
	std::mt19937 rng(0x862u);
	for (std::size_t index = 0; index < std::size(pins); ++index) {
		const PinnedScene scene = pinnedScene(rng);
		const Ring		  ring	= computeVisibilityPolygon(scene.observer, scene.radius, scene.occluders);
		std::uint64_t	  hash	= 0xCBF29CE484222325ULL;
		for (const Vec2i64& vertex : ring) {
			hash = (hash ^ static_cast<std::uint64_t>(vertex.x)) * 0x100000001B3ULL;
			hash = (hash ^ static_cast<std::uint64_t>(vertex.y)) * 0x100000001B3ULL;
		}
		EXPECT_EQ(ring.size(), pins[index].vertices) << "scene " << index;
		EXPECT_EQ(hash, pins[index].hash) << "scene " << index;
	}
}

// At a radius of a few millimetres the sight polygon's truncated vertices repeat angles (at 7 mm
// the rim starts at (7, 0), (6, 0)), so its directions do not rise strictly. The rings here are
// the construction's before it read only the occluders a ray can meet.
TEST(Visibility, MillimetreRadiiKeepTheirRings) {
	struct Pin {
		std::int64_t				 radius;
		std::vector<OccluderSegment> occluders;
		Ring						 ring;
	};
	const std::vector<OccluderSegment> walls = {
		{{1, 2}, {6, -4}}, {{-6, -2}, {4, 2}}, {{0, -6}, {-3, -1}}, {{5, 2}, {5, 6}}, {{4, 6}, {4, -1}}, {{-4, 3}, {-6, -6}},
	};
	const Pin pins[] = {
		{1, {}, {{1, 0}, {0, 1}}},
		{3, {}, {{2, 0}, {2, 2}, {-2, 2}, {-2, -2}, {1, -2}}},
		{7,
		 {},
		 {{6, 3},	{5, 3},	{5, 4},	{4, 4},	{4, 5},	{3, 5},	{3, 6},	{-3, 6}, {-3, 5}, {-4, 5}, {-4, 4}, {-5, 4}, {-5, 3}, {-6, 3},
		  {-6, -3}, {-5, -3}, {-5, -4}, {-4, -4}, {-4, -5}, {-3, -5}, {-3, -6}, {3, -6},	{3, -5},	{4, -5},	{4, -4},	{5, -4},	{5, -3},	{6, -3}}},
		{10,
		 {},
		 {{9, 3},	{8, 4},	{8, 5},	{7, 6},	{7, 7},	{6, 7},	{5, 8},	{4, 8},	{3, 9},	{-3, 9},  {-4, 8},  {-5, 8},  {-6, 7},	{-7, 7},  {-7, 6},	{-8, 5},  {-8, 4},	{-9, 3},
		  {-9, -3}, {-8, -4}, {-8, -5}, {-7, -6}, {-7, -7}, {-6, -7}, {-5, -8}, {-4, -8}, {-3, -9}, {3, -9}, {4, -8}, {5, -8}, {6, -7}, {7, -7}, {7, -6}, {8, -5}, {8, -4}, {9, -3}}},
		{7,
		 walls,
		 {{3, 0},	{2, 0},	{2, 1},	{0, 1},	{2, 6},	{-3, 6}, {-3, 5}, {-4, 5}, {-4, 4}, {-5, 4}, {-4, 3}, {-4, 2}, {-1, 0}, {-2, 0},
		  {-5, -2}, {-3, -1}, {-3, -2}, {-2, -2}, {-2, -3}, {-1, -4}, {-1, -5}, {0, -6},	{3, -6},	{3, -5},	{4, -5},	{4, -4},	{5, -4},	{5, -2}}},
	};
	for (const Pin& pin : pins) {
		EXPECT_EQ(computeVisibilityPolygon({0, 0}, pin.radius, pin.occluders), pin.ring) << "radius " << pin.radius << ", " << pin.occluders.size() << " occluders";
	}
}

// The ray along +x, where angleLess starts, through walls above and below the axis and a wall
// crossing it.
TEST(Visibility, RayAlongPlusXKeepsItsRing) {
	const std::vector<OccluderSegment> walls = {
		{{3000, 1000}, {3000, 4000}},
		{{4000, -1000}, {4000, -5000}},
		{{2000, -2000}, {6000, 2000}},
	};
	const Ring expected = {
		{6000, 2000},	{3000, 1000},	{3000, 4000},	{4195, 5593},	{3888, 5820},	{3299, 6173},	{2678, 6467},	{2031, 6698},	{1365, 6865},
		{686, 6966},	{0, 7000},		{-686, 6966},	{-1365, 6865},	{-2031, 6698},	{-2678, 6467},	{-3299, 6173},	{-3888, 5820},	{-4440, 5411},
		{-4949, 4949},	{-5411, 4440},	{-5820, 3888},	{-6173, 3299},	{-6467, 2678},	{-6698, 2031},	{-6865, 1365},	{-6966, 686},	{-7000, 0},
		{-6966, -686},	{-6865, -1365}, {-6698, -2031}, {-6467, -2678}, {-6173, -3299}, {-5820, -3888}, {-5411, -4440}, {-4949, -4949}, {-4440, -5411},
		{-3888, -5820}, {-3299, -6173}, {-2678, -6467}, {-2031, -6698}, {-1365, -6865}, {-686, -6966},	{0, -7000},		{686, -6966},	{1365, -6865},
		{2031, -6698},	{2678, -6467},	{3299, -6173},	{3888, -5820},	{4370, -5463},	{4000, -5000},	{4000, -4000},	{2000, -2000},
	};
	EXPECT_EQ(computeVisibilityPolygon({0, 0}, 7000, walls), expected);
}

TEST(OracleSweep, RandomScenesMatchBruteForce) {
	// The correctness guarantee. For hundreds of random scenes (observer + a small
	// set of occluder segments at small integer coords), compare the computed
	// polygon's boundary distance against a brute-force ground truth at many
	// uniformly-spaced angles. They must agree within a few mm everywhere.
	std::mt19937 rng(0x5151B1Eu);
	// Coordinates a few thousand mm across (still tiny relative to int64, so the
	// double oracle is exact: products of differences stay far inside the 53-bit
	// mantissa). The radius is several times larger than the occluder spread so
	// the sight circle's integer-mm grid resolves every endpoint direction
	// distinctly: at radius 3000 the angular grid is ~1/3000 rad, far finer than
	// the gaps between nearby occluder endpoints. A radius near the occluder scale
	// (e.g. 100 at coords +-50) cannot resolve those sub-pixel arcs and is a
	// grid-resolution artifact, not an implementation limit; real sight radii are
	// meters, comfortably in the resolved regime modeled here.
	std::uniform_int_distribution<std::int64_t> coord(-1000, 1000);
	std::uniform_int_distribution<int> segCount(0, 5);

	// Sized to stay thorough (tens of thousands of rays) while keeping the suite
	// snappy in CI; bump kAngles/iter for a heavier local soak.
	constexpr int kAngles = 360;
	constexpr double kTwoPi = 2.0 * std::numbers::pi;
	// Tolerance covers the mm snapping of polygon vertices and the half-degree
	// offset between the sampled angle and a sloped occluder face. Generous in
	// absolute mm because the radius is large.
	constexpr double kToleranceMm = 8.0;

	const std::int64_t radiusMm = 3000;
	int scenes = 0;
	int samples = 0;

	for (int iter = 0; iter < 300; ++iter) {
		const Vec2i64 observer{coord(rng), coord(rng)};
		const int m = segCount(rng);
		std::vector<OccluderSegment> occ;
		occ.reserve(static_cast<std::size_t>(m));
		for (int s = 0; s < m; ++s) {
			Vec2i64 a{coord(rng), coord(rng)};
			Vec2i64 b{coord(rng), coord(rng)};
			if (a == b) {
				continue; // skip degenerate zero-length occluders
			}
			occ.push_back({a, b});
		}

		// Skip near-graze scenes: an occluder passing within a small clearance of
		// the observer is the "eye inside the wall" degeneracy. Distances to such a
		// near occluder are a few mm, where the integer grid cannot resolve the
		// boundary and where, physically, the query is meaningless. No real
		// perception puts the observer that close to an opaque edge. Exact predicate.
		constexpr std::int64_t kObserverClearanceMm = 150;
		bool tooClose = false;
		for (const OccluderSegment& o : occ) {
			if (withinDistanceOfSegment(observer, o.a, o.b, kObserverClearanceMm)) {
				tooClose = true;
				break;
			}
		}
		if (tooClose) {
			continue;
		}

		Ring poly = computeVisibilityPolygon(observer, radiusMm, occ);
		ASSERT_GE(poly.size(), 3u) << "iter " << iter;
		EXPECT_TRUE(isCcw(poly)) << "iter " << iter;
		++scenes;

		for (int k = 0; k < kAngles; ++k) {
			const double ang = static_cast<double>(k) * (kTwoPi / static_cast<double>(kAngles));
			const double cx = std::cos(ang);
			const double cy = std::sin(ang);

			const double truth = bruteForceVisibleDistance(observer, cx, cy, static_cast<double>(radiusMm), occ);
			const std::optional<double> got = polygonBoundaryDistance(poly, observer, cx, cy);

			ASSERT_TRUE(got.has_value()) << "iter " << iter << " angle " << k << ": ray missed the ring";

			// Skip degenerate sub-mm truths: an occluder line passing within a
			// couple mm of the observer (the observer is essentially standing in
			// the wall). The brute-force solve finds the sub-mm intersection of two
			// ideal primitives; the integer-mm polygon cannot represent distances
			// below the grid, so the comparison is meaningless there.
			if (truth < 3.0) {
				continue;
			}

			// Skip angles where the true visible distance changes steeply across a
			// sample's width. This covers two cases that both make a single radial
			// sample an unfair test of a piecewise-linear polygon boundary: a genuine
			// discontinuity (an occluder endpoint on the ray, stepping between the
			// near face and the far field), and a close occluder face whose radial
			// distance swings fast with angle so that mid-edge the polygon chord
			// legitimately sits inside the radial truth. Both are detected the same
			// way: bracket the sample by half the sample spacing on each side and skip
			// if the truth moves by more than the tolerance. The dense 720-angle sweep
			// keeps the flat majority of directions, which is the correctness
			// guarantee; the skipped steep directions are validated by their flat
			// neighbours bracketing them within tolerance.
			const double dA = 0.5 * (kTwoPi / static_cast<double>(kAngles)); // half a sample
			const double truthLo =
				bruteForceVisibleDistance(observer, std::cos(ang - dA), std::sin(ang - dA), static_cast<double>(radiusMm), occ);
			const double truthHi =
				bruteForceVisibleDistance(observer, std::cos(ang + dA), std::sin(ang + dA), static_cast<double>(radiusMm), occ);
			if (std::abs(truthLo - truth) > kToleranceMm || std::abs(truthHi - truth) > kToleranceMm) {
				continue;
			}

			EXPECT_NEAR(*got, truth, kToleranceMm)
				<< "iter " << iter << " angle index " << k << " (rad " << ang << ")"
				<< " observer (" << observer.x << "," << observer.y << ")";
			++samples;
		}
	}

	EXPECT_GT(scenes, 100);		  // a meaningful number of random scenes actually ran
	EXPECT_GT(samples, 40000);	  // and a deep sweep of rays per scene was checked
}

// Observers in and round random buildings: on open floor, a few millimetres off wall corners,
// and in doorways. simpleAround never passes a ring an exact pairwise test finds not simple;
// the count it turns away is printed (the bench runs the same fuzz at scale).
TEST(Visibility, StarCheckIsSoundOnBuildings) {
	std::mt19937 rng(0xD00Du);
	int			 polygons = 0;
	int			 rejected = 0;
	int			 notSimple = 0;
	for (int scene = 0; scene < 6; ++scene) {
		const buildings::Building building = buildings::randomBuilding(rng);
		for (const Vec2i64& observer : buildings::observersFor(building, rng, 25)) {
			const Ring ring = computeVisibilityPolygon(observer, 30000, building.walls);
			++polygons;
			bool simple = ring.size() >= 3;
			for (std::size_t i = 0; i < ring.size() && simple; ++i) {
				for (std::size_t j = i + 1; j < ring.size() && simple; ++j) {
					const SegmentRelation relation = intersectSegments(ring[i], ring[(i + 1) % ring.size()], ring[j], ring[(j + 1) % ring.size()]).relation;
					const bool			  adjacent = j == i + 1 || (i == 0 && j == ring.size() - 1);
					simple = relation == SegmentRelation::Disjoint || (adjacent && relation == SegmentRelation::EndpointTouch);
				}
			}
			notSimple += simple ? 0 : 1;
			rejected += simpleAround(ring, observer) ? 0 : 1;
			EXPECT_EQ(simpleAround(ring, observer), simple) << "observer (" << observer.x << ", " << observer.y << ")";
		}
	}
	std::printf("[visibility simple check] %d polygons: %d turned away by simpleAround, %d not simple by every pair\n", polygons, rejected, notSimple);
	EXPECT_GE(polygons, 140);
}
