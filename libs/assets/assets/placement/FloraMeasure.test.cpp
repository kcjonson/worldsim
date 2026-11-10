#include "assets/placement/FloraMeasure.h"

#include <gtest/gtest.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace assets {
	namespace {

		// A 2 m x 2 m crown centred on the trunk, in the template-local millimetre frame.
		const std::vector<geometry::Ring> kSquareCrown{{{-1000, -1000}, {1000, -1000}, {1000, 1000}, {-1000, 1000}}};

		PlacedEntity tree(const std::string& defName, float x, float y, bool tall = true) {
			PlacedEntity e;
			e.defName = defName;
			e.position = {x, y};
			e.isTallOccluder = tall;
			return e;
		}

	} // namespace

	TEST(FloraMeasure, CountsDensityCoverAndWoodWithoutDoubleCountingOverlap) {
		const std::unordered_map<std::string, FloraDefTraits> traits{
			{"Oak", {.rings = &kSquareCrown, .woodPerTree = 25.0}},
			{"Maple", {.rings = &kSquareCrown, .woodPerTree = 10.0}},
		};
		// Two oaks whose crowns overlap by 1 m x 2 m, one maple apart, and a short plant
		// that is not a tree.
		const std::vector<PlacedEntity> world{
			tree("Oak", 5.0F, 5.0F), tree("Oak", 6.0F, 5.0F), tree("Maple", 15.0F, 5.0F), tree("Oak", 12.0F, 5.0F, false)
		};
		std::vector<const PlacedEntity*> entities;
		for (const auto& e : world) {
			entities.push_back(&e);
		}
		const FloraRect rect{0.0F, 0.0F, 20.0F, 10.0F};

		const FloraMeasurement all = measureFlora(rect, {rect}, entities, traits);
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

		// Only the west half loaded: the maple is past it, and the area is honest about it.
		const FloraMeasurement half = measureFlora(rect, {{0.0F, 0.0F, 10.0F, 10.0F}}, entities, traits);
		EXPECT_DOUBLE_EQ(half.rectAreaM2, 200.0);
		EXPECT_DOUBLE_EQ(half.measuredAreaM2, 100.0);
		EXPECT_EQ(half.treeCount, 2U);
		EXPECT_DOUBLE_EQ(half.treesPerThousandM2, 20.0);
		EXPECT_NEAR(half.canopyCover, 6.0 / 100.0, 1e-9);
	}

} // namespace assets
