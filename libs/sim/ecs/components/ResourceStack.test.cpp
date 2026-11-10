#include "ResourceStack.h"

#include <gtest/gtest.h>

#include <cstdint>

using namespace ecs;

namespace {

// A Wood-sized footprint: Wood draws ~1.07 m wide, so a full pile's radius is ~0.6 m.
constexpr float kWoodRadius = 0.6F;

float dist(glm::vec2 a, glm::vec2 b) {
	const float dx = a.x - b.x;
	const float dy = a.y - b.y;
	return std::sqrt(dx * dx + dy * dy);
}

// Every pair of new piles keeps both footprints plus the gap apart (never overlapping).
void expectSpawnsClear(const PileDropPlan& plan, float radius) {
	for (size_t i = 0; i < plan.spawns.size(); ++i) {
		for (size_t j = i + 1; j < plan.spawns.size(); ++j) {
			EXPECT_GE(dist(plan.spawns[i].first, plan.spawns[j].first), 2.0F * radius + kGroundClearanceGap - 1e-3F)
				<< "piles " << i << " and " << j << " overlap";
		}
	}
}

uint32_t sumQty(const PileDropPlan& plan) {
	uint32_t total = 0;
	for (const auto& [pos, qty] : plan.spawns) {
		total += qty;
	}
	for (const auto& [index, qty] : plan.topUps) {
		total += qty;
	}
	return total;
}

} // namespace

TEST(ResourcePileDropsTests, SplitsOverCapIntoFullCapsPlusRemainder) {
	const auto plan = resourcePileDrops(40, 90, {5.0F, 7.0F}, kWoodRadius, {});
	ASSERT_EQ(plan.spawns.size(), 3U);
	EXPECT_TRUE(plan.topUps.empty());
	EXPECT_EQ(plan.spawns[0].second, 40U);
	EXPECT_EQ(plan.spawns[1].second, 40U);
	EXPECT_EQ(plan.spawns[2].second, 10U);
	EXPECT_EQ(sumQty(plan), 90U);
	EXPECT_FLOAT_EQ(plan.spawns[0].first.x, 5.0F);
	EXPECT_FLOAT_EQ(plan.spawns[0].first.y, 7.0F);
	expectSpawnsClear(plan, kWoodRadius);
}

TEST(ResourcePileDropsTests, UnderCapStaysSinglePileAtOrigin) {
	const auto plan = resourcePileDrops(40, 30, {2.0F, -3.0F}, kWoodRadius, {});
	ASSERT_EQ(plan.spawns.size(), 1U);
	EXPECT_EQ(plan.spawns[0].second, 30U);
	EXPECT_FLOAT_EQ(plan.spawns[0].first.x, 2.0F);
	EXPECT_FLOAT_EQ(plan.spawns[0].first.y, -3.0F);
}

TEST(ResourcePileDropsTests, UnboundedCapStaysSinglePileRegardlessOfQty) {
	const auto plan = resourcePileDrops(UINT32_MAX, 100000, {0.0F, 0.0F}, kWoodRadius, {});
	ASSERT_EQ(plan.spawns.size(), 1U);
	EXPECT_EQ(plan.spawns[0].second, 100000U);
}

TEST(ResourcePileDropsTests, ZeroCapTreatedAsUnbounded) {
	const auto plan = resourcePileDrops(0, 500, {1.0F, 1.0F}, kWoodRadius, {});
	ASSERT_EQ(plan.spawns.size(), 1U);
	EXPECT_EQ(plan.spawns[0].second, 500U);
}

TEST(ResourcePileDropsTests, ZeroQuantityYieldsNoPiles) {
	const auto plan = resourcePileDrops(40, 0, {0.0F, 0.0F}, kWoodRadius, {});
	EXPECT_TRUE(plan.spawns.empty());
	EXPECT_TRUE(plan.topUps.empty());
}

