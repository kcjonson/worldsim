// Layout Scene - Demonstrates LayoutContainer for automatic component positioning
// Shows vertical/horizontal layouts with alignment and margin-based spacing

#include <GL/glew.h>

#include <components/button/Button.h>
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

constexpr const char* kSceneName = "layout";

class LayoutScene : public engine::IScene {
  public:
	const char* getName() const override { return kSceneName; }
	std::string exportState() override { return "{}"; }

	void onEnter() override {
		using namespace UI;
		using namespace Foundation;

		// Create title
		title = std::make_unique<Text>(Text::Args{
			.position = {50.0F, 30.0F},
			.text = "LayoutContainer Demo - Automatic component positioning",
			.style = {.color = Color::white(), .fontSize = 20.0F},
			.id = "title"});

		// ================================================================
		// Demo 1: Vertical Layout with buttons
		// ================================================================
		verticalLabel = std::make_unique<Text>(Text::Args{
			.position = {50.0F, 70.0F},
			.text = "Vertical Layout (buttons with margin):",
			.style = {.color = Color::yellow(), .fontSize = 16.0F},
			.id = "vertical_label"});

		verticalLayout = std::make_unique<LayoutContainer>(LayoutContainer::Args{
			.position = {50.0F, 100.0F},
			.size = {200.0F, 250.0F},
			.direction = Direction::Vertical,
			.hAlign = HAlign::Left,
			.id = "vertical_layout"});

		// Add buttons with margin for spacing
		verticalLayout->addChild(Button(Button::Args{
			.label = "Button One",
			.size = {180.0F, 40.0F},
			.type = Button::Type::Primary,
			.margin = 5.0F,
			.onClick = []() { LOG_INFO(UI, "Button One clicked!"); },
			.id = "btn_one"}));

		verticalLayout->addChild(Button(Button::Args{
			.label = "Button Two",
			.size = {180.0F, 40.0F},
			.type = Button::Type::Primary,
			.margin = 5.0F,
			.onClick = []() { LOG_INFO(UI, "Button Two clicked!"); },
			.id = "btn_two"}));

		verticalLayout->addChild(Button(Button::Args{
			.label = "Button Three",
			.size = {180.0F, 40.0F},
			.type = Button::Type::Secondary,
			.margin = 5.0F,
			.onClick = []() { LOG_INFO(UI, "Button Three clicked!"); },
			.id = "btn_three"}));

		// ================================================================
		// Demo 2: Horizontal Layout
		// ================================================================
		horizontalLabel = std::make_unique<Text>(Text::Args{
			.position = {300.0F, 70.0F},
			.text = "Horizontal Layout:",
			.style = {.color = Color::yellow(), .fontSize = 16.0F},
			.id = "horizontal_label"});

		horizontalLayout = std::make_unique<LayoutContainer>(LayoutContainer::Args{
			.position = {300.0F, 100.0F},
			.size = {450.0F, 60.0F},
			.direction = Direction::Horizontal,
			.vAlign = VAlign::Center,
			.id = "horizontal_layout"});

		horizontalLayout->addChild(Button(Button::Args{
			.label = "Left",
			.size = {100.0F, 40.0F},
			.type = Button::Type::Primary,
			.margin = 5.0F,
			.onClick = []() { LOG_INFO(UI, "Left clicked!"); },
			.id = "btn_left"}));

		horizontalLayout->addChild(Button(Button::Args{
			.label = "Center",
			.size = {100.0F, 40.0F},
			.type = Button::Type::Secondary,
			.margin = 5.0F,
			.onClick = []() { LOG_INFO(UI, "Center clicked!"); },
			.id = "btn_center"}));

		horizontalLayout->addChild(Button(Button::Args{
			.label = "Right",
			.size = {100.0F, 40.0F},
			.type = Button::Type::Primary,
			.margin = 5.0F,
			.onClick = []() { LOG_INFO(UI, "Right clicked!"); },
			.id = "btn_right"}));

		// ================================================================
		// Demo 3: Centered Alignment
		// ================================================================
		centeredLabel = std::make_unique<Text>(Text::Args{
			.position = {300.0F, 180.0F},
			.text = "Center-aligned Vertical Layout:",
			.style = {.color = Color::yellow(), .fontSize = 16.0F},
			.id = "centered_label"});

		centeredLayout = std::make_unique<LayoutContainer>(LayoutContainer::Args{
			.position = {300.0F, 210.0F},
			.size = {200.0F, 150.0F},
			.direction = Direction::Vertical,
			.hAlign = HAlign::Center,
			.id = "centered_layout"});

		centeredLayout->addChild(Button(Button::Args{
			.label = "Wide Button",
			.size = {180.0F, 35.0F},
			.type = Button::Type::Primary,
			.margin = 3.0F,
			.onClick = []() { LOG_INFO(UI, "Wide clicked!"); },
			.id = "btn_wide"}));

		centeredLayout->addChild(Button(Button::Args{
			.label = "Short",
			.size = {100.0F, 35.0F},
			.type = Button::Type::Secondary,
			.margin = 3.0F,
			.onClick = []() { LOG_INFO(UI, "Short clicked!"); },
			.id = "btn_short"}));

		centeredLayout->addChild(Button(Button::Args{
			.label = "Medium Btn",
			.size = {140.0F, 35.0F},
			.type = Button::Type::Primary,
			.margin = 3.0F,
			.onClick = []() { LOG_INFO(UI, "Medium clicked!"); },
			.id = "btn_medium"}));

		// ================================================================
		// Demo 4: Layout with Shapes
		// ================================================================
		shapesLabel = std::make_unique<Text>(Text::Args{
			.position = {550.0F, 180.0F},
			.text = "Layout with Shapes:",
			.style = {.color = Color::yellow(), .fontSize = 16.0F},
			.id = "shapes_label"});

		shapesLayout = std::make_unique<LayoutContainer>(LayoutContainer::Args{
			.position = {550.0F, 210.0F},
			.size = {200.0F, 200.0F},
			.direction = Direction::Vertical,
			.hAlign = HAlign::Center,
			.id = "shapes_layout"});

		shapesLayout->addChild(Rectangle(Rectangle::Args{
			.size = {150.0F, 40.0F},
			.style = {.fill = Color(0.204F, 0.596F, 0.859F)},  // Blue
			.margin = 5.0F,
			.id = "rect_blue"}));

		shapesLayout->addChild(Rectangle(Rectangle::Args{
			.size = {100.0F, 40.0F},
			.style = {.fill = Color(0.906F, 0.298F, 0.235F)},  // Red
			.margin = 5.0F,
			.id = "rect_red"}));

		shapesLayout->addChild(Rectangle(Rectangle::Args{
			.size = {180.0F, 40.0F},
			.style = {.fill = Color(0.180F, 0.800F, 0.443F)},  // Green
			.margin = 5.0F,
			.id = "rect_green"}));

		LOG_INFO(UI, "Layout scene initialized");
	}

