#pragma once

#include "../core/Int128.h"
#include "../core/Vec2i64.h"
#include "Arrangement.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Half-edge structure and face extraction over an Arrangement (building-
// construction D6). Construction is the standard angular-sort / twin-link walk
// (cp-algorithms): split each arrangement edge into a twin pair of directed
// half-edges, sort outgoing half-edges at each vertex by exact polar angle,
// link each half-edge's `next` to the angularly adjacent edge, then walk the
// links to enumerate face cycles. Signed area is accumulated in 128-bit:
// negative cycles bound the unbounded side (outer boundaries), positive cycles
// are bounded faces.
//
// Everything here is exact and deterministic across platforms: the angular
// comparator (geometry::angleLess in predicates) uses half-plane classification
// plus cross-product sign (no atan2, no floats), and the arrangement it consumes
// is already canonical.

namespace geometry {

	struct HalfEdge {
		std::size_t origin = 0; // vertex index this half-edge leaves from
		std::size_t target = 0; // vertex index this half-edge points to
		std::size_t twin   = 0; // opposite half-edge (target -> origin)
		std::size_t next   = 0; // next half-edge around the same face cycle
		std::size_t edge   = 0; // index into Arrangement::edges (provenance source)
		std::size_t face   = 0; // index into HalfEdgeMesh::faces, filled by extraction
	};

	// A face cycle: the loop of half-edges, its signed doubled area (shoelace
	// integer, positive for CCW bounded faces, negative for the outer cycle),
	// the deduplicated provenance of every edge on the loop, and a representative
	// interior point for bounded faces.
	struct Face {
		std::vector<std::size_t>  halfEdges;
		Int128					  signedAreaDoubled;
		std::vector<std::int64_t> provenance;

		// True when signedAreaDoubled < 0: the cycle bounds the unbounded side.
		bool outer = false;

		// Set for bounded (positive-area) faces only: a point strictly inside the
		// cycle, derived exactly from the geometry (a triangle centroid scaled to
		// the integer grid, validated by pointInCycle). Nullopt for outer cycles.
		std::optional<Vec2i64> representativePoint;
	};

	struct HalfEdgeMesh {
		std::vector<Vec2i64>  vertices; // copied from the arrangement, same indices
		std::vector<HalfEdge> halfEdges;
		std::vector<Face>	  faces;

		// --- Adjacency queries (small, concrete; consumers are booleans + rooms) ---

		// The arrangement edge indices forming a face's boundary, in loop order.
		std::vector<std::size_t> faceEdges(std::size_t faceIndex) const;

		// The face on the other side of `halfEdgeIndex` (the twin's face).
		std::size_t twinFace(std::size_t halfEdgeIndex) const;

		// All faces touching `vertexIndex`, deduplicated, in ascending order.
		std::vector<std::size_t> facesAtVertex(std::size_t vertexIndex) const;
	};

	// Build the half-edge mesh and extract all face cycles from an arrangement.
	HalfEdgeMesh extractFaces(const Arrangement& arrangement);

	// Exact containment of `point` in a face's cycle, via crossing-number over
	// the cycle's vertex ring with on-boundary detection (reuses the polygon
	// predicate). Provided so the boolean layer can classify points against a
	// face without depending on the representative point.
	PointInPolygon pointInCycle(const Vec2i64& point, const HalfEdgeMesh& mesh, std::size_t faceIndex);

} // namespace geometry
