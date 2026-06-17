#pragma once

#include "../core/Vec2i64.h"

#include <array>
#include <cstdint>
#include <vector>

namespace geometry {

	// Triangulate the region bounded by `outer` (a CCW simple ring, given as
	// indices into `vertices`) with `holes` (each a CW simple ring of indices,
	// strictly inside `outer`, mutually disjoint, touching neither `outer` nor
	// each other). Returns CCW triangles as index triples into `vertices`.
	//
	// The result is a Delaunay-improved constrained triangulation: every boundary
	// edge (outer + holes) is preserved; only interior diagonals are flipped. On a
	// degenerate or invalid input (a non-simple ring, a hole not strictly inside,
	// a wrongly-wound ring) the function returns empty rather than emitting a bad
	// mesh -- reject, don't repair. Output is deterministic for a given input.
	//
	// Precondition: coordinates must be region-local, with every pairwise
	// difference within ~2^30 mm (the same bound inCircle documents). The Lawson
	// flip calls inCircle (degree-4 determinant) and the hole-bridge ray arithmetic
	// forms int64 products; world-scale coordinates overflow silently (UB). Rebase
	// to the region origin before calling.
	std::vector<std::array<std::uint32_t, 3>> triangulateWithHoles(
		const std::vector<Vec2i64>& vertices,
		const std::vector<std::uint32_t>& outer,
		const std::vector<std::vector<std::uint32_t>>& holes);

	// Convenience: triangulate a single CCW simple ring with no holes.
	std::vector<std::array<std::uint32_t, 3>> triangulateSimple(
		const std::vector<Vec2i64>& vertices, const std::vector<std::uint32_t>& ring);

} // namespace geometry
