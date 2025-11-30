// Button Scene - UI Button Component Showcase
// Demonstrates Button component with all states: Normal, Hover, Pressed, Disabled, Focused

#include <GL/glew.h>

#include <components/button/button.h>
#include <graphics/color.h>
#include <input/input_manager.h>
#include <input/input_types.h>
#include <memory>
#include <primitives/primitives.h>
#include <scene/scene.h>
#include <scene/scene_manager.h>
#include <shapes/shapes.h>
#include <utils/log.h>
#include <vector>

namespace {

	class ButtonScene : public engine::IScene {
	  public:
		const char* getName() const override { return "button"; }
		std::string exportState() override { return "{}"; }

		void onEnter() override {
			using namespace UI;
			using namespace Foundation;

			// Create title text
			labels.push_back(std::make_unique<Text>(Text::Args{
				.position = {50.0F, 30.0F},
				.text = "Button Component Demo - Click, Hover, Tab to Focus, Enter to Activate",
				.style = {.color = Color::white(), .fontSize = 20.0F},
				.id = "title"
			}));

			// Row 1: Primary Buttons
			labels.push_back(std::make_unique<Text>(Text::Args{
				.position = {50.0F, 80.0F},
				.text = "Primary Buttons:",
				.style = {.color = Color::yellow(), .fontSize = 16.0F},
				.id = "primary_label"
			}));

			// Normal clickable button
			buttons.push_back(std::make_unique<Button>(Button::Args{
				.label = "Click Me!",
				.position = {50.0F, 110.0F},
				.size = {150.0F, 40.0F},
				.type = Button::Type::Primary,
				.onClick = [this]() {
					clickCount++;
					LOG_INFO(UI, "Button clicked! Count: {}", clickCount);
				},
				.id = "primary_button_1"
			}));

			// Another clickable button
			buttons.push_back(std::make_unique<Button>(Button::Args{
				.label = "Another Button",
				.position = {220.0F, 110.0F},
				.size = {170.0F, 40.0F},
				.type = Button::Type::Primary,
				.onClick = []() { LOG_INFO(UI, "Second button clicked!"); },
				.id = "primary_button_2"
			}));

			// Disabled button
			buttons.push_back(std::make_unique<Button>(Button::Args{
				.label = "Disabled",
				.position = {410.0F, 110.0F},
				.size = {150.0F, 40.0F},
				.type = Button::Type::Primary,
				.disabled = true,
				.onClick = []() { LOG_WARNING(UI, "This should never fire!"); },
				.id = "primary_button_disabled"
			}));

			// Row 2: Secondary Buttons
			labels.push_back(std::make_unique<Text>(Text::Args{
				.position = {50.0F, 180.0F},
				.text = "Secondary Buttons:",
				.style = {.color = Color::yellow(), .fontSize = 16.0F},
				.id = "secondary_label"
			}));

			buttons.push_back(std::make_unique<Button>(Button::Args{
				.label = "Secondary",
				.position = {50.0F, 210.0F},
				.size = {150.0F, 40.0F},
				.type = Button::Type::Secondary,
				.onClick = []() { LOG_INFO(UI, "Secondary button clicked!"); },
				.id = "secondary_button_1"
			}));

			buttons.push_back(std::make_unique<Button>(Button::Args{
				.label = "Another Secondary",
				.position = {220.0F, 210.0F},
				.size = {200.0F, 40.0F},
				.type = Button::Type::Secondary,
				.onClick = []() { LOG_INFO(UI, "Second secondary button clicked!"); },
				.id = "secondary_button_2"
			}));

			// Row 3: Different sizes
			labels.push_back(std::make_unique<Text>(Text::Args{
				.position = {50.0F, 280.0F},
				.text = "Different Sizes:",
				.style = {.color = Color::yellow(), .fontSize = 16.0F},
				.id = "size_label"
			}));

			buttons.push_back(std::make_unique<Button>(Button::Args{
				.label = "Small",
				.position = {50.0F, 310.0F},
				.size = {100.0F, 30.0F},
				.type = Button::Type::Primary,
				.onClick = []() { LOG_INFO(UI, "Small button clicked!"); },
				.id = "small_button"
			}));

			buttons.push_back(std::make_unique<Button>(Button::Args{
				.label = "Large Button",
				.position = {170.0F, 310.0F},
				.size = {250.0F, 50.0F},
				.type = Button::Type::Secondary,
				.onClick = []() { LOG_INFO(UI, "Large button clicked!"); },
				.id = "large_button"
			}));

			// Row 4: Focus demonstration
			labels.push_back(std::make_unique<Text>(Text::Args{
				.position = {50.0F, 390.0F},
				.text = "Focus (Press Tab to cycle, Enter to activate):",
				.style = {.color = Color::yellow(), .fontSize = 16.0F},
				.id = "focus_label"
			}));

			buttons.push_back(std::make_unique<Button>(Button::Args{
				.label = "Focusable 1",
				.position = {50.0F, 420.0F},
				.size = {150.0F, 40.0F},
				.type = Button::Type::Primary,
				.onClick = []() { LOG_INFO(UI, "Focusable 1 activated!"); },
				.id = "focusable_1"
			}));

			buttons.push_back(std::make_unique<Button>(Button::Args{
				.label = "Focusable 2",
				.position = {220.0F, 420.0F},
				.size = {150.0F, 40.0F},
				.type = Button::Type::Primary,
				.onClick = []() { LOG_INFO(UI, "Focusable 2 activated!"); },
				.id = "focusable_2"
			}));

			buttons.push_back(std::make_unique<Button>(Button::Args{
				.label = "Focusable 3",
				.position = {390.0F, 420.0F},
				.size = {150.0F, 40.0F},
				.type = Button::Type::Primary,
				.onClick = []() { LOG_INFO(UI, "Focusable 3 activated!"); },
				.id = "focusable_3"
			}));

			// Click counter display
			clickCounterText = std::make_unique<Text>(Text::Args{
				.position = {600.0F, 110.0F},
				.text = "Clicks: 0",
				.style = {.color = Color::green(), .fontSize = 18.0F},
				.id = "click_counter"
			});

			LOG_INFO(UI, "Button scene initialized with {} buttons", buttons.size());
		}

