#include "focus/focus_manager.h"
#include <gtest/gtest.h>

using namespace UI;

// ============================================================================
// Mock Focusable Component
// ============================================================================

class MockFocusable : public IFocusable {
  public:
	MockFocusable() = default;
	explicit MockFocusable(bool canFocus) : canFocusFlag(canFocus) {}

	// IFocusable interface
	void onFocusGained() override {
		hasFocusFlag = true;
		focusGainedCount++;
	}

	void onFocusLost() override {
		hasFocusFlag = false;
		focusLostCount++;
	}

	void handleKeyInput(engine::Key key, bool shift, bool ctrl, bool alt) override {
		lastKey = key;
		lastShift = shift;
		lastCtrl = ctrl;
		lastAlt = alt;
		keyInputCount++;
	}

	void handleCharInput(char32_t codepoint) override {
		lastChar = codepoint;
		charInputCount++;
	}

	bool canReceiveFocus() const override { return canFocusFlag; }

	// Test helpers
	void Reset() {
		hasFocusFlag = false;
		focusGainedCount = 0;
		focusLostCount = 0;
		keyInputCount = 0;
		charInputCount = 0;
		lastKey = static_cast<engine::Key>(0);
		lastChar = 0;
		lastShift = false;
		lastCtrl = false;
		lastAlt = false;
	}

	void SetCanFocus(bool canFocus) { canFocusFlag = canFocus; }

	// State accessors for testing
	bool		 hasFocusFlag{false};
	int			 focusGainedCount{0};
	int			 focusLostCount{0};
	int			 keyInputCount{0};
	int			 charInputCount{0};
	engine::Key	 lastKey{};
	char32_t	 lastChar{0};
	bool		 lastShift{false};
	bool		 lastCtrl{false};
	bool		 lastAlt{false};
	bool		 canFocusFlag{true};
};

// ============================================================================
// Registration Tests
// ============================================================================

TEST(FocusManagerTest, RegisterFocusable) {
	FocusManager  manager;
	MockFocusable component;

	manager.registerFocusable(&component);

	// No focus by default
	EXPECT_EQ(manager.getFocused(), nullptr);
	EXPECT_FALSE(manager.hasFocus(&component));
}

TEST(FocusManagerTest, RegisterMultipleFocusables) {
	FocusManager  manager;
	MockFocusable component1;
	MockFocusable component2;
	MockFocusable component3;

	manager.registerFocusable(&component1, 0);
	manager.registerFocusable(&component2, 1);
	manager.registerFocusable(&component3, 2);

	EXPECT_EQ(manager.getFocused(), nullptr);
}

TEST(FocusManagerTest, RegisterWithAutoTabIndex) {
	FocusManager  manager;
	MockFocusable component1;
	MockFocusable component2;
	MockFocusable component3;

	// Auto-assign tab indices (-1)
	manager.registerFocusable(&component1, -1);
	manager.registerFocusable(&component2, -1);
	manager.registerFocusable(&component3, -1);

	// Give focus to first and navigate
	manager.setFocus(&component1);
	manager.focusNext();

	// Should move to component2 (auto-assigned in order)
	EXPECT_TRUE(manager.hasFocus(&component2));
}

TEST(FocusManagerTest, RegisterDuplicateComponent) {
	FocusManager  manager;
	MockFocusable component;

	manager.registerFocusable(&component, 0);
	manager.registerFocusable(&component, 1); // Should warn but not crash

	// Component should only be registered once
	manager.setFocus(&component);
	EXPECT_TRUE(manager.hasFocus(&component));
}

TEST(FocusManagerTest, UnregisterFocusable) {
	FocusManager  manager;
	MockFocusable component;

	manager.registerFocusable(&component);
	manager.setFocus(&component);
	EXPECT_TRUE(manager.hasFocus(&component));

	// Unregister should clear focus
	manager.unregisterFocusable(&component);
	EXPECT_EQ(manager.getFocused(), nullptr);
	EXPECT_EQ(component.focusLostCount, 1);
}

TEST(FocusManagerTest, UnregisterUnregisteredComponent) {
	FocusManager  manager;
	MockFocusable component;

	// Should not crash
	EXPECT_NO_THROW(manager.unregisterFocusable(&component));
}

