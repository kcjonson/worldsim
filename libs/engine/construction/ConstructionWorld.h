#pragma once

#include "../ecs/EntityID.h"

#include <boolean/RingBoolean.h>
#include <core/Vec2i64.h>
#include <polygon/Polygon.h>

#include <math/Types.h>

#include <cstdint>
#include <string>
#include <vector>

// Topology store for construction foundations: the source of truth for
// foundation geometry, independent of ECS and rendering (building-construction
// D4). Committed geometry is integer millimeters (D1); the editor's float-meter
// previews quantize on commit. This is Epic C scope (foundations only); the
// vertex/segment/opening/room tables of D4 slot in later but are not built here.
//
// Invariant boundary. ConstructionWorld enforces only the STRUCTURAL invariants
// downstream consumers (face extraction, nav rasterization, band generation,
// rendering) depend on: each foundation ring is simple, counter-clockwise, has
// non-degenerate area, and no two foundations overlap in their interiors. The
// editor-facing constraint primitives (min corner angle, vertex spacing, edge
// clearance, min/max area) are draw-time UX checks the ConstructionValidator
// owns (D11); they are deliberately NOT run here, so a programmatic or
// out-of-editor caller can still commit any non-self-intersecting shape. Per D4
// this store rejects invalid input and never repairs it.

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

		// --- Lifecycle / ECS wiring -----------------------------------------

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

		std::vector<Foundation> foundations_; // stable insertion order
		FoundationId			nextId_ = 1;  // 0 reserved as invalid
		std::uint64_t			version_ = 0;
	};

} // namespace engine::construction
