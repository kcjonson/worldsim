#pragma once

// ColonistListModel - ViewModel for the colonist list panel
//
// This model:
// - Caches colonist data from the ECS world
// - Detects changes between frames to avoid unnecessary UI rebuilds
// - Owns UI-only state (selected ID) that doesn't belong in ECS
//
// Usage:
//   ColonistListModel model;
//   if (model.refresh(world)) {
//       // Data changed, rebuild UI
//       rebuildUI(model.colonists());
//   }

#include "scenes/game/ui/adapters/ColonistAdapter.h"

#include <ecs/EntityID.h>
#include <ecs/World.h>

#include <vector>

namespace world_sim {

class ColonistListModel {
  public:
	using ColonistData = adapters::ColonistData;

	/// Refresh data from ECS world
	/// @return true if data changed since last refresh
	bool refresh(ecs::World& world);

	/// Get the cached colonist data
	[[nodiscard]] const std::vector<ColonistData>& colonists() const { return colonistsData; }

	/// Get the currently selected colonist ID
	[[nodiscard]] ecs::EntityID selectedId() const { return selectedIdValue; }

	/// Set the selected colonist ID (UI-only state)
	void setSelectedId(ecs::EntityID id) { selectedIdValue = id; }

	/// Check if a colonist is currently selected
	[[nodiscard]] bool hasSelectedColonist() const { return selectedIdValue != ecs::EntityID{0}; }

  private:
	/// Compare new data with cached data
	[[nodiscard]] bool hasChanged(const std::vector<ColonistData>& newData) const;

	/// Cached colonist data from last refresh
	std::vector<ColonistData> colonistsData;

	/// Currently selected colonist (UI-only state, not stored in ECS)
	ecs::EntityID selectedIdValue{0};

	/// Track if this is the first refresh (always return true on first call)
	bool isFirstRefresh = true;
};

} // namespace world_sim
