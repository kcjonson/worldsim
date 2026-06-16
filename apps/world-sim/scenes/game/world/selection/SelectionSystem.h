#pragma once

// SelectionSystem - Manages entity selection in the world.
//
// Handles click-to-select with priority ordering:
// - Priority 1.0: Colonists (highest)
// - Priority 1.5: Crafting stations
// - Priority 1.6: Storage containers
// - Priority 2.0: World entities (placed assets)
// - Priority 2.3: Openings (above the wall they sit on, below entities/furniture)
// - Priority 2.5: Wall segments (above foundations, below entities/furniture)
// - Priority 3.0: Foundations (lowest; below everything that can stand on them)
//
// Also renders selection indicators in world-space.

#include "SelectionTypes.h"

#include <ecs/World.h>
#include <math/Types.h>
#include <world/camera/WorldCamera.h>

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace engine::assets {
class PlacementExecutor;
}

namespace engine::construction {
class ConstructionWorld;
}

namespace world_sim {

/// Selection priority constants (lower = higher priority)
namespace SelectionPriority {
	constexpr float kColonist = 1.0F;
	constexpr float kCraftingStation = 1.5F;
	constexpr float kStorageContainer = 1.6F;
	constexpr float kWorldEntity = 2.0F;
	constexpr float kOpening = 2.3F;	 // above the wall it sits on, below entities/furniture
	constexpr float kWallSegment = 2.5F; // above the host foundation, below entities/furniture
	constexpr float kFoundation = 3.0F;	 // below everything that sits on a foundation
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
		ecs::World*								 world;
		engine::world::WorldCamera*				 camera;
		engine::assets::PlacementExecutor*		 placementExecutor;
		engine::construction::ConstructionWorld* constructionWorld;
		Callbacks								 callbacks;
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

	/// Set selection to an arbitrary state and fire onSelectionChanged. The room
	/// overlay routes its hit-test result through here so the room's selection
	/// flows through the same single sink (current()) every consumer already reads.
	void setSelection(const Selection& newSelection);

	// --- Rendering ---

	/// Render selection indicator (call during render phase)
	void renderIndicator(int viewportW, int viewportH);

	// --- State Queries ---

	[[nodiscard]] const Selection& current() const { return selection; }
	[[nodiscard]] bool hasSelection() const { return world_sim::hasSelection(selection); }

  private:
	/// Stable identity for a candidate: (variant type index, entity/structure id).
	/// Used to detect "the same stack is under the cursor" between clicks so repeated
	/// same-spot clicks cycle through it. NoSelection has no key (never a candidate).
	using CandidateKey = std::pair<int, std::uint64_t>;

	/// Gather every entity under the world position, one per priority level, ordered
	/// highest-priority-first. Runs the same hit-tests as a single-pick click but
	/// appends each level's best hit instead of returning on the first. Re-gathered
	/// every click; never caches entity pointers across clicks.
	[[nodiscard]] std::vector<Selection> gatherCandidates(glm::vec2 worldPos);

	ecs::World*								 ecsWorld = nullptr;
	engine::world::WorldCamera*				 camera = nullptr;
	engine::assets::PlacementExecutor*		 placementExecutor = nullptr;
	engine::construction::ConstructionWorld* constructionWorld = nullptr;
	Callbacks								 callbacks;

	Selection selection = NoSelection{};

	// --- Click-cycling state ---
	// Repeated clicks at (approximately) the same screen spot cycle the selection
	// through every candidate under the cursor. A click elsewhere, or a changed
	// candidate set, resets to the top-priority hit. Reset on any non-click
	// selection path so a programmatic select doesn't leave stale cycle state.
	bool					  hasLastClick = false;
	glm::vec2				  lastClickScreen{0.0F, 0.0F};
	std::vector<CandidateKey> lastCandidateKeys;
	std::size_t				  cycleIndex = 0;

	/// Forget any in-progress click cycle. Call from every selection path that is
	/// not handleClick so the next same-spot click starts fresh.
	void resetCycleState();

	static constexpr float kSelectionRadius = 2.0F;		 // meters
	static constexpr float kPixelsPerMeter = 8.0F;
	static constexpr float kIndicatorRadius = 1.0F;		 // meters

	// Pick slop for thin wall segments: a thin wall's half-thickness is a small
	// target, so a click within this radius of the centerline still hits. mm,
	// max'd against the segment's half-thickness so a thick wall uses its own face.
	static constexpr std::int64_t kWallPickSlopMm = 300; // 0.3 m, matches edge-snap slop

	// Two clicks within this screen-space distance (logical px) count as the "same
	// spot" and advance the cycle rather than starting a fresh selection.
	static constexpr float kClickCycleTolerancePx = 6.0F;
};

} // namespace world_sim
