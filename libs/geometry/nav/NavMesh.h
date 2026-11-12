#pragma once

#include "../core/PagedVector.h"
#include "../core/ScratchArray.h"
#include "../core/Vec2i64.h"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

// Navmesh assembler: turn a set of tagged polygons (one or more walkable borders
// plus blocked obstacles, with door portals) into a triangle navmesh with
// adjacency, ready for path queries.
//
// The pipeline is arrangement -> one constrained Delaunay triangulation of the
// whole arrangement, every arrangement edge a constraint -> faces by flood fill
// across unconstrained edges -> one exact sample per face, classified against the
// input rings. Every in-bounds face is kept, wall and water interiors included,
// and adjacency comes straight from the triangulation, so a floor face and the
// blocked face beside it share edges.
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
// portal fits. Width filtering here covers common-knowledge terrain/obstacles and
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
		// A waterline body arrives as separate rings (a CCW outer boundary plus CW
		// land-island holes), all blocked. holeCapable marks those rings so face
		// classification uses even-odd containment parity among them (a point inside
		// an even number of nested hole-capable rings is land = floor; odd = water).
		// Everything else (river channels, ponds, obstacles, walls, the border) leaves this
		// false: solid containment, and any solid ring blocks whatever the parity.
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
		//   < 0  -> a terrain/common-knowledge sentinel (water/placed obstacle); always blocks.
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
	// Common-knowledge terrain sentinel (water/placed obstacle, or a junction with no incident
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

	// --- Face classification ------------------------------------------------------
	// What ground a point is: the one rule buildNavMesh and PatchableMesh both tag
	// faces by. A face is sampled at one exact interior point, its sample triangle's
	// centroid, held as the integer point a+b+c in 3x space.

	// An input ring as classification reads it: scaled by 3, with its box.
	struct SampleRing {
		std::vector<Vec2i64> ring3;
		Vec2i64				 lo3;
		Vec2i64				 hi3;
		bool				 blocked		= false;
		std::int64_t		 provenance		= kNoProvenance;
		std::int64_t		 opening		= kNoOpening;
		bool				 holeCapable	= false;
		Int128				 areaDoubledAbs = Int128(0); // smallest-containing ranking (unscaled)
	};

	SampleRing sampleRing(const NavInputPolygon& poly);

	// Strictly inside: a sample on the ring's boundary is outside it.
	bool ringContainsSample(const SampleRing& ring, const Vec2i64& sample3);

	struct FaceTags {
		std::int64_t blocker = kNoBlocker;
		std::int64_t opening = kNoOpening;
	};

	// Tags of in-bounds ground inside exactly the blocked rings `containing`, in any
	// order: equal areas tie-break on content (provenance, then opening, then outline),
	// so the answer never depends on input order or edit history. SOLID rings (placed obstacles,
	// walls) go by solid containment: the smallest-area one wins, so a door-span
	// footprint beats the wall band around it. HOLE-CAPABLE rings (water) go by
	// nesting parity: a water body arrives as a CCW outer boundary plus CW land-island
	// holes, separate rings, so odd depth is water (open water 1, a pond on an island
	// 3) and even depth is land; disjoint bodies never nest. A solid ring blocks even
	// over water.
	FaceTags classifyCover(const std::vector<const SampleRing*>& containing);

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

	class MeshComponents;

	// A process-wide counter, never repeating: what a mesh state is known by (see
	// NavView::epoch).
	std::uint64_t nextMeshGeneration();

	struct NavMesh {
		std::vector<Vec2i64>	 vertices;
		std::vector<NavTriangle> triangles;
		NavGrid					 grid;
		// Taken when the mesh is made; a copy carries its source's along with its content.
		// A caller that edits a built mesh in place takes a fresh one.
		std::uint64_t generation = nextMeshGeneration();
	};

	// Build the navmesh from tagged input. The triangles tile the walkable bounds:
	// no face is dropped, however its rings touch, pinch, or nest.
	NavMesh buildNavMesh(const NavMeshInput& input);

	// The triangles a path query or a corridor width reads, and the means to locate a
	// point among them. NavMesh and PatchableMesh each produce one, so each of those
	// algorithms has one implementation. A slot `live` marks dead is never located and
	// never a live triangle's neighbour.
	//
	// A view reads its owner as it is when used, never as it was when taken: component
	// labels, width currency, and the patch epoch come through the owner, and every
	// function taking a view reads the slot tables through resolved(), so a view kept
	// across a patch sees the patched mesh.
	// One of a mesh's slot tables as a view reads it: a NavMesh's contiguous vector or a
	// PatchableMesh's pages (PagedVector), as the owner held it when the view was taken
	// or last resolved (NavView::resolved).
	template <typename T> struct ViewArray {
		const T*		flat  = nullptr;
		const T* const* pages = nullptr;
		std::size_t		count = 0;

		bool		set() const { return flat != nullptr || pages != nullptr; }
		std::size_t size() const { return count; }
		const T&	operator[](std::size_t i) const {
			   return pages != nullptr ? pages[i >> PagedVector<T>::kPageShift][i & PagedVector<T>::kPageMask] : flat[i];
		}
		void of(const std::vector<T>& table) {
			flat  = table.data();
			pages = nullptr;
			count = table.size();
		}
		void of(const PagedVector<T>& table) {
			flat  = nullptr;
			pages = table.pageTable();
			count = table.size();
		}
	};

	struct NavView {
		ViewArray<Vec2i64>		vertices;
		ViewArray<NavTriangle>	triangles;
		ViewArray<std::uint8_t> live; // unset: every slot is live
		const void*				owner = nullptr;
		std::int32_t (*locateIn)(const void* owner, const Vec2i64& p)						= nullptr;
		const MeshComponents* (*componentsIn)(const void* owner)							= nullptr;
		bool (*widthsCurrentIn)(const void* owner)											= nullptr;
		std::uint64_t (*epochIn)(const void* owner)											= nullptr;
		void (*resolveIn)(const void* owner, NavView& view)									= nullptr;

		std::int32_t	   triangleSlots() const { return static_cast<std::int32_t>(triangles.size()); }
		bool			   isLive(std::int32_t t) const { return !live.set() || live[static_cast<std::size_t>(t)] != 0; }
		const NavTriangle& triangle(std::int32_t t) const { return triangles[static_cast<std::size_t>(t)]; }
		const Vec2i64&	   vertex(std::uint32_t v) const { return vertices[v]; }
		// This view with the owner's slot tables as they are now.
		NavView resolved() const {
			NavView view = *this;
			resolveIn(owner, view);
			return view;
		}
		std::int32_t	   locate(const Vec2i64& p) const { return locateIn(owner, p); }
		// The owner's connected-component labels, current with its triangles; null when it
		// keeps none, and then a path query skips its component pre-check.
		const MeshComponents* components() const { return componentsIn(owner); }
		// False while corridor widths lag behind a patch; queries read widths.
		bool widthsCurrent() const { return widthsCurrentIn(owner); }
		// Changes whenever the triangles or their tags do, to a value no mesh state in the
		// process has had before (nextMeshGeneration).
		std::uint64_t epoch() const { return epochIn(owner); }
	};

	NavView navView(const NavMesh& mesh);

	// Scratch for corridor-width searches, reused across calls.
	struct WidthSearch {
		ScratchArray<std::uint32_t> visited; // visited[t] == stamp: seen by the current search
		std::uint32_t				stamp = 0;
	};

	// Demyen-Buro width at apex `apex` of triangle `tri`: the widest disc diameter (mm)
	// that passes between the two edges meeting there, against common-knowledge
	// obstacles. A width at or above maxClearanceMm reads kUnconstrainedWidth, and the
	// search never looks farther than maxClearanceMm from the apex, so the width depends
	// only on `tri` and the triangles that come within maxClearanceMm of the apex.
	std::int64_t corridorWidthMm(const NavView& view, WidthSearch& search, std::int32_t tri, int apex, std::int64_t maxClearanceMm);

} // namespace geometry::nav
