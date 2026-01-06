#include "GameUI.h"

#include <primitives/Primitives.h>

namespace world_sim {

	namespace {
		// Layout constants
		constexpr float kPanelWidth = 340.0F;	 // Per plan: 340px for two-column colonist layout
		constexpr float kTaskListWidth = 360.0F; // 2x info panel
		constexpr float kPanelPadding = 10.0F;
		constexpr float kPanelHeight = 148.0F;
		constexpr float kTaskListMaxHeight = 400.0F;

		// Build menu dimensions
		constexpr float kBuildMenuWidth = 180.0F;
	} // namespace

	GameUI::GameUI(const Args& args)
		: onSelectionCleared(args.onSelectionCleared) {

		// Create top bar (date/time and speed controls)
		topBar = std::make_unique<TopBar>(
			TopBar::Args{.onPause = args.onPause, .onSpeedChange = args.onSpeedChange, .onMenuClick = args.onMenuClick, .id = "top_bar"}
		);

		// Create debug overlay (below top bar)
		debugOverlay = std::make_unique<DebugOverlay>(DebugOverlay::Args{});

		// Create zoom control panel (floating on right side)
		zoomControlPanel = std::make_unique<ZoomControlPanel>(ZoomControlPanel::Args{
			.onZoomIn = args.onZoomIn,
			.onZoomOut = args.onZoomOut,
			.onZoomReset = args.onZoomReset,
		});

		// Create gameplay bar (replaces build toolbar)
		gameplayBar = std::make_unique<GameplayBar>(GameplayBar::Args{
			.onBuildClick = args.onBuildToggle,
			.onProductionSelected = args.onProductionSelected,
			.id = "gameplay_bar"});

		// Create build menu (position set in layout())
		buildMenu = std::make_unique<BuildMenu>(BuildMenu::Args{
			.position = {0.0F, 0.0F}, // Will be positioned in layout()
			.onSelect = args.onBuildItemSelected,
			.onClose = [this]() { hideBuildMenu(); },
			.id = "build_menu"
		});

		// Create colonist list view (left side)
		colonistList = std::make_unique<ColonistListView>(ColonistListView::Args{
			.width = 60.0F,
			.itemHeight = 50.0F,
			.onColonistSelected = args.onColonistSelected,
			.onColonistFollowed = args.onColonistFollowed,
			.id = "colonist_list"
		});

		// Create info view (position set in layout())
		// Initial position at (0,0) - will be updated in layout()
		infoPanel = std::make_unique<EntityInfoView>(EntityInfoView::Args{
			.position = {0.0F, 0.0F},
			.width = kPanelWidth,
			.id = "entity_panel",
			.onClose =
				[this]() {
					if (onSelectionCleared) {
						onSelectionCleared();
					}
				},
			.onDetails =
				[this]() {
					// Open colonist details dialog for currently selected colonist
					if (selectedColonistId != 0) {
						showColonistDetails(selectedColonistId);
					}
				},
			.onQueueRecipe = args.onQueueRecipe,
			.onOpenCraftingDialog = args.onOpenCraftingDialog,
			.onPlace = args.onPlaceFurniture,
			.onOpenStorageConfig = args.onOpenStorageConfig,
			.queryResources = args.queryResources
		});

		// Create colonist details dialog
		colonistDetailsDialog =
			std::make_unique<ColonistDetailsDialog>(ColonistDetailsDialog::Args{.onClose = [this]() { hideColonistDetails(); }});

		// Create crafting dialog
		craftingDialog = std::make_unique<CraftingDialog>(CraftingDialog::Args{
			.onClose = [this]() { hideCraftingDialog(); },
			.onQueueRecipe = args.onQueueRecipe,
			.onCancelJob = args.onCancelJob
		});

		// Create storage config dialog
		storageConfigDialog = std::make_unique<StorageConfigDialog>(StorageConfigDialog::Args{
			.onClose = [this]() { hideStorageConfigDialog(); }
		});

		// Create task list view (position set in layout())
		taskListPanel = std::make_unique<TaskListView>(TaskListView::Args{
			.width = kTaskListWidth, .maxHeight = kTaskListMaxHeight, .onClose = [this]() { toggleTaskList(); }, .id = "task_list"
		});

		// Create resources panel (top-right, below where minimap will be)
		resourcesPanel = std::make_unique<ResourcesPanel>(ResourcesPanel::Args{.width = 160.0F, .id = "resources_panel"});

		// Create global task list panel (top-right, below resources panel)
		globalTaskList = std::make_unique<GlobalTaskListView>(GlobalTaskListView::Args{.width = 300.0F});

		// Create toast stack for notifications (bottom-right)
		toastStack = std::make_unique<UI::ToastStack>(UI::ToastStack::Args{
			.position = {0.0F, 0.0F}, // Will be positioned in layout()
			.anchor = UI::ToastAnchor::BottomRight,
			.spacing = 8.0F,
			.maxToasts = 5,
			.toastWidth = 300.0F,
			.id = "toast_stack"
		});
		toastStack->zIndex = 2000; // Above other UI
	}

