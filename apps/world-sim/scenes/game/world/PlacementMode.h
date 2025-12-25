#pragma once

// PlacementMode - State machine for placing entities in the world.
//
// States:
// - None: Normal gameplay, no placement active
// - MenuOpen: Build menu is displayed, awaiting item selection
// - Placing: Item selected, ghost preview follows cursor, awaiting click to place

#include <math/Types.h>

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
	/// Callback signature for when an entity should be spawned
	using PlaceCallback = std::function<void(const std::string& defName, Foundation::Vec2 worldPos)>;

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

	/// Check if current ghost position is valid for placement
	[[nodiscard]] bool isValidPlacement() const { return m_isValidPlacement; }

	// --- State transitions ---

	/// Open the build menu (transitions None -> MenuOpen)
	void enterMenu();

	/// Select an item to place (transitions MenuOpen -> Placing)
	void selectItem(const std::string& defName);

	/// Cancel placement and return to normal (transitions any -> None)
	void cancel();

	/// Update ghost position from world coordinates
	/// Called each frame while in Placing state
	void updateGhostPosition(Foundation::Vec2 worldPos);

	/// Attempt to place at current ghost position
	/// Returns true if placement succeeded (transitions Placing -> None)
	bool tryPlace();

  private:
	PlacementState m_state = PlacementState::None;
	std::string m_selectedDefName;
	Foundation::Vec2 m_ghostPosition{0.0F, 0.0F};
	bool m_isValidPlacement = true; // Always valid for now
	bool m_skipNextPlacement = false; // Prevents same-frame placement after item selection

	PlaceCallback m_onPlace;
};

} // namespace world_sim
