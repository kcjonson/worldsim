// Text Input Scene - TextInput Component Testing and Demonstration
// Tests TextInput with focus management, Tab navigation, selection, and clipboard

#include "components/button/Button.h"
#include "components/TextInput/TextInput.h"
#include "primitives/Primitives.h"
#include "shapes/Shapes.h"
#include <GL/glew.h>
#include <application/Application.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <sstream>
#include <utils/Log.h>
#include <vector>

namespace {

constexpr const char* kSceneName = "text_input";

class TextInputScene : public engine::IScene {
	  public:
		void onEnter() override {

			// Get actual viewport dimensions
			int viewportWidth = 0;
			int viewportHeight = 0;
			Renderer::Primitives::getViewport(viewportWidth, viewportHeight);

			// Create title text
			title = UI::Text(UI::Text::Args{
				.position = {50.0F, 40.0F},
				.text = "TextInput Component Demo",
				.style = {.color = {1.0F, 1.0F, 1.0F, 1.0F}, .fontSize = 24.0F},
				.visible = true,
				.id = "title"
			});

			// Create instructions
			instructions = UI::Text(UI::Text::Args{
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
			inputs.push_back(
				std::make_unique<UI::TextInput>(UI::TextInput::Args{
					.position = {50.0F, yPos},
					.size = {400.0F, 40.0F},
					.text = "",
					.placeholder = "Basic text input (Tab index 0)",
					.tabIndex = 0,
					.id = "input1",
					.enabled = true,
					.onChange = [this](const std::string& text) { output1 = "Input 1: " + text; }
				})
			);
			yPos += spacing;

			// Text input with initial value
			inputs.push_back(
				std::make_unique<UI::TextInput>(UI::TextInput::Args{
					.position = {50.0F, yPos},
					.size = {400.0F, 40.0F},
					.text = "Initial text value",
					.placeholder = "",
					.tabIndex = 1,
					.id = "input2",
					.enabled = true,
					.onChange = [this](const std::string& text) { output2 = "Input 2: " + text; }
				})
			);
			yPos += spacing;

			// Third text input
			inputs.push_back(
				std::make_unique<UI::TextInput>(UI::TextInput::Args{
					.position = {50.0F, yPos},
					.size = {400.0F, 40.0F},
					.text = "",
					.placeholder = "Another text input",
					.tabIndex = 2,
					.id = "input3",
					.enabled = true,
					.onChange = [this](const std::string& text) { output3 = "Input 3: " + text; }
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

			inputs.push_back(
				std::make_unique<UI::TextInput>(UI::TextInput::Args{
					.position = {50.0F, yPos},
					.size = {400.0F, 45.0F},
					.text = "",
					.placeholder = "Styled input with custom colors",
					.style = styledStyle,
					.tabIndex = 3,
					.id = "input4",
					.enabled = true,
					.onChange = [this](const std::string& text) { output4 = "Input 4 (styled): " + text; }
				})
			);
			yPos += spacing;

			// Create a button to test Tab navigation integration
			button = std::make_unique<UI::Button>(UI::Button::Args{
				.label = "Test Button (Tab index 4)",
				.position = {50.0F, yPos},
				.size = {200.0F, 40.0F},
				.tabIndex = 4,
				.id = "button1",
				.onClick = []() { LOG_INFO(UI, "Button clicked!"); }
			});

			// Create output display area
			outputLabel = UI::Text(UI::Text::Args{
				.position = {500.0F, 140.0F},
				.text = "Output (onChange callbacks):",
				.style = {.color = {1.0F, 1.0F, 1.0F, 1.0F}, .fontSize = 16.0F},
				.visible = true,
				.id = "output_label"
			});
		}

		void handleInput(float /*dt*/) override {
			// Handle input for all text inputs
			for (size_t i = 0; i < inputs.size(); i++) {
				inputs[i]->handleInput();
			}

			// Handle button input
			if (button) {
				button->handleInput();
			}
		}

		void update(float dt) override {
			// Update all text inputs
			for (auto& input : inputs) {
				input->update(dt);
			}

			// Update button
			if (button) {
				button->update(dt);
			}
		}

		void render() override {
			// Clear background to dark gray
			glClearColor(0.12F, 0.12F, 0.12F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Render title and instructions
			title.render();
			instructions.render();

			// Render all text inputs
			for (auto& input : inputs) {
				input->render();
			}

			// Render button
			if (button) {
				button->render();
			}

			// Render output labels
			outputLabel.render();
			float		outputY = 180.0F;
			const float outputSpacing = 30.0F;

			std::vector<std::string> outputs = {output1, output2, output3, output4};
			for (const auto& output : outputs) {
				if (!output.empty()) {
					UI::Text outputText(UI::Text::Args{
						.position = {500.0F, outputY},
						.text = output,
						.style = {.color = {0.8F, 0.9F, 1.0F, 1.0F}, .fontSize = 14.0F},
						.visible = true,
						.id = "output"
					});
					outputText.render();
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
			helpText.render();
		}

		void onExit() override {
			inputs.clear();
			button.reset();
		}

		std::string exportState() override {
			return R"({
			"scene": "text_input",
			"description": "TextInput component testing and demonstration"
		})";
		}

		const char* getName() const override { return kSceneName; }

	  private:
		std::vector<std::unique_ptr<UI::TextInput>> inputs;
		std::unique_ptr<UI::Button>					button;

		UI::Text title;
		UI::Text instructions;
		UI::Text outputLabel;

		std::string output1;
		std::string output2;
		std::string output3;
		std::string output4;
	};

} // anonymous namespace

// Export factory and name for scene registry
namespace ui_sandbox::scenes {
	std::unique_ptr<engine::IScene> createTextInputScene() { return std::make_unique<TextInputScene>(); }
	const char* getTextInputSceneName() { return kSceneName; }
}
