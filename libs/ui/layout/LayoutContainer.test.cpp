#include "layout/LayoutContainer.h"
#include "layout/LayoutTypes.h"
#include "shapes/Shapes.h"
#include <gtest/gtest.h>

#include <utility>
#include <vector>

using namespace UI;
using namespace Foundation;

// ============================================================================
// Mock components for testing layout behavior
// ============================================================================

class MockComponent : public Component {
  public:
	MockComponent(float width, float height, float componentMargin = 0.0F) {
		size = {width, height};
		margin = componentMargin;
	}

	void render() override {}
};

// Wrap-aware mock: like Text, its height depends on the assigned width
// (height = area / width). Used to prove the cross-assign-then-measure order.
class WrappingMockComponent : public Component {
  public:
	explicit WrappingMockComponent(float contentArea) : area(contentArea) {
		size = {contentArea, 1.0F};
		widthMode = SizeMode::Fill;
		heightMode = SizeMode::Hug;
	}

	void setLayoutSize(float w, float h) override {
		if (w >= 0.0F) {
			size.x = w;
			size.y = w == 0.0F ? 0.0F : area / w;
		}
		if (h >= 0.0F) {
			size.y = h;
		}
	}

	void render() override {}

  private:
	float area;
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
	EXPECT_EQ(layout.widthMode, SizeMode::Fixed);
	EXPECT_EQ(layout.heightMode, SizeMode::Fixed);
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

TEST(LayoutContainerTest, ZeroSizeConstructsAsHug) {
	LayoutContainer layout(LayoutContainer::Args{.size = {0.0F, 100.0F}});

	EXPECT_EQ(layout.widthMode, SizeMode::Hug);
	EXPECT_EQ(layout.heightMode, SizeMode::Fixed);
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

TEST(LayoutContainerTest, VerticalLayoutStartAlign) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {200.0F, 300.0F},
		.direction = Direction::Vertical,
		.crossAlign = CrossAlign::Start});

	auto handle = layout.addChild(MockComponent(50.0F, 30.0F));
	layout.render();

	auto* child = layout.getChild<MockComponent>(handle);
	ASSERT_NE(child, nullptr);

	// Start-aligned: x = 0
	EXPECT_FLOAT_EQ(child->position.x, 0.0F);
}

TEST(LayoutContainerTest, VerticalLayoutCenterAlign) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {200.0F, 300.0F},
		.direction = Direction::Vertical,
		.crossAlign = CrossAlign::Center});

	auto handle = layout.addChild(MockComponent(50.0F, 30.0F));
	layout.render();

	auto* child = layout.getChild<MockComponent>(handle);
	ASSERT_NE(child, nullptr);

	// Centered: x = (200 - 50) / 2 = 75
	EXPECT_FLOAT_EQ(child->position.x, 75.0F);
}

TEST(LayoutContainerTest, VerticalLayoutEndAlign) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {200.0F, 300.0F},
		.direction = Direction::Vertical,
		.crossAlign = CrossAlign::End});

	auto handle = layout.addChild(MockComponent(50.0F, 30.0F));
	layout.render();

	auto* child = layout.getChild<MockComponent>(handle);
	ASSERT_NE(child, nullptr);

	// End-aligned: x = 200 - 50 = 150
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

TEST(LayoutContainerTest, HorizontalLayoutStartAlign) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {300.0F, 100.0F},
		.direction = Direction::Horizontal,
		.crossAlign = CrossAlign::Start});

	auto handle = layout.addChild(MockComponent(50.0F, 30.0F));
	layout.render();

	auto* child = layout.getChild<MockComponent>(handle);
	ASSERT_NE(child, nullptr);

	// Start-aligned: y = 0
	EXPECT_FLOAT_EQ(child->position.y, 0.0F);
}

TEST(LayoutContainerTest, HorizontalLayoutCenterAlign) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {300.0F, 100.0F},
		.direction = Direction::Horizontal,
		.crossAlign = CrossAlign::Center});

	auto handle = layout.addChild(MockComponent(50.0F, 30.0F));
	layout.render();

	auto* child = layout.getChild<MockComponent>(handle);
	ASSERT_NE(child, nullptr);

	// Centered: y = (100 - 30) / 2 = 35
	EXPECT_FLOAT_EQ(child->position.y, 35.0F);
}

TEST(LayoutContainerTest, HorizontalLayoutEndAlign) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {300.0F, 100.0F},
		.direction = Direction::Horizontal,
		.crossAlign = CrossAlign::End});

	auto handle = layout.addChild(MockComponent(50.0F, 30.0F));
	layout.render();

	auto* child = layout.getChild<MockComponent>(handle);
	ASSERT_NE(child, nullptr);

	// End-aligned: y = 100 - 30 = 70
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

TEST(LayoutContainerTest, InvalidateLayoutRecomputesAfterChildMutation) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {200.0F, 200.0F}});

	auto handle1 = layout.addChild(MockComponent(50.0F, 30.0F));
	auto handle2 = layout.addChild(MockComponent(50.0F, 40.0F));
	layout.render();

	// Mutate child content, then invalidate (no parent back-pointers in v1)
	layout.getChild<MockComponent>(handle1)->size.y = 100.0F;
	layout.invalidateLayout();
	layout.render();

	EXPECT_FLOAT_EQ(layout.getChild<MockComponent>(handle2)->position.y, 100.0F);
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

