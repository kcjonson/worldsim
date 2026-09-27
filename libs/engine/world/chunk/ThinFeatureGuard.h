#pragma once

// ThinFeatureGuard - the 3x3 binomial softening and thin-feature guard both terrain
// fields start from (docs/technical/organic-terrain/terrain-polygons-architecture.md
// D5 steps 2-3, D16 step 1): the waterline field over biome water
// (TerrainPolygonBuilder) and the land field over each surface (SurfaceField).
// shaders/includes/land.glsl writes the same rule out for the land pass.

#include <algorithm>
#include <cstdint>

namespace engine::world {

	/// A thin sample inside the indicator is floored here, a thin one outside it
	/// capped here. They sum to 1, so where two land surfaces meet their guarded
	/// fields still sum to 1 and no gap opens between them. At 0.70 / 0.30 a lone
	/// water tile's loop falls under the waterline's kMinLoopAreaMm2 (these give
	/// 0.37-0.49 m^2), and a diagonal 1-wide run breaks into beads: its tiles join
	/// through bilinear saddles worth (2 x 0.70 + 2 x 0.25) / 4 = 0.475, where these
	/// give 0.55.
	inline constexpr float kThinFeatureFloor = 0.85F;
	inline constexpr float kThinFeatureCeil	 = 0.15F;

	/// A sample is thin when both its neighbors along either axis sit on the other
	/// side of the indicator: every sample with fewer than two same-side cardinal
	/// neighbors, and the samples of a straight 1-wide run, whose two are opposite and
	/// whose blur sits exactly on the 0.5 isoline. An L-corner's two are adjacent, so
	/// it rounds.
	template <typename Indicator>
	[[nodiscard]] bool isThinFeature(const Indicator& inside, int64_t x, int64_t y) {
		const bool self = inside(x, y);
		return (inside(x - 1, y) != self && inside(x + 1, y) != self) || (inside(x, y - 1) != self && inside(x, y + 1) != self);
	}

	/// The indicator at (x, y) softened by the 3x3 binomial kernel (1 2 1 / 2 4 2 /
	/// 1 2 1, over 16), floored or capped where it is thin.
	template <typename Indicator>
	[[nodiscard]] float guardedBlur(const Indicator& inside, int64_t x, int64_t y, float thinFloor, float thinCeil) {
		static constexpr int kBinomial[3][3] = {{1, 2, 1}, {2, 4, 2}, {1, 2, 1}};
		int					 sum			 = 0;
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				if (inside(x + dx, y + dy)) {
					sum += kBinomial[dy + 1][dx + 1];
				}
			}
		}
		const float blur = static_cast<float>(sum) * 0.0625F;
		if (!isThinFeature(inside, x, y)) {
			return blur;
		}
		return inside(x, y) ? std::max(blur, thinFloor) : std::min(blur, thinCeil);
	}

} // namespace engine::world
