#pragma once

// GameUI - Main UI container for the game scene.
//
// Contains all game UI elements as children:
// - GameOverlay: status display, zoom controls
// - BuildToolbar: build mode toggle button
// - BuildMenu: popup for selecting items to place
// - ColonistListPanel: left-side colonist portraits
// - EntityInfoPanel: selected entity information
// - TaskListPanel: expanded task queue (toggle from info panel)
//
// Handles input consumption to prevent click-through to world.

#include "BuildMenu.h"
#include "BuildToolbar.h"
#include "ColonistListPanel.h"
#include "EntityInfoPanel.h"
#include "GameOverlay.h"
#include "Selection.h"
#include "TaskListPanel.h"

#include <assets/AssetRegistry.h>
#include <ecs/World.h>
#include <graphics/Rect.h>
#include <world/camera/WorldCamera.h>
#include <world/chunk/ChunkManager.h>

#include <functional>
#include <memory>

namespace world_sim {

/// Main UI container for the game scene
class GameUI {
  public:
	struct Args {
		std::function<void()> onZoomIn;
		std::function<void()> onZoomOut;
		std::function<void()> onSelectionCleared;
		std::function<void(ecs::EntityID)> onColonistSelected;
		std::function<void()> onBuildToggle;							 ///< Called when build button clicked
		std::function<void(const std::string&)> onBuildItemSelected; ///< Called when item selected from build menu
	};

	explicit GameUI(const Args& args);

	/// Layout all UI elements within viewport bounds
	/// @param viewportBounds Logical viewport bounds (not framebuffer)
	void layout(const Foundation::Rect& viewportBounds);

	/// Handle input for UI elements
	/// @return true if UI consumed the input (prevent world interaction)
	bool handleInput();

	/// Update UI state
	void update(
		const engine::world::WorldCamera& camera,
		const engine::world::ChunkManager& chunkManager,
		ecs::World& ecsWorld,
		const engine::assets::AssetRegistry& registry,
		const Selection& selection
	);

	/// Render all UI elements
	void render();

	/// Check if a screen position is within any UI element bounds
	/// @param screenPos Position in logical screen coordinates
	/// @return true if position is over a UI element
	[[nodiscard]] bool isPointOverUI(Foundation::Vec2 screenPos) const;

	// --- Build Mode API ---

	/// Set whether build mode is active (updates toolbar button state)
	void setBuildModeActive(bool active);

	/// Show build menu with available items
	void showBuildMenu(const std::vector<BuildMenuItem>& items);

	/// Hide build menu
	void hideBuildMenu();

	/// Check if build menu is visible
	[[nodiscard]] bool isBuildMenuVisible() const;

  private:
	/// Check if a point is within info panel bounds (when visible)
	[[nodiscard]] bool isPointOverInfoPanel(Foundation::Vec2 screenPos) const;

	std::unique_ptr<GameOverlay> overlay;
	std::unique_ptr<BuildToolbar> buildToolbar;
	std::unique_ptr<BuildMenu> buildMenu;
	std::unique_ptr<ColonistListPanel> colonistList;
	std::unique_ptr<EntityInfoPanel> infoPanel;
	std::unique_ptr<TaskListPanel> taskListPanel;

	// Task list expansion state
	bool taskListExpanded = false;
	ecs::EntityID selectedColonistId{0};

	// Build mode state
	bool buildMenuVisible = false;

	// Cached bounds for hit testing
	Foundation::Rect viewportBounds;
	Foundation::Rect infoPanelBounds;
	Foundation::Rect taskListPanelBounds;
	Foundation::Rect buildMenuBounds;

	// Callbacks
	std::function<void()> onSelectionCleared;

	// Toggle task list panel visibility
	void toggleTaskList();
};

} // namespace world_sim
