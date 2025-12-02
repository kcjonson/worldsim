#pragma once

// GameOverlay - Status display overlay for the game scene.
// Shows camera position, chunk count, and control hints.

#include <shapes/Shapes.h>
#include <world/camera/WorldCamera.h>
#include <world/chunk/ChunkManager.h>

#include <memory>
#include <string>

namespace world_sim {

/// Overlay displaying game status information.
class GameOverlay {
  public:
	struct Args {
		std::string id = "game_overlay";
	};

	explicit GameOverlay(const Args& args);

	/// Update displayed values
	void update(const engine::world::WorldCamera& camera, const engine::world::ChunkManager& chunkManager);

	/// Render the overlay
	void render();

  private:
	std::unique_ptr<UI::Text> m_chunksText;
	std::unique_ptr<UI::Text> m_positionText;
	std::unique_ptr<UI::Text> m_controlsText;
	int m_viewportHeight = 0;

	void createTextElements();
};

}  // namespace world_sim
