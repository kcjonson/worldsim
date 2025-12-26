#include "layout/LayoutContainer.h"
#include "layout/LayoutTypes.h"
#include "shapes/Shapes.h"
#include <gtest/gtest.h>

using namespace UI;
using namespace Foundation;

// ============================================================================
// Mock component for testing layout behavior
// ============================================================================

class MockComponent : public Component {
  public:
	MockComponent(float width, float height, float componentMargin = 0.0F) {
		size = {width, height};
		margin = componentMargin;
	}

	void render() override {}
};

// ============================================================================
// LayoutContainer Construction Tests
// ============================================================================

TEST(LayoutContainerTest, ConstructsWithDefaultValues) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {100.0F, 200.0F},
		.size = {300.0F, 400.0F}});

	EXPECT_FLOAT_EQ(layout.getWidth(), 300.0F);
	EXPECT_FLOAT_EQ(layout.getHeight(), 400.0F);
}

TEST(LayoutContainerTest, ConstructsWithMargin) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {100.0F, 100.0F},
		.margin = 10.0F});

	// Width/height should include margin
	EXPECT_FLOAT_EQ(layout.getWidth(), 120.0F);
	EXPECT_FLOAT_EQ(layout.getHeight(), 120.0F);
}

// ============================================================================
// Vertical Layout Tests
// ============================================================================

TEST(LayoutContainerTest, VerticalLayoutPositionsChildrenTopToBottom) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {200.0F, 300.0F},
		.direction = Direction::Vertical});

	auto handle1 = layout.addChild(MockComponent(50.0F, 30.0F));
	auto handle2 = layout.addChild(MockComponent(50.0F, 40.0F));
	auto handle3 = layout.addChild(MockComponent(50.0F, 25.0F));

	// Trigger layout computation
	layout.render();

	auto* child1 = layout.getChild<MockComponent>(handle1);
	auto* child2 = layout.getChild<MockComponent>(handle2);
	auto* child3 = layout.getChild<MockComponent>(handle3);

	ASSERT_NE(child1, nullptr);
	ASSERT_NE(child2, nullptr);
	ASSERT_NE(child3, nullptr);

	// Children should be stacked vertically
	EXPECT_FLOAT_EQ(child1->position.y, 0.0F);
	EXPECT_FLOAT_EQ(child2->position.y, 30.0F);   // 0 + 30 (child1 height)
	EXPECT_FLOAT_EQ(child3->position.y, 70.0F);   // 0 + 30 + 40 (child1 + child2)
}

TEST(LayoutContainerTest, VerticalLayoutWithMarginIncludesMarginInSpacing) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {200.0F, 300.0F},
		.direction = Direction::Vertical});

	// Children with 5px margin each
	auto handle1 = layout.addChild(MockComponent(50.0F, 30.0F, 5.0F));
	auto handle2 = layout.addChild(MockComponent(50.0F, 40.0F, 5.0F));

	layout.render();

	auto* child1 = layout.getChild<MockComponent>(handle1);
	auto* child2 = layout.getChild<MockComponent>(handle2);

	ASSERT_NE(child1, nullptr);
	ASSERT_NE(child2, nullptr);

	// Child 1 at y=0, child 2 at y = child1.getHeight() (30 + 10 margin)
	EXPECT_FLOAT_EQ(child1->position.y, 0.0F);
	EXPECT_FLOAT_EQ(child2->position.y, 40.0F);  // 30 + 5 + 5 (content + margin*2)
}

TEST(LayoutContainerTest, VerticalLayoutLeftAlign) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {200.0F, 300.0F},
		.direction = Direction::Vertical,
		.hAlign = HAlign::Left});

	auto handle = layout.addChild(MockComponent(50.0F, 30.0F));
	layout.render();

	auto* child = layout.getChild<MockComponent>(handle);
	ASSERT_NE(child, nullptr);

	// Left-aligned: x = 0
	EXPECT_FLOAT_EQ(child->position.x, 0.0F);
}

TEST(LayoutContainerTest, VerticalLayoutCenterAlign) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {200.0F, 300.0F},
		.direction = Direction::Vertical,
		.hAlign = HAlign::Center});

	auto handle = layout.addChild(MockComponent(50.0F, 30.0F));
	layout.render();

	auto* child = layout.getChild<MockComponent>(handle);
	ASSERT_NE(child, nullptr);

	// Centered: x = (200 - 50) / 2 = 75
	EXPECT_FLOAT_EQ(child->position.x, 75.0F);
}

TEST(LayoutContainerTest, VerticalLayoutRightAlign) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {200.0F, 300.0F},
		.direction = Direction::Vertical,
		.hAlign = HAlign::Right});

	auto handle = layout.addChild(MockComponent(50.0F, 30.0F));
	layout.render();

	auto* child = layout.getChild<MockComponent>(handle);
	ASSERT_NE(child, nullptr);

	// Right-aligned: x = 200 - 50 = 150
	EXPECT_FLOAT_EQ(child->position.x, 150.0F);
}

// ============================================================================
// Horizontal Layout Tests
// ============================================================================

