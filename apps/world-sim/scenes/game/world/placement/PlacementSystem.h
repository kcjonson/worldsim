#pragma once

// PlacementSystem - Manages entity placement in the world.
//
// Coordinates placement mode, ghost rendering, and entity spawning.
// Handles both new entity placement (via build menu) and furniture
// relocation (via Place button on packaged items).

#include "GhostRenderer.h"
#include "PlacementMode.h"
#include "PlacementTypes.h"

#include <construction/ItemClearance.h>
#include <ecs/EntityID.h>
#include <ecs/PlacedItemIndex.h>
#include <ecs/systems/PlacedItemSystem.h>
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
	void setConstructionWorld(const construction::ConstructionWorld* world) {
		constructionWorld = world;
		wallRingsVersion = UINT64_MAX;
	}

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
	/// items awaiting colonist delivery (those with targetPosition set), and beside a refused ghost the reason.
	void render(int viewportW, int viewportH, float uiScale);

	// --- State Queries ---

	[[nodiscard]] bool isActive() const { return placementMode.isActive(); }
	[[nodiscard]] PlacementState state() const { return placementMode.state(); }
	[[nodiscard]] bool ghostValid() const { return placementMode.isValidPlacement(); }
	/// Why the ghost can't go where it stands (rotatable-placed-items 14.2); nothing while it can.
	[[nodiscard]] std::optional<construction::PlacementRefusal> ghostRefusal() const { return refusal; }
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
	/// (insideUnbuiltSite), and its footprint, taken in by the flush tolerance (construction::takenIn) so it may stand
	/// flush against an obstacle or a wall, is clear ground out of any ford (NavigationSystem::isAreaBuildableNow).
	/// The footprint is a parts def's collider rings, another def's collision outline, or just `worldPos` when it
	/// has neither. False with no nav system. The ground rule of refusalAt.
	[[nodiscard]] bool footprintBuildableNow(const std::string& defName, Foundation::Vec2 worldPos, uint32_t turnQuanta) const;

	/// Which of the placement rules a check applies: all of them, as the player's ghost and a crafted crate's install
	/// target do, or all but the explored rule, as the dev verbs do (they put things on meshed ground no one has seen).
	enum class PlacementRules : uint8_t { Player, Dev };

	/// Why `defName` can't be set up at `worldPos` turned `turnQuanta` (rotatable-placed-items 14.2), the checks in order:
	/// its body against other items' bodies, installed and pending, then against the walls (construction::bodyRefusal);
	/// its ground (footprintBuildableNow); its footprint seen, when player visibility is on and `rules` is Player; its clearance
	/// (construction::clearanceRefusal). `ignore` (the crate being moved) is left out. Nothing when it can. The ghost's,
	/// the dev spawn's, and a crafted crate's install target's.
	[[nodiscard]] std::optional<construction::PlacementRefusal>
	refusalAt(const std::string& defName, Foundation::Vec2 worldPos, uint32_t turnQuanta, ecs::EntityID ignore, PlacementRules rules) const;

	/// Where a freshly crafted `defName` crate is installed beside the station at `stationPos`, at turn 0: a
	/// spot at least 1 m off it, tried first along `aim` (NavigationSystem::findValidPositionNear),
	/// that refusalAt takes, held in the placed item index as a pending target until its crate is spawned. nullopt
	/// when none does, so the crate waits for the player to place it.
	[[nodiscard]] std::optional<glm::vec2> installTargetNear(const std::string& defName, glm::vec2 stationPos, glm::vec2 aim);

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

	/// What the last pointer move settled the ghost with, the world's revisions among it; a move that changes none
	/// of it settles nothing.
	struct PointerState {
		glm::vec2	  pointer{0.0F, 0.0F};
		bool		  freeform = false;
		bool		  pressed = false;
		std::uint64_t constructionVersion = 0; // walls and foundations
		std::uint64_t navigationGeneration = 0; // ground, water, and placed obstacles in the mesh
		std::uint64_t seenEpoch = 0;			// ground the player has seen
		bool		  revealed = false;
		std::uint64_t placedItemsRevision = 0;	// items installed, moved, turned, targeted, or removed

		bool operator==(const PointerState&) const = default;
	};
	std::optional<PointerState> lastMove;
	/// Why the ghost can't go where it was last settled.
	std::optional<construction::PlacementRefusal> refusal;

	/// The pointer state now, with the world's revisions.
	[[nodiscard]] PointerState pointerState(glm::vec2 pointerMeters, bool freeform) const;

	/// Where the item stands before the flush slide: the pointer, or the press during a drag.
	glm::vec2 rawPosition{0.0F, 0.0F};

	/// The last turn used for each def this game, the ghost's starting turn (rotatable-placed-items 14.1).
	std::unordered_map<std::string, uint32_t> lastTurns;

	/// Sets the ghost at `rawPosition` turned `turnQuanta`: slid flush when a parts item, its validity read there.
	void settleGhost(uint32_t turnQuanta);

	/// `defName`'s outlines at `worldPos` turned `turnQuanta`: a parts def's collider and reserved region, any other
	/// def's collision outline as both (it never turns); empty for a def with neither.
	[[nodiscard]] ecs::ItemOutlines outlinesAt(const std::string& defName, Foundation::Vec2 worldPos, uint32_t turnQuanta) const;

	/// `defName`'s footprint at `worldPos` turned `turnQuanta`: outlinesAt's collider rings, or just `worldPos` when it
	/// has none.
	[[nodiscard]] std::vector<std::vector<glm::vec2>> footprintAt(const std::string& defName, Foundation::Vec2 worldPos, uint32_t turnQuanta) const;

	/// The placed item index, from the world's PlacedItemSystem; null when it has none.
	[[nodiscard]] const ecs::PlacedItemIndex* placedItems() const;

	/// The walls' outlines (construction::wallOutlines), kept until the construction world's version moves.
	[[nodiscard]] const std::vector<std::vector<glm::vec2>>& wallRings() const;
	mutable std::vector<std::vector<glm::vec2>> wallRingsCache;
	mutable std::uint64_t						wallRingsVersion = UINT64_MAX;
	static constexpr float kPixelsPerMeter = 8.0F;
};

} // namespace world_sim
