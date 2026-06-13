#pragma once

#include "../ecs/EntityID.h"

#include <boolean/RingBoolean.h>
#include <core/Vec2i64.h>
#include <polygon/Polygon.h>

#include <math/Types.h>

#include <cstdint>
#include <string>
#include <vector>

// Topology store for construction foundations and walls: the source of truth
// for structure geometry, independent of ECS and rendering (building-
// construction D4). Committed geometry is integer millimeters (D1); the editor's
// float-meter previews quantize on commit. Foundations are Epic C; the wall
// topology (vertices/segments/openings of D4) is Epic D phase 1, built here. The
// rooms table (D6) stays derived elsewhere.
//
// Invariant boundary. ConstructionWorld enforces only the STRUCTURAL/TOPOLOGY
// invariants downstream consumers (face extraction, nav rasterization, band
// generation, rendering) depend on:
//   Foundations: each ring is simple, counter-clockwise, has non-degenerate
//     area, and no two foundations overlap in their interiors.
//   Walls: no zero-length segment, no X-crossing (two segment interiors
//     properly crossing), valid vertex references, and a T-junction (an
//     endpoint landing on another segment's interior) is pre-split into two
//     incident segments so the graph stays planar with shared vertices only.
// The editor-facing constraint primitives (min corner angle, vertex spacing,
// edge clearance, min/max area for foundations; min segment length, min junction
// angle, parallel clearance, host-foundation containment for walls) are
// draw-time UX checks the ConstructionValidator owns (D11); they are
// deliberately NOT run here, so a programmatic or out-of-editor caller can still
// commit any topologically valid geometry. Per D4 this store rejects invalid
// input and never repairs it.

namespace engine::construction {

	using FoundationId = std::uint64_t;

	constexpr FoundationId kInvalidFoundation = 0;

	// Lifecycle state kept minimal on purpose. The intermediate gameplay states
	// from the design spec (clearing footprint, delivering materials, under
	// construction) are progress on the ECS mirror entity, not topology, so they
	// do not belong here; topology only distinguishes a not-yet-built blueprint
	// from a finished structure (D4: graph answers "what is connected", ECS
	// answers "what state is this in").
	enum class FoundationState {
		Blueprint,
		Built,
	};

	// POD record, serialization-friendly (D4). `ring` is CCW integer mm and
	// kept simple/non-degenerate by ConstructionWorld's invariants. `entity` is
	// the ECS mirror handle, set by the caller via setEntity once the gameplay
	// entity is spawned; kInvalidEntity until then.
	struct Foundation {
		FoundationId	id = kInvalidFoundation;
		geometry::Ring	ring;
		std::string		material;
		FoundationState state = FoundationState::Blueprint;
		ecs::EntityID	entity = ecs::kInvalidEntity;
	};

	// Outcome of a commit or edit. Mirrors geometry's reject-don't-repair model:
	// structural rejections map to the Structural* values, boolean-op rejections
	// pass geometry::BooleanStatus through verbatim so callers get the precise
	// reason (hole, split, pinch, disjoint, consumes, no-effect).
	enum class CommitStatus {
		Ok,
		TooFewVertices,	   // ring has fewer than 3 vertices
		NotSimple,		   // ring self-intersects (geometry::isSimple failed)
		DegenerateArea,	   // zero signed area
		OverlapsExisting,  // interior overlaps an already-committed foundation
		UnknownFoundation, // edit referenced an id not in the store
		// Boolean-edit failures, forwarded from geometry::BooleanStatus:
		BooleanInvalidInput,
		BooleanDisjoint,
		BooleanPinchVertex,
		BooleanResultHasHole,
		BooleanResultSplits,
		BooleanConsumesInput,
		BooleanNoEffect,
	};

	struct CommitResult {
		CommitStatus status = CommitStatus::TooFewVertices;
		FoundationId id = kInvalidFoundation; // valid only when status == Ok

		bool ok() const { return status == CommitStatus::Ok; }
	};

	struct Aabb {
		geometry::Vec2i64 min;
		geometry::Vec2i64 max;
	};

