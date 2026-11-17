#include "StorageConfiguration.h"

#include <gtest/gtest.h>

namespace ecs::test {

TEST(StorageConfigurationTests, SetRuleReplacesTheRuleForTheSameItem) {
	StorageConfiguration config;
	config.addRule(StorageRule{.defName = "Stone", .category = assets::ItemCategory::RawMaterial, .minAmount = 3});
	EXPECT_EQ(config.setRule(StorageRule{.defName = "Wood", .category = assets::ItemCategory::RawMaterial, .minAmount = 200}), 0U);
	EXPECT_EQ(config.setRule(StorageRule{.defName = "Wood", .category = assets::ItemCategory::RawMaterial, .minAmount = 800}), 1U);

	ASSERT_EQ(config.getRuleCount(), 2U);
	EXPECT_EQ(config.getMinAmountFor("Wood", assets::ItemCategory::RawMaterial), 800U);
	EXPECT_EQ(config.getMinAmountFor("Stone", assets::ItemCategory::RawMaterial), 3U);
}

TEST(StorageConfigurationTests, SetRuleReplacesAWildcardOnlyInItsOwnCategory) {
	StorageConfiguration config;
	config.addRule(StorageRule{.defName = "*", .category = assets::ItemCategory::RawMaterial});
	config.addRule(StorageRule{.defName = "*", .category = assets::ItemCategory::RawMaterial, .minAmount = 4});
	config.addRule(StorageRule{.defName = "*", .category = assets::ItemCategory::Food});

	EXPECT_EQ(config.setRule(StorageRule{.defName = "*", .category = assets::ItemCategory::RawMaterial, .minAmount = 9}), 2U);
	ASSERT_EQ(config.getRuleCount(), 2U);
	EXPECT_EQ(config.getMinAmountFor("Wood", assets::ItemCategory::RawMaterial), 9U);
	EXPECT_TRUE(config.acceptsItem("Berry", assets::ItemCategory::Food));
}

} // namespace ecs::test