// ============================================================================
// Focus Control Tests
// ============================================================================

TEST(FocusManagerTest, SetFocus) {
	FocusManager  manager;
	MockFocusable component;

	manager.registerFocusable(&component);
	manager.setFocus(&component);

	EXPECT_TRUE(manager.hasFocus(&component));
	EXPECT_EQ(manager.getFocused(), &component);
	EXPECT_TRUE(component.hasFocusFlag);
	EXPECT_EQ(component.focusGainedCount, 1);
}

TEST(FocusManagerTest, SetFocusTransfersFromPrevious) {
	FocusManager  manager;
	MockFocusable component1;
	MockFocusable component2;

	manager.registerFocusable(&component1, 0);
	manager.registerFocusable(&component2, 1);

	// Give focus to first
	manager.setFocus(&component1);
	EXPECT_EQ(component1.focusGainedCount, 1);
	EXPECT_EQ(component1.focusLostCount, 0);

	// Transfer focus to second
	manager.setFocus(&component2);
	EXPECT_FALSE(component1.hasFocusFlag);
	EXPECT_TRUE(component2.hasFocusFlag);
	EXPECT_EQ(component1.focusLostCount, 1);
	EXPECT_EQ(component2.focusGainedCount, 1);
}

TEST(FocusManagerTest, SetFocusSameComponent) {
	FocusManager  manager;
	MockFocusable component;

	manager.registerFocusable(&component);

	// Give focus
	manager.setFocus(&component);
	EXPECT_EQ(component.focusGainedCount, 1);

	// Set focus again (should be no-op)
	manager.setFocus(&component);
	EXPECT_EQ(component.focusGainedCount, 1); // No second call
	EXPECT_EQ(component.focusLostCount, 0);
}

TEST(FocusManagerTest, ClearFocus) {
	FocusManager  manager;
	MockFocusable component;

	manager.registerFocusable(&component);
	manager.setFocus(&component);
	EXPECT_TRUE(manager.hasFocus(&component));

	manager.clearFocus();
	EXPECT_EQ(manager.getFocused(), nullptr);
	EXPECT_FALSE(component.hasFocusFlag);
	EXPECT_EQ(component.focusLostCount, 1);
}

TEST(FocusManagerTest, ClearFocusWhenNone) {
	FocusManager manager;

	// Should not crash
	EXPECT_NO_THROW(manager.clearFocus());
	EXPECT_EQ(manager.getFocused(), nullptr);
}

// ============================================================================
// Tab Navigation Tests
// ============================================================================

TEST(FocusManagerTest, FocusNext) {
	FocusManager  manager;
	MockFocusable component1;
	MockFocusable component2;
	MockFocusable component3;

	manager.registerFocusable(&component1, 0);
	manager.registerFocusable(&component2, 1);
	manager.registerFocusable(&component3, 2);

	// Start with no focus
	manager.focusNext();
	EXPECT_TRUE(manager.hasFocus(&component1));

	manager.focusNext();
	EXPECT_TRUE(manager.hasFocus(&component2));

	manager.focusNext();
	EXPECT_TRUE(manager.hasFocus(&component3));
}

TEST(FocusManagerTest, FocusNextWrapsAround) {
	FocusManager  manager;
	MockFocusable component1;
	MockFocusable component2;
	MockFocusable component3;

	manager.registerFocusable(&component1, 0);
	manager.registerFocusable(&component2, 1);
	manager.registerFocusable(&component3, 2);

	manager.setFocus(&component3);

	// Should wrap to first
	manager.focusNext();
	EXPECT_TRUE(manager.hasFocus(&component1));
}

TEST(FocusManagerTest, FocusPrevious) {
	FocusManager  manager;
	MockFocusable component1;
	MockFocusable component2;
	MockFocusable component3;

	manager.registerFocusable(&component1, 0);
	manager.registerFocusable(&component2, 1);
	manager.registerFocusable(&component3, 2);

	manager.setFocus(&component3);

	manager.focusPrevious();
	EXPECT_TRUE(manager.hasFocus(&component2));

	manager.focusPrevious();
	EXPECT_TRUE(manager.hasFocus(&component1));
}

