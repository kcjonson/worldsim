#pragma once

#include "../core/IncrementalHashMap.h"
#include "../core/PagedVector.h"
#include "../core/Vec2i64.h"
#include "MeshComponents.h"
#include "NavMesh.h"

#include <array>
#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

// A persistent nav mesh that takes ring edits as patches (nav-mesh-variable-resolution
// section 4.3). Every add, remove, and move cuts a hole along the ring edges whose
// geometry is in question, rebuilds it from the rings' original outlines with the same
// arrangement and constrained Delaunay a first build uses, splices it back, and
// legalizes the boundary. The vertices and constrained edges after any sequence of
// patches are meant to equal a from-scratch build of the same rings (section 4.3.2).
//
// Triangle and vertex ids are stable: a patch frees some slots and reuses them, and
// every id it did not touch still means what it meant. Ground exists only where a
// triangle's cover set holds a bounds ring (blocked == false); elsewhere there are no
// triangles. Bounds rings may share edges and vertices but their interiors must not
// overlap.
//
// The mesh carries what path queries and reachability read: face tags from the rings,
// door tags on door edges, corridor widths capped at a maximum clearance (brought up to
// date by refreshWidths), and connected-component labels (kept by every patch).
// view() hands all of it to PathQuery, which serves NavMesh too.

namespace geometry::nav {

	using RingId		   = std::int32_t;
	constexpr RingId kNoRing = -1;

	// Original edge `edge` of a ring: outline[edge] -> outline[(edge + 1) % n].
	struct EdgeSource {
		RingId		 ring = kNoRing;
		std::int32_t edge = 0;

		auto operator<=>(const EdgeSource&) const = default;
	};

	struct RegisteredRing {
		NavInputPolygon			  polygon; // original outline and tags
		SampleRing				  sample;
		Vec2i64					  lo;
		Vec2i64					  hi;
		std::vector<std::int32_t> edgeIds; // per outline edge, its id in the edge index
		bool					  live = false;
	};

	struct RingMove {
		RingId				 ring = kNoRing;
		std::vector<Vec2i64> outline;
	};

	// One patch: every edit re-derives in one hole, so overlapping edits merge.
	struct PatchEdits {
		std::vector<NavInputPolygon> adds;
		std::vector<RingId>			 removes;
		std::vector<RingMove>		 moves;
	};

	// Why a triangle id is in PatchReport::changes; one id can carry several.
	constexpr std::uint8_t kTriangleFreed	= 1 << 0; // its old triangle is gone
	constexpr std::uint8_t kTriangleCreated = 1 << 1; // it holds a triangle the patch made
	constexpr std::uint8_t kTriangleFlipped = 1 << 2; // a flip at the hole's boundary remade it (freed and created too)
	constexpr std::uint8_t kTriangleBorder	= 1 << 3; // kept, but what lies across one of its edges changed or went
	constexpr std::uint8_t kTriangleWidths	= 1 << 4; // refreshWidths changed one of its corridor widths

	struct ChangedTriangle {
		std::int32_t id	 = -1;
		std::uint8_t why = 0;

		bool operator==(const ChangedTriangle&) const = default;
	};

	// What a patch (applyPatch) or a width refresh (refreshWidths) changed. Anything that
	// holds triangle ids checks them against `changes`.
	struct PatchReport {
		std::vector<RingId>			 added;			 // ids of PatchEdits::adds, in order
		std::vector<ChangedTriangle> changes;		 // every id touched, ascending, each once
		Vec2i64						 lo;			 // patch: box of every freed and new triangle; refresh: of every changed one
		Vec2i64						 hi;			 // (nothing changed: unset)
		std::int32_t				 passes = 0;	 // re-derivations until the boundary held
		// The patch may have opened a way for some mover and belief: it removed or moved a
		// ring, or added ground, a door, a hole-capable ring, or a wall. Only solid water and
		// obstacle rings arriving leave it false.
		bool opens = false;
	};

	// The outline with every edge longer than maxEdgeMm cut into near-equal pieces at
	// lattice points on it, so each piece is exactly on the original edge (on an axis-
	// aligned side, every millimetre). An edge whose lattice is too coarse keeps fewer
	// cuts. maxEdgeMm 0 returns the outline whole.
	std::vector<Vec2i64> splitLongEdges(const std::vector<Vec2i64>& outline, std::int64_t maxEdgeMm);

