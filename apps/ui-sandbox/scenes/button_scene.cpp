// Button Scene - UI Button Component Showcase
// Demonstrates Button component with all states: Normal, Hover, Pressed, Disabled, Focused

#include <components/button/button.h>
#include <font/font_renderer.h>
#include <font/text_batch_renderer.h>
#include <graphics/color.h>
#include <input/input_manager.h>
#include <input/input_types.h>
#include <layer/layer_manager.h>
#include <primitives/primitives.h>
#include <scene/scene.h>
#include <scene/scene_manager.h>
#include <shapes/shapes.h>
#include <utils/log.h>

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>

namespace {

	class ButtonScene : public engine::IScene {
	  public:
		const char* GetName() const override { return "Button Component Demo"; }
		std::string ExportState() override { return "{}"; } // No state to export

		void OnEnter() override {
			using namespace UI;
			using namespace Foundation;

			// Initialize font renderer for button text
			m_fontRenderer = std::make_unique<ui::FontRenderer>();
			if (!m_fontRenderer->Initialize()) {
				LOG_ERROR(UI, "Failed to initialize FontRenderer!");
				return;
			}

			// Set up projection matrix for text rendering
			int viewportWidth = 0;
			int viewportHeight = 0;
			Renderer::Primitives::GetViewport(viewportWidth, viewportHeight);
			glm::mat4 projection = glm::ortho(0.0F, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0.0F);
			m_fontRenderer->SetProjectionMatrix(projection);

			// Set font renderer in Primitives API so buttons can use it
			Renderer::Primitives::SetFontRenderer(m_fontRenderer.get());

			// Initialize text batch renderer for batched SDF text rendering
			m_textBatchRenderer = std::make_unique<ui::TextBatchRenderer>();
			m_textBatchRenderer->Initialize(m_fontRenderer.get());
			m_textBatchRenderer->SetProjectionMatrix(projection); // Set projection for MSDF shader
			Renderer::Primitives::SetTextBatchRenderer(m_textBatchRenderer.get());

			LOG_INFO(UI, "FontRenderer and TextBatchRenderer initialized for button scene");

			// Create root container
			Container rootContainer{.id = "root_container"};
			m_rootLayer = m_layerManager.Create(rootContainer);

			// NOTE: No fullscreen background - the window clear color handles this
			// A batched fullscreen rect would cover immediately-rendered text

			// Create title text (high z-index so it renders on top)
			Text titleText{
				.position = {50.0F, 30.0F},
				.text = "Button Component Demo - Click, Hover, Tab to Focus, Enter to Activate",
				.style = {.color = Color::White(), .fontSize = 20.0F},
				.zIndex = 100.0F, // Explicit high z-index
				.id = "title"
			};
			m_layerManager.AddChild(m_rootLayer, titleText);

			// Row 1: Primary Buttons
			Text primaryLabel{
				.position = {50.0F, 80.0F},
				.text = "Primary Buttons:",
				.style = {.color = Color::Yellow(), .fontSize = 16.0F},
				.zIndex = 100.0F, // Explicit high z-index
				.id = "primary_label"
			};
			m_layerManager.AddChild(m_rootLayer, primaryLabel);

			// Normal clickable button
			m_buttons.push_back(
				Button{Button::Args{
					.label = "Click Me!",
					.position = {50.0F, 110.0F},
					.size = {150.0F, 40.0F},
					.type = Button::Type::Primary,
					.onClick =
						[this]() {
							m_clickCount++;
							LOG_INFO(UI, "Button clicked! Count: {}", m_clickCount);
						},
					.id = "primary_button_1"
				}}
			);

			// Another clickable button
			m_buttons.push_back(
				Button{Button::Args{
					.label = "Another Button",
					.position = {220.0F, 110.0F},
					.size = {170.0F, 40.0F},
					.type = Button::Type::Primary,
					.onClick = []() { LOG_INFO(UI, "Second button clicked!"); },
					.id = "primary_button_2"
				}}
			);

			// Disabled button
			m_buttons.push_back(
				Button{Button::Args{
					.label = "Disabled",
					.position = {410.0F, 110.0F},
					.size = {150.0F, 40.0F},
					.type = Button::Type::Primary,
					.disabled = true,
					.onClick = []() { LOG_WARNING(UI, "This should never fire!"); },
					.id = "primary_button_disabled"
				}}
			);

			// Row 2: Secondary Buttons
			Text secondaryLabel{
				.position = {50.0F, 180.0F},
				.text = "Secondary Buttons:",
				.style = {.color = Color::Yellow(), .fontSize = 16.0F},
				.zIndex = 100.0F,
				.id = "secondary_label"
			};
			m_layerManager.AddChild(m_rootLayer, secondaryLabel);

			m_buttons.push_back(
				Button{Button::Args{
					.label = "Secondary",
					.position = {50.0F, 210.0F},
					.size = {150.0F, 40.0F},
					.type = Button::Type::Secondary,
					.onClick = []() { LOG_INFO(UI, "Secondary button clicked!"); },
					.id = "secondary_button_1"
				}}
			);

			m_buttons.push_back(
				Button{Button::Args{
					.label = "Another Secondary",
					.position = {220.0F, 210.0F},
					.size = {200.0F, 40.0F},
					.type = Button::Type::Secondary,
					.onClick = []() { LOG_INFO(UI, "Second secondary button clicked!"); },
					.id = "secondary_button_2"
				}}
			);

			// Row 3: Different sizes
			Text sizeLabel{
				.position = {50.0F, 280.0F},
				.text = "Different Sizes:",
				.style = {.color = Color::Yellow(), .fontSize = 16.0F},
				.zIndex = 100.0F,
				.id = "size_label"
			};
			m_layerManager.AddChild(m_rootLayer, sizeLabel);

			m_buttons.push_back(
				Button{Button::Args{
					.label = "Small",
					.position = {50.0F, 310.0F},
					.size = {100.0F, 30.0F},
					.type = Button::Type::Primary,
					.onClick = []() { LOG_INFO(UI, "Small button clicked!"); },
					.id = "small_button"
				}}
			);

			m_buttons.push_back(
				Button{Button::Args{
					.label = "Large Button",
					.position = {170.0F, 310.0F},
					.size = {250.0F, 50.0F},
					.type = Button::Type::Secondary,
					.onClick = []() { LOG_INFO(UI, "Large button clicked!"); },
					.id = "large_button"
				}}
			);

			// Row 4: Focus demonstration
			Text focusLabel{
				.position = {50.0F, 390.0F},
				.text = "Focus (Press Tab to cycle, Enter to activate):",
				.style = {.color = Color::Yellow(), .fontSize = 16.0F},
				.zIndex = 100.0F,
				.id = "focus_label"
			};
			m_layerManager.AddChild(m_rootLayer, focusLabel);

			m_buttons.push_back(
				Button{Button::Args{
					.label = "Focusable 1",
					.position = {50.0F, 420.0F},
					.size = {150.0F, 40.0F},
					.type = Button::Type::Primary,
					.onClick = []() { LOG_INFO(UI, "Focusable 1 activated!"); },
					.id = "focusable_1"
				}}
			);

			m_buttons.push_back(
				Button{Button::Args{
					.label = "Focusable 2",
					.position = {220.0F, 420.0F},
					.size = {150.0F, 40.0F},
					.type = Button::Type::Primary,
					.onClick = []() { LOG_INFO(UI, "Focusable 2 activated!"); },
					.id = "focusable_2"
				}}
			);

			m_buttons.push_back(
				Button{Button::Args{
					.label = "Focusable 3",
					.position = {390.0F, 420.0F},
					.size = {150.0F, 40.0F},
					.type = Button::Type::Primary,
					.onClick = []() { LOG_INFO(UI, "Focusable 3 activated!"); },
					.id = "focusable_3"
				}}
			);

			// Set first button as focused by default
			m_focusedButtonIndex = 7; // First focusable button (index 7)
			m_buttons[m_focusedButtonIndex].SetFocused(true);

			// Click counter display
			m_clickCounterText = Text{
				.position = {600.0F, 110.0F},
				.text = "Clicks: 0",
				.style = {.color = Color::Green(), .fontSize = 18.0F},
				.zIndex = 100.0F,
				.id = "click_counter"
			};
			m_clickCounterTextLayer = m_layerManager.AddChild(m_rootLayer, m_clickCounterText);

			LOG_INFO(UI, "Button scene initialized with {} buttons", m_buttons.size());
		}