// Many overflow piles of a wide item spill past the first ring and still never overlap.
TEST(ResourcePileDropsTests, OverflowPilesOfWideItemNeverOverlap) {
	const auto plan = resourcePileDrops(10, 200, {0.0F, 0.0F}, kWoodRadius, {}); // 20 piles -> rings 0,1,2
	ASSERT_EQ(plan.spawns.size(), 20U);
	EXPECT_EQ(sumQty(plan), 200U);
	expectSpawnsClear(plan, kWoodRadius);
}

// Repeated drops at one spot grow the pile already there instead of stacking a second sprite on it.
TEST(ResourcePileDropsTests, TopsUpSameDefPileAtOrigin) {
	const std::vector<GroundOccupant> nearby{{{3.0F, 4.0F}, kWoodRadius, 5, true}};
	const auto						  plan = resourcePileDrops(40, 10, {3.0F, 4.0F}, kWoodRadius, nearby);
	EXPECT_TRUE(plan.spawns.empty());
	ASSERT_EQ(plan.topUps.size(), 1U);
	EXPECT_EQ(plan.topUps[0].first, 0U);
	EXPECT_EQ(plan.topUps[0].second, 10U);
}

// Once the pile at the spot is full, the overflow starts a new pile clear of it.
TEST(ResourcePileDropsTests, FillsPileToCapThenSpawnsBesideIt) {
	const std::vector<GroundOccupant> nearby{{{0.0F, 0.0F}, kWoodRadius, 35, true}};
	const auto						  plan = resourcePileDrops(40, 12, {0.0F, 0.0F}, kWoodRadius, nearby);
	ASSERT_EQ(plan.topUps.size(), 1U);
	EXPECT_EQ(plan.topUps[0].second, 5U);
	ASSERT_EQ(plan.spawns.size(), 1U);
	EXPECT_EQ(plan.spawns[0].second, 7U);
	EXPECT_GE(dist(plan.spawns[0].first, {0.0F, 0.0F}), 2.0F * kWoodRadius + kGroundClearanceGap - 1e-3F);
}

// A different item lying at the drop spot pushes the new pile clear by both footprints.
TEST(ResourcePileDropsTests, DifferentItemAtOriginPushesNewPileClear) {
	constexpr float					  kStoneRadius = 0.25F;
	const std::vector<GroundOccupant> nearby{{{0.0F, 0.0F}, kStoneRadius, 3, false}};
	const auto						  plan = resourcePileDrops(40, 20, {0.0F, 0.0F}, kWoodRadius, nearby);
	EXPECT_TRUE(plan.topUps.empty()) << "a different item never takes units";
	ASSERT_EQ(plan.spawns.size(), 1U);
	EXPECT_EQ(plan.spawns[0].second, 20U);
	EXPECT_GE(dist(plan.spawns[0].first, {0.0F, 0.0F}), kWoodRadius + kStoneRadius + kGroundClearanceGap - 1e-3F);
}

// A packaged crate's footprint (no quantity, not the item) is avoided like any other occupant.
TEST(ResourcePileDropsTests, AvoidsPackagedCrateFootprint) {
	constexpr float					  kCrateRadius = 0.63F;
	const glm::vec2					  crate{0.3F, -0.2F};
	const std::vector<GroundOccupant> nearby{{crate, kCrateRadius, 0, false}};
	const auto						  plan = resourcePileDrops(1, 3, {0.0F, 0.0F}, 0.3F, nearby);
	ASSERT_EQ(plan.spawns.size(), 3U) << "unstackable: one pile per unit";
	for (const auto& [pos, qty] : plan.spawns) {
		EXPECT_GE(dist(pos, crate), 0.3F + kCrateRadius + kGroundClearanceGap - 1e-3F);
	}
	expectSpawnsClear(plan, 0.3F);
}

// A spot off walkable ground is skipped while the walkable search lasts.
TEST(ResourcePileDropsTests, SkipsSpotsOffGround) {
	const auto plan = resourcePileDrops(40, 10, {0.0F, 0.0F}, kWoodRadius, {}, [](glm::vec2 p) { return p.x > 0.5F; });
	ASSERT_EQ(plan.spawns.size(), 1U);
	EXPECT_GT(plan.spawns[0].first.x, 0.5F);
}
