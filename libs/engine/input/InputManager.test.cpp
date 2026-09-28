#include "input/InputManager.h"

#include <gtest/gtest.h>

// keyFromName is a pure static (no GLFW context needed), used by the debug input
// API to map an injected key name to a Key. These cover the letter/digit/named/
// function-key paths plus the unknown fallback.

using engine::InputManager;
using engine::Key;

TEST(InputManagerKeyFromName, SingleLetterIsCaseInsensitive) {
	auto upper = InputManager::keyFromName("R");
	auto lower = InputManager::keyFromName("r");
	ASSERT_TRUE(upper.has_value());
	ASSERT_TRUE(lower.has_value());
	EXPECT_EQ(*upper, Key::R);
	EXPECT_EQ(*lower, Key::R);
	EXPECT_EQ(*InputManager::keyFromName("a"), Key::A);
	EXPECT_EQ(*InputManager::keyFromName("Z"), Key::Z);
}

TEST(InputManagerKeyFromName, Digits) {
	EXPECT_EQ(*InputManager::keyFromName("0"), Key::Num0);
	EXPECT_EQ(*InputManager::keyFromName("9"), Key::Num9);
}

TEST(InputManagerKeyFromName, NamedKeys) {
	EXPECT_EQ(*InputManager::keyFromName("Escape"), Key::Escape);
	EXPECT_EQ(*InputManager::keyFromName("esc"), Key::Escape);
	EXPECT_EQ(*InputManager::keyFromName("Enter"), Key::Enter);
	EXPECT_EQ(*InputManager::keyFromName("space"), Key::Space);
	EXPECT_EQ(*InputManager::keyFromName("Backspace"), Key::Backspace);
	EXPECT_EQ(*InputManager::keyFromName("Up"), Key::Up);
}

TEST(InputManagerKeyFromName, FunctionKeys) {
	EXPECT_EQ(*InputManager::keyFromName("F1"), Key::F1);
	EXPECT_EQ(*InputManager::keyFromName("f5"), Key::F5);
	EXPECT_EQ(*InputManager::keyFromName("F12"), Key::F12);
	EXPECT_FALSE(InputManager::keyFromName("F0").has_value());
	EXPECT_FALSE(InputManager::keyFromName("F13").has_value());
}

TEST(InputManagerKeyFromName, UnknownReturnsNullopt) {
	EXPECT_FALSE(InputManager::keyFromName("").has_value());
	EXPECT_FALSE(InputManager::keyFromName("NotAKey").has_value());
	EXPECT_FALSE(InputManager::keyFromName("RR").has_value()); // multi-char non-named
}

// Injected cursor (/api/input pointer commands). A window-less manager is driven
// through the static GLFW callbacks, the same entry points real host input uses.
class InputManagerInjectedCursor : public ::testing::Test {
  protected:
	void SetUp() override {
		InputManager::setInstance(&input);
		InputManager::CursorPosCallback(nullptr, 10.0, 20.0); // host cursor
	}
	void TearDown() override { InputManager::setInstance(nullptr); }

	InputManager input{nullptr};
};

TEST_F(InputManagerInjectedCursor, HoldsAcrossFrames) {
	input.injectMousePosition({300.0F, 400.0F});
	for (int frame = 0; frame < 3; ++frame) {
		input.update(1.0F / 60.0F);
		EXPECT_EQ(input.getMousePosition(), glm::vec2(300.0F, 400.0F));
	}
}

TEST_F(InputManagerInjectedCursor, HostMoveTakesOver) {
	input.injectMousePosition({300.0F, 400.0F});
	InputManager::CursorPosCallback(nullptr, 50.0, 60.0);
	EXPECT_EQ(input.getMousePosition(), glm::vec2(50.0F, 60.0F));
}

TEST_F(InputManagerInjectedCursor, HostButtonReclaimsHostPosition) {
	input.injectMousePosition({300.0F, 400.0F});
	InputManager::MouseButtonCallback(nullptr, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
	EXPECT_EQ(input.getMousePosition(), glm::vec2(10.0F, 20.0F));
	EXPECT_EQ(input.getDragStartPosition(), glm::vec2(10.0F, 20.0F));
}

TEST_F(InputManagerInjectedCursor, HostScrollReclaimsHostPosition) {
	input.injectMousePosition({300.0F, 400.0F});
	InputManager::ScrollCallback(nullptr, 0.0, 1.0);
	EXPECT_EQ(input.getMousePosition(), glm::vec2(10.0F, 20.0F));
}
