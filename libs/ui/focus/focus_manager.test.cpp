#include "focus/focus_manager.h"
#include <gtest/gtest.h>

using namespace UI;

// ============================================================================
// Mock Focusable Component
// ============================================================================

class MockFocusable : public IFocusable {
  public:
	MockFocusable() = default;
	explicit MockFocusable(bool canFocus) : m_canFocus(canFocus) {}

	// IFocusable interface
	void OnFocusGained() override {
		m_hasFocus = true;
		m_focusGainedCount++;
	}

	void OnFocusLost() override {
		m_hasFocus = false;
		m_focusLostCount++;
	}

	void HandleKeyInput(engine::Key key, bool shift, bool ctrl, bool alt) override {
		m_lastKey = key;
		m_lastShift = shift;
		m_lastCtrl = ctrl;
		m_lastAlt = alt;
		m_keyInputCount++;
	}

	void HandleCharInput(char32_t codepoint) override {
		m_lastChar = codepoint;
		m_charInputCount++;
	}

	bool CanReceiveFocus() const override { return m_canFocus; }

	// Test helpers
	void Reset() {
		m_hasFocus = false;
		m_focusGainedCount = 0;
		m_focusLostCount = 0;
		m_keyInputCount = 0;
		m_charInputCount = 0;
		m_lastKey = static_cast<engine::Key>(0);
		m_lastChar = 0;
		m_lastShift = false;
		m_lastCtrl = false;
		m_lastAlt = false;
	}

	void SetCanFocus(bool canFocus) { m_canFocus = canFocus; }

	// State accessors for testing
	bool		 m_hasFocus{false};
	int			 m_focusGainedCount{0};
	int			 m_focusLostCount{0};
	int			 m_keyInputCount{0};
	int			 m_charInputCount{0};
	engine::Key	 m_lastKey{};
	char32_t	 m_lastChar{0};
	bool		 m_lastShift{false};
	bool		 m_lastCtrl{false};
	bool		 m_lastAlt{false};
	bool		 m_canFocus{true};
};

// ============================================================================
// Registration Tests
// ============================================================================

TEST(FocusManagerTest, RegisterFocusable) {
	FocusManager  manager;
	MockFocusable component;

	manager.RegisterFocusable(&component);

	// No focus by default
	EXPECT_EQ(manager.GetFocused(), nullptr);
	EXPECT_FALSE(manager.HasFocus(&component));
}

TEST(FocusManagerTest, RegisterMultipleFocusables) {
	FocusManager  manager;
	MockFocusable component1;
	MockFocusable component2;
	MockFocusable component3;

	manager.RegisterFocusable(&component1, 0);
	manager.RegisterFocusable(&component2, 1);
	manager.RegisterFocusable(&component3, 2);

	EXPECT_EQ(manager.GetFocused(), nullptr);
}

TEST(FocusManagerTest, RegisterWithAutoTabIndex) {
	FocusManager  manager;
	MockFocusable component1;
	MockFocusable component2;
	MockFocusable component3;

	// Auto-assign tab indices (-1)
	manager.RegisterFocusable(&component1, -1);
	manager.RegisterFocusable(&component2, -1);
	manager.RegisterFocusable(&component3, -1);

	// Give focus to first and navigate
	manager.SetFocus(&component1);
	manager.FocusNext();

	// Should move to component2 (auto-assigned in order)
	EXPECT_TRUE(manager.HasFocus(&component2));
}

TEST(FocusManagerTest, RegisterDuplicateComponent) {
	FocusManager  manager;
	MockFocusable component;

	manager.RegisterFocusable(&component, 0);
	manager.RegisterFocusable(&component, 1); // Should warn but not crash

	// Component should only be registered once
	manager.SetFocus(&component);
	EXPECT_TRUE(manager.HasFocus(&component));
}

TEST(FocusManagerTest, UnregisterFocusable) {
	FocusManager  manager;
	MockFocusable component;

	manager.RegisterFocusable(&component);
	manager.SetFocus(&component);
	EXPECT_TRUE(manager.HasFocus(&component));

	// Unregister should clear focus
	manager.UnregisterFocusable(&component);
	EXPECT_EQ(manager.GetFocused(), nullptr);
	EXPECT_EQ(component.m_focusLostCount, 1);
}