		void OnExit() override {
			m_buttons.clear();
			m_textBatchRenderer.reset();
			m_fontRenderer.reset();
			LOG_INFO(UI, "Button scene exited");
		}

		void HandleInput(float /*deltaTime*/) override {
			// Handle Tab key for focus cycling
			auto& input = engine::InputManager::Get();
			if (input.IsKeyPressed(engine::Key::Tab)) {
				// Unfocus current button
				m_buttons[m_focusedButtonIndex].SetFocused(false);

				// Move to next focusable button (wrap around)
				// Skip disabled buttons
				size_t startIndex = m_focusedButtonIndex;
				do {
					m_focusedButtonIndex = (m_focusedButtonIndex + 1) % m_buttons.size();
					if (!m_buttons[m_focusedButtonIndex].IsDisabled()) {
						break;
					}
				} while (m_focusedButtonIndex != startIndex);

				// Focus new button
				m_buttons[m_focusedButtonIndex].SetFocused(true);
				LOG_INFO(UI, "Focus moved to button index {}", m_focusedButtonIndex);
			}

			// Update all buttons' input state
			for (auto& button : m_buttons) {
				button.HandleInput();
			}
		}

		void Update(float deltaTime) override {
			// Update all buttons
			for (auto& button : m_buttons) {
				button.Update(deltaTime);
			}

			// Update click counter text
			if (m_lastClickCount != m_clickCount) {
				// Get mutable reference to the Text data and update it safely
				auto* textDataPtr = std::get_if<UI::Text>(&m_layerManager.GetData(m_clickCounterTextLayer));
				if (textDataPtr) {
					textDataPtr->text = "Clicks: " + std::to_string(m_clickCount);
					m_lastClickCount = m_clickCount;
				} else {
					LOG_ERROR(UI, "Layer %u does not contain UI::Text for click counter", m_clickCounterTextLayer);
				}
			}
		}

