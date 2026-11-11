#include "assets/placement/FloraMeasure.h"

#include <gtest/gtest.h>
#include <cmath>
#include <limits>

#include <string>
#include <unordered_map>
#include <vector>

namespace assets {
	namespace {

		// A 2 m x 2 m crown centred on the trunk, in the template-local millimetre frame.
		const std::vector<geometry::Ring> kSquareCrown{{{-1000, -1000}, {1000, -1000}, {1000, 1000}, {-1000, 1000}}};

		const std::vector<geometry::Ring>* squareCrowns(const PlacedEntity& /*tree*/) {
			return &kSquareCrown;
		}

		PlacedEntity tree(const std::string& defName, float x, float y, bool tall = true, uint8_t ageClass = 0) {
			PlacedEntity e;
			e.defName = defName;
			e.position = {x, y};
			e.isTallOccluder = tall;
			e.traits.ageClass = ageClass;
			return e;
		}

	} // namespace

	TEST(FloraMeasure, CountsTreesDensityCoverAndWoodWithoutDoubleCountingOverlap) {
		const std::unordered_map<std::string, FloraDefTraits> traits{
			{"Oak", {.isTree = true, .woodPerTree = 25.0}},
			{"Maple", {.isTree = true, .woodPerTree = 10.0}},
			{"Rock", {.isTree = false, .woodPerTree = 0.0}},
		};
		// Two oaks whose crowns overlap by 1 m x 2 m, one maple apart, a short plant, and a
		// tall rock that is not a tree and stays out of the totals and the cover.
		const std::vector<PlacedEntity> world{
			tree("Oak", 5.0F, 5.0F),
			tree("Oak", 6.0F, 5.0F),
			tree("Maple", 15.0F, 5.0F),
			tree("Oak", 12.0F, 5.0F, false),
			tree("Rock", 10.0F, 5.0F)
		};
		std::vector<const PlacedEntity*> entities;
		for (const auto& e : world) {
			entities.push_back(&e);
		}
		const FloraRect rect{0.0F, 0.0F, 20.0F, 10.0F};

		const FloraMeasurement all = measureFlora(rect, {rect}, entities, traits, squareCrowns);
		EXPECT_DOUBLE_EQ(all.measuredAreaM2, 200.0);
		EXPECT_EQ(all.treeCount, 3U);
		EXPECT_DOUBLE_EQ(all.treesPerThousandM2, 15.0);
		EXPECT_NEAR(all.canopyCover, 10.0 / 200.0, 1e-9); // 3x2 oak union plus 2x2 maple
		EXPECT_DOUBLE_EQ(all.woodPerThousandM2, 300.0);
		ASSERT_EQ(all.defs.size(), 2U);
		EXPECT_EQ(all.defs[0].defName, "Maple");
		EXPECT_EQ(all.defs[0].count, 1U);
		EXPECT_EQ(all.defs[1].defName, "Oak");
		EXPECT_EQ(all.defs[1].count, 2U);
		EXPECT_DOUBLE_EQ(all.defs[1].perThousandM2, 10.0);
		EXPECT_DOUBLE_EQ(all.defs[1].woodPerThousandM2, 250.0);
		ASSERT_EQ(all.otherTallOccluders.size(), 1U);
		EXPECT_EQ(all.otherTallOccluders[0].defName, "Rock");
		EXPECT_EQ(all.otherTallOccluders[0].count, 1U);

		// Only the west half loaded: the maple is past it, and the area is honest about it.
		const FloraMeasurement half = measureFlora(rect, {{0.0F, 0.0F, 10.0F, 10.0F}}, entities, traits, squareCrowns);
		EXPECT_DOUBLE_EQ(half.rectAreaM2, 200.0);
		EXPECT_DOUBLE_EQ(half.measuredAreaM2, 100.0);
		EXPECT_EQ(half.treeCount, 2U);
		EXPECT_DOUBLE_EQ(half.treesPerThousandM2, 20.0);
		EXPECT_NEAR(half.canopyCover, 6.0 / 100.0, 1e-9);
	}

