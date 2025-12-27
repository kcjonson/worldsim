// ContextMenu Scene - Demonstrates the ContextMenu component
// Shows right-click popup menus with different configurations

#include <GL/glew.h>

#include "SceneTypes.h"
#include <components/button/Button.h>
#include <components/contextmenu/ContextMenu.h>
#include <graphics/Color.h>
#include <input/InputEvent.h>
#include <memory>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <shapes/Shapes.h>
#include <theme/Theme.h>
#include <utils/Log.h>

namespace {

constexpr const char* kSceneName = "contextmenu";

class ContextMenuScene : public engine::IScene {
  public:
	const char* getName() const override { return kSceneName; }
	std::string exportState() override { return "{}"; }

	void onEnter() override {
		using namespace UI;
		using namespace Foundation;

		// Create title
		title = std::make_unique<Text>(Text::Args{
			.position = {50.0F, 30.0F},
			.text = "Context Menu Demo",
			.style = {.color = Color::white(), .fontSize = 20.0F},
			.id = "title"});

		// ================================================================
		// Instructions
		// ================================================================
		instructions1 = std::make_unique<Text>(Text::Args{
			.position = {50.0F, 70.0F},
			.text = "Right-click anywhere to open a context menu",
			.style = {.color = Color::yellow(), .fontSize = 14.0F},
			.id = "instructions_1"});

		instructions2 = std::make_unique<Text>(Text::Args{
			.position = {50.0F, 95.0F},
			.text = "Use arrow keys to navigate, Enter to select, Escape to close",
			.style = {.color = Color(0.6F, 0.6F, 0.7F, 1.0F), .fontSize = 12.0F},
			.id = "instructions_2"});

		// ================================================================
		// Status display
		// ================================================================
		statusLabel = std::make_unique<Text>(Text::Args{
			.position = {50.0F, 150.0F},
			.text = "Last action: (none)",
			.style = {.color = Color::white(), .fontSize = 14.0F},
			.id = "status"});

		// ================================================================
		// Demo zones
		// ================================================================
		// Zone 1: Edit menu
		zone1Label = std::make_unique<Text>(Text::Args{
			.position = {75.0F, 220.0F},
			.text = "Edit Zone",
			.style = {.color = Color::white(), .fontSize = 12.0F},
			.id = "zone1_label"});

		zone1Rect = Rect{50.0F, 200.0F, 200.0F, 150.0F};

		// Zone 2: File menu
		zone2Label = std::make_unique<Text>(Text::Args{
			.position = {325.0F, 220.0F},
			.text = "File Zone",
			.style = {.color = Color::white(), .fontSize = 12.0F},
			.id = "zone2_label"});

		zone2Rect = Rect{300.0F, 200.0F, 200.0F, 150.0F};

		// Zone 3: Mixed (with disabled items)
		zone3Label = std::make_unique<Text>(Text::Args{
			.position = {575.0F, 220.0F},
			.text = "Mixed Zone",
			.style = {.color = Color::white(), .fontSize = 12.0F},
			.id = "zone3_label"});

		zone3Rect = Rect{550.0F, 200.0F, 200.0F, 150.0F};

		// Create context menus
		editMenu = std::make_unique<ContextMenu>(ContextMenu::Args{
			.items =
				{
					{.label = "Cut", .onSelect = [this]() { setStatus("Cut"); }},
					{.label = "Copy", .onSelect = [this]() { setStatus("Copy"); }},
					{.label = "Paste", .onSelect = [this]() { setStatus("Paste"); }},
					{.label = "Select All", .onSelect = [this]() { setStatus("Select All"); }},
				},
			.onClose = [this]() { LOG_INFO(UI, "Edit menu closed"); },
		});

		fileMenu = std::make_unique<ContextMenu>(ContextMenu::Args{
			.items =
				{
					{.label = "New", .onSelect = [this]() { setStatus("New"); }},
					{.label = "Open", .onSelect = [this]() { setStatus("Open"); }},
					{.label = "Save", .onSelect = [this]() { setStatus("Save"); }},
					{.label = "Save As...", .onSelect = [this]() { setStatus("Save As"); }},
				},
			.onClose = [this]() { LOG_INFO(UI, "File menu closed"); },
		});

		mixedMenu = std::make_unique<ContextMenu>(ContextMenu::Args{
			.items =
				{
					{.label = "Enabled Item", .onSelect = [this]() { setStatus("Enabled Item"); }},
					{.label = "Disabled Item", .onSelect = []() {}, .enabled = false},
					{.label = "Another Enabled", .onSelect = [this]() { setStatus("Another Enabled"); }},
					{.label = "Also Disabled", .onSelect = []() {}, .enabled = false},
				},
			.onClose = [this]() { LOG_INFO(UI, "Mixed menu closed"); },
		});

		// Default menu for outside zones
		defaultMenu = std::make_unique<ContextMenu>(ContextMenu::Args{
			.items =
				{
					{.label = "Default Action 1", .onSelect = [this]() { setStatus("Default 1"); }},
					{.label = "Default Action 2", .onSelect = [this]() { setStatus("Default 2"); }},
				},
			.onClose = [this]() { LOG_INFO(UI, "Default menu closed"); },
		});

		LOG_INFO(UI, "ContextMenu scene initialized");
	}

	void onExit() override {
		title.reset();
		instructions1.reset();
		instructions2.reset();
		statusLabel.reset();
		zone1Label.reset();
		zone2Label.reset();
		zone3Label.reset();
		editMenu.reset();
		fileMenu.reset();
		mixedMenu.reset();
		defaultMenu.reset();
		LOG_INFO(UI, "ContextMenu scene exited");
	}

