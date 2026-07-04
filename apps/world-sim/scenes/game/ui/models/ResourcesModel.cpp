#include "ResourcesModel.h"

namespace world_sim {

bool ResourcesModel::refresh(ecs::World& world, const engine::assets::AssetRegistry& registry, float deltaTime) {
	timeSinceRefresh += deltaTime;
	if (!isFirstRefresh && timeSinceRefresh < kRefreshInterval) {
		return false;
	}
	timeSinceRefresh = 0.0F;

	auto newData = adapters::getStorageResources(world, registry);

	if (isFirstRefresh) {
		isFirstRefresh = false;
		data = std::move(newData);
		return true;
	}

	if (newData.rows != data.rows || newData.containerCount != data.containerCount) {
		data = std::move(newData);
		return true;
	}

	return false;
}

} // namespace world_sim
