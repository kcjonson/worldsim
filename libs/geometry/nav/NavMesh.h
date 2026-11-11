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
		// A waterline body arrives as separate rings (a CCW outer boundary plus CW
		// land-island holes), all blocked. holeCapable marks those rings so face
		// classification uses even-odd containment parity among them (a point inside
		// an even number of nested hole-capable rings is land = floor; odd = water).
		// Everything else (river channels, ponds, flora, walls, the border) leaves this
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
		// The detail-class obstacle tags (flora, later movers' footprints): a path query may
		// squeeze a disc past them at a price (NavTriangle::structuralWidthMm). Empty: none.
		std::vector<std::int64_t> detailTags;
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
		// The same width with detail-class faces (the mesh's detail tags) read as open
		// ground: a gap that admits a disc here but not by edgePairWidthMm is narrowed by
		// detail, and a query that prices detail may squeeze through it. Where detail
		// alone made the apex an obstacle vertex, a disc centred on it counts too, since
		// the wedge measure is too narrow at a free apex. Equal to edgePairWidthMm when the
		// mesh has no detail tags; capped at the mesh's squeeze width (apexWidthsMm).
		std::array<std::int64_t, 3> structuralWidthMm = {kUnconstrainedWidth, kUnconstrainedWidth, kUnconstrainedWidth};
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
	// so the answer never depends on input order or edit history. SOLID rings (flora,
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

	// A process-wide counter, never repeating: what a mesh state is known by (see
	// NavView::epoch).
	std::uint64_t nextMeshGeneration();

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
		std::vector<std::int64_t> detailTags; // NavMeshInput::detailTags
		// Taken when the mesh is made; a copy carries its source's along with its content.
		// A caller that edits a built mesh in place takes a fresh one.
		std::uint64_t generation = nextMeshGeneration();
	};

	// Build the navmesh from tagged input. The triangles tile the walkable bounds:
	// no face is dropped, however its rings touch, pinch, or nest.
	NavMesh buildNavMesh(const NavMeshInput& input);

	// The triangles a path query, a corridor width, or a forest reads, and the means to
	// locate a point among them. NavMesh and PatchableMesh each produce one, so each of
	// those algorithms has one implementation. A slot `live` marks dead is never located,
	// never a live triangle's neighbour, and never a forest node.
	//
	// A view reads its owner as it is when used, never as it was when taken: forests,
	// width currency, and the patch epoch come through the owner, and every function
	// taking a view reads the slot tables through resolved(), so a view kept across a
	// patch sees the patched mesh and its missing forests.
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
		const ReachabilityForest* (*forestIn)(const void* owner, bool truth)				= nullptr;
		bool (*widthsCurrentIn)(const void* owner)											= nullptr;
		std::uint64_t (*epochIn)(const void* owner)											= nullptr;
		void (*resolveIn)(const void* owner, NavView& view)									= nullptr;
		const std::vector<std::int64_t>* detailTags											= nullptr; // the owner's; null: none

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
		// The truth or terrain forest over the live triangles; null when the owner has
		// none current, and then a path query skips its forest pre-check.
		const ReachabilityForest* forest(bool truth) const { return forestIn(owner, truth); }
		// False while corridor widths lag behind a patch; queries read widths.
		bool widthsCurrent() const { return widthsCurrentIn(owner); }
		// Changes whenever the triangles or their tags do, to a value no mesh state in the
		// process has had before (nextMeshGeneration).
		std::uint64_t epoch() const { return epochIn(owner); }
	};

	NavView navView(const NavMesh& mesh);

	struct ReachabilityForests {
		ReachabilityForest truth;
		ReachabilityForest terrain;
	};

	// Working storage for a forest build, kept between builds so a rebuild allocates
	// nothing once it has grown.
	struct ForestScratch {
		// A portal between two triangles with its sound disc-diameter capacity, owned
		// by the lower triangle.
		struct Portal {
			std::int32_t triA = -1;
			std::int32_t triB = -1;
			std::int64_t cap  = kUnconstrainedWidth;
		};
		std::vector<Portal>		  portals;
		std::vector<Portal>		  sorted;
		std::vector<std::uint8_t> truthNode;
		std::vector<std::uint8_t> terrainNode;
		std::vector<std::int32_t> parent;
		std::vector<std::int32_t> setNode;
		std::vector<std::int32_t> nodeParent;
		std::vector<std::int32_t> root;
	};

	// Both forests over the view's live triangles, from their corridor widths and door
	// tags (see ReachabilityForest), built into `out` and reusing its storage.
	void				buildReachabilityForests(const NavView& view, ForestScratch& scratch, ReachabilityForests& out);
	ReachabilityForests buildReachabilityForests(const NavView& view);

	// Scratch for corridor-width searches, reused across calls.
	struct WidthSearch {
		ScratchArray<std::uint32_t>				  visited; // visited[t] == stamp: seen by the current search
		std::uint32_t							  stamp = 0;
		std::vector<std::pair<std::int32_t, int>> star; // an apex's triangles and their corners on it
		// Which vertices an obstacle edge meets, kept for one mesh state (owner and epoch):
		// [0] with nothing read as open, [1] with the detail tags open.
		const void*								  vertexOwner = nullptr;
		std::uint64_t							  vertexEpoch = 0;
		std::uint32_t							  vertexStamp = 0;
		std::array<ScratchArray<std::uint32_t>, 2> vertexSeen;
		std::array<ScratchArray<std::uint8_t>, 2>  vertexObstacle;
	};

	// Demyen-Buro width at apex `apex` of triangle `tri`: the widest disc diameter (mm)
	// that passes between the two edges meeting there, against common-knowledge
	// obstacles. A width at or above maxClearanceMm reads kUnconstrainedWidth, and the
	// search never looks farther than maxClearanceMm from the apex, so the width depends
	// only on `tri` and the triangles that come within maxClearanceMm of the apex.
	std::int64_t corridorWidthMm(const NavView& view, WidthSearch& search, std::int32_t tri, int apex, std::int64_t maxClearanceMm);

	// An apex's two stored widths (NavTriangle::edgePairWidthMm and structuralWidthMm),
	// the second with the view's detail tags read as open ground. The full width reads
	// nothing farther than maxClearanceMm from the apex; the structural one, which only
	// decides squeezes, nothing farther than maxSqueezeMm (where it is lower), and reads
	// maxSqueezeMm when it is at least that: a squeeze opens for discs up to that wide.
	struct ApexWidths {
		std::int64_t full		= kUnconstrainedWidth;
		std::int64_t structural = kUnconstrainedWidth;
	};
	ApexWidths apexWidthsMm(const NavView& view, WidthSearch& search, std::int32_t tri, int apex, std::int64_t maxClearanceMm,
							std::int64_t maxSqueezeMm);

	// The reconstruction-tree node whose subtree is every triangle a disc of `diameterMm` can
	// POSSIBLY reach from `tri`: tri's highest ancestor whose capacity admits the disc. A goal
	// triangle is under it iff the widest disc any path from tri to it admits is >= diameterMm.
	// -1 when tri is a non-node. O(log n).
	std::int32_t reachRootInForest(const ReachabilityForest& forest, std::int32_t tri, std::int64_t diameterMm);

	// Is triangle `tri` in the subtree of reconstruction-tree node `node`? False for a non-node
	// triangle or node -1. O(log n).
	bool inForestSubtree(const ReachabilityForest& forest, std::int32_t node, std::int32_t tri);

} // namespace geometry::nav