// ============================================================================
// Characterization Tests - originally pinned the pre-A2 engine defects, now
// flipped to the corrected semantics the A2 engine work settled on.
// ============================================================================

// Component that counts layout() calls, to observe propagation (or its absence).
class LayoutCountingComponent : public Component {
  public:
	LayoutCountingComponent(float width, float height) { size = {width, height}; }

	void render() override {}

	void layout(const Foundation::Rect& newBounds) override {
		layoutCallCount++;
		Component::layout(newBounds);
	}

	int layoutCallCount{0};
};

// FIXED (A2): layout() is a final-rect assignment - it adopts the bounds
// position AND size (previously the size was dropped on the floor).
TEST(LayoutContainerCharacterization, LayoutAdoptsBoundsPositionAndSize) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {100.0F, 50.0F}});

	layout.layout(Foundation::Rect{25.0F, 35.0F, 400.0F, 300.0F});

	EXPECT_FLOAT_EQ(layout.position.x, 25.0F);
	EXPECT_FLOAT_EQ(layout.position.y, 35.0F);
	EXPECT_FLOAT_EQ(layout.getWidth(), 400.0F);
	EXPECT_FLOAT_EQ(layout.getHeight(), 300.0F);
}

// FIXED (A2): the parent-assigned size now reaches the alignment math - a
// centered child centers within the assigned 400px bounds (was -25 before).
TEST(LayoutContainerCharacterization, ParentAssignedSizeReachesAlignmentMath) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {0.0F, 0.0F},
		.direction = Direction::Vertical,
		.crossAlign = CrossAlign::Center});

	auto handle = layout.addChild(MockComponent(50.0F, 30.0F));
	layout.layout(Foundation::Rect{0.0F, 0.0F, 400.0F, 300.0F});
	layout.render();

	auto* child = layout.getChild<MockComponent>(handle);
	ASSERT_NE(child, nullptr);

	EXPECT_FLOAT_EQ(child->position.x, 175.0F); // (400 - 50) / 2
}

// FIXED (A2): Hug + Center aligns against the hug extent (the max child), so
// the child stays inside the container (was pushed to negative X before).
TEST(LayoutContainerCharacterization, HugWithCenterAlignKeepsChildInside) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {100.0F, 0.0F},
		.size = {0.0F, 0.0F},
		.direction = Direction::Vertical,
		.crossAlign = CrossAlign::Center});

	auto handle = layout.addChild(MockComponent(50.0F, 30.0F));
	layout.render();

	auto* child = layout.getChild<MockComponent>(handle);
	ASSERT_NE(child, nullptr);

	// Hug width == child width, so centering is a no-op: x = container x
	EXPECT_FLOAT_EQ(child->position.x, 100.0F);
}

// FIXED (A2): Hug + End alignment no longer lands a child-width left of the
// container.
TEST(LayoutContainerCharacterization, HugWithEndAlignKeepsChildInside) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {100.0F, 0.0F},
		.size = {0.0F, 0.0F},
		.direction = Direction::Vertical,
		.crossAlign = CrossAlign::End});

	auto handle = layout.addChild(MockComponent(50.0F, 30.0F));
	layout.render();

	auto* child = layout.getChild<MockComponent>(handle);
	ASSERT_NE(child, nullptr);

	EXPECT_FLOAT_EQ(child->position.x, 100.0F);
}

// FIXED (A2): same defect on the Y axis (horizontal layout) is gone too.
TEST(LayoutContainerCharacterization, HugWithCenterAlignKeepsChildInsideOnYAxis) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 100.0F},
		.size = {0.0F, 0.0F},
		.direction = Direction::Horizontal,
		.crossAlign = CrossAlign::Center});

	auto handle = layout.addChild(MockComponent(50.0F, 30.0F));
	layout.render();

	auto* child = layout.getChild<MockComponent>(handle);
	ASSERT_NE(child, nullptr);

	EXPECT_FLOAT_EQ(child->position.y, 100.0F);
}

