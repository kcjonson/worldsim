#include "Button.h"

#include "focus/FocusManager.h"
#include <gtest/gtest.h>

namespace UI {

class ButtonTest : public ::testing::Test {
  protected:
	FocusManager focusManager;

	void SetUp() override { FocusManager::setInstance(&focusManager); }
	void TearDown() override { FocusManager::setInstance(nullptr); }
};

TEST_F(ButtonTest, ConstructorSyncsRenderedLabel) {
	Button button(Button::Args{.label = "Tasks (0)"});
	EXPECT_EQ(button.label, "Tasks (0)");
	EXPECT_EQ(button.renderedLabel(), "Tasks (0)");
}

// Regression: GlobalTaskListView mutated Button::label directly, which left the
// rendered Text primitive stale ("Tasks (0)") even though the model had tasks.
// setLabel must update both the field and what actually draws.
TEST_F(ButtonTest, SetLabelUpdatesRenderedText) {
	Button button(Button::Args{.label = "Tasks (0)"});

	button.setLabel("Tasks (5)");

	EXPECT_EQ(button.label, "Tasks (5)");
	EXPECT_EQ(button.renderedLabel(), "Tasks (5)");
}

TEST_F(ButtonTest, SetLabelToSameValueIsNoop) {
	Button button(Button::Args{.label = "Same"});
	button.setLabel("Same");
	EXPECT_EQ(button.renderedLabel(), "Same");
}

TEST_F(ButtonTest, SetLabelKeepsTextVisibleWhenNonEmpty) {
	Button button(Button::Args{.label = "A"});
	button.setLabel("Longer label");
	EXPECT_EQ(button.renderedLabel(), "Longer label");
}

} // namespace UI
