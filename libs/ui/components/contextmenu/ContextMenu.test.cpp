#include "ContextMenu.h"

#include "focus/FocusManager.h"

#include <gtest/gtest.h>

namespace UI {

class ContextMenuTest : public ::testing::Test {
  protected:
	FocusManager focusManager;

	void SetUp() override { FocusManager::setInstance(&focusManager); }

	void TearDown() override { FocusManager::setInstance(nullptr); }
};

TEST_F(ContextMenuTest, ConstructsWithItems) {
	bool called = false;
	ContextMenu menu(ContextMenu::Args{
		.items = {{.label = "Item 1", .onSelect = [&] { called = true; }},
				  {.label = "Item 2", .onSelect = [] {}}},
	});

	EXPECT_EQ(menu.getItems().size(), 2);
	EXPECT_EQ(menu.getItems()[0].label, "Item 1");
	EXPECT_EQ(menu.getItems()[1].label, "Item 2");
}

TEST_F(ContextMenuTest, StartsClosedAndInvisible) {
	ContextMenu menu(ContextMenu::Args{
		.items = {{.label = "Test", .onSelect = [] {}}},
	});

	EXPECT_FALSE(menu.isOpen());
	EXPECT_FALSE(menu.visible);
}

TEST_F(ContextMenuTest, OpensAtPosition) {
	ContextMenu menu(ContextMenu::Args{
		.items = {{.label = "Test", .onSelect = [] {}}},
	});

	menu.openAt({100.0F, 100.0F}, 800.0F, 600.0F);

	EXPECT_TRUE(menu.isOpen());
	EXPECT_TRUE(menu.visible);
}

TEST_F(ContextMenuTest, CloseFiresCallback) {
	bool closeCalled = false;
	ContextMenu menu(ContextMenu::Args{
		.items = {{.label = "Test", .onSelect = [] {}}},
		.onClose = [&] { closeCalled = true; },
	});

	menu.openAt({100.0F, 100.0F}, 800.0F, 600.0F);
	menu.close();

	EXPECT_TRUE(closeCalled);
	EXPECT_FALSE(menu.isOpen());
}

TEST_F(ContextMenuTest, ClickOutsideCloses) {
	bool closeCalled = false;
	ContextMenu menu(ContextMenu::Args{
		.items = {{.label = "Test", .onSelect = [] {}}},
		.onClose = [&] { closeCalled = true; },
	});

	menu.openAt({100.0F, 100.0F}, 800.0F, 600.0F);

	// Click far outside the menu
	InputEvent event{.type = InputEvent::Type::MouseDown, .position = {500.0F, 500.0F}};
	menu.handleEvent(event);

	EXPECT_TRUE(closeCalled);
	EXPECT_FALSE(menu.isOpen());
}

TEST_F(ContextMenuTest, ClickInsideSelectsItem) {
	bool item1Selected = false;
	ContextMenu menu(ContextMenu::Args{
		.items = {{.label = "Item 1", .onSelect = [&] { item1Selected = true; }},
				  {.label = "Item 2", .onSelect = [] {}}},
	});

	menu.openAt({100.0F, 100.0F}, 800.0F, 600.0F);

	// Click on first item (inside menu at position after padding)
	InputEvent mouseDown{.type = InputEvent::Type::MouseDown,
						 .position = {110.0F, 110.0F}}; // Inside first item
	menu.handleEvent(mouseDown);

	InputEvent mouseUp{.type = InputEvent::Type::MouseUp, .position = {110.0F, 110.0F}};
	menu.handleEvent(mouseUp);

	EXPECT_TRUE(item1Selected);
	EXPECT_FALSE(menu.isOpen()); // Menu closes after selection
}

TEST_F(ContextMenuTest, DisabledItemNotSelectable) {
	bool itemSelected = false;
	ContextMenu menu(ContextMenu::Args{
		.items = {{.label = "Disabled", .onSelect = [&] { itemSelected = true; }, .enabled = false}},
	});

	menu.openAt({100.0F, 100.0F}, 800.0F, 600.0F);

	// Click on disabled item
	InputEvent mouseDown{.type = InputEvent::Type::MouseDown, .position = {110.0F, 110.0F}};
	menu.handleEvent(mouseDown);

	InputEvent mouseUp{.type = InputEvent::Type::MouseUp, .position = {110.0F, 110.0F}};
	menu.handleEvent(mouseUp);

	EXPECT_FALSE(itemSelected);
}

TEST_F(ContextMenuTest, MouseMoveUpdatesHoveredIndex) {
	ContextMenu menu(ContextMenu::Args{
		.items = {{.label = "Item 1", .onSelect = [] {}}, {.label = "Item 2", .onSelect = [] {}}},
	});

	menu.openAt({100.0F, 100.0F}, 800.0F, 600.0F);

	EXPECT_EQ(menu.getHoveredIndex(), -1); // Initially no hover

	// Move mouse over first item
	InputEvent moveEvent{.type = InputEvent::Type::MouseMove, .position = {110.0F, 110.0F}};
	menu.handleEvent(moveEvent);

	EXPECT_EQ(menu.getHoveredIndex(), 0);
}

TEST_F(ContextMenuTest, ClampsToRightEdge) {
	ContextMenu menu(ContextMenu::Args{
		.items = {{.label = "Test", .onSelect = [] {}}},
	});

	// Open near right edge of 800px screen
	menu.openAt({750.0F, 100.0F}, 800.0F, 600.0F);

	// Menu should be clamped to stay on screen
	EXPECT_LE(menu.position.x + Theme::ContextMenu::minWidth, 800.0F);
}

TEST_F(ContextMenuTest, ClampsToBottomEdge) {
	ContextMenu menu(ContextMenu::Args{
		.items = {{.label = "Test1", .onSelect = [] {}},
				  {.label = "Test2", .onSelect = [] {}},
				  {.label = "Test3", .onSelect = [] {}}},
	});

	// Open near bottom edge of 600px screen
	menu.openAt({100.0F, 580.0F}, 800.0F, 600.0F);

	// Menu should be clamped to stay on screen
	float menuHeight =
		3.0F * Theme::ContextMenu::itemHeight + Theme::ContextMenu::padding * 2;
	EXPECT_LE(menu.position.y + menuHeight, 600.0F);
}

TEST_F(ContextMenuTest, KeyboardEscapeCloses) {
	bool closeCalled = false;
	ContextMenu menu(ContextMenu::Args{
		.items = {{.label = "Test", .onSelect = [] {}}},
		.onClose = [&] { closeCalled = true; },
	});

	menu.openAt({100.0F, 100.0F}, 800.0F, 600.0F);

	menu.handleKeyInput(engine::Key::Escape, false, false, false);

	EXPECT_TRUE(closeCalled);
	EXPECT_FALSE(menu.isOpen());
}

TEST_F(ContextMenuTest, KeyboardNavigationArrowDown) {
	ContextMenu menu(ContextMenu::Args{
		.items = {{.label = "Item 1", .onSelect = [] {}},
				  {.label = "Item 2", .onSelect = [] {}},
				  {.label = "Item 3", .onSelect = [] {}}},
	});

	menu.openAt({100.0F, 100.0F}, 800.0F, 600.0F);

	EXPECT_EQ(menu.getHoveredIndex(), -1);

	menu.handleKeyInput(engine::Key::Down, false, false, false);
	EXPECT_EQ(menu.getHoveredIndex(), 0);

	menu.handleKeyInput(engine::Key::Down, false, false, false);
	EXPECT_EQ(menu.getHoveredIndex(), 1);

	menu.handleKeyInput(engine::Key::Down, false, false, false);
	EXPECT_EQ(menu.getHoveredIndex(), 2);

	// Wrap around
	menu.handleKeyInput(engine::Key::Down, false, false, false);
	EXPECT_EQ(menu.getHoveredIndex(), 0);
}

TEST_F(ContextMenuTest, KeyboardNavigationArrowUp) {
	ContextMenu menu(ContextMenu::Args{
		.items = {{.label = "Item 1", .onSelect = [] {}},
				  {.label = "Item 2", .onSelect = [] {}},
				  {.label = "Item 3", .onSelect = [] {}}},
	});

	menu.openAt({100.0F, 100.0F}, 800.0F, 600.0F);

	menu.handleKeyInput(engine::Key::Up, false, false, false);
	EXPECT_EQ(menu.getHoveredIndex(), 2); // Wraps to end

	menu.handleKeyInput(engine::Key::Up, false, false, false);
	EXPECT_EQ(menu.getHoveredIndex(), 1);
}

TEST_F(ContextMenuTest, KeyboardEnterSelectsItem) {
	bool itemSelected = false;
	ContextMenu menu(ContextMenu::Args{
		.items = {{.label = "Item 1", .onSelect = [&] { itemSelected = true; }},
				  {.label = "Item 2", .onSelect = [] {}}},
	});

	menu.openAt({100.0F, 100.0F}, 800.0F, 600.0F);

	// Navigate to first item
	menu.handleKeyInput(engine::Key::Down, false, false, false);

	// Press Enter
	menu.handleKeyInput(engine::Key::Enter, false, false, false);

	EXPECT_TRUE(itemSelected);
	EXPECT_FALSE(menu.isOpen());
}

TEST_F(ContextMenuTest, KeyboardSkipsDisabledItems) {
	ContextMenu menu(ContextMenu::Args{
		.items = {{.label = "Item 1", .onSelect = [] {}, .enabled = true},
				  {.label = "Disabled", .onSelect = [] {}, .enabled = false},
				  {.label = "Item 3", .onSelect = [] {}, .enabled = true}},
	});

	menu.openAt({100.0F, 100.0F}, 800.0F, 600.0F);

	menu.handleKeyInput(engine::Key::Down, false, false, false);
	EXPECT_EQ(menu.getHoveredIndex(), 0);

	menu.handleKeyInput(engine::Key::Down, false, false, false);
	EXPECT_EQ(menu.getHoveredIndex(), 2); // Skips index 1 (disabled)
}

TEST_F(ContextMenuTest, DoesNotHandleEventsWhenClosed) {
	ContextMenu menu(ContextMenu::Args{
		.items = {{.label = "Test", .onSelect = [] {}}},
	});

	// Menu is closed
	InputEvent event{.type = InputEvent::Type::MouseDown, .position = {110.0F, 110.0F}};
	bool handled = menu.handleEvent(event);

	EXPECT_FALSE(handled);
}

TEST_F(ContextMenuTest, VerifyCanReceiveFocus) {
	ContextMenu menu(ContextMenu::Args{
		.items = {{.label = "Test", .onSelect = [] {}}},
	});

	// Menu is closed - cannot receive focus
	EXPECT_FALSE(menu.canReceiveFocus());

	// Open menu - can receive focus
	menu.openAt({100.0F, 100.0F}, 800.0F, 600.0F);
	EXPECT_TRUE(menu.canReceiveFocus());
}

} // namespace UI
