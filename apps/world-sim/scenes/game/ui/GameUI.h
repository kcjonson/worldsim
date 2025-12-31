#pragma once

// GameUI - Main UI container for the game scene.
//
// Contains all game UI elements as children:
// - DebugOverlay: chunk/position/biome display (top-left)
// - ZoomControlPanel: floating zoom controls (right side)
// - BuildToolbar: build mode toggle button (temporary, will be replaced by GameplayBar)
// - BuildMenu: popup for selecting items to place
// - ColonistListView: left-side colonist portraits
// - EntityInfoView: selected entity information
// - TaskListView: expanded task queue (toggle from info panel)
//
// Handles input consumption to prevent click-through to world.

#include "scenes/game/ui/views/BuildMenu.h"
#include "scenes/game/ui/views/GameplayBar.h"
#include "scenes/game/ui/views/ColonistListView.h"
#include "scenes/game/ui/views/DebugOverlay.h"
#include "scenes/game/ui/views/TopBar.h"
#include "scenes/game/ui/views/ResourcesPanel.h"
#include "scenes/game/ui/models/ColonistListModel.h"
#include "scenes/game/ui/models/TimeModel.h"
#include "scenes/game/ui/views/EntityInfoView.h"
#include "scenes/game/ui/views/ZoomControlPanel.h"
#include "scenes/game/world/selection/SelectionTypes.h"
#include "scenes/game/ui/views/TaskListView.h"
#include "scenes/game/ui/dialogs/ColonistDetailsDialog.h"
#include "scenes/game/ui/dialogs/CraftingDialog.h"

#include <components/toast/ToastStack.h>

#include <ecs/systems/TimeSystem.h>

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

/// Callback to query remaining resource count for a world entity
using ResourceQueryCallback = std::function<std::optional<uint32_t>(const std::string& defName, Foundation::Vec2 position)>;

/// Main UI container for the game scene
class GameUI {
  public:
	struct Args {
		std::function<void()> onZoomIn;
		std::function<void()> onZoomOut;
		std::function<void()> onZoomReset;
		std::function<void()> onSelectionCleared;
		std::function<void(ecs::EntityID)> onColonistSelected;
		std::function<void(ecs::EntityID)> onColonistFollowed;		 ///< Called on double-click to follow
		std::function<void()> onBuildToggle;							 ///< Called when build button clicked
		std::function<void(const std::string&)> onBuildItemSelected; ///< Called when item selected from build menu
		std::function<void(const std::string&)> onProductionSelected; ///< Called when production item selected (e.g., CraftingSpot)
		QueueRecipeCallback onQueueRecipe;								 ///< Called when recipe queued at station
		std::function<void(const std::string&)> onCancelJob;			 ///< Called when job canceled from queue
		std::function<void(ecs::EntityID, const std::string&)> onOpenCraftingDialog; ///< Called to open crafting dialog
		std::function<void()> onPause;									 ///< Called when pause button clicked
		std::function<void(ecs::GameSpeed)> onSpeedChange;			 ///< Called when speed changed
		std::function<void()> onMenuClick;								 ///< Called when menu button clicked
		std::function<void()> onPlaceFurniture;						 ///< Called when Place button clicked for packaged furniture
		ResourceQueryCallback queryResources;							 ///< Query remaining resource count for harvestable entities
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
		float deltaTime,
		const engine::world::WorldCamera& camera,
		const engine::world::ChunkManager& chunkManager,
		ecs::World& ecsWorld,
		const engine::assets::AssetRegistry& assetRegistry,
		const engine::assets::RecipeRegistry& recipeRegistry,
		const Selection& selection
	);

	/// Render all UI elements
	void render();

	/// Push a notification to the toast stack
	/// @param title Short title for the notification
	/// @param message Detailed message
	/// @param severity Info, Warning, or Critical (affects styling)
	/// @param autoDismissTime Seconds before auto-dismiss (0 = persistent)
	/// @param onClick Optional callback when notification is clicked (for navigation)
	void pushNotification(const std::string& title, const std::string& message,
						  UI::ToastSeverity severity = UI::ToastSeverity::Info,
						  float autoDismissTime = 5.0F,
						  std::function<void()> onClick = nullptr);

	// --- Build Mode API ---

	/// Set whether build mode is active (updates toolbar button state)
	void setBuildModeActive(bool active);

	/// Show build menu with available items
	void showBuildMenu(const std::vector<BuildMenuItem>& items);

	/// Hide build menu
	void hideBuildMenu();

	/// Check if build menu is visible
	[[nodiscard]] bool isBuildMenuVisible() const;

	/// Set the production station items in the Production dropdown
	/// @param items Vector of {defName, label} pairs for placeable production stations
	void setProductionItems(const std::vector<std::pair<std::string, std::string>>& items);

	// --- Colonist Details Dialog API ---

	/// Show colonist details dialog for a specific colonist
	void showColonistDetails(ecs::EntityID colonistId);

	/// Hide colonist details dialog
	void hideColonistDetails();

	/// Check if colonist details dialog is visible
	[[nodiscard]] bool isColonistDetailsVisible() const;

	// --- Crafting Dialog API ---

	/// Show crafting dialog for a specific station
	void showCraftingDialog(ecs::EntityID stationId, const std::string& stationDefName);

	/// Hide crafting dialog
	void hideCraftingDialog();

	/// Check if crafting dialog is visible
	[[nodiscard]] bool isCraftingDialogVisible() const;

  private:
	std::unique_ptr<TopBar> topBar;
	std::unique_ptr<DebugOverlay> debugOverlay;
	std::unique_ptr<ZoomControlPanel> zoomControlPanel;
	std::unique_ptr<GameplayBar> gameplayBar;
	std::unique_ptr<BuildMenu> buildMenu;
	std::unique_ptr<ColonistListView> colonistList;
	std::unique_ptr<EntityInfoView> infoPanel;
	std::unique_ptr<TaskListView> taskListPanel;
	std::unique_ptr<ResourcesPanel> resourcesPanel;
	std::unique_ptr<UI::ToastStack> toastStack;
	std::unique_ptr<ColonistDetailsDialog> colonistDetailsDialog;
	std::unique_ptr<CraftingDialog> craftingDialog;

	// ViewModels (own data + change detection)
	TimeModel timeModel;
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
