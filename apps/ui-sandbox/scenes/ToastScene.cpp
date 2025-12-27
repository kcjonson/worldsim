// Toast Scene - Demonstrates the Toast and ToastStack components
// Shows toast notifications with different severities and behaviors

#include <GL/glew.h>

#include "SceneTypes.h"
#include <components/button/Button.h>
#include <components/toast/Toast.h>
#include <components/toast/ToastStack.h>
#include <graphics/Color.h>
#include <input/InputEvent.h>
#include <layout/LayoutContainer.h>
#include <layout/LayoutTypes.h>
#include <memory>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <shapes/Shapes.h>
#include <theme/Theme.h>
#include <utils/Log.h>

namespace {

	constexpr const char* kSceneName = "toast";

	class ToastScene : public engine::IScene {
	  public:
		const char* getName() const override { return kSceneName; }
		std::string exportState() override { return "{}"; }

		void onEnter() override {
			using namespace UI;
			using namespace Foundation;

			// Create title
			title = std::make_unique<Text>(Text::Args{
				.position = {50.0F, 30.0F},
				.text = "Toast Notification Demo",
				.style = {.color = Color::white(), .fontSize = 20.0F},
				.id = "title"});

			// Create toast stack (bottom-right corner)
			toastStack = std::make_unique<ToastStack>(ToastStack::Args{
				.position = {780.0F, 580.0F}, // Bottom-right area
				.anchor = ToastAnchor::BottomRight,
				.spacing = 8.0F,
				.maxToasts = 5,
			});

			// ================================================================
			// Demo 1: Severity Buttons
			// ================================================================
			label1 = std::make_unique<Text>(Text::Args{
				.position = {50.0F, 70.0F},
				.text = "1. Trigger by Severity:",
				.style = {.color = Color::yellow(), .fontSize = 14.0F},
				.id = "label_1"});

			buttonLayout = std::make_unique<LayoutContainer>(LayoutContainer::Args{
				.position = {50.0F, 95.0F},
				.size = {0.0F, 0.0F}, // Auto-size
				.direction = Direction::Horizontal,
				.vAlign = VAlign::Top,
				.id = "button_layout"});

			// Info button
			buttonLayout->addChild(Button(Button::Args{
				.label = "Info",
				.position = {0.0F, 0.0F},
				.size = {100.0F, 36.0F},
				.onClick =
					[this]() {
						infoCount++;
						toastStack->addToast(
							"Information",
							"This is info message #" + std::to_string(infoCount),
							ToastSeverity::Info, 5.0F);
					},
				.margin = 4.0F,
			}));

			// Warning button
			buttonLayout->addChild(Button(Button::Args{
				.label = "Warning",
				.position = {0.0F, 0.0F},
				.size = {100.0F, 36.0F},
				.onClick =
					[this]() {
						warningCount++;
						toastStack->addToast(
							"Warning",
							"Something needs attention #" + std::to_string(warningCount),
							ToastSeverity::Warning, 7.0F);
					},
				.margin = 4.0F,
			}));

			// Critical button
			buttonLayout->addChild(Button(Button::Args{
				.label = "Critical",
				.position = {0.0F, 0.0F},
				.size = {100.0F, 36.0F},
				.onClick =
					[this]() {
						criticalCount++;
						toastStack->addToast(
							"Critical Alert",
							"Immediate action required #" + std::to_string(criticalCount),
							ToastSeverity::Critical, 0.0F); // Persistent
					},
				.margin = 4.0F,
			}));

			buttonLayout->layout(Rect{50.0F, 95.0F, 400.0F, 50.0F});

			// ================================================================
			// Demo 2: Special Actions
			// ================================================================
			label2 = std::make_unique<Text>(Text::Args{
				.position = {50.0F, 160.0F},
				.text = "2. Special Actions:",
				.style = {.color = Color::yellow(), .fontSize = 14.0F},
				.id = "label_2"});

			actionLayout = std::make_unique<LayoutContainer>(LayoutContainer::Args{
				.position = {50.0F, 185.0F},
				.size = {0.0F, 0.0F},
				.direction = Direction::Horizontal,
				.vAlign = VAlign::Top,
				.id = "action_layout"});

			// Dismiss All button
			actionLayout->addChild(Button(Button::Args{
				.label = "Dismiss All",
				.position = {0.0F, 0.0F},
				.size = {120.0F, 36.0F},
				.onClick = [this]() { toastStack->dismissAll(); },
				.margin = 4.0F,
			}));

			// Spam 5 button (to test max toasts)
			actionLayout->addChild(Button(Button::Args{
				.label = "Spam 5",
				.position = {0.0F, 0.0F},
				.size = {100.0F, 36.0F},
				.onClick =
					[this]() {
						for (int i = 0; i < 5; i++) {
							spamCount++;
							ToastSeverity sev = static_cast<ToastSeverity>(spamCount % 3);
							toastStack->addToast("Spam #" + std::to_string(spamCount),
												 "Rapid fire toast", sev, 3.0F);
						}
					},
				.margin = 4.0F,
			}));

			actionLayout->layout(Rect{50.0F, 185.0F, 300.0F, 50.0F});

			// ================================================================
			// Demo 3: Standalone Toast (manual positioning)
			// ================================================================
			label3 = std::make_unique<Text>(Text::Args{
				.position = {50.0F, 250.0F},
				.text = "3. Standalone Toast (center):",
				.style = {.color = Color::yellow(), .fontSize = 14.0F},
				.id = "label_3"});

			standaloneButton = std::make_unique<Button>(Button::Args{
				.label = "Show Standalone",
				.position = {50.0F, 275.0F},
				.size = {140.0F, 36.0F},
				.onClick =
					[this]() {
						if (!standaloneToast || standaloneToast->isFinished()) {
							standaloneToast = std::make_unique<Toast>(Toast::Args{
								.title = "Standalone Toast",
								.message = "This toast is not in a stack",
								.severity = ToastSeverity::Info,
								.autoDismissTime = 4.0F,
								.position = {250.0F, 350.0F}, // Center-ish
								.width = 280.0F,
							});
						}
					},
			});

			// ================================================================
			// Instructions
			// ================================================================
			instructions = std::make_unique<Text>(Text::Args{
				.position = {50.0F, 400.0F},
				.text = "Click buttons to show toasts | Click X to dismiss | Critical toasts "
						"are persistent",
				.style = {.color = Color(0.6F, 0.6F, 0.7F, 1.0F), .fontSize = 12.0F},
				.id = "instructions"});

			LOG_INFO(UI, "Toast scene initialized");
		}

