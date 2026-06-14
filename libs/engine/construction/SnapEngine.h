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
// angle snap relative to the previous segment > raw.
//
// Snapping is ON by default and escapable: angle snapping is suppressed when
// `freeform` is set (the tool maps this to holding Alt, per the design). Vertex
// and edge snapping to existing geometry are always on; they are the zero-gap
// adjacency the clearance rule depends on, not a stylistic nicety.
//
// Snap radii and the angle increment come from engine::assets::SnappingConfig
// (D10); the engine does not hardcode them.
//
// Walls (Epic D phase 2). Walls draw as an OPEN polyline chain, not a closed
// polygon, so they get their own entry point snapWall() rather than a mode flag
// on snap(): the two tools differ in more than snapping (no origin-close for
// walls, an extra pair of snap targets, a hit-target id the WallTool reads), so
// keeping them separate keeps the foundation path provably unchanged. The wall
// snap priority is the design's full stack:
//   wall endpoint (existing wall vertex) > foundation vertex >
//   point along an existing wall segment (T-junction anywhere) >
//   foundation edge > angle snap off the previous chain point > raw.
// A wall endpoint and a foundation vertex are both "vertex" targets but the wall
// endpoint wins so a chain continues/joins an existing wall in preference to
// merely landing on a foundation corner. The snapped point of an endpoint or
// segment hit is a dequantized integer-mm position, so it round-trips exactly
// back through geometry::quantize when the WallTool commits it (determinism: the
// committed vertex is exact, and segmentAt's tie-break is the highest id).

#include "ConstructionWorld.h"

#include <core/Vec2i64.h>
#include <math/Types.h>

#include <vector>

namespace engine::assets {
	struct SnappingConfig;
}

namespace engine::construction {

	enum class SnapKind {
		None,		  // raw cursor, no snap applied
		Angle,		  // snapped onto an angle increment off the previous segment
		Vertex,		  // snapped onto an existing foundation vertex
		Edge,		  // snapped onto the nearest point of an existing foundation edge
		Origin,		  // within originClose radius of the first point: closes the shape
		WallEndpoint, // snapped onto an existing wall vertex (endpoint/junction)
		WallSegment,  // snapped onto the nearest point along an existing wall segment (a T-junction)
	};

	struct SnapResult {
		::Foundation::Vec2 point{0.0F, 0.0F};
		SnapKind		   kind = SnapKind::None;

		// For Angle snaps: the guide ray from the previous vertex through the
		// snapped point, so the tool can draw the faint guide line. Zero otherwise.
		::Foundation::Vec2 guideFrom{0.0F, 0.0F};
		::Foundation::Vec2 guideTo{0.0F, 0.0F};

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

	class SnapEngine {
	  public:
		SnapEngine(const engine::assets::SnappingConfig& snapping, const ConstructionWorld& world)
			: snapping_(&snapping),
			  world_(&world) {}

		// Snap `cursor` given the points already placed (`points`, world meters) and
		// whether the user is holding the freeform modifier (suppresses angle snap).
		[[nodiscard]] SnapResult snap(const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 cursor, bool freeform) const;

		// Wall-chain snap. `points` is the in-progress open polyline (world meters);
		// `freeform` suppresses angle snap (Alt). Priority: wall endpoint > foundation
		// vertex > point-on-wall-segment (T-junction) > foundation edge > angle snap
		// off the previous chain point > raw. No origin-close (the chain is open). The
		// result's hitVertex/hitSegment tell the WallTool what to commit against.
		[[nodiscard]] SnapResult snapWall(const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 cursor, bool freeform) const;

		// Opening snap (Epic F). Find the nearest BUILT wall segment whose centerline
		// passes within edgeSnapRadius of `cursor`, project the cursor onto it, and
		// return the placement for an opening of `openingWidthMeters`. t is clamped so
		// the opening honors the end margins from openingMargin config: t in
		// [(half+margin)/L, 1-(half+margin)/L]. Returns valid=false if no built wall
		// is in range, or the nearest one is too short to fit the opening. Only BUILT
		// segments are candidates (a blueprint wall has no surface to cut yet).
		// Reads segments() in stable order; ties keep the lowest-id segment.
		[[nodiscard]] OpeningSnap snapOpening(::Foundation::Vec2 cursor, float openingWidthMeters) const;

	  private:
		// Nearest existing-foundation vertex within vertexSnapRadius, if any.
		[[nodiscard]] bool snapToVertex(::Foundation::Vec2 cursor, ::Foundation::Vec2& out) const;

		// Nearest point on any existing-foundation edge within edgeSnapRadius.
		[[nodiscard]] bool snapToEdge(::Foundation::Vec2 cursor, ::Foundation::Vec2& out) const;

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

		const engine::assets::SnappingConfig* snapping_;
		const ConstructionWorld*			  world_;
	};

} // namespace engine::construction
