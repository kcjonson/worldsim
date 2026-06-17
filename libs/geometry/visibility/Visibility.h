#pragma once

#include "../core/Vec2i64.h"
#include "../polygon/Polygon.h"

#include <cstdint>
#include <vector>

// Exact 2D visibility-polygon construction over the integer-millimeter substrate
// (Vec2i64). Given an observer, a sight radius, and opaque occluder segments, it
// computes the star-shaped region the observer can see, bounded by the nearest
// occluders and clipped to the sight circle. The perception primitive the Vision
// System builds on (canSee, fog-of-war mask). Pure geometry: no engine, no ECS.
//
// All math is exact integer (orientation / segment intersection via 128-bit
// predicates) except for one isolated, documented step: the sight circle has no
// exact integer representation, so it is approximated as a fine N-gon via float
// trig + std::llround, matching the library's curved-geometry policy (the flora
// octagon, the renderer's 64-gon).

namespace geometry {

	struct OccluderSegment {
		Vec2i64 a;
		Vec2i64 b;
	};

	// The star-shaped CCW visibility polygon around `observer`, bounded by the
	// nearest occluders and clipped to the sight circle of radius sightRadiusMm.
	// Integer-mm frame, same as inputs. With no occluders this is the sight
	// circle (a fine polygon approximation). Robust to observer on/near an
	// endpoint, collinear occluders, and occluders crossing the circle.
	Ring computeVisibilityPolygon(const Vec2i64& observer, std::int64_t sightRadiusMm,
								   const std::vector<OccluderSegment>& occluders);

	// One-off line of sight: is the segment observer->target unobstructed by any
	// occluder (and within sightRadiusMm)? For AI canSee() and candidate tests
	// that don't want the full polygon. Exact (uses intersectSegments).
	bool hasLineOfSight(const Vec2i64& observer, const Vec2i64& target, std::int64_t sightRadiusMm,
						const std::vector<OccluderSegment>& occluders);

} // namespace geometry
