// Text Input Scene - TextInput Component Testing and Demonstration
// Tests TextInput with focus management, Tab navigation, selection, and clipboard

#include "components/button/button.h"
#include "components/text_input/text_input.h"
#include "primitives/primitives.h"
#include "shapes/shapes.h"
#include <GL/glew.h>
#include <application/application.h>
#include <scene/scene.h>
#include <scene/scene_manager.h>
#include <sstream>
#include <utils/log.h>
#include <vector>

namespace {

	class TextInputScene : public engine::IScene {
	  public:
		void OnEnter() override {

			// Get actual viewport dimensions
			int viewportWidth = 0;
			int viewportHeight = 0;
			Renderer::Primitives::GetViewport(viewportWidth, viewportHeight);

			// Create title text
			m_title = UI::Text(UI::Text::Args{
				.position = {50.0F, 40.0F},
				.text = "TextInput Component Demo",
				.style = {.color = {1.0F, 1.0F, 1.0F, 1.0F}, .fontSize = 24.0F},
				.visible = true,
				.id = "title"
			});

			// Create instructions
			m_instructions = UI::Text(UI::Text::Args{
				.position = {50.0F, 80.0F},
				.text = "Use Tab to navigate between fields. Try selection (Shift+Arrow, mouse drag) and clipboard (Ctrl+C/X/V/A)",
				.style = {.color = {0.7F, 0.7F, 0.7F, 1.0F}, .fontSize = 14.0F},
				.visible = true,
				.id = "instructions"
			});

			// Create text input components with different configurations
			float		yPos = 140.0F;
			const float spacing = 80.0F;

			// Basic text input
			m_inputs.push_back(
				std::make_unique<UI::TextInput>(UI::TextInput::Args{
					.position = {50.0F, yPos},
					.size = {400.0F, 40.0F},
					.text = "",
					.placeholder = "Basic text input (Tab index 0)",
					.tabIndex = 0,
					.id = "input1",
					.enabled = true,
					.onChange = [this](const std::string& text) { m_output1 = "Input 1: " + text; }
				})
			);
			yPos += spacing;

			// Text input with initial value
			m_inputs.push_back(
				std::make_unique<UI::TextInput>(UI::TextInput::Args{
					.position = {50.0F, yPos},
					.size = {400.0F, 40.0F},
					.text = "Initial text value",
					.placeholder = "",
					.tabIndex = 1,
					.id = "input2",
					.enabled = true,
					.onChange = [this](const std::string& text) { m_output2 = "Input 2: " + text; }
				})
			);
			yPos += spacing;

			// Third text input
			m_inputs.push_back(
				std::make_unique<UI::TextInput>(UI::TextInput::Args{
					.position = {50.0F, yPos},
					.size = {400.0F, 40.0F},
					.text = "",
					.placeholder = "Another text input",
					.tabIndex = 2,
					.id = "input3",
					.enabled = true,
					.onChange = [this](const std::string& text) { m_output3 = "Input 3: " + text; }
				})
			);
			yPos += spacing;

			// Styled text input
			UI::TextInputStyle styledStyle;
			styledStyle.backgroundColor = {0.1F, 0.15F, 0.2F, 1.0F};
			styledStyle.borderColor = {0.3F, 0.6F, 0.9F, 1.0F};
			styledStyle.focusedBorderColor = {0.5F, 0.8F, 1.0F, 1.0F};
			styledStyle.textColor = {0.9F, 0.95F, 1.0F, 1.0F};
			styledStyle.selectionColor = {0.4F, 0.6F, 1.0F, 0.4F};
			styledStyle.cursorColor = {0.5F, 0.8F, 1.0F, 1.0F};
			styledStyle.borderWidth = 2.0F;
			styledStyle.fontSize = 18.0F;

			m_inputs.push_back(
				std::make_unique<UI::TextInput>(UI::TextInput::Args{
					.position = {50.0F, yPos},
					.size = {400.0F, 45.0F},
					.text = "",
					.placeholder = "Styled input with custom colors",
					.style = styledStyle,
					.tabIndex = 3,
					.id = "input4",
					.enabled = true,
					.onChange = [this](const std::string& text) { m_output4 = "Input 4 (styled): " + text; }
				})
			);
			yPos += spacing;

			// Create a button to test Tab navigation integration
			m_button = std::make_unique<UI::Button>(UI::Button::Args{
				.label = "Test Button (Tab index 4)",
				.position = {50.0F, yPos},
				.size = {200.0F, 40.0F},
				.tabIndex = 4,
				.id = "button1",
				.onClick = []() { LOG_INFO(UI, "Button clicked!"); }
			});

			// Create output display area
			m_outputLabel = UI::Text(UI::Text::Args{
				.position = {500.0F, 140.0F},
				.text = "Output (onChange callbacks):",
				.style = {.color = {1.0F, 1.0F, 1.0F, 1.0F}, .fontSize = 16.0F},
				.visible = true,
				.id = "output_label"
			});
		}