TEST(FocusManagerTest, UnregisterUnregisteredComponent) {
	FocusManager  manager;
	MockFocusable component;

	// Should not crash
	EXPECT_NO_THROW(manager.UnregisterFocusable(&component));
}

// ============================================================================
// Focus Control Tests
// ============================================================================

TEST(FocusManagerTest, SetFocus) {
	FocusManager  manager;
	MockFocusable component;

	manager.RegisterFocusable(&component);
	manager.SetFocus(&component);

	EXPECT_TRUE(manager.HasFocus(&component));
	EXPECT_EQ(manager.GetFocused(), &component);
	EXPECT_TRUE(component.m_hasFocus);
	EXPECT_EQ(component.m_focusGainedCount, 1);
}

TEST(FocusManagerTest, SetFocusTransfersFromPrevious) {
	FocusManager  manager;
	MockFocusable component1;
	MockFocusable component2;

	manager.RegisterFocusable(&component1, 0);
	manager.RegisterFocusable(&component2, 1);

	// Give focus to first
	manager.SetFocus(&component1);
	EXPECT_EQ(component1.m_focusGainedCount, 1);
	EXPECT_EQ(component1.m_focusLostCount, 0);

	// Transfer focus to second
	manager.SetFocus(&component2);
	EXPECT_FALSE(component1.m_hasFocus);
	EXPECT_TRUE(component2.m_hasFocus);
	EXPECT_EQ(component1.m_focusLostCount, 1);
	EXPECT_EQ(component2.m_focusGainedCount, 1);
}

TEST(FocusManagerTest, SetFocusSameComponent) {
	FocusManager  manager;
	MockFocusable component;

	manager.RegisterFocusable(&component);

	// Give focus
	manager.SetFocus(&component);
	EXPECT_EQ(component.m_focusGainedCount, 1);

	// Set focus again (should be no-op)
	manager.SetFocus(&component);
	EXPECT_EQ(component.m_focusGainedCount, 1); // No second call
	EXPECT_EQ(component.m_focusLostCount, 0);
}

TEST(FocusManagerTest, ClearFocus) {
	FocusManager  manager;
	MockFocusable component;

	manager.RegisterFocusable(&component);
	manager.SetFocus(&component);
	EXPECT_TRUE(manager.HasFocus(&component));

	manager.ClearFocus();
	EXPECT_EQ(manager.GetFocused(), nullptr);
	EXPECT_FALSE(component.m_hasFocus);
	EXPECT_EQ(component.m_focusLostCount, 1);
}

TEST(FocusManagerTest, ClearFocusWhenNone) {
	FocusManager manager;

	// Should not crash
	EXPECT_NO_THROW(manager.ClearFocus());
	EXPECT_EQ(manager.GetFocused(), nullptr);
}

// ============================================================================
// Tab Navigation Tests
// ============================================================================

TEST(FocusManagerTest, FocusNext) {
	FocusManager  manager;
	MockFocusable component1;
	MockFocusable component2;
	MockFocusable component3;

	manager.RegisterFocusable(&component1, 0);
	manager.RegisterFocusable(&component2, 1);
	manager.RegisterFocusable(&component3, 2);

	// Start with no focus
	manager.FocusNext();
	EXPECT_TRUE(manager.HasFocus(&component1));

	manager.FocusNext();
	EXPECT_TRUE(manager.HasFocus(&component2));

	manager.FocusNext();
	EXPECT_TRUE(manager.HasFocus(&component3));
}

TEST(FocusManagerTest, FocusNextWrapsAround) {
	FocusManager  manager;
	MockFocusable component1;
	MockFocusable component2;
	MockFocusable component3;

	manager.RegisterFocusable(&component1, 0);
	manager.RegisterFocusable(&component2, 1);
	manager.RegisterFocusable(&component3, 2);

	manager.SetFocus(&component3);

	// Should wrap to first
	manager.FocusNext();
	EXPECT_TRUE(manager.HasFocus(&component1));
}

TEST(FocusManagerTest, FocusPrevious) {
	FocusManager  manager;
	MockFocusable component1;
	MockFocusable component2;
	MockFocusable component3;

	manager.RegisterFocusable(&component1, 0);
	manager.RegisterFocusable(&component2, 1);
	manager.RegisterFocusable(&component3, 2);

	manager.SetFocus(&component3);

	manager.FocusPrevious();
	EXPECT_TRUE(manager.HasFocus(&component2));

	manager.FocusPrevious();
	EXPECT_TRUE(manager.HasFocus(&component1));
}

