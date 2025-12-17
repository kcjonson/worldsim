// TabBar Scene - UI TabBar Component Showcase
// Demonstrates TabBar component with all states: Normal, Hover, Active (selected), Disabled, Focused

#include <GL/glew.h>

#include <components/tabbar/TabBar.h>
#include <graphics/Color.h>
#include <input/InputManager.h>
#include <input/InputTypes.h>
#include <memory>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include "SceneTypes.h"
#include <shapes/Shapes.h>
#include <utils/Log.h>
#include <vector>

namespace {

constexpr const char* kSceneName = "tabbar";

class TabBarScene : public engine::IScene {
  public:
	const char* getName() const override { return kSceneName; }
	std::string exportState() override { return "{}"; }

	void onEnter() override {
		using namespace UI;
		using namespace Foundation;

		// Create title text
		labels.push_back(std::make_unique<Text>(Text::Args{
			.position = {50.0F, 30.0F},
			.text = "TabBar Component Demo - Click tabs, Tab for focus, Arrow keys to navigate",
			.style = {.color = Color::white(), .fontSize = 20.0F},
			.id = "title"}));

		// Demo 1: Basic Tab Bar
		labels.push_back(std::make_unique<Text>(Text::Args{
			.position = {50.0F, 80.0F},
			.text = "Basic TabBar (Status/Inventory/Equipment):",
			.style = {.color = Color::yellow(), .fontSize = 16.0F},
			.id = "demo1_label"}));

		tabBar1 = std::make_unique<TabBar>(TabBar::Args{
			.position = {50.0F, 110.0F},
			.width = 400.0F,
			.tabs =
				{
					{.id = "status", .label = "Status"},
					{.id = "inventory", .label = "Inventory"},
					{.id = "equipment", .label = "Equipment", .disabled = true},
				},
			.selectedId = "status",
			.onSelect =
				[this](const std::string& tabId) {
					currentTab1 = tabId;
					LOG_INFO(UI, "TabBar 1 selected: {}", tabId.c_str());
				},
			.id = "tabbar_1"});

		// Content area for demo 1
		contentBg1 = std::make_unique<Rectangle>(Rectangle::Args{
			.position = {50.0F, 150.0F},
			.size = {400.0F, 150.0F},
			.style =
				{
					.fill = Color{0.15F, 0.15F, 0.2F, 0.9F},
					.border = BorderStyle{
						.color = Color{0.25F, 0.25F, 0.3F, 1.0F},
						.width = 1.0F,
						.cornerRadius = 4.0F,
						.position = BorderPosition::Inside,
					},
				},
			.id = "content_bg_1"});

		contentText1 = std::make_unique<Text>(Text::Args{
			.position = {250.0F, 225.0F},
			.text = "Content: Status",
			.style =
				{
					.color = Color::white(),
					.fontSize = 18.0F,
					.hAlign = HorizontalAlign::Center,
					.vAlign = VerticalAlign::Middle,
				},
			.id = "content_text_1"});

		// Demo 2: More tabs
		labels.push_back(std::make_unique<Text>(Text::Args{
			.position = {50.0F, 330.0F},
			.text = "TabBar with many tabs:",
			.style = {.color = Color::yellow(), .fontSize = 16.0F},
			.id = "demo2_label"}));

		tabBar2 = std::make_unique<TabBar>(TabBar::Args{
			.position = {50.0F, 360.0F},
			.width = 600.0F,
			.tabs =
				{
					{.id = "general", .label = "General"},
					{.id = "graphics", .label = "Graphics"},
					{.id = "audio", .label = "Audio"},
					{.id = "controls", .label = "Controls"},
					{.id = "gameplay", .label = "Gameplay"},
				},
			.selectedId = "general",
			.onSelect =
				[this](const std::string& tabId) {
					currentTab2 = tabId;
					LOG_INFO(UI, "TabBar 2 selected: {}", tabId.c_str());
				},
			.id = "tabbar_2"});

		// Content area for demo 2
		contentBg2 = std::make_unique<Rectangle>(Rectangle::Args{
			.position = {50.0F, 400.0F},
			.size = {600.0F, 120.0F},
			.style =
				{
					.fill = Color{0.15F, 0.15F, 0.2F, 0.9F},
					.border = BorderStyle{
						.color = Color{0.25F, 0.25F, 0.3F, 1.0F},
						.width = 1.0F,
						.cornerRadius = 4.0F,
						.position = BorderPosition::Inside,
					},
				},
			.id = "content_bg_2"});

		contentText2 = std::make_unique<Text>(Text::Args{
			.position = {350.0F, 460.0F},
			.text = "Settings: General",
			.style =
				{
					.color = Color::white(),
					.fontSize = 18.0F,
					.hAlign = HorizontalAlign::Center,
					.vAlign = VerticalAlign::Middle,
				},
			.id = "content_text_2"});

		// Instructions
		labels.push_back(std::make_unique<Text>(Text::Args{
			.position = {50.0F, 560.0F},
			.text = "Instructions: Click tabs to select. Press Tab to focus TabBar, then use Left/Right arrows.",
			.style = {.color = Color{0.7F, 0.7F, 0.7F, 1.0F}, .fontSize = 14.0F},
			.id = "instructions"}));

		labels.push_back(std::make_unique<Text>(Text::Args{
			.position = {50.0F, 580.0F},
			.text = "Disabled tabs (Equipment) cannot be selected.",
			.style = {.color = Color{0.7F, 0.7F, 0.7F, 1.0F}, .fontSize = 14.0F},
			.id = "instructions2"}));

		LOG_INFO(UI, "TabBar scene initialized");
	}