	void GameUI::layout(const Foundation::Rect& newBounds) {
		viewportBounds = newBounds;

		// Layout top bar (full width at top)
		float topBarHeight = 0.0F;
		if (topBar) {
			topBar->layout(newBounds);
			topBarHeight = topBar->getHeight();
		}

		// Create bounds for content below top bar
		Foundation::Rect contentBounds{newBounds.x, newBounds.y + topBarHeight, newBounds.width, newBounds.height - topBarHeight};

		// Layout debug overlay at bottom-left (below colonist list)
		// Per spec: Debug info appears in bottom-left corner
		if (debugOverlay) {
			// Position above the gameplay bar, leaving room for colonist list
			float			 debugY = newBounds.height - 100.0F; // Above bottom
			Foundation::Rect debugBounds{
				newBounds.x,
				debugY,
				200.0F, // Width for debug text
				80.0F	// Height for 3 lines of text
			};
			debugOverlay->layout(debugBounds);
		}

		// Layout zoom control panel (floating right side)
		if (zoomControlPanel) {
			zoomControlPanel->layout(newBounds);
		}

		// Layout gameplay bar at bottom center
		if (gameplayBar) {
			gameplayBar->layout(newBounds);
		}

		// Position build menu above the gameplay bar, centered
		if (buildMenu) {
			float menuX = (newBounds.width - kBuildMenuWidth) * 0.5F;
			float menuY = newBounds.height - gameplayBar->getHeight() - 12.0F - 10.0F - 150.0F; // Above bar
			buildMenu->setPosition({menuX, menuY});
		}

		// Position colonist list on left side, below top bar and debug overlay
		if (colonistList) {
			colonistList->setPosition(0.0F, topBarHeight + 100.0F);
		}

		// Position info panel in bottom-left corner (flush with edges)
		float panelX = 0.0F;

		// Update panel position with bottom-left alignment (panel computes Y from viewport height)
		if (infoPanel) {
			infoPanel->setBottomLeftPosition(panelX, newBounds.height);

			// Cache panel bounds for hit testing using actual dynamic height
			float actualHeight = infoPanel->getHeight();
			float panelY = newBounds.height - actualHeight;
			infoPanelBounds = Foundation::Rect{panelX, panelY, kPanelWidth, actualHeight};
		}

		// Position task list panel above info panel
		if (taskListPanel) {
			// Calculate available height (viewport height minus top UI area)
			float availableHeight = newBounds.height - 100.0F; // Leave 100px for top-left overlay
			float taskListHeight = std::min(kTaskListMaxHeight, availableHeight);

			// Position at same X as info panel, bottom edge at info panel top
			float taskListBottomY = infoPanelBounds.y;
			float taskListY = taskListBottomY - taskListHeight;

			taskListPanelBounds = Foundation::Rect{panelX, taskListY, kTaskListWidth, taskListHeight};
			taskListPanel->setPosition(panelX, taskListBottomY);
		}

		// Position toast stack in bottom-right corner
		// Per design spec: Notifications appear bottom-right, stacking upward
		if (toastStack) {
			float rightMargin = 20.0F;
			float bottomMargin = 60.0F; // Above gameplay bar
			toastStack->setPosition(newBounds.width - rightMargin, newBounds.height - bottomMargin);
		}

		// Position resources panel in top-right corner, below zoom controls
		// Zoom controls are at Y=80, height=28, so start at Y=120
		if (resourcesPanel) {
			float rightMargin = 20.0F; // Match zoom control margin
			float topMargin = 120.0F;  // Below zoom controls (80 + 28 + 12 margin)
			resourcesPanel->setAnchorPosition(newBounds.width - rightMargin, topMargin);
		}

		// Position global task list below resources panel
		if (globalTaskList) {
			float rightMargin = 20.0F;
			float taskListY = 120.0F; // Start at resources panel position
			if (resourcesPanel) {
				// Position below resources panel bounds
				taskListY = resourcesPanel->getBounds().y + resourcesPanel->getBounds().height + 8.0F;
			}
			globalTaskList->setAnchorPosition(newBounds.width - rightMargin, taskListY);
		}
	}