	void onExit() override {
		title.reset();
		verticalLabel.reset();
		horizontalLabel.reset();
		centeredLabel.reset();
		shapesLabel.reset();
		verticalLayout.reset();
		horizontalLayout.reset();
		centeredLayout.reset();
		shapesLayout.reset();
		LOG_INFO(UI, "Layout scene exited");
	}

	bool handleInput(UI::InputEvent& event) override {
		// Dispatch to layouts (they forward to children)
		if (verticalLayout && verticalLayout->handleEvent(event)) {
			return true;
		}
		if (horizontalLayout && horizontalLayout->handleEvent(event)) {
			return true;
		}
		if (centeredLayout && centeredLayout->handleEvent(event)) {
			return true;
		}
		if (shapesLayout && shapesLayout->handleEvent(event)) {
			return true;
		}
		return false;
	}

	void update(float deltaTime) override {
		if (verticalLayout) {
			verticalLayout->update(deltaTime);
		}
		if (horizontalLayout) {
			horizontalLayout->update(deltaTime);
		}
		if (centeredLayout) {
			centeredLayout->update(deltaTime);
		}
		if (shapesLayout) {
			shapesLayout->update(deltaTime);
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
		if (verticalLabel) {
			verticalLabel->render();
		}
		if (horizontalLabel) {
			horizontalLabel->render();
		}
		if (centeredLabel) {
			centeredLabel->render();
		}
		if (shapesLabel) {
			shapesLabel->render();
		}

		// Render layouts (they render their children)
		if (verticalLayout) {
			verticalLayout->render();
		}
		if (horizontalLayout) {
			horizontalLayout->render();
		}
		if (centeredLayout) {
			centeredLayout->render();
		}
		if (shapesLayout) {
			shapesLayout->render();
		}
	}

  private:
	// Labels
	std::unique_ptr<UI::Text> title;
	std::unique_ptr<UI::Text> verticalLabel;
	std::unique_ptr<UI::Text> horizontalLabel;
	std::unique_ptr<UI::Text> centeredLabel;
	std::unique_ptr<UI::Text> shapesLabel;

	// Layout containers
	std::unique_ptr<UI::LayoutContainer> verticalLayout;
	std::unique_ptr<UI::LayoutContainer> horizontalLayout;
	std::unique_ptr<UI::LayoutContainer> centeredLayout;
	std::unique_ptr<UI::LayoutContainer> shapesLayout;
};

} // anonymous namespace

// Export scene info for registry
namespace ui_sandbox::scenes {
extern const ui_sandbox::SceneInfo Layout = {kSceneName, []() { return std::make_unique<LayoutScene>(); }};
}