		void HandleInput(float /*dt*/) override {
			// Handle input for all text inputs
			for (size_t i = 0; i < m_inputs.size(); i++) {
				m_inputs[i]->HandleInput();
			}

			// Handle button input
			if (m_button) {
				m_button->HandleInput();
			}
		}

		void Update(float dt) override {
			// Update all text inputs
			for (auto& input : m_inputs) {
				input->Update(dt);
			}

			// Update button
			if (m_button) {
				m_button->Update(dt);
			}
		}

		void Render() override {
			// Clear background to dark gray
			glClearColor(0.12F, 0.12F, 0.12F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Render title and instructions
			m_title.Render();
			m_instructions.Render();

			// Render all text inputs
			for (auto& input : m_inputs) {
				input->Render();
			}

			// Render button
			if (m_button) {
				m_button->Render();
			}

			// Render output labels
			m_outputLabel.Render();
			float		outputY = 180.0F;
			const float outputSpacing = 30.0F;

			std::vector<std::string> outputs = {m_output1, m_output2, m_output3, m_output4};
			for (const auto& output : outputs) {
				if (!output.empty()) {
					UI::Text outputText(UI::Text::Args{
						.position = {500.0F, outputY},
						.text = output,
						.style = {.color = {0.8F, 0.9F, 1.0F, 1.0F}, .fontSize = 14.0F},
						.visible = true,
						.id = "output"
					});
					outputText.Render();
					outputY += outputSpacing;
				}
			}

			// Render help text
			UI::Text helpText(UI::Text::Args{
				.position = {500.0F, 400.0F},
				.text = "Keyboard Shortcuts:\n"
						"  Ctrl+C: Copy\n"
						"  Ctrl+X: Cut\n"
						"  Ctrl+V: Paste\n"
						"  Ctrl+A: Select All\n"
						"  Shift+Arrows: Extend selection\n"
						"  Tab: Next field",
				.style = {.color = {0.6F, 0.6F, 0.6F, 1.0F}, .fontSize = 13.0F},
				.visible = true,
				.id = "help"
			});
			helpText.Render();
		}

		void OnExit() override {
			m_inputs.clear();
			m_button.reset();
		}

		std::string ExportState() override {
			return R"({
			"scene": "text_input",
			"description": "TextInput component testing and demonstration"
		})";
		}

		const char* GetName() const override { return "text_input"; }

	  private:
		std::vector<std::unique_ptr<UI::TextInput>> m_inputs;
		std::unique_ptr<UI::Button>					m_button;

		UI::Text m_title;
		UI::Text m_instructions;
		UI::Text m_outputLabel;

		std::string m_output1;
		std::string m_output2;
		std::string m_output3;
		std::string m_output4;
	};

	// Register scene with SceneManager
	bool g_registered = []() {
		engine::SceneManager::Get().RegisterScene("text_input", []() { return std::make_unique<TextInputScene>(); });
		return true;
	}();

} // anonymous namespace
