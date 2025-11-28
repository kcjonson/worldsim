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
		const char* GetName() const override { return "button"; }
		std::string ExportState() override { return "{}"; }

		void OnEnter() override {
			using namespace UI;
			using namespace Foundation;

			// Create title text
			m_labels.push_back(std::make_unique<Text>(Text::Args{
				.position = {50.0F, 30.0F},
				.text = "Button Component Demo - Click, Hover, Tab to Focus, Enter to Activate",
				.style = {.color = Color::White(), .fontSize = 20.0F},
				.id = "title"
			}));

			// Row 1: Primary Buttons
			m_labels.push_back(std::make_unique<Text>(Text::Args{
				.position = {50.0F, 80.0F},
				.text = "Primary Buttons:",
				.style = {.color = Color::Yellow(), .fontSize = 16.0F},
				.id = "primary_label"
			}));

			// Normal clickable button
			m_buttons.push_back(std::make_unique<Button>(Button::Args{
				.label = "Click Me!",
				.position = {50.0F, 110.0F},
				.size = {150.0F, 40.0F},
				.type = Button::Type::Primary,
				.onClick = [this]() {
					m_clickCount++;
					LOG_INFO(UI, "Button clicked! Count: {}", m_clickCount);
				},
				.id = "primary_button_1"
			}));

			// Another clickable button
			m_buttons.push_back(std::make_unique<Button>(Button::Args{
				.label = "Another Button",
				.position = {220.0F, 110.0F},
				.size = {170.0F, 40.0F},
				.type = Button::Type::Primary,
				.onClick = []() { LOG_INFO(UI, "Second button clicked!"); },
				.id = "primary_button_2"
			}));

			// Disabled button
			m_buttons.push_back(std::make_unique<Button>(Button::Args{
				.label = "Disabled",
				.position = {410.0F, 110.0F},
				.size = {150.0F, 40.0F},
				.type = Button::Type::Primary,
				.disabled = true,
				.onClick = []() { LOG_WARNING(UI, "This should never fire!"); },
				.id = "primary_button_disabled"
			}));

			// Row 2: Secondary Buttons
			m_labels.push_back(std::make_unique<Text>(Text::Args{
				.position = {50.0F, 180.0F},
				.text = "Secondary Buttons:",
				.style = {.color = Color::Yellow(), .fontSize = 16.0F},
				.id = "secondary_label"
			}));

			m_buttons.push_back(std::make_unique<Button>(Button::Args{
				.label = "Secondary",
				.position = {50.0F, 210.0F},
				.size = {150.0F, 40.0F},
				.type = Button::Type::Secondary,
				.onClick = []() { LOG_INFO(UI, "Secondary button clicked!"); },
				.id = "secondary_button_1"
			}));

			m_buttons.push_back(std::make_unique<Button>(Button::Args{
				.label = "Another Secondary",
				.position = {220.0F, 210.0F},
				.size = {200.0F, 40.0F},
				.type = Button::Type::Secondary,
				.onClick = []() { LOG_INFO(UI, "Second secondary button clicked!"); },
				.id = "secondary_button_2"
			}));

			// Row 3: Different sizes
			m_labels.push_back(std::make_unique<Text>(Text::Args{
				.position = {50.0F, 280.0F},
				.text = "Different Sizes:",
				.style = {.color = Color::Yellow(), .fontSize = 16.0F},
				.id = "size_label"
			}));

			m_buttons.push_back(std::make_unique<Button>(Button::Args{
				.label = "Small",
				.position = {50.0F, 310.0F},
				.size = {100.0F, 30.0F},
				.type = Button::Type::Primary,
				.onClick = []() { LOG_INFO(UI, "Small button clicked!"); },
				.id = "small_button"
			}));

			m_buttons.push_back(std::make_unique<Button>(Button::Args{
				.label = "Large Button",
				.position = {170.0F, 310.0F},
				.size = {250.0F, 50.0F},
				.type = Button::Type::Secondary,
				.onClick = []() { LOG_INFO(UI, "Large button clicked!"); },
				.id = "large_button"
			}));

			// Row 4: Focus demonstration
			m_labels.push_back(std::make_unique<Text>(Text::Args{
				.position = {50.0F, 390.0F},
				.text = "Focus (Press Tab to cycle, Enter to activate):",
				.style = {.color = Color::Yellow(), .fontSize = 16.0F},
				.id = "focus_label"
			}));

			m_buttons.push_back(std::make_unique<Button>(Button::Args{
				.label = "Focusable 1",
				.position = {50.0F, 420.0F},
				.size = {150.0F, 40.0F},
				.type = Button::Type::Primary,
				.onClick = []() { LOG_INFO(UI, "Focusable 1 activated!"); },
				.id = "focusable_1"
			}));

			m_buttons.push_back(std::make_unique<Button>(Button::Args{
				.label = "Focusable 2",
				.position = {220.0F, 420.0F},
				.size = {150.0F, 40.0F},
				.type = Button::Type::Primary,
				.onClick = []() { LOG_INFO(UI, "Focusable 2 activated!"); },
				.id = "focusable_2"
			}));

			m_buttons.push_back(std::make_unique<Button>(Button::Args{
				.label = "Focusable 3",
				.position = {390.0F, 420.0F},
				.size = {150.0F, 40.0F},
				.type = Button::Type::Primary,
				.onClick = []() { LOG_INFO(UI, "Focusable 3 activated!"); },
				.id = "focusable_3"
			}));

			// Click counter display
			m_clickCounterText = std::make_unique<Text>(Text::Args{
				.position = {600.0F, 110.0F},
				.text = "Clicks: 0",
				.style = {.color = Color::Green(), .fontSize = 18.0F},
				.id = "click_counter"
			});

			LOG_INFO(UI, "Button scene initialized with {} buttons", m_buttons.size());
		}

		void OnExit() override {
			m_buttons.clear();
			m_labels.clear();
			m_clickCounterText.reset();
			LOG_INFO(UI, "Button scene exited");
		}

		void HandleInput(float /*deltaTime*/) override {
			// Update all buttons' input state
			for (auto& button : m_buttons) {
				button->HandleInput();
			}
		}

		void Update(float deltaTime) override {
			// Update all buttons
			for (auto& button : m_buttons) {
				button->Update(deltaTime);
			}

			// Update click counter text
			if (m_lastClickCount != m_clickCount) {
				m_clickCounterText->text = "Clicks: " + std::to_string(m_clickCount);
				m_lastClickCount = m_clickCount;
			}
		}

		void Render() override {
			// Clear background
			glClearColor(0.12F, 0.12F, 0.15F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Render labels
			for (auto& label : m_labels) {
				label->Render();
			}

			// Render click counter
			if (m_clickCounterText) {
				m_clickCounterText->Render();
			}

			// Render all buttons
			for (auto& button : m_buttons) {
				button->Render();
			}
		}

	  private:
		// UI Components
		std::vector<std::unique_ptr<UI::Button>> m_buttons;
		std::vector<std::unique_ptr<UI::Text>> m_labels;
		std::unique_ptr<UI::Text> m_clickCounterText;

		// Click tracking
		int m_clickCount{0};
		int m_lastClickCount{0};
	};

	// Register scene with scene manager
	static bool registered = []() {
		engine::SceneManager::Get().RegisterScene("button", []() { return std::make_unique<ButtonScene>(); });
		return true;
	}();

} // anonymous namespace
