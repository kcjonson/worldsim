#include "layer/layer_manager.h"
#include <gtest/gtest.h>

using namespace UI;

// ============================================================================
// Layer Creation Tests
// ============================================================================

TEST(LayerManagerTest, Create) {
	LayerManager manager;

	Rectangle	rect{.position = {10.0f, 20.0f}, .size = {100.0f, 50.0f}};
	LayerHandle handle = manager.Create(rect);

	EXPECT_EQ(handle.GetIndex(), 0);
	EXPECT_EQ(handle.GetGeneration(), 0);
	EXPECT_EQ(manager.GetLayerCount(), 1);

	const LayerNode& node = manager.GetNode(handle);
	EXPECT_TRUE(std::holds_alternative<Rectangle>(node.data));

	const auto& createdRect = std::get<Rectangle>(node.data);
	EXPECT_EQ(createdRect.position.x, 10.0f);
	EXPECT_EQ(createdRect.position.y, 20.0f);
	EXPECT_EQ(createdRect.size.x, 100.0f);
	EXPECT_EQ(createdRect.size.y, 50.0f);
}

TEST(LayerManagerTest, CreateContainer) {
	LayerManager manager;

	Container	container{.id = "test_container", .zIndex = 5.0f};
	LayerHandle handle = manager.Create(container);

	EXPECT_EQ(handle.GetIndex(), 0);
	EXPECT_TRUE(std::holds_alternative<Container>(manager.GetNode(handle).data));
	EXPECT_EQ(manager.GetNode(handle).zIndex, 5.0f);
}

TEST(LayerManagerTest, CreateMultipleLayers) {
	LayerManager manager;

	LayerHandle container = manager.Create(Container{});
	LayerHandle rect1 = manager.Create(Rectangle{});
	LayerHandle rect2 = manager.Create(Rectangle{});
	LayerHandle circle = manager.Create(Circle{});
	LayerHandle line = manager.Create(Line{});

	EXPECT_EQ(container.GetIndex(), 0);
	EXPECT_EQ(rect1.GetIndex(), 1);
	EXPECT_EQ(rect2.GetIndex(), 2);
	EXPECT_EQ(circle.GetIndex(), 3);
	EXPECT_EQ(line.GetIndex(), 4);
	EXPECT_EQ(manager.GetLayerCount(), 5);
}

TEST(LayerManagerTest, CreateCircle) {
	LayerManager manager;

	Circle		circle{.center = {50.0f, 50.0f}, .radius = 25.0f};
	LayerHandle handle = manager.Create(circle);

	const auto& createdCircle = std::get<Circle>(manager.GetNode(handle).data);
	EXPECT_EQ(createdCircle.center.x, 50.0f);
	EXPECT_EQ(createdCircle.radius, 25.0f);
}

TEST(LayerManagerTest, CreateText) {
	LayerManager manager;

	Text		text{.position = {100.0f, 100.0f}, .text = "Hello, World!"};
	LayerHandle handle = manager.Create(text);

	const auto& createdText = std::get<Text>(manager.GetNode(handle).data);
	EXPECT_EQ(createdText.text, "Hello, World!");
}

TEST(LayerManagerTest, CreateLine) {
	LayerManager manager;

	Line		line{.start = {0.0f, 0.0f}, .end = {100.0f, 100.0f}};
	LayerHandle handle = manager.Create(line);

	const auto& createdLine = std::get<Line>(manager.GetNode(handle).data);
	EXPECT_EQ(createdLine.start.x, 0.0f);
	EXPECT_EQ(createdLine.end.x, 100.0f);
}

TEST(LayerManagerTest, CreateReadsZIndexFromShape) {
	LayerManager manager;

	Rectangle	rect{.zIndex = 42.0f};
	LayerHandle handle = manager.Create(rect);

	EXPECT_EQ(manager.GetNode(handle).zIndex, 42.0f);
}

TEST(LayerManagerTest, CreateReadsVisibleFromShape) {
	LayerManager manager;

	Rectangle	rect{.visible = false};
	LayerHandle handle = manager.Create(rect);

	EXPECT_FALSE(manager.GetNode(handle).visible);
}

