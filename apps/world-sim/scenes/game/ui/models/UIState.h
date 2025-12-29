#pragma once

// UIState - Shared UI state across all panels in the game scene
//
// This struct centralizes selection state that was previously scattered
// across GameScene and individual panels. All panels read from UIState
// rather than passing Selection through multiple layers.
//
// Future extensions:
// - multiSelection: For box-select of multiple entities
// - hoveredEntity: For tooltips and hover highlighting

#include "scenes/game/world/selection/SelectionTypes.h"

#include <optional>
#include <set>

namespace world_sim {

	/// Shared UI state for the game scene
	struct UIState {
		/// Current selection (single entity or none)
		Selection selection = NoSelection{};

		/// Multi-selection for future box-select feature
		/// When populated, overrides single selection for batch operations
		std::set<ecs::EntityID> multiSelection;

		/// Currently hovered entity (for tooltips, highlighting)
		std::optional<ecs::EntityID> hoveredEntity;

		// -------------------------------------------------------------------------
		// Convenience accessors
		// -------------------------------------------------------------------------

		/// Check if anything is selected
		[[nodiscard]] bool hasSelection() const { return world_sim::hasSelection(selection) || !multiSelection.empty(); }

		/// Get the single selected colonist ID, if any
		[[nodiscard]] std::optional<ecs::EntityID> selectedColonistId() const {
			if (auto* colonist = std::get_if<ColonistSelection>(&selection)) {
				return colonist->entityId;
			}
			return std::nullopt;
		}

		/// Clear all selection state
		void clearSelection() {
			selection = NoSelection{};
			multiSelection.clear();
		}
	};

} // namespace world_sim
