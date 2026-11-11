// The canopy weight against the trees: the forest floor lies only where placement keeps a stand's
// density, never in a glade, and fades into a glade over a few meters, not the grove band's tens.

#include "world/generation/GroveField.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

namespace world::grove {

	namespace {
		constexpr uint64_t kSeed = 0x0DDBA11C0FFEEULL;
	}

	TEST(GroveFieldTest, CanopyWeightFollowsTheTrees) {
		size_t floor = 0;
		size_t samples = 0;
		for (int32_t y = -1000; y < 1000; y += 3) {
			for (int32_t x = -1000; x < 1000; x += 3) {
				const auto	fx = static_cast<float>(x);
				const auto	fy = static_cast<float>(y);
				const float w = canopyWeight(fx, fy, kSeed, 1.0F);
				ASSERT_EQ(canopyWeight(fx, fy, kSeed, 0.0F), 0.0F);
				ASSERT_GE(w, 0.0F);
				ASSERT_LE(w, 1.0F);
				const float grove = groveValue(fx, fy, kSeed);
				// Placement's spawn scale (PlacementExecutor): a glade keeps kGladeSpawnMin of it.
				const float spawn = kGladeSpawnMin + (1.0F - kGladeSpawnMin) * gladeWeight(grove) * gapWeight(fx, fy, kSeed);
				if (grove <= kFloorEdgeLo) {
					ASSERT_EQ(w, 0.0F) << x << ", " << y;
				}
				if (w >= 0.5F) {
					EXPECT_GE(spawn, 0.3F) << x << ", " << y;
				}
				floor += w > 0.0F ? 1 : 0;
				++samples;
			}
		}
		// Most of a forest has a floor; its glades and gaps don't.
		const double share = static_cast<double>(floor) / static_cast<double>(samples);
		EXPECT_GT(share, 0.5);
		EXPECT_LT(share, 0.95);
	}

	// The fade from glade to floor: along rows, the distance from the last zero to the first sample
	// at half the stand's floor, the edge the eye reads. Its median is a few meters.
	TEST(GroveFieldTest, CanopyEdgeFadesOverAFewMeters) {
		std::vector<float> widths;
		for (int32_t y = -2000; y < 2000; y += 37) {
			float lastZero = 0.0F;
			bool  seenZero = false;
			for (int32_t x = -2000; x < 2000; x += 1) {
				const float w = canopyWeight(static_cast<float>(x), static_cast<float>(y), kSeed, 1.0F);
				if (w == 0.0F) {
					lastZero = static_cast<float>(x);
					seenZero = true;
				} else if (seenZero && w >= 0.5F * kFloorLoose) {
					widths.push_back(static_cast<float>(x) - lastZero);
					seenZero = false;
				}
			}
		}
		ASSERT_GT(widths.size(), 50U);
		std::sort(widths.begin(), widths.end());
		const float median = widths[widths.size() / 2];
		std::cout << "canopy edge: " << widths.size() << " crossings, quartiles " << widths[widths.size() / 4] << " / " << median
				  << " / " << widths[widths.size() * 3 / 4] << " m\n";
		EXPECT_GE(median, 2.0F);
		EXPECT_LE(median, 8.0F);
	}

} // namespace world::grove