TEST(FocusManagerTest, FocusPreviousWrapsAround) {
	FocusManager  manager;
	MockFocusable component1;
	MockFocusable component2;
	MockFocusable component3;

	manager.RegisterFocusable(&component1, 0);
	manager.RegisterFocusable(&component2, 1);
	manager.RegisterFocusable(&component3, 2);

	manager.SetFocus(&component1);

	// Should wrap to last
	manager.FocusPrevious();
	EXPECT_TRUE(manager.HasFocus(&component3));
}

TEST(FocusManagerTest, FocusNextSkipsDisabledComponents) {
	FocusManager  manager;
	MockFocusable component1(true);  // Can focus
	MockFocusable component2(false); // Cannot focus (disabled)
	MockFocusable component3(true);  // Can focus

	manager.RegisterFocusable(&component1, 0);
	manager.RegisterFocusable(&component2, 1);
	manager.RegisterFocusable(&component3, 2);

	manager.SetFocus(&component1);

	// Should skip component2 (disabled)
	manager.FocusNext();
	EXPECT_TRUE(manager.HasFocus(&component3));
}

TEST(FocusManagerTest, FocusPreviousSkipsDisabledComponents) {
	FocusManager  manager;
	MockFocusable component1(true);  // Can focus
	MockFocusable component2(false); // Cannot focus (disabled)
	MockFocusable component3(true);  // Can focus

	manager.RegisterFocusable(&component1, 0);
	manager.RegisterFocusable(&component2, 1);
	manager.RegisterFocusable(&component3, 2);

	manager.SetFocus(&component3);

	// Should skip component2 (disabled)
	manager.FocusPrevious();
	EXPECT_TRUE(manager.HasFocus(&component1));
}

TEST(FocusManagerTest, FocusNextWithAllDisabled) {
	FocusManager  manager;
	MockFocusable component1(false);
	MockFocusable component2(false);
	MockFocusable component3(false);

	manager.RegisterFocusable(&component1, 0);
	manager.RegisterFocusable(&component2, 1);
	manager.RegisterFocusable(&component3, 2);

	// Should clear focus (no valid components)
	manager.FocusNext();
	EXPECT_EQ(manager.GetFocused(), nullptr);
}

TEST(FocusManagerTest, FocusNextWithEmptyList) {
	FocusManager manager;

	// Should not crash
	EXPECT_NO_THROW(manager.FocusNext());
	EXPECT_EQ(manager.GetFocused(), nullptr);
}

TEST(FocusManagerTest, FocusPreviousWithEmptyList) {
	FocusManager manager;

	// Should not crash
	EXPECT_NO_THROW(manager.FocusPrevious());
	EXPECT_EQ(manager.GetFocused(), nullptr);
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
	manager.RegisterFocusable(&component3, 2);
	manager.RegisterFocusable(&component1, 0);
	manager.RegisterFocusable(&component2, 1);

	// Should navigate in sorted order (0, 1, 2)
	manager.FocusNext();
	EXPECT_TRUE(manager.HasFocus(&component1));

	manager.FocusNext();
	EXPECT_TRUE(manager.HasFocus(&component2));

	manager.FocusNext();
	EXPECT_TRUE(manager.HasFocus(&component3));
}

TEST(FocusManagerTest, AutoTabIndexIncrements) {
	FocusManager  manager;
	MockFocusable component1;
	MockFocusable component2;
	MockFocusable component3;

	// Auto-assign tab indices
	manager.RegisterFocusable(&component1, -1); // Auto: 0
	manager.RegisterFocusable(&component2, -1); // Auto: 1
	manager.RegisterFocusable(&component3, -1); // Auto: 2

	// Should navigate in registration order
	manager.FocusNext();
	EXPECT_TRUE(manager.HasFocus(&component1));

	manager.FocusNext();
	EXPECT_TRUE(manager.HasFocus(&component2));

	manager.FocusNext();
	EXPECT_TRUE(manager.HasFocus(&component3));
}

