#include "ConstrainedDelaunay.h"
#include "../core/Vec2i64.h"
#include "../predicates/Predicates.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;

namespace {

	using Constraint = std::array<std::uint32_t, 2>;

	bool onClosedSegment(const Vec2i64& p, const Vec2i64& a, const Vec2i64& b) {
		return orientation(a, b, p) == Orientation::Collinear && p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x) &&
			   p.y >= std::min(a.y, b.y) && p.y <= std::max(a.y, b.y);
	}

	// Checks everything the triangulation promises and returns a description of
	// the first violation, or "" when valid:
	//  - every triangle is CCW with positive area;
	//  - each directed edge appears once, every interior edge is matched by its
	//    reverse in the recorded neighbor with the same constraint tag, and the only
	//    unmatched edges are the four frame sides. With every triangle positive, that
	//    makes the triangles tile the frame exactly once (the chain's boundary is the
	//    frame's), which the area sum then confirms;
	//  - every constrained edge lies on its constraint, and each constraint is
	//    covered from end to end by edges carrying its index;
	//  - every unconstrained interior edge is locally Delaunay.
	std::string violation(const ConstrainedDelaunay& cdt, const std::vector<Constraint>& constraints) {
		const std::vector<Vec2i64>& v = cdt.vertices;
		const std::uint32_t			f = cdt.frameStart;
		if (v.size() != static_cast<std::size_t>(f) + 4) {
			return "vertex count is not input + 4 frame corners";
		}
		std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<std::int32_t, int>> directed;
		Int128																			area2(0);
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(cdt.triangles.size()); ++t) {
			const CdtTriangle& tri = cdt.triangles[static_cast<std::size_t>(t)];
			const Int128	   a2  = cross(v[tri.v[1]] - v[tri.v[0]], v[tri.v[2]] - v[tri.v[0]]);
			if (a2.sign() <= 0) {
				return "triangle " + std::to_string(t) + " is not CCW with positive area";
			}
			area2 = area2 + a2;
			for (int i = 0; i < 3; ++i) {
				if (!directed.emplace(std::make_pair(tri.v[i], tri.v[(i + 1) % 3]), std::make_pair(t, i)).second) {
					return "directed edge used twice";
				}
			}
		}
		const Vec2i64 lo = v[f];
		const Vec2i64 hi = v[f + 2];
		if (!(area2 == Int128::product(2 * (hi.x - lo.x), hi.y - lo.y))) {
			return "triangle areas do not sum to the frame's";
		}
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(cdt.triangles.size()); ++t) {
			const CdtTriangle& tri = cdt.triangles[static_cast<std::size_t>(t)];
			for (int i = 0; i < 3; ++i) {
				const std::uint32_t a	 = tri.v[i];
				const std::uint32_t b	 = tri.v[(i + 1) % 3];
				const auto			back = directed.find({b, a});
				if (back == directed.end()) {
					const bool frameSide = a >= f && b >= f && (b - f) == ((a - f) + 1) % 4;
					if (!frameSide || tri.neighbor[i] != -1) {
						return "unmatched edge off the frame";
					}
					continue;
				}
				if (tri.neighbor[i] != back->second.first) {
					return "neighbor disagrees with the edge's reverse";
				}
				const CdtTriangle& other = cdt.triangles[static_cast<std::size_t>(back->second.first)];
				if (other.neighbor[back->second.second] != t || other.constraint[back->second.second] != tri.constraint[i]) {
					return "the two sides of an edge disagree";
				}
				const std::int32_t c = tri.constraint[i];
				if (c >= 0) {
					const Constraint& k = constraints[static_cast<std::size_t>(c)];
					if (!onClosedSegment(v[a], v[k[0]], v[k[1]]) || !onClosedSegment(v[b], v[k[0]], v[k[1]])) {
						return "edge tagged with a constraint it does not lie on";
					}
				} else {
					const std::uint32_t far = other.v[(back->second.second + 2) % 3];
					if (inCircle(v[tri.v[0]], v[tri.v[1]], v[tri.v[2]], v[far]) == InCircle::Inside) {
						return "unconstrained edge is not locally Delaunay";
					}
				}
			}
		}
		for (std::size_t c = 0; c < constraints.size(); ++c) {
			// Walk from one end to the other along edges tagged c.
			std::uint32_t cur	= constraints[c][0];
			const auto	  end	= constraints[c][1];
			int			  guard = 0;
			while (cur != end && guard++ < 100000) {
				bool advanced = false;
				for (const auto& [edge, owner] : directed) {
					if (edge.first != cur) {
						continue;
					}
					const CdtTriangle& tri = cdt.triangles[static_cast<std::size_t>(owner.first)];
					if (tri.constraint[owner.second] == static_cast<std::int32_t>(c) &&
						dot(v[edge.second] - v[cur], v[end] - v[cur]).sign() > 0) {
						cur		 = edge.second;
						advanced = true;
						break;
					}
				}
				if (!advanced) {
					return "constraint " + std::to_string(c) + " is not covered end to end";
				}
			}
		}
		return "";
	}

	// Distinct random points plus random segments between them that meet only at
	// shared endpoints and pass through no other point: a planar straight-line graph.
	struct Pslg {
		std::vector<Vec2i64>	points;
		std::vector<Constraint> segments;
	};

	Pslg randomPslg(std::uint32_t seed, int pointCount, int segmentTries, std::int64_t extent) {
		std::mt19937								rng(seed);
		std::uniform_int_distribution<std::int64_t> coord(-extent, extent);
		Pslg										g;
		std::set<Vec2i64>							seen;
		while (static_cast<int>(g.points.size()) < pointCount) {
			const Vec2i64 p{coord(rng), coord(rng)};
			if (seen.insert(p).second) {
				g.points.push_back(p);
			}
		}
		std::uniform_int_distribution<std::size_t> pick(0, g.points.size() - 1);
		for (int tries = 0; tries < segmentTries; ++tries) {
			const std::uint32_t a = static_cast<std::uint32_t>(pick(rng));
			const std::uint32_t b = static_cast<std::uint32_t>(pick(rng));
			if (a == b) {
				continue;
			}
			bool ok = true;
			for (std::size_t p = 0; p < g.points.size() && ok; ++p) {
				ok = p == a || p == b || !onClosedSegment(g.points[p], g.points[a], g.points[b]);
			}
			for (const Constraint& s : g.segments) {
				if (!ok) {
					break;
				}
				const SegmentIntersection r = intersectSegments(g.points[a], g.points[b], g.points[s[0]], g.points[s[1]]);
				const bool shared = s[0] == a || s[0] == b || s[1] == a || s[1] == b;
				ok = r.relation == SegmentRelation::Disjoint || (r.relation == SegmentRelation::EndpointTouch && shared);
			}
			if (ok) {
				g.segments.push_back({a, b});
			}
		}
		return g;
	}

	std::vector<Constraint> ringConstraints(std::uint32_t first, std::uint32_t count) {
		std::vector<Constraint> out;
		for (std::uint32_t i = 0; i < count; ++i) {
			out.push_back({first + i, first + (i + 1) % count});
		}
		return out;
	}

} // namespace

