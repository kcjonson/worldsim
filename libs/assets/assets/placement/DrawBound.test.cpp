#include "assets/placement/DrawBound.h"

#include <gtest/gtest.h>

#include <optional>

namespace assets {

	// within() is the union of the boxes that meet the region, each grown by the margin, cut to the
	// region; meets() is whether there is one. An empty bound holds nothing; everywhere() hands back
	// the region itself.
	TEST(DrawBound, WithinIsTheUnionOfTheBoxesMeetingTheRegionCutToIt) {
		DrawBound bound;
		EXPECT_FALSE(bound.meets({-1000.0F, 1000.0F, -1000.0F, 1000.0F}));

		bound.add({0.0F, 10.0F, 0.0F, 10.0F});
		bound.add({20.0F, 30.0F, 5.0F, 8.0F});
		bound.add({500.0F, 510.0F, 0.0F, 10.0F});

		const std::optional<VisibleBounds> both = bound.within({-5.0F, 25.0F, -5.0F, 50.0F});
		ASSERT_TRUE(both.has_value());
		EXPECT_EQ(both->minX, 0.0F);
		EXPECT_EQ(both->maxX, 25.0F) << "cut to the region";
		EXPECT_EQ(both->minY, 0.0F);
		EXPECT_EQ(both->maxY, 10.0F);

		EXPECT_FALSE(bound.meets({11.0F, 19.0F, 0.0F, 10.0F})) << "between the boxes";
		EXPECT_TRUE(bound.meets({11.0F, 19.0F, 0.0F, 10.0F}, 1.5F)) << "within the margin of one";
		const std::optional<VisibleBounds> grown = bound.within({11.0F, 19.0F, 0.0F, 10.0F}, 1.5F);
		ASSERT_TRUE(grown.has_value());
		EXPECT_EQ(grown->minX, 11.0F);
		EXPECT_EQ(grown->maxX, 19.0F);

		const std::optional<VisibleBounds> whole = DrawBound::everywhere().within({-3.0F, 4.0F, 7.0F, 9.0F}, 2.0F);
		ASSERT_TRUE(whole.has_value());
		EXPECT_EQ(whole->minX, -3.0F);
		EXPECT_EQ(whole->maxX, 4.0F);
		EXPECT_EQ(whole->minY, 7.0F);
		EXPECT_EQ(whole->maxY, 9.0F);
	}

} // namespace assets
