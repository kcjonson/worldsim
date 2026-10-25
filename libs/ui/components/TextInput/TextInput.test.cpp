#include "TextInput.h"

#include "clipboard/IClipboard.h"
#include "focus/FocusManager.h"

#include <gtest/gtest.h>

#include <string>

namespace UI {

	class FakeClipboard : public IClipboard {
	  public:
		std::string getText() const override { return contents; }

		void setText(const std::string& text) override {
			contents = text;
			++setCount;
		}

		std::string contents;
		int			setCount{0};
	};

	class TextInputClipboardTest : public ::testing::Test {
	  protected:
		FocusManager  focusManager;
		FakeClipboard clipboard;

		void SetUp() override {
			FocusManager::setInstance(&focusManager);
			IClipboard::setInstance(&clipboard);
		}
		void TearDown() override {
			IClipboard::setInstance(nullptr);
			FocusManager::setInstance(nullptr);
		}

		static void press(TextInput& input, engine::Key key) { input.handleKeyInput(key, false, true, false); }
	};

	TEST_F(TextInputClipboardTest, CopyPutsSelectionOnClipboardAndKeepsText) {
		TextInput input(TextInput::Args{.text = "hello"});
		input.onFocusGained();

		press(input, engine::Key::A);
		press(input, engine::Key::C);

		EXPECT_EQ(clipboard.contents, "hello");
		EXPECT_EQ(clipboard.setCount, 1);
		EXPECT_EQ(input.text, "hello");
	}

	TEST_F(TextInputClipboardTest, CopyWithEmptySelectionDoesNotTouchClipboard) {
		TextInput input(TextInput::Args{.text = "hello"});
		input.onFocusGained();

		ASSERT_FALSE(input.selection.has_value());
		press(input, engine::Key::C);

		EXPECT_EQ(clipboard.setCount, 0);
		EXPECT_TRUE(clipboard.contents.empty());
	}

	TEST_F(TextInputClipboardTest, CutCopiesThenDeletesSelectionAndFiresOnChange) {
		std::string changed = "unset";
		TextInput	input(TextInput::Args{.text = "hello", .onChange = [&](const std::string& value) { changed = value; }});
		input.onFocusGained();

		press(input, engine::Key::A);
		press(input, engine::Key::X);

		EXPECT_EQ(clipboard.contents, "hello");
		EXPECT_EQ(input.text, "");
		EXPECT_EQ(changed, "");
		EXPECT_EQ(input.cursorPosition, 0U);
	}

	TEST_F(TextInputClipboardTest, PasteInsertsAtCursor) {
		TextInput input(TextInput::Args{.text = "ab"});
		input.onFocusGained();
		clipboard.contents = "cd";

		press(input, engine::Key::V);

		EXPECT_EQ(input.text, "abcd");
		EXPECT_EQ(input.cursorPosition, 4U);
	}

	TEST_F(TextInputClipboardTest, PasteReplacesSelection) {
		TextInput input(TextInput::Args{.text = "hello"});
		input.onFocusGained();
		clipboard.contents = "x";

		press(input, engine::Key::A);
		press(input, engine::Key::V);

		EXPECT_EQ(input.text, "x");
		EXPECT_EQ(input.cursorPosition, 1U);
		EXPECT_FALSE(input.selection.has_value());
	}

	TEST_F(TextInputClipboardTest, PasteStripsNewlines) {
		TextInput input(TextInput::Args{});
		input.onFocusGained();
		clipboard.contents = "a\nb\r\nc";

		press(input, engine::Key::V);

		EXPECT_EQ(input.text, "abc");
	}

} // namespace UI