	using DoorId		   = std::int32_t;

	// Covers any agent the game could have: a 2 m radius, over six times the colonist's.
	constexpr std::int64_t kDefaultMaxClearanceMm = 4000;

	struct PatchableMeshArgs {
		// A bounds ring enters the mesh with its edges split to at most this
		// (splitLongEdges), and ring() returns that outline; 0 keeps them whole. A patch
		// re-derives a changed edge whole, so this bounds the cost of a ring crossing a
		// long chunk side.
		std::int64_t maxBoundsEdgeMm = 0;
		// Corridor widths are exact below this and read kUnconstrainedWidth at or above
		// it, so a patch refreshes only the widths within this distance of what it changed.
		std::int64_t maxClearanceMm = kDefaultMaxClearanceMm;
		// The face tags component labels read as open ground (MeshComponents).
		std::vector<std::int64_t> componentOpenTags;
	};

	class PatchableMesh {
	  public:
		explicit PatchableMesh(const PatchableMeshArgs& args = {});

		PatchReport applyPatch(const PatchEdits& edits);

		const PagedVector<Vec2i64>&		vertices() const { return verts; }
		const PagedVector<NavTriangle>& triangles() const { return tris; }
		bool							triangleLive(std::int32_t t) const { return live[static_cast<std::size_t>(t)] != 0; }
		std::int32_t					liveTriangleCount() const { return liveTriangles; }

		// The ring edges a triangle edge lies on; empty when the edge is unconstrained.
		const std::vector<EdgeSource>& edgeSources(std::int32_t tri, int edge) const;
		// The rings the triangle lies inside, ascending.
		const std::vector<RingId>& cover(std::int32_t tri) const;

		const RegisteredRing& ring(RingId id) const { return rings[static_cast<std::size_t>(id)]; }

		// The live triangle containing p (edges and vertices included), the lowest id on
		// a tie; -1 off the mesh.
		std::int32_t locate(const Vec2i64& p) const;

		// A door tags the triangle edge (a, b) with its opening and clear width wherever
		// that edge exists, through every later patch; where two doors name one edge, the
		// lower id tags it. Tags only: no geometry changes.
		DoorId addDoor(const DoorPortal& door);
		void   removeDoor(DoorId door);

		// Corridor widths (NavTriangle::edgePairWidthMm, capped at maxClearanceMm) lag
		// behind patches: a patch only records where it changed the mesh, and this brings
		// every width up to date, once for all the patches since the last call. The report
		// lists the triangles whose widths changed, each kTriangleWidths.
		PatchReport refreshWidths();
		bool widthsCurrent() const { return freshWidthTriangles.empty() && pendingWidthSeeds.empty(); }

		// What path queries read (see NavView).
		NavView view() const;

		// Moves on every patch and door edit, to a value no mesh state has had before
		// (nextMeshGeneration): anything keyed by triangle id is stale once it moves.
		std::uint64_t epoch() const { return patchEpoch; }

		// Connected components, brought up to date by every patch.
		const MeshComponents& components() const { return labels; }

		// Full consistency check; the first violation found, or empty.
		std::string validate() const;

	  private:
		template <typename Value> class InternTable {
		  public:
			std::int32_t acquire(const Value& value);
			void		 release(std::int32_t id);
			void		 retain(std::int32_t id) { ++refs[static_cast<std::size_t>(id)]; }

			const Value& get(std::int32_t id) const { return values[static_cast<std::size_t>(id)]; }
			std::int32_t slots() const { return static_cast<std::int32_t>(values.size()); }
			std::int32_t refCount(std::int32_t id) const { return refs[static_cast<std::size_t>(id)]; }

		  private:
			// Ids by each value's hash, the ids sharing a hash chained through sameHash:
			// a lookup, or a fresh slot once a freed value's storage has grown, allocates
			// nothing, and nothing grows by moving what is there.
			struct HashOf {
				std::uint64_t operator()(std::uint64_t hash) const {
					hash ^= hash >> 31;
					hash *= 0xBF58476D1CE4E5B9ULL;
					return hash ^ (hash >> 29);
				}
			};
			PagedVector<Value>										values; // a freed id keeps its value's capacity
			PagedVector<std::uint64_t>								hashes; // per id
			PagedVector<std::int32_t>								refs;
			PagedVector<std::int32_t>								sameHash; // the next id with this id's hash, -1 none
			std::vector<std::int32_t>								freeIds;
			IncrementalHashMap<std::uint64_t, std::int32_t, HashOf> byHash; // the first id with a hash
		};

