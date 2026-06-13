#include "HalfEdge.h"
#include "Arrangement.h"
#include "../core/Vec2i64.h"

#include <algorithm>
#include <cstdint>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;

namespace {

	std::size_t countBounded(const HalfEdgeMesh& mesh) {
		std::size_t n = 0;
		for (const Face& f : mesh.faces) {
			if (!f.outer) {
				++n;
			}
		}
		return n;
	}

	std::size_t countOuter(const HalfEdgeMesh& mesh) {
		std::size_t n = 0;
		for (const Face& f : mesh.faces) {
			if (f.outer) {
				++n;
			}
		}
		return n;
	}

	Arrangement square(std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1, std::int64_t base) {
		std::vector<InputSegment> segs = {
			{{x0, y0}, {x1, y0}, base + 0},
			{{x1, y0}, {x1, y1}, base + 1},
			{{x1, y1}, {x0, y1}, base + 2},
			{{x0, y1}, {x0, y0}, base + 3},
		};
		return buildArrangement(segs);
	}

} // namespace

TEST(AngleLess, AllEightOctantsOrderedCcwFromPlusX) {
	const Vec2i64 e{1000, 0};	  // 0
	const Vec2i64 ne{1000, 1000}; // 45
	const Vec2i64 n{0, 1000};	  // 90
	const Vec2i64 nw{-1000, 1000};// 135
	const Vec2i64 w{-1000, 0};	  // 180
	const Vec2i64 sw{-1000, -1000};// 225
	const Vec2i64 s{0, -1000};	  // 270
	const Vec2i64 se{1000, -1000};// 315

	std::vector<Vec2i64> dirs = {n, sw, e, se, w, ne, s, nw};
	std::sort(dirs.begin(), dirs.end(), angleLess);
	std::vector<Vec2i64> expected = {e, ne, n, nw, w, sw, s, se};
	EXPECT_EQ(dirs, expected);
}

TEST(AngleLess, CollinearOppositeDirectionsSeparate) {
	// +x and -x must order strictly (different half-planes), as must +y and -y.
	EXPECT_TRUE(angleLess({1000, 0}, {-1000, 0}));
	EXPECT_FALSE(angleLess({-1000, 0}, {1000, 0}));
	EXPECT_TRUE(angleLess({1000, 0}, {0, -1000}));
	EXPECT_TRUE(angleLess({0, 1000}, {0, -1000}));
}

TEST(Faces, SquareOneBoundedOneOuter) {
	HalfEdgeMesh mesh = extractFaces(square(0, 0, 1000, 1000, 0));
	EXPECT_EQ(countBounded(mesh), 1u);
	EXPECT_EQ(countOuter(mesh), 1u);
	EXPECT_EQ(mesh.halfEdges.size(), 8u); // 4 edges * 2

	for (const Face& f : mesh.faces) {
		if (!f.outer) {
			// CCW square of side 1000 -> signed doubled area = +2,000,000.
			EXPECT_EQ(f.signedAreaDoubled.sign(), 1);
			EXPECT_TRUE(f.signedAreaDoubled == Int128(2000000));
			ASSERT_TRUE(f.representativePoint.has_value());
			EXPECT_EQ(pointInPolygon(*f.representativePoint, {{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}}),
					  PointInPolygon::Inside);
		} else {
			EXPECT_EQ(f.signedAreaDoubled.sign(), -1);
			EXPECT_FALSE(f.representativePoint.has_value());
		}
	}
}

