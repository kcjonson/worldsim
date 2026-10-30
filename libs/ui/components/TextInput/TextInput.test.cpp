#include "TextInput.h"

#include "clipboard/IClipboard.h"
#include "focus/FocusManager.h"

#include <backend/NullBackend.h>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>
#include <primitives/Primitives.h>

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

		// The last flush's vertices, as uploaded.
		std::vector<Renderer::BatchVertex> uploaded() const {
			const std::span<const uint8_t>	   bytes = gpu->bufferContents(gpu->geometry(gpu->draws().back().call.geometry)->vertices);
			std::vector<Renderer::BatchVertex> vertices(bytes.size() / sizeof(Renderer::BatchVertex));
			std::memcpy(vertices.data(), bytes.data(), vertices.size() * sizeof(Renderer::BatchVertex));
			return vertices;
		}

		Renderer::backend::NullBackend* gpu = nullptr;
		FocusManager					focusManager;
	};

	// The text area's clip is in screen space, so under a container's offset it moves with the input.
	TEST_F(TextInputRenderTest, TheTextAreaClipFollowsTheOffsetTheInputIsDrawnUnder) {
		TextInput input(TextInput::Args{.position = {10.0F, 20.0F}, .size = {200.0F, 40.0F}});
		input.onFocusGained(); // so the cursor draws, under the clip

		Renderer::Primitives::beginFrame();
		Renderer::Primitives::PushTransform(glm::translate(Foundation::Mat4(1.0F), Foundation::Vec3(100.0F, 50.0F, 0.0F)));
		input.render();
		Renderer::Primitives::PopTransform();
		Renderer::Primitives::endFrame();

		ASSERT_FALSE(gpu->draws().empty());
		const std::vector<Renderer::BatchVertex> vertices = uploaded();
		ASSERT_FALSE(vertices.empty());
		const TextInputStyle style;
		EXPECT_EQ(
			vertices.back().clipBounds,
			Foundation::Vec4(
				110.0F + style.paddingLeft, 70.0F + style.paddingTop, 310.0F - style.paddingRight, 110.0F - style.paddingBottom
			)
		);
	}

} // namespace UI