	bool GameUI::dispatchEvent(UI::InputEvent& event) {
		// Dispatch to UI children in z-order (highest first)
		// Panels that can overlap get priority

		// Storage config dialog (highest z-order - modal overlay)
		if (storageConfigDialog && storageConfigDialog->isOpen()) {
			if (storageConfigDialog->handleEvent(event)) {
				return true;
			}
			if (event.isConsumed()) {
				return true;
			}
		}

		// Crafting dialog (highest z-order - modal overlay)
		if (craftingDialog && craftingDialog->isOpen()) {
			if (craftingDialog->handleEvent(event)) {
				return true;
			}
			if (event.isConsumed()) {
				return true;
			}
		}

		// Colonist details dialog (highest z-order - modal overlay)
		if (colonistDetailsDialog && colonistDetailsDialog->isOpen()) {
			if (colonistDetailsDialog->handleEvent(event)) {
				return true;
			}
			if (event.isConsumed()) {
				return true;
			}
		}

		// Toast stack (highest z-order - notifications)
		if (toastStack) {
			if (toastStack->handleEvent(event)) {
				return true;
			}
			if (event.isConsumed()) {
				return true;
			}
		}

		// Resources panel (top-right)
		if (resourcesPanel) {
			if (resourcesPanel->handleEvent(event)) {
				return true;
			}
			if (event.isConsumed()) {
				return true;
			}
		}

		// Global task list (top-right, below resources panel)
		if (globalTaskList) {
			if (globalTaskList->handleEvent(event)) {
				return true;
			}
			if (event.isConsumed()) {
				return true;
			}
		}

		// Top bar (highest z-order at top of screen)
		if (topBar) {
			if (topBar->handleEvent(event)) {
				return true;
			}
			if (event.isConsumed()) {
				return true;
			}
		}

		// Task list panel (high z-order - appears on top of info panel)
		if (taskListExpanded && taskListPanel && taskListPanel->visible) {
			if (taskListPanel->handleEvent(event)) {
				return true;
			}
			if (event.isConsumed()) {
				return true;
			}
		}

		// Build menu (high z-order - popup over other UI)
		if (buildMenuVisible && buildMenu) {
			if (buildMenu->handleEvent(event)) {
				return true;
			}
			if (event.isConsumed()) {
				return true;
			}
		}

		// Info panel
		if (infoPanel && infoPanel->isVisible()) {
			if (infoPanel->handleEvent(event)) {
				return true;
			}
			if (event.isConsumed()) {
				return true;
			}
		}

		// Colonist list panel
		if (colonistList) {
			if (colonistList->handleEvent(event)) {
				return true;
			}
			if (event.isConsumed()) {
				return true;
			}
		}

		// Gameplay bar
		if (gameplayBar) {
			if (gameplayBar->handleEvent(event)) {
				return true;
			}
			if (event.isConsumed()) {
				return true;
			}
		}

		// Zoom control panel (floating controls)
		if (zoomControlPanel) {
			if (zoomControlPanel->handleEvent(event)) {
				return true;
			}
		}

		// Debug overlay doesn't handle events (text only)

		return event.isConsumed();
	}

