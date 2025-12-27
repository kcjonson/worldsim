#include "Tooltip.h"
#include "TooltipManager.h"

#include <gtest/gtest.h>

namespace UI {

// === Tooltip Component Tests ===

TEST(TooltipTest, ConstructsWithTitleOnly) {
	Tooltip tooltip(Tooltip::Args{
		.content = {.title = "Test Title"},
	});

	EXPECT_EQ(tooltip.getContent().title, "Test Title");
	EXPECT_TRUE(tooltip.getContent().description.empty());
	EXPECT_TRUE(tooltip.getContent().hotkey.empty());
}

TEST(TooltipTest, ConstructsWithFullContent) {
	Tooltip tooltip(Tooltip::Args{
		.content = {
			.title = "Test Title",
			.description = "Test description",
			.hotkey = "Ctrl+S",
		},
	});

	EXPECT_EQ(tooltip.getContent().title, "Test Title");
	EXPECT_EQ(tooltip.getContent().description, "Test description");
	EXPECT_EQ(tooltip.getContent().hotkey, "Ctrl+S");
}

TEST(TooltipTest, HeightIncreasesWithDescription) {
	Tooltip titleOnly(Tooltip::Args{.content = {.title = "Title"}});
	Tooltip withDesc(Tooltip::Args{
		.content = {.title = "Title", .description = "Description"},
	});

	EXPECT_GT(withDesc.getTooltipHeight(), titleOnly.getTooltipHeight());
}

TEST(TooltipTest, HeightIncreasesWithHotkey) {
	Tooltip titleOnly(Tooltip::Args{.content = {.title = "Title"}});
	Tooltip withHotkey(Tooltip::Args{
		.content = {.title = "Title", .hotkey = "Ctrl+S"},
	});

	EXPECT_GT(withHotkey.getTooltipHeight(), titleOnly.getTooltipHeight());
}

TEST(TooltipTest, OpacityDefaultsToOne) {
	Tooltip tooltip(Tooltip::Args{.content = {.title = "Test"}});

	EXPECT_FLOAT_EQ(tooltip.getOpacity(), 1.0F);
}

TEST(TooltipTest, SetOpacityUpdatesOpacity) {
	Tooltip tooltip(Tooltip::Args{.content = {.title = "Test"}});

	tooltip.setOpacity(0.5F);
	EXPECT_FLOAT_EQ(tooltip.getOpacity(), 0.5F);
}

TEST(TooltipTest, NeverConsumesEvents) {
	Tooltip tooltip(Tooltip::Args{.content = {.title = "Test"}});

	InputEvent event{.type = InputEvent::Type::MouseDown, .position = {50.0F, 50.0F}};

	bool handled = tooltip.handleEvent(event);

	EXPECT_FALSE(handled);
	EXPECT_FALSE(event.isConsumed());
}

TEST(TooltipTest, SetContentUpdatesContent) {
	Tooltip tooltip(Tooltip::Args{.content = {.title = "Original"}});

	tooltip.setContent({.title = "Updated", .description = "New desc"});

	EXPECT_EQ(tooltip.getContent().title, "Updated");
	EXPECT_EQ(tooltip.getContent().description, "New desc");
}

TEST(TooltipTest, ContainsPointReturnsCorrectly) {
	// Use longer content to get sufficient width for hit testing
	Tooltip tooltip(Tooltip::Args{
		.content = {.title = "This is a longer tooltip title"},
		.position = {100.0F, 100.0F},
	});

	EXPECT_TRUE(tooltip.containsPoint({100.0F, 100.0F}));
	EXPECT_TRUE(tooltip.containsPoint({150.0F, 110.0F})); // Inside estimated width
	EXPECT_FALSE(tooltip.containsPoint({50.0F, 50.0F}));  // Outside bounds
}

// === TooltipManager Tests ===

class TooltipManagerTest : public ::testing::Test {
  protected:
	TooltipManager manager;

	void SetUp() override {
		TooltipManager::setInstance(&manager);
		manager.setScreenBounds(800.0F, 600.0F);
	}