	bool handleInput(UI::InputEvent& event) override {
		using namespace UI;

		// Check if any menu is open and handle its events first
		if (editMenu && editMenu->isOpen()) {
			return editMenu->handleEvent(event);
		}
		if (fileMenu && fileMenu->isOpen()) {
			return fileMenu->handleEvent(event);
		}
		if (mixedMenu && mixedMenu->isOpen()) {
			return mixedMenu->handleEvent(event);
		}
		if (defaultMenu && defaultMenu->isOpen()) {
			return defaultMenu->handleEvent(event);
		}

		// Handle right-click to open menu
		if (event.type == InputEvent::Type::MouseDown) {
			// Check for right-click (button 1)
			// Note: In real implementation, we'd check for right mouse button
			// For demo, we'll use any click since we don't have button info
			if (event.button == engine::MouseButton::Right) { // Right click
				openMenuAt(event.position);
				event.consume();
				return true;
			}
		}

		return false;
	}

	void update(float deltaTime) override {
		if (editMenu) {
			editMenu->update(deltaTime);
		}
		if (fileMenu) {
			fileMenu->update(deltaTime);
		}
		if (mixedMenu) {
			mixedMenu->update(deltaTime);
		}
		if (defaultMenu) {
			defaultMenu->update(deltaTime);
		}
	}

	void render() override {
		using namespace Foundation;
		using namespace UI;

		// Clear background
		glClearColor(0.10F, 0.10F, 0.13F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);

		// Render labels
		if (title) {
			title->render();
		}
		if (instructions1) {
			instructions1->render();
		}
		if (instructions2) {
			instructions2->render();
		}
		if (statusLabel) {
			statusLabel->render();
		}

		// Render zone backgrounds
		Renderer::Primitives::drawRect(Renderer::Primitives::RectArgs{
			.bounds = zone1Rect,
			.style = {.fill = Color(0.2F, 0.25F, 0.3F, 1.0F),
					  .border = BorderStyle{.color = Color(0.4F, 0.5F, 0.6F, 1.0F), .width = 1.0F}},
			.zIndex = 0,
		});
		Renderer::Primitives::drawRect(Renderer::Primitives::RectArgs{
			.bounds = zone2Rect,
			.style = {.fill = Color(0.25F, 0.2F, 0.3F, 1.0F),
					  .border = BorderStyle{.color = Color(0.5F, 0.4F, 0.6F, 1.0F), .width = 1.0F}},
			.zIndex = 0,
		});
		Renderer::Primitives::drawRect(Renderer::Primitives::RectArgs{
			.bounds = zone3Rect,
			.style = {.fill = Color(0.3F, 0.2F, 0.25F, 1.0F),
					  .border = BorderStyle{.color = Color(0.6F, 0.4F, 0.5F, 1.0F), .width = 1.0F}},
			.zIndex = 0,
		});

		// Render zone labels
		if (zone1Label) {
			zone1Label->render();
		}
		if (zone2Label) {
			zone2Label->render();
		}
		if (zone3Label) {
			zone3Label->render();
		}

		// Render context menus (on top)
		if (editMenu) {
			editMenu->render();
		}
		if (fileMenu) {
			fileMenu->render();
		}
		if (mixedMenu) {
			mixedMenu->render();
		}
		if (defaultMenu) {
			defaultMenu->render();
		}
	}

  private:
	void setStatus(const std::string& action) {
		if (statusLabel) {
			statusLabel->text = "Last action: " + action;
		}
	}

	void openMenuAt(Foundation::Vec2 pos) {
		// Close any open menus first
		if (editMenu)
			editMenu->close();
		if (fileMenu)
			fileMenu->close();
		if (mixedMenu)
			mixedMenu->close();
		if (defaultMenu)
			defaultMenu->close();

		// Determine which zone was clicked
		if (zone1Rect.contains(pos)) {
			editMenu->openAt(pos, 800.0F, 600.0F);
		} else if (zone2Rect.contains(pos)) {
			fileMenu->openAt(pos, 800.0F, 600.0F);
		} else if (zone3Rect.contains(pos)) {
			mixedMenu->openAt(pos, 800.0F, 600.0F);
		} else {
			defaultMenu->openAt(pos, 800.0F, 600.0F);
		}
	}

	// Labels
	std::unique_ptr<UI::Text> title;
	std::unique_ptr<UI::Text> instructions1;
	std::unique_ptr<UI::Text> instructions2;
	std::unique_ptr<UI::Text> statusLabel;
	std::unique_ptr<UI::Text> zone1Label;
	std::unique_ptr<UI::Text> zone2Label;
	std::unique_ptr<UI::Text> zone3Label;

	// Zone rectangles
	Foundation::Rect zone1Rect;
	Foundation::Rect zone2Rect;
	Foundation::Rect zone3Rect;

	// Context menus
	std::unique_ptr<UI::ContextMenu> editMenu;
	std::unique_ptr<UI::ContextMenu> fileMenu;
	std::unique_ptr<UI::ContextMenu> mixedMenu;
	std::unique_ptr<UI::ContextMenu> defaultMenu;
};

} // anonymous namespace

// Export scene info for registry
namespace ui_sandbox::scenes {
extern const ui_sandbox::SceneInfo ContextMenu_ = {kSceneName, []() { return std::make_unique<ContextMenuScene>(); }};
} // namespace ui_sandbox::scenes
