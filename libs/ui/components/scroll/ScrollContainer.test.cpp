#include "ScrollContainer.h"

#include <gtest/gtest.h>

namespace UI {

class ScrollContainerTest : public ::testing::Test {
  protected:
	void SetUp() override {
		// Common setup if needed
	}
};

// === Construction Tests ===

TEST_F(ScrollContainerTest, ConstructsWithDefaults) {
	ScrollContainer scroll(ScrollContainer::Args{});

	EXPECT_FLOAT_EQ(scroll.getScrollPosition(), 0.0F);
	EXPECT_FLOAT_EQ(scroll.getMaxScroll(), 0.0F);
	EXPECT_FLOAT_EQ(scroll.getContentHeight(), 0.0F);
	EXPECT_FLOAT_EQ(scroll.getWidth(), 200.0F);	 // Default size.x
	EXPECT_FLOAT_EQ(scroll.getHeight(), 300.0F); // Default size.y
}

TEST_F(ScrollContainerTest, ConstructsWithCustomSize) {
	ScrollContainer scroll(ScrollContainer::Args{
		.position = {10.0F, 20.0F},
		.size = {150.0F, 200.0F},
	});

	EXPECT_FLOAT_EQ(scroll.getWidth(), 150.0F);
	EXPECT_FLOAT_EQ(scroll.getHeight(), 200.0F);
}

TEST_F(ScrollContainerTest, ConstructsWithMargin) {
	ScrollContainer scroll(ScrollContainer::Args{
		.size = {100.0F, 100.0F},
		.margin = 10.0F,
	});

	// getWidth/getHeight include margin on both sides
	EXPECT_FLOAT_EQ(scroll.getWidth(), 120.0F);	 // 100 + 10*2
	EXPECT_FLOAT_EQ(scroll.getHeight(), 120.0F); // 100 + 10*2
}

// === Scroll Bounds Tests ===

TEST_F(ScrollContainerTest, MaxScrollZeroWhenContentFits) {
	ScrollContainer scroll(ScrollContainer::Args{
		.size = {200.0F, 300.0F},
	});

	// Content smaller than viewport
	scroll.setContentHeight(100.0F);

	EXPECT_FLOAT_EQ(scroll.getMaxScroll(), 0.0F);
	EXPECT_FLOAT_EQ(scroll.getContentHeight(), 100.0F);
}

TEST_F(ScrollContainerTest, MaxScrollCalculatedWhenContentOverflows) {
	ScrollContainer scroll(ScrollContainer::Args{
		.size = {200.0F, 300.0F},
	});

	// Content taller than viewport
	scroll.setContentHeight(500.0F);

	// maxScroll = contentHeight - viewportHeight = 500 - 300 = 200
	EXPECT_FLOAT_EQ(scroll.getMaxScroll(), 200.0F);
	EXPECT_FLOAT_EQ(scroll.getContentHeight(), 500.0F);
}

// === Scroll Position Tests ===

TEST_F(ScrollContainerTest, ScrollToSetsPosition) {
	ScrollContainer scroll(ScrollContainer::Args{
		.size = {200.0F, 300.0F},
	});
	scroll.setContentHeight(500.0F);

	scroll.scrollTo(100.0F);
	EXPECT_FLOAT_EQ(scroll.getScrollPosition(), 100.0F);
}

TEST_F(ScrollContainerTest, ScrollToClampsToBounds) {
	ScrollContainer scroll(ScrollContainer::Args{
		.size = {200.0F, 300.0F},
	});
	scroll.setContentHeight(500.0F); // maxScroll = 200

	// Try to scroll past max
	scroll.scrollTo(300.0F);
	EXPECT_FLOAT_EQ(scroll.getScrollPosition(), 200.0F);

	// Try to scroll negative
	scroll.scrollTo(-50.0F);
	EXPECT_FLOAT_EQ(scroll.getScrollPosition(), 0.0F);
}

TEST_F(ScrollContainerTest, ScrollByDelta) {
	ScrollContainer scroll(ScrollContainer::Args{
		.size = {200.0F, 300.0F},
	});
	scroll.setContentHeight(500.0F);

	scroll.scrollTo(50.0F);
	scroll.scrollBy(25.0F);
	EXPECT_FLOAT_EQ(scroll.getScrollPosition(), 75.0F);

	scroll.scrollBy(-30.0F);
	EXPECT_FLOAT_EQ(scroll.getScrollPosition(), 45.0F);
}

TEST_F(ScrollContainerTest, ScrollToTop) {
	ScrollContainer scroll(ScrollContainer::Args{
		.size = {200.0F, 300.0F},
	});
	scroll.setContentHeight(500.0F);
	scroll.scrollTo(150.0F);

	scroll.scrollToTop();
	EXPECT_FLOAT_EQ(scroll.getScrollPosition(), 0.0F);
}

TEST_F(ScrollContainerTest, ScrollToBottom) {
	ScrollContainer scroll(ScrollContainer::Args{
		.size = {200.0F, 300.0F},
	});
	scroll.setContentHeight(500.0F);

	scroll.scrollToBottom();
	EXPECT_FLOAT_EQ(scroll.getScrollPosition(), 200.0F); // maxScroll
}

// === Position Tests ===

TEST_F(ScrollContainerTest, SetPositionUpdatesBase) {
	ScrollContainer scroll(ScrollContainer::Args{
		.position = {10.0F, 20.0F},
		.size = {200.0F, 300.0F},
	});

	scroll.setPosition(50.0F, 60.0F);

	Foundation::Vec2 contentPos = scroll.getContentPosition();
	EXPECT_FLOAT_EQ(contentPos.x, 50.0F);
	EXPECT_FLOAT_EQ(contentPos.y, 60.0F);
}

TEST_F(ScrollContainerTest, SetPositionWithMargin) {
	ScrollContainer scroll(ScrollContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {200.0F, 300.0F},
		.margin = 10.0F,
	});