// FIXED (A2): nested LayoutContainers now receive a resolved size through
// layout(assignedBounds) instead of just a dirty flag - a stretched inner
// container adopts the outer's content width and aligns its own children
// within it.
TEST(LayoutContainerCharacterization, NestedContainerReceivesResolvedSize) {
	LayoutContainer outer(LayoutContainer::Args{
		.position = {10.0F, 20.0F},
		.size = {300.0F, 200.0F},
		.direction = Direction::Vertical,
		.crossAlign = CrossAlign::Stretch});

	LayoutContainer inner(LayoutContainer::Args{
		.size = {0.0F, 0.0F},
		.direction = Direction::Vertical,
		.crossAlign = CrossAlign::Center});
	auto innerHandle = outer.addChild(std::move(inner));

	auto* innerPtr = outer.getChild<LayoutContainer>(innerHandle);
	ASSERT_NE(innerPtr, nullptr);
	auto childHandle = innerPtr->addChild(MockComponent(50.0F, 30.0F));

	outer.render();

	// Inner adopted the outer's content position AND its stretched width
	EXPECT_FLOAT_EQ(innerPtr->position.x, 10.0F);
	EXPECT_FLOAT_EQ(innerPtr->position.y, 20.0F);
	EXPECT_FLOAT_EQ(innerPtr->getWidth(), 300.0F);
	EXPECT_FLOAT_EQ(innerPtr->getHeight(), 30.0F);

	// The inner container centered its child within the resolved 300px width
	auto* innerChild = innerPtr->getChild<MockComponent>(childHandle);
	ASSERT_NE(innerChild, nullptr);
	EXPECT_FLOAT_EQ(innerChild->position.x, 135.0F); // 10 + (300 - 50) / 2
	EXPECT_FLOAT_EQ(innerChild->position.y, 20.0F);
}

// SETTLED (A2): layout() recursion is reserved for nested LayoutContainers.
// Plain children are positioned via setPosition and resized via
// setLayoutSize; their own layout() never runs (components like
// ScrollContainer manage their internals in their own coordinate space).
TEST(LayoutContainerCharacterization, PlainChildLayoutIsNotCalled) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {200.0F, 200.0F},
		.direction = Direction::Vertical});

	auto handle = layout.addChild(LayoutCountingComponent(50.0F, 30.0F));
	layout.render();

	auto* child = layout.getChild<LayoutCountingComponent>(handle);
	ASSERT_NE(child, nullptr);
	EXPECT_EQ(child->layoutCallCount, 0);
}

// SETTLED (A2): reported size is the margin box (content + margin*2) for
// every component, containers included - an explicit size is the content
// size, so 100x50 with margin 10 reports 120x70 to its parent. This is the
// codebase-wide convention, kept deliberately.
TEST(LayoutContainerCharacterization, ExplicitSizeReportsMarginBox) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {100.0F, 50.0F},
		.margin = 10.0F});

	EXPECT_FLOAT_EQ(layout.getWidth(), 120.0F);
	EXPECT_FLOAT_EQ(layout.getHeight(), 70.0F);
}

// ============================================================================
// Engine Tests - gap
// ============================================================================

TEST(LayoutContainerEngine, GapSpacesChildren) {
	LayoutContainer layout(LayoutContainer::Args{
		.size = {200.0F, 300.0F},
		.direction = Direction::Vertical,
		.gap = 10.0F});

	auto handle1 = layout.addChild(MockComponent(50.0F, 30.0F));
	auto handle2 = layout.addChild(MockComponent(50.0F, 40.0F));
	auto handle3 = layout.addChild(MockComponent(50.0F, 25.0F));
	layout.render();

	EXPECT_FLOAT_EQ(layout.getChild<MockComponent>(handle1)->position.y, 0.0F);
	EXPECT_FLOAT_EQ(layout.getChild<MockComponent>(handle2)->position.y, 40.0F);  // 30 + 10
	EXPECT_FLOAT_EQ(layout.getChild<MockComponent>(handle3)->position.y, 90.0F);  // 40 + 40 + 10
}

TEST(LayoutContainerEngine, GapGrowsHugMainAxis) {
	LayoutContainer layout(LayoutContainer::Args{
		.size = {0.0F, 0.0F},
		.direction = Direction::Vertical,
		.gap = 10.0F});

	layout.addChild(MockComponent(50.0F, 30.0F));
	layout.addChild(MockComponent(50.0F, 40.0F));

	EXPECT_FLOAT_EQ(layout.getHeight(), 80.0F); // 30 + 40 + one 10px gap
	EXPECT_FLOAT_EQ(layout.getWidth(), 50.0F);  // cross axis: max, no gap
}

// Contract: non-resizable leaves (Circle, Line) inherit the no-op
// setLayoutSize, so Fill/Stretch assignments are silently ignored and the
// shape keeps its intrinsic size (see the LayoutContainer.h doc block).
TEST(LayoutContainerEngine, NonResizableLeafIgnoresFillAssignment) {
	LayoutContainer layout(LayoutContainer::Args{
		.size = {200.0F, 300.0F},
		.direction = Direction::Vertical,
		.crossAlign = CrossAlign::Stretch});

	auto circle = Circle(Circle::Args{.radius = 20.0F});
	circle.widthMode = SizeMode::Fill;
	auto handle = layout.addChild(std::move(circle));
	layout.render();

	EXPECT_FLOAT_EQ(layout.getChild<Circle>(handle)->getWidth(), 40.0F);
	EXPECT_FLOAT_EQ(layout.getChild<Circle>(handle)->getHeight(), 40.0F);
}

// ============================================================================
// Engine Tests - padding
// ============================================================================

