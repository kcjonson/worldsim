#include "DropdownButton.h"

#include "focus/FocusManager.h"
#include <gtest/gtest.h>

namespace UI {

class DropdownButtonTest : public ::testing::Test {
  protected:
	FocusManager focusManager;

	void SetUp() override {
		// Register our test FocusManager as the singleton instance
		FocusManager::setInstance(&focusManager);
	}

	void TearDown() override {
		// Clear the singleton instance
		FocusManager::setInstance(nullptr);
	}

	// Helper to create test items
	std::vector<DropdownItem> createTestItems() {
		return {
			DropdownItem{.label = "Item 1", .onSelect = []() {}, .enabled = true},
			DropdownItem{.label = "Item 2", .onSelect = []() {}, .enabled = true},
			DropdownItem{.label = "Disabled", .onSelect = []() {}, .enabled = false},
		};
	}
};

// === Construction Tests ===

TEST_F(DropdownButtonTest, ConstructsWithDefaults) {
	DropdownButton dropdown(DropdownButton::Args{
		.label = "Test",
	});

	EXPECT_EQ(dropdown.getLabel(), "Test");
	EXPECT_FALSE(dropdown.isOpen());
	EXPECT_FLOAT_EQ(dropdown.getWidth(), 120.0F);  // Default buttonSize.x
	EXPECT_FLOAT_EQ(dropdown.getHeight(), 36.0F); // Default buttonSize.y
	EXPECT_TRUE(dropdown.getItems().empty());
}

TEST_F(DropdownButtonTest, ConstructsWithCustomSize) {
	DropdownButton dropdown(DropdownButton::Args{
		.label = "Actions",
		.buttonSize = {150.0F, 40.0F},
	});

	EXPECT_FLOAT_EQ(dropdown.getWidth(), 150.0F);
	EXPECT_FLOAT_EQ(dropdown.getHeight(), 40.0F);
}

TEST_F(DropdownButtonTest, ConstructsWithMargin) {
	DropdownButton dropdown(DropdownButton::Args{
		.label = "Test",
		.buttonSize = {100.0F, 30.0F},
		.margin = 5.0F,
	});

	// getWidth/getHeight include margin on both sides
	EXPECT_FLOAT_EQ(dropdown.getWidth(), 110.0F);  // 100 + 5*2
	EXPECT_FLOAT_EQ(dropdown.getHeight(), 40.0F); // 30 + 5*2
}

TEST_F(DropdownButtonTest, ConstructsWithItems) {
	DropdownButton dropdown(DropdownButton::Args{
		.label = "Test",
		.items = createTestItems(),
	});

	EXPECT_EQ(dropdown.getItems().size(), 3);
}

// === State Tests ===

TEST_F(DropdownButtonTest, OpenMenuOpensDropdown) {
	DropdownButton dropdown(DropdownButton::Args{
		.label = "Test",
		.items = createTestItems(),
	});

	EXPECT_FALSE(dropdown.isOpen());
	dropdown.openMenu();
	EXPECT_TRUE(dropdown.isOpen());
}

TEST_F(DropdownButtonTest, CloseMenuClosesDropdown) {
	DropdownButton dropdown(DropdownButton::Args{
		.label = "Test",
		.items = createTestItems(),
	});

	dropdown.openMenu();
	EXPECT_TRUE(dropdown.isOpen());

	dropdown.closeMenu();
	EXPECT_FALSE(dropdown.isOpen());
}

TEST_F(DropdownButtonTest, ToggleFlipsState) {
	DropdownButton dropdown(DropdownButton::Args{
		.label = "Test",
		.items = createTestItems(),
	});

	EXPECT_FALSE(dropdown.isOpen());
	dropdown.toggle();
	EXPECT_TRUE(dropdown.isOpen());
	dropdown.toggle();
	EXPECT_FALSE(dropdown.isOpen());
}

TEST_F(DropdownButtonTest, OpenMenuWithNoItemsDoesNothing) {
	DropdownButton dropdown(DropdownButton::Args{
		.label = "Test",
		// No items
	});

	dropdown.openMenu();
	EXPECT_FALSE(dropdown.isOpen()); // Should stay closed
}

// === Items Tests ===

TEST_F(DropdownButtonTest, SetItemsUpdatesItems) {
	DropdownButton dropdown(DropdownButton::Args{
		.label = "Test",
	});

	EXPECT_TRUE(dropdown.getItems().empty());

	dropdown.setItems(createTestItems());
	EXPECT_EQ(dropdown.getItems().size(), 3);
}

TEST_F(DropdownButtonTest, SetItemsToEmptyClosesMenu) {
	DropdownButton dropdown(DropdownButton::Args{
		.label = "Test",
		.items = createTestItems(),
	});

	dropdown.openMenu();
	EXPECT_TRUE(dropdown.isOpen());

	dropdown.setItems({});
	EXPECT_FALSE(dropdown.isOpen());
}

// === Hit Testing ===

TEST_F(DropdownButtonTest, ContainsPointInButtonBounds) {
	DropdownButton dropdown(DropdownButton::Args{
		.label = "Test",
		.position = {100.0F, 100.0F},
		.buttonSize = {120.0F, 36.0F},
	});

	// Inside button
	EXPECT_TRUE(dropdown.containsPoint({150.0F, 118.0F}));
	EXPECT_TRUE(dropdown.containsPoint({100.0F, 100.0F})); // Top-left corner

	// Outside button
	EXPECT_FALSE(dropdown.containsPoint({50.0F, 118.0F}));	// Left of
	EXPECT_FALSE(dropdown.containsPoint({250.0F, 118.0F})); // Right of
	EXPECT_FALSE(dropdown.containsPoint({150.0F, 50.0F}));	// Above
	EXPECT_FALSE(dropdown.containsPoint({150.0F, 200.0F})); // Below (menu not open)
}

TEST_F(DropdownButtonTest, ContainsPointIncludesMenuWhenOpen) {
	DropdownButton dropdown(DropdownButton::Args{
		.label = "Test",
		.position = {100.0F, 100.0F},
		.buttonSize = {120.0F, 36.0F},
		.items = createTestItems(), // 3 items
	});

	dropdown.openMenu();

	// Button still contains points
	EXPECT_TRUE(dropdown.containsPoint({150.0F, 118.0F}));

	// Menu area (below button) now also contains points
	// Menu starts at y=136 (100 + 36)
	EXPECT_TRUE(dropdown.containsPoint({150.0F, 150.0F}));
}

// === Label Tests ===

TEST_F(DropdownButtonTest, SetLabelUpdatesLabel) {
	DropdownButton dropdown(DropdownButton::Args{
		.label = "Original",
	});

	EXPECT_EQ(dropdown.getLabel(), "Original");

	dropdown.setLabel("Changed");
	EXPECT_EQ(dropdown.getLabel(), "Changed");
}

// === Position Tests ===

TEST_F(DropdownButtonTest, SetPositionUpdatesBase) {
	DropdownButton dropdown(DropdownButton::Args{
		.label = "Test",
		.position = {10.0F, 20.0F},
	});

	dropdown.setPosition(50.0F, 60.0F);

	Foundation::Vec2 contentPos = dropdown.getContentPosition();
	EXPECT_FLOAT_EQ(contentPos.x, 50.0F);
	EXPECT_FLOAT_EQ(contentPos.y, 60.0F);
}

TEST_F(DropdownButtonTest, SetPositionWithMargin) {
	DropdownButton dropdown(DropdownButton::Args{
		.label = "Test",
		.position = {0.0F, 0.0F},
		.margin = 8.0F,
	});

	dropdown.setPosition(100.0F, 200.0F);

	Foundation::Vec2 contentPos = dropdown.getContentPosition();
	EXPECT_FLOAT_EQ(contentPos.x, 108.0F); // 100 + 8 margin
	EXPECT_FLOAT_EQ(contentPos.y, 208.0F); // 200 + 8 margin
}

// === Focus Tests ===

TEST_F(DropdownButtonTest, CanReceiveFocusWhenVisible) {
	DropdownButton dropdown(DropdownButton::Args{
		.label = "Test",
	});

	EXPECT_TRUE(dropdown.canReceiveFocus());

	dropdown.visible = false;
	EXPECT_FALSE(dropdown.canReceiveFocus());
}

TEST_F(DropdownButtonTest, FocusLostClosesMenu) {
	DropdownButton dropdown(DropdownButton::Args{
		.label = "Test",
		.items = createTestItems(),
	});

	dropdown.openMenu();
	EXPECT_TRUE(dropdown.isOpen());

	dropdown.onFocusLost();
	EXPECT_FALSE(dropdown.isOpen());
}

} // namespace UI
