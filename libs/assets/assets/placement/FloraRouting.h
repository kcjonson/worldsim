#pragma once

// Tall or short routing for placed statics. Short flora is baked per chunk and fades to the
// ground texture at far zoom; tall occluders (trees, tall bushes, rocks) draw live in the
// Y-sorted upright stream (WorldDepthSort) so they interleave with colonists by
// ground-contact Y. A def routes one way for every instance, decided from the height of
// its mature variant 0 (tree-variety.md D9), so a sapling of a tall species still sorts
// with actors and no instance flips path by its placement scale.

namespace assets {

	inline constexpr float kShortFloraMaxHeight = 1.0F; // meters; short/tall split

	/// Whether a def whose mature variant 0 is `matureHeightMeters` tall (unscaled) routes to
	/// the sorted stream. Groundcover never does: it renders on its own instanced path.
	[[nodiscard]] constexpr bool routesTall(float matureHeightMeters, bool isGroundcover) {
		return matureHeightMeters >= kShortFloraMaxHeight && !isGroundcover;
	}

} // namespace assets
