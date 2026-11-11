#pragma once

// The grove field: low-frequency, domain-warped value noise in [0,1] that varies
// "spaced" flora (trees) so a forest reads as dense stands with rare open glades and
// rare extra-dense thickets, rather than an even orchard. A pure function of world
// position and seed, so it is continuous across chunk seams. Placement thins its low
// tail to glades (spawn chance) and tightens spacing in its high tail (thickets); the
// broad middle is normal forest, densest toward its high end. The same noise at about a
// fifth of the size opens small gaps inside stands, and per-tree traits read it at another
// size for the age field.
//
// The canopy weight reads the same terms for the ground: the forest floor (litter and
// shade) lies where the trees stand, so trees and floor agree by construction.

#include "world/Biome.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace world::grove {

	inline constexpr float kFeatureMeters = 105.0F; // grove, glade, and thicket scale
	inline constexpr float kGladeSpawnMin = 0.12F;	// glade floor: fraction of base spawn chance
	inline constexpr float kGladeBandLo = 0.25F;	// raw field below this is a full glade
	inline constexpr float kGladeBandHi = 0.65F;	// density rises across the band to a stand core here
	inline constexpr float kThicketBandLo = 0.70F;	// above this thickets begin
	inline constexpr float kThicketBandHi = 0.90F;	// fully a thicket core here (rare)
	inline constexpr float kThicketTighten = 0.45F; // thicket cores pack to (1 - this) of base minDistance
	inline constexpr float kGapFeatureMeters = 22.0F; // small gaps inside a stand
	inline constexpr float kGapBandLo = 0.28F;		  // gap field below this is an open gap (about 8% of the ground)
	inline constexpr float kGapBandHi = 0.40F;		  // above this the gap field leaves density alone

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

	/// 0 in a small gap inside a stand, rising to 1 where there is none: warpedNoise at
	/// kGapFeatureMeters through the gap band.
	[[nodiscard]] float gapWeight(float worldX, float worldY, uint64_t seed);

	// The forest floor follows the canopy: its edge sits partway up the glade band, where the
	// trees have thinned to about a third of a stand's, and crosses it over a few meters; inside,
	// the loose forest near the edge carries less floor than a stand core, and the small gaps
	// stay greener.
	inline constexpr float kFloorEdgeLo = 0.36F; // grove field below this has no floor (the glade)
	inline constexpr float kFloorEdgeHi = 0.42F; // the floor's edge is complete here
	inline constexpr float kFloorLoose = 0.55F;	 // floor at the edge, against 1 where the glade band ends
	inline constexpr float kFloorInGap = 0.25F;	 // floor left in the middle of a small gap

	/// How much forest each biome carries, 0 (none: open land) to 1 (closed canopy), from the tree
	/// defs that place there (assets::canopyCoverFromDefinitions). Indexed by Biome.
	struct CanopyCover {
		std::array<float, static_cast<size_t>(Biome::Count)> byBiome{};

		[[nodiscard]] float at(Biome biome) const { return byBiome[static_cast<size_t>(biome)]; }
	};

	/// The forest floor's weight at a world position, 0 (open ground) to 1 (a stand's full
	/// floor), for a biome whose canopy cover is `cover`: exactly 0 where cover is 0 or the grove
	/// field opens a glade.
	[[nodiscard]] float canopyWeight(float worldX, float worldY, uint64_t seed, float cover);

} // namespace world::grove
