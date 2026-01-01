// TextWrap Scene - Demonstrates text auto-sizing and word wrapping
// Shows Text shapes that compute their own dimensions and wrap to width

#include <GL/glew.h>

#include <graphics/Color.h>
#include <input/InputEvent.h>
#include <layout/LayoutContainer.h>
#include <layout/LayoutTypes.h>
#include <memory>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include "SceneTypes.h"
#include <shapes/Shapes.h>
#include <utils/Log.h>

namespace {

constexpr const char* kSceneName = "textwrap";

class TextWrapScene : public engine::IScene {
  public:
	const char* getName() const override { return kSceneName; }
	std::string exportState() override { return "{}"; }

	void onEnter() override {
		using namespace UI;
		using namespace Foundation;

		// Create title
		title = std::make_unique<Text>(Text::Args{
			.position = {50.0F, 30.0F},
			.text = "Text Measurement & Wrapping Demo",
			.style = {.color = Color::white(), .fontSize = 24.0F},
			.id = "title"});

		// ================================================================
		// Demo 1: Auto-sizing Text (no width/height set)
		// ================================================================
		autoSizeLabel = std::make_unique<Text>(Text::Args{
			.position = {50.0F, 80.0F},
			.text = "Auto-sizing Text (reports its own dimensions):",
			.style = {.color = Color::yellow(), .fontSize = 16.0F},
			.id = "autosize_label"});

		// Layout with auto-sizing text elements
		autoSizeLayout = std::make_unique<LayoutContainer>(LayoutContainer::Args{
			.position = {50.0F, 110.0F},
			.size = {400.0F, 200.0F},
			.direction = Direction::Vertical,
			.hAlign = HAlign::Left,
			.id = "autosize_layout"});

		// These Text elements don't set width/height - they auto-size!
		autoSizeLayout->addChild(Text(Text::Args{
			.text = "Short text",
			.style = {.color = Color::white(), .fontSize = 16.0F},
			.margin = 4.0F,
			.id = "text_short"}));

		autoSizeLayout->addChild(Text(Text::Args{
			.text = "This is a longer piece of text that should auto-size",
			.style = {.color = Color(0.7F, 0.9F, 1.0F), .fontSize = 16.0F},
			.margin = 4.0F,
			.id = "text_long"}));

		autoSizeLayout->addChild(Text(Text::Args{
			.text = "Different font size",
			.style = {.color = Color(1.0F, 0.8F, 0.6F), .fontSize = 20.0F},
			.margin = 4.0F,
			.id = "text_large"}));

		autoSizeLayout->addChild(Text(Text::Args{
			.text = "Smaller text",
			.style = {.color = Color(0.8F, 1.0F, 0.8F), .fontSize = 12.0F},
			.margin = 4.0F,
			.id = "text_small"}));

		// ================================================================
		// Demo 2: Word-wrapped Text
		// ================================================================
		wrapLabel = std::make_unique<Text>(Text::Args{
			.position = {500.0F, 80.0F},
			.text = "Word-wrapped Text (wordWrap: true + width):",
			.style = {.color = Color::yellow(), .fontSize = 16.0F},
			.id = "wrap_label"});

		// Container background to show the wrap width
		wrapBackground = std::make_unique<Rectangle>(Rectangle::Args{
			.position = {500.0F, 110.0F},
			.size = {250.0F, 180.0F},
			.style = {.fill = Color(0.2F, 0.2F, 0.25F)},
			.id = "wrap_bg"});

		// Wrapped text with explicit width
		wrappedText = std::make_unique<Text>(Text::Args{
			.position = {510.0F, 120.0F},
			.width = 230.0F,  // Set width for wrapping
			.text = "This is a longer paragraph of text that will automatically wrap to "
					"fit within the specified width. Word-based wrapping ensures that "
					"words stay intact and only break at spaces.",
			.style = {.color = Color::white(), .fontSize = 14.0F, .wordWrap = true},
			.id = "text_wrapped"});

		// ================================================================
		// Demo 3: Multi-line with explicit newlines
		// ================================================================
		newlineLabel = std::make_unique<Text>(Text::Args{
			.position = {500.0F, 310.0F},
			.text = "Explicit newlines (\\n in text):",
			.style = {.color = Color::yellow(), .fontSize = 16.0F},
			.id = "newline_label"});

		newlineBackground = std::make_unique<Rectangle>(Rectangle::Args{
			.position = {500.0F, 340.0F},
			.size = {250.0F, 100.0F},
			.style = {.fill = Color(0.2F, 0.25F, 0.2F)},
			.id = "newline_bg"});

		newlineText = std::make_unique<Text>(Text::Args{
			.position = {510.0F, 350.0F},
			.width = 230.0F,
			.text = "Line one\nLine two\nLine three with more words",
			.style = {.color = Color::white(), .fontSize = 14.0F, .wordWrap = true},
			.id = "text_newline"});

		// ================================================================
		// Demo 4: Alignment with wrapped text
		// ================================================================
		alignLabel = std::make_unique<Text>(Text::Args{
			.position = {50.0F, 340.0F},
			.text = "Per-line alignment (Center):",
			.style = {.color = Color::yellow(), .fontSize = 16.0F},
			.id = "align_label"});

		alignBackground = std::make_unique<Rectangle>(Rectangle::Args{
			.position = {50.0F, 370.0F},
			.size = {350.0F, 120.0F},
			.style = {.fill = Color(0.25F, 0.2F, 0.25F)},
			.id = "align_bg"});

		alignedText = std::make_unique<Text>(Text::Args{
			.position = {50.0F, 380.0F},
			.width = 350.0F,
			.text = "This wrapped text is center-aligned. Each line is independently "
					"centered within the container width.",
			.style = {.color = Color::white(),
					  .fontSize = 14.0F,
					  .hAlign = HorizontalAlign::Center,
					  .wordWrap = true},
			.id = "text_centered"});

		LOG_INFO(UI, "TextWrap scene initialized");
	}

