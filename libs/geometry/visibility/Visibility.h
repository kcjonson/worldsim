#pragma once

#include "../core/Vec2i64.h"
#include "../polygon/Polygon.h"

#include <cstdint>
#include <vector>

// Exact 2D visibility-polygon construction over the integer-millimeter substrate
// (Vec2i64). Given an observer, a sight radius, and opaque occluder segments, it
// computes the star-shaped region the observer can see, bounded by the nearest
// occluders and clipped to the sight circle. The perception primitive the Vision
// System builds on (canSee, player visibility). Pure geometry: no engine, no ECS.
//
// All math is exact integer (orientation / segment intersection via 128-bit
// predicates); hits are snapped to the millimetre. The sight circle is the sight
// polygon (SightPolygon.h), built from a compile-time table, so the result is the
// same on every toolchain.

namespace geometry {

	struct OccluderSegment {
		Vec2i64 a;
		Vec2i64 b;
	};

	// The CCW visibility polygon around `observer`, bounded by the nearest
	// occluders and clipped to sightPolygon(observer, sightRadiusMm). Integer-mm
	// frame, same as inputs. With no occluders it is that sight polygon. Robust to
	// observer on/near an endpoint, collinear occluders, and occluders crossing the
	// rim. Star-shaped about the observer up to the millimetre snapping of its hits:
	// a shadow edge's far end, snapped, can sit a hair behind its near end's angle,
	// so the ring is not promised simple. A caller that needs a simple ring checks
	// simpleAround.
	Ring computeVisibilityPolygon(const Vec2i64& observer, std::int64_t sightRadiusMm,
								   const std::vector<OccluderSegment>& occluders);

	// An angle round a centre that counts whole turns: turns x 2 pi plus the angle of
	// `direction` (never zero) counter-clockwise from +x.
	struct UnwrappedAngle {
		std::int64_t turns = 0;
		Vec2i64		 direction;
	};

	// Exact strict order of unwrapped angles.
	bool angleBefore(const UnwrappedAngle& first, const UnwrappedAngle& second);

	// The unwrapped angles of the ring's vertices round `centre`, vertex 0 in turn 0 and
	// each step taken the short way round, then vertex 0 again at the end (so `out` has
	// one more entry than the ring, and its last turns count is the ring's winding
	// number). False, with `out` unfinished, when a vertex is at the centre or an edge
	// passes through it.
	bool unwrapAngles(const Ring& ring, const Vec2i64& centre, std::vector<UnwrappedAngle>& out);

	// Whether `ring` is simple and winds once counter-clockwise round `centre`, which
	// lies strictly inside it. Exact. Two edges can meet only where their angular spans
	// round the centre meet, so only those pairs are tested: one pair a vertex on a
	// near star-shaped ring like a visibility polygon. False also for a ring whose
	// vertices spread over more than two turns round the centre.
	bool simpleAround(const Ring& ring, const Vec2i64& centre);

	// One-off line of sight: is the segment observer->target unobstructed by any
	// occluder (and within sightRadiusMm)? For AI canSee() and candidate tests
	// that don't want the full polygon. Exact (uses intersectSegments).
	bool hasLineOfSight(const Vec2i64& observer, const Vec2i64& target, std::int64_t sightRadiusMm,
						const std::vector<OccluderSegment>& occluders);

} // namespace geometry
