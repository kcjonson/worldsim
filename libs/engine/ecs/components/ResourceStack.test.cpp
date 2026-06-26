#include "ResourceStack.h"

#include <gtest/gtest.h>

#include <cstdint>

using namespace ecs;

namespace {

// Min spacing the layout must beat so two piles never alias inside the 0.25 m pickup epsilon.
constexpr float kMinPairDist = 0.3F;

void expectPairwiseDistinct(const std::vector<std::pair<glm::vec2, uint32_t>>& drops) {
	for (size_t i = 0; i < drops.size(); ++i) {
		for (size_t j = i + 1; j < drops.size(); ++j) {
			const float dx = drops[i].first.x - drops[j].first.x;
			const float dy = drops[i].first.y - drops[j].first.y;
			EXPECT_GT(std::sqrt(dx * dx + dy * dy), kMinPairDist) << "piles " << i << " and " << j << " too close";
		}
	}
}

uint32_t sumQty(const std::vector<std::pair<glm::vec2, uint32_t>>& drops) {
	uint32_t total = 0;
	for (const auto& [pos, qty] : drops) {
		total += qty;
	}
	return total;
}

} // namespace

TEST(ResourcePileDropsTests, SplitsOverCapIntoFullCapsPlusRemainder) {
	const auto drops = resourcePileDrops(40, 90, {5.0F, 7.0F});
	ASSERT_EQ(drops.size(), 3U);
	EXPECT_EQ(drops[0].second, 40U);
	EXPECT_EQ(drops[1].second, 40U);
	EXPECT_EQ(drops[2].second, 10U);
	EXPECT_EQ(sumQty(drops), 90U);
	EXPECT_FLOAT_EQ(drops[0].first.x, 5.0F);
	EXPECT_FLOAT_EQ(drops[0].first.y, 7.0F);
	expectPairwiseDistinct(drops);
}

TEST(ResourcePileDropsTests, UnderCapStaysSinglePileAtOrigin) {
	const auto drops = resourcePileDrops(40, 30, {2.0F, -3.0F});
	ASSERT_EQ(drops.size(), 1U);
	EXPECT_EQ(drops[0].second, 30U);
	EXPECT_FLOAT_EQ(drops[0].first.x, 2.0F);
	EXPECT_FLOAT_EQ(drops[0].first.y, -3.0F);
}

TEST(ResourcePileDropsTests, UnboundedCapStaysSinglePileRegardlessOfQty) {
	const auto drops = resourcePileDrops(UINT32_MAX, 100000, {0.0F, 0.0F});
	ASSERT_EQ(drops.size(), 1U);
	EXPECT_EQ(drops[0].second, 100000U);
}

TEST(ResourcePileDropsTests, ZeroCapTreatedAsUnbounded) {
	const auto drops = resourcePileDrops(0, 500, {1.0F, 1.0F});
	ASSERT_EQ(drops.size(), 1U);
	EXPECT_EQ(drops[0].second, 500U);
}

TEST(ResourcePileDropsTests, EvenMultipleOfCapAllFullDistinct) {
	const auto drops = resourcePileDrops(40, 200, {10.0F, 10.0F});
	ASSERT_EQ(drops.size(), 5U);
	for (const auto& [pos, qty] : drops) {
		EXPECT_EQ(qty, 40U);
	}
	EXPECT_EQ(sumQty(drops), 200U);
	expectPairwiseDistinct(drops);
}

TEST(ResourcePileDropsTests, ZeroQuantityYieldsNoPiles) {
	const auto drops = resourcePileDrops(40, 0, {0.0F, 0.0F});
	EXPECT_TRUE(drops.empty());
}

// A larger drop fills past the first ring (1 center + 6 = 7 slots); confirm the layout keeps
// piles distinct as it spills into the second ring rather than aliasing.
TEST(ResourcePileDropsTests, SpillsIntoSecondRingStillDistinct) {
	const auto drops = resourcePileDrops(10, 200, {0.0F, 0.0F}); // 20 piles -> rings 0,1,2
	ASSERT_EQ(drops.size(), 20U);
	EXPECT_EQ(sumQty(drops), 200U);
	expectPairwiseDistinct(drops);
}
