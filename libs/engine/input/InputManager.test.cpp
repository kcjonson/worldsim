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