TEST(LayerManagerTest, AutoAssignZIndexOnCreate) {
	LayerManager manager;

	// When zIndex is not specified (defaults to -1.0F), auto-assign based on insertion order
	Rectangle rect1{}; // zIndex = -1.0F (default)
	Rectangle rect2{}; // zIndex = -1.0F (default)
	Rectangle rect3{}; // zIndex = -1.0F (default)

	LayerHandle handle1 = manager.Create(rect1);
	LayerHandle handle2 = manager.Create(rect2);
	LayerHandle handle3 = manager.Create(rect3);

	// Should auto-assign 1.0, 2.0, 3.0
	EXPECT_EQ(manager.GetNode(handle1).zIndex, 1.0f);
	EXPECT_EQ(manager.GetNode(handle2).zIndex, 2.0f);
	EXPECT_EQ(manager.GetNode(handle3).zIndex, 3.0f);
}

TEST(LayerManagerTest, ExplicitZeroZIndexAllowed) {
	LayerManager manager;

	// Explicit 0.0F should be allowed (not auto-assigned)
	Rectangle	rect{.zIndex = 0.0f};
	LayerHandle handle = manager.Create(rect);

	EXPECT_EQ(manager.GetNode(handle).zIndex, 0.0f); // Should keep 0.0, not auto-assign
}

TEST(LayerManagerTest, AutoAssignZIndexOnAddChild) {
	LayerManager manager;

	LayerHandle parent = manager.Create(Container{});

	// AddChild should also auto-assign zIndex
	Rectangle	rect1{};
	Rectangle	rect2{};
	LayerHandle child1 = manager.AddChild(parent, rect1);
	LayerHandle child2 = manager.AddChild(parent, rect2);

	// Auto-assignment continues from previous counter
	// parent got 1.0, so children should get 2.0, 3.0
	EXPECT_EQ(manager.GetNode(child1).zIndex, 2.0f);
	EXPECT_EQ(manager.GetNode(child2).zIndex, 3.0f);
}

TEST(LayerManagerTest, ExplicitZIndexOverridesAuto) {
	LayerManager manager;

	Rectangle rect1{};				  // Auto-assign (1.0)
	Rectangle rect2{.zIndex = 99.0f}; // Explicit
	Rectangle rect3{};				  // Auto-assign (2.0, not 100.0)

	LayerHandle handle1 = manager.Create(rect1);
	LayerHandle handle2 = manager.Create(rect2);
	LayerHandle handle3 = manager.Create(rect3);

	EXPECT_EQ(manager.GetNode(handle1).zIndex, 1.0f);  // Auto
	EXPECT_EQ(manager.GetNode(handle2).zIndex, 99.0f); // Explicit
	EXPECT_EQ(manager.GetNode(handle3).zIndex, 2.0f);  // Auto continues
}

TEST(LayerManagerTest, StableSortPreservesInsertionOrder) {
	LayerManager manager;

	LayerHandle parent = manager.Create(Container{});

	// Add three children with the SAME zIndex
	Rectangle rect1{.zIndex = 5.0f};
	Rectangle rect2{.zIndex = 5.0f};
	Rectangle rect3{.zIndex = 5.0f};

	LayerHandle child1 = manager.AddChild(parent, rect1);
	LayerHandle child2 = manager.AddChild(parent, rect2);
	LayerHandle child3 = manager.AddChild(parent, rect3);

	// Add a child with different zIndex to trigger dirty flag
	Rectangle	rect4{.zIndex = 1.0f};
	LayerHandle child4 = manager.AddChild(parent, rect4);

	// Manually trigger sort (normally happens during render)
	manager.SortChildren(parent);

	// Verify stable_sort preserved insertion order for equal zIndex
	const auto& children = manager.GetChildren(parent);
	EXPECT_EQ(children.size(), 4u);
	EXPECT_EQ(children[0], child4); // zIndex 1.0 (lowest)
	EXPECT_EQ(children[1], child1); // zIndex 5.0 (first added)
	EXPECT_EQ(children[2], child2); // zIndex 5.0 (second added)
	EXPECT_EQ(children[3], child3); // zIndex 5.0 (third added)
}

// ============================================================================
// Hierarchy Management Tests
// ============================================================================

TEST(LayerManagerTest, AddChild) {
	LayerManager manager;

	LayerHandle parent = manager.Create(Rectangle{});
	LayerHandle child = manager.Create(Rectangle{});

	manager.AddChild(parent, child);

	const auto& parentNode = manager.GetNode(parent);
	const auto& childNode = manager.GetNode(child);

	EXPECT_EQ(parentNode.childHandles.size(), 1);
	EXPECT_EQ(parentNode.childHandles[0], child);
	EXPECT_EQ(childNode.parent, parent);
}