	// --- Wall topology (D4) -------------------------------------------------
	//
	// The wall graph: vertices (junctions and endpoints), segments (the unit of
	// selection and demolition), and openings (doors/windows, modeled now so
	// T-junction splits re-attach them; the opening creation tool is Epic F).
	// Separate id spaces, 0 == invalid in each, allocated from counters parallel
	// to FoundationId. FoundationState is reused for the Blueprint/Built mirror:
	// a wall, like a foundation, only distinguishes a not-yet-built blueprint
	// from a finished structure; richer gameplay states live on the ECS entity.

	using VertexId = std::uint64_t;
	using SegmentId = std::uint64_t;
	using OpeningId = std::uint64_t;

	constexpr VertexId	kInvalidVertex = 0;
	constexpr SegmentId kInvalidSegment = 0;
	constexpr OpeningId kInvalidOpening = 0;

	// A graph node. `segments` is the adjacency list (every segment incident to
	// this vertex); its size is the vertex degree. Vertices are merged by exact
	// integer position, so two segments meeting at a point share one Vertex.
	struct Vertex {
		VertexId			   id = kInvalidVertex;
		geometry::Vec2i64	   pos;
		std::vector<SegmentId> segments; // incident segment ids, insertion order
	};

	// A wall span between two vertices. `material` and `thicknessPreset` are
	// config NAMES resolved against the construction config elsewhere (band
	// thickness, work factor, fill pattern); the topology stores the strings and
	// stays config-agnostic. `hostFoundation` is the single foundation that
	// contains the segment's full-thickness footprint (D4/design: no co-owned
	// walls); the editor picks it, the store records it. `entity` is the ECS
	// mirror handle, kInvalidEntity until the caller spawns the gameplay entity.
	struct WallSegment {
		SegmentId		id = kInvalidSegment;
		VertexId		v0 = kInvalidVertex;
		VertexId		v1 = kInvalidVertex;
		std::string		material;
		std::string		thicknessPreset;
		FoundationId	hostFoundation = kInvalidFoundation;
		FoundationState state = FoundationState::Blueprint;
		ecs::EntityID	entity = ecs::kInvalidEntity;
	};

	// A door or window attached to a segment at parameter `t` in [0,1] measured
	// from the segment's v0 toward v1. Attachment is by id + parameter, not
	// position, so it survives a T-junction split (the split re-maps t into the
	// half it falls in). Full opening creation/validation is Epic F; this record
	// plus the split re-attach are all that is built now.
	struct Opening {
		OpeningId		id = kInvalidOpening;
		SegmentId		segment = kInvalidSegment;
		float			t = 0.0F;
		std::string		type;
		std::string		material;
		FoundationState state = FoundationState::Blueprint;
		ecs::EntityID	entity = ecs::kInvalidEntity;
	};

	// Why a segment commit was rejected. Ok carries the new id. The two topology
	// rejections mirror the foundation reject-don't-repair model.
	enum class SegmentStatus {
		Ok,
		ZeroLength,	 // a == b after quantization
		Duplicate,	 // an existing segment already joins these two vertices
		XCrossing,	 // interior would properly cross an existing segment interior
		UnknownHost, // hostFoundation id is not in the store
	};

	struct SegmentCommitResult {
		SegmentStatus status = SegmentStatus::ZeroLength;
		SegmentId	  id = kInvalidSegment; // valid only when status == Ok

		bool ok() const { return status == SegmentStatus::Ok; }
	};

	class ConstructionWorld {
	  public:
		// --- Construction ---------------------------------------------------

		// Commit a new foundation from a ring already quantized to integer mm.
		// Validates structural invariants, normalizes winding to CCW, rejects on
		// any violation (including interior overlap with an existing foundation),
		// and on success allocates a stable id and stores the record.
		CommitResult commitFoundation(geometry::Ring ring, std::string material);

		// Convenience overload: ring in float world meters, quantized via
		// geometry::quantize before the same commit path. The fully-qualified
		// ::Foundation::Vec2 disambiguates the math namespace from this struct.
		CommitResult commitFoundation(const std::vector<::Foundation::Vec2>& meters, std::string material);

		// --- Editing (Add / Subtract / whole demolish) ----------------------

