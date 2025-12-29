#pragma once

// SelectionSystem - Manages entity selection in the world.
//
// Handles click-to-select with priority ordering:
// - Priority 1.0: Colonists (highest)
// - Priority 1.5: Crafting stations
// - Priority 1.6: Storage containers
// - Priority 2.0: World entities (placed assets)
//
// Also renders selection indicators in world-space.

#include "SelectionTypes.h"

#include <ecs/World.h>
#include <math/Types.h>
#include <world/camera/WorldCamera.h>

#include <functional>

namespace engine::assets {
class PlacementExecutor;
}

namespace world_sim {

/// Selection priority constants (lower = higher priority)
namespace SelectionPriority {
	constexpr float kColonist = 1.0F;
	constexpr float kCraftingStation = 1.5F;
	constexpr float kStorageContainer = 1.6F;
	constexpr float kWorldEntity = 2.0F;
} // namespace SelectionPriority

/// SelectionSystem - Manages entity selection and rendering.
///
/// Responsibilities:
/// - Click-to-select with priority-based entity selection
/// - Selection state ownership
/// - Selection indicator rendering
class SelectionSystem {
  public:
	struct Callbacks {
		/// Called when selection changes
		std::function<void(const Selection&)> onSelectionChanged;
	};

	struct Args {
		ecs::World*							  world;
		engine::world::WorldCamera*			  camera;
		engine::assets::PlacementExecutor*	  placementExecutor;
		Callbacks							  callbacks;
	};

	SelectionSystem() = default;
	explicit SelectionSystem(const Args& args);

	// --- Selection Operations ---

	/// Handle click to select entity
	/// @param screenPos Mouse position in screen coordinates
	/// @param viewportW Viewport width
	/// @param viewportH Viewport height
	void handleClick(float screenX, float screenY, int viewportW, int viewportH);

	/// Clear current selection
	void clearSelection();

	/// Select a specific colonist (from UI)
	void selectColonist(ecs::EntityID entityId);

	// --- Rendering ---

	/// Render selection indicator (call during render phase)
	void renderIndicator(int viewportW, int viewportH);

	// --- State Queries ---

	[[nodiscard]] const Selection& current() const { return selection; }
	[[nodiscard]] bool hasSelection() const { return world_sim::hasSelection(selection); }

  private:
	ecs::World*							ecsWorld = nullptr;
	engine::world::WorldCamera*			camera = nullptr;
	engine::assets::PlacementExecutor*	placementExecutor = nullptr;
	Callbacks							callbacks;

	Selection selection = NoSelection{};

	static constexpr float kSelectionRadius = 2.0F;		 // meters
	static constexpr float kPixelsPerMeter = 8.0F;
	static constexpr float kIndicatorRadius = 1.0F;		 // meters
};

} // namespace world_sim