	scroll.setPosition(100.0F, 200.0F);

	// Content position should account for margin
	Foundation::Vec2 contentPos = scroll.getContentPosition();
	EXPECT_FLOAT_EQ(contentPos.x, 110.0F); // 100 + 10 margin
	EXPECT_FLOAT_EQ(contentPos.y, 210.0F); // 200 + 10 margin
}

// === Viewport Size Tests ===

TEST_F(ScrollContainerTest, SetViewportSizeUpdatesSize) {
	ScrollContainer scroll(ScrollContainer::Args{
		.size = {200.0F, 300.0F},
	});
	scroll.setContentHeight(500.0F);

	// Change viewport size
	scroll.setViewportSize({150.0F, 250.0F});

	EXPECT_FLOAT_EQ(scroll.getWidth(), 150.0F);
	EXPECT_FLOAT_EQ(scroll.getHeight(), 250.0F);
	// maxScroll should update: 500 - 250 = 250
	EXPECT_FLOAT_EQ(scroll.getMaxScroll(), 250.0F);
}

TEST_F(ScrollContainerTest, SetViewportSizeClampsScroll) {
	ScrollContainer scroll(ScrollContainer::Args{
		.size = {200.0F, 300.0F},
	});
	scroll.setContentHeight(500.0F);
	scroll.scrollTo(200.0F); // At max scroll

	// Increase viewport size, reducing maxScroll
	scroll.setViewportSize({200.0F, 400.0F});

	// New maxScroll = 500 - 400 = 100
	// Scroll position should clamp to new max
	EXPECT_FLOAT_EQ(scroll.getMaxScroll(), 100.0F);
	EXPECT_FLOAT_EQ(scroll.getScrollPosition(), 100.0F);
}

// === Contains Point Tests ===

TEST_F(ScrollContainerTest, ContainsPointInViewport) {
	ScrollContainer scroll(ScrollContainer::Args{
		.position = {50.0F, 50.0F},
		.size = {200.0F, 300.0F},
	});

	// Inside viewport
	EXPECT_TRUE(scroll.containsPoint({100.0F, 150.0F}));
	EXPECT_TRUE(scroll.containsPoint({50.0F, 50.0F}));	 // Top-left corner
	EXPECT_TRUE(scroll.containsPoint({249.0F, 349.0F})); // Near bottom-right

	// Outside viewport
	EXPECT_FALSE(scroll.containsPoint({49.0F, 150.0F}));  // Left of viewport
	EXPECT_FALSE(scroll.containsPoint({251.0F, 150.0F})); // Right of viewport
	EXPECT_FALSE(scroll.containsPoint({100.0F, 49.0F}));  // Above viewport
	EXPECT_FALSE(scroll.containsPoint({100.0F, 351.0F})); // Below viewport
}

} // namespace UI