TEST(FocusManagerTest, FocusPreviousWrapsAround) {
	FocusManager  manager;
	MockFocusable component1;
	MockFocusable component2;
	MockFocusable component3;

	manager.registerFocusable(&component1, 0);
	manager.registerFocusable(&component2, 1);
	manager.registerFocusable(&component3, 2);

	manager.setFocus(&component1);

	// Should wrap to last
	manager.focusPrevious();
	EXPECT_TRUE(manager.hasFocus(&component3));
}

TEST(FocusManagerTest, FocusNextSkipsDisabledComponents) {
	FocusManager  manager;
	MockFocusable component1(true);  // Can focus
	MockFocusable component2(false); // Cannot focus (disabled)
	MockFocusable component3(true);  // Can focus

	manager.registerFocusable(&component1, 0);
	manager.registerFocusable(&component2, 1);
	manager.registerFocusable(&component3, 2);

	manager.setFocus(&component1);

	// Should skip component2 (disabled)
	manager.focusNext();
	EXPECT_TRUE(manager.hasFocus(&component3));
}

TEST(FocusManagerTest, FocusPreviousSkipsDisabledComponents) {
	FocusManager  manager;
	MockFocusable component1(true);  // Can focus
	MockFocusable component2(false); // Cannot focus (disabled)
	MockFocusable component3(true);  // Can focus

	manager.registerFocusable(&component1, 0);
	manager.registerFocusable(&component2, 1);
	manager.registerFocusable(&component3, 2);

	manager.setFocus(&component3);

	// Should skip component2 (disabled)
	manager.focusPrevious();
	EXPECT_TRUE(manager.hasFocus(&component1));
}

TEST(FocusManagerTest, FocusNextWithAllDisabled) {
	FocusManager  manager;
	MockFocusable component1(false);
	MockFocusable component2(false);
	MockFocusable component3(false);

	manager.registerFocusable(&component1, 0);
	manager.registerFocusable(&component2, 1);
	manager.registerFocusable(&component3, 2);

	// Should clear focus (no valid components)
	manager.focusNext();
	EXPECT_EQ(manager.getFocused(), nullptr);
}

TEST(FocusManagerTest, FocusNextWithEmptyList) {
	FocusManager manager;

	// Should not crash
	EXPECT_NO_THROW(manager.focusNext());
	EXPECT_EQ(manager.getFocused(), nullptr);
}

TEST(FocusManagerTest, FocusPreviousWithEmptyList) {
	FocusManager manager;

	// Should not crash
	EXPECT_NO_THROW(manager.focusPrevious());
	EXPECT_EQ(manager.getFocused(), nullptr);
}

// ============================================================================
// Tab Order Tests
// ============================================================================

TEST(FocusManagerTest, TabOrderRespected) {
	FocusManager  manager;
	MockFocusable component1;
	MockFocusable component2;
	MockFocusable component3;

	// Register out of order
	manager.registerFocusable(&component3, 2);
	manager.registerFocusable(&component1, 0);
	manager.registerFocusable(&component2, 1);

	// Should navigate in sorted order (0, 1, 2)
	manager.focusNext();
	EXPECT_TRUE(manager.hasFocus(&component1));

	manager.focusNext();
	EXPECT_TRUE(manager.hasFocus(&component2));

	manager.focusNext();
	EXPECT_TRUE(manager.hasFocus(&component3));
}

TEST(FocusManagerTest, AutoTabIndexIncrements) {
	FocusManager  manager;
	MockFocusable component1;
	MockFocusable component2;
	MockFocusable component3;

	// Auto-assign tab indices
	manager.registerFocusable(&component1, -1); // Auto: 0
	manager.registerFocusable(&component2, -1); // Auto: 1
	manager.registerFocusable(&component3, -1); // Auto: 2

	// Should navigate in registration order
	manager.focusNext();
	EXPECT_TRUE(manager.hasFocus(&component1));

	manager.focusNext();
	EXPECT_TRUE(manager.hasFocus(&component2));

	manager.focusNext();
	EXPECT_TRUE(manager.hasFocus(&component3));
}