TEST(FocusManagerTest, MixedExplicitAndAutoTabIndex) {
	FocusManager  manager;
	MockFocusable component1;
	MockFocusable component2;
	MockFocusable component3;
	MockFocusable component4;

	manager.RegisterFocusable(&component1, 0);	// Explicit
	manager.RegisterFocusable(&component2, -1); // Auto: 1
	manager.RegisterFocusable(&component3, 10); // Explicit
	manager.RegisterFocusable(&component4, -1); // Auto: 2

	// Should navigate in sorted order: 0, 1, 2, 10
	manager.FocusNext();
	EXPECT_TRUE(manager.HasFocus(&component1)); // tabIndex 0

	manager.FocusNext();
	EXPECT_TRUE(manager.HasFocus(&component2)); // tabIndex 1

	manager.FocusNext();
	EXPECT_TRUE(manager.HasFocus(&component4)); // tabIndex 2

	manager.FocusNext();
	EXPECT_TRUE(manager.HasFocus(&component3)); // tabIndex 10
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
	manager.RegisterFocusable(&background1, 0);
	manager.RegisterFocusable(&background2, 1);

	// Give focus to background
	manager.SetFocus(&background1);
	EXPECT_TRUE(manager.HasFocus(&background1));

	// Register modal components
	manager.RegisterFocusable(&modal1, 2);
	manager.RegisterFocusable(&modal2, 3);

	// Push modal scope (saves current focus)
	std::vector<IFocusable*> modalComponents = {&modal1, &modal2};
	manager.PushFocusScope(modalComponents);

	// Focus should be cleared
	EXPECT_EQ(manager.GetFocused(), nullptr);
	EXPECT_FALSE(background1.m_hasFocus);
}

TEST(FocusManagerTest, FocusNextRespectsFocusScope) {
	FocusManager  manager;
	MockFocusable background1;
	MockFocusable background2;
	MockFocusable modal1;
	MockFocusable modal2;

	manager.RegisterFocusable(&background1, 0);
	manager.RegisterFocusable(&background2, 1);
	manager.RegisterFocusable(&modal1, 2);
	manager.RegisterFocusable(&modal2, 3);

	// Push modal scope
	std::vector<IFocusable*> modalComponents = {&modal1, &modal2};
	manager.PushFocusScope(modalComponents);

	// Tab navigation should only cycle through modal components
	manager.FocusNext();
	EXPECT_TRUE(manager.HasFocus(&modal1));

	manager.FocusNext();
	EXPECT_TRUE(manager.HasFocus(&modal2));

	manager.FocusNext(); // Wrap
	EXPECT_TRUE(manager.HasFocus(&modal1));

	// Background components should not receive focus
	EXPECT_FALSE(manager.HasFocus(&background1));
	EXPECT_FALSE(manager.HasFocus(&background2));
}

TEST(FocusManagerTest, PopFocusScope) {
	FocusManager  manager;
	MockFocusable background1;
	MockFocusable background2;
	MockFocusable modal1;
	MockFocusable modal2;

	manager.RegisterFocusable(&background1, 0);
	manager.RegisterFocusable(&background2, 1);
	manager.RegisterFocusable(&modal1, 2);
	manager.RegisterFocusable(&modal2, 3);

	// Focus background
	manager.SetFocus(&background1);

	// Push modal scope
	std::vector<IFocusable*> modalComponents = {&modal1, &modal2};
	manager.PushFocusScope(modalComponents);

	// Focus modal
	manager.SetFocus(&modal1);
	EXPECT_TRUE(manager.HasFocus(&modal1));

	// Pop scope (should restore previous focus)
	manager.PopFocusScope();
	EXPECT_TRUE(manager.HasFocus(&background1));
	EXPECT_FALSE(modal1.m_hasFocus);
}

TEST(FocusManagerTest, PopFocusScopeWithUnregisteredPrevious) {
	FocusManager  manager;
	MockFocusable background;
	MockFocusable modal;

	manager.RegisterFocusable(&background, 0);
	manager.RegisterFocusable(&modal, 1);

	// Focus background
	manager.SetFocus(&background);

	// Push modal scope
	std::vector<IFocusable*> modalComponents = {&modal};
	manager.PushFocusScope(modalComponents);

	// Unregister background while modal is active
	manager.UnregisterFocusable(&background);

	// Pop scope (previous focus component no longer exists)
	manager.PopFocusScope();

	// Should clear focus (previous component gone)
	EXPECT_EQ(manager.GetFocused(), nullptr);
}