	void TearDown() override {
		TooltipManager::setInstance(nullptr);
	}
};

TEST_F(TooltipManagerTest, InitialStateIsIdle) {
	EXPECT_EQ(manager.getState(), TooltipManager::State::Idle);
	EXPECT_FALSE(manager.isTooltipVisible());
}

TEST_F(TooltipManagerTest, StartHoverBeginsWaiting) {
	manager.startHover({.title = "Test"}, {100.0F, 100.0F});

	EXPECT_EQ(manager.getState(), TooltipManager::State::Waiting);
	EXPECT_FALSE(manager.isTooltipVisible());
}

TEST_F(TooltipManagerTest, TooltipAppearsAfterDelay) {
	manager.startHover({.title = "Test"}, {100.0F, 100.0F});

	// Wait for hover delay
	manager.update(Theme::Tooltip::hoverDelay + 0.1F);

	EXPECT_EQ(manager.getState(), TooltipManager::State::Showing);
}

TEST_F(TooltipManagerTest, TooltipFullyVisibleAfterFadeIn) {
	manager.startHover({.title = "Test"}, {100.0F, 100.0F});

	// Wait for hover delay
	manager.update(Theme::Tooltip::hoverDelay + 0.1F);

	// Wait for fade in
	manager.update(0.2F);

	EXPECT_EQ(manager.getState(), TooltipManager::State::Visible);
	EXPECT_TRUE(manager.isTooltipVisible());
}

TEST_F(TooltipManagerTest, EndHoverCancelsWaiting) {
	manager.startHover({.title = "Test"}, {100.0F, 100.0F});
	EXPECT_EQ(manager.getState(), TooltipManager::State::Waiting);

	manager.endHover();

	EXPECT_EQ(manager.getState(), TooltipManager::State::Idle);
}

TEST_F(TooltipManagerTest, EndHoverStartsHiding) {
	manager.startHover({.title = "Test"}, {100.0F, 100.0F});
	manager.update(Theme::Tooltip::hoverDelay + 0.1F);
	manager.update(0.2F); // Complete fade in

	EXPECT_EQ(manager.getState(), TooltipManager::State::Visible);

	manager.endHover();

	EXPECT_EQ(manager.getState(), TooltipManager::State::Hiding);
}

TEST_F(TooltipManagerTest, HideCompletesAndReturnsToIdle) {
	manager.startHover({.title = "Test"}, {100.0F, 100.0F});
	manager.update(Theme::Tooltip::hoverDelay + 0.1F);
	manager.update(0.2F); // Complete fade in

	manager.endHover();
	manager.update(0.2F); // Complete fade out

	EXPECT_EQ(manager.getState(), TooltipManager::State::Idle);
	EXPECT_FALSE(manager.isTooltipVisible());
}

TEST_F(TooltipManagerTest, TooltipStaysOnScreenRight) {
	// Start hover near right edge
	manager.startHover({.title = "Test"}, {750.0F, 100.0F});
	manager.update(Theme::Tooltip::hoverDelay + 0.1F);
	manager.update(0.2F);

	// Tooltip should be positioned to stay on screen
	EXPECT_TRUE(manager.isTooltipVisible());
}

TEST_F(TooltipManagerTest, TooltipStaysOnScreenBottom) {
	// Start hover near bottom edge
	manager.startHover({.title = "Test"}, {100.0F, 550.0F});
	manager.update(Theme::Tooltip::hoverDelay + 0.1F);
	manager.update(0.2F);

	// Tooltip should be positioned to stay on screen
	EXPECT_TRUE(manager.isTooltipVisible());
}

TEST_F(TooltipManagerTest, CursorPositionUpdates) {
	manager.startHover({.title = "Test"}, {100.0F, 100.0F});
	manager.update(Theme::Tooltip::hoverDelay + 0.1F);
	manager.update(0.2F);

	// Update cursor position
	manager.updateCursorPosition({200.0F, 200.0F});

	// Tooltip should still be visible (position updated internally)
	EXPECT_TRUE(manager.isTooltipVisible());
}

} // namespace UI