		void onExit() override {
			title.reset();
			label1.reset();
			label2.reset();
			label3.reset();
			instructions.reset();
			buttonLayout.reset();
			actionLayout.reset();
			standaloneButton.reset();
			toastStack.reset();
			standaloneToast.reset();
			LOG_INFO(UI, "Toast scene exited");
		}

		bool handleInput(UI::InputEvent& event) override {
			// Dispatch to buttons
			if (buttonLayout && buttonLayout->dispatchEvent(event)) {
				return true;
			}
			if (actionLayout && actionLayout->dispatchEvent(event)) {
				return true;
			}
			if (standaloneButton && standaloneButton->handleEvent(event)) {
				return true;
			}

			// Dispatch to toasts
			if (toastStack && toastStack->handleEvent(event)) {
				return true;
			}
			if (standaloneToast && standaloneToast->handleEvent(event)) {
				return true;
			}

			return false;
		}

		void update(float deltaTime) override {
			if (buttonLayout) {
				buttonLayout->update(deltaTime);
			}
			if (actionLayout) {
				actionLayout->update(deltaTime);
			}
			if (standaloneButton) {
				standaloneButton->update(deltaTime);
			}
			if (toastStack) {
				toastStack->update(deltaTime);
			}
			if (standaloneToast) {
				standaloneToast->update(deltaTime);
				// Remove finished standalone toast
				if (standaloneToast->isFinished()) {
					standaloneToast.reset();
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
			if (instructions) {
				instructions->render();
			}

			// Render buttons
			if (buttonLayout) {
				buttonLayout->render();
			}
			if (actionLayout) {
				actionLayout->render();
			}
			if (standaloneButton) {
				standaloneButton->render();
			}

			// Render toasts
			if (standaloneToast) {
				standaloneToast->render();
			}
			if (toastStack) {
				toastStack->render();
			}
		}

	  private:
		// Labels
		std::unique_ptr<UI::Text> title;
		std::unique_ptr<UI::Text> label1;
		std::unique_ptr<UI::Text> label2;
		std::unique_ptr<UI::Text> label3;
		std::unique_ptr<UI::Text> instructions;

		// Button layouts
		std::unique_ptr<UI::LayoutContainer> buttonLayout;
		std::unique_ptr<UI::LayoutContainer> actionLayout;
		std::unique_ptr<UI::Button>			 standaloneButton;

		// Toast components
		std::unique_ptr<UI::ToastStack> toastStack;
		std::unique_ptr<UI::Toast>		standaloneToast;

		// Counters for demo
		int infoCount = 0;
		int warningCount = 0;
		int criticalCount = 0;
		int spamCount = 0;
	};

} // anonymous namespace

// Export scene info for registry
namespace ui_sandbox::scenes {
	extern const ui_sandbox::SceneInfo Toast_ = {kSceneName, []() { return std::make_unique<ToastScene>(); }};
} // namespace ui_sandbox::scenes
