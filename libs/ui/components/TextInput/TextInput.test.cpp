#include "TextInput.h"

#include "clipboard/IClipboard.h"
#include "focus/FocusManager.h"

#include <backend/NullBackend.h>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>
#include <primitives/Primitives.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

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

		static void press(TextInput& input, foundation::Key key) { input.handleKeyInput(key, false, true, false); }
	};

	TEST_F(TextInputClipboardTest, CopyPutsSelectionOnClipboardAndKeepsText) {
		TextInput input(TextInput::Args{.text = "hello"});
		input.onFocusGained();

		press(input, foundation::Key::A);
		press(input, foundation::Key::C);

		EXPECT_EQ(clipboard.contents, "hello");
		EXPECT_EQ(clipboard.setCount, 1);
		EXPECT_EQ(input.text, "hello");
	}

	TEST_F(TextInputClipboardTest, CopyWithEmptySelectionDoesNotTouchClipboard) {
		TextInput input(TextInput::Args{.text = "hello"});
		input.onFocusGained();

		ASSERT_FALSE(input.selection.has_value());
		press(input, foundation::Key::C);

		EXPECT_EQ(clipboard.setCount, 0);
		EXPECT_TRUE(clipboard.contents.empty());
	}

	TEST_F(TextInputClipboardTest, CutCopiesThenDeletesSelectionAndFiresOnChange) {
		std::string changed = "unset";
		TextInput	input(TextInput::Args{.text = "hello", .onChange = [&](const std::string& value) { changed = value; }});
		input.onFocusGained();

		press(input, foundation::Key::A);
		press(input, foundation::Key::X);

		EXPECT_EQ(clipboard.contents, "hello");
		EXPECT_EQ(input.text, "");
		EXPECT_EQ(changed, "");
		EXPECT_EQ(input.cursorPosition, 0U);
	}

	TEST_F(TextInputClipboardTest, PasteInsertsAtCursor) {
		TextInput input(TextInput::Args{.text = "ab"});
		input.onFocusGained();
		clipboard.contents = "cd";

		press(input, foundation::Key::V);

		EXPECT_EQ(input.text, "abcd");
		EXPECT_EQ(input.cursorPosition, 4U);
	}

	TEST_F(TextInputClipboardTest, PasteReplacesSelection) {
		TextInput input(TextInput::Args{.text = "hello"});
		input.onFocusGained();
		clipboard.contents = "x";

		press(input, foundation::Key::A);
		press(input, foundation::Key::V);

		EXPECT_EQ(input.text, "x");
		EXPECT_EQ(input.cursorPosition, 1U);
		EXPECT_FALSE(input.selection.has_value());
	}

	TEST_F(TextInputClipboardTest, PasteStripsNewlines) {
		TextInput input(TextInput::Args{});
		input.onFocusGained();
		clipboard.contents = "a\nb\r\nc";

		press(input, foundation::Key::V);

		EXPECT_EQ(input.text, "abc");
	}

	// Primitives on the null backend, so what a component draws can be read back.
	class TextInputRenderTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			auto owned = std::make_unique<Renderer::backend::NullBackend>();
			gpu = owned.get();
			Renderer::Primitives::init(std::move(owned), {});
			Renderer::Primitives::setViewport(800, 600);
			FocusManager::setInstance(&focusManager);
		}
		void TearDown() override {
			FocusManager::setInstance(nullptr);
			Renderer::Primitives::shutdown();
		}

		// The last flush's quads, as uploaded.
		std::vector<Renderer::BatchInstance> uploaded() const {
			const std::span<const uint8_t>		 bytes = gpu->bufferContents(gpu->geometry(gpu->draws().back().call.geometry)->vertices);
			std::vector<Renderer::BatchInstance> quads(bytes.size() / sizeof(Renderer::BatchInstance));
			std::memcpy(quads.data(), bytes.data(), quads.size() * sizeof(Renderer::BatchInstance));
			return quads;
		}

		Renderer::backend::NullBackend* gpu = nullptr;
		FocusManager					focusManager;
	};

	// The text clip is in the input's own space, which the renderer maps through a
	// container's offset; the caret, at cursor 0 on the text area's left edge, draws outside it.
	TEST_F(TextInputRenderTest, TheTextClipIsInTheInputsSpaceAndTheCaretDrawsOutsideIt) {
		TextInput input(TextInput::Args{.position = {10.0F, 20.0F}, .size = {200.0F, 40.0F}});
		input.onFocusGained(); // so the caret draws

		Renderer::Primitives::beginFrame();
		Renderer::Primitives::PushTransform(glm::translate(Foundation::Mat4(1.0F), Foundation::Vec3(100.0F, 50.0F, 0.0F)));
		const Foundation::Rect clip = input.textAreaClip();
		input.render();
		Renderer::Primitives::PopTransform();
		Renderer::Primitives::endFrame();

		// Across, the text area inside the padding; down, the border's inner edge.
		const TextInputStyle style;
		EXPECT_FLOAT_EQ(clip.x, 10.0F + style.paddingLeft);
		EXPECT_FLOAT_EQ(clip.y, 20.0F + (style.borderWidth * 0.5F));
		EXPECT_FLOAT_EQ(clip.width, 200.0F - style.paddingLeft - style.paddingRight);
		EXPECT_FLOAT_EQ(clip.height, 40.0F - style.borderWidth);

		// The caret is the last thing drawn: whole, across the clip's left edge on screen, under no clip.
		const float clipLeft = clip.x + 100.0F;
		ASSERT_FALSE(gpu->draws().empty());
		const std::vector<Renderer::BatchInstance> quads = uploaded();
		ASSERT_FALSE(quads.empty());
		const Renderer::BatchInstance& caret = quads.back();
		float						   left = caret.corners[0].x;
		float						   right = left;
		for (const Foundation::Vec2& corner : caret.corners) {
			left = std::min(left, corner.x);
			right = std::max(right, corner.x);
		}
		EXPECT_EQ(caret.clip, Foundation::Vec4(0.0F));
		EXPECT_LT(left, clipLeft);
		EXPECT_GT(right, clipLeft);
	}

} // namespace UI
