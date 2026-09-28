#pragma once

#include "../core/Vec2d.h"

#include <cstddef>
#include <span>
#include <vector>

// Centerline sampling for river ribbons (terrain-polygons D7 step 1). Two
// chunks sample the same river from their own gathered copies of its segment
// list, cut at different places, so every sample is a function of a bounded
// window of input points and never of where the input starts or ends.

namespace geometry {

	struct CenterlineSample {
		Vec2d		position;		  // world meters
		double		halfWidthM = 0.0; // linear in t between the span's endpoint half-widths
		std::size_t segment	   = 0;	  // the sample lies on span [segment, segment + 1]
		double		t		   = 0.0; // parameter fraction along that span, 0 at its first point
	};

	// Centripetal (alpha 0.5) Catmull-Rom through `points`, sampled in world
	// meters. Span [i, i + 1] is cut into n = max(1, ceil(|p[i+1] - p[i]| /
	// maxSpacingM)) equal parameter steps, so its samples depend only on p[i-1]
	// through p[i+2]; the first and last spans use a reflected phantom point
	// (2 p[0] - p[1], and likewise at the far end). Parameter steps are not arc
	// steps: the curve's speed varies along a span where neighboring chords
	// differ, so sample spacing can run over maxSpacingM (about 10% on a river
	// polyline with ~20 m points, more where adjacent chords differ sharply).
	//
	// Every input point appears exactly once, copied bit-for-bit: point i as the
	// t = 0 sample of span i, the last point as the t = 1 sample of the last span.
	// Preconditions: at least 2 points, one half-width per point, consecutive
	// points distinct, maxSpacingM > 0.
	std::vector<CenterlineSample>
	sampleCatmullRom(std::span<const Vec2d> points, std::span<const double> halfWidthsM, double maxSpacingM);

} // namespace geometry
