#pragma once

// 2D geometry predicates for the sweep-line tessellator.
//
// Ported to modern C++ from libtess2 (geom.c) -- the SGI GLU tessellator by
// Eric Veach, SGI Free Software License B 2.0 (http://oss.sgi.com/projects/FreeB/).
// Vertices are compared/evaluated in sweep space (s == x, t == y). The cheap
// edge-sign variant is intentionally omitted: per libtess2 issue #22 it can
// disagree with the accurate eval near s==0, so edge-sign delegates to edgeEval.

#include "Mesh.h"

namespace renderer::tess {

	// Lexicographic vertex order: by s, then t.
	inline bool vertEq(const Vertex& u, const Vertex& v) { return u.s == v.s && u.t == v.t; }
	inline bool vertLeq(const Vertex& u, const Vertex& v) { return (u.s < v.s) || (u.s == v.s && u.t <= v.t); }
	// Same, with s and t transposed (used for the t-coordinate intersection pass).
	inline bool transLeq(const Vertex& u, const Vertex& v) { return (u.t < v.t) || (u.t == v.t && u.s <= v.s); }

	inline bool edgeGoesLeft(const HalfEdge& e) { return vertLeq(*e.dst(), *e.org); }
	inline bool edgeGoesRight(const HalfEdge& e) { return vertLeq(*e.org, *e.dst()); }

	// Signed distance from edge uw to v at v's s-coordinate (> 0 above, < 0 below).
	// Requires vertLeq(u,v) && vertLeq(v,w). Accurate and stable near u or w.
	float edgeEval(const Vertex& u, const Vertex& v, const Vertex& w);
	// Sign matches edgeEval; delegates to it (see header note).
	inline float edgeSign(const Vertex& u, const Vertex& v, const Vertex& w) { return edgeEval(u, v, w); }

	// edgeEval / cheap-sign with s and t transposed. Requires transLeq(u,v) && transLeq(v,w).
	float transEval(const Vertex& u, const Vertex& v, const Vertex& w);
	float transSign(const Vertex& u, const Vertex& v, const Vertex& w);

	// True if u, v, w wind counter-clockwise (>= 0). Unreliable for near-degenerate input.
	bool vertCcw(const Vertex& u, const Vertex& v, const Vertex& w);

	// Intersection of edges (o1,d1) and (o2,d2), written to out.s/out.t. The result
	// is guaranteed to lie within the intersection of the two edges' bounding boxes.
	void edgeIntersect(const Vertex& o1, const Vertex& d1, const Vertex& o2, const Vertex& d2, Vertex& out);

} // namespace renderer::tess