TEST(LayoutContainerEngine, PaddingOffsetsChildrenAndGrowsHugSize) {
	LayoutContainer layout(LayoutContainer::Args{
		.size = {0.0F, 0.0F},
		.direction = Direction::Vertical,
		.padding = Insets{5.0F, 6.0F, 7.0F, 8.0F}}); // top right bottom left

	auto handle = layout.addChild(MockComponent(50.0F, 30.0F));
	layout.render();

	auto* child = layout.getChild<MockComponent>(handle);
	EXPECT_FLOAT_EQ(child->position.x, 8.0F);  // padding.left
	EXPECT_FLOAT_EQ(child->position.y, 5.0F);  // padding.top
	EXPECT_FLOAT_EQ(layout.getWidth(), 64.0F);  // 50 + 8 + 6
	EXPECT_FLOAT_EQ(layout.getHeight(), 42.0F); // 30 + 5 + 7
}

TEST(LayoutContainerEngine, PaddingGapAndMarginsInterplay) {
	LayoutContainer layout(LayoutContainer::Args{
		.size = {0.0F, 0.0F},
		.direction = Direction::Vertical,
		.gap = 6.0F,
		.padding = Insets{4.0F},
		.margin = 10.0F});

	auto handle1 = layout.addChild(MockComponent(50.0F, 30.0F, 5.0F));
	auto handle2 = layout.addChild(MockComponent(50.0F, 20.0F, 5.0F));
	layout.render();

	// Content origin = container margin (10) + padding.left/top (4)
	EXPECT_FLOAT_EQ(layout.getChild<MockComponent>(handle1)->position.x, 14.0F);
	EXPECT_FLOAT_EQ(layout.getChild<MockComponent>(handle1)->position.y, 14.0F);
	// Next child: 14 + child1 margin box (40) + gap (6)
	EXPECT_FLOAT_EQ(layout.getChild<MockComponent>(handle2)->position.y, 60.0F);
	// Hug margin box: children extent + padding + container margin
	EXPECT_FLOAT_EQ(layout.getWidth(), 88.0F);   // 60 + 4 + 4 + 10 + 10
	EXPECT_FLOAT_EQ(layout.getHeight(), 104.0F); // (40 + 30 + 6) + 8 + 20
}

// ============================================================================
// Engine Tests - distribution (1, 2, and 5 children per mode)
// ============================================================================

namespace {

	// Builds a 100x300 vertical container with `count` children of the given
	// height and returns the resolved child Y positions.
	std::vector<float> distributionYs(Distribution distribution, int count, float childHeight, float gap = 0.0F) {
		LayoutContainer layout(LayoutContainer::Args{
			.size = {100.0F, 300.0F},
			.direction = Direction::Vertical,
			.gap = gap,
			.distribution = distribution});
		std::vector<LayerHandle> handles;
		for (int i = 0; i < count; i++) {
			handles.push_back(layout.addChild(MockComponent(50.0F, childHeight)));
		}
		layout.render();
		std::vector<float> ys;
		for (auto handle : handles) {
			ys.push_back(layout.getChild<MockComponent>(handle)->position.y);
		}
		return ys;
	}

} // namespace

TEST(LayoutContainerEngine, DistributionStart) {
	EXPECT_EQ(distributionYs(Distribution::Start, 1, 30.0F), (std::vector<float>{0.0F}));
	EXPECT_EQ(distributionYs(Distribution::Start, 2, 30.0F), (std::vector<float>{0.0F, 30.0F}));
	EXPECT_EQ(
		distributionYs(Distribution::Start, 5, 20.0F),
		(std::vector<float>{0.0F, 20.0F, 40.0F, 60.0F, 80.0F}));
}

TEST(LayoutContainerEngine, DistributionCenter) {
	EXPECT_EQ(distributionYs(Distribution::Center, 1, 30.0F), (std::vector<float>{135.0F}));
	EXPECT_EQ(distributionYs(Distribution::Center, 2, 30.0F), (std::vector<float>{120.0F, 150.0F}));
	EXPECT_EQ(
		distributionYs(Distribution::Center, 5, 20.0F),
		(std::vector<float>{100.0F, 120.0F, 140.0F, 160.0F, 180.0F}));
}

TEST(LayoutContainerEngine, DistributionEnd) {
	EXPECT_EQ(distributionYs(Distribution::End, 1, 30.0F), (std::vector<float>{270.0F}));
	EXPECT_EQ(distributionYs(Distribution::End, 2, 30.0F), (std::vector<float>{240.0F, 270.0F}));
	EXPECT_EQ(
		distributionYs(Distribution::End, 5, 20.0F),
		(std::vector<float>{200.0F, 220.0F, 240.0F, 260.0F, 280.0F}));
}

TEST(LayoutContainerEngine, DistributionSpaceBetween) {
	// Single child: no between-space to distribute, stays at start
	EXPECT_EQ(distributionYs(Distribution::SpaceBetween, 1, 30.0F), (std::vector<float>{0.0F}));
	EXPECT_EQ(distributionYs(Distribution::SpaceBetween, 2, 30.0F), (std::vector<float>{0.0F, 270.0F}));
	EXPECT_EQ(
		distributionYs(Distribution::SpaceBetween, 5, 20.0F),
		(std::vector<float>{0.0F, 70.0F, 140.0F, 210.0F, 280.0F}));
}

