#include "StorageConfiguration.h"

#include <gtest/gtest.h>

namespace ecs {

TEST(StorageConfigurationTests, SetRuleReplacesTheRuleForTheSameItem) {
	StorageConfiguration config;
	config.addRule(StorageRule{.defName = "Stone", .category = assets::ItemCategory::RawMaterial, .minAmount = 3});
	EXPECT_EQ(config.setRule(StorageRule{.defName = "Wood", .category = assets::ItemCategory::RawMaterial, .minAmount = 200}), 0U);
	EXPECT_EQ(config.setRule(StorageRule{.defName = "Wood", .category = assets::ItemCategory::RawMaterial, .minAmount = 800}), 1U);

	ASSERT_EQ(config.getRuleCount(), 2U);
	EXPECT_EQ(config.getMinAmountFor("Wood", assets::ItemCategory::RawMaterial), 800U);
	EXPECT_EQ(config.getMinAmountFor("Stone", assets::ItemCategory::RawMaterial), 3U);
}

} // namespace ecs
