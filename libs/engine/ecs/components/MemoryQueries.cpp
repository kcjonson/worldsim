#include "MemoryQueries.h"

#include "assets/AssetRegistry.h"

#include <glm/geometric.hpp>

#include <limits>

namespace ecs {

std::vector<KnownWorldEntity> findKnownWithCapability(
	const Memory&						  memory,
	const engine::assets::AssetRegistry&  /*registry*/,
	engine::assets::CapabilityType		  capability) {
	std::vector<KnownWorldEntity> result;

	// Use capability index for O(1) set access instead of iterating all entities
	const auto& entityKeys = memory.getEntitiesWithCapability(capability);
	result.reserve(entityKeys.size());

	for (uint64_t key : entityKeys) {
		const auto* entity = memory.getWorldEntity(key);
		if (entity != nullptr) {
			result.push_back(*entity);
		}
	}

	return result;
}

std::optional<KnownWorldEntity> findNearestWithCapability(
	const Memory&						  memory,
	const engine::assets::AssetRegistry&  /*registry*/,
	engine::assets::CapabilityType		  capability,
	const glm::vec2&					  fromPosition) {
	std::optional<KnownWorldEntity> nearest;
	float							minDistSq = std::numeric_limits<float>::max();

	// Use capability index for O(1) set access
	const auto& entityKeys = memory.getEntitiesWithCapability(capability);

	for (uint64_t key : entityKeys) {
		const auto* entity = memory.getWorldEntity(key);
		if (entity != nullptr) {
			glm::vec2 diff = entity->position - fromPosition;
			float	  distSq = glm::dot(diff, diff); // Squared distance avoids sqrt
			if (distSq < minDistSq) {
				minDistSq = distSq;
				nearest = *entity;
			}
		}
	}

	return nearest;
}

size_t countKnownWithCapability(
	const Memory&						  memory,
	const engine::assets::AssetRegistry&  /*registry*/,
	engine::assets::CapabilityType		  capability) {
	// O(1) count using capability index
	return memory.countWithCapability(capability);
}

} // namespace ecs