TEST(FocusManagerTest, NestedFocusScopes) {
	FocusManager  manager;
	MockFocusable background;
	MockFocusable modal1;
	MockFocusable modal2;

	manager.RegisterFocusable(&background, 0);
	manager.RegisterFocusable(&modal1, 1);
	manager.RegisterFocusable(&modal2, 2);

	// Focus background
	manager.SetFocus(&background);

	// Push first modal
	std::vector<IFocusable*> scope1 = {&modal1};
	manager.PushFocusScope(scope1);
	manager.SetFocus(&modal1);

	// Push second modal (nested)
	std::vector<IFocusable*> scope2 = {&modal2};
	manager.PushFocusScope(scope2);
	manager.SetFocus(&modal2);

	EXPECT_TRUE(manager.HasFocus(&modal2));

	// Pop second modal (restore modal1)
	manager.PopFocusScope();
	EXPECT_TRUE(manager.HasFocus(&modal1));

	// Pop first modal (restore background)
	manager.PopFocusScope();
	EXPECT_TRUE(manager.HasFocus(&background));
}

TEST(FocusManagerTest, PopFocusScopeEmptyStack) {
	FocusManager manager;

	// Should assert (death test)
	EXPECT_DEATH(manager.PopFocusScope(), "PopFocusScope called with empty stack");
}

// ============================================================================
// Input Routing Tests
// ============================================================================

TEST(FocusManagerTest, RouteKeyInputToFocused) {
	FocusManager  manager;
	MockFocusable component;

	manager.RegisterFocusable(&component);
	manager.SetFocus(&component);

	// Route key input
	manager.RouteKeyInput(engine::Key::Enter, false, false, false);

	EXPECT_EQ(component.m_keyInputCount, 1);
	EXPECT_EQ(component.m_lastKey, engine::Key::Enter);
	EXPECT_FALSE(component.m_lastShift);
	EXPECT_FALSE(component.m_lastCtrl);
	EXPECT_FALSE(component.m_lastAlt);
}

TEST(FocusManagerTest, RouteKeyInputWithModifiers) {
	FocusManager  manager;
	MockFocusable component;

	manager.RegisterFocusable(&component);
	manager.SetFocus(&component);

	// Route key input with modifiers
	manager.RouteKeyInput(engine::Key::C, true, true, false);

	EXPECT_EQ(component.m_keyInputCount, 1);
	EXPECT_EQ(component.m_lastKey, engine::Key::C);
	EXPECT_TRUE(component.m_lastShift);
	EXPECT_TRUE(component.m_lastCtrl);
	EXPECT_FALSE(component.m_lastAlt);
}

TEST(FocusManagerTest, RouteKeyInputNoFocus) {
	FocusManager  manager;
	MockFocusable component;

	manager.RegisterFocusable(&component);
	// No focus set

	// Should not crash
	EXPECT_NO_THROW(manager.RouteKeyInput(engine::Key::Enter, false, false, false));

	// Component should not receive input
	EXPECT_EQ(component.m_keyInputCount, 0);
}

TEST(FocusManagerTest, RouteCharInputToFocused) {
	FocusManager  manager;
	MockFocusable component;

	manager.RegisterFocusable(&component);
	manager.SetFocus(&component);

	// Route character input
	manager.RouteCharInput(U'A');

	EXPECT_EQ(component.m_charInputCount, 1);
	EXPECT_EQ(component.m_lastChar, U'A');
}

TEST(FocusManagerTest, RouteCharInputUnicode) {
	FocusManager  manager;
	MockFocusable component;

	manager.RegisterFocusable(&component);
	manager.SetFocus(&component);

	// Route Unicode character
	manager.RouteCharInput(U'世'); // Chinese character

	EXPECT_EQ(component.m_charInputCount, 1);
	EXPECT_EQ(component.m_lastChar, U'世');
}

TEST(FocusManagerTest, RouteCharInputNoFocus) {
	FocusManager  manager;
	MockFocusable component;

	manager.RegisterFocusable(&component);
	// No focus set

	// Should not crash
	EXPECT_NO_THROW(manager.RouteCharInput(U'A'));

	// Component should not receive input
	EXPECT_EQ(component.m_charInputCount, 0);
}

