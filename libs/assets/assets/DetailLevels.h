#pragma once

// Mesh levels of detail (a def's <detailLevels>): the screen densities, in pixels a meter, its
// generator cuts a template for. The renderer draws each template at the level its camera's density
// picks; everything that isn't drawing (silhouette, reach, routing, collision) reads one reference level.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace assets {

	/// A def declares at most this many levels; a level index fits in 3 bits.
	inline constexpr size_t kMaxDetailLevels = 8;

	/// The density the non-drawing template data is taken at and the load prewarms: the game's default
	/// view, 8 px/m at zoom 3 (GameScene asserts the two agree).
	inline constexpr float kReferencePixelsPerMeter = 24.0F;

	/// A level may reach at most this factor times its template's reference level's reach. The cull
	/// margin and the selection broad phase read the reference level, so they are widened by it, and
	/// the render smoke fails a def whose level goes past it.
	inline constexpr float kLevelReachMargin = 1.1F;

	/// The level a screen density draws: the highest level at or below it, else the lowest. 0 for a def
	/// with no levels (its one mesh). `levels` is ascending.
	[[nodiscard]] inline uint8_t detailLevelFor(std::span<const float> levels, float pixelsPerMeter) {
		uint8_t level = 0;
		for (size_t i = 1; i < levels.size(); ++i) {
			if (levels[i] <= pixelsPerMeter) {
				level = static_cast<uint8_t>(i);
			}
		}
		return level;
	}

	/// The level non-drawing data is read from: the one the reference density draws.
	[[nodiscard]] inline uint8_t referenceDetailLevel(std::span<const float> levels) {
		return detailLevelFor(levels, kReferencePixelsPerMeter);
	}

	/// The ready level closest to `wanted` (bit i of readyMask: level i is ready), the finer of two
	/// equally close; nullopt when none is ready.
	[[nodiscard]] inline std::optional<uint8_t> nearestReadyLevel(uint8_t readyMask, uint8_t wanted) {
		for (int distance = 0; distance < static_cast<int>(kMaxDetailLevels); ++distance) {
			for (const int level : {wanted + distance, wanted - distance}) {
				if (level >= 0 && level < static_cast<int>(kMaxDetailLevels) && (readyMask & (1U << level)) != 0) {
					return static_cast<uint8_t>(level);
				}
			}
		}
		return std::nullopt;
	}

} // namespace assets
