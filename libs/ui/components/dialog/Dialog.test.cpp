#include "Dialog.h"

#include "focus/FocusManager.h"
#include <gtest/gtest.h>

namespace UI {

class DialogTest : public ::testing::Test {
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
};

// === Dialog Construction Tests ===

TEST_F(DialogTest, ConstructsWithDefaults) {
	Dialog dialog(Dialog::Args{
		.title = "Test Dialog",
	});

	EXPECT_EQ(dialog.getTitle(), "Test Dialog");
	EXPECT_FALSE(dialog.isOpen());
	EXPECT_FLOAT_EQ(dialog.getDialogSize().x, Theme::Dialog::defaultWidth);
	EXPECT_FLOAT_EQ(dialog.getDialogSize().y, Theme::Dialog::defaultHeight);
}

TEST_F(DialogTest, ConstructsWithCustomSize) {
	Dialog dialog(Dialog::Args{
		.title = "Custom Size",
		.size = {400.0F, 300.0F},
	});

	EXPECT_FLOAT_EQ(dialog.getDialogSize().x, 400.0F);
	EXPECT_FLOAT_EQ(dialog.getDialogSize().y, 300.0F);
}

TEST_F(DialogTest, SetTitleUpdatesTitle) {
	Dialog dialog(Dialog::Args{.title = "Original"});

	dialog.setTitle("Updated Title");
	EXPECT_EQ(dialog.getTitle(), "Updated Title");
}

// === Dialog State Tests ===

TEST_F(DialogTest, InitiallyClosed) {
	Dialog dialog(Dialog::Args{.title = "Test"});

	EXPECT_FALSE(dialog.isOpen());
	EXPECT_FALSE(dialog.isAnimating());
	EXPECT_FLOAT_EQ(dialog.getOpacity(), 0.0F);
}

TEST_F(DialogTest, OpenChangesState) {
	Dialog dialog(Dialog::Args{.title = "Test"});

	dialog.open(800.0F, 600.0F);

	EXPECT_TRUE(dialog.isOpen());
	EXPECT_TRUE(dialog.isAnimating()); // Opening animation in progress
}

TEST_F(DialogTest, CloseChangesState) {
	Dialog dialog(Dialog::Args{.title = "Test"});

	dialog.open(800.0F, 600.0F);
	// Advance past opening animation
	for (int i = 0; i < 20; ++i) {
		dialog.update(0.01F);
	}

	EXPECT_TRUE(dialog.isOpen());
	EXPECT_FALSE(dialog.isAnimating()); // Fully open now

	dialog.close();

	EXPECT_TRUE(dialog.isOpen()); // Still "open" during close animation
	EXPECT_TRUE(dialog.isAnimating());
}

TEST_F(DialogTest, CloseAnimationCompletesAndCallsCallback) {
	bool closeCalled = false;
	Dialog dialog(Dialog::Args{
		.title = "Test",
		.onClose = [&closeCalled]() { closeCalled = true; },
	});

	dialog.open(800.0F, 600.0F);
	// Complete open animation
	for (int i = 0; i < 20; ++i) {
		dialog.update(0.01F);
	}

	dialog.close();
	// Complete close animation
	for (int i = 0; i < 20; ++i) {
		dialog.update(0.01F);
	}

	EXPECT_FALSE(dialog.isOpen());
	EXPECT_TRUE(closeCalled);
}

TEST_F(DialogTest, OpenWhileOpenDoesNothing) {
	Dialog dialog(Dialog::Args{.title = "Test"});

	dialog.open(800.0F, 600.0F);
	float initialOpacity = dialog.getOpacity();

	dialog.open(800.0F, 600.0F); // Second call should be ignored

	EXPECT_FLOAT_EQ(dialog.getOpacity(), initialOpacity);
}

TEST_F(DialogTest, CloseWhileClosedDoesNothing) {
	Dialog dialog(Dialog::Args{.title = "Test"});

	// Calling close on already-closed dialog should be safe
	dialog.close();

	EXPECT_FALSE(dialog.isOpen());
}

// === Dialog Animation Tests ===

TEST_F(DialogTest, OpenAnimationIncreasesOpacity) {
	Dialog dialog(Dialog::Args{.title = "Test"});

	dialog.open(800.0F, 600.0F);
	EXPECT_FLOAT_EQ(dialog.getOpacity(), 0.0F);

	dialog.update(0.05F); // Partial animation
	EXPECT_GT(dialog.getOpacity(), 0.0F);
	EXPECT_LT(dialog.getOpacity(), 1.0F);

	// Complete animation
	for (int i = 0; i < 20; ++i) {
		dialog.update(0.01F);
	}
	EXPECT_FLOAT_EQ(dialog.getOpacity(), 1.0F);
}

TEST_F(DialogTest, CloseAnimationDecreasesOpacity) {
	Dialog dialog(Dialog::Args{.title = "Test"});

	dialog.open(800.0F, 600.0F);
	// Complete open animation
	for (int i = 0; i < 20; ++i) {
		dialog.update(0.01F);
	}
	EXPECT_FLOAT_EQ(dialog.getOpacity(), 1.0F);

	dialog.close();
	dialog.update(0.05F); // Partial animation

	EXPECT_LT(dialog.getOpacity(), 1.0F);
	EXPECT_GT(dialog.getOpacity(), 0.0F);
}

// === Dialog Content Bounds Tests ===

TEST_F(DialogTest, ContentBoundsExcludesTitleBar) {
	Dialog dialog(Dialog::Args{
		.title = "Test",
		.size = {600.0F, 400.0F},
	});

	dialog.open(800.0F, 600.0F);

	Foundation::Rect contentBounds = dialog.getContentBounds();

	// Content should start below title bar with padding
	EXPECT_GT(contentBounds.y, 0.0F);

	// Content width should be dialog width minus padding on both sides
	float expectedWidth = 600.0F - Theme::Dialog::contentPadding * 2;
	EXPECT_FLOAT_EQ(contentBounds.width, expectedWidth);
}

// === Dialog Hit Testing Tests ===

TEST_F(DialogTest, ContainsPointCoversScreenWhenOpen) {
	Dialog dialog(Dialog::Args{.title = "Test"});

	// Closed dialog doesn't contain any points
	EXPECT_FALSE(dialog.containsPoint({400.0F, 300.0F}));

	dialog.open(800.0F, 600.0F);

	// Open dialog contains entire screen
	EXPECT_TRUE(dialog.containsPoint({0.0F, 0.0F}));
	EXPECT_TRUE(dialog.containsPoint({400.0F, 300.0F}));
	EXPECT_TRUE(dialog.containsPoint({799.0F, 599.0F}));
}

TEST_F(DialogTest, ContainsPointReturnsFalseWhenClosed) {
	Dialog dialog(Dialog::Args{.title = "Test"});

	EXPECT_FALSE(dialog.containsPoint({400.0F, 300.0F}));
}

// === Dialog Event Handling Tests ===

TEST_F(DialogTest, ConsumesAllEventsWhenOpen) {
	Dialog dialog(Dialog::Args{.title = "Test"});
	dialog.open(800.0F, 600.0F);

	// Complete animation
	for (int i = 0; i < 20; ++i) {
		dialog.update(0.01F);
	}

	InputEvent mouseDown{.type = InputEvent::Type::MouseDown, .position = {400.0F, 300.0F}};

	bool handled = dialog.handleEvent(mouseDown);

	EXPECT_TRUE(handled);
	EXPECT_TRUE(mouseDown.isConsumed());
}

TEST_F(DialogTest, DoesNotConsumeEventsWhenClosed) {
	Dialog dialog(Dialog::Args{.title = "Test"});

	InputEvent mouseDown{.type = InputEvent::Type::MouseDown, .position = {400.0F, 300.0F}};

	bool handled = dialog.handleEvent(mouseDown);

	EXPECT_FALSE(handled);
	EXPECT_FALSE(mouseDown.isConsumed());
}

TEST_F(DialogTest, ClickOutsidePanelClosesDialog) {
	Dialog dialog(Dialog::Args{
		.title = "Test",
		.size = {400.0F, 300.0F},
	});
	dialog.open(800.0F, 600.0F);

	// Complete animation
	for (int i = 0; i < 20; ++i) {
		dialog.update(0.01F);
	}

	// Panel is centered: (200, 150) to (600, 450)
	// Click outside panel (top-left corner of screen)
	InputEvent clickOutside{.type = InputEvent::Type::MouseDown, .position = {50.0F, 50.0F}};

	dialog.handleEvent(clickOutside);

	EXPECT_TRUE(dialog.isAnimating()); // Should be closing
}

TEST_F(DialogTest, EscapeClosesDialogViaKeyInput) {
	Dialog dialog(Dialog::Args{.title = "Test"});
	dialog.open(800.0F, 600.0F);

	// Complete animation
	for (int i = 0; i < 20; ++i) {
		dialog.update(0.01F);
	}

	// Escape is handled via IFocusable::handleKeyInput, not InputEvent
	dialog.handleKeyInput(engine::Key::Escape, false, false, false);

	EXPECT_TRUE(dialog.isAnimating()); // Should be closing
}

} // namespace UI