	void GameUI::update(
		float								  deltaTime,
		const engine::world::WorldCamera&	  camera,
		const engine::world::ChunkManager&	  chunkManager,
		ecs::World&							  ecsWorld,
		const engine::assets::AssetRegistry&  assetRegistry,
		const engine::assets::RecipeRegistry& recipeRegistry,
		const Selection&					  selection
	) {
		// Update time model and top bar
		if (topBar) {
			timeModel.refresh(ecsWorld);
			topBar->updateData(timeModel);
		}

		// Update debug overlay display values
		if (debugOverlay) {
			debugOverlay->updateData(camera, chunkManager);
		}

		// Update zoom control panel
		if (zoomControlPanel) {
			zoomControlPanel->setZoomPercent(camera.zoomPercent());
		}

		// Update colonist list with model-based change detection
		if (colonistList) {
			ecs::EntityID currentlySelected{0};
			if (auto* colonistSel = std::get_if<ColonistSelection>(&selection)) {
				currentlySelected = colonistSel->entityId;
			}
			colonistListModel.setSelectedId(currentlySelected);
			colonistList->update(colonistListModel, ecsWorld);
		}

		// Update global task list (throttled 5Hz refresh)
		if (globalTaskList) {
			globalTaskList->update(deltaTime);
			glm::vec2 cameraCenter{camera.position().x, camera.position().y};
			if (globalTaskListModel.refresh(ecsWorld, cameraCenter, deltaTime)) {
				globalTaskList->setTasks(globalTaskListModel.tasks());
			}
			globalTaskList->setTaskCount(globalTaskListModel.taskCount());
		}

		// Track selected colonist for task list panel
		ecs::EntityID newColonistId{0};
		if (auto* colonistSel = std::get_if<ColonistSelection>(&selection)) {
			newColonistId = colonistSel->entityId;
		}

		// Close task list if selection changed or not a colonist
		if (newColonistId != selectedColonistId) {
			selectedColonistId = newColonistId;
			if (taskListExpanded) {
				taskListExpanded = false;
				if (taskListPanel) {
					taskListPanel->visible = false;
				}
			}
		}

		// Update info panel with selection
		if (infoPanel) {
			infoPanel->update(ecsWorld, assetRegistry, recipeRegistry, selection);
		}

		// Update task list panel if expanded
		if (taskListExpanded && taskListPanel && selectedColonistId != 0) {
			taskListPanel->update(ecsWorld, selectedColonistId);
		}

		// Update toast stack animations
		if (toastStack) {
			toastStack->update(deltaTime);
		}

		// Update colonist details dialog if open
		if (colonistDetailsDialog && colonistDetailsDialog->isOpen()) {
			colonistDetailsDialog->update(ecsWorld, deltaTime);
		}

		// Update crafting dialog if open
		if (craftingDialog && craftingDialog->isOpen()) {
			craftingDialog->update(ecsWorld, recipeRegistry, deltaTime);
		}

		// Update storage config dialog if open
		if (storageConfigDialog && storageConfigDialog->isOpen()) {
			storageConfigDialog->update(ecsWorld, assetRegistry, deltaTime);
		}
	}