TEST(LayoutContainerTest, HorizontalLayoutPositionsChildrenLeftToRight) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {300.0F, 100.0F},
		.direction = Direction::Horizontal});

	auto handle1 = layout.addChild(MockComponent(50.0F, 30.0F));
	auto handle2 = layout.addChild(MockComponent(60.0F, 30.0F));
	auto handle3 = layout.addChild(MockComponent(40.0F, 30.0F));

	layout.render();

	auto* child1 = layout.getChild<MockComponent>(handle1);
	auto* child2 = layout.getChild<MockComponent>(handle2);
	auto* child3 = layout.getChild<MockComponent>(handle3);

	ASSERT_NE(child1, nullptr);
	ASSERT_NE(child2, nullptr);
	ASSERT_NE(child3, nullptr);

	// Children should be stacked horizontally
	EXPECT_FLOAT_EQ(child1->position.x, 0.0F);
	EXPECT_FLOAT_EQ(child2->position.x, 50.0F);   // 0 + 50 (child1 width)
	EXPECT_FLOAT_EQ(child3->position.x, 110.0F);  // 0 + 50 + 60 (child1 + child2)
}

TEST(LayoutContainerTest, HorizontalLayoutTopAlign) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {300.0F, 100.0F},
		.direction = Direction::Horizontal,
		.vAlign = VAlign::Top});

	auto handle = layout.addChild(MockComponent(50.0F, 30.0F));
	layout.render();

	auto* child = layout.getChild<MockComponent>(handle);
	ASSERT_NE(child, nullptr);

	// Top-aligned: y = 0
	EXPECT_FLOAT_EQ(child->position.y, 0.0F);
}

TEST(LayoutContainerTest, HorizontalLayoutCenterAlign) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {300.0F, 100.0F},
		.direction = Direction::Horizontal,
		.vAlign = VAlign::Center});

	auto handle = layout.addChild(MockComponent(50.0F, 30.0F));
	layout.render();

	auto* child = layout.getChild<MockComponent>(handle);
	ASSERT_NE(child, nullptr);

	// Centered: y = (100 - 30) / 2 = 35
	EXPECT_FLOAT_EQ(child->position.y, 35.0F);
}

TEST(LayoutContainerTest, HorizontalLayoutBottomAlign) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {300.0F, 100.0F},
		.direction = Direction::Horizontal,
		.vAlign = VAlign::Bottom});

	auto handle = layout.addChild(MockComponent(50.0F, 30.0F));
	layout.render();

	auto* child = layout.getChild<MockComponent>(handle);
	ASSERT_NE(child, nullptr);

	// Bottom-aligned: y = 100 - 30 = 70
	EXPECT_FLOAT_EQ(child->position.y, 70.0F);
}

// ============================================================================
// Dirty Flag Tests
// ============================================================================

TEST(LayoutContainerTest, AddChildMarksLayoutDirty) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {200.0F, 200.0F}});

	// First render computes layout
	layout.render();

	auto handle1 = layout.addChild(MockComponent(50.0F, 30.0F));
	auto handle2 = layout.addChild(MockComponent(50.0F, 40.0F));

	// Render again after adding children
	layout.render();

	auto* child1 = layout.getChild<MockComponent>(handle1);
	auto* child2 = layout.getChild<MockComponent>(handle2);

	ASSERT_NE(child1, nullptr);
	ASSERT_NE(child2, nullptr);

	// Layout should have been recomputed
	EXPECT_FLOAT_EQ(child2->position.y, 30.0F);
}

// ============================================================================
// Container Margin Tests
// ============================================================================

TEST(LayoutContainerTest, ContainerMarginOffsetsChildren) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {200.0F, 200.0F},
		.direction = Direction::Vertical,
		.margin = 10.0F});

	auto handle = layout.addChild(MockComponent(50.0F, 30.0F));
	layout.render();

	auto* child = layout.getChild<MockComponent>(handle);
	ASSERT_NE(child, nullptr);

	// Child should be offset by container's margin
	EXPECT_FLOAT_EQ(child->position.x, 10.0F);  // Container margin
	EXPECT_FLOAT_EQ(child->position.y, 10.0F);  // Container margin
}

// ============================================================================
// Visibility Tests
// ============================================================================

TEST(LayoutContainerTest, InvisibleChildrenAreSkipped) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {200.0F, 300.0F},
		.direction = Direction::Vertical});

	auto handle1 = layout.addChild(MockComponent(50.0F, 30.0F));
	auto handle2 = layout.addChild(MockComponent(50.0F, 40.0F));
	auto handle3 = layout.addChild(MockComponent(50.0F, 25.0F));

	auto* child2 = layout.getChild<MockComponent>(handle2);
	ASSERT_NE(child2, nullptr);
	child2->visible = false;

	layout.render();

	auto* child1 = layout.getChild<MockComponent>(handle1);
	auto* child3 = layout.getChild<MockComponent>(handle3);

	ASSERT_NE(child1, nullptr);
	ASSERT_NE(child3, nullptr);

	// Child3 should be positioned right after child1 (skipping invisible child2)
	EXPECT_FLOAT_EQ(child1->position.y, 0.0F);
	EXPECT_FLOAT_EQ(child3->position.y, 30.0F);  // child1 height only
}
