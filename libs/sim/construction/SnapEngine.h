#pragma once

// SnapEngine
//
// Cursor snapping for the foundation drawing tool (building-construction D11).
// Pure logic, engine-lib so the snap math is unit-testable without the app.
// Given the raw cursor in world meters, the in-progress polygon, and the
// committed ConstructionWorld, it returns a snapped point plus a kind tag the
// tool uses for visual feedback.
//
// Snap priority (most specific first, matching the design's "endpoints >
// vertices > points-on-segments > edges" stack, narrowed to the targets the
// foundation tool needs): origin-close > existing vertex > existing edge >
// world-axis guide > angle snap relative to the previous segment > raw.
//
// The guide and angle helpers follow the player's DrawingAids and are suppressed
// when `freeform` is set (the tool maps this to holding Alt, per the design). Vertex
// and edge snapping to existing geometry are always on; they are the zero-gap
// adjacency the clearance rule depends on, not a stylistic nicety.
//
// Snap radii and the angle increment come from assets::SnappingConfig
// (D10); the engine does not hardcode them.
//
// Walls (Epic D phase 2). Walls draw as a polyline chain, not a closed polygon,
// so they get their own entry point snapWall() rather than a mode flag on snap():
// the two tools differ in more than snapping (inset foundation hits, an extra pair
// of snap targets, a hit-target id the WallTool reads), so keeping them separate
// keeps the foundation path provably unchanged. The wall snap priority is the
// design's full stack, led by the same origin-close as snap() once the chain has
// kMinClosingWallChainPoints points:
//   origin-close > wall endpoint (existing wall vertex) > foundation vertex >
//   point along an existing wall segment (T-junction anywhere) >
//   foundation edge > world-axis guide > angle snap off the previous chain point > raw.
// A wall endpoint and a foundation vertex are both "vertex" targets but the wall
// endpoint wins so a chain continues/joins an existing wall in preference to
// merely landing on a foundation corner. The snapped point of an endpoint or
// segment hit is a dequantized integer-mm position, so it round-trips exactly
// back through geometry::quantize when the WallTool commits it (determinism: the
// committed vertex is exact, and segmentAt's tie-break is the highest id).

#include "ConstructionWorld.h"
#include "DrawingAids.h"

#include <core/Vec2i64.h>
#include <math/Types.h>

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace assets {
	struct SnappingConfig;
}

namespace construction {

	enum class SnapKind {
		None,		   // raw cursor, no snap applied
		Angle,		   // snapped onto an angle increment off the previous segment
		Vertex,		   // snapped onto an existing foundation vertex
		Edge,		   // snapped onto the nearest point of an existing foundation edge
		Origin,		   // within originClose radius of the first point: closes the shape
		WallEndpoint,  // snapped onto an existing wall vertex (endpoint/junction)
		WallSegment,   // snapped onto the nearest point along an existing wall segment (a T-junction)
		GuideLine,	   // locked onto one guide line
		GuideCrossing, // snapped to the crossing of two guide lines; point is the crossing
	};

	/// Where a guide's direction comes from.
	enum class GuideSource {
		DrawnShape,		 // the shape being drawn
		NearbyStructure, // an existing building or wall near the cursor
		WorldAxes,		 // the world's x and y axes
	};

	/// How a guide runs relative to its source direction. AngleStep is the angle
	/// snap's ray from the previous point, at a step off its reference: a ray to the
	/// snapped point, not a line through a node.
	enum class GuideRelation {
		Parallel,
		Perpendicular,
		AngleStep,
	};

	/// One guide the preview draws, from its node to the snapped point.
	struct SnapGuide {
		::Foundation::Vec2 from{0.0F, 0.0F}; // the guide's node; a right-angle marker sits here
		::Foundation::Vec2 to{0.0F, 0.0F};	 // the snapped point
		GuideSource		   source = GuideSource::DrawnShape;
		GuideRelation	   relation = GuideRelation::Parallel;
		bool			   rightAngle = false;
		::Foundation::Vec2 rightAngleEdgeDirection{0.0F, 0.0F}; // unit, from `from` along the perpendicular edge
		// Measured from the guide's own node; it is the segment's length only when throughPreviousPoint.
		float distanceFromNodeMeters = 0.0F; // |to - from|
		// True when the node is within 1 mm of the last placed point: with one point placed a guide
		// through it is true, with three or more a guide through the FIRST point is false.
		bool throughPreviousPoint = false;
	};

	inline constexpr std::size_t kMaxSnapGuides = 2;

	struct SnapResult {
		::Foundation::Vec2 point{0.0F, 0.0F};
		SnapKind		   kind = SnapKind::None;