TEST(LayoutContainerEngine, DistributionSpaceAround) {
	// Single child: half-space either side = centered
	EXPECT_EQ(distributionYs(Distribution::SpaceAround, 1, 30.0F), (std::vector<float>{135.0F}));
	EXPECT_EQ(distributionYs(Distribution::SpaceAround, 2, 30.0F), (std::vector<float>{60.0F, 210.0F}));
	EXPECT_EQ(
		distributionYs(Distribution::SpaceAround, 5, 20.0F),
		(std::vector<float>{20.0F, 80.0F, 140.0F, 200.0F, 260.0F}));
}

TEST(LayoutContainerEngine, DistributionSpaceEvenly) {
	EXPECT_EQ(distributionYs(Distribution::SpaceEvenly, 1, 30.0F), (std::vector<float>{135.0F}));
	EXPECT_EQ(distributionYs(Distribution::SpaceEvenly, 2, 30.0F), (std::vector<float>{80.0F, 190.0F}));
	const float step = 200.0F / 6.0F;
	auto		ys = distributionYs(Distribution::SpaceEvenly, 5, 20.0F);
	ASSERT_EQ(ys.size(), 5U);
	for (int i = 0; i < 5; i++) {
		EXPECT_NEAR(ys[static_cast<size_t>(i)], step * static_cast<float>(i + 1) + 20.0F * static_cast<float>(i), 0.001F);
	}
}

TEST(LayoutContainerEngine, SpaceBetweenStacksWithFixedGap) {
	// leftover = 300 - 100 - 10(gap) = 190; child2 = 50 + 10 + 190 = 250
	EXPECT_EQ(
		distributionYs(Distribution::SpaceBetween, 2, 50.0F, 10.0F),
		(std::vector<float>{0.0F, 250.0F}));
}

TEST(LayoutContainerEngine, DistributionOnHugMainAxisIsInert) {
	LayoutContainer layout(LayoutContainer::Args{
		.size = {100.0F, 0.0F},
		.direction = Direction::Vertical,
		.distribution = Distribution::Center});

	auto handle1 = layout.addChild(MockComponent(50.0F, 30.0F));
	auto handle2 = layout.addChild(MockComponent(50.0F, 30.0F));
	layout.render();

	// Hug main axis has no leftover: children pack from the origin
	EXPECT_FLOAT_EQ(layout.getChild<MockComponent>(handle1)->position.y, 0.0F);
	EXPECT_FLOAT_EQ(layout.getChild<MockComponent>(handle2)->position.y, 30.0F);
}

// ============================================================================
// Engine Tests - Fill sizing
// ============================================================================

TEST(LayoutContainerEngine, SingleFillChildTakesLeftover) {
	LayoutContainer layout(LayoutContainer::Args{
		.size = {100.0F, 300.0F},
		.direction = Direction::Vertical});

	auto fixedHandle = layout.addChild(MockComponent(80.0F, 100.0F));
	MockComponent fill(80.0F, 0.0F);
	fill.heightMode = SizeMode::Fill;
	auto fillHandle = layout.addChild(std::move(fill));
	layout.render();

	EXPECT_FLOAT_EQ(layout.getChild<MockComponent>(fillHandle)->size.y, 200.0F);
	EXPECT_FLOAT_EQ(layout.getChild<MockComponent>(fillHandle)->position.y, 100.0F);
	EXPECT_FLOAT_EQ(layout.getChild<MockComponent>(fixedHandle)->position.y, 0.0F);
}

TEST(LayoutContainerEngine, MixedFixedHugFill) {
	LayoutContainer layout(LayoutContainer::Args{
		.size = {100.0F, 300.0F},
		.direction = Direction::Vertical});

	layout.addChild(MockComponent(50.0F, 60.0F)); // Fixed
	MockComponent hug(50.0F, 40.0F);
	hug.heightMode = SizeMode::Hug; // measured, not resized
	layout.addChild(std::move(hug));
	MockComponent fill(50.0F, 0.0F);
	fill.heightMode = SizeMode::Fill;
	auto fillHandle = layout.addChild(std::move(fill));
	layout.render();

	// leftover = 300 - 60 - 40 = 200
	EXPECT_FLOAT_EQ(layout.getChild<MockComponent>(fillHandle)->size.y, 200.0F);
	EXPECT_FLOAT_EQ(layout.getChild<MockComponent>(fillHandle)->position.y, 100.0F);
}

TEST(LayoutContainerEngine, FillWeightSplitsLeftoverProportionally) {
	LayoutContainer layout(LayoutContainer::Args{
		.size = {100.0F, 300.0F},
		.direction = Direction::Vertical});

	layout.addChild(MockComponent(50.0F, 100.0F)); // Fixed, leaves 200
	MockComponent fillA(50.0F, 0.0F);
	fillA.heightMode = SizeMode::Fill;
	fillA.fillWeight = 1.0F;
	auto handleA = layout.addChild(std::move(fillA));
	MockComponent fillB(50.0F, 0.0F);
	fillB.heightMode = SizeMode::Fill;
	fillB.fillWeight = 3.0F;
	auto handleB = layout.addChild(std::move(fillB));
	layout.render();

	EXPECT_FLOAT_EQ(layout.getChild<MockComponent>(handleA)->size.y, 50.0F);
	EXPECT_FLOAT_EQ(layout.getChild<MockComponent>(handleB)->size.y, 150.0F);
}

