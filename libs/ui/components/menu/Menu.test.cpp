#include "Menu.h"

#include <gtest/gtest.h>

namespace UI {

class MenuTest : public ::testing::Test {
  protected:
	// Helper to create test items
	std::vector<MenuItem> createTestItems() {
		return {
			MenuItem{.label = "Item 1", .onSelect = []() {}, .enabled = true},
			MenuItem{.label = "Item 2", .onSelect = []() {}, .enabled = true},
			MenuItem{.label = "Disabled", .onSelect = []() {}, .enabled = false},
		};
	}
};

// === Construction Tests ===

TEST_F(MenuTest, ConstructsWithDefaults) {
	Menu menu(Menu::Args{
		.position = {100.0F, 100.0F},
	});

	EXPECT_FLOAT_EQ(menu.getMenuWidth(), 150.0F); // Default width
	EXPECT_TRUE(menu.getItems().empty());
	EXPECT_EQ(menu.getHoveredIndex(), -1);
	EXPECT_EQ(menu.getItemCount(), 0);
}

TEST_F(MenuTest, ConstructsWithCustomWidth) {
	Menu menu(Menu::Args{
		.position = {0.0F, 0.0F},
		.width = 200.0F,
	});

	EXPECT_FLOAT_EQ(menu.getMenuWidth(), 200.0F);
}

TEST_F(MenuTest, ConstructsWithItems) {
	Menu menu(Menu::Args{
		.position = {0.0F, 0.0F},
		.items = createTestItems(),
	});

	EXPECT_EQ(menu.getItems().size(), 3);
	EXPECT_EQ(menu.getItemCount(), 3);
}

TEST_F(MenuTest, ConstructsWithInitialHoveredIndex) {
	Menu menu(Menu::Args{
		.position = {0.0F, 0.0F},
		.items = createTestItems(),
		.hoveredIndex = 1,
	});

	EXPECT_EQ(menu.getHoveredIndex(), 1);
}

// === Items Tests ===

TEST_F(MenuTest, SetItemsUpdatesItems) {
	Menu menu(Menu::Args{
		.position = {0.0F, 0.0F},
	});

	EXPECT_TRUE(menu.getItems().empty());

	menu.setItems(createTestItems());
	EXPECT_EQ(menu.getItems().size(), 3);
}

TEST_F(MenuTest, SetItemsToEmptyWorks) {
	Menu menu(Menu::Args{
		.position = {0.0F, 0.0F},
		.items = createTestItems(),
	});

	EXPECT_EQ(menu.getItemCount(), 3);

	menu.setItems({});
	EXPECT_EQ(menu.getItemCount(), 0);
}

// === Dimensions Tests ===

TEST_F(MenuTest, GetMenuHeightCalculatesFromItems) {
	Menu menu(Menu::Args{
		.position = {0.0F, 0.0F},
		.items = createTestItems(), // 3 items
	});

	// Menu height = items * itemHeight + padding*2
	// itemHeight is Theme::Dropdown::menuItemHeight (30.0F)
	// padding is 4.0F
	float expectedHeight = 3.0F * 30.0F + 4.0F * 2;
	EXPECT_FLOAT_EQ(menu.getMenuHeight(), expectedHeight);
}

TEST_F(MenuTest, GetMenuHeightWithNoItemsHasPaddingOnly) {
	Menu menu(Menu::Args{
		.position = {0.0F, 0.0F},
	});

	// Empty menu just has padding
	EXPECT_FLOAT_EQ(menu.getMenuHeight(), 8.0F); // 4.0 * 2
}

TEST_F(MenuTest, SetWidthUpdatesWidth) {
	Menu menu(Menu::Args{
		.position = {0.0F, 0.0F},
		.width = 150.0F,
	});

	menu.setWidth(250.0F);
	EXPECT_FLOAT_EQ(menu.getMenuWidth(), 250.0F);
}

TEST_F(MenuTest, GetBoundsIncludesPositionAndSize) {
	Menu menu(Menu::Args{
		.position = {50.0F, 100.0F},
		.width = 200.0F,
		.items = createTestItems(),
	});

	Foundation::Rect bounds = menu.getBounds();
	EXPECT_FLOAT_EQ(bounds.x, 50.0F);
	EXPECT_FLOAT_EQ(bounds.y, 100.0F);
	EXPECT_FLOAT_EQ(bounds.width, 200.0F);
}

// === Hit Testing ===

TEST_F(MenuTest, ContainsPointInsideBounds) {
	Menu menu(Menu::Args{
		.position = {100.0F, 100.0F},
		.width = 150.0F,
		.items = createTestItems(),
	});

	// Inside menu
	EXPECT_TRUE(menu.containsPoint({150.0F, 120.0F}));
	EXPECT_TRUE(menu.containsPoint({100.0F, 100.0F})); // Top-left corner
}

TEST_F(MenuTest, ContainsPointOutsideBounds) {
	Menu menu(Menu::Args{
		.position = {100.0F, 100.0F},
		.width = 150.0F,
		.items = createTestItems(),
	});

	// Outside menu
	EXPECT_FALSE(menu.containsPoint({50.0F, 120.0F}));	// Left of
	EXPECT_FALSE(menu.containsPoint({300.0F, 120.0F})); // Right of
	EXPECT_FALSE(menu.containsPoint({150.0F, 50.0F}));	// Above
	EXPECT_FALSE(menu.containsPoint({150.0F, 300.0F})); // Below
}

TEST_F(MenuTest, GetItemAtPointReturnsCorrectIndex) {
	Menu menu(Menu::Args{
		.position = {100.0F, 100.0F},
		.width = 150.0F,
		.items = createTestItems(),
	});

	// Item 0 starts at y = 100 + 4 (padding), itemHeight = 30.0F
	EXPECT_EQ(menu.getItemAtPoint({150.0F, 110.0F}), 0);

	// Item 1 starts at y = 100 + 4 + 30
	EXPECT_EQ(menu.getItemAtPoint({150.0F, 140.0F}), 1);

	// Item 2 starts at y = 100 + 4 + 60
	EXPECT_EQ(menu.getItemAtPoint({150.0F, 170.0F}), 2);
}

TEST_F(MenuTest, GetItemAtPointReturnsNegativeWhenOutside) {
	Menu menu(Menu::Args{
		.position = {100.0F, 100.0F},
		.width = 150.0F,
		.items = createTestItems(),
	});

	EXPECT_EQ(menu.getItemAtPoint({50.0F, 120.0F}), -1); // Outside menu
}

TEST_F(MenuTest, GetItemAtPointReturnsNegativeWhenEmpty) {
	Menu menu(Menu::Args{
		.position = {100.0F, 100.0F},
	});

	EXPECT_EQ(menu.getItemAtPoint({150.0F, 120.0F}), -1);
}

TEST_F(MenuTest, GetItemBoundsReturnsCorrectBounds) {
	Menu menu(Menu::Args{
		.position = {100.0F, 100.0F},
		.width = 150.0F,
		.items = createTestItems(),
	});

	Foundation::Rect itemBounds = menu.getItemBounds(0);
	EXPECT_FLOAT_EQ(itemBounds.x, 104.0F);				// 100 + 4 padding
	EXPECT_FLOAT_EQ(itemBounds.y, 104.0F);				// 100 + 4 padding
	EXPECT_FLOAT_EQ(itemBounds.width, 142.0F);			// 150 - 4*2 padding
	EXPECT_FLOAT_EQ(itemBounds.height, 30.0F);			// menuItemHeight
}

// === Hovered Index Tests ===

TEST_F(MenuTest, SetHoveredIndexUpdatesIndex) {
	Menu menu(Menu::Args{
		.position = {0.0F, 0.0F},
		.items = createTestItems(),
	});

	EXPECT_EQ(menu.getHoveredIndex(), -1);

	menu.setHoveredIndex(1);
	EXPECT_EQ(menu.getHoveredIndex(), 1);

	menu.setHoveredIndex(-1);
	EXPECT_EQ(menu.getHoveredIndex(), -1);
}

// === Selection Tests ===

TEST_F(MenuTest, SelectItemCallsOnSelect) {
	bool wasSelected = false;

	Menu menu(Menu::Args{
		.position = {0.0F, 0.0F},
		.items = {
			MenuItem{.label = "Test", .onSelect = [&wasSelected]() { wasSelected = true; }, .enabled = true},
		},
	});

	EXPECT_FALSE(wasSelected);
	menu.selectItem(0);
	EXPECT_TRUE(wasSelected);
}

TEST_F(MenuTest, SelectDisabledItemDoesNothing) {
	bool wasSelected = false;

	Menu menu(Menu::Args{
		.position = {0.0F, 0.0F},
		.items = {
			MenuItem{.label = "Disabled", .onSelect = [&wasSelected]() { wasSelected = true; }, .enabled = false},
		},
	});

	menu.selectItem(0);
	EXPECT_FALSE(wasSelected); // Should not be called
}

TEST_F(MenuTest, SelectItemOutOfRangeDoesNothing) {
	bool wasSelected = false;

	Menu menu(Menu::Args{
		.position = {0.0F, 0.0F},
		.items = {
			MenuItem{.label = "Test", .onSelect = [&wasSelected]() { wasSelected = true; }, .enabled = true},
		},
	});

	menu.selectItem(5); // Out of range
	EXPECT_FALSE(wasSelected);
}

// === Visibility Tests ===

TEST_F(MenuTest, VisibilityDefaultsToTrue) {
	Menu menu(Menu::Args{
		.position = {0.0F, 0.0F},
	});

	EXPECT_TRUE(menu.visible);
}

TEST_F(MenuTest, ContainsPointReturnsFalseWhenNotVisible) {
	Menu menu(Menu::Args{
		.position = {100.0F, 100.0F},
		.width = 150.0F,
		.items = createTestItems(),
	});

	menu.visible = false;

	// Should not hit test when not visible (based on handleEvent behavior)
	// containsPoint itself doesn't check visibility, but handleEvent does
	EXPECT_TRUE(menu.containsPoint({150.0F, 120.0F})); // Still returns true
}

// === Event Handling Tests ===

TEST_F(MenuTest, HandleEventIgnoresWhenNotVisible) {
	Menu menu(Menu::Args{
		.position = {100.0F, 100.0F},
		.items = createTestItems(),
	});

	menu.visible = false;

	InputEvent event{
		.type = InputEvent::Type::MouseMove,
		.position = {150.0F, 120.0F},
	};

	EXPECT_FALSE(menu.handleEvent(event));
}

TEST_F(MenuTest, HandleEventIgnoresWhenEmpty) {
	Menu menu(Menu::Args{
		.position = {100.0F, 100.0F},
	});

	InputEvent event{
		.type = InputEvent::Type::MouseMove,
		.position = {150.0F, 120.0F},
	};

	EXPECT_FALSE(menu.handleEvent(event));
}

TEST_F(MenuTest, MouseMoveUpdatesHoveredIndex) {
	Menu menu(Menu::Args{
		.position = {100.0F, 100.0F},
		.items = createTestItems(),
	});

	EXPECT_EQ(menu.getHoveredIndex(), -1);

	InputEvent event{
		.type = InputEvent::Type::MouseMove,
		.position = {150.0F, 110.0F}, // Over item 0
	};

	menu.handleEvent(event);
	EXPECT_EQ(menu.getHoveredIndex(), 0);
}

TEST_F(MenuTest, MouseUpSelectsItem) {
	bool wasSelected = false;

	Menu menu(Menu::Args{
		.position = {100.0F, 100.0F},
		.items = {
			MenuItem{.label = "Test", .onSelect = [&wasSelected]() { wasSelected = true; }, .enabled = true},
		},
	});

	InputEvent event{
		.type = InputEvent::Type::MouseUp,
		.position = {150.0F, 110.0F}, // Over item 0
	};

	EXPECT_TRUE(menu.handleEvent(event));
	EXPECT_TRUE(wasSelected);
}

} // namespace UI