TEST(FocusManagerTest, MixedExplicitAndAutoTabIndex) {
	FocusManager  manager;
	MockFocusable component1;
	MockFocusable component2;
	MockFocusable component3;
	MockFocusable component4;

	manager.registerFocusable(&component1, 0);	// Explicit
	manager.registerFocusable(&component2, -1); // Auto: 1
	manager.registerFocusable(&component3, 10); // Explicit
	manager.registerFocusable(&component4, -1); // Auto: 2

	// Should navigate in sorted order: 0, 1, 2, 10
	manager.focusNext();
	EXPECT_TRUE(manager.hasFocus(&component1)); // tabIndex 0

	manager.focusNext();
	EXPECT_TRUE(manager.hasFocus(&component2)); // tabIndex 1

	manager.focusNext();
	EXPECT_TRUE(manager.hasFocus(&component4)); // tabIndex 2

	manager.focusNext();
	EXPECT_TRUE(manager.hasFocus(&component3)); // tabIndex 10
}

// ============================================================================
// Focus Scope Tests (Modals)
// ============================================================================

TEST(FocusManagerTest, PushFocusScope) {
	FocusManager  manager;
	MockFocusable background1;
	MockFocusable background2;
	MockFocusable modal1;
	MockFocusable modal2;

	// Register background components
	manager.registerFocusable(&background1, 0);
	manager.registerFocusable(&background2, 1);

	// Give focus to background
	manager.setFocus(&background1);
	EXPECT_TRUE(manager.hasFocus(&background1));

	// Register modal components
	manager.registerFocusable(&modal1, 2);
	manager.registerFocusable(&modal2, 3);

	// Push modal scope (saves current focus)
	std::vector<IFocusable*> modalComponents = {&modal1, &modal2};
	manager.pushFocusScope(modalComponents);

	// Focus should be cleared
	EXPECT_EQ(manager.getFocused(), nullptr);
	EXPECT_FALSE(background1.hasFocusFlag);
}

TEST(FocusManagerTest, FocusNextRespectsFocusScope) {
	FocusManager  manager;
	MockFocusable background1;
	MockFocusable background2;
	MockFocusable modal1;
	MockFocusable modal2;

	manager.registerFocusable(&background1, 0);
	manager.registerFocusable(&background2, 1);
	manager.registerFocusable(&modal1, 2);
	manager.registerFocusable(&modal2, 3);

	// Push modal scope
	std::vector<IFocusable*> modalComponents = {&modal1, &modal2};
	manager.pushFocusScope(modalComponents);

	// Tab navigation should only cycle through modal components
	manager.focusNext();
	EXPECT_TRUE(manager.hasFocus(&modal1));

	manager.focusNext();
	EXPECT_TRUE(manager.hasFocus(&modal2));

	manager.focusNext(); // Wrap
	EXPECT_TRUE(manager.hasFocus(&modal1));

	// Background components should not receive focus
	EXPECT_FALSE(manager.hasFocus(&background1));
	EXPECT_FALSE(manager.hasFocus(&background2));
}

TEST(FocusManagerTest, PopFocusScope) {
	FocusManager  manager;
	MockFocusable background1;
	MockFocusable background2;
	MockFocusable modal1;
	MockFocusable modal2;

	manager.registerFocusable(&background1, 0);
	manager.registerFocusable(&background2, 1);
	manager.registerFocusable(&modal1, 2);
	manager.registerFocusable(&modal2, 3);

	// Focus background
	manager.setFocus(&background1);

	// Push modal scope
	std::vector<IFocusable*> modalComponents = {&modal1, &modal2};
	manager.pushFocusScope(modalComponents);

	// Focus modal
	manager.setFocus(&modal1);
	EXPECT_TRUE(manager.hasFocus(&modal1));

	// Pop scope (should restore previous focus)
	manager.popFocusScope();
	EXPECT_TRUE(manager.hasFocus(&background1));
	EXPECT_FALSE(modal1.hasFocusFlag);
}

TEST(FocusManagerTest, PopFocusScopeWithUnregisteredPrevious) {
	FocusManager  manager;
	MockFocusable background;
	MockFocusable modal;

	manager.registerFocusable(&background, 0);
	manager.registerFocusable(&modal, 1);

	// Focus background
	manager.setFocus(&background);

	// Push modal scope
	std::vector<IFocusable*> modalComponents = {&modal};
	manager.pushFocusScope(modalComponents);

	// Unregister background while modal is active
	manager.unregisterFocusable(&background);

	// Pop scope (previous focus component no longer exists)
	manager.popFocusScope();

	// Should clear focus (previous component gone)
	EXPECT_EQ(manager.getFocused(), nullptr);
}

