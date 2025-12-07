#include "MemoryQueries.h"

#include "assets/AssetRegistry.h"

#include <glm/geometric.hpp>

#include <limits>

namespace ecs {

std::vector<KnownWorldEntity> findKnownWithCapability(
	const Memory&						  memory,
	const engine::assets::AssetRegistry&  registry,
	engine::assets::CapabilityType		  capability) {
	std::vector<KnownWorldEntity> result;

	for (const auto& [key, entity] : memory.knownWorldEntities) {
		const auto* def = registry.getDefinition(entity.defName);
		if (def != nullptr && def->capabilities.has(capability)) {
			result.push_back(entity);
		}
	}

	return result;
}

std::optional<KnownWorldEntity> findNearestWithCapability(
	const Memory&						  memory,
	const engine::assets::AssetRegistry&  registry,
	engine::assets::CapabilityType		  capability,
	const glm::vec2&					  fromPosition) {
	std::optional<KnownWorldEntity> nearest;
	float							minDistSq = std::numeric_limits<float>::max();

	for (const auto& [key, entity] : memory.knownWorldEntities) {
		const auto* def = registry.getDefinition(entity.defName);
		if (def != nullptr && def->capabilities.has(capability)) {
			glm::vec2 diff = entity.position - fromPosition;
			float	  distSq = glm::dot(diff, diff); // Squared distance avoids sqrt
			if (distSq < minDistSq) {
				minDistSq = distSq;
				nearest = entity;
			}
		}
	}

	return nearest;
}

size_t countKnownWithCapability(
	const Memory&						  memory,
	const engine::assets::AssetRegistry&  registry,
	engine::assets::CapabilityType		  capability) {
	size_t count = 0;

	for (const auto& [key, entity] : memory.knownWorldEntities) {
		const auto* def = registry.getDefinition(entity.defName);
		if (def != nullptr && def->capabilities.has(capability)) {
			++count;
		}
	}

	return count;
}

} // namespace ecs
