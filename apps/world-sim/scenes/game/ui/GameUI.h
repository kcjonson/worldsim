#pragma once

// GameUI - Main UI container for the game scene.
//
// Contains all game UI elements as children:
// - GameOverlay: status display, zoom controls
// - BuildToolbar: build mode toggle button
// - BuildMenu: popup for selecting items to place
// - ColonistListView: left-side colonist portraits
// - EntityInfoView: selected entity information
// - TaskListView: expanded task queue (toggle from info panel)
//
// Handles input consumption to prevent click-through to world.

#include "scenes/game/ui/views/BuildMenu.h"
#include "scenes/game/ui/views/BuildToolbar.h"
#include "scenes/game/ui/views/ColonistListView.h"
#include "scenes/game/ui/models/ColonistListModel.h"
#include "scenes/game/ui/views/EntityInfoView.h"
#include "scenes/game/ui/views/GameOverlay.h"
#include "scenes/game/world/NotificationManager.h"
#include "scenes/game/ui/components/Selection.h"
#include "scenes/game/ui/views/TaskListView.h"

#include <input/InputEvent.h>

#include <assets/AssetRegistry.h>
#include <assets/RecipeRegistry.h>
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
		QueueRecipeCallback onQueueRecipe;								 ///< Called when recipe queued at station
	};

	explicit GameUI(const Args& args);

	/// Layout all UI elements within viewport bounds
	/// @param viewportBounds Logical viewport bounds (not framebuffer)
	void layout(const Foundation::Rect& viewportBounds);

	/// Dispatch an input event to all UI children
	/// @param event The event to dispatch - will have consumed flag set if any child handled it
	/// @return true if any child consumed the event
	bool dispatchEvent(UI::InputEvent& event);

	/// Update UI state
	void update(
		const engine::world::WorldCamera& camera,
		const engine::world::ChunkManager& chunkManager,
		ecs::World& ecsWorld,
		const engine::assets::AssetRegistry& assetRegistry,
		const engine::assets::RecipeRegistry& recipeRegistry,
		const Selection& selection
	);

	/// Render all UI elements
	void render();

	/// Render notifications (call after render() for proper z-order)
	void renderNotifications(const NotificationManager& notifications);

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
	std::unique_ptr<GameOverlay> overlay;
	std::unique_ptr<BuildToolbar> buildToolbar;
	std::unique_ptr<BuildMenu> buildMenu;
	std::unique_ptr<ColonistListView> colonistList;
	std::unique_ptr<EntityInfoView> infoPanel;
	std::unique_ptr<TaskListView> taskListPanel;

	// ViewModel for colonist list (owns data + change detection)
	ColonistListModel colonistListModel;

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