TEST(LayerManagerTest, AddMultipleChildren) {
	LayerManager manager;

	LayerHandle parent = manager.Create(Rectangle{});
	LayerHandle child1 = manager.Create(Rectangle{});
	LayerHandle child2 = manager.Create(Rectangle{});
	LayerHandle child3 = manager.Create(Rectangle{});

	manager.AddChild(parent, child1);
	manager.AddChild(parent, child2);
	manager.AddChild(parent, child3);

	const auto& parentNode = manager.GetNode(parent);
	EXPECT_EQ(parentNode.childHandles.size(), 3);
	EXPECT_EQ(parentNode.childHandles[0], child1);
	EXPECT_EQ(parentNode.childHandles[1], child2);
	EXPECT_EQ(parentNode.childHandles[2], child3);
}

TEST(LayerManagerTest, RemoveChild) {
	LayerManager manager;

	LayerHandle parent = manager.Create(Rectangle{});
	LayerHandle child = manager.Create(Rectangle{});

	manager.AddChild(parent, child);
	manager.RemoveChild(parent, child);

	const auto& parentNode = manager.GetNode(parent);
	const auto& childNode = manager.GetNode(child);

	EXPECT_EQ(parentNode.childHandles.size(), 0);
	EXPECT_FALSE(childNode.parent.IsValid()); // Now root
}

TEST(LayerManagerTest, ReparentChild) {
	LayerManager manager;

	LayerHandle parent1 = manager.Create(Rectangle{});
	LayerHandle parent2 = manager.Create(Rectangle{});
	LayerHandle child = manager.Create(Rectangle{});

	// Add to first parent
	manager.AddChild(parent1, child);
	EXPECT_EQ(manager.GetNode(parent1).childHandles.size(), 1);
	EXPECT_EQ(manager.GetNode(child).parent, parent1);

	// Reparent to second parent
	manager.AddChild(parent2, child);
	EXPECT_EQ(manager.GetNode(parent1).childHandles.size(), 0); // Removed from first
	EXPECT_EQ(manager.GetNode(parent2).childHandles.size(), 1); // Added to second
	EXPECT_EQ(manager.GetNode(child).parent, parent2);
}

TEST(LayerManagerTest, NestedHierarchy) {
	LayerManager manager;

	LayerHandle root = manager.Create(Rectangle{});
	LayerHandle level1a = manager.Create(Rectangle{});
	LayerHandle level1b = manager.Create(Rectangle{});
	LayerHandle level2 = manager.Create(Rectangle{});

	manager.AddChild(root, level1a);
	manager.AddChild(root, level1b);
	manager.AddChild(level1a, level2);

	// Verify structure
	EXPECT_EQ(manager.GetNode(root).childHandles.size(), 2);
	EXPECT_EQ(manager.GetNode(level1a).childHandles.size(), 1);
	EXPECT_EQ(manager.GetNode(level1b).childHandles.size(), 0);
	EXPECT_EQ(manager.GetNode(level2).parent, level1a);
}

TEST(LayerManagerTest, CycleDetection) {
	LayerManager manager;

	LayerHandle root = manager.Create(Rectangle{});
	LayerHandle child = manager.Create(Rectangle{});
	LayerHandle grandchild = manager.Create(Rectangle{});

	manager.AddChild(root, child);
	manager.AddChild(child, grandchild);

	// Attempting to make root a child of its own descendant should trigger assertion
	// This would create a cycle: root -> child -> grandchild -> root
	EXPECT_DEATH(manager.AddChild(grandchild, root), "Cannot add ancestor as child");
	EXPECT_DEATH(manager.AddChild(child, root), "Cannot add ancestor as child");
}

