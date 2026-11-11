#include "world/rendering/OverlapGrid.h"

#include <gtest/gtest.h>

namespace worldview {

	TEST(OverlapGrid, FootprintsThatShareAPixelCollide) {
		OverlapGrid grid;
		grid.beginFrame(400, 300);
		grid.mark(grid.cellsOf({100.0F, 100.0F}, {110.0F, 110.0F}), 3);
		EXPECT_EQ(grid.latest(grid.cellsOf({110.0F, 105.0F}, {120.0F, 115.0F})), 3U);
	}

	// Far out a tree is a couple of pixels across; two of them a few cells apart must not
	// collide, however large the view is in world space.
	TEST(OverlapGrid, SmallFootprintsAFewPixelsApartDoNotCollide) {
		OverlapGrid grid;
		grid.beginFrame(3072, 1728);
		grid.mark(grid.cellsOf({100.0F, 100.0F}, {102.0F, 102.0F}), 5);
		EXPECT_EQ(grid.latest(grid.cellsOf({112.0F, 100.0F}, {114.0F, 102.0F})), 0U);
	}

	TEST(OverlapGrid, MarkNeverLowersALaterRun) {
		OverlapGrid grid;
		grid.beginFrame(64, 64);
		const OverlapGrid::Cells cells = grid.cellsOf({10.0F, 10.0F}, {12.0F, 12.0F});
		grid.mark(cells, 7);
		grid.mark(cells, 4);
		EXPECT_EQ(grid.latest(cells), 7U);
	}

	TEST(OverlapGrid, AFootprintOffTheScreenCoversNothing) {
		OverlapGrid grid;
		grid.beginFrame(100, 100);
		const OverlapGrid::Cells off = grid.cellsOf({-50.0F, 20.0F}, {-10.0F, 30.0F});
		EXPECT_GT(off.x0, off.x1);
		grid.mark(off, 2);
		EXPECT_EQ(grid.latest(grid.cellsOf({0.0F, 0.0F}, {99.0F, 99.0F})), 0U);
	}

	TEST(OverlapGrid, ANewFrameStartsEmpty) {
		OverlapGrid grid;
		grid.beginFrame(100, 100);
		const OverlapGrid::Cells cells = grid.cellsOf({10.0F, 10.0F}, {20.0F, 20.0F});
		grid.mark(cells, 9);
		grid.beginFrame(100, 100);
		EXPECT_EQ(grid.latest(cells), 0U);
		grid.mark(cells, 1);
		EXPECT_EQ(grid.latest(cells), 1U);
	}

} // namespace worldview
