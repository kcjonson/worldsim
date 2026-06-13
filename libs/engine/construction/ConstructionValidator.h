#pragma once

// ConstructionValidator
//
// Draw-time UX validation for the foundation tool (building-construction D11).
// Pure logic, engine-lib so it is unit-testable without the app: given the
// in-progress polygon (float world meters) and a candidate next point, it
// quantizes to integer mm and runs the geometry constraint primitives plus an
// overlap check against committed foundations. Reject-don't-repair (D4): a
// violation reports a reason and the offending vertex/edge; nothing is fixed.
//
// Two entry points, mirroring the two moments the tool checks:
//   - validatePoint: the candidate ring (existing points + candidate) used for
//     the live rubber-band colorizing and per-vertex hard rejects.
//   - validateRing:  the would-be-closed polygon, used to gate the commit.
//
// The validator does NOT own constraint values; it reads them from a supplied
// engine::assets::ConstraintConfig so tuning lives in config (D10).

#include <core/Vec2i64.h>
#include <math/Types.h>

#include <cstddef>
#include <string>
#include <vector>

namespace engine::assets {
	struct ConstraintConfig;
}

namespace engine::construction {

	class ConstructionWorld;

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

	  private:
		// Shared ring-shape checks (spacing, angle, simplicity, clearance) over a
		// quantized ring. `closed` selects whether the implicit closing edge and
		// the wrap-around corner/spacing are included.
		[[nodiscard]] ValidationResult checkShape(const std::vector<geometry::Vec2i64>& ring, bool closed) const;

		const engine::assets::ConstraintConfig* constraints_;
		const ConstructionWorld*				world_;
	};

} // namespace engine::construction
