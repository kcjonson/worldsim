#pragma once

// ConstructionValidator
//
// Draw-time UX validation for the foundation and wall tools (building-
// construction D11). Pure logic, engine-lib so it is unit-testable without the
// app: given the in-progress geometry (float world meters) and a candidate, it
// quantizes to integer mm and runs the geometry constraint primitives.
// Reject-don't-repair (D4): a violation reports a reason and the offending
// vertex/edge; nothing is fixed.
//
// Foundation entry points, mirroring the two moments the tool checks:
//   - validatePoint: the candidate ring (existing points + candidate) used for
//     the live rubber-band colorizing and per-vertex hard rejects.
//   - validateRing:  the would-be-closed polygon, used to gate the commit.
//
// Wall entry points (Epic D phase 2). Walls are an OPEN chain, hosted by one
// foundation, with full-thickness footprints that must stay inside the host and
// clear of other walls. ConstructionWorld owns only the hard topology invariants
// (no zero-length, no X-crossing, T-junction pre-split); the SOFT wall
// constraints (min segment length, min junction angle, host containment, overlap
// and parallel clearance) are this validator's job, per the world's "invariant
// boundary" comment. Two entry points mirror the foundation pair:
//   - validateWallPoint: the in-progress chain with `candidate` appended, for the
//     live rubber-band colorizing as each point is dragged.
//   - validateWallSegment: a single a->b segment, for the per-segment commit gate
//     and for the X-crossing / overlap / clearance feedback the tool colorizes.
// Thickness is resolved from a supplied ThicknessPreset (half-thickness mm) so
// the validator measures the real footprint, not just the centerline.
//
// The validator does NOT own constraint values; it reads them from a supplied
// engine::assets::ConstraintConfig so tuning lives in config (D10).

#include "ConstructionWorld.h"

#include <core/Vec2i64.h>
#include <math/Types.h>

#include <cstddef>
#include <string>
#include <vector>

namespace engine::assets {
	struct ConstraintConfig;
	struct ThicknessPreset;
} // namespace engine::assets

namespace ecs {
	struct StructureBlueprint;
}

namespace engine::construction {

	// Which constraint a candidate violated. Ordered roughly by the sequence the
	// validator tests them; the first failure is the one reported. Ok last so a
	// truthiness check (`code != Ok`) reads naturally.
	enum class ValidationCode {
		Ok,
		TooFewPoints,		   // fewer than 3 points (commit gate only)
		TooManyPoints,		   // candidate would exceed maxPoints
		VerticesTooClose,	   // adjacent vertices closer than minVertexSpacing
		AngleTooSharp,		   // an interior corner below minCornerAngle
		SelfIntersects,		   // ring is not simple
		EdgeClearanceTooSmall, // non-adjacent edges closer than segmentClearance
		AreaTooSmall,		   // closed area below minArea
		AreaTooLarge,		   // closed area above maxArea
		OverlapsExisting,	   // interior overlaps a committed foundation
		// Wall codes (Epic D phase 2). AngleTooSharp (junction angle) and
		// SelfIntersects (open chain crosses itself) are reused for walls where
		// they already fit; these are the wall-only reasons.
		SegmentTooShort,			  // wall segment shorter than minSegmentLength
		NotContainedInHostFoundation, // full-thickness footprint pokes outside the host ring
		WallsOverlap,				  // footprint overlaps another wall's footprint (not a junction)
		ParallelClearanceTooSmall,	  // faces run closer than minParallelClearance without joining
		XCrossing,					  // centerline would properly cross an existing wall centerline
		// Opening codes (Epic F). An opening is placed on a BUILT wall segment at a
		// centerline parameter t; these are the opening-only reasons.
		OpeningSegmentInvalid,	// target segment is missing or not a wall segment in the world
		OpeningTypeInvalid,		// opening type name is not a known opening type
		OpeningMaterialInvalid, // opening material name is not a known material
		OpeningParamOutOfRange, // t outside [0,1]
		OpeningMarginTooSmall,	// opening edge closer to a segment end than openingMargin
		OpeningWallTooShort,	// segment too short to host width + 2*openingMargin
		OpeningOverlap,			// overlaps another opening already on the same segment
		// Foundation clearance: another foundation is closer than pathing clearance
		// without touching it (snapped edge-to-edge is the only legal small gap).
		TooCloseToFoundation,
		// Foundation Add / Subtract codes (Epic G3). Shape, area, overlap, and
		// clearance failures of the resulting outline reuse the codes above.
		FoundationNotFound,		// edit target is not in the world
		EditMaterialsDelivered, // blueprint already has material delivered or work done
		BuiltNeverShrinks,		// subtract on a built foundation
		ExtensionPending,		// built target already has an unmerged extension
		ExtensionNotEditable,	// the target is itself a pending extension
		EditDisjoint,			// the drawn region doesn't touch the target
		EditPinch,				// the shapes meet at a single point
		EditEnclosesHole,		// add: the result would enclose a hole
		EditCutsHole,			// subtract: the region sits strictly inside, cutting a hole
		EditSplits,				// subtract: the result would be two pieces
		EditConsumes,			// subtract: nothing of the foundation would remain
		EditNothingToAdd,		// add: the region lies inside the foundation
		EditNothingToRemove,	// subtract: the region misses the foundation's interior
		ExtensionSplits,		// built add: the added region would be two separate pieces
		ExtensionWrapsAround,	// built add: the added region would ring the foundation
		EditUnderWall,			// subtract: a hosted wall's footprint would leave the remainder
		BeingDemolished,		// the target is marked for demolition
	};

