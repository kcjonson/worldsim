#pragma once

#include "../core/Vec2i64.h"
#include "../predicates/Predicates.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// Planar arrangement builder: take a batch of segments and produce a clean
// planar subdivision (building-construction D2/D6). Vertices are merged by
// exact equality, segments are split at every intersection, and geometrically
// coincident edges are deduplicated to exactly one edge carrying the provenance
// (the input segment indices) of every contributor.
//
// This is the engine the boolean layer and room detection sit on: booleans
// classify result edges by which input ring they came from; room detection maps
// extracted faces back to wall segment IDs. Both read provenance off the edges.

namespace geometry {

	// One input segment to arrange. `index` is the caller's stable identifier
	// (wall segment ID, input ring edge index); it is propagated to every output
	// edge derived from this segment as provenance and is otherwise opaque.
	struct InputSegment {
		Vec2i64		a;
		Vec2i64		b;
		std::int64_t index = 0;
	};

	// An edge of the arrangement, as an index pair into the vertex list with
	// `from < to` (lexicographic) for canonical, direction-independent identity.
	// `provenance` lists every input segment index whose geometry covers this
	// edge, sorted ascending and deduplicated. A coincident-edge overlap merges
	// the provenance of all contributors here.
	struct ArrangementEdge {
		std::size_t				 from = 0;
		std::size_t				 to	  = 0;
		std::vector<std::int64_t> provenance;
	};

	// Clean planar subdivision. Vertices are unique (exact ==) and sorted
	// lexicographically; edges are unique by {from,to} and sorted. The structure
	// is canonical: the same input segments inserted in any order produce
	// byte-identical vertices and edges. No zero-length edges exist.
	struct Arrangement {
		std::vector<Vec2i64>		 vertices;
		std::vector<ArrangementEdge> edges;
	};

	// Build the arrangement from a batch of segments. Zero-length inputs (a == b)
	// are rejected and ignored. Resolution iterates splits through the exact
	// intersection pipeline until stable, so rounded crossing points that land on
	// a third segment, or two crossings that round together, are absorbed before
	// the result is returned.
	Arrangement buildArrangement(const std::vector<InputSegment>& segments);

} // namespace geometry