		// The guides behind the snap: one for a GuideLine or an Angle snap, two for a
		// GuideCrossing, none otherwise. A guide whose cursor sits exactly on its node
		// has from == to and draws nothing.
		std::array<SnapGuide, kMaxSnapGuides> guides{};
		std::size_t							  guideCount = 0;

		// Wall hit target, read by the WallTool to commit into ConstructionWorld:
		//   kind == WallEndpoint -> hitVertex is the existing vertex the chain
		//     joins (the tool passes its exact pos to commitSegment so endpoints
		//     merge by exact integer equality, continuing/joining the wall graph).
		//   kind == WallSegment  -> hitSegment is the segment the snapped point
		//     lands on; the point is on that segment's interior, so commitSegment
		//     will split it into a T-junction. The tool detects "this point makes a
		//     T-junction" by hitSegment != kInvalidSegment.
		// Both are the invalid sentinel for every other (and every foundation) kind.
		SegmentId hitSegment = kInvalidSegment;
		VertexId  hitVertex = kInvalidVertex;

		// True when the snap closes the polygon onto its origin (kind == Origin):
		// the tool reads this to know a click commits.
		[[nodiscard]] bool closesShape() const { return kind == SnapKind::Origin; }
	};

	// Result of an opening snap: the BUILT wall segment the cursor snapped to, the
	// centerline parameter t (clamped so an opening of the queried width honors the
	// end margins), and the snapped world point (centerline at t) for the ghost
	// preview. `valid` is false when no built wall is within the snap radius or the
	// nearest one is too short to fit the opening; the other fields are unspecified
	// then. The OpeningTool reads `segment` + `t` to commit and `point` to draw.
	struct OpeningSnap {
		SegmentId		   segment = kInvalidSegment;
		float			   t = 0.0F;
		::Foundation::Vec2 point{0.0F, 0.0F};
		bool			   valid = false;
	};

	// Outer-face-flush alignment (design "Alignment": walls snapped to a foundation
	// edge align outer-face-flush, so the full thickness sits ON the foundation and
	// the foundation never lips outside the building). Returns the centerline corner
	// for `ring`'s vertex `vertexIndex`, offset INWARD to the mitered meeting point of
	// the two incident edges' inset lines. Two perimeter walls that share this corner
	// resolve to the identical point, so they join at one vertex on commit. World
	// meters; the interior side is found exactly via point-in-polygon so the ring's
	// winding doesn't matter. A small fixed safety bias (kept inside the offset, applied
	// to every edge) absorbs the millimeter quantization that would otherwise push a
	// diagonal edge's outer band corner just outside the ring; on axis-aligned edges,
	// which have no rounding, it just seats the corner a flat ~2 mm inside. halfThicknessMm
	// <= 0 or a degenerate ring returns the corner unchanged. Shared by the WallTool's
	// foundation-vertex snap and the edge-fill path.
	[[nodiscard]] ::Foundation::Vec2
	outerFaceFlushCorner(const geometry::Ring& ring, std::size_t vertexIndex, std::int64_t halfThicknessMm);

	/// An existing foundation corner near the cursor: the raw ring vertex, which
	/// foundation and which vertex of its ring, and how far it is from the cursor.
	struct FoundationCornerHit {
		::Foundation::Vec2 point{0.0F, 0.0F};
		FoundationId	   foundation = kInvalidFoundation;
		std::size_t		   vertexIndex = 0;
		float			   distanceMeters = 0.0F;
	};

	/// An existing foundation edge near the cursor: the foot of the cursor on edge
	/// `edgeIndex` (ring[edgeIndex] to the next vertex), clamped to the edge.
	struct FoundationEdgeHit {
		::Foundation::Vec2 point{0.0F, 0.0F};
		FoundationId	   foundation = kInvalidFoundation;
		std::size_t		   edgeIndex = 0;
		float			   distanceMeters = 0.0F;
	};

	/// The nearest foundation ring vertex within `radiusMeters` of `cursor`, blueprints
	/// included. Ties go to the later foundation in foundations() order, then the
	/// later vertex. The snap engine's corner rule uses this.
	[[nodiscard]] std::optional<FoundationCornerHit>
	nearestFoundationCorner(const ConstructionWorld& world, ::Foundation::Vec2 cursor, float radiusMeters);

	/// The nearest point on any foundation ring edge within `radiusMeters` of `cursor`,
	/// with the same tie rule as nearestFoundationCorner. The snap engine's edge rule
	/// uses this.
	[[nodiscard]] std::optional<FoundationEdgeHit>
	nearestFoundationEdge(const ConstructionWorld& world, ::Foundation::Vec2 cursor, float radiusMeters);

