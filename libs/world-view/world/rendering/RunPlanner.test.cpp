#include "world/rendering/RunPlanner.h"

#include <gtest/gtest.h>

#include <vector>

namespace worldview {

	namespace {
		// A 20 px footprint at x (large, so every pair that overlaps is ordered).
		OverlapGrid::Cells at(const RunPlanner& planner, float x) { return planner.cellsOf({x, 100.0F}, {x + 20.0F, 120.0F}); }

		std::vector<uint32_t> closeAll(RunPlanner& planner) {
			const std::span<const uint32_t> order = planner.close();
			return {order.begin(), order.end()};
		}
	} // namespace

	TEST(RunPlanner, EntitiesThatCannotTouchShareTheirMeshsDraw) {
		RunPlanner planner;
		planner.beginFrame(1000, 400, 2);
		EXPECT_EQ(planner.place(0, at(planner, 0.0F), false), 0U);
		EXPECT_EQ(planner.place(1, at(planner, 100.0F), false), 1U);
		EXPECT_EQ(planner.place(0, at(planner, 200.0F), false), 0U);
		EXPECT_EQ(planner.place(1, at(planner, 300.0F), false), 1U);
		EXPECT_EQ(closeAll(planner), (std::vector<uint32_t>{0, 1}));
		EXPECT_EQ(planner.splits(), 0U);
	}

	TEST(RunPlanner, OverlappingEntitiesDrawInDepthOrder) {
		RunPlanner planner;
		planner.beginFrame(1000, 400, 2);
		const uint32_t back = planner.place(0, at(planner, 0.0F), false);
		const uint32_t middle = planner.place(1, at(planner, 10.0F), false);
		const uint32_t front = planner.place(0, at(planner, 20.0F), false);
		EXPECT_NE(back, front);
		EXPECT_EQ(closeAll(planner), (std::vector<uint32_t>{back, middle, front}));
		EXPECT_EQ(planner.splits(), 1U);
	}

	TEST(RunPlanner, OverlappingEntitiesOfOneMeshShareADraw) {
		RunPlanner planner;
		planner.beginFrame(1000, 400, 1);
		EXPECT_EQ(planner.place(0, at(planner, 0.0F), false), 0U);
		EXPECT_EQ(planner.place(0, at(planner, 10.0F), false), 0U);
		EXPECT_EQ(closeAll(planner).size(), 1U);
	}

	// The greedy rule this replaces could only join a mesh's latest draw; an entity that overlaps
	// nothing now goes back to its mesh's first draw however late it sorts.
	TEST(RunPlanner, AnEntityOverlappingNothingJoinsItsMeshsFirstDraw) {
		RunPlanner planner;
		planner.beginFrame(1000, 400, 2);
		const uint32_t first = planner.place(1, at(planner, 500.0F), false);
		(void)planner.place(0, at(planner, 0.0F), false);
		(void)planner.place(1, at(planner, 10.0F), false);
		(void)planner.place(0, at(planner, 20.0F), false);
		EXPECT_EQ(planner.place(1, at(planner, 800.0F), false), first);
	}

	TEST(RunPlanner, SmallEntitiesMayDrawInEitherOrder) {
		RunPlanner planner;
		planner.beginFrame(1000, 400, 2);
		const auto speck = [&](float x) { return planner.cellsOf({x, 100.0F}, {x + 4.0F, 104.0F}); };
		const uint32_t first = planner.place(0, speck(0.0F), true);
		(void)planner.place(1, speck(2.0F), true);
		EXPECT_EQ(planner.place(0, speck(4.0F), true), first);
		EXPECT_EQ(closeAll(planner).size(), 2U);
	}

	TEST(RunPlanner, ASmallEntityStaysInFrontOfALargeOneItOverlaps) {
		RunPlanner planner;
		planner.beginFrame(1000, 400, 2);
		const uint32_t speckBehind = planner.place(0, planner.cellsOf({5.0F, 105.0F}, {9.0F, 109.0F}), true);
		const uint32_t large = planner.place(1, at(planner, 0.0F), false);
		const uint32_t speckFront = planner.place(0, planner.cellsOf({5.0F, 105.0F}, {9.0F, 109.0F}), true);
		EXPECT_NE(speckBehind, speckFront);
		EXPECT_EQ(closeAll(planner), (std::vector<uint32_t>{speckBehind, large, speckFront}));
	}

	TEST(RunPlanner, EverythingPlacedAfterACloseDrawsAfterIt) {
		RunPlanner planner;
		planner.beginFrame(1000, 400, 2);
		EXPECT_EQ(planner.place(0, at(planner, 0.0F), false), 0U);
		EXPECT_EQ(closeAll(planner), (std::vector<uint32_t>{0}));
		EXPECT_EQ(planner.place(0, at(planner, 500.0F), false), 1U);
		EXPECT_EQ(planner.place(1, at(planner, 0.0F), false), 2U);
		EXPECT_EQ(closeAll(planner), (std::vector<uint32_t>{1, 2}));
		EXPECT_EQ(planner.splits(), 0U);
	}

	// At 2^20 meshes a layer is 2^20 keys apart, so keys pass OverlapGrid::kMaxRunKey after about 1,023
	// layers and would wrap at 4,096. Two meshes alternating over one spot open a layer every other
	// entity; past the last layer the draws stay in the order they were opened in.
	TEST(RunPlanner, KeysStopAtTheGridsLargestKey) {
		RunPlanner planner;
		planner.beginFrame(1000, 400, 1U << 20);
		for (uint32_t i = 0; i < 10000; ++i) {
			(void)planner.place(i % 2, at(planner, 0.0F), false);
		}
		const std::vector<uint32_t> order = closeAll(planner);
		ASSERT_GE(order.size(), 2000U);
		ASSERT_LT(order.size(), 4096U);
		for (uint32_t i = 0; i < order.size(); ++i) {
			ASSERT_EQ(order[i], i);
		}
	}

	TEST(RunPlanner, ANewFrameStartsEmpty) {
		RunPlanner planner;
		planner.beginFrame(1000, 400, 2);
		(void)planner.place(1, at(planner, 0.0F), false);
		(void)planner.place(0, at(planner, 10.0F), false);
		(void)planner.close();
		planner.beginFrame(1000, 400, 2);
		EXPECT_EQ(planner.place(0, at(planner, 0.0F), false), 0U);
		EXPECT_EQ(planner.place(1, at(planner, 100.0F), false), 1U);
		EXPECT_EQ(closeAll(planner), (std::vector<uint32_t>{0, 1}));
	}

} // namespace worldview