TEST(FocusManagerTest, NestedFocusScopes) {
	FocusManager  manager;
	MockFocusable background;
	MockFocusable modal1;
	MockFocusable modal2;

	manager.registerFocusable(&background, 0);
	manager.registerFocusable(&modal1, 1);
	manager.registerFocusable(&modal2, 2);

	// Focus background
	manager.setFocus(&background);

	// Push first modal
	std::vector<IFocusable*> scope1 = {&modal1};
	manager.pushFocusScope(scope1);
	manager.setFocus(&modal1);

	// Push second modal (nested)
	std::vector<IFocusable*> scope2 = {&modal2};
	manager.pushFocusScope(scope2);
	manager.setFocus(&modal2);

	EXPECT_TRUE(manager.hasFocus(&modal2));

	// Pop second modal (restore modal1)
	manager.popFocusScope();
	EXPECT_TRUE(manager.hasFocus(&modal1));

	// Pop first modal (restore background)
	manager.popFocusScope();
	EXPECT_TRUE(manager.hasFocus(&background));
}

TEST(FocusManagerTest, PopFocusScopeEmptyStack) {
	FocusManager manager;

	// Should assert (death test)
	EXPECT_DEATH(manager.popFocusScope(), "PopFocusScope called with empty stack");
}

// ============================================================================
// Input Routing Tests
// ============================================================================

TEST(FocusManagerTest, RouteKeyInputToFocused) {
	FocusManager  manager;
	MockFocusable component;

	manager.registerFocusable(&component);
	manager.setFocus(&component);

	// Route key input
	manager.routeKeyInput(engine::Key::Enter, false, false, false);

	EXPECT_EQ(component.keyInputCount, 1);
	EXPECT_EQ(component.lastKey, engine::Key::Enter);
	EXPECT_FALSE(component.lastShift);
	EXPECT_FALSE(component.lastCtrl);
	EXPECT_FALSE(component.lastAlt);
}

TEST(FocusManagerTest, RouteKeyInputWithModifiers) {
	FocusManager  manager;
	MockFocusable component;

	manager.registerFocusable(&component);
	manager.setFocus(&component);

	// Route key input with modifiers
	manager.routeKeyInput(engine::Key::C, true, true, false);

	EXPECT_EQ(component.keyInputCount, 1);
	EXPECT_EQ(component.lastKey, engine::Key::C);
	EXPECT_TRUE(component.lastShift);
	EXPECT_TRUE(component.lastCtrl);
	EXPECT_FALSE(component.lastAlt);
}

TEST(FocusManagerTest, RouteKeyInputNoFocus) {
	FocusManager  manager;
	MockFocusable component;

	manager.registerFocusable(&component);
	// No focus set

	// Should not crash
	EXPECT_NO_THROW(manager.routeKeyInput(engine::Key::Enter, false, false, false));

	// Component should not receive input
	EXPECT_EQ(component.keyInputCount, 0);
}

TEST(FocusManagerTest, RouteCharInputToFocused) {
	FocusManager  manager;
	MockFocusable component;

	manager.registerFocusable(&component);
	manager.setFocus(&component);

	// Route character input
	manager.routeCharInput(U'A');

	EXPECT_EQ(component.charInputCount, 1);
	EXPECT_EQ(component.lastChar, U'A');
}

TEST(FocusManagerTest, RouteCharInputUnicode) {
	FocusManager  manager;
	MockFocusable component;

	manager.registerFocusable(&component);
	manager.setFocus(&component);

	// Route Unicode character
	manager.routeCharInput(U'世'); // Chinese character

	EXPECT_EQ(component.charInputCount, 1);
	EXPECT_EQ(component.lastChar, U'世');
}

TEST(FocusManagerTest, RouteCharInputNoFocus) {
	FocusManager  manager;
	MockFocusable component;

	manager.registerFocusable(&component);
	// No focus set

	// Should not crash
	EXPECT_NO_THROW(manager.routeCharInput(U'A'));

	// Component should not receive input
	EXPECT_EQ(component.charInputCount, 0);
}