	/// Everything a snap reads besides the placed points and the cursor.
	struct SnapInput {
		DrawingAids aids;
		bool		freeform = false; // Alt: drop the guides and the angle step
		// Overrides the configured origin-close radius when non-negative; the tools pass
		// a zoom-stable radius so the close stays reachable when zoomed out.
		float originCloseRadiusMeters = -1.0F;
		// The wall tool's half-thickness, by which foundation corner and edge hits are
		// inset for outer-face-flush alignment. Unused by snap().
		std::int64_t wallHalfThicknessMm = 0;
	};

	class SnapEngine {
	  public:
		SnapEngine(const assets::SnappingConfig& snapping, const ConstructionWorld& world)
			: snapping_(&snapping),
			  world_(&world) {}

		// Snap `cursor` given the points already placed (`points`, world meters).
		// Priority: origin-close (3+ points, regardless of freeform) > foundation
		// vertex > foundation edge > world-axis guide > angle step > raw.
		[[nodiscard]] SnapResult
		snap(const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 cursor, const SnapInput& input) const;

		// Wall-chain snap. `points` is the in-progress polyline (world meters).
		// Priority: origin-close > wall endpoint > foundation vertex >
		// point-on-wall-segment (T-junction) > foundation edge > world-axis guide >
		// angle step off the previous chain point > raw. Origin-close needs
		// kMinClosingWallChainPoints points, is skipped under freeform, and returns the
		// chain's first point exactly. The result's hitVertex/hitSegment tell the
		// WallTool what to commit against. Foundation-vertex and foundation-edge hits
		// are inset by the input's half-thickness (see outerFaceFlushCorner), so a wall
		// traced along the perimeter sits ON the foundation; wall-endpoint and
		// T-junction hits snap to the existing wall graph exactly and are never inset.
		[[nodiscard]] SnapResult
		snapWall(const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 cursor, const SnapInput& input) const;

		// Opening snap (Epic F). Find the nearest BUILT wall segment whose centerline
		// passes within edgeSnapRadius of `cursor`, project the cursor onto it, and
		// return the placement for an opening of `openingWidthMeters`. t is clamped to
		// openingRange (OpeningGeometry.h), the range the validator accepts: the end
		// margins from openingMargin config, measured from each end of the wall's
		// band as its corners trim it. A wall with no range (its wall graph cannot be
		// resolved) or too short to fit the opening is skipped, and a farther wall may
		// win. Returns valid=false if no built wall in range is left. Only BUILT
		// segments are candidates (a blueprint wall has no surface to cut yet).
		// Reads segments() in stable order; ties keep the lowest-id segment.
		[[nodiscard]] OpeningSnap snapOpening(::Foundation::Vec2 cursor, float openingWidthMeters) const;

	  private:
		// Whether `cursor` is within the origin-close radius of the first point, once
		// `points` holds at least `minPoints`. A negative `radiusMeters` uses the config.
		[[nodiscard]] bool snapToOrigin(
			const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 cursor, std::size_t minPoints, float radiusMeters
		) const;

		// Nearest existing WALL vertex within vertexSnapRadius. On a hit, `out` is the
		// snapped point and `outVertex` the vertex id (so the chain joins it exactly).
		[[nodiscard]] bool snapToWallVertex(::Foundation::Vec2 cursor, ::Foundation::Vec2& out, VertexId& outVertex) const;

		// Nearest point on any existing wall segment's interior within edgeSnapRadius.
		// On a hit, `out` is the projected point and `outSegment` the segment id (so
		// the tool knows the commit will split it into a T-junction). Endpoints are
		// excluded: those are wall-vertex hits, which win on priority anyway.
		[[nodiscard]] bool snapToWallSegment(::Foundation::Vec2 cursor, ::Foundation::Vec2& out, SegmentId& outSegment) const;

		// Angle-snap the segment (prev -> cursor) onto the nearest multiple of the
		// configured increment, measured relative to the previous segment when one
		// exists, else relative to the world +x axis. Distance along the ray is
		// preserved.
		[[nodiscard]] SnapResult snapAngle(const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 cursor) const;

		// World-axis guides: if the cursor lines up (within guideTolerance) on X and/or
		// Y with a reference node, snap that axis onto the node and report a guide
		// through it (a GuideLine for one axis, a GuideCrossing for both). Reference
		// nodes are the in-progress `points` plus, when `alignToExistingBuildings`,
		// committed-foundation vertices within guideRange of the cursor. Kind None when
		// neither axis aligns.
		[[nodiscard]] SnapResult
		alignToWorldAxes(const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 cursor, bool alignToExistingBuildings) const;

		// The rules shared by snap() and snapWall() after the geometry hits: the
		// world-axis guides, then the angle step, then the raw cursor, each as the aids
		// and freeform allow.
		[[nodiscard]] SnapResult
		snapToHelpers(const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 cursor, const SnapInput& input) const;

		const assets::SnappingConfig* snapping_;
		const ConstructionWorld*			  world_;
	};

} // namespace construction
