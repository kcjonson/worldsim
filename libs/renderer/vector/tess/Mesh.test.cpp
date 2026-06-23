#include "Mesh.h"
#include "Predicates.h"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace renderer::tess {
	namespace {

		int countVertices(const Mesh& mesh) {
			int n = 0;
			for (const Vertex* v = mesh.vHead.next; v != &mesh.vHead; v = v->next) {
				++n;
			}
			return n;
		}

		int countFaces(const Mesh& mesh) {
			int n = 0;
			for (const Face* f = mesh.fHead.next; f != &mesh.fHead; f = f->next) {
				++n;
			}
			return n;
		}

		int countEdges(const Mesh& mesh) {
			int n = 0;
			for (const HalfEdge* e = mesh.eHead.next; e != &mesh.eHead; e = e->next) {
				++n;
			}
			return n;
		}

		int countFaceVerts(const Face* f) {
			int				n = 0;
			const HalfEdge* e = f->anEdge;
			do {
				++n;
				e = e->lnext;
			} while (e != f->anEdge);
			return n;
		}

		// Build a closed contour the way libtess's AddContour does: the first vertex is a
		// self-loop (makeEdge + splice), each subsequent vertex splits the running edge.
		HalfEdge* buildContour(Mesh& mesh, const std::vector<std::pair<float, float>>& pts) {
			HalfEdge* e = nullptr;
			for (const auto& [x, y] : pts) {
				if (e == nullptr) {
					e = mesh.makeEdge();
					mesh.splice(e, e->sym);
				} else {
					mesh.splitEdge(e);
					e = e->lnext;
				}
				e->org->s = x;
				e->org->t = y;
				e->winding = 1;
				e->sym->winding = -1;
			}
			return e;
		}

	} // namespace

	TEST(TessMesh, EmptyMeshIsConsistent) {
		Mesh mesh;
		EXPECT_TRUE(mesh.checkConsistency());
		EXPECT_EQ(countVertices(mesh), 0);
		EXPECT_EQ(countFaces(mesh), 0);
		EXPECT_EQ(countEdges(mesh), 0);
	}

	TEST(TessMesh, MakeEdgeProducesOneEdgeTwoVertsOneFace) {
		Mesh	  mesh;
		HalfEdge* e = mesh.makeEdge();

		EXPECT_TRUE(mesh.checkConsistency());
		EXPECT_EQ(countVertices(mesh), 2);
		EXPECT_EQ(countFaces(mesh), 1);
		EXPECT_EQ(countEdges(mesh), 1);

		// A lone edge: distinct endpoints, both half-edges share the single loop.
		EXPECT_NE(e->org, e->dst());
		EXPECT_EQ(e->lface, e->sym->lface);
		EXPECT_EQ(e->onext, e);		   // origin has only this edge
		EXPECT_EQ(e->lnext, e->sym);   // left-face loop is the two half-edges
		EXPECT_EQ(e->sym->lnext, e);
		EXPECT_EQ(e->sym->sym, e);
	}

	TEST(TessMesh, SplitEdgeAddsVertexAndEdge) {
		Mesh	  mesh;
		HalfEdge* e = mesh.makeEdge();
		mesh.splitEdge(e);

		EXPECT_TRUE(mesh.checkConsistency());
		EXPECT_EQ(countVertices(mesh), 3);
		EXPECT_EQ(countEdges(mesh), 2);
		// The new edge follows e around the left face, sharing a fresh midpoint vertex.
		EXPECT_EQ(e->dst(), e->lnext->org);
	}

	TEST(TessMesh, TriangleContourTopology) {
		Mesh mesh;
		buildContour(mesh, {{0.0F, 0.0F}, {100.0F, 0.0F}, {50.0F, 100.0F}});

		EXPECT_TRUE(mesh.checkConsistency());
		EXPECT_EQ(countVertices(mesh), 3);
		EXPECT_EQ(countEdges(mesh), 3);
		EXPECT_EQ(countFaces(mesh), 2); // interior loop + exterior loop
	}

	TEST(TessMesh, PentagonContourLoopsHaveAllVerts) {
		Mesh	  mesh;
		HalfEdge* e = buildContour(
			mesh, {{0.0F, 0.0F}, {100.0F, 0.0F}, {120.0F, 80.0F}, {50.0F, 130.0F}, {-20.0F, 80.0F}});

		EXPECT_TRUE(mesh.checkConsistency());
		EXPECT_EQ(countVertices(mesh), 5);
		EXPECT_EQ(countEdges(mesh), 5);
		EXPECT_EQ(countFaces(mesh), 2);
		// Both sides of the contour are 5-gon loops.
		EXPECT_EQ(countFaceVerts(e->lface), 5);
		EXPECT_EQ(countFaceVerts(e->rface()), 5);
	}

	TEST(TessMesh, ConnectSplitsFaceIntoTwoLoops) {
		Mesh mesh;
		// Quad contour; connect a chord and confirm it splits the loop in two.
		HalfEdge* e =
			buildContour(mesh, {{0.0F, 0.0F}, {100.0F, 0.0F}, {100.0F, 100.0F}, {0.0F, 100.0F}});
		ASSERT_TRUE(mesh.checkConsistency());

		HalfEdge* across = mesh.connect(e, e->lnext->lnext);
		EXPECT_TRUE(mesh.checkConsistency());
		EXPECT_EQ(countEdges(mesh), 5);
		EXPECT_EQ(countFaces(mesh), 3); // outer loop + the two halves
		// The chord borders two distinct loops; cutting an n-gon with one chord yields
		// two loops whose edge counts sum to n + 2.
		EXPECT_NE(across->lface, across->rface());
		EXPECT_EQ(countFaceVerts(across->lface) + countFaceVerts(across->rface()), 6);
	}

	TEST(TessMesh, DeleteEdgeUndoesConnect) {
		Mesh	  mesh;
		HalfEdge* e =
			buildContour(mesh, {{0.0F, 0.0F}, {100.0F, 0.0F}, {100.0F, 100.0F}, {0.0F, 100.0F}});
		HalfEdge* across = mesh.connect(e, e->lnext->lnext);
		ASSERT_EQ(countEdges(mesh), 5);

		mesh.deleteEdge(across);
		EXPECT_TRUE(mesh.checkConsistency());
		EXPECT_EQ(countEdges(mesh), 4);
		EXPECT_EQ(countFaces(mesh), 2);
	}

	TEST(TessPredicates, LexicographicOrder) {
		Vertex a{.s = 0.0F, .t = 0.0F};
		Vertex b{.s = 0.0F, .t = 1.0F};
		Vertex c{.s = 1.0F, .t = -5.0F};
		EXPECT_TRUE(vertLeq(a, b));
		EXPECT_TRUE(vertLeq(a, c));
		EXPECT_TRUE(vertLeq(b, c)); // smaller s wins regardless of t
		EXPECT_FALSE(vertLeq(c, a));
		EXPECT_TRUE(vertEq(a, Vertex{.s = 0.0F, .t = 0.0F}));
	}

	TEST(TessPredicates, EdgeSignAndCcw) {
		Vertex u{.s = 0.0F, .t = 0.0F};
		Vertex w{.s = 2.0F, .t = 0.0F};
		Vertex above{.s = 1.0F, .t = 1.0F};
		Vertex below{.s = 1.0F, .t = -1.0F};
		EXPECT_GT(edgeEval(u, above, w), 0.0F); // above the edge u->w
		EXPECT_LT(edgeEval(u, below, w), 0.0F);
		EXPECT_TRUE(vertCcw(u, w, above));
		EXPECT_FALSE(vertCcw(u, w, below));
	}

	TEST(TessPredicates, EdgeIntersectAtCrossing) {
		Vertex o1{.s = 0.0F, .t = 0.0F};
		Vertex d1{.s = 2.0F, .t = 2.0F};
		Vertex o2{.s = 0.0F, .t = 2.0F};
		Vertex d2{.s = 2.0F, .t = 0.0F};
		Vertex out{};
		edgeIntersect(o1, d1, o2, d2, out);
		EXPECT_NEAR(out.s, 1.0F, 1e-4F);
		EXPECT_NEAR(out.t, 1.0F, 1e-4F);
	}

} // namespace renderer::tess