	// Result of a check. `code` drives both the red colorizing and the reason
	// text. `vertexIndex`/`otherIndex` index into the meters polygon passed in
	// (the candidate point is the last index) so the tool can highlight the
	// offending vertex or edge. `measuredValue` is the measured quantity in the
	// constraint's natural units (degrees or meters), for richer messages.
	struct ValidationResult {
		ValidationCode code = ValidationCode::Ok;
		std::size_t	   vertexIndex = 0;
		std::size_t	   otherIndex = 0;
		double		   measuredValue = 0.0;

		[[nodiscard]] bool ok() const { return code == ValidationCode::Ok; }
	};

	enum class FoundationEditMode {
		Add,
		Subtract,
	};

	// Outcome of validateFoundationEdit. `outline` is the target's ring after the
	// edit (merged or remainder, CCW integer mm), the shape the validation judged.
	// `extension` is set only for an Add onto a Built foundation: the added region
	// that becomes the extension blueprint (ConstructionWorld::commitExtension).
	// `buildsOn` is set for every Add: the ground the caller checks for
	// buildability. For a built target that is the extension alone, the only new
	// ground (the target was judged when it was placed, and its walls and floor
	// are no business of this edit). For a blueprint target it is the whole drawn
	// polygon: the added part of an in-place Add may be several pieces, which no
	// single ring can hold, and the rest lies on the blueprint's own footprint.
	// All three are empty when the edit is rejected.
	struct FoundationEditResult {
		ValidationResult validation;
		geometry::Ring	 outline;
		geometry::Ring	 extension;
		geometry::Ring	 buildsOn;

		[[nodiscard]] bool ok() const { return validation.ok(); }
	};

	// Human-readable, short reason for a code (e.g. "corner too tight"). Empty
	// for Ok. Matches the terse phrasing the design spec shows near the cursor.
	[[nodiscard]] std::string validationReason(ValidationCode code);

	class ConstructionValidator {
	  public:
		ConstructionValidator(const engine::assets::ConstraintConfig& constraints, const ConstructionWorld& world)
			: constraints_(&constraints),
			  world_(&world) {}

		// Validate the open polygon formed by `points` with `candidate` appended.
		// Used live as the cursor moves: checks the things that must hold for the
		// NEXT vertex (spacing to the previous point, the new corner's angle, no
		// self-intersection of the open chain, edge clearance, point cap). Area is
		// not checked here (the shape isn't closed yet). `points` may be empty (the
		// first click is always allowed) or hold the already-committed vertices.
		[[nodiscard]] ValidationResult validatePoint(const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 candidate) const;

		// Validate the closed polygon `ring` (>= 3 points, closing edge implicit)
		// against every constraint including area and overlap. Used to gate commit.
		[[nodiscard]] ValidationResult validateRing(const std::vector<::Foundation::Vec2>& ring) const;

		// --- Foundation Add / Subtract (Epic G3) ------------------------------

		// Live per-point check while drawing an Add/Subtract region. The drawn
		// polygon is a cutter, not a foundation, so only what the boolean needs
		// applies to it: the point cap, vertex spacing, and a simple open chain.
		// Corner angle, edge clearance, area, overlap, and clearance to other
		// foundations are judged on the resulting outline by validateFoundationEdit.
		[[nodiscard]] ValidationResult validateEditPoint(const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 candidate) const;

		// Validate editing foundation `target` with the closed drawn polygon `drawn`
		// (world meters). `targetBlueprint` is the target's ECS mirror (null when it
		// has none): topology alone can't see deliveries, work, or a demolish order.
		// Rules (design "Editing After Build: Add / Subtract"):
		//   - Marked for demolition (either state): both rejected. An extension made
		//     now would hold the teardown open without being part of it.
		//   - Blueprint + editable (StructureBlueprint::shapeEditable): Add unions in
		//     place, Subtract carves in place.
		//   - Blueprint + materials delivered or worked: both rejected.
		//   - Built: Subtract rejected (built foundations never shrink); Add yields an
		//     extension (drawn minus target) whose merge is the outline, rejected
		//     while another extension is pending.
		//   - A pending extension is not itself editable.
		// The resulting outline must pass every closed-foundation constraint (shape,
		// point cap, area, overlap and clearance against every other foundation), and
		// a Subtract must keep each hosted wall's full-thickness footprint inside the
		// remainder. First failure wins; its vertexIndex indexes the outline.
		[[nodiscard]] FoundationEditResult validateFoundationEdit(
			FoundationId						   target,
			const std::vector<::Foundation::Vec2>& drawn,
			FoundationEditMode					   mode,
			const ecs::StructureBlueprint*		   targetBlueprint
		) const;

