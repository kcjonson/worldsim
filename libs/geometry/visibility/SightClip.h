#pragma once

// A sight shape cut to a convex outline round the same point (player-visibility.md 3.3): vision's
// polygon, built a rebuild step past sight, cut to the capsule a stretch of a colonist's path swept,
// or to the sight polygon round where he stands this frame.

#include "../core/Vec2i64.h"
#include "../polygon/Polygon.h"

namespace geometry {

	// The part of `star` inside `convex`, counter-clockwise. `star` winds counter-clockwise round
	// `centre` and is star-shaped round it up to the millimetre snapping of a visibility polygon's
	// hits; `convex` is a convex ring, counter-clockwise, with `centre` strictly inside. Both are
	// star-shaped round `centre`, so along each ray from it the result reaches the nearer of the two
	// boundaries.
	//
	// Its vertices are the star's own inside `convex`, the convex ring's own inside the star, and
	// where an edge of the star crosses the convex ring's boundary, rounded to the millimetre and then
	// stepped a millimetre at a time onto the inner side of both that edge and that side, so a ring cut
	// from the result again keeps inside it. Exact but for that rounding, in integers throughout.
	// Linear in the two rings but for a binary search per vertex. Empty when `convex` does not hold `centre` strictly inside, when a
	// vertex of the star is at `centre`, or when the star's crossings do not pair up (a star far from
	// star-shaped). Not checked for simplicity: a caller that needs a simple ring checks simpleAround.
	Ring starClippedToConvex(const Ring& star, const Vec2i64& centre, const Ring& convex);

} // namespace geometry
