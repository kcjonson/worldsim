// Tooltip Scene - Demonstrates the Tooltip and TooltipManager system
// Shows tooltips with different content configurations

#include <GL/glew.h>

#include "SceneTypes.h"
#include <components/button/Button.h>
#include <components/tooltip/Tooltip.h>
#include <components/tooltip/TooltipManager.h>
#include <graphics/Color.h>
#include <input/InputEvent.h>
#include <memory>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <shapes/Shapes.h>
#include <theme/Theme.h>
#include <utils/Log.h>

namespace {

constexpr const char* kSceneName = "tooltip";

// Button with associated tooltip content
struct TooltipButton {
	std::unique_ptr<UI::Button> button;
	UI::TooltipContent			content;
};

class TooltipScene : public engine::IScene {
  public:
	const char* getName() const override { return kSceneName; }
	std::string exportState() override { return "{}"; }

	void onEnter() override {
		using namespace UI;
		using namespace Foundation;

		// Set up tooltip manager
		UI::TooltipManager::setInstance(&tooltipManager);
		tooltipManager.setScreenBounds(800.0F, 600.0F);

		// Create title
		title = std::make_unique<Text>(Text::Args{
			.position = {50.0F, 30.0F},
			.text = "Tooltip System Demo",
			.style = {.color = Color::white(), .fontSize = 20.0F},
			.id = "title"});

		// ================================================================
		// Demo 1: Title-only tooltips
		// ================================================================
		label1 = std::make_unique<Text>(Text::Args{
			.position = {50.0F, 70.0F},
			.text = "1. Title-only tooltips (hover over buttons):",
			.style = {.color = Color::yellow(), .fontSize = 14.0F},
			.id = "label_1"});

		buttons.push_back({
			std::make_unique<Button>(Button::Args{
				.label = "Save",
				.position = {50.0F, 95.0F},
				.size = {80.0F, 36.0F},
			}),
			{.title = "Save File"},
		});
		buttons.push_back({
			std::make_unique<Button>(Button::Args{
				.label = "Load",
				.position = {140.0F, 95.0F},
				.size = {80.0F, 36.0F},
			}),
			{.title = "Load File"},
		});
		buttons.push_back({
			std::make_unique<Button>(Button::Args{
				.label = "New",
				.position = {230.0F, 95.0F},
				.size = {80.0F, 36.0F},
			}),
			{.title = "New Document"},
		});

		// ================================================================
		// Demo 2: Title + Description
		// ================================================================
		label2 = std::make_unique<Text>(Text::Args{
			.position = {50.0F, 160.0F},
			.text = "2. Title + Description:",
			.style = {.color = Color::yellow(), .fontSize = 14.0F},
			.id = "label_2"});

		buttons.push_back({
			std::make_unique<Button>(Button::Args{
				.label = "Cut",
				.position = {50.0F, 185.0F},
				.size = {80.0F, 36.0F},
			}),
			{.title = "Cut", .description = "Remove selection and copy to clipboard"},
		});
		buttons.push_back({
			std::make_unique<Button>(Button::Args{
				.label = "Copy",
				.position = {140.0F, 185.0F},
				.size = {80.0F, 36.0F},
			}),
			{.title = "Copy", .description = "Copy selection to clipboard"},
		});
		buttons.push_back({
			std::make_unique<Button>(Button::Args{
				.label = "Paste",
				.position = {230.0F, 185.0F},
				.size = {80.0F, 36.0F},
			}),
			{.title = "Paste", .description = "Insert from clipboard at cursor"},
		});

		// ================================================================
		// Demo 3: Full tooltip (title + description + hotkey)
		// ================================================================
		label3 = std::make_unique<Text>(Text::Args{
			.position = {50.0F, 250.0F},
			.text = "3. Full tooltips (title + description + hotkey):",
			.style = {.color = Color::yellow(), .fontSize = 14.0F},
			.id = "label_3"});

		buttons.push_back({
			std::make_unique<Button>(Button::Args{
				.label = "Undo",
				.position = {50.0F, 275.0F},
				.size = {80.0F, 36.0F},
			}),
			{.title = "Undo", .description = "Revert the last action", .hotkey = "Ctrl+Z"},
		});
		buttons.push_back({
			std::make_unique<Button>(Button::Args{
				.label = "Redo",
				.position = {140.0F, 275.0F},
				.size = {80.0F, 36.0F},
			}),
			{.title = "Redo", .description = "Repeat the last undone action", .hotkey = "Ctrl+Y"},
		});
		buttons.push_back({
			std::make_unique<Button>(Button::Args{
				.label = "Find",
				.position = {230.0F, 275.0F},
				.size = {80.0F, 36.0F},
			}),
			{.title = "Find", .description = "Search for text in document", .hotkey = "Ctrl+F"},
		});

		// ================================================================
		// Demo 4: Edge positioning
		// ================================================================
		label4 = std::make_unique<Text>(Text::Args{
			.position = {50.0F, 340.0F},
			.text = "4. Edge positioning (tooltip stays on screen):",
			.style = {.color = Color::yellow(), .fontSize = 14.0F},
			.id = "label_4"});

		// Button near right edge
		buttons.push_back({
			std::make_unique<Button>(Button::Args{
				.label = "Right Edge",
				.position = {650.0F, 365.0F},
				.size = {120.0F, 36.0F},
			}),
			{.title = "Right Edge", .description = "This tooltip flips to the left"},
		});

		// Button near bottom edge
		buttons.push_back({
			std::make_unique<Button>(Button::Args{
				.label = "Bottom Edge",
				.position = {50.0F, 530.0F},
				.size = {120.0F, 36.0F},
			}),
			{.title = "Bottom Edge", .description = "This tooltip flips upward"},
		});

		// ================================================================
		// Instructions
		// ================================================================
		instructions = std::make_unique<Text>(Text::Args{
			.position = {50.0F, 450.0F},
			.text = "Hover over buttons for 0.5s to see tooltips | Move mouse to reposition",
			.style = {.color = Color(0.6F, 0.6F, 0.7F, 1.0F), .fontSize = 12.0F},
			.id = "instructions"});

		LOG_INFO(UI, "Tooltip scene initialized");
	}