TEST(LayerManagerTest, AddChildConvenienceOverload_Rectangle) {
	LayerManager manager;

	LayerHandle parent = manager.Create(Container{});

	Rectangle	rect{.position = {10.0f, 20.0f}, .size = {100.0f, 50.0f}, .zIndex = 3.0f, .visible = false};
	LayerHandle child = manager.AddChild(parent, rect);

	// Verify hierarchy
	EXPECT_EQ(manager.GetNode(parent).childHandles.size(), 1);
	EXPECT_EQ(manager.GetNode(parent).childHandles[0], child);
	EXPECT_EQ(manager.GetNode(child).parent, parent);

	// Verify zIndex and visible were read from shape
	EXPECT_EQ(manager.GetNode(child).zIndex, 3.0f);
	EXPECT_FALSE(manager.GetNode(child).visible);

	// Verify shape data
	const auto& createdRect = std::get<Rectangle>(manager.GetNode(child).data);
	EXPECT_EQ(createdRect.position.x, 10.0f);
}

TEST(LayerManagerTest, AddChildConvenienceOverload_Circle) {
	LayerManager manager;

	LayerHandle parent = manager.Create(Container{.id = "parent"});

	Circle		circle{.center = {50.0f, 50.0f}, .radius = 25.0f, .zIndex = 10.0f};
	LayerHandle child = manager.AddChild(parent, circle);

	EXPECT_EQ(manager.GetNode(child).parent, parent);
	EXPECT_EQ(manager.GetNode(child).zIndex, 10.0f);
	EXPECT_TRUE(std::holds_alternative<Circle>(manager.GetNode(child).data));
}

TEST(LayerManagerTest, AddChildConvenienceOverload_Container) {
	LayerManager manager;

	LayerHandle parent = manager.Create(Container{});

	Container	childContainer{.id = "child_container", .zIndex = 5.0f};
	LayerHandle child = manager.AddChild(parent, childContainer);

	EXPECT_EQ(manager.GetNode(child).parent, parent);
	EXPECT_TRUE(std::holds_alternative<Container>(manager.GetNode(child).data));
}

// ============================================================================
// Z-Index Management Tests
// ============================================================================

TEST(LayerManagerTest, SetZIndex) {
	LayerManager manager;

	LayerHandle layer = manager.Create(Rectangle{});

	manager.SetZIndex(layer, 42.0f);
	EXPECT_EQ(manager.GetZIndex(layer), 42.0f);

	manager.SetZIndex(layer, -10.0f);
	EXPECT_EQ(manager.GetZIndex(layer), -10.0f);
}

TEST(LayerManagerTest, ZIndexMarksDirtyFlag) {
	LayerManager manager;

	LayerHandle parent = manager.Create(Rectangle{});
	LayerHandle child = manager.Create(Rectangle{});

	manager.AddChild(parent, child);

	// Initially not dirty (assuming in-order add)
	auto& parentNode = manager.GetNode(parent);
	parentNode.childrenNeedSorting = false;

	// Changing child's z-index should mark parent dirty
	manager.SetZIndex(child, 100.0f);
	EXPECT_TRUE(parentNode.childrenNeedSorting);
}

TEST(LayerManagerTest, SortChildren) {
	LayerManager manager;

	LayerHandle parent = manager.Create(Rectangle{});
	LayerHandle child1 = manager.Create(Rectangle{});
	LayerHandle child2 = manager.Create(Rectangle{});
	LayerHandle child3 = manager.Create(Rectangle{});

	manager.AddChild(parent, child1);
	manager.AddChild(parent, child2);
	manager.AddChild(parent, child3);

	// Set z-indices out of order
	manager.SetZIndex(child1, 30.0f);
	manager.SetZIndex(child2, 10.0f);
	manager.SetZIndex(child3, 20.0f);

	// Sort children
	manager.SortChildren(parent);

	// Verify sorted order (10, 20, 30)
	const auto& children = manager.GetChildren(parent);
	EXPECT_EQ(manager.GetZIndex(children[0]), 10.0f); // child2
	EXPECT_EQ(manager.GetZIndex(children[1]), 20.0f); // child3
	EXPECT_EQ(manager.GetZIndex(children[2]), 30.0f); // child1
}