TEST(ConstrainedDelaunay, EmptyInputHasNoTriangles) {
	const ConstrainedDelaunay cdt = buildConstrainedDelaunay({}, {});
	EXPECT_TRUE(cdt.triangles.empty());
}

TEST(ConstrainedDelaunay, SinglePointSplitsTheFrame) {
	const std::vector<Vec2i64> pts = {{5, 7}};
	const ConstrainedDelaunay  cdt = buildConstrainedDelaunay(pts, {});
	EXPECT_EQ(violation(cdt, {}), "");
	EXPECT_EQ(cdt.frameStart, 1u);
}

// A thin kite whose Delaunay diagonal is the short one; constraining the long one
// must keep it anyway.
TEST(ConstrainedDelaunay, ConstrainedEdgeOverridesDelaunayChoice) {
	const std::vector<Vec2i64>	  pts = {{0, 100}, {100, 0}, {200, 100}, {100, 130}};
	const std::vector<Constraint> con = {{0, 2}};
	const ConstrainedDelaunay	  cdt = buildConstrainedDelaunay(pts, con);
	EXPECT_EQ(violation(cdt, con), "");

	const ConstrainedDelaunay free = buildConstrainedDelaunay(pts, {});
	EXPECT_EQ(violation(free, {}), "");
	bool hasShort = false;
	for (const CdtTriangle& t : free.triangles) {
		for (int i = 0; i < 3; ++i) {
			hasShort = hasShort || (std::min(t.v[i], t.v[(i + 1) % 3]) == 1 && std::max(t.v[i], t.v[(i + 1) % 3]) == 3);
		}
	}
	EXPECT_TRUE(hasShort) << "without the constraint the Delaunay diagonal is the short one";
}

