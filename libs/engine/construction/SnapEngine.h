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

#include <core/Vec2i64.h>
#include <math/Types.h>

#include <vector>

namespace engine::assets {
	struct SnappingConfig;
}

namespace engine::construction {

	class ConstructionWorld;

	enum class SnapKind {
		None,	// raw cursor, no snap applied
		Angle,	// snapped onto an angle increment off the previous segment
		Vertex, // snapped onto an existing foundation vertex
		Edge,	// snapped onto the nearest point of an existing foundation edge
		Origin, // within originClose radius of the first point: closes the shape
	};

	struct SnapResult {
		::Foundation::Vec2 point{0.0F, 0.0F};
		SnapKind		   kind = SnapKind::None;

		// For Angle snaps: the guide ray from the previous vertex through the
		// snapped point, so the tool can draw the faint guide line. Zero otherwise.
		::Foundation::Vec2 guideFrom{0.0F, 0.0F};
		::Foundation::Vec2 guideTo{0.0F, 0.0F};

		// True when the snap closes the polygon onto its origin (kind == Origin):
		// the tool reads this to know a click commits.
		[[nodiscard]] bool closesShape() const { return kind == SnapKind::Origin; }
	};

	class SnapEngine {
	  public:
		SnapEngine(const engine::assets::SnappingConfig& snapping, const ConstructionWorld& world)
			: snapping_(&snapping),
			  world_(&world) {}

		// Snap `cursor` given the points already placed (`points`, world meters) and
		// whether the user is holding the freeform modifier (suppresses angle snap).
		[[nodiscard]] SnapResult snap(const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 cursor, bool freeform) const;

	  private:
		// Nearest existing-foundation vertex within vertexSnapRadius, if any.
		[[nodiscard]] bool snapToVertex(::Foundation::Vec2 cursor, ::Foundation::Vec2& out) const;

		// Nearest point on any existing-foundation edge within edgeSnapRadius.
		[[nodiscard]] bool snapToEdge(::Foundation::Vec2 cursor, ::Foundation::Vec2& out) const;

		// Angle-snap the segment (prev -> cursor) onto the nearest multiple of the
		// configured increment, measured relative to the previous segment when one
		// exists, else relative to the world +x axis. Distance along the ray is
		// preserved.
		[[nodiscard]] SnapResult snapAngle(const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 cursor) const;

		const engine::assets::SnappingConfig* snapping_;
		const ConstructionWorld*			  world_;
	};

} // namespace engine::construction