TEST(LayerManagerTest, SortOnlyWhenDirty) {
	LayerManager manager;

	LayerHandle parent = manager.Create(Rectangle{});
	LayerHandle child1 = manager.Create(Rectangle{});
	LayerHandle child2 = manager.Create(Rectangle{});

	manager.SetZIndex(child1, 10.0f);
	manager.SetZIndex(child2, 20.0f);

	manager.AddChild(parent, child1);
	manager.AddChild(parent, child2);

	// Manually clear dirty flag (children were added in order)
	auto& parentNode = manager.GetNode(parent);
	parentNode.childrenNeedSorting = false;

	// Sort should not reorder (dirty flag is false)
	manager.SortChildren(parent);
	EXPECT_FALSE(parentNode.childrenNeedSorting);

	// Now mark dirty and change order
	manager.SetZIndex(child1, 30.0f);
	EXPECT_TRUE(parentNode.childrenNeedSorting);

	// Sort should reorder
	manager.SortChildren(parent);
	EXPECT_FALSE(parentNode.childrenNeedSorting); // Cleared after sort

	const auto& children = manager.GetChildren(parent);
	EXPECT_EQ(children[0], child2); // 20.0f
	EXPECT_EQ(children[1], child1); // 30.0f
}

// ============================================================================
// Visibility Tests
// ============================================================================

TEST(LayerManagerTest, DefaultVisibility) {
	LayerManager manager;
	LayerHandle	 layer = manager.Create(Rectangle{});

	EXPECT_TRUE(manager.IsVisible(layer));
}

TEST(LayerManagerTest, SetVisibility) {
	LayerManager manager;
	LayerHandle	 layer = manager.Create(Rectangle{});

	manager.SetVisible(layer, false);
	EXPECT_FALSE(manager.IsVisible(layer));

	manager.SetVisible(layer, true);
	EXPECT_TRUE(manager.IsVisible(layer));
}

// ============================================================================
// Lifecycle Tests
// ============================================================================

TEST(LayerManagerTest, DestroyLayer) {
	LayerManager manager;

	LayerHandle parent = manager.Create(Rectangle{});
	LayerHandle child = manager.Create(Rectangle{});

	manager.AddChild(parent, child);

	// Destroy child
	manager.DestroyLayer(child);

	// Child should be removed from parent
	EXPECT_EQ(manager.GetNode(parent).childHandles.size(), 0);

	// Old child handle should now be invalid
	EXPECT_FALSE(manager.IsValidHandle(child));

	// New layer should reuse the index with incremented generation
	LayerHandle newLayer = manager.Create(Rectangle{});
	EXPECT_EQ(newLayer.GetIndex(), child.GetIndex()); // Reused index
	EXPECT_EQ(newLayer.GetGeneration(), 1);			  // Incremented generation
}

TEST(LayerManagerTest, DestroyLayerWithChildren) {
	LayerManager manager;

	LayerHandle root = manager.Create(Rectangle{});
	LayerHandle child1 = manager.Create(Rectangle{});
	LayerHandle child2 = manager.Create(Rectangle{});

	manager.AddChild(root, child1);
	manager.AddChild(child1, child2);

	// Destroy root (should destroy entire subtree)
	manager.DestroyLayer(root);

	// All handles should now be invalid
	EXPECT_FALSE(manager.IsValidHandle(root));
	EXPECT_FALSE(manager.IsValidHandle(child1));
	EXPECT_FALSE(manager.IsValidHandle(child2));

	// Creating new layers should reuse indices
	LayerHandle new1 = manager.Create(Rectangle{});
	LayerHandle new2 = manager.Create(Rectangle{});
	LayerHandle new3 = manager.Create(Rectangle{});

	// Should reuse destroyed indices (with incremented generations)
	EXPECT_TRUE(new1.GetIndex() <= 2);
	EXPECT_TRUE(new2.GetIndex() <= 2);
	EXPECT_TRUE(new3.GetIndex() <= 2);
}

TEST(LayerManagerTest, Clear) {
	LayerManager manager;

	manager.Create(Rectangle{});
	manager.Create(Rectangle{});
	manager.Create(Rectangle{});

	EXPECT_EQ(manager.GetLayerCount(), 3);

	manager.Clear();

	EXPECT_EQ(manager.GetLayerCount(), 0);
}

// ============================================================================
// Handle Validation Tests (Generation Tracking)
// ============================================================================

TEST(LayerManagerTest, HandleValidation) {
	LayerManager manager;

	LayerHandle handle = manager.Create(Rectangle{});
	EXPECT_TRUE(manager.IsValidHandle(handle));

	manager.DestroyLayer(handle);
	EXPECT_FALSE(manager.IsValidHandle(handle)); // Stale handle
}

