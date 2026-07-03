#include "ResourcesAdapter.h"

#include <assets/AssetRegistry.h>
#include <ecs/World.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/StorageConfiguration.h>
#include <ecs/components/Transform.h>

#include <algorithm>
#include <unordered_map>

namespace world_sim::adapters {

namespace {

std::string displayNameFor(const std::string& defName, const engine::assets::AssetRegistry& registry) {
	const auto* def = registry.getDefinition(defName);
	if (def != nullptr && !def->label.empty()) {
		return def->label;
	}
	return defName;
}

} // namespace

StorageResourcesData getStorageResources(ecs::World& world, const engine::assets::AssetRegistry& registry) {
	StorageResourcesData data;

	std::unordered_map<std::string, uint32_t> totals;
	for (auto [entity, storageConfig, pos, inventory] : world.view<ecs::StorageConfiguration, ecs::Position, ecs::Inventory>()) {
		++data.containerCount;
		for (const auto& stack : inventory.items) {
			totals[stack.defName] += stack.quantity;
		}
	}

	data.rows.reserve(totals.size());
	for (const auto& [defName, count] : totals) {
		data.rows.push_back(ResourceRowData{defName, displayNameFor(defName, registry), count});
	}
	std::sort(data.rows.begin(), data.rows.end(), [](const ResourceRowData& a, const ResourceRowData& b) {
		if (a.count != b.count) {
			return a.count > b.count;
		}
		return a.displayName < b.displayName;
	});

	return data;
}

} // namespace world_sim::adapters