	void onExit() override {
		title.reset();
		autoSizeLabel.reset();
		autoSizeLayout.reset();
		wrapLabel.reset();
		wrapBackground.reset();
		wrappedText.reset();
		newlineLabel.reset();
		newlineBackground.reset();
		newlineText.reset();
		alignLabel.reset();
		alignBackground.reset();
		alignedText.reset();
		LOG_INFO(UI, "TextWrap scene exited");
	}

	bool handleInput(UI::InputEvent& event) override {
		if (autoSizeLayout && autoSizeLayout->handleEvent(event)) {
			return true;
		}
		return false;
	}

	void update(float deltaTime) override {
		if (autoSizeLayout) {
			autoSizeLayout->update(deltaTime);
		}
	}

	void render() override {
		// Clear background
		glClearColor(0.12F, 0.12F, 0.15F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);

		// Render labels
		if (title) {
			title->render();
		}

		// Demo 1: Auto-sizing
		if (autoSizeLabel) {
			autoSizeLabel->render();
		}
		if (autoSizeLayout) {
			autoSizeLayout->render();
		}

		// Demo 2: Word wrapping
		if (wrapLabel) {
			wrapLabel->render();
		}
		if (wrapBackground) {
			wrapBackground->render();
		}
		if (wrappedText) {
			wrappedText->render();
		}

		// Demo 3: Explicit newlines
		if (newlineLabel) {
			newlineLabel->render();
		}
		if (newlineBackground) {
			newlineBackground->render();
		}
		if (newlineText) {
			newlineText->render();
		}

		// Demo 4: Alignment
		if (alignLabel) {
			alignLabel->render();
		}
		if (alignBackground) {
			alignBackground->render();
		}
		if (alignedText) {
			alignedText->render();
		}
	}

  private:
	// Title
	std::unique_ptr<UI::Text> title;

	// Demo 1: Auto-sizing
	std::unique_ptr<UI::Text>			 autoSizeLabel;
	std::unique_ptr<UI::LayoutContainer> autoSizeLayout;

	// Demo 2: Word wrapping
	std::unique_ptr<UI::Text>	   wrapLabel;
	std::unique_ptr<UI::Rectangle> wrapBackground;
	std::unique_ptr<UI::Text>	   wrappedText;

	// Demo 3: Explicit newlines
	std::unique_ptr<UI::Text>	   newlineLabel;
	std::unique_ptr<UI::Rectangle> newlineBackground;
	std::unique_ptr<UI::Text>	   newlineText;

	// Demo 4: Alignment
	std::unique_ptr<UI::Text>	   alignLabel;
	std::unique_ptr<UI::Rectangle> alignBackground;
	std::unique_ptr<UI::Text>	   alignedText;
};

} // anonymous namespace

// Export scene info for registry
namespace ui_sandbox::scenes {
extern const ui_sandbox::SceneInfo TextWrap = {kSceneName, []() { return std::make_unique<TextWrapScene>(); }};
}
