#pragma once

#include "../polygon/Polygon.h" // Ring

#include <cstdint>
#include <vector>

// Selection silhouettes as vector outlines: unions of polygons, closed and simplified by vector operations only.
// An outline is a set of disjoint simple CCW rings with their holes filled. Every length is in the input's units
// (integer millimetres for the game's assets). No shape is ever sampled into cells: the unions are exact
// arrangements of the rings' edges (whose broad phase indexes segment boxes only to pick the pairs it tests).

namespace geometry {

	// The outline of the union of rings, each filled whatever its winding: every ring is wound CCW, the union
	// taken by the nonzero rule in halves (see silhouetteOfTriangles), and only the outer rings kept, so any hole
	// is filled. Each halving level's union simplifies by kBooleanSimplifyEpsMm, so the outline lies within that
	// many millimetres a level of the exact union. Rings with no area are ignored; empty for no area.
	std::vector<Ring> outlineOfUnion(std::vector<Ring> rings);

	// The same in one union, for a few rings: within kBooleanSimplifyEpsMm of the exact union.
	std::vector<Ring> outlineOfUnionInOnePass(std::vector<Ring> rings);

	// A convex ring grown by `radius`: its Minkowski sum with the regular octagon whose edges touch the circle of
	// that radius, so every point within `radius` of it is inside. The convex hull of each vertex moved by each
	// octagon vertex, with no union and no simplification.
	Ring dilateConvex(const Ring& convex, std::int64_t radius);

	// The outline closed by `radius` (dilated, then eroded, by the octagon of dilateConvex), simplified by
	// `tolerance`, and unioned with the outline as given, so it holds all of it: gaps and notches narrower than
	// about twice `radius` fill and nothing is lost. The erosion is the dilation of the complement, taken inside a
	// box round the outline. A radius of 0 or less returns the outline unchanged.
	std::vector<Ring> closeOutline(const std::vector<Ring>& outline, std::int64_t radius, std::int64_t tolerance);

	// Each ring simplified (Douglas-Peucker) so no removed vertex lies further than `tolerance` from what is
	// kept, then unioned again so every ring stays simple. A ring the simplification would flatten to nothing (a
	// body thinner than the tolerance) is kept as it was, so no body is lost.
	std::vector<Ring> simplifyOutline(std::vector<Ring> outline, std::int64_t tolerance);

	// The filled outline of a set of triangles (the tessellated draw geometry, fills and stroke bands alike):
	// `triangleVertices.size()` is a multiple of 3, each consecutive triple one triangle of any winding;
	// degenerate triangles are ignored. The triangles' union in halves of their order (each half's outline unioned
	// with the other's, so a triangle's inner edges never meet the rest of the shape), simplified by `tolerance`,
	// then closed by `closeRadius` (closeOutline). Every triangle lies inside the result or within
	// silhouetteBoundMm of it, but for a body under about 2 mm across: each union's own simplification by
	// kBooleanSimplifyEpsMm can collapse one that thin (a 0.8 by 300 mm stroke), and a piece left with fewer than three
	// vertices is dropped. Empty for no area.
	std::vector<Ring> silhouetteOfTriangles(
		const std::vector<Vec2i64>& triangleVertices, std::int64_t closeRadius, std::int64_t tolerance
	);

	// How far a triangle given to silhouetteOfTriangles can lie outside its result: the simplification's
	// tolerance, plus kBooleanSimplifyEpsMm for each halving level of `triangleCount` triangles and for the two
	// unions after it.
	std::int64_t silhouetteBoundMm(std::size_t triangleCount, std::int64_t tolerance);

} // namespace geometry
