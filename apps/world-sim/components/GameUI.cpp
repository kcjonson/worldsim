#include "GameUI.h"

#include <input/InputManager.h>

namespace world_sim {

namespace {
	// Layout constants
	constexpr float kPanelWidth = 180.0F;
	constexpr float kPanelPadding = 10.0F;
	constexpr float kPanelHeight = 148.0F;
} // namespace

GameUI::GameUI(const Args& args)
	: onSelectionCleared(args.onSelectionCleared) {

	// Create overlay with zoom callbacks
	overlay = std::make_unique<GameOverlay>(GameOverlay::Args{
		.onZoomIn = args.onZoomIn,
		.onZoomOut = args.onZoomOut,
	});

	// Create info panel (position set in layout())
	// Initial position at (0,0) - will be updated in layout()
	infoPanel = std::make_unique<EntityInfoPanel>(EntityInfoPanel::Args{
		.position = {0.0F, 0.0F},
		.width = kPanelWidth,
		.id = "entity_panel",
		.onClose = [this]() {
			if (onSelectionCleared) {
				onSelectionCleared();
			}
		}});
}

void GameUI::layout(const Foundation::Rect& newBounds) {
	viewportBounds = newBounds;

	// Layout overlay to full viewport
	overlay->layout(newBounds);

	// Position info panel in bottom-left corner (flush with edges)
	float panelX = 0.0F;
	float panelY = newBounds.height - kPanelHeight - kPanelPadding;

	// Cache panel bounds for hit testing
	infoPanelBounds = Foundation::Rect{panelX, panelY, kPanelWidth, kPanelHeight};

	// Update panel position with bottom-left alignment (panel computes Y from viewport height)
	if (infoPanel) {
		infoPanel->setBottomLeftPosition(panelX, newBounds.height);
	}
}

bool GameUI::handleInput() {
	auto& input = engine::InputManager::Get();

	// Handle overlay input first (zoom buttons)
	overlay->handleInput();

	// Check if click is over info panel (when visible)
	if (input.isMouseButtonReleased(engine::MouseButton::Left)) {
		auto mousePos = input.getMousePosition();

		if (isPointOverInfoPanel(Foundation::Vec2{mousePos.x, mousePos.y})) {
			// Click is over info panel - consume it
			return true;
		}
	}

	return false;
}

void GameUI::update(
	const engine::world::WorldCamera& camera,
	const engine::world::ChunkManager& chunkManager,
	const ecs::World& ecsWorld,
	const engine::assets::AssetRegistry& registry,
	const Selection& selection
) {
	// Update overlay display values
	overlay->update(camera, chunkManager);

	// Update info panel with selection
	if (infoPanel) {
		infoPanel->update(ecsWorld, registry, selection);
	}
}

void GameUI::render() {
	// Render overlay
	overlay->render();

	// Render info panel if visible
	if (infoPanel && infoPanel->isVisible()) {
		infoPanel->render();
	}
}

bool GameUI::isPointOverUI(Foundation::Vec2 screenPos) const {
	// QUICKFIX: Check overlay elements (zoom control)
	// This manual delegation should be replaced by the InputEvent consumption system.
	// See /docs/technical/ui-framework/event-system.md
	if (overlay && overlay->isPointOverUI(screenPos)) {
		return true;
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

} // namespace world_sim