		// Boxes by id, for point location and box queries: a grid per level, cell side
		// kBaseCell << level, every level's cells in one open-addressed hash table. An id
		// sits in the level whose cell is at least its box's larger side, in the up to
		// four cells its box overlaps.
		class BoxGrid {
		  public:
			static constexpr int		  kLevels	= 40;
			static constexpr std::int64_t kBaseCell = 256;

			void insert(std::int32_t id, const Vec2i64& lo, const Vec2i64& hi);
			void erase(std::int32_t id, const Vec2i64& lo, const Vec2i64& hi);

			// Visits every id whose box overlaps the box, the box given in `scale` times mesh
			// space; an id in several cells the box overlaps is visited once per cell.
			template <typename Visit> void forEachInBox(const Vec2i64& lo, const Vec2i64& hi, std::int64_t scale, Visit visit) const;

			// Visits the ids whose box holds p (in `scale` times mesh space), finest level
			// first, until `visit` returns true. Each comes up once.
			template <typename Visit> void forEachAtPoint(const Vec2i64& p, std::int64_t scale, Visit visit) const;

			template <typename Visit> void forEachEntry(Visit visit) const;

		  private:
			// A cell by level and full cell coordinates: no packing, so no coordinate the
			// mesh can hold aliases another cell.
			struct CellKey {
				std::int64_t x	   = 0;
				std::int64_t y	   = 0;
				std::int32_t level = -1;
				bool		 operator==(const CellKey&) const = default;
			};
			// The box rides along so a query rejects an entry without touching its owner.
			struct Entry {
				std::int32_t id = -1;
				Vec2i64		 lo;
				Vec2i64		 hi;
			};
			struct Cell {
				CellKey			   key;
				std::vector<Entry> entries;
				std::int32_t	   levelSlot = -1; // position in levelCells
			};

			static int			 levelFor(const Vec2i64& lo, const Vec2i64& hi);
			std::int32_t find(const CellKey& key) const;
			std::int32_t findOrAdd(const CellKey& key);
			void				 dropCell(std::int32_t cell, int level);

			struct CellHash {
				std::uint64_t operator()(const CellKey& key) const;
			};
			IncrementalHashMap<CellKey, std::int32_t, CellHash> table; // cell by key
			PagedVector<Cell>								   cells;
			std::vector<std::int32_t>					   freeCells;
			std::array<PagedVector<std::int32_t>, kLevels> levelCells;
			std::uint64_t								   levelMask = 0; // levels holding any id
		};

		struct VertexHash {
			std::size_t operator()(const Vec2i64& p) const;
		};

		class Patch;
		friend class Patch;
		struct PatchScratch;

		// A patch's working storage, kept between patches so one allocates little. A copied
		// mesh starts with its own, empty.
		struct PatchScratchSlot {
			PatchScratchSlot();
			PatchScratchSlot(const PatchScratchSlot& other);
			PatchScratchSlot(PatchScratchSlot&& other) noexcept;
			PatchScratchSlot& operator=(const PatchScratchSlot& other);
			PatchScratchSlot& operator=(PatchScratchSlot&& other) noexcept;
			~PatchScratchSlot();

			std::unique_ptr<PatchScratch> scratch;
		};

		std::int32_t locateScaled(const Vec2i64& p, std::int64_t scale) const;
		template <typename Visit> void forEachLiveInBox(const Vec2i64& lo, const Vec2i64& hi, Visit visit);
		template <typename Visit> void forEachAroundVertex(std::uint32_t v, std::int32_t seed, Visit visit) const;
		void triangleBox(std::int32_t t, Vec2i64& lo, Vec2i64& hi) const;
		std::vector<RingId> ringsInBox(const Vec2i64& lo, const Vec2i64& hi) const;
		// found = every registered ring edge whose segment box overlaps the box, each once,
		// ascending. ids is scratch.
		void ringEdgesInBox(const Vec2i64& lo, const Vec2i64& hi, std::vector<std::int32_t>& ids, std::vector<EdgeSource>& found) const;
		void					indexRingEdges(RingId ring);
		void					unindexRingEdges(RingId ring);
		bool				nearBoundsEdge(const Vec2i64& lo, const Vec2i64& hi) const;

