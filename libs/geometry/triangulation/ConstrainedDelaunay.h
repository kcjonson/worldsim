#pragma once

#include "../core/Vec2i64.h"

#include <array>
#include <cstdint>
#include <vector>

// Constrained Delaunay triangulation of a planar straight-line graph, exact on the
// integer grid.
//
// Vertices go in first, in index order, each located by a visibility walk and
// spliced in with Lawson flips. Then each constraint is recovered by clearing the
// triangles it crosses and retriangulating the pseudo-polygon on either side
// (Anglada 1997), and a last Lawson pass restores the Delaunay property on every
// unconstrained edge. Every decision is an exact orientation or inCircle sign and
// nothing depends on hash order, so the result is a pure function of the input.
//
// The triangulation covers a frame rectangle strictly enclosing the input, so
// every input vertex is interior and there is no convex-hull special case.
// Triangles touching a frame corner are part of the result; a caller that wants
// only the input's own region drops them.

namespace geometry {

	struct CdtTriangle {
		std::array<std::uint32_t, 3> v;			 // CCW vertex indices
		std::array<std::int32_t, 3>	 neighbor;	 // triangle across edge (v[i], v[(i+1)%3]); -1 on the frame
		std::array<std::int32_t, 3>	 constraint; // index of the input constraint covering edge i, else -1
	};

	struct ConstrainedDelaunay {
		// The input vertices, then the four frame corners from frameStart on.
		std::vector<Vec2i64>	 vertices;
		std::vector<CdtTriangle> triangles;
		std::uint32_t			 frameStart = 0;
	};

	// Triangulate `vertices` with every edge in `constraints` (index pairs into
	// `vertices`) present in the result, each edge tagged with its constraint's index.
	//
	// Preconditions: the vertices are distinct, and the constraints form a planar
	// straight-line graph, meeting only at shared endpoints. A vertex lying on a
	// constraint splits it and both pieces carry its index; a constraint crossing an
	// earlier one violates the precondition, and the piece that would cross is left
	// out. The frame sits the input's extent beyond its bounding box, so pairwise
	// coordinate differences reach three times that extent and must stay within the
	// ~2^30 mm inCircle is exact for (inputs up to ~350 km across). Absolute
	// coordinates may be large.
	ConstrainedDelaunay buildConstrainedDelaunay(
		const std::vector<Vec2i64>& vertices, const std::vector<std::array<std::uint32_t, 2>>& constraints);

} // namespace geometry