TEST(Faces, TwoSquaresSharingEdgeTwoBoundedFaces) {
	std::vector<InputSegment> segs = {
		{{0, 0}, {1000, 0}, 0},
		{{1000, 0}, {1000, 1000}, 1},
		{{1000, 1000}, {0, 1000}, 2},
		{{0, 1000}, {0, 0}, 3},
		{{1000, 0}, {2000, 0}, 4},
		{{2000, 0}, {2000, 1000}, 5},
		{{2000, 1000}, {1000, 1000}, 6},
		{{1000, 1000}, {1000, 0}, 7},
	};
	HalfEdgeMesh mesh = extractFaces(buildArrangement(segs));
	EXPECT_EQ(countBounded(mesh), 2u); // two rooms
	EXPECT_EQ(countOuter(mesh), 1u);   // one shared outer boundary

	// The shared edge is crossed by the two bounded faces (twin faces differ and
	// neither is outer for that half-edge pair).
	std::size_t sharedHe = SIZE_MAX;
	for (std::size_t h = 0; h < mesh.halfEdges.size(); ++h) {
		const Vec2i64& o = mesh.vertices[mesh.halfEdges[h].origin];
		const Vec2i64& t = mesh.vertices[mesh.halfEdges[h].target];
		if ((o == Vec2i64{1000, 0} && t == Vec2i64{1000, 1000})) {
			sharedHe = h;
		}
	}
	ASSERT_NE(sharedHe, SIZE_MAX);
	const std::size_t fA = mesh.halfEdges[sharedHe].face;
	const std::size_t fB = mesh.twinFace(sharedHe);
	EXPECT_NE(fA, fB);
	EXPECT_FALSE(mesh.faces[fA].outer);
	EXPECT_FALSE(mesh.faces[fB].outer);
}

TEST(Faces, XCrossingDegreeFourVertex) {
	std::vector<InputSegment> segs = {
		{{0, 0}, {1000, 1000}, 0},
		{{0, 1000}, {1000, 0}, 1},
	};
	HalfEdgeMesh mesh = extractFaces(buildArrangement(segs));
	// The center has degree 4: four outgoing half-edges.
	std::size_t center = SIZE_MAX;
	for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
		if (mesh.vertices[i] == Vec2i64{500, 500}) {
			center = i;
		}
	}
	ASSERT_NE(center, SIZE_MAX);
	std::size_t deg = 0;
	for (const HalfEdge& he : mesh.halfEdges) {
		if (he.origin == center) {
			++deg;
		}
	}
	EXPECT_EQ(deg, 4u);
	EXPECT_EQ(mesh.facesAtVertex(center).size(), mesh.faces.size()); // all faces touch center
}

TEST(Faces, SignedAreasMatchShoelace) {
	HalfEdgeMesh mesh = extractFaces(square(0, 0, 2000, 3000, 0));
	bool sawBounded = false;
	for (const Face& f : mesh.faces) {
		if (!f.outer) {
			sawBounded = true;
			// 2000 x 3000 = 6,000,000 area -> doubled = 12,000,000.
			EXPECT_TRUE(f.signedAreaDoubled == Int128(12000000));
		}
	}
	EXPECT_TRUE(sawBounded);
}

TEST(Faces, ProvenanceCarriesThroughToFaces) {
	HalfEdgeMesh mesh = extractFaces(square(0, 0, 1000, 1000, 100));
	for (const Face& f : mesh.faces) {
		if (!f.outer) {
			// Bounded face touches all four input segments 100..103.
			EXPECT_EQ(f.provenance, (std::vector<std::int64_t>{100, 101, 102, 103}));
		}
	}
}

TEST(Faces, GridFourBoundedFaces) {
	std::vector<InputSegment> segs;
	std::int64_t			  idx = 0;
	for (std::int64_t y : {0, 500, 1000}) {
		segs.push_back({{0, y}, {1000, y}, idx++});
	}
	for (std::int64_t x : {0, 500, 1000}) {
		segs.push_back({{x, 0}, {x, 1000}, idx++});
	}
	HalfEdgeMesh mesh = extractFaces(buildArrangement(segs));
	EXPECT_EQ(countBounded(mesh), 4u); // 2x2 cells
	EXPECT_EQ(countOuter(mesh), 1u);
}

TEST(Faces, PointInCycleContainment) {
	HalfEdgeMesh mesh = extractFaces(square(0, 0, 1000, 1000, 0));
	std::size_t bounded = SIZE_MAX;
	for (std::size_t i = 0; i < mesh.faces.size(); ++i) {
		if (!mesh.faces[i].outer) {
			bounded = i;
		}
	}
	ASSERT_NE(bounded, SIZE_MAX);
	EXPECT_EQ(pointInCycle({500, 500}, mesh, bounded), PointInPolygon::Inside);
	EXPECT_EQ(pointInCycle({1500, 500}, mesh, bounded), PointInPolygon::Outside);
	EXPECT_EQ(pointInCycle({0, 500}, mesh, bounded), PointInPolygon::OnBoundary);
}