TEST(LayoutContainerEngine, FillChildMarginComesOutOfLeftover) {
	LayoutContainer layout(LayoutContainer::Args{
		.size = {100.0F, 200.0F},
		.direction = Direction::Vertical});

	layout.addChild(MockComponent(50.0F, 80.0F));
	MockComponent fill(50.0F, 0.0F, 10.0F);
	fill.heightMode = SizeMode::Fill;
	auto fillHandle = layout.addChild(std::move(fill));
	layout.render();

	// leftover = 200 - 80 - fill margins (20) = 100 content
	auto* fillPtr = layout.getChild<MockComponent>(fillHandle);
	EXPECT_FLOAT_EQ(fillPtr->size.y, 100.0F);
	EXPECT_FLOAT_EQ(fillPtr->getHeight(), 120.0F); // margin box fills the rest exactly
}

TEST(LayoutContainerEngine, FillWithNoLeftoverGetsZero) {
	LayoutContainer layout(LayoutContainer::Args{
		.size = {100.0F, 100.0F},
		.direction = Direction::Vertical});

	layout.addChild(MockComponent(50.0F, 120.0F)); // overflows on its own
	MockComponent fill(50.0F, 0.0F);
	fill.heightMode = SizeMode::Fill;
	auto fillHandle = layout.addChild(std::move(fill));
	layout.render();

	EXPECT_FLOAT_EQ(layout.getChild<MockComponent>(fillHandle)->size.y, 0.0F);
}

// ============================================================================
// Engine Tests - cross-axis Stretch and Fill
// ============================================================================

TEST(LayoutContainerEngine, StretchResizesHugChildMinusMargin) {
	LayoutContainer layout(LayoutContainer::Args{
		.size = {200.0F, 100.0F},
		.direction = Direction::Vertical,
		.crossAlign = CrossAlign::Stretch});

	MockComponent child(50.0F, 30.0F, 5.0F);
	child.widthMode = SizeMode::Hug;
	auto handle = layout.addChild(std::move(child));
	layout.render();

	auto* childPtr = layout.getChild<MockComponent>(handle);
	EXPECT_FLOAT_EQ(childPtr->size.x, 190.0F);	  // 200 - margin*2
	EXPECT_FLOAT_EQ(childPtr->getWidth(), 200.0F); // margin box spans the content box
	EXPECT_FLOAT_EQ(childPtr->position.x, 0.0F);
}

TEST(LayoutContainerEngine, StretchLeavesFixedChildrenAlone) {
	LayoutContainer layout(LayoutContainer::Args{
		.size = {200.0F, 100.0F},
		.direction = Direction::Vertical,
		.crossAlign = CrossAlign::Stretch});

	auto handle = layout.addChild(MockComponent(50.0F, 30.0F)); // Fixed by default
	layout.render();

	auto* childPtr = layout.getChild<MockComponent>(handle);
	EXPECT_FLOAT_EQ(childPtr->size.x, 50.0F);
	EXPECT_FLOAT_EQ(childPtr->position.x, 0.0F); // Fixed under Stretch aligns at Start
}

TEST(LayoutContainerEngine, CrossAxisFillStretchesWithoutStretchAlign) {
	LayoutContainer layout(LayoutContainer::Args{
		.size = {200.0F, 100.0F},
		.direction = Direction::Vertical,
		.crossAlign = CrossAlign::Start});

	MockComponent child(50.0F, 30.0F);
	child.widthMode = SizeMode::Fill;
	auto handle = layout.addChild(std::move(child));
	layout.render();

	EXPECT_FLOAT_EQ(layout.getChild<MockComponent>(handle)->size.x, 200.0F);
}

// ============================================================================
// Engine Tests - edge cases
// ============================================================================

TEST(LayoutContainerEngine, ZeroChildrenIsSafe) {
	LayoutContainer layout(LayoutContainer::Args{
		.size = {0.0F, 0.0F},
		.padding = Insets{4.0F}});

	layout.render();

	EXPECT_FLOAT_EQ(layout.getWidth(), 8.0F); // padding only
	EXPECT_FLOAT_EQ(layout.getHeight(), 8.0F);
}

TEST(LayoutContainerEngine, OverflowDegradesToStart) {
	LayoutContainer layout(LayoutContainer::Args{
		.size = {100.0F, 50.0F},
		.direction = Direction::Vertical,
		.distribution = Distribution::Center});

	auto handle1 = layout.addChild(MockComponent(50.0F, 40.0F));
	auto handle2 = layout.addChild(MockComponent(50.0F, 40.0F));
	layout.render();

	// 80 > 50: leftover clamps to 0, children overflow past the end edge
	EXPECT_FLOAT_EQ(layout.getChild<MockComponent>(handle1)->position.y, 0.0F);
	EXPECT_FLOAT_EQ(layout.getChild<MockComponent>(handle2)->position.y, 40.0F);
}