TEST(ConstrainedDelaunay, RandomPointsAreDelaunay) {
	for (std::uint32_t seed : {1u, 2u, 3u}) {
		const Pslg				  g	  = randomPslg(seed, 300, 0, 100000);
		const ConstrainedDelaunay cdt = buildConstrainedDelaunay(g.points, {});
		EXPECT_EQ(violation(cdt, {}), "") << "seed " << seed;
	}
}

TEST(ConstrainedDelaunay, RandomPlanarGraphs) {
	for (std::uint32_t seed = 10; seed < 210; ++seed) {
		const Pslg				  g	  = randomPslg(seed, 120, 400, 5000);
		const ConstrainedDelaunay cdt = buildConstrainedDelaunay(g.points, g.segments);
		EXPECT_EQ(violation(cdt, g.segments), "") << "seed " << seed << ", " << g.segments.size() << " segments";
	}
}

// Tiny coordinates make collinear and cocircular points common.
TEST(ConstrainedDelaunay, RandomPlanarGraphsOnACoarseLattice) {
	for (std::uint32_t seed = 40; seed < 440; ++seed) {
		const Pslg				  g	  = randomPslg(seed, 60, 200, 8);
		const ConstrainedDelaunay cdt = buildConstrainedDelaunay(g.points, g.segments);
		EXPECT_EQ(violation(cdt, g.segments), "") << "seed " << seed;
	}
}

// A lattice is maximally degenerate: every cell's corners are cocircular and every
// row, column, and diagonal is collinear. Constraints run along whole rows and
// columns, through the lattice points they pass, and along diagonals.
TEST(ConstrainedDelaunay, LatticeWithConstraintsThroughCollinearVertices) {
	std::vector<Vec2i64> pts;
	for (std::int64_t y = 0; y < 12; ++y) {
		for (std::int64_t x = 0; x < 12; ++x) {
			pts.push_back({x * 100, y * 100});
		}
	}
	auto at = [](std::uint32_t x, std::uint32_t y) { return y * 12 + x; };
	const std::vector<Constraint> con = {
		{at(0, 3), at(11, 3)}, {at(2, 0), at(2, 11)}, {at(0, 0), at(11, 11)}, {at(10, 0), at(4, 6)}, {at(5, 9), at(9, 9)},
		{at(9, 1), at(9, 6)},
	};
	const ConstrainedDelaunay cdt = buildConstrainedDelaunay(pts, con);
	EXPECT_EQ(violation(cdt, con), "");
}

// Two squares sharing one corner, and a triangle whose apex rests on a square's
// edge: the pinches and T-junctions navmesh input produces. The edge the apex
// rests on splits there, both halves keeping its constraint index.
TEST(ConstrainedDelaunay, RingsTouchingAtSinglePoints) {
	const std::vector<Vec2i64> pts = {
		{0, 0},		  {1000, 0},	{1000, 1000}, {0, 1000},	 // square, 0..3
		{2000, 1000}, {2000, 2000}, {1000, 2000},				 // square sharing corner 2, 4..6
		{500, 0},	  {800, -400},	{200, -400},				 // triangle, apex on edge 0-1, 7..9
	};
	std::vector<Constraint> con = ringConstraints(0, 4);
	for (const Constraint& c : std::vector<Constraint>{{2, 4}, {4, 5}, {5, 6}, {6, 2}, {7, 8}, {8, 9}, {9, 7}}) {
		con.push_back(c);
	}
	const ConstrainedDelaunay cdt = buildConstrainedDelaunay(pts, con);
	EXPECT_EQ(violation(cdt, con), "");
}