		void Render() override {
			// CRITICAL: We need to flush batched primitives BEFORE text renders
			// Otherwise batched rectangles draw over immediately-rendered text

			// Flush any previous batched primitives first
			Renderer::Primitives::EndFrame();

			// Render layer hierarchy text labels (immediate rendering)
			m_layerManager.RenderAll();

			// Render all buttons (batches rectangles, then immediately renders text)
			for (const auto& button : m_buttons) {
				button.Render();
			}

			// Flush batched button rectangles so they appear
			Renderer::Primitives::EndFrame();
		}

	  private:
		// Font renderer
		std::unique_ptr<ui::FontRenderer>	   m_fontRenderer;
		std::unique_ptr<ui::TextBatchRenderer> m_textBatchRenderer;

		// Layer management
		UI::LayerManager m_layerManager;
		uint32_t		 m_rootLayer{0};
		uint32_t		 m_clickCounterTextLayer{0};

		// Buttons
		std::vector<UI::Button> m_buttons;

		// Focus management
		size_t m_focusedButtonIndex{0};

		// Click tracking
		int m_clickCount{0};
		int m_lastClickCount{0};

		// Click counter text (needs to be member for update)
		UI::Text m_clickCounterText;
	};

	// Register scene with scene manager
	static bool registered = []() {
		engine::SceneManager::Get().RegisterScene("button_scene", []() { return std::make_unique<ButtonScene>(); });
		return true;
	}();

} // anonymous namespace