TEST(LayoutContainerEngine, CrossOverflowDegradesToStart) {
	LayoutContainer layout(LayoutContainer::Args{
		.size = {100.0F, 100.0F},
		.direction = Direction::Vertical,
		.crossAlign = CrossAlign::Center});

	auto handle = layout.addChild(MockComponent(150.0F, 30.0F)); // wider than container
	layout.render();

	EXPECT_FLOAT_EQ(layout.getChild<MockComponent>(handle)->position.x, 0.0F);
}

TEST(LayoutContainerEngine, NestingThreeDeepPropagatesStretch) {
	LayoutContainer outer(LayoutContainer::Args{
		.position = {10.0F, 10.0F},
		.size = {300.0F, 300.0F},
		.direction = Direction::Vertical,
		.crossAlign = CrossAlign::Stretch});

	LayoutContainer mid(LayoutContainer::Args{
		.direction = Direction::Vertical,
		.crossAlign = CrossAlign::Stretch});
	auto midHandle = outer.addChild(std::move(mid));
	auto* midPtr = outer.getChild<LayoutContainer>(midHandle);
	ASSERT_NE(midPtr, nullptr);

	LayoutContainer inner(LayoutContainer::Args{
		.direction = Direction::Vertical,
		.crossAlign = CrossAlign::Stretch});
	auto innerHandle = midPtr->addChild(std::move(inner));
	auto* innerPtr = midPtr->getChild<LayoutContainer>(innerHandle);
	ASSERT_NE(innerPtr, nullptr);

	MockComponent leaf(50.0F, 30.0F);
	leaf.widthMode = SizeMode::Hug;
	auto leafHandle = innerPtr->addChild(std::move(leaf));

	outer.render();

	EXPECT_FLOAT_EQ(midPtr->getWidth(), 300.0F);
	EXPECT_FLOAT_EQ(innerPtr->getWidth(), 300.0F);
	auto* leafPtr = innerPtr->getChild<MockComponent>(leafHandle);
	ASSERT_NE(leafPtr, nullptr);
	EXPECT_FLOAT_EQ(leafPtr->size.x, 300.0F);
	EXPECT_FLOAT_EQ(leafPtr->position.x, 10.0F);
	EXPECT_FLOAT_EQ(leafPtr->position.y, 10.0F);
}

// ============================================================================
// Engine Tests - definite axes (zero is a valid resolved size)
// ============================================================================

// A Fill container squeezed out of a full parent resolves to 0 and REPORTS 0,
// even though it has children to hug: the sibling after it lands right where
// the leftover math says, not shifted by a phantom hug measurement.
TEST(LayoutContainerEngine, FillContainerResolvedToZeroReportsZero) {
	LayoutContainer parent(LayoutContainer::Args{
		.size = {100.0F, 100.0F},
		.direction = Direction::Vertical});

	auto topHandle = parent.addChild(MockComponent(50.0F, 60.0F));
	LayoutContainer fill(LayoutContainer::Args{.size = {50.0F, 0.0F}});
	fill.heightMode = SizeMode::Fill;
	auto fillHandle = parent.addChild(std::move(fill));
	auto* fillPtr = parent.getChild<LayoutContainer>(fillHandle);
	ASSERT_NE(fillPtr, nullptr);
	fillPtr->addChild(MockComponent(40.0F, 30.0F)); // would hug to 30
	auto bottomHandle = parent.addChild(MockComponent(50.0F, 40.0F));

	parent.render();

	// 60 + 40 fixed leaves no leftover: the Fill container is 0, not 30
	EXPECT_FLOAT_EQ(fillPtr->getHeight(), 0.0F);
	EXPECT_FLOAT_EQ(parent.getChild<MockComponent>(topHandle)->position.y, 0.0F);
	EXPECT_FLOAT_EQ(fillPtr->position.y, 60.0F);
	EXPECT_FLOAT_EQ(parent.getChild<MockComponent>(bottomHandle)->position.y, 60.0F);
}

// A nested Hug container stretched on the cross axis adopts the assigned
// size even when the parent's content box has collapsed to 0.
TEST(LayoutContainerEngine, StretchedHugContainerAdoptsZeroCrossSize) {
	LayoutContainer outer(LayoutContainer::Args{
		.size = {20.0F, 100.0F},
		.direction = Direction::Vertical,
		.padding = Insets{0.0F, 10.0F, 0.0F, 10.0F}, // content width = 0
		.crossAlign = CrossAlign::Stretch});

	LayoutContainer inner(LayoutContainer::Args{});
	auto innerHandle = outer.addChild(std::move(inner));
	auto* innerPtr = outer.getChild<LayoutContainer>(innerHandle);
	ASSERT_NE(innerPtr, nullptr);
	innerPtr->addChild(MockComponent(50.0F, 30.0F)); // would hug to 50

	outer.render();

	EXPECT_FLOAT_EQ(innerPtr->getWidth(), 0.0F);
	EXPECT_FLOAT_EQ(innerPtr->getHeight(), 30.0F); // main axis still hugs
}

