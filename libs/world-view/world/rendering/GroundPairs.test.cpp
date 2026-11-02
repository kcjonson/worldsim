// Tests for the land shader's pair table: which def's transition lands at which entry, and what a draw
// packs into u_landPair.

#include "world/rendering/GroundPairs.h"

#include <gtest/gtest.h>

#include <array>

using namespace worldview;

namespace {

	constexpr size_t id(world::Surface s) {
		return static_cast<size_t>(s);
	}

	size_t level(world::Surface s) {
		return static_cast<size_t>(world::surfaceStackLevel(static_cast<uint8_t>(s)) - 1);
	}

	size_t pairIndex(world::Surface upper, world::Surface lower) {
		return level(upper) * kLandLevels + level(lower);
	}

	/// Every surface's transitions told apart by width: upper surface id * 0.1 + lower surface id * 0.01.
	std::array<assets::GroundTransitions, world::kSurfaceCount> markedTransitions() {
		std::array<assets::GroundTransitions, world::kSurfaceCount> bySurface{};
		for (size_t upper = 0; upper < world::kSurfaceCount; ++upper) {
			for (size_t lower = 0; lower < world::kSurfaceCount; ++lower) {
				bySurface[upper][lower].widthM = 0.1F * static_cast<float>(upper) + 0.01F * static_cast<float>(lower);
			}
		}
		return bySurface;
	}

} // namespace

TEST(GroundPairsTest, AnEntryIsTheUpperSurfacesTransitionOverTheLower) {
	const auto			  bySurface = markedTransitions();
	const GroundPairTable table = buildGroundPairTable(bySurface);
	using S = world::Surface;
	for (const auto& [upper, lower] : {std::pair{S::Rock, S::Sand}, {S::Snow, S::Mud}, {S::GrassTall, S::GrassShort}, {S::Sand, S::Mud}, {S::Grass, S::Dirt}}) {
		EXPECT_EQ(table[pairIndex(upper, lower)], bySurface[id(upper)][id(lower)]) << world::surfaceToString(upper) << " over " << world::surfaceToString(lower);
	}
	EXPECT_EQ(pairIndex(S::Snow, S::Rock), 8U * 9U + 7U) << "Snow and Rock are D13 levels 8 and 7";

	// Never read: the upper level isn't above the lower one.
	for (size_t upper = 0; upper < kLandLevels; ++upper) {
		for (size_t lower = upper; lower < kLandLevels; ++lower) {
			EXPECT_EQ(table[upper * kLandLevels + lower], assets::GroundTransition{}) << upper << ", " << lower;
		}
	}
}

TEST(GroundPairsTest, ADrawPacksItsMipWindowAndPixelFloor) {
	using S = world::Surface;
	std::array<assets::GroundTransitions, world::kSurfaceCount> bySurface{};
	bySurface[id(S::Rock)][id(S::Sand)] = {.widthM = 0.7F, .offsetM = -0.2F, .softness = 0.04F, .breakup = 0.5F, .shadow = 0.1F, .stain = 0.2F, .ridge = 0.06F, .tint = 0.3F};
	const GroundPairTable table = buildGroundPairTable(bySurface);

	std::array<float, world::kSurfaceCount> mips{};
	mips[id(S::Rock)] = 1.2F;
	mips[id(S::Sand)] = 3.0F; // the coarser of the two sets the window: 3 / 6

	std::array<float, kLandPairFloats> out{};
	packGroundPairs(table, mips, 0.05F, out);
	const float* v = out.data() + pairIndex(S::Rock, S::Sand) * kLandPairVec4s * 4;
	EXPECT_FLOAT_EQ(v[0], 0.5F / 0.7F);
	EXPECT_FLOAT_EQ(v[1], 0.5F + 0.5F * -0.2F / 0.7F);
	EXPECT_FLOAT_EQ(v[2], 0.5F / 0.5F);
	EXPECT_FLOAT_EQ(v[3], 0.25F * 0.5F / 0.5F);
	EXPECT_EQ((std::array{v[4], v[5], v[6], v[7]}), (std::array{0.1F, 0.2F, 0.06F, 0.3F}));

	// Magnified, the window is the pair's softness; a pixel wider than the band widens it.
	mips.fill(-0.5F);
	packGroundPairs(table, mips, 1.0F, out);
	EXPECT_FLOAT_EQ(v[0], 0.5F);
	EXPECT_FLOAT_EQ(v[1], 0.5F - 0.1F);
	EXPECT_FLOAT_EQ(v[2], 0.5F / 0.04F);

	// The contest's compact support: at b = +-1 the weight is exactly 1 or 0 for any heights in [0, 1].
	const float inv2W = v[2];
	const float nu4W  = v[3];
	for (const float b : {-1.0F, 1.0F}) {
		for (const float dh : {-1.0F, 1.0F}) {
			for (const float along : {-1.0F, 1.0F}) {
				const float s = dh * inv2W + b * (inv2W + nu4W + 0.5F) + along * nu4W + 0.5F;
				EXPECT_TRUE(b > 0.0F ? s >= 1.0F : s <= 0.0F) << b << " " << dh << " " << along;
			}
		}
	}
}