	void onExit() override {
		tabBar1.reset();
		tabBar2.reset();
		contentBg1.reset();
		contentBg2.reset();
		contentText1.reset();
		contentText2.reset();
		labels.clear();
		LOG_INFO(UI, "TabBar scene exited");
	}

	void handleInput(float /*deltaTime*/) override {
		if (tabBar1) {
			tabBar1->handleInput();
		}
		if (tabBar2) {
			tabBar2->handleInput();
		}
	}

	void update(float deltaTime) override {
		if (tabBar1) {
			tabBar1->update(deltaTime);
		}
		if (tabBar2) {
			tabBar2->update(deltaTime);
		}

		// Update content text based on selected tabs
		if (contentText1 && !currentTab1.empty()) {
			std::string capitalizedTab = currentTab1;
			if (!capitalizedTab.empty()) {
				capitalizedTab[0] = static_cast<char>(toupper(capitalizedTab[0]));
			}
			contentText1->text = "Content: " + capitalizedTab;
		}

		if (contentText2 && !currentTab2.empty()) {
			std::string capitalizedTab = currentTab2;
			if (!capitalizedTab.empty()) {
				capitalizedTab[0] = static_cast<char>(toupper(capitalizedTab[0]));
			}
			contentText2->text = "Settings: " + capitalizedTab;
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

		// Render content backgrounds
		if (contentBg1) {
			contentBg1->render();
		}
		if (contentBg2) {
			contentBg2->render();
		}

		// Render tab bars
		if (tabBar1) {
			tabBar1->render();
		}
		if (tabBar2) {
			tabBar2->render();
		}

		// Render content text (on top of backgrounds)
		if (contentText1) {
			contentText1->render();
		}
		if (contentText2) {
			contentText2->render();
		}
	}

  private:
	// Tab bars
	std::unique_ptr<UI::TabBar> tabBar1;
	std::unique_ptr<UI::TabBar> tabBar2;

	// Content areas
	std::unique_ptr<UI::Rectangle> contentBg1;
	std::unique_ptr<UI::Rectangle> contentBg2;
	std::unique_ptr<UI::Text> contentText1;
	std::unique_ptr<UI::Text> contentText2;

	// Labels
	std::vector<std::unique_ptr<UI::Text>> labels;

	// Current tab selections
	std::string currentTab1 = "status";
	std::string currentTab2 = "general";
};

}  // anonymous namespace

// Export scene info for registry
namespace ui_sandbox::scenes {
extern const ui_sandbox::SceneInfo TabBar = {kSceneName, []() { return std::make_unique<TabBarScene>(); }};
}