		// Foundation Add: union the foundation's ring with `addend`
		// (geometry::unionRings). Replaces the ring on success, re-validating the
		// structural invariants; forwards the boolean status on failure. The
		// foundation's ring is left unchanged when the edit is rejected.
		CommitStatus addToFoundation(FoundationId id, const geometry::Ring& addend);

		// Foundation Subtract: carve `subtrahend` off the foundation's ring
		// (geometry::subtractRings). Same success/failure contract as add.
		CommitStatus subtractFromFoundation(FoundationId id, const geometry::Ring& subtrahend);

		// Whole-foundation demolish (the only removal the design spec allows).
		// Returns false if the id is unknown.
		bool removeFoundation(FoundationId id);

		// --- Queries --------------------------------------------------------

		// Topmost foundation containing `point`, or kInvalidFoundation if none.
		// A point strictly inside exactly one foundation returns that one; on the
		// shared edge between two snapped-adjacent foundations a point is on a
		// boundary of both, so the tie-break is the highest id (the most recently
		// committed foundation), which is deterministic and matches "topmost".
		FoundationId foundationAt(const geometry::Vec2i64& point) const;

		const Foundation* get(FoundationId id) const;

		const std::vector<Foundation>& foundations() const { return foundations_; }

		// Axis-aligned bounds of a foundation's ring in mm. Returns a zero-extent
		// box at the origin for an unknown id.
		Aabb footprintAabb(FoundationId id) const;

		// Foundation area in square meters (geometry::signedAreaDoubled / 2,
		// scaled out of mm^2). Zero for an unknown id.
		float areaSquareMeters(FoundationId id) const;

		// --- Walls: commit ---------------------------------------------------

		// Commit a wall segment between integer-mm endpoints `a` and `b` on host
		// foundation `host`. Enforces the wall topology invariants and, on
		// success, allocates a stable SegmentId, merges endpoints into vertices
		// (exact-position dedup), and wires the adjacency. Returns the new id.
		//
		// Topology handling, in order:
		//   - ZeroLength if a == b.
		//   - UnknownHost if `host` is non-zero and not in the store (zero is the
		//     unhosted/freestanding sentinel and is accepted; walls today always
		//     pass a real host, freestanding walls are a future tool).
		//   - XCrossing if the new segment's interior would properly cross any
		//     existing segment's interior. T-junctions (an endpoint on a segment's
		//     interior) are NOT crossings; they split (below). A duplicate of an
		//     existing segment (same vertex pair) is rejected as Duplicate.
		//   - T-junction split: if endpoint a or b lands on the interior of an
		//     existing segment S, S is split at that point into two new segments;
		//     if the new segment passes through an existing vertex, the new
		//     segment is itself split at that vertex. Both directions are handled,
		//     so the committed graph never has a vertex sitting on a segment's
		//     interior. See commitSegment in the .cpp for the split mechanics and
		//     opening re-attach.
		SegmentCommitResult commitSegment(
			const geometry::Vec2i64& a,
			const geometry::Vec2i64& b,
			std::string				 material,
			std::string				 thicknessPreset,
			FoundationId			 host
		);

		// Remove a segment, drop it from its endpoints' adjacency, and delete any
		// vertex left with no incident segments. Openings on the segment are
		// removed with it. Returns false for an unknown id. (Per-segment demolish
		// is the design's wall removal unit; the gameplay demolish-with-refund
		// flow lives in ConstructionSystem and calls through here.)
		bool removeSegment(SegmentId id);

		// --- Walls: queries --------------------------------------------------

		// Nearest segment whose centerline passes within `pickRadiusMm` of
		// `point`, or kInvalidSegment if none. The radius is the pick tolerance in
		// mm; callers pass max(half-thickness, ui-pick-slop) since the topology
		// store has no thickness without config. Ties (equal distance) break to
		// the highest id (most recently committed), deterministic and matching
		// foundationAt. Distance is exact integer point-to-centerline.
		SegmentId segmentAt(const geometry::Vec2i64& point, std::int64_t pickRadiusMm) const;

		// Vertex at exactly `point` (exact integer match), or kInvalidVertex.
		VertexId vertexAt(const geometry::Vec2i64& point) const;

