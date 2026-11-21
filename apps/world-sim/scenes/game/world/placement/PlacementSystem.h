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
#include <unordered_map>
#include <vector>

namespace assets {
	class PlacementExecutor;
}

namespace construction {
	class ConstructionWorld;
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
/// - Turning a parts item while placing it: Q/E, and press-drag to face the pointer, with the turn and flush
///   snaps (rotatable-placed-items 14.3)
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
		/// Whether a point lies inside the footprint of a foundation that is not built yet. Empty = none does.
		std::function<bool(glm::vec2)> insideUnbuiltSite{};
	};

	PlacementSystem() = default;
	explicit PlacementSystem(const Args& args);

	/// The walls and foundations a turned item squares to and slides flush against. Null = none.
	void setConstructionWorld(const construction::ConstructionWorld* world) { constructionWorld = world; }

	// --- Build Menu Flow ---

	/// Toggle build menu visibility (B key)
	void toggleBuildMenu();

	/// Select item from build menu for placement, its ghost at the turn last used for it this game.
	void selectBuildItem(const std::string& defName);

	// --- Furniture Relocation Flow ---

	/// Begin relocating an existing packaged entity, its ghost at the entity's own turn.
	void beginRelocation(ecs::EntityID entityId, const std::string& defName);

	// --- Input Handling ---

	/// The pointer moved, in the window's logical px. With a press held on the world the item stays where the
	/// press was and, once the pointer is past the click distance, turns to face it; otherwise the ghost follows
	/// the pointer. `freeform` (Alt) drops the turn snaps.
	void handleMouseMove(float screenX, float screenY, int viewportW, int viewportH, bool freeform);

	/// A left press on the world while placing: fixes the ghost's position for a drag. True when it was taken.
	bool handlePress(float screenX, float screenY, int viewportW, int viewportH);

	/// A left release while placing: places at the ghost's position and turn (a click, or the end of a drag).
	/// True when it placed.
	bool handleRelease();

	/// Q (`direction` -1) or E (+1) while placing a parts item: the next turn stop that way. True when taken.
	bool handleTurnKey(int direction, bool freeform);

	/// Forgets a held press without placing: its release went outside the window.
	void dropPress();

	/// The left button is up this frame: a press whose release never reached the world (the interface took it, or
	/// the window lost focus) is dropped, and so is the record of a press placement was cancelled under.
	void leftButtonUp();

	/// Whether this left release belongs to a press held when placement was cancelled (a right release or Escape
	/// mid-drag): true once, so the release selects nothing.
	bool takeReleaseOfCancelledPress();

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
	[[nodiscard]] const std::string& selectedDefName() const { return placementMode.selectedDefName(); }
	[[nodiscard]] Foundation::Vec2 ghostPosition() const { return placementMode.ghostPosition(); }
	[[nodiscard]] uint32_t ghostTurnQuanta() const { return placementMode.ghostTurnQuanta(); }

	// --- Entity Spawning (public for ActionSystem callback) ---

	/// Spawn a placed entity with appropriate components, turned `turnQuanta` (rotatable-placed-items 3).
	/// Used by placement workflow and by dropItemCallback for crafted items
	ecs::EntityID spawnEntity(const std::string& defName, Foundation::Vec2 worldPos, uint32_t turnQuanta);

	/// Whether `worldPos` lies inside the footprint of a foundation that is not built yet (a blueprint
	/// being cleared, awaiting materials, or under construction). Furniture is never set up there: the
	/// structure would be built around it. A built foundation's floor is fine.
	[[nodiscard]] bool insideUnbuiltSite(Foundation::Vec2 worldPos) const;

	/// Whether `defName` may be set up at `worldPos` turned `turnQuanta`: not on an unbuilt foundation
	/// (insideUnbuiltSite), and its footprint is clear ground out of any ford (NavigationSystem::isAreaBuildableNow).
	/// The footprint is a parts def's collider rings, another def's collision outline, or just `worldPos` when it
	/// has neither. False with no nav system. The placement tool's ground gate, the /api/dev/spawn verb's for
	/// furniture, and a crafted crate's install target's.
	[[nodiscard]] bool footprintBuildableNow(const std::string& defName, Foundation::Vec2 worldPos, uint32_t turnQuanta) const;

	/// Where a freshly crafted `defName` crate is installed beside the station at `stationPos`, at turn 0: a
	/// spot at least 1 m off it, tried first along `aim` (NavigationSystem::findValidPositionNear),
	/// that footprintBuildableNow takes. nullopt when none does, so the crate waits for the player to
	/// place it.
	[[nodiscard]] std::optional<glm::vec2> installTargetNear(const std::string& defName, glm::vec2 stationPos, glm::vec2 aim) const;

	/// Whether `defName` is furniture that is built or installed where it stands: a crafting station
	/// or a storage container, the two kinds spawnEntity sets up and the build menu places.
	[[nodiscard]] static bool isFurniture(const std::string& defName);

	/// Whether `defName` turns when placed: a parts item. Every other def is placed at turn 0.
	[[nodiscard]] static bool turns(const std::string& defName);

  private:
	ecs::World*						   ecsWorld = nullptr;
	world::WorldCamera*		   camera = nullptr;
	assets::PlacementExecutor* placementExecutor = nullptr;
	ecs::NavigationSystem*			   navigation = nullptr;
	uint64_t						   worldSeed = 0;
	Callbacks						   callbacks;
	std::function<bool(glm::vec2)>	   unbuiltSite{};
	const construction::ConstructionWorld* constructionWorld = nullptr;

	PlacementMode placementMode;
	GhostRenderer ghostRenderer;

	/// Entity being relocated (0 = spawning new, non-zero = relocating)
	ecs::EntityID relocatingEntityId{0};

	/// A left press held on the world: where the item stays while a drag turns it, and the press in screen px.
	struct Press {
		glm::vec2 pivot{0.0F, 0.0F};
		glm::vec2 screen{0.0F, 0.0F};
		bool	  turning = false;
	};
	std::optional<Press> press;
	/// Placement was cancelled with a press held; its release is not a click on the world.
	bool cancelledPress = false;

	/// What the last pointer move settled the ghost with; a move that changes none of it settles nothing.
	struct PointerState {
		glm::vec2 pointer{0.0F, 0.0F};
		bool	  freeform = false;
		bool	  pressed = false;
	};
	std::optional<PointerState> lastMove;

	/// Where the item stands before the flush slide: the pointer, or the press during a drag.
	glm::vec2 rawPosition{0.0F, 0.0F};

	/// The last turn used for each def this game, the ghost's starting turn (rotatable-placed-items 14.1).
	std::unordered_map<std::string, uint32_t> lastTurns;

	/// Each parts def's reserved region outlines at turn 0, read once under the registry's generation.
	mutable std::unordered_map<std::string, std::vector<std::vector<glm::vec2>>> reservedOutlines;
	mutable uint64_t															 outlinesGeneration = 0;

	/// `defName`'s reserved region outlines at turn 0 (assets::reservedRegion's outer rings); empty for a def that
	/// isn't parts or has none.
	[[nodiscard]] const std::vector<std::vector<glm::vec2>>& reservedOutlinesOf(const std::string& defName) const;

	/// Sets the ghost at `rawPosition` turned `turnQuanta`: slid flush when a parts item, its validity read there.
	void settleGhost(uint32_t turnQuanta);

	/// The faces near `pivot` the held item slides flush against: wall faces and other items' reserved regions,
	/// installed and pending, within `reachMeters`.
	[[nodiscard]] std::vector<std::vector<glm::vec2>> otherReservedRings(glm::vec2 pivot, float reachMeters) const;

	/// Whether the held item can go at `worldPos` turned `turnQuanta`: footprintBuildableNow, and with the
	/// footprint seen when player visibility is on.
	[[nodiscard]] bool placeable(Foundation::Vec2 worldPos, uint32_t turnQuanta) const;

	static constexpr float kPixelsPerMeter = 8.0F;
};

} // namespace world_sim
