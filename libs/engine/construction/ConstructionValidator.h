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
		SegmentTooShort,			// wall segment shorter than minSegmentLength
		NotContainedInHostFoundation, // full-thickness footprint pokes outside the host ring
		WallsOverlap,				// footprint overlaps another wall's footprint (not a junction)
		ParallelClearanceTooSmall,	// faces run closer than minParallelClearance without joining
		XCrossing,					// centerline would properly cross an existing wall centerline
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
			const std::vector<::Foundation::Vec2>&	points,
			::Foundation::Vec2						candidate,
			const engine::assets::ThicknessPreset&	thickness,
			FoundationId							host
		) const;

		// Validate a single committed-segment a->b (world meters): length, host
		// containment, X-crossing against existing wall centerlines, and overlap /
		// parallel-clearance against existing wall footprints. Two walls that join
		// at a shared endpoint legitimately touch and are exempt from the
		// overlap/clearance check (they meet at a junction). Used as the per-segment
		// commit gate and the colorizing feedback source.
		[[nodiscard]] ValidationResult validateWallSegment(
			::Foundation::Vec2						a,
			::Foundation::Vec2						b,
			const engine::assets::ThicknessPreset&	thickness,
			FoundationId							host
		) const;

	  private:
		// Shared ring-shape checks (spacing, angle, simplicity, clearance) over a
		// quantized ring. `closed` selects whether the implicit closing edge and
		// the wrap-around corner/spacing are included.
		[[nodiscard]] ValidationResult checkShape(const std::vector<geometry::Vec2i64>& ring, bool closed) const;

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
