#pragma once

// DebugOverlay - Debug information display for development.
//
// Shows:
// - Chunks loaded count
// - Camera position and current chunk
// - Current biome
//
// Positioned in bottom-left corner, always visible during gameplay.
// Extends UI::Component to use the Layer system for child management.

#include <component/Component.h>
#include <graphics/Rect.h>
#include <layer/Layer.h>
#include <shapes/Shapes.h>
#include <world/camera/WorldCamera.h>
#include <world/chunk/ChunkManager.h>

#include <string>

namespace world_sim {

/// Debug overlay showing development information.
class DebugOverlay : public UI::Component {
  public:
	struct Args {
		std::string id = "debug_overlay";
	};

	explicit DebugOverlay(const Args& args);

	/// Position elements within the given bounds (call on viewport resize)
	void layout(const Foundation::Rect& bounds) override;

	/// Update displayed values from camera and chunk manager
	void updateData(const engine::world::WorldCamera& camera, const engine::world::ChunkManager& chunkManager);

	// render() inherited from Component - auto-renders children

  private:
	UI::LayerHandle chunksTextHandle;
	UI::LayerHandle positionTextHandle;
	UI::LayerHandle biomeTextHandle;

	// Layout constants
	static constexpr float kLineSpacing = 20.0F;
	static constexpr float kPadding = 10.0F;
};

}  // namespace world_sim
