#include "GameUI.h"

#include <primitives/Primitives.h>

namespace world_sim {

namespace {
	// Layout constants
	constexpr float kPanelWidth = 240.0F;
	constexpr float kTaskListWidth = 360.0F; // 2x info panel
	constexpr float kPanelPadding = 10.0F;
	constexpr float kPanelHeight = 148.0F;
	constexpr float kTaskListMaxHeight = 400.0F;

	// Build toolbar dimensions
	constexpr float kBuildToolbarWidth = 70.0F;
	constexpr float kBuildToolbarHeight = 28.0F;
	constexpr float kBuildToolbarBottomMargin = 20.0F;

	// Build menu dimensions
	constexpr float kBuildMenuWidth = 180.0F;
} // namespace

GameUI::GameUI(const Args& args)
	: onSelectionCleared(args.onSelectionCleared) {

	// Create overlay with zoom callbacks
	overlay = std::make_unique<GameOverlay>(GameOverlay::Args{
		.onZoomIn = args.onZoomIn,
		.onZoomOut = args.onZoomOut,
	});

	// Create build toolbar (position set in layout())
	buildToolbar = std::make_unique<BuildToolbar>(BuildToolbar::Args{
		.position = {0.0F, 0.0F},  // Will be positioned in layout()
		.onBuildClick = args.onBuildToggle,
		.id = "build_toolbar"
	});

	// Create build menu (position set in layout())
	buildMenu = std::make_unique<BuildMenu>(BuildMenu::Args{
		.position = {0.0F, 0.0F},  // Will be positioned in layout()
		.onSelect = args.onBuildItemSelected,
		.onClose = [this]() { hideBuildMenu(); },
		.id = "build_menu"
	});

	// Create colonist list panel (left side)
	colonistList = std::make_unique<ColonistListPanel>(ColonistListPanel::Args{
		.width = 60.0F,
		.itemHeight = 50.0F,
		.onColonistSelected = args.onColonistSelected,
		.id = "colonist_list"});

	// Create info panel (position set in layout())
	// Initial position at (0,0) - will be updated in layout()
	infoPanel = std::make_unique<EntityInfoPanel>(EntityInfoPanel::Args{
		.position = {0.0F, 0.0F},
		.width = kPanelWidth,
		.id = "entity_panel",
		.onClose =
			[this]() {
				if (onSelectionCleared) {
					onSelectionCleared();
				}
			},
		.onTaskListToggle = [this]() { toggleTaskList(); },
		.onQueueRecipe = args.onQueueRecipe});

	// Create task list panel (position set in layout())
	taskListPanel = std::make_unique<TaskListPanel>(TaskListPanel::Args{
		.width = kTaskListWidth,
		.maxHeight = kTaskListMaxHeight,
		.onClose = [this]() { toggleTaskList(); },
		.id = "task_list"});
}

void GameUI::layout(const Foundation::Rect& newBounds) {
	viewportBounds = newBounds;

	// Layout overlay to full viewport
	overlay->layout(newBounds);

	// Position build toolbar at bottom center
	if (buildToolbar) {
		float toolbarX = (newBounds.width - kBuildToolbarWidth) * 0.5F;
		float toolbarY = newBounds.height - kBuildToolbarHeight - kBuildToolbarBottomMargin;
		buildToolbar->setPosition({toolbarX, toolbarY});
	}

	// Position build menu above the toolbar, centered
	if (buildMenu) {
		float menuX = (newBounds.width - kBuildMenuWidth) * 0.5F;
		float menuY = newBounds.height - kBuildToolbarHeight - kBuildToolbarBottomMargin - 10.0F - 150.0F; // Above toolbar
		buildMenu->setPosition({menuX, menuY});
	}

	// Position colonist list on left side, below overlay and zoom controls
	if (colonistList) {
		colonistList->setPosition(0.0F, 130.0F);
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
}

bool GameUI::dispatchEvent(UI::InputEvent& event) {
	// Dispatch to UI children in z-order (highest first)
	// Panels that can overlap get priority

	// Task list panel (highest - appears on top of info panel)
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

	// Build toolbar
	if (buildToolbar) {
		if (buildToolbar->handleEvent(event)) {
			return true;
		}
		if (event.isConsumed()) {
			return true;
		}
	}

	// Overlay (contains zoom control with buttons)
	if (overlay) {
		if (overlay->handleEvent(event)) {
			return true;
		}
	}

	return event.isConsumed();
}

void GameUI::update(
	const engine::world::WorldCamera& camera,
	const engine::world::ChunkManager& chunkManager,
	ecs::World& ecsWorld,
	const engine::assets::AssetRegistry& assetRegistry,
	const engine::assets::RecipeRegistry& recipeRegistry,
	const Selection& selection
) {
	// Update overlay display values
	overlay->update(camera, chunkManager);

	// Update colonist list
	if (colonistList) {
		ecs::EntityID currentlySelected{0};
		if (auto* colonistSel = std::get_if<ColonistSelection>(&selection)) {
			currentlySelected = colonistSel->entityId;
		}
		colonistList->update(ecsWorld, currentlySelected);
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
}

void GameUI::render() {
	// Render overlay
	overlay->render();

	// Render build toolbar
	if (buildToolbar) {
		buildToolbar->render();
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
}

void GameUI::renderNotifications(const NotificationManager& notifications) {
	if (!notifications.hasNotifications()) {
		return;
	}

	// Notification styling
	constexpr float kPadding = 12.0F;
	constexpr float kMargin = 8.0F;
	constexpr float kFontScale = 0.875F; // 14px equivalent (14/16 base)
	constexpr float kMaxWidth = 300.0F;
	constexpr float kRightMargin = 20.0F;
	constexpr float kBottomMargin = 20.0F;

	// Position at bottom-right of screen, stacking upwards
	float rightEdge = viewportBounds.x + viewportBounds.width - kRightMargin;
	float currentY = viewportBounds.y + viewportBounds.height - kBottomMargin;

	size_t count = 0;
	for (const auto& notification : notifications.notifications()) {
		if (count >= NotificationManager::kMaxVisible) {
			break;
		}

		float opacity = notification.opacity();
		if (opacity <= 0.0F) {
			continue;
		}

		// Calculate text bounds
		float textWidth = std::min(kMaxWidth, notification.message.length() * 7.0F); // Approximate
		float boxWidth = textWidth + kPadding * 2.0F;
		float boxHeight = 14.0F + kPadding * 2.0F; // 14px text height

		// Position box at bottom-right, moving up for each notification
		currentY -= boxHeight;

		// Draw background
		Foundation::Rect bgRect{
			rightEdge - boxWidth,
			currentY,
			boxWidth,
			boxHeight
		};

		Renderer::Primitives::drawRect({
			.bounds = bgRect,
			.style = {.fill = {0.15F, 0.15F, 0.2F, 0.9F * opacity}},
			.id = "notification_bg",
			.zIndex = 2000
		});

		// Draw text
		Renderer::Primitives::drawText({
			.text = notification.message,
			.position = {bgRect.x + kPadding, bgRect.y + kPadding},
			.scale = kFontScale,
			.color = {1.0F, 1.0F, 0.8F, opacity}, // Warm yellow-white
			.id = "notification_text",
			.zIndex = 2001
		});

		currentY -= kMargin; // Add margin before next notification (stacking upwards)
		count++;
	}
}

void GameUI::toggleTaskList() {
	taskListExpanded = !taskListExpanded;
	if (taskListPanel) {
		taskListPanel->visible = taskListExpanded;
	}
}

// --- Build Mode API ---

void GameUI::setBuildModeActive(bool active) {
	if (buildToolbar) {
		buildToolbar->setActive(active);
	}
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

} // namespace world_sim
