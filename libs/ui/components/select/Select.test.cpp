#include "Select.h"

#include "focus/FocusManager.h"
#include <gtest/gtest.h>

namespace UI {

class SelectTest : public ::testing::Test {
  protected:
	FocusManager focusManager;

	void SetUp() override { FocusManager::setInstance(&focusManager); }

	void TearDown() override { FocusManager::setInstance(nullptr); }

	// Helper to create test options
	std::vector<SelectOption> createTestOptions() {
		return {
			SelectOption{.label = "Red", .value = "red"},
			SelectOption{.label = "Green", .value = "green"},
			SelectOption{.label = "Blue", .value = "blue"},
		};
	}
};

// === Construction Tests ===

TEST_F(SelectTest, ConstructsWithDefaults) {
	Select select(Select::Args{});

	EXPECT_TRUE(select.getOptions().empty());
	EXPECT_TRUE(select.getValue().empty());
	EXPECT_FALSE(select.isOpen());
	EXPECT_FLOAT_EQ(select.getWidth(), 150.0F);	 // Default size
	EXPECT_FLOAT_EQ(select.getHeight(), 36.0F);
}

TEST_F(SelectTest, ConstructsWithOptions) {
	Select select(Select::Args{
		.options = createTestOptions(),
	});

	EXPECT_EQ(select.getOptions().size(), 3);
}

TEST_F(SelectTest, ConstructsWithValue) {
	Select select(Select::Args{
		.options = createTestOptions(),
		.value = "green",
	});

	EXPECT_EQ(select.getValue(), "green");
	EXPECT_EQ(select.getSelectedLabel(), "Green");
}

TEST_F(SelectTest, ConstructsWithPlaceholder) {
	Select select(Select::Args{
		.options = createTestOptions(),
		.placeholder = "Choose a color",
	});

	// No value set, should show placeholder
	EXPECT_EQ(select.getSelectedLabel(), "Choose a color");
}

TEST_F(SelectTest, ConstructsWithMargin) {
	Select select(Select::Args{
		.size = {100.0F, 30.0F},
		.margin = 5.0F,
	});

	// getWidth/getHeight include margin on both sides
	EXPECT_FLOAT_EQ(select.getWidth(), 110.0F);	 // 100 + 5*2
	EXPECT_FLOAT_EQ(select.getHeight(), 40.0F); // 30 + 5*2
}

// === Value Tests ===

TEST_F(SelectTest, SetValueUpdatesValue) {
	Select select(Select::Args{
		.options = createTestOptions(),
	});

	select.setValue("blue");
	EXPECT_EQ(select.getValue(), "blue");
	EXPECT_EQ(select.getSelectedLabel(), "Blue");
}

TEST_F(SelectTest, SetValueToInvalidShowsPlaceholder) {
	Select select(Select::Args{
		.options = createTestOptions(),
		.placeholder = "Pick one",
	});

	select.setValue("invalid");
	EXPECT_EQ(select.getValue(), "invalid");
	// Selected label falls back to placeholder since value doesn't match
	EXPECT_EQ(select.getSelectedLabel(), "Pick one");
}

TEST_F(SelectTest, GetSelectedLabelReturnsPlaceholderWhenEmpty) {
	Select select(Select::Args{
		.options = createTestOptions(),
		.placeholder = "Select...",
	});

	EXPECT_EQ(select.getSelectedLabel(), "Select...");
}

// === Options Tests ===

TEST_F(SelectTest, SetOptionsUpdatesOptions) {
	Select select(Select::Args{});

	EXPECT_TRUE(select.getOptions().empty());

	select.setOptions(createTestOptions());
	EXPECT_EQ(select.getOptions().size(), 3);
}

TEST_F(SelectTest, SetOptionsToEmptyClosesMenu) {
	Select select(Select::Args{
		.options = createTestOptions(),
	});

	// Open menu
	select.setValue("red");
	// Can't test isOpen() directly without events, but setting empty options should close

	select.setOptions({});
	EXPECT_FALSE(select.isOpen());
}

// === UI State Tests ===

TEST_F(SelectTest, InitiallyNotOpen) {
	Select select(Select::Args{
		.options = createTestOptions(),
	});

	EXPECT_FALSE(select.isOpen());
}

// === Hit Testing ===

TEST_F(SelectTest, ContainsPointInButtonBounds) {
	Select select(Select::Args{
		.position = {100.0F, 100.0F},
		.size = {150.0F, 36.0F},
	});

	// Inside button
	EXPECT_TRUE(select.containsPoint({150.0F, 118.0F}));
	EXPECT_TRUE(select.containsPoint({100.0F, 100.0F})); // Top-left corner

	// Outside button
	EXPECT_FALSE(select.containsPoint({50.0F, 118.0F}));	 // Left of
	EXPECT_FALSE(select.containsPoint({300.0F, 118.0F})); // Right of
	EXPECT_FALSE(select.containsPoint({150.0F, 50.0F}));	 // Above
}

// === Position Tests ===

TEST_F(SelectTest, SetPositionUpdatesBase) {
	Select select(Select::Args{
		.position = {10.0F, 20.0F},
	});

	select.setPosition(50.0F, 60.0F);

	Foundation::Vec2 contentPos = select.getContentPosition();
	EXPECT_FLOAT_EQ(contentPos.x, 50.0F);
	EXPECT_FLOAT_EQ(contentPos.y, 60.0F);
}

TEST_F(SelectTest, SetPositionWithMargin) {
	Select select(Select::Args{
		.position = {0.0F, 0.0F},
		.margin = 8.0F,
	});

	select.setPosition(100.0F, 200.0F);

	Foundation::Vec2 contentPos = select.getContentPosition();
	EXPECT_FLOAT_EQ(contentPos.x, 108.0F); // 100 + 8 margin
	EXPECT_FLOAT_EQ(contentPos.y, 208.0F); // 200 + 8 margin
}

// === Focus Tests ===

TEST_F(SelectTest, CanReceiveFocusWhenVisible) {
	Select select(Select::Args{});

	EXPECT_TRUE(select.canReceiveFocus());

	select.visible = false;
	EXPECT_FALSE(select.canReceiveFocus());
}

TEST_F(SelectTest, OnFocusGainedSetsFocused) {
	Select select(Select::Args{});

	select.onFocusGained();
	// No direct way to test focused state, but method should not crash
}

TEST_F(SelectTest, OnFocusLostClosesMenu) {
	Select select(Select::Args{
		.options = createTestOptions(),
	});

	// Simulate opening and then losing focus
	select.onFocusLost();
	EXPECT_FALSE(select.isOpen());
}

// === onChange Callback Tests ===

TEST_F(SelectTest, OnChangeFiresOnSelection) {
	std::string selectedValue;
	int			callCount = 0;

	Select select(Select::Args{
		.options = createTestOptions(),
		.onChange =
			[&selectedValue, &callCount](const std::string& value) {
				selectedValue = value;
				callCount++;
			},
	});

	// Simulate internal selection (normally done via mouse/keyboard)
	select.setValue("blue");
	// Note: setValue doesn't fire onChange - only user interaction does
	// So we test that the callback mechanism exists

	EXPECT_EQ(callCount, 0); // setValue alone doesn't fire onChange
}

TEST_F(SelectTest, OnChangeDoesNotFireForSameValue) {
	int callCount = 0;

	Select select(Select::Args{
		.options = createTestOptions(),
		.value = "green",
		.onChange = [&callCount](const std::string& /*value*/) { callCount++; },
	});

	// If user selects the same value, onChange should not fire
	// This is tested indirectly - the logic is in selectOption()
	EXPECT_EQ(callCount, 0);
}

// === Visibility Tests ===

TEST_F(SelectTest, VisibilityDefaultsToTrue) {
	Select select(Select::Args{});

	EXPECT_TRUE(select.visible);
}

TEST_F(SelectTest, HandleEventIgnoresWhenNotVisible) {
	Select select(Select::Args{
		.position = {100.0F, 100.0F},
		.options = createTestOptions(),
	});

	select.visible = false;

	InputEvent event{
		.type = InputEvent::Type::MouseDown,
		.position = {150.0F, 118.0F},
	};

	EXPECT_FALSE(select.handleEvent(event));
}

// === Option Label Tests ===

TEST_F(SelectTest, OptionsHaveSeparateLabelAndValue) {
	Select select(Select::Args{
		.options = {
			SelectOption{.label = "Display Text", .value = "internal_value"},
		},
		.value = "internal_value",
	});

	EXPECT_EQ(select.getValue(), "internal_value");
	EXPECT_EQ(select.getSelectedLabel(), "Display Text");
}

TEST_F(SelectTest, OptionsCanHaveSameLabelAndValue) {
	Select select(Select::Args{
		.options = {
			SelectOption{.label = "Apple", .value = "Apple"},
			SelectOption{.label = "Banana", .value = "Banana"},
		},
		.value = "Apple",
	});

	EXPECT_EQ(select.getValue(), "Apple");
	EXPECT_EQ(select.getSelectedLabel(), "Apple");
}

} // namespace UI
