#pragma once

#include <polygon/Polygon.h>

#include <glm/vec2.hpp>

#include <vector>

// Builder work slots on a foundation (WOR-509): how many colonists may build it at once, and the
// candidate point each one works from. Pure geometry over the foundation's ring; where a builder
// can actually stand near a candidate is the nav mesh's answer, not this file's.

namespace construction {

	/// The ring's perimeter in meters, closing edge included.
	[[nodiscard]] double ringPerimeterMeters(const geometry::Ring& ring);

	/// A ceiling on the builder spot count that only an absurd configuration reaches (a tiny spacing
	/// with a huge rate); it keeps the count inside an int.
	inline constexpr int kMaxBuilderSpots = 1000;

	/// `capBase + floor(area * capPerSquareMeter)`, capped at `floor(perimeter / minSpacingMeters)` so
	/// spots spread round the ring are at least that far apart, and at kMaxBuilderSpots, and never
	/// below 1. `areaSquareMeters` is taken as its absolute value.
	[[nodiscard]] int builderSpotCount(
		double areaSquareMeters, double perimeterMeters, int capBase, float capPerSquareMeter, float minSpacingMeters
	);

	/// `count` points in world meters, spaced evenly by arc length round the ring: spot k sits on the
	/// ring `(k + 0.5) * perimeter / count` from its first vertex, then `insetMeters` into the
	/// footprint along that edge's inward normal, whichever way the ring winds. A spot within a
	/// millimetre of a vertex goes along the bisector of its two edges instead, so at a right angle it
	/// ends `insetMeters / sqrt(2)` from each edge. A spot whose inset would not end strictly inside the
	/// footprint, or would cross another edge on the way (a strip thinner than twice the inset, a spot
	/// just short of a sharp corner), stays on the ring. A spot on an edge near a convex corner keeps
	/// its inset from its own edge and can be much closer than that to the adjacent one; the spot is
	/// only a candidate, and the nav mesh decides where the builder stands. Empty for a ring of fewer
	/// than three vertices or a count below 1.
	[[nodiscard]] std::vector<glm::vec2> builderSpots(const geometry::Ring& ring, int count, float insetMeters);

} // namespace construction