TEST(Faces, RepresentativePointInsideNonConvexFace) {
	// An L-shaped room: representative point must land inside the concave region.
	std::vector<InputSegment> segs = {
		{{0, 0}, {2000, 0}, 0},
		{{2000, 0}, {2000, 1000}, 1},
		{{2000, 1000}, {1000, 1000}, 2},
		{{1000, 1000}, {1000, 2000}, 3},
		{{1000, 2000}, {0, 2000}, 4},
		{{0, 2000}, {0, 0}, 5},
	};
	HalfEdgeMesh mesh = extractFaces(buildArrangement(segs));
	ASSERT_EQ(countBounded(mesh), 1u);
	for (const Face& f : mesh.faces) {
		if (!f.outer) {
			ASSERT_TRUE(f.representativePoint.has_value());
			EXPECT_EQ(pointInCycle(*f.representativePoint, mesh, &f - mesh.faces.data()),
					  PointInPolygon::Inside);
		}
	}
}

TEST(AngleLess, StrictWeakOrderingExhaustive) {
	// A broken comparator is undefined behavior in std::sort. Verify the three
	// strict-weak-ordering axioms over a dense set of integer directions: every
	// nonzero vector in a grid, which covers all angle classes and many collinear
	// duplicates per direction.
	std::vector<Vec2i64> dirs;
	for (std::int64_t y = -7; y <= 7; ++y) {
		for (std::int64_t x = -7; x <= 7; ++x) {
			if (x == 0 && y == 0) {
				continue; // zero vector is not a valid input
			}
			dirs.push_back({x, y});
		}
	}

	auto equiv = [](const Vec2i64& a, const Vec2i64& b) { return !angleLess(a, b) && !angleLess(b, a); };

	// Irreflexivity.
	for (const Vec2i64& a : dirs) {
		ASSERT_FALSE(angleLess(a, a)) << a.x << "," << a.y;
	}

	// Asymmetry and a consistent equivalence (transitivity of equivalence plus
	// transitivity of <), checked over all triples.
	for (std::size_t i = 0; i < dirs.size(); ++i) {
		for (std::size_t j = 0; j < dirs.size(); ++j) {
			const bool ltij = angleLess(dirs[i], dirs[j]);
			const bool ltji = angleLess(dirs[j], dirs[i]);
			ASSERT_FALSE(ltij && ltji) << "asymmetry " << i << "," << j;
			for (std::size_t k = 0; k < dirs.size(); ++k) {
				// Transitivity of <.
				if (ltij && angleLess(dirs[j], dirs[k])) {
					ASSERT_TRUE(angleLess(dirs[i], dirs[k])) << "transitivity i<j<k";
				}
				// Transitivity of equivalence.
				if (equiv(dirs[i], dirs[j]) && equiv(dirs[j], dirs[k])) {
					ASSERT_TRUE(equiv(dirs[i], dirs[k])) << "transitivity of equiv";
				}
			}
		}
	}
}

TEST(AngleLess, EquivalenceMeansSameDirection) {
	// Two directions compare equivalent under angleLess iff they point the same
	// way (same ray from origin). Distinct angles must order strictly one way.
	std::vector<Vec2i64> dirs;
	for (std::int64_t y = -5; y <= 5; ++y) {
		for (std::int64_t x = -5; x <= 5; ++x) {
			if (x == 0 && y == 0) {
				continue;
			}
			dirs.push_back({x, y});
		}
	}
	for (const Vec2i64& a : dirs) {
		for (const Vec2i64& b : dirs) {
			const bool equiv	   = !angleLess(a, b) && !angleLess(b, a);
			// Same ray: cross == 0 and dot > 0.
			const bool sameRay = cross(a, b).sign() == 0 && dot(a, b).sign() > 0;
			ASSERT_EQ(equiv, sameRay) << a.x << "," << a.y << " vs " << b.x << "," << b.y;
		}
	}
}
