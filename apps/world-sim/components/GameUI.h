#pragma once

// GameUI - Main UI container for the game scene.
//
// Contains all game UI elements as children:
// - GameOverlay: status display, zoom controls
// - EntityInfoPanel: selected entity information
//
// Handles input consumption to prevent click-through to world.

#include "EntityInfoPanel.h"
#include "GameOverlay.h"
#include "Selection.h"

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
		const ecs::World& ecsWorld,
		const engine::assets::AssetRegistry& registry,
		const Selection& selection
	);

	/// Render all UI elements
	void render();

	/// Check if a screen position is within any UI element bounds
	/// @param screenPos Position in logical screen coordinates
	/// @return true if position is over a UI element
	[[nodiscard]] bool isPointOverUI(Foundation::Vec2 screenPos) const;

  private:
	/// Check if a point is within info panel bounds (when visible)
	[[nodiscard]] bool isPointOverInfoPanel(Foundation::Vec2 screenPos) const;

	std::unique_ptr<GameOverlay> overlay;
	std::unique_ptr<EntityInfoPanel> infoPanel;

	// Cached bounds for hit testing
	Foundation::Rect viewportBounds;
	Foundation::Rect infoPanelBounds;

	// Callbacks
	std::function<void()> onSelectionCleared;
};

} // namespace world_sim
