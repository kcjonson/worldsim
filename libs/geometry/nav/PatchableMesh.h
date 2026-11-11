#pragma once

#include "../core/Vec2i64.h"
#include "NavMesh.h"

#include <array>
#include <compare>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
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
// Corridor widths, door portals, and reachability forests are not maintained here:
// edgeOpening, edgeClearWidthMm, and edgePairWidthMm stay at their defaults.

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
		NavInputPolygon polygon; // original outline and tags
		SampleRing		sample;
		Vec2i64			lo;
		Vec2i64			hi;
		bool			live = false;
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

	struct PatchReport {
		std::vector<RingId>		  added;		  // ids of PatchEdits::adds, in order
		std::vector<std::int32_t> freedTriangles; // ids whose old triangle is gone, sorted
		std::vector<std::int32_t> newTriangles;	  // ids holding a new triangle, sorted
		Vec2i64					  lo;			  // box of every freed and new triangle (both lists empty: unset)
		Vec2i64					  hi;
		std::int32_t			  passes = 0; // re-derivations until the boundary held
	};

	// The outline with every edge longer than maxEdgeMm cut into near-equal pieces at
	// lattice points on it, so each piece is exactly on the original edge (on an axis-
	// aligned side, every millimetre). An edge whose lattice is too coarse keeps fewer
	// cuts. maxEdgeMm 0 returns the outline whole.
	std::vector<Vec2i64> splitLongEdges(const std::vector<Vec2i64>& outline, std::int64_t maxEdgeMm);

	class PatchableMesh {
	  public:
		// A bounds ring enters the mesh with its edges split to at most maxBoundsEdgeMm
		// (splitLongEdges), and ring() returns that outline. A patch re-derives a changed
		// edge whole, so this bounds the cost of a ring crossing a long chunk side.
		explicit PatchableMesh(std::int64_t maxBoundsEdgeMm = 0) : maxBoundsEdgeMm(maxBoundsEdgeMm) {}

		RingId		addRing(const NavInputPolygon& ring);
		void		removeRing(RingId ring);
		void		moveRing(RingId ring, const std::vector<Vec2i64>& outline);
		PatchReport applyPatch(const PatchEdits& edits);

		const PatchReport& lastPatch() const { return report; }

		const std::vector<Vec2i64>&		vertices() const { return verts; }
		bool							vertexLive(std::uint32_t v) const { return vertexUses[v] > 0; }
		const std::vector<NavTriangle>& triangles() const { return tris; }
		bool							triangleLive(std::int32_t t) const { return live[static_cast<std::size_t>(t)] != 0; }
		std::int32_t					liveTriangleCount() const { return liveTriangles; }

		// The ring edges a triangle edge lies on; empty when the edge is unconstrained.
		const std::vector<EdgeSource>& edgeSources(std::int32_t tri, int edge) const;
		// The rings the triangle lies inside, ascending.
		const std::vector<RingId>& cover(std::int32_t tri) const;

		std::int32_t		  ringSlots() const { return static_cast<std::int32_t>(rings.size()); }
		const RegisteredRing& ring(RingId id) const { return rings[static_cast<std::size_t>(id)]; }

		// The live triangle containing p (edges and vertices included), the lowest id on
		// a tie; -1 off the mesh.
		std::int32_t locate(const Vec2i64& p) const;

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
			std::vector<Value>			   values;
			std::vector<std::int32_t>	   refs;
			std::vector<std::int32_t>	   freeIds;
			std::map<Value, std::int32_t> ids;
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
			// The box rides along so a query rejects an entry without touching its owner.
			struct Entry {
				std::int32_t id = -1;
				Vec2i64		 lo;
				Vec2i64		 hi;
			};
			struct Cell {
				std::uint64_t	   key = 0;
				std::vector<Entry> entries;
				std::int32_t	   levelSlot = -1; // position in levelCells
			};

			static int			 levelFor(const Vec2i64& lo, const Vec2i64& hi);
			static std::uint64_t keyOf(int level, std::int64_t cx, std::int64_t cy);
			std::size_t			 home(std::uint64_t key) const;
			std::int32_t		 find(std::uint64_t key) const;
			std::int32_t		 findOrAdd(std::uint64_t key, int level);
			void				 dropCell(std::int32_t cell, int level);

			std::vector<std::uint64_t>					   slotKeys; // kEmptySlot or a cell key
			std::vector<std::int32_t>					   slotCells;
			std::size_t									   occupied = 0;
			std::vector<Cell>							   cells;
			std::vector<std::int32_t>					   freeCells;
			std::array<std::vector<std::int32_t>, kLevels> levelCells;
			std::uint64_t								   levelMask = 0; // levels holding any id
		};

		struct VertexHash {
			std::size_t operator()(const Vec2i64& p) const;
		};

		class Patch;
		friend class Patch;

		std::int32_t locateScaled(const Vec2i64& p, std::int64_t scale) const;
		template <typename Visit> void forEachLiveInBox(const Vec2i64& lo, const Vec2i64& hi, Visit visit) const;
		template <typename Visit> void forEachAroundVertex(std::uint32_t v, Visit visit) const;
		void triangleBox(std::int32_t t, Vec2i64& lo, Vec2i64& hi) const;
		std::vector<RingId> ringsInBox(const Vec2i64& lo, const Vec2i64& hi) const;
		bool				nearBoundsEdge(const Vec2i64& lo, const Vec2i64& hi) const;

		std::int32_t acquireCover(const std::vector<RingId>& ringSet);
		void		 setEdgeSources(std::int32_t t, int e, std::int32_t sourceId);
		void		 clearEdgeSources(std::int32_t t, int e);
		void		 unregisterEdgeSources(std::int32_t t, int e);
		void		 legalize(std::vector<std::pair<std::int32_t, int>>& stack, std::vector<std::int32_t>& flipped);
		void		 flip(std::int32_t t, int e);

		static std::uint64_t sourceKey(const EdgeSource& s);

		std::int64_t				maxBoundsEdgeMm = 0;
		std::vector<RegisteredRing> rings;
		std::vector<RingId>			freeRings;

		std::vector<Vec2i64>							verts;
		std::vector<std::int32_t>						vertexUses; // live triangle corners on the vertex
		std::vector<std::uint32_t>						freeVertices;
		std::unordered_map<Vec2i64, std::uint32_t, VertexHash> vertexAt;

		std::vector<NavTriangle>				 tris;
		std::vector<std::uint8_t>				 live;
		std::vector<std::array<std::int32_t, 3>> edgeSourceIds; // interned source list, -1 unconstrained
		std::vector<std::int32_t>				 coverIds;
		std::vector<std::int32_t>				 freeTriangles;
		std::int32_t							 liveTriangles = 0;

		InternTable<std::vector<EdgeSource>> sourceLists;
		InternTable<std::vector<RingId>>	 covers;
		std::vector<FaceTags>				 coverTags; // per cover id

		// Every half-edge (t * 3 + e) each original ring edge lies under.
		std::unordered_map<std::uint64_t, std::vector<std::int32_t>> halfEdgesOf;

		mutable std::vector<std::uint32_t> visitStamp; // per triangle, for box queries
		mutable std::uint32_t			   stamp		= 0;
		mutable bool					   boxQueryOpen = false;

		BoxGrid		grid;	   // live triangles
		BoxGrid		ringGrid;  // live rings, by outline box
		PatchReport report;
	};

} // namespace geometry::nav
