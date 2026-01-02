#pragma once

// Action Type Definition
// Defines properties for action types loaded from XML configuration.
// Used to determine if actions need free hands (for chain interruption logic).

#include <string>

namespace engine::assets {

	/// Definition of an action type.
	/// Loaded from assets/config/actions/action-types.xml
	struct ActionTypeDef {
		/// Unique identifier (e.g., "Eat", "Pickup", "Sleep")
		std::string defName;

		/// Human-readable description
		std::string description;

		/// Whether this action requires free hands.
		/// If true and colonist is holding an item, they must stow or drop it first.
		bool needsHands = false;
	};

} // namespace engine::assets
