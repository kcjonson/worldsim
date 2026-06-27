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
		// A water body is emitted as separate marching-squares loops (a CCW outer
		// boundary plus CW land-island holes), all blocked. holeCapable marks those
		// rings so face classification uses even-odd containment parity (a point
		// inside an even number of nested water rings is land = floor; odd = water).
		// Flora, walls, and the border leave this false and use solid containment.
		bool				 holeCapable  = false;
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

	// --- Face-walkable predicates (shared truth/terrain/belief logic) ------------
	// The three queries (truth reachability, terrain reachability, per-belief A*)
	// classify a triangle's faceBlocker the same way; only the faceBlocker>0 (wall)
	// branch differs. These free functions isolate the two belief-FREE branches so
	// PathQuery::traversable reuses them and never duplicates the floor/terrain logic.

	// Real floor: always walkable, every query agrees.
	inline bool isFloorFace(const NavTriangle& t) {
		return t.faceBlocker == kNoBlocker;
	}
	// Common-knowledge terrain sentinel (water/tree, or a junction with no incident
	// wall id): always blocks, belief or not. (faceBlocker < 0 and != kNoBlocker.)
	inline bool isCommonKnowledgeTerrainFace(const NavTriangle& t) {
		return t.faceBlocker < 0 && t.faceBlocker != kNoBlocker;
	}

	// TRUTH: floor, or a wall face that a door passes through (faceBlocker>0 with an
	// opening). Solid walls block. This is AI goal validity (does a real route exist).
	inline bool truthTraversable(const NavTriangle& t) {
		if (isFloorFace(t)) {
			return true;
		}
		if (isCommonKnowledgeTerrainFace(t)) {
			return false;
		}
		return t.faceOpening != kNoOpening; // faceBlocker > 0: wall, walkable iff a door spans it
	}

	// TERRAIN (most-optimistic belief): floor, or ANY wall face treated as OPEN; only
	// common-knowledge terrain blocks. The belief reject uses this because the most
	// optimistic agent (knows nothing) routes through every unseen wall, so a terrain
	// disconnect is unreachable for EVERY belief.
	inline bool terrainTraversable(const NavTriangle& t) {
		return !isCommonKnowledgeTerrainFace(t); // floor or any wall is open
	}

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

	// Width-aware reachability over the triangle dual graph (P3.3). A Kruskal
	// max-spanning-forest + reconstruction tree (a.k.a. min-max / merge tree) that
	// answers two queries online: which component a triangle is in, and the
	// bottleneck (the widest disc that any path between two triangles admits = the
	// max over paths of the min edge capacity). Edge capacity is the floored mm
	// disc DIAMETER a portal admits (see NavMesh.cpp step 10).
	//
	// The reconstruction tree has the mesh's triangles as leaves [0, triangleCount):
	// each Kruskal union (processing portal edges in DESCENDING capacity) creates an
	// internal node storing that edge's capacity, with the two merged subtree roots
	// as children. Then bottleneck(a, b) = the capacity stored at LCA(a, b), because
	// the union that first joined a and b used the smallest capacity on the widest
	// path between them. "Different tree" (different root) => disconnected for every
	// diameter. Triangles not traversable for this forest's predicate are not nodes;
	// they get component == -1 and any bottleneck query touching them is unreachable.
	//
	// SOUNDNESS (the only direction we rely on): a disc of diameter D crossing a path
	// uses, at each portal, an apex passage <= that portal's capacity, so D <=
	// min-cap of the path <= bottleneck(a, b). Hence D > bottleneck(a, b) => no path
	// admits the disc => unreachable. This is an UPPER bound; it can over-approximate
	// (bottleneck high but the funnel still fails), never the reverse.
	struct ReachabilityForest {
		// Per leaf-triangle component id (root of its reconstruction tree), or -1 if
		// the triangle is not traversable for this forest's predicate. Length ==
		// triangleCount. Two traversable triangles are connected (ignoring width) iff
		// they share a component id.
		std::vector<std::int32_t> component;

		// Reconstruction-tree node capacities. Leaf nodes [0, triangleCount) store
		// kUnconstrainedWidth (a triangle is unconstrained with itself); internal
		// nodes [triangleCount, nodeCount) store the merge capacity. Disconnected
		// (non-node) leaves still occupy their slot but are never reached by a query.
		std::vector<std::int64_t> nodeCap;

		// Binary-lifting LCA tables over the reconstruction-tree forest. up[k][i] is
		// the 2^k-th ancestor of node i (i when it runs past a root). depth[i] is the
		// node's depth from its root. parent (up[0]) of a root is itself.
		std::vector<std::int32_t>			   depth;
		std::vector<std::vector<std::int32_t>> up; // up[level][node]
		std::int32_t						   levels = 0; // number of lifting levels
	};

	struct NavMesh {
		std::vector<Vec2i64>	 vertices;
		std::vector<NavTriangle> triangles;
		NavGrid					 grid;
		// Two forests over the SAME geometric edge capacities, differing only in which
		// triangles are nodes (the traversability predicate):
		//   truthForest:   nodes = truth-traversable triangles (floor or a door span).
		//                  Answers AI goal validity (truth reachability).
		//   terrainForest: nodes = terrain-traversable triangles (floor or ANY wall
		//                  face treated as OPEN; only common-knowledge terrain blocks).
		//                  Answers the BELIEF reject -- sound for every possible belief,
		//                  since the most optimistic belief routes through unseen walls.
		ReachabilityForest truthForest;
		ReachabilityForest terrainForest;
	};

	// Build the navmesh from tagged input. A walkable face whose triangulation
	// degenerates is skipped (its triangles are omitted) rather than aborting the
	// whole mesh, so a single bad region yields a partial mesh, not an empty one.
	NavMesh buildNavMesh(const NavMeshInput& input);

	// Are triangles triA and triB in the same component of `forest` (connected
	// ignoring width)? False if either index is out of range or is a non-node for
	// the forest's predicate. O(1).
	bool reachableInForest(const ReachabilityForest& forest, std::int32_t triA, std::int32_t triB);

	// The bottleneck between triA and triB in `forest`: the max over paths of the min
	// edge capacity (the widest disc DIAMETER any path between them admits). Returns 0
	// when disconnected (or an endpoint is a non-node), kUnconstrainedWidth when
	// triA == triB. The sound reject is `diameter > bottleneck => unreachable`.
	// O(log n) via binary-lifting LCA over the reconstruction tree.
	std::int64_t bottleneckInForest(const ReachabilityForest& forest, std::int32_t triA, std::int32_t triB);

} // namespace geometry::nav
