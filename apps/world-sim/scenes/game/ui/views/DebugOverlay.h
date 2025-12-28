#pragma once

// DebugOverlay - Debug information display for development.
//
// Shows:
// - Chunks loaded count
// - Camera position and current chunk
// - Current biome
//
// Positioned in top-left corner, always visible during gameplay.

#include <graphics/Rect.h>
#include <shapes/Shapes.h>
#include <world/camera/WorldCamera.h>
#include <world/chunk/ChunkManager.h>

#include <memory>
#include <string>

namespace world_sim {

/// Debug overlay showing development information.
class DebugOverlay {
  public:
	struct Args {
		std::string id = "debug_overlay";
	};

	explicit DebugOverlay(const Args& args);

	/// Position elements within the given bounds (call on viewport resize)
	void layout(const Foundation::Rect& viewportBounds);

	/// Update displayed values from camera and chunk manager
	void update(const engine::world::WorldCamera& camera, const engine::world::ChunkManager& chunkManager);

	/// Render the overlay
	void render();

  private:
	std::unique_ptr<UI::Text> chunksText;
	std::unique_ptr<UI::Text> positionText;
	std::unique_ptr<UI::Text> biomeText;

	void createElements();
};

}  // namespace world_sim