		std::int32_t acquireCover(const std::vector<RingId>& ringSet);
		void		 setEdgeSources(std::int32_t t, int e, std::int32_t sourceId);
		void		 clearEdgeSources(std::int32_t t, int e);
		void		 unregisterEdgeSources(std::int32_t t, int e);
		void		 legalize(std::vector<std::pair<std::int32_t, int>>& stack, std::vector<std::int32_t>& flipped);
		void		 flip(std::int32_t t, int e);

		std::int64_t pieceDeviationMm(RingId ring) const;
		void		 applyDoorTags(std::int32_t t);
		void		 retagEdge(const Vec2i64& a, const Vec2i64& b);
		static std::pair<Vec2i64, Vec2i64> edgeKey(Vec2i64 a, Vec2i64 b);

		// The half-edges a registered ring edge lies under.
		const std::vector<std::int32_t>& piecesOf(const EdgeSource& s) const;

		std::int64_t				maxBoundsEdgeMm = 0;
		std::int64_t				maxClearanceMm	= kDefaultMaxClearanceMm;
		PagedVector<RegisteredRing> rings;
		std::vector<RingId>			freeRings;

		PagedVector<Vec2i64>							verts;
		PagedVector<std::int32_t>						vertexUses; // live triangle corners on the vertex
		std::vector<std::uint32_t>						freeVertices;
		IncrementalHashMap<Vec2i64, std::uint32_t, VertexHash> vertexAt;

		PagedVector<NavTriangle>				 tris;
		PagedVector<std::uint8_t>				 live;
		PagedVector<std::array<std::int32_t, 3>> edgeSourceIds; // interned source list, -1 unconstrained
		PagedVector<std::int32_t>				 coverIds;
		std::vector<std::int32_t>				 freeTriangles;
		std::int32_t							 liveTriangles = 0;

		InternTable<std::vector<EdgeSource>> sourceLists;
		InternTable<std::vector<RingId>>	 covers;
		std::vector<FaceTags>				 coverTags; // per cover id


		// A patch's box queries. No read of the mesh writes anything, so readers on
		// several threads may share it while no patch runs.
		ScratchArray<std::uint32_t> visitStamp; // per triangle
		std::uint32_t			   stamp		= 0;
		bool					   boxQueryOpen = false;

		BoxGrid					grid;		 // live triangles
		BoxGrid					ringGrid;	 // live rings, by outline box
		BoxGrid					edgeGrid;	 // live rings' edges, by segment box
		PagedVector<EdgeSource> edgeOf;		 // edge index id -> ring edge
		PagedVector<std::vector<std::int32_t>> pieces; // per edge index id: half-edges t * 3 + e
		std::vector<std::int32_t> freeEdgeIds;
		PatchScratchSlot		patchScratch;

		std::vector<DoorPortal>									   doors;
		std::vector<std::uint8_t>								   doorLive;
		std::vector<DoorId>										   freeDoors;
		std::map<std::pair<Vec2i64, Vec2i64>, std::vector<DoorId>> doorsAt; // ascending ids per edge

		// An edge where unchanged ground (tri) meets what a patch changed (see refreshWidths).
		struct WidthSeed {
			std::int32_t tri = -1;
			Vec2i64		 a;
			Vec2i64		 b;
		};
		WidthSearch			   widthSearch;
		std::vector<std::int32_t> freshWidthTriangles; // new or flipped since refreshWidths
		std::vector<WidthSeed>	   pendingWidthSeeds;	// edges whose far side went since then
		// refreshWidths scratch
		std::vector<std::int32_t>							staleApexes; // 3 * t + apex
		std::vector<WidthSeed>								widthSeeds;
		std::vector<std::pair<std::uint64_t, std::int32_t>> seedBuckets;
		std::vector<std::int32_t>							seedCellRuns;
		std::vector<std::int32_t>							seedCellTable;
		std::vector<std::int32_t>							widthWalk;
		ScratchArray<std::uint32_t>							freshMark;
		ScratchArray<std::uint32_t>							walkMark;
		std::uint32_t										freshStamp = 0;
		std::uint32_t										walkStamp  = 0;

		std::uint64_t  patchEpoch = nextMeshGeneration();
		MeshComponents labels;
	};

} // namespace geometry::nav
