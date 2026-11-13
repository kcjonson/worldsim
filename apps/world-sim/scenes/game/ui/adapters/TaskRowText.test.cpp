#include "TaskRowText.h"

#include <gtest/gtest.h>

#include <vector>

namespace world_sim::adapters {

	TEST(TaskRowText, NamesTwoThenCountsTheRest) {
		const std::vector<std::string> names{"Alice", "Bob", "Cara", "Dev"};
		EXPECT_EQ(joinNames(std::span(names).first(0)), "");
		EXPECT_EQ(joinNames(std::span(names).first(1)), "Alice");
		EXPECT_EQ(joinNames(std::span(names).first(2)), "Alice, Bob");
		EXPECT_EQ(joinNames(std::span(names).first(3)), "Alice, Bob +1");
		EXPECT_EQ(joinNames(names), "Alice, Bob +2");
	}

	TEST(TaskRowText, QuantityLeavesOutAZeroPart) {
		EXPECT_EQ(quantityText(12, 4), "needs 12, 4 on the way");
		EXPECT_EQ(quantityText(12, 0), "needs 12");
		EXPECT_EQ(quantityText(0, 4), "4 on the way");
		EXPECT_EQ(quantityText(0, 0), "");
	}

} // namespace world_sim::adapters