// ============================================================================
// Query Tests
// ============================================================================

TEST(FocusManagerTest, GetFocused) {
	FocusManager  manager;
	MockFocusable component;

	manager.RegisterFocusable(&component);

	EXPECT_EQ(manager.GetFocused(), nullptr);

	manager.SetFocus(&component);
	EXPECT_EQ(manager.GetFocused(), &component);

	manager.ClearFocus();
	EXPECT_EQ(manager.GetFocused(), nullptr);
}

TEST(FocusManagerTest, HasFocus) {
	FocusManager  manager;
	MockFocusable component1;
	MockFocusable component2;

	manager.RegisterFocusable(&component1);
	manager.RegisterFocusable(&component2);

	EXPECT_FALSE(manager.HasFocus(&component1));
	EXPECT_FALSE(manager.HasFocus(&component2));

	manager.SetFocus(&component1);
	EXPECT_TRUE(manager.HasFocus(&component1));
	EXPECT_FALSE(manager.HasFocus(&component2));

	manager.SetFocus(&component2);
	EXPECT_FALSE(manager.HasFocus(&component1));
	EXPECT_TRUE(manager.HasFocus(&component2));
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

	manager.RegisterFocusable(&button1, 0);
	manager.RegisterFocusable(&button2, 1);
	manager.RegisterFocusable(&textInput, 2);
	manager.RegisterFocusable(&disabledButton, 3);
	manager.RegisterFocusable(&checkbox, 4);

	// Tab through (should skip disabled)
	manager.FocusNext();
	EXPECT_TRUE(manager.HasFocus(&button1));

	manager.FocusNext();
	EXPECT_TRUE(manager.HasFocus(&button2));

	manager.FocusNext();
	EXPECT_TRUE(manager.HasFocus(&textInput));

	manager.FocusNext(); // Skip disabledButton
	EXPECT_TRUE(manager.HasFocus(&checkbox));

	manager.FocusNext(); // Wrap
	EXPECT_TRUE(manager.HasFocus(&button1));

	// Shift+Tab backwards
	manager.FocusPrevious(); // Wrap
	EXPECT_TRUE(manager.HasFocus(&checkbox));

	manager.FocusPrevious(); // Skip disabledButton
	EXPECT_TRUE(manager.HasFocus(&textInput));

	manager.FocusPrevious();
	EXPECT_TRUE(manager.HasFocus(&button2));
}

TEST(FocusManagerTest, DynamicEnableDisable) {
	FocusManager  manager;
	MockFocusable component1(true);
	MockFocusable component2(true);
	MockFocusable component3(true);

	manager.RegisterFocusable(&component1, 0);
	manager.RegisterFocusable(&component2, 1);
	manager.RegisterFocusable(&component3, 2);

	manager.SetFocus(&component1);

	// Disable component2
	component2.SetCanFocus(false);

	// Should skip component2
	manager.FocusNext();
	EXPECT_TRUE(manager.HasFocus(&component3));

	// Re-enable component2
	component2.SetCanFocus(true);

	// Should navigate normally
	manager.FocusPrevious();
	EXPECT_TRUE(manager.HasFocus(&component2));
}

TEST(FocusManagerTest, UnregisterFocusedComponentInScope) {
	FocusManager  manager;
	MockFocusable background;
	MockFocusable modal1;
	MockFocusable modal2;

	manager.RegisterFocusable(&background, 0);
	manager.RegisterFocusable(&modal1, 1);
	manager.RegisterFocusable(&modal2, 2);

	manager.SetFocus(&background);

	// Push modal scope
	std::vector<IFocusable*> modalComponents = {&modal1, &modal2};
	manager.PushFocusScope(modalComponents);

	manager.SetFocus(&modal1);
	EXPECT_TRUE(manager.HasFocus(&modal1));

	// Unregister focused modal component
	manager.UnregisterFocusable(&modal1);

	// Focus should be cleared
	EXPECT_EQ(manager.GetFocused(), nullptr);

	// Pop scope
	manager.PopFocusScope();

	// Background focus should be restored
	EXPECT_TRUE(manager.HasFocus(&background));
}
