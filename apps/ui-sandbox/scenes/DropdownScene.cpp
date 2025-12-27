// Dropdown Scene - Demonstrates the DropdownButton and Select components
// Shows dropdown menus, controlled select elements, and keyboard navigation

#include <GL/glew.h>

#include "SceneTypes.h"
#include <components/dropdown/DropdownButton.h>
#include <components/select/Select.h>
#include <focus/FocusManager.h>
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

	constexpr const char* kSceneName = "dropdown";

	class DropdownScene : public engine::IScene {
	  public:
		const char* getName() const override { return kSceneName; }
		std::string exportState() override { return "{}"; }

		void onEnter() override {
			using namespace UI;
			using namespace Foundation;

			// Initialize FocusManager for this scene
			FocusManager::setInstance(&focusManager);

			// Create title
			title = std::make_unique<Text>(Text::Args{
				.position = {50.0F, 30.0F},
				.text = "DropdownButton & Select Component Demo",
				.style = {.color = Color::white(), .fontSize = 20.0F},
				.id = "title"
			});

			// ================================================================
			// Demo 1: Basic Dropdown
			// ================================================================
			label1 = std::make_unique<Text>(Text::Args{
				.position = {50.0F, 70.0F},
				.text = "1. Actions Menu:",
				.style = {.color = Color::yellow(), .fontSize = 14.0F},
				.id = "label_1"
			});

			dropdown1 = std::make_unique<DropdownButton>(DropdownButton::Args{
				.label = "Actions",
				.position = {50.0F, 95.0F},
				.buttonSize = {120.0F, 36.0F},
				.items =
					{
						DropdownItem{.label = "Move", .onSelect = []() { LOG_INFO(UI, "Move selected"); }},
						DropdownItem{.label = "Attack", .onSelect = []() { LOG_INFO(UI, "Attack selected"); }},
						DropdownItem{.label = "Build", .onSelect = []() { LOG_INFO(UI, "Build selected"); }},
						DropdownItem{.label = "Cancel", .onSelect = []() { LOG_INFO(UI, "Cancel selected"); }, .enabled = false},
					},
				.id = "dropdown_actions"
			});

			// ================================================================
			// Demo 2: Build Menu
			// ================================================================
			label2 = std::make_unique<Text>(Text::Args{
				.position = {200.0F, 70.0F},
				.text = "2. Build Menu:",
				.style = {.color = Color::yellow(), .fontSize = 14.0F},
				.id = "label_2"
			});

			dropdown2 = std::make_unique<DropdownButton>(DropdownButton::Args{
				.label = "Build",
				.position = {200.0F, 95.0F},
				.buttonSize = {130.0F, 36.0F},
				.items =
					{
						DropdownItem{.label = "Wall", .onSelect = []() { LOG_INFO(UI, "Wall selected"); }},
						DropdownItem{.label = "Floor", .onSelect = []() { LOG_INFO(UI, "Floor selected"); }},
						DropdownItem{.label = "Door", .onSelect = []() { LOG_INFO(UI, "Door selected"); }},
						DropdownItem{.label = "Furniture", .onSelect = []() { LOG_INFO(UI, "Furniture selected"); }},
						DropdownItem{.label = "Production", .onSelect = []() { LOG_INFO(UI, "Production selected"); }},
					},
				.id = "dropdown_build"
			});

			// ================================================================
			// Demo 3: Dropdowns in Layout
			// ================================================================
			label3 = std::make_unique<Text>(Text::Args{
				.position = {50.0F, 200.0F},
				.text = "3. Dropdowns in Horizontal Layout:",
				.style = {.color = Color::yellow(), .fontSize = 14.0F},
				.id = "label_3"
			});

			layoutContainer = std::make_unique<LayoutContainer>(LayoutContainer::Args{
				.position = {50.0F, 225.0F},
				.size = {0.0F, 0.0F}, // Auto-size
				.direction = Direction::Horizontal,
				.vAlign = VAlign::Top,
				.id = "dropdown_layout"
			});

			layoutContainer->addChild(DropdownButton(
				DropdownButton::Args{
					.label = "File",
					.buttonSize = {80.0F, 32.0F},
					.items =
						{
							DropdownItem{.label = "New", .onSelect = []() { LOG_INFO(UI, "New selected"); }},
							DropdownItem{.label = "Open", .onSelect = []() { LOG_INFO(UI, "Open selected"); }},
							DropdownItem{.label = "Save", .onSelect = []() { LOG_INFO(UI, "Save selected"); }},
						},
					.margin = 4.0F,
				}
			));

			layoutContainer->addChild(DropdownButton(
				DropdownButton::Args{
					.label = "Edit",
					.buttonSize = {80.0F, 32.0F},
					.items =
						{
							DropdownItem{.label = "Undo", .onSelect = []() { LOG_INFO(UI, "Undo selected"); }},
							DropdownItem{.label = "Redo", .onSelect = []() { LOG_INFO(UI, "Redo selected"); }},
							DropdownItem{.label = "Cut", .onSelect = []() { LOG_INFO(UI, "Cut selected"); }},
							DropdownItem{.label = "Copy", .onSelect = []() { LOG_INFO(UI, "Copy selected"); }},
							DropdownItem{.label = "Paste", .onSelect = []() { LOG_INFO(UI, "Paste selected"); }},
						},
					.margin = 4.0F,
				}
			));

			layoutContainer->addChild(DropdownButton(
				DropdownButton::Args{
					.label = "View",
					.buttonSize = {80.0F, 32.0F},
					.items =
						{
							DropdownItem{.label = "Zoom In", .onSelect = []() { LOG_INFO(UI, "Zoom In selected"); }},
							DropdownItem{.label = "Zoom Out", .onSelect = []() { LOG_INFO(UI, "Zoom Out selected"); }},
							DropdownItem{.label = "Reset", .onSelect = []() { LOG_INFO(UI, "Reset selected"); }},
						},
					.margin = 4.0F,
				}
			));

			// Force layout calculation
			layoutContainer->layout(Rect{50.0F, 225.0F, 400.0F, 100.0F});

			// ================================================================
			// Demo 4: Controlled Select Components
			// ================================================================
			label4 = std::make_unique<Text>(Text::Args{
				.position = {50.0F, 320.0F},
				.text = "4. Controlled Select (form element):",
				.style = {.color = Color::yellow(), .fontSize = 14.0F},
				.id = "label_4"
			});

			// Color picker select
			colorSelect = std::make_unique<Select>(Select::Args{
				.position = {50.0F, 345.0F},
				.size = {140.0F, 36.0F},
				.options =
					{
						SelectOption{.label = "Red", .value = "red"},
						SelectOption{.label = "Green", .value = "green"},
						SelectOption{.label = "Blue", .value = "blue"},
						SelectOption{.label = "Yellow", .value = "yellow"},
					},
				.value = "blue",
				.placeholder = "Choose color...",
				.onChange =
					[this](const std::string& value) {
						LOG_INFO(UI, "Color selected: {}", value.c_str());
						selectedColor = value;
					},
				.id = "select_color"
			});

			// Size select
			sizeSelect = std::make_unique<Select>(Select::Args{
				.position = {210.0F, 345.0F},
				.size = {120.0F, 36.0F},
				.options =
					{
						SelectOption{.label = "Small", .value = "sm"},
						SelectOption{.label = "Medium", .value = "md"},
						SelectOption{.label = "Large", .value = "lg"},
						SelectOption{.label = "X-Large", .value = "xl"},
					},
				.placeholder = "Size...",
				.onChange = [](const std::string& value) { LOG_INFO(UI, "Size selected: {}", value.c_str()); },
				.id = "select_size"
			});

			// Selection display
			selectionDisplay = std::make_unique<Text>(Text::Args{
				.position = {350.0F, 355.0F},
				.text = "Selected: blue",
				.style = {.color = Color(0.7F, 0.8F, 1.0F, 1.0F), .fontSize = 12.0F},
				.id = "selection_display"
			});

			// ================================================================
			// Instructions
			// ================================================================
			instructions = std::make_unique<Text>(Text::Args{
				.position = {50.0F, 420.0F},
				.text = "Click to open | Arrow keys to navigate | Enter to select | Escape to close",
				.style = {.color = Color(0.6F, 0.6F, 0.7F, 1.0F), .fontSize = 12.0F},
				.id = "instructions"
			});

			LOG_INFO(UI, "Dropdown scene initialized");
		}

		void onExit() override {
			title.reset();
			label1.reset();
			label2.reset();
			label3.reset();
			label4.reset();
			instructions.reset();
			selectionDisplay.reset();
			dropdown1.reset();
			dropdown2.reset();
			layoutContainer.reset();
			colorSelect.reset();
			sizeSelect.reset();
			UI::FocusManager::setInstance(nullptr);
			LOG_INFO(UI, "Dropdown scene exited");
		}

		bool handleInput(UI::InputEvent& event) override {
			// Dispatch to dropdowns
			if (dropdown1 && dropdown1->handleEvent(event)) {
				return true;
			}
			if (dropdown2 && dropdown2->handleEvent(event)) {
				return true;
			}
			if (layoutContainer && layoutContainer->dispatchEvent(event)) {
				return true;
			}
			// Dispatch to selects
			if (colorSelect && colorSelect->handleEvent(event)) {
				return true;
			}
			if (sizeSelect && sizeSelect->handleEvent(event)) {
				return true;
			}
			return false;
		}

		void update(float deltaTime) override {
			if (dropdown1) {
				dropdown1->update(deltaTime);
			}
			if (dropdown2) {
				dropdown2->update(deltaTime);
			}
			if (layoutContainer) {
				layoutContainer->update(deltaTime);
			}
			if (colorSelect) {
				colorSelect->update(deltaTime);
				// Update display text when color changes
				if (selectionDisplay && !selectedColor.empty()) {
					selectionDisplay->text = "Selected: " + selectedColor;
				}
			}
			if (sizeSelect) {
				sizeSelect->update(deltaTime);
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
			if (selectionDisplay) {
				selectionDisplay->render();
			}

			// Render dropdowns
			if (dropdown1) {
				dropdown1->render();
			}
			if (dropdown2) {
				dropdown2->render();
			}
			if (layoutContainer) {
				layoutContainer->render();
			}

			// Render selects
			if (colorSelect) {
				colorSelect->render();
			}
			if (sizeSelect) {
				sizeSelect->render();
			}
		}

	  private:
		// Focus manager for this scene
		UI::FocusManager focusManager;

		// Labels
		std::unique_ptr<UI::Text> title;
		std::unique_ptr<UI::Text> label1;
		std::unique_ptr<UI::Text> label2;
		std::unique_ptr<UI::Text> label3;
		std::unique_ptr<UI::Text> label4;
		std::unique_ptr<UI::Text> instructions;
		std::unique_ptr<UI::Text> selectionDisplay;

		// Dropdowns
		std::unique_ptr<UI::DropdownButton> dropdown1;
		std::unique_ptr<UI::DropdownButton> dropdown2;

		// Layout container
		std::unique_ptr<UI::LayoutContainer> layoutContainer;

		// Selects
		std::unique_ptr<UI::Select> colorSelect;
		std::unique_ptr<UI::Select> sizeSelect;

		// State for demo
		std::string selectedColor = "blue";
	};

} // anonymous namespace

// Export scene info for registry
namespace ui_sandbox::scenes {
	extern const ui_sandbox::SceneInfo Dropdown = {kSceneName, []() { return std::make_unique<DropdownScene>(); }};
} // namespace ui_sandbox::scenes