// One long constraint across a dense field of points must clear and rebuild a large
// cavity.
TEST(ConstrainedDelaunay, LongConstraintThroughDenseField) {
	Pslg g = randomPslg(77, 2000, 0, 20000);
	g.points.push_back({-30000, -1});
	g.points.push_back({30000, 3});
	const std::uint32_t			  n	  = static_cast<std::uint32_t>(g.points.size());
	const std::vector<Constraint> con = {{n - 2, n - 1}};
	const ConstrainedDelaunay	  cdt = buildConstrainedDelaunay(g.points, con);
	EXPECT_EQ(violation(cdt, con), "");
}

// Exactness depends only on coordinate differences: a region 70 km out triangulates
// exactly like the same region at the origin.
TEST(ConstrainedDelaunay, TranslationInvariantAtWorldScale) {
	const Pslg g = randomPslg(5, 150, 300, 60000);
	Pslg	   far = g;
	for (Vec2i64& p : far.points) {
		p = p + Vec2i64{70000000, -3000000};
	}
	const ConstrainedDelaunay near = buildConstrainedDelaunay(g.points, g.segments);
	const ConstrainedDelaunay out  = buildConstrainedDelaunay(far.points, far.segments);
	EXPECT_EQ(violation(out, far.segments), "");
	ASSERT_EQ(near.triangles.size(), out.triangles.size());
	for (std::size_t t = 0; t < near.triangles.size(); ++t) {
		EXPECT_EQ(near.triangles[t].v, out.triangles[t].v);
		EXPECT_EQ(near.triangles[t].constraint, out.triangles[t].constraint);
	}
}

TEST(ConstrainedDelaunay, Deterministic) {
	const Pslg				  g = randomPslg(9, 400, 800, 30000);
	const ConstrainedDelaunay a = buildConstrainedDelaunay(g.points, g.segments);
	const ConstrainedDelaunay b = buildConstrainedDelaunay(g.points, g.segments);
	ASSERT_EQ(a.triangles.size(), b.triangles.size());
	EXPECT_EQ(a.vertices, b.vertices);
	for (std::size_t t = 0; t < a.triangles.size(); ++t) {
		EXPECT_EQ(a.triangles[t].v, b.triangles[t].v);
		EXPECT_EQ(a.triangles[t].neighbor, b.triangles[t].neighbor);
		EXPECT_EQ(a.triangles[t].constraint, b.triangles[t].constraint);
	}
}

// The second segment passes just below vertex 0 and leaves and re-enters the star
// of the first constraint's far end, so its cavity wraps around the first
// constraint without crossing it. Rebuilding that hanging edge must keep its
// constraint index. (Reduced from a random planar graph that lost it.)
TEST(ConstrainedDelaunay, CavityAroundHangingConstraintKeepsIt) {
	const std::vector<Vec2i64>	  pts = {{358, 4269}, {-486, 3594}, {-3698, 4121}, {473, 4976}, {1497, 3726}, {3039, 4251}};
	const std::vector<Constraint> con = {{0, 3}, {2, 5}};
	const ConstrainedDelaunay	  cdt = buildConstrainedDelaunay(pts, con);
	EXPECT_EQ(violation(cdt, con), "");
}
// All input on one line: the frame still gives a full triangulation.
TEST(ConstrainedDelaunay, CollinearInput) {
	std::vector<Vec2i64> pts;
	for (std::int64_t i = 0; i < 20; ++i) {
		pts.push_back({i * 37, i * 11});
	}
	const std::vector<Constraint> con = {{0, 19}};
	const ConstrainedDelaunay	  cdt = buildConstrainedDelaunay(pts, con);
	EXPECT_EQ(violation(cdt, con), "");
}