		// --- Walls ----------------------------------------------------------

		// Validate the in-progress open chain `points` with `candidate` appended
		// (world meters). Live as the cursor moves; checks the things that must
		// hold for the NEXT wall vertex: the new segment's length (>= min segment
		// length), the junction angle at the previous vertex (>= min junction
		// angle), the open chain stays simple (no self-cross), and the new
		// segment's full-thickness footprint stays inside the host foundation.
		// `points` may be empty (first click always allowed) or hold the chain so
		// far. `thickness` supplies the half-thickness mm for the footprint (the
		// WallTool resolves it from the active material+preset via
		// ConstructionRegistry::getThicknessPreset and passes it in; passing the
		// preset keeps the validator pure and testable without a loaded registry).
		// `host` is the host foundation id (kInvalidFoundation skips the containment
		// check, for the freestanding-wall future). `vertexIndex` in a failure
		// indexes into `points`+candidate (candidate is the last index).
		[[nodiscard]] ValidationResult validateWallPoint(
			const std::vector<::Foundation::Vec2>& points,
			::Foundation::Vec2					   candidate,
			const engine::assets::ThicknessPreset& thickness,
			FoundationId						   host
		) const;

		// Validate a single committed-segment a->b (world meters): length, host
		// containment, X-crossing against existing wall centerlines, and overlap /
		// parallel-clearance against existing wall footprints. Two walls that join
		// at a shared endpoint legitimately touch and are exempt from the
		// overlap/clearance check (they meet at a junction). Used as the per-segment
		// commit gate and the colorizing feedback source.
		[[nodiscard]] ValidationResult validateWallSegment(
			::Foundation::Vec2					   a,
			::Foundation::Vec2					   b,
			const engine::assets::ThicknessPreset& thickness,
			FoundationId						   host
		) const;

		// --- Openings -------------------------------------------------------

		// Validate placing an opening of `type` (material `material`) on wall
		// `segment` at centerline parameter `t` in [0,1] (measured v0->v1). The
		// per-opening placement gate for the OpeningTool (Epic F): the segment must
		// exist and be a wall, the type and material must be known, t must be in
		// range, the opening's near edge must clear `openingMargin` from each
		// segment end, the segment must be long enough to host width + 2*margin, and
		// the opening's [t-half/L, t+half/L] interval must not overlap (within the
		// inter-opening margin) any opening already on the SAME segment. Width is
		// resolved from the registry's OpeningTypeDef (passing the type name keeps
		// the validator reading config the same way validateWallSegment does for
		// existing-wall thickness). First failure is reported with the measured
		// value; the result's vertexIndex/otherIndex are unused for openings.
		[[nodiscard]] ValidationResult
		validateOpening(SegmentId segment, float t, const std::string& type, const std::string& material) const;

	  private:
		// Shared ring-shape checks (spacing, angle, simplicity, clearance) over a
		// quantized ring. `closed` selects whether the implicit closing edge and
		// the wrap-around corner/spacing are included.
		[[nodiscard]] ValidationResult checkShape(const std::vector<geometry::Vec2i64>& ring, bool closed) const;
		[[nodiscard]] ValidationResult checkSpacing(const std::vector<geometry::Vec2i64>& ring, bool closed) const;
		[[nodiscard]] ValidationResult checkAngles(const std::vector<geometry::Vec2i64>& ring, bool closed) const;
		[[nodiscard]] ValidationResult checkSimple(const std::vector<geometry::Vec2i64>& ring, bool closed) const;
		[[nodiscard]] ValidationResult checkEdgeClearance(const std::vector<geometry::Vec2i64>& ring, bool closed) const;

		// Every constraint a closed foundation outline must meet: point cap, shape,
		// area, and overlap + clearance against every committed foundation except
		// `ignore` (the foundation being edited). Shared by validateRing and
		// validateFoundationEdit so a drawn foundation and an edited one obey one rule.
		[[nodiscard]] ValidationResult checkOutline(geometry::Ring ring, FoundationId ignore) const;

		// Is the full-thickness band of centerline a->b (integer mm) entirely inside
		// host foundation `host`? True when host is kInvalidFoundation (no host to
		// contain it). False if the host id is unknown.
		[[nodiscard]] bool bandContainedInFoundation(
			const geometry::Vec2i64& a,
			const geometry::Vec2i64& b,
			std::int64_t			 halfThickMm,
			FoundationId			 host
		) const;

		const engine::assets::ConstraintConfig* constraints_;
		const ConstructionWorld*				world_;
	};

} // namespace engine::construction
