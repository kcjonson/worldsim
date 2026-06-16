#pragma once

#include "../core/Vec2i64.h"

#include <array>
#include <cstdint>
#include <vector>

// Navmesh assembler: turn a set of tagged polygons (one walkable border plus
// blocked obstacles, with door portals) into a triangle navmesh with adjacency,
// ready for path queries.
//
// The pipeline is arrangement -> half-edge face extraction -> per-walkable-face
// constrained triangulation -> global edge-hash adjacency. Walkable faces are
// separated by blocked bands (no shared triangle edges between distinct faces),
// so the adjacency graph links only intra-face neighbors and across door gaps
// (a door gap is interior to a single walkable face).
//
// This library is pure: no ECS, no engine dependencies. Coordinates are the
// region-local integer millimeter frame shared by the rest of geometry/.
//
// DEFERRED (not implemented): per-portal corridor-width (Demyen) filtering. v1
// assumes one agent size (0.3 m radius, 0.6 m diameter) and the construction
// clearance constant guarantees walkable gaps >= 0.7 m, so every agent fits
// through any walkable portal. Width filtering is a later enhancement for
// variable agent sizes.

namespace geometry::nav {

	constexpr std::int64_t kNoProvenance = INT64_MIN;
	constexpr std::int64_t kNoOpening	 = -1;

	// One tagged input ring. Exactly one polygon in NavMeshInput has blocked=false:
	// the walkable bounds (the chunk/region border). All others are blocked
	// obstacles. `ring` is a simple closed ring (no repeated closing vertex);
	// winding is normalized internally, callers need not pre-orient it.
	struct NavInputPolygon {
		std::vector<Vec2i64> ring;
		bool				 blocked	  = false;
		std::int64_t		 provenanceId = kNoProvenance;
	};

	// A door portal: an open gap in a blocked band that agents may cross. (a, b)
	// are the gap's two endpoints as exact mesh vertices. `clearWidthMm` is kept
	// for the deferred width-filtering pass.
	struct DoorPortal {
		std::int64_t openingId	  = kNoOpening;
		Vec2i64		 a, b;
		std::int64_t clearWidthMm = 0;
	};

	struct NavMeshInput {
		std::vector<NavInputPolygon> polygons;
		std::vector<DoorPortal>		 doors;
	};

	struct NavTriangle {
		std::array<std::uint32_t, 3> v;				 // CCW vertex indices into NavMesh::vertices
		std::array<std::int32_t, 3>	 neighbor;		 // triangle across edge (v[i],v[(i+1)%3]); -1 = boundary
		std::array<std::int64_t, 3>	 edgeProvenance; // provenance of constraint edge i, else kNoProvenance
		std::array<std::int64_t, 3>	 edgeOpening;	 // door openingId if edge i is a portal, else kNoOpening
	};

	struct NavMesh {
		std::vector<Vec2i64>	 vertices;
		std::vector<NavTriangle> triangles;
	};

	// Build the navmesh from tagged input. A walkable face whose triangulation
	// degenerates is skipped (its triangles are omitted) rather than aborting the
	// whole mesh, so a single bad region yields a partial mesh, not an empty one.
	NavMesh buildNavMesh(const NavMeshInput& input);

} // namespace geometry::nav
