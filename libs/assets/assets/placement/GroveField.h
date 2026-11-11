#pragma once

// The grove field: low-frequency, domain-warped value noise in [0,1] that varies
// "spaced" flora (trees) so a forest reads as dense stands with rare open glades and
// rare extra-dense thickets, rather than an even orchard. A pure function of world
// position and seed, so it is continuous across chunk seams. Placement thins its low
// tail to glades (spawn chance) and tightens spacing in its high tail (thickets); the
// broad middle is normal forest. Per-tree traits read the same noise at a smaller
// feature size for the age field.

#include <cstdint>

namespace assets::grove {

	inline constexpr float kFeatureMeters = 105.0F; // grove, glade, and thicket scale
	inline constexpr float kGladeSpawnMin = 0.12F;	// glade floor: fraction of base spawn chance
	inline constexpr float kGladeBandLo = 0.20F;	// raw field below this is a full glade
	inline constexpr float kGladeBandHi = 0.42F;	// above this is normal forest density
	inline constexpr float kThicketBandLo = 0.70F;	// above this thickets begin
	inline constexpr float kThicketBandHi = 0.90F;	// fully a thicket core here (rare)
	inline constexpr float kThicketTighten = 0.45F; // thicket cores pack to (1 - this) of base minDistance

	/// Smoothstep of v over [a, b], clamped to [0,1].
	[[nodiscard]] float smooth01(float a, float b, float v);

	/// Domain-warped two-octave value noise in [0,1], with features about `featureMeters` across.
	[[nodiscard]] float warpedNoise(float worldX, float worldY, float featureMeters, uint64_t seed);

	/// The raw grove field at a world position: warpedNoise at kFeatureMeters.
	[[nodiscard]] float groveValue(float worldX, float worldY, uint64_t seed);

	/// 0 in normal forest, rising to 1 in a thicket core.
	[[nodiscard]] float thicketWeight(float grove);

	/// 0 in a full glade, rising to 1 at normal forest density.
	[[nodiscard]] float gladeWeight(float grove);

} // namespace assets::grove