// ============================================================================
// Query Tests
// ============================================================================

TEST(FocusManagerTest, GetFocused) {
	FocusManager  manager;
	MockFocusable component;

	manager.registerFocusable(&component);

	EXPECT_EQ(manager.getFocused(), nullptr);

	manager.setFocus(&component);
	EXPECT_EQ(manager.getFocused(), &component);

	manager.clearFocus();
	EXPECT_EQ(manager.getFocused(), nullptr);
}

TEST(FocusManagerTest, HasFocus) {
	FocusManager  manager;
	MockFocusable component1;
	MockFocusable component2;

	manager.registerFocusable(&component1);
	manager.registerFocusable(&component2);

	EXPECT_FALSE(manager.hasFocus(&component1));
	EXPECT_FALSE(manager.hasFocus(&component2));

	manager.setFocus(&component1);
	EXPECT_TRUE(manager.hasFocus(&component1));
	EXPECT_FALSE(manager.hasFocus(&component2));

	manager.setFocus(&component2);
	EXPECT_FALSE(manager.hasFocus(&component1));
	EXPECT_TRUE(manager.hasFocus(&component2));
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(FocusManagerTest, ComplexNavigationScenario) {
	FocusManager  manager;
	MockFocusable button1(true);
	MockFocusable button2(true);
	MockFocusable textInput(true);
	MockFocusable disabledButton(false);
	MockFocusable checkbox(true);

	manager.registerFocusable(&button1, 0);
	manager.registerFocusable(&button2, 1);
	manager.registerFocusable(&textInput, 2);
	manager.registerFocusable(&disabledButton, 3);
	manager.registerFocusable(&checkbox, 4);

	// Tab through (should skip disabled)
	manager.focusNext();
	EXPECT_TRUE(manager.hasFocus(&button1));

	manager.focusNext();
	EXPECT_TRUE(manager.hasFocus(&button2));

	manager.focusNext();
	EXPECT_TRUE(manager.hasFocus(&textInput));

	manager.focusNext(); // Skip disabledButton
	EXPECT_TRUE(manager.hasFocus(&checkbox));

	manager.focusNext(); // Wrap
	EXPECT_TRUE(manager.hasFocus(&button1));

	// Shift+Tab backwards
	manager.focusPrevious(); // Wrap
	EXPECT_TRUE(manager.hasFocus(&checkbox));

	manager.focusPrevious(); // Skip disabledButton
	EXPECT_TRUE(manager.hasFocus(&textInput));

	manager.focusPrevious();
	EXPECT_TRUE(manager.hasFocus(&button2));
}

TEST(FocusManagerTest, DynamicEnableDisable) {
	FocusManager  manager;
	MockFocusable component1(true);
	MockFocusable component2(true);
	MockFocusable component3(true);

	manager.registerFocusable(&component1, 0);
	manager.registerFocusable(&component2, 1);
	manager.registerFocusable(&component3, 2);

	manager.setFocus(&component1);

	// Disable component2
	component2.SetCanFocus(false);

	// Should skip component2
	manager.focusNext();
	EXPECT_TRUE(manager.hasFocus(&component3));

	// Re-enable component2
	component2.SetCanFocus(true);

	// Should navigate normally
	manager.focusPrevious();
	EXPECT_TRUE(manager.hasFocus(&component2));
}

TEST(FocusManagerTest, UnregisterFocusedComponentInScope) {
	FocusManager  manager;
	MockFocusable background;
	MockFocusable modal1;
	MockFocusable modal2;

	manager.registerFocusable(&background, 0);
	manager.registerFocusable(&modal1, 1);
	manager.registerFocusable(&modal2, 2);

	manager.setFocus(&background);

	// Push modal scope
	std::vector<IFocusable*> modalComponents = {&modal1, &modal2};
	manager.pushFocusScope(modalComponents);

	manager.setFocus(&modal1);
	EXPECT_TRUE(manager.hasFocus(&modal1));

	// Unregister focused modal component
	manager.unregisterFocusable(&modal1);

	// Focus should be cleared
	EXPECT_EQ(manager.getFocused(), nullptr);

	// Pop scope
	manager.popFocusScope();

	// Background focus should be restored
	EXPECT_TRUE(manager.hasFocus(&background));
}
