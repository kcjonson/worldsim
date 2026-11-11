#include "world/rendering/OverlapGrid.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace worldview {

	TEST(OverlapGrid, OverlappingFootprintsCollide) {
		OverlapGrid grid;
		grid.beginFrame(400, 300);
		grid.mark(grid.cellsOf({100.0F, 100.0F}, {110.0F, 110.0F}), 3);
		EXPECT_EQ(grid.latest(grid.cellsOf({106.0F, 105.0F}, {120.0F, 115.0F})), 3U);
	}

	// Bounds that overlap by under a pixel (kTolerancePixels a side) may swap.
	TEST(OverlapGrid, FootprintsOverlappingByUnderAPixelDoNotCollide) {
		OverlapGrid grid;
		grid.beginFrame(400, 300);
		grid.mark(grid.cellsOf({100.0F, 100.0F}, {108.4F, 110.0F}), 3);
		EXPECT_EQ(grid.latest(grid.cellsOf({107.6F, 100.0F}, {116.0F, 110.0F})), 0U);
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

	// cellsOf's floor is std::floor's, so a footprint covers the cells it did.
	TEST(OverlapGrid, FloorOfIsStdFloor) {
		for (const float v : {0.0F, -0.0F, 0.25F, -0.25F, 1.0F, -1.0F, 2.5F, -2.5F, 767.999F, -0.999F, 8388607.5F, -8388607.5F,
							  8388608.0F, -16777216.0F, 3.0e9F, -3.0e9F}) {
			EXPECT_EQ(OverlapGrid::floorOf(v), std::floor(v)) << v;
		}
		EXPECT_TRUE(std::isnan(OverlapGrid::floorOf(std::numeric_limits<float>::quiet_NaN())));
		EXPECT_EQ(OverlapGrid::floorOf(std::numeric_limits<float>::infinity()), std::numeric_limits<float>::infinity());
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