// Direct contract: setLayoutSize(0, 0) on a Hug container beats hugging.
TEST(LayoutContainerEngine, ResolvedZeroBeatsHugMeasurement) {
	LayoutContainer layout(LayoutContainer::Args{});
	layout.addChild(MockComponent(50.0F, 30.0F));

	EXPECT_FLOAT_EQ(layout.getWidth(), 50.0F); // unresolved: hugs
	EXPECT_FLOAT_EQ(layout.getHeight(), 30.0F);

	layout.setLayoutSize(0.0F, 0.0F);

	EXPECT_FLOAT_EQ(layout.getWidth(), 0.0F);
	EXPECT_FLOAT_EQ(layout.getHeight(), 0.0F);
}

// A standalone Hug container that never received setLayoutSize keeps
// measuring live from its children.
TEST(LayoutContainerEngine, UnresolvedHugContainerStillHugsChildren) {
	LayoutContainer layout(LayoutContainer::Args{.direction = Direction::Vertical});
	layout.addChild(MockComponent(50.0F, 30.0F));
	layout.addChild(MockComponent(70.0F, 40.0F));
	layout.render();

	EXPECT_FLOAT_EQ(layout.getWidth(), 70.0F);	// max child width
	EXPECT_FLOAT_EQ(layout.getHeight(), 70.0F); // summed heights
}

// A Fixed container reports its explicit size no matter what its children
// measure.
TEST(LayoutContainerEngine, FixedContainerReportsExplicitSize) {
	LayoutContainer layout(LayoutContainer::Args{.size = {100.0F, 50.0F}});
	layout.addChild(MockComponent(200.0F, 200.0F)); // overflows
	layout.render();

	EXPECT_FLOAT_EQ(layout.getWidth(), 100.0F);
	EXPECT_FLOAT_EQ(layout.getHeight(), 50.0F);
}

// ============================================================================
// Engine Tests - wrap-aware sizing (the "text fits" fix)
// ============================================================================

// A wrap-aware child in a narrow Fill column: the cross pass assigns its
// width BEFORE the main pass measures its height, so the container's Hug
// height grows with the wrapped content. Mirrors Text + wordWrap (proven
// live in the ui-sandbox layout scene; unit-tested here with a mock because
// Text needs a FontRenderer to measure).
TEST(LayoutContainerEngine, WrapAwareFillChildGrowsHugHeight) {
	LayoutContainer wide(LayoutContainer::Args{
		.size = {100.0F, 0.0F},
		.direction = Direction::Vertical});
	auto wideHandle = wide.addChild(WrappingMockComponent(3000.0F));
	wide.render();

	EXPECT_FLOAT_EQ(wide.getChild<WrappingMockComponent>(wideHandle)->size.y, 30.0F);
	EXPECT_FLOAT_EQ(wide.getHeight(), 30.0F);

	LayoutContainer narrow(LayoutContainer::Args{
		.size = {50.0F, 0.0F},
		.direction = Direction::Vertical});
	auto narrowHandle = narrow.addChild(WrappingMockComponent(3000.0F));
	narrow.render();

	// Same content, half the width: wrapped height doubles and the Hug
	// container grows with it
	EXPECT_FLOAT_EQ(narrow.getChild<WrappingMockComponent>(narrowHandle)->size.y, 60.0F);
	EXPECT_FLOAT_EQ(narrow.getHeight(), 60.0F);
}

// A wrap-aware child assigned width 0 (collapsed content box) accepts the
// assignment and collapses to 0x0 instead of ignoring it (or dividing by 0).
TEST(LayoutContainerEngine, WrapAwareChildAcceptsZeroWidth) {
	LayoutContainer layout(LayoutContainer::Args{
		.size = {20.0F, 0.0F},
		.direction = Direction::Vertical,
		.padding = Insets{0.0F, 10.0F, 0.0F, 10.0F}}); // content width = 0
	auto handle = layout.addChild(WrappingMockComponent(3000.0F));
	layout.render();

	auto* child = layout.getChild<WrappingMockComponent>(handle);
	EXPECT_FLOAT_EQ(child->size.x, 0.0F);
	EXPECT_FLOAT_EQ(child->size.y, 0.0F);
	EXPECT_FLOAT_EQ(layout.getHeight(), 0.0F);
}

// Text wiring for the same fix: a parent-assigned width becomes the wrap
// width (Text::width), which the FontRenderer measurement cache keys on.
TEST(LayoutContainerEngine, TextLayoutSizeSetsWrapWidth) {
	Text text(Text::Args{.text = "hello world"});
	EXPECT_EQ(text.widthMode, SizeMode::Hug);
	EXPECT_EQ(text.heightMode, SizeMode::Hug);

	text.setLayoutSize(120.0F, kSizeKeep);
	ASSERT_TRUE(text.width.has_value());
	EXPECT_FLOAT_EQ(*text.width, 120.0F);
	EXPECT_FLOAT_EQ(text.getWidth(), 120.0F);

	// An explicit authored width maps to Fixed (layout never resizes it)
	Text fixed(Text::Args{.width = 80.0F, .text = "hi"});
	EXPECT_EQ(fixed.widthMode, SizeMode::Fixed);
}
