#pragma once

#include "../core/Vec2i64.h"
#include "../polygon/Polygon.h"

#include <cstdint>

// Polygon booleans for foundation shape editing (building-construction D2/G).
// Inputs are two SIMPLE rings; the only legal output is a SINGLE simple ring
// with no holes. Anything else is rejected with a reason and never repaired
// (D4 reject-don't-repair). Implemented as arrangement-insert + face
// classification on the planar-graph core (Arrangement + HalfEdge): insert both
// rings' edges, extract faces, classify each face's interior against the input
// rings, select the kept faces, then walk the half-edge boundary into result
// rings. A CW result ring means an enclosed hole; two CCW rings mean a split or
// disjoint pieces. Either is rejected for the foundation use case.
//
// `unionRings` is foundation Add (merge a drawn region into a foundation);
// `subtractRings` is foundation Subtract (carve a region off a blueprint).
// Both inputs are editor-validated simple rings in integer millimeters, and the
// shared-edge case (snapped-adjacent foundations) is the normal Add case, not
// the exception, so it is handled exactly via the arrangement's coincident-edge
// dedup rather than as a special path.

namespace geometry {

	enum class BooleanStatus {
		Ok,
		InvalidInput,	   // a ring is not simple, or has fewer than 3 vertices
		Disjoint,		   // no shared area or edge: not a single connected result
		PinchVertex,	   // inputs meet only at a point: the result self-touches
		ResultHasHole,	   // a kept face encloses a void: an interior hole would form
		ResultSplits,	   // the result is two or more separate pieces
		ConsumesInput,	   // subtract: b covers a entirely, nothing remains
		NoEffect,		   // subtract: b removes nothing from a (disjoint interiors)
	};

	struct BooleanResult {
		BooleanStatus status = BooleanStatus::InvalidInput;

		// The single simple CCW result ring. Populated only when status == Ok.
		Ring ring;

		bool ok() const { return status == BooleanStatus::Ok; }
	};

	// Foundation Add: union of two simple rings. Success only when the union is a
	// single simple hole-free ring (the inputs overlap in area or share a length
	// of edge). Failure modes: Disjoint (no shared area or edge), PinchVertex
	// (touch at a single point only), ResultHasHole (the union encloses a void),
	// InvalidInput. The result is simplified (1 mm) and re-validated before
	// return; identical rings return that ring, and b fully inside a returns a.
	BooleanResult unionRings(const Ring& a, const Ring& b);

	// Foundation Subtract: a minus b. Success only when the remainder is a single
	// simple hole-free ring. Failure modes: ResultHasHole (b strictly interior to
	// a, would carve an enclosed hole), ResultSplits (b cuts a into two pieces),
	// ConsumesInput (b covers a), NoEffect (interiors disjoint, nothing removed),
	// InvalidInput. The result is simplified (1 mm) and re-validated before
	// return.
	BooleanResult subtractRings(const Ring& a, const Ring& b);

	// Exact interior-intersection predicate: do the open interiors of a and b
	// overlap? Sharing only an edge or a vertex is NOT an interior overlap (that
	// is the legal snapped-adjacent foundation case), so this returns false there;
	// it returns true only when the rings share positive area. Used to reject two
	// separate foundations placed on top of each other while allowing edge-to-edge
	// snapping. Returns false on invalid (non-simple) input.
	bool ringsInteriorOverlap(const Ring& a, const Ring& b);

	constexpr std::int64_t kBooleanSimplifyEpsMm = 1;

} // namespace geometry
