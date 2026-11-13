#pragma once

#include "../arrangement/Arrangement.h"
#include "../core/PagedVector.h"
#include "../core/ScratchArray.h"
#include "../core/Vec2i64.h"

#include <array>
#include <compare>
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
// COMMON-KNOWLEDGE obstacle feature in the wedge, the mesh edge included (outside
// the mesh is impassable); edgeClearWidthMm[i] is a door gap's clear width on a
// portal edge. See NavMesh.cpp for the exact-integer adaptation (no bare point
// obstacles; clearance is distance-to-feature, never edge length, so a sliver
// away from obstacles and the mesh edge stays unconstrained).
//
// DEFERRED (still not implemented): clearance against BELIEF-DEPENDENT walls
// (faceBlocker > 0). Those widths vary per agent belief and only bite oversized
// agents in built corridors; construction guarantees walkable wall gaps >= 0.7 m,
// so for the one shipped agent size (0.3 m radius, 0.6 m diameter) every wall
// portal fits. Width filtering here covers common-knowledge terrain/obstacles, the
// mesh edge, and door gaps only.

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
	// the walkable bounds (a border, or several disjoint ones).
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
		// A blocked ring that tags no face: its outline is in the mesh and it is in the
		// cover of the ground inside it, but classifyCover passes over it, so that ground
		// keeps the tags of whatever else covers it and every query crosses it. For ground
		// a reader of PatchableMesh::cover must tell apart from the ground around it.
		bool				 coverOnly	  = false;
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
	// What ground a point is, read exactly from the input rings at one sample: a
	// triangle's centroid, held as the integer point a+b+c in 3x space. classifyCover
	// turns a cover into the tags buildNavMesh and PatchableMesh both give a face.

	// An input ring as classification reads it: scaled by 3, with its box.
	struct SampleRing {
		std::vector<Vec2i64> ring3;
		Vec2i64				 lo3;
		Vec2i64				 hi3;
		bool				 blocked		= false;
		std::int64_t		 provenance		= kNoProvenance;
		std::int64_t		 opening		= kNoOpening;
		bool				 holeCapable	= false;
		bool				 coverOnly		= false;
		Int128				 areaDoubledAbs = Int128(0); // smallest-containing ranking (unscaled)
		bool				 counterClockwise = true;
	};

	SampleRing sampleRing(const NavInputPolygon& poly);

	// Strictly inside: a sample on the ring's boundary is outside it.
	bool ringContainsSample(const SampleRing& ring, const Vec2i64& sample3);

	// Ground test for a bounds ring: whether the sample moved by (e, e^2), for an
	// infinitesimal e, is inside. Off the outline it is ringContainsSample; on it, of
	// rings sharing that stretch of outline without overlapping, exactly one holds the
	// sample. A face's sample lies on a bounds edge where the edge is dissolved, and an
	// old triangle's patch sample can lie on a removed ring's outline.
	bool boundsContainSample(const SampleRing& ring, const Vec2i64& sample3);

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
	// over water. COVER-ONLY rings tag nothing and are passed over.
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

	// --- Ground: the union of the ground rings -------------------------------------
	// Ground rings (blocked == false) may overlap, nest, touch, or share sides; ground is
	// wherever a point is inside any of them. A ground ring edge is arranged only along its
	// boundary intervals, the stretches where ground differs across it; the rest of it is
	// dissolved, with no constraint, mesh edge, or vertex. buildNavMesh and PatchableMesh
	// apply this one rule.

	using RingId			 = std::int32_t;
	constexpr RingId kNoRing = -1;

	// Original edge `edge` of a ring: outline[edge] -> outline[(edge + 1) % n]. The ring
	// is a PatchableMesh ring id, or for buildNavMesh an index into its input.
	struct EdgeSource {
		RingId		 ring = kNoRing;
		std::int32_t edge = 0;

		auto operator<=>(const EdgeSource&) const = default;
	};

	// A ground ring as groundBoundaries reads it.
	struct GroundRing {
		const std::vector<Vec2i64>* outline = nullptr;
		Vec2i64						lo;
		Vec2i64						hi;
		bool						counterClockwise = true;
		RingId						id				 = kNoRing;
	};

	GroundRing groundRing(const std::vector<Vec2i64>& outline, RingId id);

	// The point a + (num / den)(b - a) of an edge a -> b, den > 0.
	struct EdgeParam {
		std::int64_t num = 0;
		std::int64_t den = 1;
	};

	// A boundary interval of a ground edge: exactly the stretch [fromAt, toAt] of it, and
	// the segment the arrangement takes, from -> to. An end on the edge's own corner or on
	// another outline's vertex is that point; an end where another ground edge crosses is
	// that crossing rounded as the arrangement rounds two crossing segments.
	struct GroundInterval {
		Vec2i64	  from;
		Vec2i64	  to;
		EdgeParam fromAt;
		EdgeParam toAt;

		bool operator==(const GroundInterval& other) const { return from == other.from && to == other.to; }
	};

	// Working storage for groundBoundaries, reused across calls.
	struct GroundBoundaryScratch {
		struct Event {
			EdgeParam	 at;
			int			 kind = 0;
			std::int32_t ring = 0;	 // index into `near`
			std::int32_t edge = 0;
			bool		 exact = false; // `point` is the exact crossing
			Vec2i64		 point;
		};
		struct Collinear {
			EdgeParam  lo;
			EdgeParam  hi;
			EdgeSource source;
			Vec2i64	   a;
			Vec2i64	   b;
		};
		std::vector<Event>		   events;
		std::vector<Collinear>	   collinear;
		std::vector<std::uint8_t>  inside; // per near ring
		std::vector<std::pair<EdgeSource, std::array<Vec2i64, 2>>> lines;
	};

	// groundBoundaries orders crossings exactly while every ground ring it reads spans less
	// than this on each axis: two rings whose boxes meet are then closer than twice it at
	// every pair of points, so each product of two differences fits in an int64 and each
	// comparison of two quotients in an Int128.
	constexpr std::int64_t kMaxGroundExtentMm = std::int64_t{1} << 30;

	// The boundary intervals of edge `edge` of `ring`, in order along it, given every other
	// ground ring whose box meets the edge's box (`near` may hold `ring` itself and rings
	// that do not meet it). Where another ring's edge runs along this one with its inside
	// on the same side, the shared stretch is input once, by the lower EdgeSource.
	void groundBoundaries(const GroundRing& ring, std::int32_t edge, const std::vector<GroundRing>& near, GroundBoundaryScratch& scratch,
						  std::vector<GroundInterval>& out);

	// The arrangement input of the rings, each segment with the ring index and edge it
	// comes from: a blocked ring's every edge, and a ground ring's boundary intervals. Rings
	// under three vertices add nothing.
	void ringSegments(const std::vector<const NavInputPolygon*>& rings, std::vector<std::pair<EdgeSource, std::array<Vec2i64, 2>>>& out);

	// ringSegments as arrangement input, each segment's index tag(ring, edge).
	template <typename Tag>
	void ringsArrangementInput(const std::vector<const NavInputPolygon*>& rings, Tag tag, std::vector<InputSegment>& segments) {
		std::vector<std::pair<EdgeSource, std::array<Vec2i64, 2>>> pieces;
		ringSegments(rings, pieces);
		for (const auto& [source, ends] : pieces) {
			segments.push_back({ends[0], ends[1], tag(source.ring, source.edge)});
		}
	}

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
