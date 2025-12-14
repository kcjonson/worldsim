#include "GameUI.h"

#include <input/InputManager.h>

namespace world_sim {

namespace {
	// Layout constants
	constexpr float kPanelWidth = 180.0F;
	constexpr float kTaskListWidth = 360.0F; // 2x info panel
	constexpr float kPanelPadding = 10.0F;
	constexpr float kPanelHeight = 148.0F;
	constexpr float kTaskListMaxHeight = 400.0F;
} // namespace

GameUI::GameUI(const Args& args)
	: onSelectionCleared(args.onSelectionCleared) {

	// Create overlay with zoom callbacks
	overlay = std::make_unique<GameOverlay>(GameOverlay::Args{
		.onZoomIn = args.onZoomIn,
		.onZoomOut = args.onZoomOut,
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
		.onTaskListToggle = [this]() { toggleTaskList(); }});

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

	// Position colonist list on left side, below overlay and zoom controls
	if (colonistList) {
		colonistList->setPosition(0.0F, 130.0F);
	}

	// Position info panel in bottom-left corner (flush with edges)
	float panelX = 0.0F;
	float panelY = newBounds.height - kPanelHeight - kPanelPadding;

	// Cache panel bounds for hit testing
	infoPanelBounds = Foundation::Rect{panelX, panelY, kPanelWidth, kPanelHeight};

	// Update panel position with bottom-left alignment (panel computes Y from viewport height)
	if (infoPanel) {
		infoPanel->setBottomLeftPosition(panelX, newBounds.height);
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

bool GameUI::handleInput() {
	auto& input = engine::InputManager::Get();

	// Handle overlay input first (zoom buttons)
	overlay->handleInput();

	// Handle colonist list input
	if (colonistList && colonistList->handleInput()) {
		return true;
	}

	// Check if click is over UI elements
	if (input.isMouseButtonReleased(engine::MouseButton::Left)) {
		auto mousePos = input.getMousePosition();
		auto pos = Foundation::Vec2{mousePos.x, mousePos.y};

		// Check task list panel first (it's on top)
		if (taskListExpanded && taskListPanel && taskListPanel->visible) {
			if (pos.x >= taskListPanelBounds.x && pos.x <= taskListPanelBounds.x + taskListPanelBounds.width &&
				pos.y >= taskListPanelBounds.y && pos.y <= taskListPanelBounds.y + taskListPanelBounds.height) {
				return true;
			}
		}

		if (isPointOverInfoPanel(pos)) {
			// Click is over info panel - consume it
			return true;
		}
	}

	return false;
}

void GameUI::update(
	const engine::world::WorldCamera& camera,
	const engine::world::ChunkManager& chunkManager,
	ecs::World& ecsWorld,
	const engine::assets::AssetRegistry& registry,
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
		infoPanel->update(ecsWorld, registry, selection);
	}

	// Update task list panel if expanded
	if (taskListExpanded && taskListPanel && selectedColonistId != 0) {
		taskListPanel->update(ecsWorld, selectedColonistId);
	}
}

void GameUI::render() {
	// Render overlay
	overlay->render();

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

bool GameUI::isPointOverUI(Foundation::Vec2 screenPos) const {
	// QUICKFIX: Check overlay elements (zoom control)
	// This manual delegation should be replaced by the InputEvent consumption system.
	// See /docs/technical/ui-framework/event-system.md
	if (overlay && overlay->isPointOverUI(screenPos)) {
		return true;
	}

	// Check colonist list bounds
	if (colonistList) {
		Foundation::Rect bounds = colonistList->getBounds();
		if (screenPos.x >= bounds.x && screenPos.x <= bounds.x + bounds.width &&
			screenPos.y >= bounds.y && screenPos.y <= bounds.y + bounds.height) {
			return true;
		}
	}

	// Check task list panel (if expanded)
	if (taskListExpanded && taskListPanel && taskListPanel->visible) {
		if (screenPos.x >= taskListPanelBounds.x && screenPos.x <= taskListPanelBounds.x + taskListPanelBounds.width &&
			screenPos.y >= taskListPanelBounds.y && screenPos.y <= taskListPanelBounds.y + taskListPanelBounds.height) {
			return true;
		}
	}

	return isPointOverInfoPanel(screenPos);
}

bool GameUI::isPointOverInfoPanel(Foundation::Vec2 screenPos) const {
	if (!infoPanel || !infoPanel->isVisible()) {
		return false;
	}
	return screenPos.x >= infoPanelBounds.x && screenPos.x <= infoPanelBounds.x + infoPanelBounds.width &&
		   screenPos.y >= infoPanelBounds.y && screenPos.y <= infoPanelBounds.y + infoPanelBounds.height;
}

void GameUI::toggleTaskList() {
	taskListExpanded = !taskListExpanded;
	if (taskListPanel) {
		taskListPanel->visible = taskListExpanded;
	}
}

} // namespace world_sim
