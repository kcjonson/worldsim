#pragma once

// ResourcesModel - ViewModel for the storage resources panel
//
// Caches aggregated storage contents from ResourcesAdapter, throttles
// refresh to 2Hz, and detects changes so the panel only rebuilds its
// rows when the data actually changed.

#include "scenes/game/ui/adapters/ResourcesAdapter.h"

namespace world_sim {

class ResourcesModel {
  public:
	/// Refresh data from the ECS world (throttled to 2Hz)
	/// @return true if data changed since last refresh
	bool refresh(ecs::World& world, const engine::assets::AssetRegistry& registry, float deltaTime);

	/// Aggregated rows, sorted by count descending
	[[nodiscard]] const std::vector<adapters::ResourceRowData>& rows() const { return data.rows; }

	/// Storage containers found (0 = show empty state)
	[[nodiscard]] size_t containerCount() const { return data.containerCount; }

  private:
	adapters::StorageResourcesData data;

	/// Throttle timer (accumulates until kRefreshInterval)
	float timeSinceRefresh = 0.0F;

	/// Refresh interval in seconds (2Hz = 0.5s)
	static constexpr float kRefreshInterval = 0.5F;

	/// Track first refresh (always return true)
	bool isFirstRefresh = true;
};

} // namespace world_sim
