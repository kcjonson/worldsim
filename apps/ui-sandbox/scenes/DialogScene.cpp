// Dialog Scene - Demonstrates the Dialog component
// Shows modal dialogs with different configurations

#include <GL/glew.h>

#include "SceneTypes.h"
#include <components/button/Button.h>
#include <components/dialog/Dialog.h>
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

constexpr const char* kSceneName = "dialog";

class DialogScene : public engine::IScene {
  public:
	const char* getName() const override { return kSceneName; }
	std::string exportState() override { return "{}"; }

	void onEnter() override {
		using namespace UI;
		using namespace Foundation;

		// Create title
		title = std::make_unique<Text>(Text::Args{
			.position = {50.0F, 30.0F},
			.text = "Dialog Component Demo",
			.style = {.color = Color::white(), .fontSize = 20.0F},
			.id = "title"});

		// ================================================================
		// Demo 1: Basic Dialog
		// ================================================================
		label1 = std::make_unique<Text>(Text::Args{
			.position = {50.0F, 70.0F},
			.text = "1. Basic Dialog (default size):",
			.style = {.color = Color::yellow(), .fontSize = 14.0F},
			.id = "label_1"});

		basicDialogButton = std::make_unique<Button>(Button::Args{
			.label = "Open Basic Dialog",
			.position = {50.0F, 95.0F},
			.size = {160.0F, 36.0F},
			.onClick =
				[this]() {
					if (!basicDialog->isOpen()) {
						basicDialog->open(800.0F, 600.0F);
					}
				},
		});

		basicDialog = std::make_unique<Dialog>(Dialog::Args{
			.title = "Basic Dialog",
			.onClose = [this]() { LOG_INFO(UI, "Basic dialog closed"); },
		});

		// ================================================================
		// Demo 2: Small Dialog
		// ================================================================
		label2 = std::make_unique<Text>(Text::Args{
			.position = {50.0F, 150.0F},
			.text = "2. Small Dialog (300x200):",
			.style = {.color = Color::yellow(), .fontSize = 14.0F},
			.id = "label_2"});

		smallDialogButton = std::make_unique<Button>(Button::Args{
			.label = "Open Small Dialog",
			.position = {50.0F, 175.0F},
			.size = {160.0F, 36.0F},
			.onClick =
				[this]() {
					if (!smallDialog->isOpen()) {
						smallDialog->open(800.0F, 600.0F);
					}
				},
		});

		smallDialog = std::make_unique<Dialog>(Dialog::Args{
			.title = "Small Dialog",
			.size = {300.0F, 200.0F},
			.onClose = [this]() { LOG_INFO(UI, "Small dialog closed"); },
		});

		// ================================================================
		// Demo 3: Large Dialog
		// ================================================================
		label3 = std::make_unique<Text>(Text::Args{
			.position = {50.0F, 230.0F},
			.text = "3. Large Dialog (700x500):",
			.style = {.color = Color::yellow(), .fontSize = 14.0F},
			.id = "label_3"});

		largeDialogButton = std::make_unique<Button>(Button::Args{
			.label = "Open Large Dialog",
			.position = {50.0F, 255.0F},
			.size = {160.0F, 36.0F},
			.onClick =
				[this]() {
					if (!largeDialog->isOpen()) {
						largeDialog->open(800.0F, 600.0F);
					}
				},
		});

		largeDialog = std::make_unique<Dialog>(Dialog::Args{
			.title = "Large Dialog with Long Title",
			.size = {700.0F, 500.0F},
			.onClose = [this]() { LOG_INFO(UI, "Large dialog closed"); },
		});

		// ================================================================
		// Instructions
		// ================================================================
		instructions = std::make_unique<Text>(Text::Args{
			.position = {50.0F, 350.0F},
			.text = "Close dialogs via: [X] button | Escape key | Click outside",
			.style = {.color = Color(0.6F, 0.6F, 0.7F, 1.0F), .fontSize = 12.0F},
			.id = "instructions"});

		LOG_INFO(UI, "Dialog scene initialized");
	}

	void onExit() override {
		title.reset();
		label1.reset();
		label2.reset();
		label3.reset();
		instructions.reset();
		basicDialogButton.reset();
		smallDialogButton.reset();
		largeDialogButton.reset();
		basicDialog.reset();
		smallDialog.reset();
		largeDialog.reset();
		LOG_INFO(UI, "Dialog scene exited");
	}

	bool handleInput(UI::InputEvent& event) override {
		// Dialogs take priority when open
		if (basicDialog && basicDialog->isOpen()) {
			return basicDialog->handleEvent(event);
		}
		if (smallDialog && smallDialog->isOpen()) {
			return smallDialog->handleEvent(event);
		}
		if (largeDialog && largeDialog->isOpen()) {
			return largeDialog->handleEvent(event);
		}

		// Then buttons
		if (basicDialogButton && basicDialogButton->handleEvent(event)) {
			return true;
		}
		if (smallDialogButton && smallDialogButton->handleEvent(event)) {
			return true;
		}
		if (largeDialogButton && largeDialogButton->handleEvent(event)) {
			return true;
		}

		return false;
	}

	void update(float deltaTime) override {
		if (basicDialogButton) {
			basicDialogButton->update(deltaTime);
		}
		if (smallDialogButton) {
			smallDialogButton->update(deltaTime);
		}
		if (largeDialogButton) {
			largeDialogButton->update(deltaTime);
		}
		if (basicDialog) {
			basicDialog->update(deltaTime);
		}
		if (smallDialog) {
			smallDialog->update(deltaTime);
		}
		if (largeDialog) {
			largeDialog->update(deltaTime);
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
		if (basicDialogButton) {
			basicDialogButton->render();
		}
		if (smallDialogButton) {
			smallDialogButton->render();
		}
		if (largeDialogButton) {
			largeDialogButton->render();
		}

		// Render dialogs (on top)
		if (basicDialog) {
			basicDialog->render();
		}
		if (smallDialog) {
			smallDialog->render();
		}
		if (largeDialog) {
			largeDialog->render();
		}
	}

  private:
	// Labels
	std::unique_ptr<UI::Text> title;
	std::unique_ptr<UI::Text> label1;
	std::unique_ptr<UI::Text> label2;
	std::unique_ptr<UI::Text> label3;
	std::unique_ptr<UI::Text> instructions;

	// Buttons
	std::unique_ptr<UI::Button> basicDialogButton;
	std::unique_ptr<UI::Button> smallDialogButton;
	std::unique_ptr<UI::Button> largeDialogButton;

	// Dialogs
	std::unique_ptr<UI::Dialog> basicDialog;
	std::unique_ptr<UI::Dialog> smallDialog;
	std::unique_ptr<UI::Dialog> largeDialog;
};

} // anonymous namespace

// Export scene info for registry
namespace ui_sandbox::scenes {
extern const ui_sandbox::SceneInfo Dialog_ = {kSceneName, []() { return std::make_unique<DialogScene>(); }};
} // namespace ui_sandbox::scenes