	// Trees of a def with age classes are counted per class too, and the nearest-of-age
	// query lists one class, nearest first, filtered by def when asked.
	TEST(FloraMeasure, CountsAgeClassesAndFindsTheNearestOfAnAge) {
		const std::unordered_map<std::string, FloraDefTraits> traits{
			{"Oak", {.isTree = true, .woodPerTree = 25.0, .ageClassNames = {"sapling", "mature", "veteran"}}},
			{"Maple", {.isTree = true, .woodPerTree = 10.0, .ageClassNames = {"sapling", "mature", "veteran"}}},
			{"Pine", {.isTree = true, .woodPerTree = 10.0}},
		};
		const std::vector<PlacedEntity> world{
			tree("Oak", 5.0F, 5.0F, true, 0),
			tree("Oak", 9.0F, 5.0F, true, 2),
			tree("Oak", 2.0F, 5.0F, true, 0),
			tree("Maple", 16.0F, 5.0F, true, 0),
			tree("Pine", 3.0F, 3.0F),
		};
		std::vector<const PlacedEntity*> entities;
		for (const auto& e : world) {
			entities.push_back(&e);
		}
		const FloraRect rect{0.0F, 0.0F, 20.0F, 10.0F};

		const FloraMeasurement m = measureFlora(rect, {rect}, entities, traits, squareCrowns);
		ASSERT_EQ(m.defs.size(), 3U);
		const FloraDefMeasure& oak = m.defs[1];
		ASSERT_EQ(oak.defName, "Oak");
		ASSERT_EQ(oak.ageClasses.size(), 3U);
		EXPECT_EQ(oak.ageClasses[0].name, "sapling");
		EXPECT_EQ(oak.ageClasses[0].count, 2U);
		EXPECT_DOUBLE_EQ(oak.ageClasses[0].perThousandM2, 10.0);
		EXPECT_EQ(oak.ageClasses[1].count, 0U);
		EXPECT_EQ(oak.ageClasses[2].count, 1U);
		EXPECT_TRUE(m.defs[2].ageClasses.empty()); // Pine has none

		const auto saplings = nearestTreesOfAge({10.0F, 5.0F}, "sapling", "", kFloraNearestMax, entities, traits);
		ASSERT_EQ(saplings.size(), 3U);
		EXPECT_EQ(saplings[0].defName, "Oak");
		EXPECT_FLOAT_EQ(saplings[0].position.x, 5.0F);
		EXPECT_EQ(saplings[1].defName, "Maple");
		EXPECT_EQ(saplings[2].ageClass, "sapling");
		EXPECT_DOUBLE_EQ(saplings[2].distance, 8.0);

		EXPECT_EQ(nearestTreesOfAge({10.0F, 5.0F}, "sapling", "Maple", kFloraNearestMax, entities, traits).size(), 1U);
		EXPECT_EQ(nearestTreesOfAge({10.0F, 5.0F}, "sapling", "", 1, entities, traits).size(), 1U);
		EXPECT_EQ(nearestTreesOfAge({10.0F, 5.0F}, "veteran", "", kFloraNearestMax, entities, traits).front().position.x, 9.0F);
	}

	TEST(FloraMeasure, RectLimitsBoundTheGridNotJustTheArea) {
		EXPECT_TRUE(floraRectWithinLimits({0.0F, 0.0F, 500.0F, 500.0F}));
		EXPECT_FALSE(floraRectWithinLimits({0.0F, 0.0F, 100000000.0F, 0.001F})); // ~100,000 m2 but a billion cells
		EXPECT_FALSE(floraRectWithinLimits({0.0F, 0.0F, 501.0F, 10.0F}));
		EXPECT_FALSE(floraRectWithinLimits({0.0F, 0.0F, 0.0F, 10.0F}));
		EXPECT_FALSE(floraRectWithinLimits({10.0F, 10.0F, 0.0F, 0.0F}));

		// Coordinates are bounded by the world, not just the size: a rect far out is refused.
		EXPECT_TRUE(floraRectWithinLimits({kFloraMaxCoordMeters - 10.0F, -kFloraMaxCoordMeters, kFloraMaxCoordMeters, -kFloraMaxCoordMeters + 10.0F}));
		EXPECT_FALSE(floraRectWithinLimits({1e30F, 0.0F, 1e30F + 1e24F, 10.0F}));
		EXPECT_FALSE(floraRectWithinLimits({0.0F, 0.0F, 10.0F, 2.0F * kFloraMaxCoordMeters}));
		EXPECT_FALSE(floraRectWithinLimits({-1e30F, 0.0F, -1e30F + 1e24F, 10.0F}));

		EXPECT_TRUE(floraPointWithinWorld(0.0F, 0.0F));
		EXPECT_TRUE(floraPointWithinWorld(-kFloraMaxCoordMeters, kFloraMaxCoordMeters));
		EXPECT_FALSE(floraPointWithinWorld(1e30F, 0.0F));
		EXPECT_FALSE(floraPointWithinWorld(0.0F, -1e30F));
		EXPECT_FALSE(floraPointWithinWorld(std::nextafter(kFloraMaxCoordMeters, 1e30F), 0.0F));
		EXPECT_FALSE(floraPointWithinWorld(std::numeric_limits<float>::quiet_NaN(), 0.0F));
		EXPECT_FALSE(floraPointWithinWorld(0.0F, std::numeric_limits<float>::infinity()));

		// An oversized rect measures nothing and allocates no grid.
		const FloraMeasurement m = measureFlora({0.0F, 0.0F, 100000000.0F, 0.001F}, {}, {}, {}, squareCrowns);
		EXPECT_EQ(m.rectAreaM2, 0.0);
		EXPECT_EQ(m.treeCount, 0U);
	}

} // namespace assets
