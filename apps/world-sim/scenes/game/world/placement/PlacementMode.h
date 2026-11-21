#pragma once

// PlacementMode - State machine for placing entities in the world.
//
// States:
// - None: Normal gameplay, no placement active
// - MenuOpen: Build menu is displayed, awaiting item selection
// - Placing: Item selected, ghost preview follows cursor, awaiting click to place

#include <math/Types.h>

#include <cstdint>
#include <functional>
#include <string>

namespace world_sim {

/// Placement state machine states
enum class PlacementState {
	None,	  ///< Normal gameplay
	MenuOpen, ///< Build menu is open
	Placing	  ///< Placing an item in the world
};

/// State machine for world entity placement.
/// Manages the flow: None -> MenuOpen -> Placing -> None
class PlacementMode {
  public:
	/// Called to place `defName` at `worldPos`, turned `turnQuanta` (rotatable-placed-items 3).
	using PlaceCallback = std::function<void(const std::string& defName, Foundation::Vec2 worldPos, uint32_t turnQuanta)>;

	struct Args {
		PlaceCallback onPlace;
	};

	PlacementMode() = default;
	explicit PlacementMode(const Args& args);

	/// Get current state
	[[nodiscard]] PlacementState state() const { return m_state; }

	/// Check if in any active placement state
	[[nodiscard]] bool isActive() const { return m_state != PlacementState::None; }

	/// Get currently selected item definition name (empty if none)
	[[nodiscard]] const std::string& selectedDefName() const { return m_selectedDefName; }

	/// Get current ghost position in world coordinates
	[[nodiscard]] Foundation::Vec2 ghostPosition() const { return m_ghostPosition; }

	/// The ghost's turn in quanta: the turn the item is placed at.
	[[nodiscard]] uint32_t ghostTurnQuanta() const { return m_ghostTurnQuanta; }

	/// Check if current ghost position is valid for placement
	[[nodiscard]] bool isValidPlacement() const { return m_isValidPlacement; }

	// --- State transitions ---

	/// Open the build menu (transitions None -> MenuOpen)
	void enterMenu();

	/// Select an item to place, its ghost starting at `turnQuanta` (transitions MenuOpen -> Placing)
	void selectItem(const std::string& defName, uint32_t turnQuanta);

	/// Cancel placement and return to normal (transitions any -> None)
	void cancel();

	/// Update the ghost's position, turn, and whether it is placeable there. `valid` comes from
	/// PlacementSystem's checks (its ground, the footprint seen, and other items' bodies and clearance and the
	/// walls, rotatable-placed-items 14.2); PlacementMode itself owns no terrain knowledge. Called each frame while
	/// Placing.
	void updateGhost(Foundation::Vec2 worldPos, uint32_t turnQuanta, bool valid);

	/// Attempt to place at current ghost position
	/// Returns true if placement succeeded (transitions Placing -> None)
	bool tryPlace();

  private:
	PlacementState	 m_state = PlacementState::None;
	std::string		 m_selectedDefName;
	Foundation::Vec2 m_ghostPosition{0.0F, 0.0F};
	uint32_t		 m_ghostTurnQuanta = 0;
	bool			 m_isValidPlacement = false; // set each frame from the nav-mesh validity gate

	PlaceCallback m_onPlace;
};

} // namespace world_sim
