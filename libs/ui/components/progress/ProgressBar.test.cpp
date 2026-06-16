#include "ProgressBar.h"

#include <gtest/gtest.h>

namespace UI {

class ProgressBarTest : public ::testing::Test {
  protected:
	void SetUp() override {
		// Common setup if needed
	}
};

// === Construction Tests ===

TEST_F(ProgressBarTest, ConstructsWithDefaults) {
	ProgressBar bar(ProgressBar::Args{});

	EXPECT_FLOAT_EQ(bar.getValue(), 1.0F);
	EXPECT_FLOAT_EQ(bar.size.x, 200.0F); // Default width
}

TEST_F(ProgressBarTest, ConstructsWithCustomWidth) {
	ProgressBar bar(ProgressBar::Args{
		.width = 200.0F,
		.value = 0.5F,
	});

	EXPECT_FLOAT_EQ(bar.getValue(), 0.5F);
	EXPECT_FLOAT_EQ(bar.size.x, 200.0F);
}

TEST_F(ProgressBarTest, ConstructsWithLabel) {
	ProgressBar bar(ProgressBar::Args{
		.width = 150.0F,
		.label = "Test",
		.valueText = "50%",
	});

	// Width is the track width; a header row adds height, not width.
	EXPECT_FLOAT_EQ(bar.size.x, 150.0F);
}

TEST_F(ProgressBarTest, ConstructsWithMargin) {
	ProgressBar bar(ProgressBar::Args{
		.width = 100.0F,
		.margin = 5.0F,
	});

	// size is the bar's own footprint; margin is separate (folded into content position).
	EXPECT_FLOAT_EQ(bar.size.x, 100.0F);
}

// === Value Tests ===

TEST_F(ProgressBarTest, SetValueClampsToRange) {
	ProgressBar bar(ProgressBar::Args{});

	bar.setValue(1.5F);
	EXPECT_FLOAT_EQ(bar.getValue(), 1.0F);

	bar.setValue(-0.5F);
	EXPECT_FLOAT_EQ(bar.getValue(), 0.0F);

	bar.setValue(0.75F);
	EXPECT_FLOAT_EQ(bar.getValue(), 0.75F);
}

TEST_F(ProgressBarTest, ConstructorClampsValue) {
	ProgressBar barHigh(ProgressBar::Args{.value = 2.0F});
	EXPECT_FLOAT_EQ(barHigh.getValue(), 1.0F);

	ProgressBar barLow(ProgressBar::Args{.value = -1.0F});
	EXPECT_FLOAT_EQ(barLow.getValue(), 0.0F);
}

TEST_F(ProgressBarTest, SetValueZero) {
	ProgressBar bar(ProgressBar::Args{.value = 1.0F});

	bar.setValue(0.0F);
	EXPECT_FLOAT_EQ(bar.getValue(), 0.0F);
}

// === Position Tests ===

TEST_F(ProgressBarTest, SetPositionUpdatesBase) {
	ProgressBar bar(ProgressBar::Args{
		.position = {10.0F, 20.0F},
	});

	bar.setPosition({50.0F, 60.0F});

	// Note: We can't easily check internal child positions without render,
	// but we can verify the base position is updated
	Foundation::Vec2 contentPos = bar.getContentPosition();
	EXPECT_FLOAT_EQ(contentPos.x, 50.0F);
	EXPECT_FLOAT_EQ(contentPos.y, 60.0F);
}

TEST_F(ProgressBarTest, SetPositionWithMargin) {
	ProgressBar bar(ProgressBar::Args{
		.position = {0.0F, 0.0F},
		.margin = 10.0F,
	});

	bar.setPosition({100.0F, 200.0F});

	// Content position should account for margin
	Foundation::Vec2 contentPos = bar.getContentPosition();
	EXPECT_FLOAT_EQ(contentPos.x, 110.0F);	// 100 + 10 margin
	EXPECT_FLOAT_EQ(contentPos.y, 210.0F);	// 200 + 10 margin
}

// === Integration Tests ===

TEST_F(ProgressBarTest, FullWidthBarNoLabel) {
	ProgressBar bar(ProgressBar::Args{
		.width = 80.0F,
		.value = 0.5F,
		// No label - bar should take full width
	});

	EXPECT_FLOAT_EQ(bar.size.x, 80.0F);
	EXPECT_FLOAT_EQ(bar.getValue(), 0.5F);
}

} // namespace UI
