#include "debug/LayoutLint.h"

#include "layout/LayoutContainer.h"

#include <gtest/gtest.h>

using namespace UI;
using namespace Foundation;

namespace {

	constexpr Vec2 kViewport{800.0F, 600.0F};

	class StubComponent : public Component {
	  public:
		StubComponent(float width, float height) { size = {width, height}; }

		void render() override {}
	};

} // namespace

TEST(LayoutLintTest, CleanLayoutHasNoViolations) {
	StubComponent parent(200.0F, 300.0F);
	parent.position = {10.0F, 10.0F};

	auto handleA = parent.addChild(StubComponent(50.0F, 30.0F));
	auto handleB = parent.addChild(StubComponent(50.0F, 30.0F));
	parent.getChild<StubComponent>(handleA)->position = {10.0F, 10.0F};
	parent.getChild<StubComponent>(handleB)->position = {10.0F, 40.0F};

	LintResult result = lintUiTree({&parent}, kViewport);

	EXPECT_TRUE(result.clean());
}

TEST(LayoutLintTest, OverlappingSiblingsWithEqualZIndexFlagged) {
	StubComponent parent(200.0F, 300.0F);
	parent.position = {10.0F, 10.0F};

	auto handleA = parent.addChild(StubComponent(50.0F, 30.0F));
	auto handleB = parent.addChild(StubComponent(50.0F, 30.0F));
	parent.getChild<StubComponent>(handleA)->position = {10.0F, 10.0F};
	parent.getChild<StubComponent>(handleB)->position = {30.0F, 20.0F};

	LintResult result = lintUiTree({&parent}, kViewport);

	ASSERT_EQ(result.violations.size(), 1U);
	EXPECT_EQ(result.violations[0].rule, LintRule::SiblingOverlap);
	EXPECT_EQ(result.violations[0].path, "Component[0]/Component[0]");
	EXPECT_EQ(result.violations[0].otherPath, "Component[0]/Component[1]");
}

TEST(LayoutLintTest, OverlapAllowedWhenZIndexDiffers) {
	StubComponent parent(200.0F, 300.0F);
	parent.position = {10.0F, 10.0F};

	auto handleA = parent.addChild(StubComponent(50.0F, 30.0F));
	auto handleB = parent.addChild(StubComponent(50.0F, 30.0F));
	parent.getChild<StubComponent>(handleA)->position = {10.0F, 10.0F};
	auto* childB = parent.getChild<StubComponent>(handleB);
	childB->position = {30.0F, 20.0F};
	childB->zIndex = 1;

	LintResult result = lintUiTree({&parent}, kViewport);

	EXPECT_TRUE(result.clean());
}

TEST(LayoutLintTest, TouchingEdgesAreNotOverlap) {
	StubComponent parent(200.0F, 300.0F);
	parent.position = {0.0F, 0.0F};

	auto handleA = parent.addChild(StubComponent(50.0F, 30.0F));
	auto handleB = parent.addChild(StubComponent(50.0F, 30.0F));
	parent.getChild<StubComponent>(handleA)->position = {0.0F, 0.0F};
	parent.getChild<StubComponent>(handleB)->position = {50.0F, 0.0F};

	LintResult result = lintUiTree({&parent}, kViewport);

	EXPECT_TRUE(result.clean());
}

TEST(LayoutLintTest, ChildOutsideParentFlagged) {
	StubComponent parent(100.0F, 100.0F);
	parent.position = {50.0F, 50.0F};

	auto handle = parent.addChild(StubComponent(50.0F, 30.0F));
	parent.getChild<StubComponent>(handle)->position = {10.0F, 50.0F};

	LintResult result = lintUiTree({&parent}, kViewport);

	ASSERT_EQ(result.violations.size(), 1U);
	EXPECT_EQ(result.violations[0].rule, LintRule::ChildOutsideParent);
	EXPECT_EQ(result.violations[0].otherPath, "Component[0]");
	EXPECT_FLOAT_EQ(result.violations[0].bounds.x, 10.0F);
}

TEST(LayoutLintTest, ChildWithinEpsilonOfParentEdgeIsClean) {
	StubComponent parent(100.0F, 100.0F);
	parent.position = {50.0F, 50.0F};

	auto handle = parent.addChild(StubComponent(50.0F, 30.0F));
	parent.getChild<StubComponent>(handle)->position = {49.75F, 50.0F};

	LintResult result = lintUiTree({&parent}, kViewport);

	EXPECT_TRUE(result.clean());
}

TEST(LayoutLintTest, ElementOutsideViewportFlagged) {
	StubComponent root(50.0F, 30.0F);
	root.position = {790.0F, 10.0F};

	LintResult result = lintUiTree({&root}, kViewport);

	ASSERT_EQ(result.violations.size(), 1U);
	EXPECT_EQ(result.violations[0].rule, LintRule::OutsideViewport);
	EXPECT_EQ(result.violations[0].otherPath, "viewport");
}

TEST(LayoutLintTest, ZeroSizeVisibleElementFlagged) {
	StubComponent root(0.0F, 30.0F);
	root.position = {10.0F, 10.0F};

	LintResult result = lintUiTree({&root}, kViewport);

	ASSERT_EQ(result.violations.size(), 1U);
	EXPECT_EQ(result.violations[0].rule, LintRule::ZeroOrNegativeSize);
	EXPECT_EQ(result.violations[0].path, "Component[0]");
}

TEST(LayoutLintTest, InvisibleSubtreeIsSkipped) {
	StubComponent parent(100.0F, 100.0F);
	parent.position = {0.0F, 0.0F};
	parent.visible = false;

	// Would violate ChildOutsideParent and SiblingOverlap if visible
	auto handleA = parent.addChild(StubComponent(50.0F, 30.0F));
	auto handleB = parent.addChild(StubComponent(50.0F, 30.0F));
	parent.getChild<StubComponent>(handleA)->position = {200.0F, 0.0F};
	parent.getChild<StubComponent>(handleB)->position = {200.0F, 0.0F};

	LintResult result = lintUiTree({&parent}, kViewport);

	EXPECT_TRUE(result.clean());
}

// FIXED (A2): the Hug + Center defect this test used to pin is gone - the
// child now centers within the hug extent and the linter stays clean.
TEST(LayoutLintTest, HugCenterContainerIsClean) {
	LayoutContainer layout(LayoutContainer::Args{
		.position = {100.0F, 100.0F},
		.size = {0.0F, 0.0F},
		.direction = Direction::Vertical,
		.crossAlign = CrossAlign::Center,
		.id = "hug_center"});
	layout.addChild(StubComponent(50.0F, 30.0F));
	layout.render();

	LintResult result = lintUiTree({&layout}, kViewport);

	EXPECT_TRUE(result.clean());
}

TEST(LayoutLintTest, JsonEntryPointMatchesDirectResult) {
	StubComponent root(0.0F, 30.0F);
	root.position = {10.0F, 10.0F};

	nlohmann::json json = nlohmann::json::parse(lintUiTreeJson({&root}, kViewport));

	EXPECT_EQ(json["count"].get<size_t>(), 1U);
	ASSERT_EQ(json["violations"].size(), 1U);
	EXPECT_EQ(json["violations"][0]["rule"], "zero-or-negative-size");
	EXPECT_EQ(json["violations"][0]["path"], "Component[0]");
}