		void onExit() override {
			buttons.clear();
			labels.clear();
			clickCounterText.reset();
			LOG_INFO(UI, "Button scene exited");
		}

		void handleInput(float /*deltaTime*/) override {
			// Update all buttons' input state
			for (auto& button : buttons) {
				button->handleInput();
			}
		}

		void update(float deltaTime) override {
			// Update all buttons
			for (auto& button : buttons) {
				button->update(deltaTime);
			}

			// Update click counter text
			if (lastClickCount != clickCount) {
				clickCounterText->text = "Clicks: " + std::to_string(clickCount);
				lastClickCount = clickCount;
			}
		}

		void render() override {
			// Clear background
			glClearColor(0.12F, 0.12F, 0.15F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Render labels
			for (auto& label : labels) {
				label->render();
			}

			// Render click counter
			if (clickCounterText) {
				clickCounterText->render();
			}

			// Render all buttons
			for (auto& button : buttons) {
				button->render();
			}
		}

	  private:
		// UI Components
		std::vector<std::unique_ptr<UI::Button>> buttons;
		std::vector<std::unique_ptr<UI::Text>> labels;
		std::unique_ptr<UI::Text> clickCounterText;

		// Click tracking
		int clickCount{0};
		int lastClickCount{0};
	};

	// Register scene with scene manager
	static bool registered = []() {
		engine::SceneManager::Get().registerScene("button", []() { return std::make_unique<ButtonScene>(); });
		return true;
	}();

} // anonymous namespace