		const WallSegment* getSegment(SegmentId id) const;
		const Vertex*	   getVertex(VertexId id) const;
		const Opening*	   getOpening(OpeningId id) const;

		const std::vector<WallSegment>& segments() const { return segments_; }
		const std::vector<Vertex>&		vertices() const { return vertices_; }
		const std::vector<Opening>&		openings() const { return openings_; }

		// Incident segment ids of a vertex (its adjacency list), empty for an
		// unknown id. Returned by value to keep a stable snapshot independent of
		// later mutations.
		std::vector<SegmentId> segmentsOfVertex(VertexId id) const;

		// --- Walls: lifecycle / ECS wiring -----------------------------------

		// Mirror mutators for the ECS-backed gameplay state, parallel to the
		// foundation ones. Both bump the version. Return false on unknown id.
		bool setSegmentState(SegmentId id, FoundationState state);
		bool setSegmentEntity(SegmentId id, ecs::EntityID entity);

		// --- Walls: openings (Epic F creation lives elsewhere) ---------------

		// Attach an opening to a segment at parameter t in [0,1]; t is clamped.
		// Minimal creation hook so the split re-attach path is testable now; the
		// OpeningTool's placement validation (margins, overlap) is Epic F.
		// Returns kInvalidOpening for an unknown segment.
		OpeningId addOpening(SegmentId segment, float t, std::string type, std::string material);

		// --- Foundation lifecycle / ECS wiring -------------------------------

		// Mutators for later pieces (ConstructionSystem, the ECS mirror). Both
		// bump the topology version. Return false on unknown id.
		bool setState(FoundationId id, FoundationState state);
		bool setEntity(FoundationId id, ecs::EntityID entity);

		// --- Versioning -----------------------------------------------------

		// Monotonic counter bumped on every topology change (commit, edit,
		// remove, state/entity mutation). Consumers (rendering, nav) cache against
		// it and rebuild when it moves. Pure queries never bump it.
		std::uint64_t version() const { return version_; }

	  private:
		Foundation*		  find(FoundationId id);
		const Foundation* find(FoundationId id) const;

		// Structural invariant gate shared by commit and the edit paths.
		// `ignoreId` excludes a foundation from the overlap check (the one being
		// edited in place). On success `ring` is left CCW-normalized.
		CommitStatus validateStructure(geometry::Ring& ring, FoundationId ignoreId) const;

		// Wall topology helpers.
		WallSegment* findSegment(SegmentId id);
		Vertex*		 findVertex(VertexId id);

		// Return the id of the vertex at exactly `pos`, creating one if none
		// exists. Merge is by exact integer equality (Vec2i64::operator==).
		VertexId findOrCreateVertex(const geometry::Vec2i64& pos);

		// Append/remove a segment id in a vertex's adjacency list.
		void addAdjacency(VertexId vertex, SegmentId segment);
		void removeAdjacency(VertexId vertex, SegmentId segment);

		// Delete a vertex if its adjacency list is empty (no remaining incident
		// segments). No-op otherwise. Bumps nothing; the caller owns versioning.
		void pruneVertexIfOrphan(VertexId vertex);

		// Split existing segment `host` at integer-mm point `at` (which must lie
		// on host's interior). Replaces `host` with two new segments carrying its
		// material/thickness/host/state, re-attaches host's openings by parameter
		// range, and clears host's ECS entity (the caller/ConstructionSystem
		// despawns or re-maps it; see the .cpp). The new vertex at `at` is
		// returned. Does NOT bump the version; the enclosing commit owns that.
		VertexId splitSegmentAt(SegmentId host, const geometry::Vec2i64& at);

		std::vector<Foundation> foundations_; // stable insertion order
		FoundationId			nextFoundationId_ = 1;
		std::uint64_t			version_ = 0;

		std::vector<Vertex>		 vertices_; // stable insertion order
		std::vector<WallSegment> segments_;
		std::vector<Opening>	 openings_;
		VertexId				 nextVertexId_ = 1; // 0 reserved as invalid
		SegmentId				 nextSegmentId_ = 1;
		OpeningId				 nextOpeningId_ = 1;
	};

} // namespace engine::construction
