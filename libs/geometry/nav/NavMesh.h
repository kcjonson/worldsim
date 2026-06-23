#pragma once

#include "../core/Vec2i64.h"

#include <array>
#include <cstdint>
#include <vector>

// Navmesh assembler: turn a set of tagged polygons (one or more walkable borders
// plus blocked obstacles, with door portals) into a triangle navmesh with
// adjacency, ready for path queries.
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
// CORRIDOR-WIDTH FILTERING. Each triangle carries Demyen-Buro corridor widths
// (Efficient Triangulation-Based Pathfinding, 2006, sec. 4.1) so the path query
// can reject a corridor too narrow for a given agent diameter. edgePairWidthMm[i]
// is the widest disc DIAMETER that fits passing the two edges meeting at vertex
// v[i] (the apex), measured as the exact distance from that apex to the nearest
// COMMON-KNOWLEDGE obstacle feature in the wedge; edgeClearWidthMm[i] is a door
// gap's clear width on a portal edge. See NavMesh.cpp for the exact-integer
// adaptation (no bare point obstacles; clearance is distance-to-feature, never
// edge length, so an open-floor sliver stays unconstrained).
//
// DEFERRED (still not implemented): clearance against BELIEF-DEPENDENT walls
// (faceBlocker > 0). Those widths vary per agent belief and only bite oversized
// agents in built corridors; construction guarantees walkable wall gaps >= 0.7 m,
// so for the one shipped agent size (0.3 m radius, 0.6 m diameter) every wall
// portal fits. Width filtering here covers common-knowledge terrain/flora and
// door gaps only.

namespace geometry::nav {

	constexpr std::int64_t kNoProvenance = INT64_MIN;
	constexpr std::int64_t kNoOpening	 = -1;
	// Floor sentinel for NavTriangle::faceBlocker: a triangle on real floor, always
	// walkable regardless of belief. Distinct from kNoProvenance only conceptually;
	// shares the same INT64_MIN bit pattern (no provenance == real floor).
	constexpr std::int64_t kNoBlocker = INT64_MIN;

	// Corridor-width sentinel: a passage no common-knowledge obstacle constrains
	// (e.g. an open-floor sliver). Larger than any real millimeter width, so the
	// "< 2*radius" reject test always admits it.
	constexpr std::int64_t kUnconstrainedWidth = INT64_MAX;

	// One tagged input ring. One or more polygons in NavMeshInput have blocked=false:
	// the walkable bounds (the chunk/region border, or several disjoint regions).
	// All others are blocked obstacles. `ring` is a simple closed ring (no repeated
	// closing vertex); winding is normalized internally, callers need not pre-orient
	// it.
	//
	// `openingId` tags a blocked ring that is a pathable door's footprint sub-region:
	// in a truth query the triangles inside it are walkable (the door passes), in a
	// belief query they are walkable only if the agent knows both the wall segment
	// and that opening. A solid wall ring leaves it kNoOpening.
	struct NavInputPolygon {
		std::vector<Vec2i64> ring;
		bool				 blocked	  = false;
		std::int64_t		 provenanceId = kNoProvenance;
		std::int64_t		 openingId	  = kNoOpening;
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

		// Demyen corridor width keyed by APEX vertex: edgePairWidthMm[i] is the max
		// disc diameter (mm) that fits passing the two edges meeting at vertex v[i]
		// -- edges i and (i+2)%3, whose shared vertex is v[i] -- against common-
		// knowledge obstacles. kUnconstrainedWidth when no such obstacle pinches it.
		std::array<std::int64_t, 3> edgePairWidthMm = {kUnconstrainedWidth, kUnconstrainedWidth,
													   kUnconstrainedWidth};
		// Door clear width (mm) when edge i is a door portal, else kUnconstrainedWidth.
		// Threaded from DoorPortal::clearWidthMm; the A* gates a door crossing by it.
		std::array<std::int64_t, 3> edgeClearWidthMm = {kUnconstrainedWidth, kUnconstrainedWidth,
														kUnconstrainedWidth};

		// Per-face blocker tag, the belief filter's hook. The whole region (wall
		// interiors included) is triangulated; this says what each triangle sits on:
		//   kNoBlocker (== INT64_MIN) -> real floor, always walkable.
		//   > 0  -> a wall segment id; belief-gated (truth: walkable iff faceOpening
		//           is set, i.e. a door passes and solid wall blocks).
		//   < 0  -> a terrain/common-knowledge sentinel (water/tree); always blocks.
		std::int64_t faceBlocker = kNoBlocker;
		// Pathable door's openingId when this triangle is inside that door's footprint
		// (faceBlocker is then the hosting segment id); else kNoOpening.
		std::int64_t faceOpening = kNoOpening;
	};

	// Uniform-grid spatial index over the triangle set, built by buildNavMesh.
	// Each cell stores the indices of candidate triangles whose AABBs overlap it,
	// in ascending order so locateTriangle reproduces the linear scan's lowest-index
	// tie-break for points on shared edges.
	struct NavGrid {
		Vec2i64					 minPt;			// AABB min of all vertices (mm)
		Vec2i64					 maxPt;			// AABB max of all vertices (mm)
		std::int64_t			 cellSize = 1;	// cell side length (mm)
		std::int32_t			 cols	  = 0;	// number of cells along X
		std::int32_t			 rows	  = 0;	// number of cells along Y
		// CSR-style: candidates for cell (r*cols+c) are cells[cellStart[r*cols+c] ..
		// cellStart[r*cols+c+1]).  Length == cols*rows+1.
		std::vector<std::int32_t> cellStart;	// prefix sums into candidates
		std::vector<std::int32_t> candidates;	// triangle indices, ascending within each cell
	};

	struct NavMesh {
		std::vector<Vec2i64>	 vertices;
		std::vector<NavTriangle> triangles;
		NavGrid					 grid;
	};

	// Build the navmesh from tagged input. A walkable face whose triangulation
	// degenerates is skipped (its triangles are omitted) rather than aborting the
	// whole mesh, so a single bad region yields a partial mesh, not an empty one.
	NavMesh buildNavMesh(const NavMeshInput& input);

} // namespace geometry::nav
