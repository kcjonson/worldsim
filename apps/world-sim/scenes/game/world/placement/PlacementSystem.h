#pragma once

// PlacementSystem - Manages entity placement in the world.
//
// Coordinates placement mode, ghost rendering, and entity spawning.
// Handles both new entity placement (via build menu) and furniture
// relocation (via Place button on packaged items).

#include "GhostRenderer.h"
#include "PlacementMode.h"
#include "PlacementTypes.h"

#include <ecs/EntityID.h>
#include <ecs/World.h>
#include <math/Types.h>
#include <world/camera/WorldCamera.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace assets {
	class PlacementExecutor;
}

namespace ecs {
	class NavigationSystem;
}

namespace world_sim {

/// PlacementSystem - Coordinates entity placement workflow.
///
/// Responsibilities:
/// - Build menu flow (B key → select item → place)
/// - Furniture relocation (Place button → select position → place)
/// - Ghost preview rendering
/// - Entity spawning with proper components
class PlacementSystem {
  public:
	struct Callbacks {
		/// Called to show/hide the build menu
		std::function<void(bool)> onBuildMenuVisibility;
		/// Called to show build menu with items
		std::function<void(const std::vector<BuildMenuItem>&)> onShowBuildMenu;
		/// Called to hide build menu
		std::function<void()> onHideBuildMenu;
		/// Called when selection should be cleared (after placing)
		std::function<void()> onSelectionCleared;
	};

	struct Args {
		ecs::World*						 world;
		world::WorldCamera*		 camera;
		assets::PlacementExecutor* placementExecutor; // owns harvestable resource pools
		ecs::NavigationSystem*			 navigation;		  // where furniture may go (isAreaBuildableNow)
		uint64_t						 worldSeed;		  // for the traits of a spawned entity (computeTreeTraits)
		Callbacks						 callbacks;
	};

	PlacementSystem() = default;
	explicit PlacementSystem(const Args& args);

	// --- Build Menu Flow ---

	/// Toggle build menu visibility (B key)
	void toggleBuildMenu();

	/// Select item from build menu for placement
	void selectBuildItem(const std::string& defName);

	// --- Furniture Relocation Flow ---

	/// Begin relocating an existing packaged entity
	void beginRelocation(ecs::EntityID entityId, const std::string& defName);

	// --- Input Handling ---

	/// Update ghost position from mouse movement
	/// @param screenPos Mouse position in screen coordinates
	/// @param viewportW Viewport width
	/// @param viewportH Viewport height
	void handleMouseMove(float screenX, float screenY, int viewportW, int viewportH);

	/// Handle click to place entity
	/// @return true if placement occurred
	bool handleClick();

	/// Cancel placement mode (Escape key)
	void cancel();

	// --- Rendering ---

	/// Render ghost preview (call during render phase)
	/// Renders both the active placement ghost AND ghosts for all packaged
	/// items awaiting colonist delivery (those with targetPosition set).
	void render(int viewportW, int viewportH);

	// --- State Queries ---

	[[nodiscard]] bool isActive() const { return placementMode.isActive(); }
	[[nodiscard]] PlacementState state() const { return placementMode.state(); }
	[[nodiscard]] bool ghostValid() const { return placementMode.isValidPlacement(); }

	// --- Entity Spawning (public for ActionSystem callback) ---

	/// Spawn a placed entity with appropriate components
	/// Used by placement workflow and by dropItemCallback for crafted items
	ecs::EntityID spawnEntity(const std::string& defName, Foundation::Vec2 worldPos);

	/// Whether `defName` may be set up at `worldPos`: its footprint is clear ground out of any ford
	/// (NavigationSystem::isAreaBuildableNow). The footprint is its collision outline there, or just
	/// `worldPos` when it declares no rect or polygon outline. False with no nav system. The
	/// placement tool's ground gate, the /api/dev/spawn verb's for furniture, and a crafted crate's
	/// install target's.
	[[nodiscard]] bool footprintBuildableNow(const std::string& defName, Foundation::Vec2 worldPos) const;

	/// Where a freshly crafted `defName` crate is installed beside the station at `stationPos`: a
	/// spot at least 1 m off it, tried first along `aim` (NavigationSystem::findValidPositionNear),
	/// that footprintBuildableNow takes. nullopt when none does, so the crate waits for the player to
	/// place it.
	[[nodiscard]] std::optional<glm::vec2> installTargetNear(const std::string& defName, glm::vec2 stationPos, glm::vec2 aim) const;

	/// Whether `defName` is furniture that is built or installed where it stands: a crafting station
	/// or a storage container, the two kinds spawnEntity sets up and the build menu places.
	[[nodiscard]] static bool isFurniture(const std::string& defName);

  private:
	ecs::World*						   ecsWorld = nullptr;
	world::WorldCamera*		   camera = nullptr;
	assets::PlacementExecutor* placementExecutor = nullptr;
	ecs::NavigationSystem*			   navigation = nullptr;
	uint64_t						   worldSeed = 0;
	Callbacks						   callbacks;

	PlacementMode placementMode;
	GhostRenderer ghostRenderer;

	/// Entity being relocated (0 = spawning new, non-zero = relocating)
	ecs::EntityID relocatingEntityId{0};

	/// Whether the held item can go at `worldPos`: footprintBuildableNow, and with the footprint seen
	/// when player visibility is on.
	[[nodiscard]] bool placeable(Foundation::Vec2 worldPos) const;

	static constexpr float kPixelsPerMeter = 8.0F;
};

} // namespace world_sim