	void GameUI::render() {
		// Render top bar (date/time and speed controls)
		if (topBar) {
			topBar->render();
		}

		// Render debug overlay (below top bar)
		if (debugOverlay) {
			debugOverlay->render();
		}

		// Render zoom control panel
		if (zoomControlPanel) {
			zoomControlPanel->render();
		}

		// Render gameplay bar
		if (gameplayBar) {
			gameplayBar->render();
		}

		// Render build menu if visible
		if (buildMenuVisible && buildMenu) {
			buildMenu->render();
		}

		// Render colonist list
		if (colonistList) {
			colonistList->render();
		}

		// Render info panel if visible
		if (infoPanel && infoPanel->isVisible()) {
			infoPanel->render();
		}

		// Render task list panel if expanded
		if (taskListExpanded && taskListPanel && taskListPanel->visible) {
			taskListPanel->render();
		}

		// Render resources panel (top-right)
		if (resourcesPanel) {
			resourcesPanel->render();
		}

		// Render global task list (top-right, below resources panel)
		if (globalTaskList) {
			globalTaskList->render();
		}

		// Render toast notifications (highest z-order)
		if (toastStack) {
			toastStack->render();
		}

		// Render colonist details dialog (highest z-order - modal overlay)
		if (colonistDetailsDialog && colonistDetailsDialog->isOpen()) {
			colonistDetailsDialog->render();
		}

		// Render crafting dialog (highest z-order - modal overlay)
		if (craftingDialog && craftingDialog->isOpen()) {
			craftingDialog->render();
		}

		// Render storage config dialog (highest z-order - modal overlay)
		if (storageConfigDialog && storageConfigDialog->isOpen()) {
			storageConfigDialog->render();
		}
	}

	void GameUI::pushNotification(
		const std::string&	  title,
		const std::string&	  message,
		UI::ToastSeverity	  severity,
		float				  autoDismissTime,
		std::function<void()> onClick
	) {
		if (toastStack) {
			if (onClick) {
				toastStack->addToast(title, message, severity, autoDismissTime, std::move(onClick));
			} else {
				toastStack->addToast(title, message, severity, autoDismissTime);
			}
		}
	}

	void GameUI::toggleTaskList() {
		taskListExpanded = !taskListExpanded;
		if (taskListPanel) {
			taskListPanel->visible = taskListExpanded;
		}
	}

	// --- Build Mode API ---

	void GameUI::setBuildModeActive(bool /*active*/) {
		// GameplayBar doesn't track active state - build menu visibility is enough
	}

	void GameUI::showBuildMenu(const std::vector<BuildMenuItem>& items) {
		if (buildMenu) {
			buildMenu->setItems(items);
			buildMenuVisible = true;
			buildMenuBounds = buildMenu->bounds();
		}
	}

	void GameUI::hideBuildMenu() {
		buildMenuVisible = false;
	}

	bool GameUI::isBuildMenuVisible() const {
		return buildMenuVisible;
	}

	void GameUI::setProductionItems(const std::vector<std::pair<std::string, std::string>>& items) {
		if (gameplayBar) {
			gameplayBar->setProductionItems(items);
		}
	}

	// --- Colonist Details Dialog API ---

	void GameUI::showColonistDetails(ecs::EntityID colonistId) {
		if (colonistDetailsDialog) {
			colonistDetailsDialog->open(colonistId, viewportBounds.width, viewportBounds.height);
		}
	}

	void GameUI::hideColonistDetails() {
		if (colonistDetailsDialog) {
			colonistDetailsDialog->close();
		}
	}

	bool GameUI::isColonistDetailsVisible() const {
		return colonistDetailsDialog && colonistDetailsDialog->isOpen();
	}

	// --- Crafting Dialog API ---

	void GameUI::showCraftingDialog(ecs::EntityID stationId, const std::string& stationDefName) {
		if (craftingDialog) {
			craftingDialog->open(stationId, stationDefName, viewportBounds.width, viewportBounds.height);
		}
	}

	void GameUI::hideCraftingDialog() {
		if (craftingDialog) {
			craftingDialog->close();
		}
	}

	bool GameUI::isCraftingDialogVisible() const {
		return craftingDialog && craftingDialog->isOpen();
	}

	// --- Storage Config Dialog API ---

	void GameUI::showStorageConfigDialog(ecs::EntityID containerId, const std::string& containerDefName) {
		if (storageConfigDialog) {
			storageConfigDialog->open(containerId, containerDefName, viewportBounds.width, viewportBounds.height);
		}
	}

	void GameUI::hideStorageConfigDialog() {
		if (storageConfigDialog) {
			storageConfigDialog->close();
		}
	}

	bool GameUI::isStorageConfigVisible() const {
		return storageConfigDialog && storageConfigDialog->isOpen();
	}

	bool GameUI::isGlobalTaskListExpanded() const {
		return globalTaskList && globalTaskList->isExpanded();
	}

} // namespace world_sim