	void onExit() override {
		UI::TooltipManager::setInstance(nullptr);
		title.reset();
		label1.reset();
		label2.reset();
		label3.reset();
		label4.reset();
		instructions.reset();
		buttons.clear();
		LOG_INFO(UI, "Tooltip scene exited");
	}

	bool handleInput(UI::InputEvent& event) override {
		using namespace UI;

		// Track hover state for tooltip triggering
		if (event.type == InputEvent::Type::MouseMove) {
			bool foundHover = false;

			// Check all buttons for hover
			for (auto& tb : buttons) {
				if (tb.button && tb.button->containsPoint(event.position)) {
					if (currentHoveredButton != tb.button.get()) {
						currentHoveredButton = tb.button.get();
						tooltipManager.startHover(tb.content, event.position);
					} else {
						tooltipManager.updateCursorPosition(event.position);
					}
					foundHover = true;
					break;
				}
			}

			// If we're no longer hovering any button, end hover
			if (!foundHover && currentHoveredButton != nullptr) {
				tooltipManager.endHover();
				currentHoveredButton = nullptr;
			}
		}

		// Dispatch to buttons
		for (auto& tb : buttons) {
			if (tb.button && tb.button->handleEvent(event)) {
				return true;
			}
		}

		return false;
	}

	void update(float deltaTime) override {
		tooltipManager.update(deltaTime);

		for (auto& tb : buttons) {
			if (tb.button) {
				tb.button->update(deltaTime);
			}
		}
	}

	void render() override {
		// Clear background
		glClearColor(0.10F, 0.10F, 0.13F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);

		// Render labels
		if (title) {
			title->render();
		}
		if (label1) {
			label1->render();
		}
		if (label2) {
			label2->render();
		}
		if (label3) {
			label3->render();
		}
		if (label4) {
			label4->render();
		}
		if (instructions) {
			instructions->render();
		}

		// Render buttons
		for (auto& tb : buttons) {
			if (tb.button) {
				tb.button->render();
			}
		}

		// Render tooltip (on top of everything)
		tooltipManager.render();
	}

  private:
	// Tooltip manager
	UI::TooltipManager tooltipManager;

	// Labels
	std::unique_ptr<UI::Text> title;
	std::unique_ptr<UI::Text> label1;
	std::unique_ptr<UI::Text> label2;
	std::unique_ptr<UI::Text> label3;
	std::unique_ptr<UI::Text> label4;
	std::unique_ptr<UI::Text> instructions;

	// Buttons with their tooltip content
	std::vector<TooltipButton> buttons;

	// State
	UI::Button* currentHoveredButton = nullptr;
};

} // anonymous namespace

// Export scene info for registry
namespace ui_sandbox::scenes {
extern const ui_sandbox::SceneInfo Tooltip_ = {kSceneName, []() { return std::make_unique<TooltipScene>(); }};
} // namespace ui_sandbox::scenes
