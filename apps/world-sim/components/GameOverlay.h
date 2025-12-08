#pragma once

// GameOverlay - Status display overlay for the game scene.
// Shows camera position, chunk count, zoom control, and hints.

#include "ZoomControl.h"

#include <graphics/Rect.h>
#include <shapes/Shapes.h>
#include <world/camera/WorldCamera.h>
#include <world/chunk/ChunkManager.h>

#include <functional>
#include <memory>
#include <string>

namespace world_sim {

/// Overlay displaying game status information.
class GameOverlay {
  public:
	struct Args {
		std::function<void()> onZoomIn = nullptr;
		std::function<void()> onZoomOut = nullptr;
		std::string id = "game_overlay";
	};

	explicit GameOverlay(const Args& args);

	/// Position elements within the given bounds (call on viewport resize)
	void layout(const Foundation::Rect& viewportBounds);

	/// Update displayed values
	void update(const engine::world::WorldCamera& camera, const engine::world::ChunkManager& chunkManager);

	/// Handle input for interactive elements
	void handleInput();

	/// Render the overlay
	void render();

	/// Check if a point is over any interactive overlay elements
	/// QUICKFIX: This is a temporary solution until the UI event system is implemented.
	/// See /docs/technical/ui-framework/event-system.md for the proper design.
	/// Remove this method when InputEvent consumption is implemented.
	[[nodiscard]] bool isPointOverUI(Foundation::Vec2 screenPos) const;

  private:
	std::unique_ptr<UI::Text> chunksText;
	std::unique_ptr<UI::Text> positionText;
	std::unique_ptr<UI::Text> biomeText;
	std::unique_ptr<ZoomControl> zoomControl;
	Foundation::Rect viewportBounds;

	std::function<void()> onZoomIn;
	std::function<void()> onZoomOut;

	void createElements();
};

}  // namespace world_sim