TEST(LayerManagerTest, StaleHandleDetection) {
	LayerManager manager;

	// Create and destroy a layer
	LayerHandle oldHandle = manager.Create(Rectangle{});
	manager.DestroyLayer(oldHandle);

	// Create a new layer (should reuse the slot)
	LayerHandle newHandle = manager.Create(Rectangle{});

	// Both handles have same index, but different generations
	EXPECT_EQ(oldHandle.GetIndex(), newHandle.GetIndex());
	EXPECT_NE(oldHandle.GetGeneration(), newHandle.GetGeneration());

	// Old handle is stale, new handle is valid
	EXPECT_FALSE(manager.IsValidHandle(oldHandle));
	EXPECT_TRUE(manager.IsValidHandle(newHandle));
}

TEST(LayerManagerTest, GenerationIncrementsOnReuse) {
	LayerManager manager;

	// Create layer at index 0, generation 0
	LayerHandle first = manager.Create(Rectangle{});
	EXPECT_EQ(first.GetIndex(), 0);
	EXPECT_EQ(first.GetGeneration(), 0);

	// Destroy and recreate
	manager.DestroyLayer(first);
	LayerHandle second = manager.Create(Rectangle{});
	EXPECT_EQ(second.GetIndex(), 0);
	EXPECT_EQ(second.GetGeneration(), 1);

	// Destroy and recreate again
	manager.DestroyLayer(second);
	LayerHandle third = manager.Create(Rectangle{});
	EXPECT_EQ(third.GetIndex(), 0);
	EXPECT_EQ(third.GetGeneration(), 2);
}

TEST(LayerManagerTest, InvalidHandleRejected) {
	LayerManager manager;

	LayerHandle invalid = LayerHandle::Invalid();
	EXPECT_FALSE(manager.IsValidHandle(invalid));
}

// ============================================================================
// Rendering Tests
// ============================================================================

TEST(LayerManagerTest, RenderAllDoesNotCrash) {
	LayerManager manager;

	LayerHandle rect = manager.Create(Rectangle{});
	LayerHandle circ = manager.Create(Rectangle{});

	manager.AddChild(rect, circ);

	// Should not crash (we can't test actual rendering without GL context)
	EXPECT_NO_THROW(manager.RenderAll());
}

TEST(LayerManagerTest, RenderSubtree) {
	LayerManager manager;

	LayerHandle root = manager.Create(Rectangle{});
	LayerHandle child = manager.Create(Rectangle{});

	manager.AddChild(root, child);

	// Should not crash
	EXPECT_NO_THROW(manager.RenderSubtree(root));
}

// ============================================================================
// Update Tests
// ============================================================================

TEST(LayerManagerTest, UpdateAllDoesNotCrash) {
	LayerManager manager;

	manager.Create(Rectangle{});
	manager.Create(Rectangle{});

	EXPECT_NO_THROW(manager.UpdateAll(0.016f));
}

TEST(LayerManagerTest, UpdateSubtree) {
	LayerManager manager;

	LayerHandle root = manager.Create(Rectangle{});

	EXPECT_NO_THROW(manager.UpdateSubtree(root, 0.016f));
}

// ============================================================================
// Memory Layout Tests
// ============================================================================

TEST(LayerManagerTest, ContiguousStorage) {
	LayerManager manager;

	// Create many layers
	constexpr size_t count = 100;
	for (size_t i = 0; i < count; ++i) {
		manager.Create(Rectangle{});
	}

	EXPECT_EQ(manager.GetLayerCount(), count);

	// Verify all handles are valid
	for (uint16_t i = 0; i < count; ++i) {
		LayerHandle handle = LayerHandle::Make(i, 0);
		EXPECT_TRUE(manager.IsValidHandle(handle));
	}
}

TEST(LayerManagerTest, FreeListReuse) {
	LayerManager manager;

	LayerHandle first = manager.Create(Rectangle{});
	LayerHandle second = manager.Create(Rectangle{});
	LayerHandle third = manager.Create(Rectangle{});

	EXPECT_EQ(first.GetIndex(), 0);
	EXPECT_EQ(second.GetIndex(), 1);
	EXPECT_EQ(third.GetIndex(), 2);

	// Destroy second
	manager.DestroyLayer(second);

	// Create new - should reuse second's index
	LayerHandle reused = manager.Create(Rectangle{});
	EXPECT_EQ(reused.GetIndex(), second.GetIndex());
	EXPECT_EQ(reused.GetIndex(), 1);
	EXPECT_EQ(reused.GetGeneration(), 1); // Incremented from 0
}
