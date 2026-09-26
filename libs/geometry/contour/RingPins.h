#pragma once

#include "../polygon/Polygon.h"

#include <cstdint>
#include <span>
#include <vector>

// Border pins (terrain-polygons D4/D6). Two chunks build the same shoreline from
// their own extended regions, so their rings differ in start vertex and far from
// the shared border. Pinning the vertices where a ring crosses the chunk-border
// lines, and never moving or removing them afterwards, keeps every later step a
// function of the run between two pins, which both chunks compute identically.

namespace geometry {

	// Inserts a vertex wherever an edge properly crosses (endpoints strictly on
	// either side) one of the vertical lines x = xLinesMm[k] or horizontal lines
	// y = yLinesMm[k]. The coordinate on the line is exact; the other is rounded
	// to the nearest mm (halves up) from the lexicographically ordered endpoints,
	// so a->b and b->a give the same point. Returns a mask, same length as the
	// ring, nonzero for every vertex lying exactly on any of the lines (inserted or
	// pre-existing). Bytes rather than vector<bool>, whose element access takes a
	// process-wide lock in MSVC debug builds and serializes concurrent chunk workers.
	std::vector<std::uint8_t>
	pinAxisLineCrossings(Ring& ring, std::span<const std::int64_t> xLinesMm, std::span<const std::int64_t> yLinesMm);

	// Resamples the closed ring to ~spacingMm vertex spacing. Pinned vertices are
	// kept exactly; each run between consecutive pinned vertices is resampled
	// independently to n = max(1, round(runLength / spacingMm)) equal arc-length
	// steps, new vertices rounded to the nearest mm. With no pins the whole ring is
	// one run starting at vertex 0 (a single pin likewise anchors one run), and n
	// is at least 3 so the ring stays a polygon. Consecutive duplicates from
	// rounding are collapsed. The mask is updated in place.
	void resampleRing(Ring& ring, std::int64_t spacingMm, std::vector<std::uint8_t>& pinned);

} // namespace geometry
