#pragma once

// ResourcesAdapter - Query layer for the storage resources panel
//
// Walks the same container set the dev API's /api/state?what=storage
// serializer does (entities with StorageConfiguration + Position + Inventory)
// and aggregates their contents into display-ready rows.

#include <cstdint>
#include <string>
#include <vector>

namespace ecs {
	class World;
}

namespace engine::assets {
	class AssetRegistry;
}

namespace world_sim::adapters {

/// One aggregated resource row (summed across all storage containers)
struct ResourceRowData {
	std::string defName;
	std::string displayName;
	uint32_t	count = 0;

	bool operator==(const ResourceRowData&) const = default;
};

/// Colony storage contents for the resources panel
struct StorageResourcesData {
	std::vector<ResourceRowData> rows;			 // Sorted by count descending
	size_t						 containerCount = 0; // 0 = no stockpiles built (empty state)
};

/// Aggregate the contents of every storage container into display rows
[[nodiscard]] StorageResourcesData getStorageResources(ecs::World& world, const engine::assets::AssetRegistry& registry);

} // namespace world_sim::adapters
