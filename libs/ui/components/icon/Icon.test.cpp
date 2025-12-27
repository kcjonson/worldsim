#include "Icon.h"

#include <gtest/gtest.h>

namespace UI {

class IconTest : public ::testing::Test {
  protected:
	void SetUp() override {
		// Common setup if needed
	}
};

// === Construction Tests ===

TEST_F(IconTest, ConstructsWithDefaults) {
	Icon icon(Icon::Args{});

	EXPECT_FLOAT_EQ(icon.getIconSize(), Theme::Icons::defaultSize);
	EXPECT_FLOAT_EQ(icon.getWidth(), Theme::Icons::defaultSize);	// Size.x
	EXPECT_FLOAT_EQ(icon.getHeight(), Theme::Icons::defaultSize); // Size.y (square)
	EXPECT_TRUE(icon.getSvgPath().empty());
	EXPECT_FALSE(icon.isLoaded()); // No SVG loaded
}

TEST_F(IconTest, ConstructsWithCustomSize) {
	Icon icon(Icon::Args{
		.size = 32.0F,
	});

	EXPECT_FLOAT_EQ(icon.getIconSize(), 32.0F);
	EXPECT_FLOAT_EQ(icon.getWidth(), 32.0F);
	EXPECT_FLOAT_EQ(icon.getHeight(), 32.0F);
}

TEST_F(IconTest, ConstructsWithMargin) {
	Icon icon(Icon::Args{
		.size = 16.0F,
		.margin = 4.0F,
	});

	// getWidth/getHeight include margin on both sides
	EXPECT_FLOAT_EQ(icon.getWidth(), 24.0F);  // 16 + 4*2
	EXPECT_FLOAT_EQ(icon.getHeight(), 24.0F); // 16 + 4*2
}

TEST_F(IconTest, ConstructsWithTint) {
	Foundation::Color customTint{1.0F, 0.5F, 0.0F, 1.0F};
	Icon			  icon(Icon::Args{
		 .tint = customTint,
	 });

	Foundation::Color tint = icon.getTint();
	EXPECT_FLOAT_EQ(tint.r, 1.0F);
	EXPECT_FLOAT_EQ(tint.g, 0.5F);
	EXPECT_FLOAT_EQ(tint.b, 0.0F);
	EXPECT_FLOAT_EQ(tint.a, 1.0F);
}

// === Setter Tests ===

TEST_F(IconTest, SetTintChangesColor) {
	Icon icon(Icon::Args{});

	Foundation::Color newTint{0.0F, 1.0F, 0.0F, 0.8F};
	icon.setTint(newTint);

	Foundation::Color tint = icon.getTint();
	EXPECT_FLOAT_EQ(tint.r, 0.0F);
	EXPECT_FLOAT_EQ(tint.g, 1.0F);
	EXPECT_FLOAT_EQ(tint.b, 0.0F);
	EXPECT_FLOAT_EQ(tint.a, 0.8F);
}

TEST_F(IconTest, SetIconSizeUpdatesSize) {
	Icon icon(Icon::Args{
		.size = 16.0F,
	});

	icon.setIconSize(24.0F);

	EXPECT_FLOAT_EQ(icon.getIconSize(), 24.0F);
	EXPECT_FLOAT_EQ(icon.getWidth(), 24.0F);
	EXPECT_FLOAT_EQ(icon.getHeight(), 24.0F);
}

TEST_F(IconTest, SetIconSizeWithMargin) {
	Icon icon(Icon::Args{
		.size = 16.0F,
		.margin = 5.0F,
	});

	icon.setIconSize(32.0F);

	// getWidth/getHeight should include margin
	EXPECT_FLOAT_EQ(icon.getWidth(), 42.0F);  // 32 + 5*2
	EXPECT_FLOAT_EQ(icon.getHeight(), 42.0F); // 32 + 5*2
}

// === Position Tests ===

TEST_F(IconTest, SetPositionUpdatesBase) {
	Icon icon(Icon::Args{
		.position = {10.0F, 20.0F},
	});

	icon.setPosition(50.0F, 60.0F);

	Foundation::Vec2 contentPos = icon.getContentPosition();
	EXPECT_FLOAT_EQ(contentPos.x, 50.0F);
	EXPECT_FLOAT_EQ(contentPos.y, 60.0F);
}

TEST_F(IconTest, SetPositionWithMargin) {
	Icon icon(Icon::Args{
		.position = {0.0F, 0.0F},
		.margin = 8.0F,
	});

	icon.setPosition(100.0F, 200.0F);

	// Content position should account for margin
	Foundation::Vec2 contentPos = icon.getContentPosition();
	EXPECT_FLOAT_EQ(contentPos.x, 108.0F); // 100 + 8 margin
	EXPECT_FLOAT_EQ(contentPos.y, 208.0F); // 200 + 8 margin
}

// === SVG Path Tests ===

TEST_F(IconTest, SetSvgPathUpdatesPath) {
	Icon icon(Icon::Args{});

	icon.setSvgPath("/path/to/icon.svg");

	EXPECT_EQ(icon.getSvgPath(), "/path/to/icon.svg");
}

TEST_F(IconTest, EmptySvgPathNotLoaded) {
	Icon icon(Icon::Args{
		.svgPath = "",
	});

	EXPECT_FALSE(icon.isLoaded());
}

} // namespace UI
