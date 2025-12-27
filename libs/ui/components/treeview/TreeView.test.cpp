#include "TreeView.h"

#include <gtest/gtest.h>

namespace UI {

class TreeViewTest : public ::testing::Test {
  protected:
	void SetUp() override {
		// Common setup if needed
	}

	// Helper to create a simple tree structure
	std::vector<TreeNode> createTestTree() {
		return {
			TreeNode{
				.label = "Category A",
				.count = 10,
				.children =
					{
						TreeNode{.label = "Item A1", .count = 5},
						TreeNode{.label = "Item A2", .count = 3},
						TreeNode{.label = "Subcategory",
								 .children =
									 {
										 TreeNode{.label = "Nested Item"},
									 }},
					},
			},
			TreeNode{
				.label = "Category B",
				.count = 20,
				.children =
					{
						TreeNode{.label = "Item B1"},
					},
			},
			TreeNode{
				.label = "Category C (empty)",
			},
		};
	}
};

// === Construction Tests ===

TEST_F(TreeViewTest, ConstructsWithDefaults) {
	TreeView tree(TreeView::Args{});

	EXPECT_FLOAT_EQ(tree.getRowHeight(), Theme::TreeView::rowHeight);
	EXPECT_FLOAT_EQ(tree.getIndentWidth(), Theme::TreeView::indentWidth);
	EXPECT_FLOAT_EQ(tree.getWidth(), 200.0F);  // Default size
	EXPECT_FLOAT_EQ(tree.getHeight(), 300.0F); // Default size
	EXPECT_EQ(tree.getVisibleRowCount(), 0);   // No nodes yet
}

TEST_F(TreeViewTest, ConstructsWithCustomSize) {
	TreeView tree(TreeView::Args{
		.size = {300.0F, 400.0F},
		.rowHeight = 30.0F,
		.indentWidth = 20.0F,
	});

	EXPECT_FLOAT_EQ(tree.getRowHeight(), 30.0F);
	EXPECT_FLOAT_EQ(tree.getIndentWidth(), 20.0F);
	EXPECT_FLOAT_EQ(tree.getWidth(), 300.0F);
	EXPECT_FLOAT_EQ(tree.getHeight(), 400.0F);
}

TEST_F(TreeViewTest, ConstructsWithMargin) {
	TreeView tree(TreeView::Args{
		.size = {200.0F, 300.0F},
		.margin = 10.0F,
	});

	// getWidth/getHeight include margin on both sides
	EXPECT_FLOAT_EQ(tree.getWidth(), 220.0F);  // 200 + 10*2
	EXPECT_FLOAT_EQ(tree.getHeight(), 320.0F); // 300 + 10*2
}

// === Data Management Tests ===

TEST_F(TreeViewTest, SetRootNodesPopulatesTree) {
	TreeView tree(TreeView::Args{});
	tree.setRootNodes(createTestTree());

	// All nodes collapsed initially, so only root nodes visible
	EXPECT_EQ(tree.getVisibleRowCount(), 3); // Category A, B, C
	EXPECT_EQ(tree.getRootNodes().size(), 3);
}

TEST_F(TreeViewTest, GetRootNodesReturnsReference) {
	TreeView tree(TreeView::Args{});
	tree.setRootNodes(createTestTree());

	// Modify through reference
	tree.getRootNodes()[0].label = "Modified";
	EXPECT_EQ(tree.getRootNodes()[0].label, "Modified");
}

// === Expand/Collapse Tests ===

TEST_F(TreeViewTest, ToggleNodeExpandsCollapsed) {
	TreeView tree(TreeView::Args{});
	tree.setRootNodes(createTestTree());

	// Initially 3 visible (all collapsed)
	EXPECT_EQ(tree.getVisibleRowCount(), 3);

	// Expand first category (has 3 children)
	tree.toggleNode(0);

	// Now should show: Category A, Item A1, Item A2, Subcategory, Category B, Category C
	EXPECT_EQ(tree.getVisibleRowCount(), 6);
	EXPECT_TRUE(tree.getRootNodes()[0].expanded);
}

TEST_F(TreeViewTest, ToggleNodeCollapsesExpanded) {
	TreeView tree(TreeView::Args{});
	tree.setRootNodes(createTestTree());

	// Expand first
	tree.toggleNode(0);
	EXPECT_EQ(tree.getVisibleRowCount(), 6);

	// Collapse
	tree.toggleNode(0);
	EXPECT_EQ(tree.getVisibleRowCount(), 3);
	EXPECT_FALSE(tree.getRootNodes()[0].expanded);
}

TEST_F(TreeViewTest, ExpandAllExpandsAllNodes) {
	TreeView tree(TreeView::Args{});
	tree.setRootNodes(createTestTree());

	tree.expandAll();

	// All nodes visible: Cat A + 3 children (one has nested) + Cat B + 1 child + Cat C
	// Category A, Item A1, Item A2, Subcategory, Nested Item, Category B, Item B1, Category C
	EXPECT_EQ(tree.getVisibleRowCount(), 8);
	EXPECT_TRUE(tree.getRootNodes()[0].expanded);
	EXPECT_TRUE(tree.getRootNodes()[1].expanded);
}

TEST_F(TreeViewTest, CollapseAllCollapsesAllNodes) {
	TreeView tree(TreeView::Args{});
	tree.setRootNodes(createTestTree());

	tree.expandAll();
	tree.collapseAll();

	EXPECT_EQ(tree.getVisibleRowCount(), 3);
	EXPECT_FALSE(tree.getRootNodes()[0].expanded);
	EXPECT_FALSE(tree.getRootNodes()[1].expanded);
}

// === Callback Tests ===

TEST_F(TreeViewTest, OnExpandCallbackFires) {
	TreeView tree(TreeView::Args{});
	tree.setRootNodes(createTestTree());

	bool callbackFired = false;
	std::string expandedLabel;

	tree.setOnExpand([&](TreeNode& node) {
		callbackFired = true;
		expandedLabel = node.label;
	});

	tree.toggleNode(0); // Expand Category A

	EXPECT_TRUE(callbackFired);
	EXPECT_EQ(expandedLabel, "Category A");
}

TEST_F(TreeViewTest, OnCollapseCallbackFires) {
	TreeView tree(TreeView::Args{});
	tree.setRootNodes(createTestTree());

	tree.toggleNode(0); // Expand first

	bool callbackFired = false;
	std::string collapsedLabel;

	tree.setOnCollapse([&](TreeNode& node) {
		callbackFired = true;
		collapsedLabel = node.label;
	});

	tree.toggleNode(0); // Collapse

	EXPECT_TRUE(callbackFired);
	EXPECT_EQ(collapsedLabel, "Category A");
}

// === Hit Testing Tests ===

TEST_F(TreeViewTest, ContainsPointInsideBounds) {
	TreeView tree(TreeView::Args{
		.position = {100.0F, 100.0F},
		.size = {200.0F, 300.0F},
	});

	// Inside bounds
	EXPECT_TRUE(tree.containsPoint({150.0F, 150.0F}));
	EXPECT_TRUE(tree.containsPoint({100.0F, 100.0F})); // Top-left corner

	// Outside bounds
	EXPECT_FALSE(tree.containsPoint({50.0F, 150.0F}));	// Left of
	EXPECT_FALSE(tree.containsPoint({350.0F, 150.0F})); // Right of
	EXPECT_FALSE(tree.containsPoint({150.0F, 50.0F}));	// Above
	EXPECT_FALSE(tree.containsPoint({150.0F, 450.0F})); // Below
}

TEST_F(TreeViewTest, LeafNodeToggleDoesNothing) {
	TreeView tree(TreeView::Args{});

	// Create tree with leaf nodes only
	tree.setRootNodes({
		TreeNode{.label = "Leaf 1"},
		TreeNode{.label = "Leaf 2"},
	});

	EXPECT_EQ(tree.getVisibleRowCount(), 2);

	// Toggle leaf node - should have no effect
	tree.toggleNode(0);
	EXPECT_EQ(tree.getVisibleRowCount(), 2);
}

TEST_F(TreeViewTest, ToggleOutOfBoundsDoesNothing) {
	TreeView tree(TreeView::Args{});
	tree.setRootNodes(createTestTree());

	// Toggle non-existent index
	tree.toggleNode(100);

	// Should not crash, no change
	EXPECT_EQ(tree.getVisibleRowCount(), 3);
}

// === Position Tests ===

TEST_F(TreeViewTest, SetPositionUpdatesBase) {
	TreeView tree(TreeView::Args{
		.position = {10.0F, 20.0F},
	});

	tree.setPosition(50.0F, 60.0F);

	Foundation::Vec2 contentPos = tree.getContentPosition();
	EXPECT_FLOAT_EQ(contentPos.x, 50.0F);
	EXPECT_FLOAT_EQ(contentPos.y, 60.0F);
}

TEST_F(TreeViewTest, SetPositionWithMargin) {
	TreeView tree(TreeView::Args{
		.position = {0.0F, 0.0F},
		.margin = 8.0F,
	});

	tree.setPosition(100.0F, 200.0F);

	Foundation::Vec2 contentPos = tree.getContentPosition();
	EXPECT_FLOAT_EQ(contentPos.x, 108.0F); // 100 + 8 margin
	EXPECT_FLOAT_EQ(contentPos.y, 208.0F); // 200 + 8 margin
}

} // namespace UI
