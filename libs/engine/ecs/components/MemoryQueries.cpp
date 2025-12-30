#include "MemoryQueries.h"

#include "assets/AssetRegistry.h"

#include <glm/geometric.hpp>
#include <utils/Log.h>

#include <cmath>
#include <limits>

namespace ecs {

	std::vector<KnownWorldEntity> findKnownWithCapability(
		const Memory& memory,
		const engine::assets::AssetRegistry& /*registry*/,
		engine::assets::CapabilityType capability
	) {
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
		const Memory& memory,
		const engine::assets::AssetRegistry& /*registry*/,
		engine::assets::CapabilityType capability,
		const glm::vec2&			   fromPosition
	) {
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

	std::optional<KnownWorldEntity> findOptimalForTrip(
		const Memory&										memory,
		const glm::vec2&									fromPosition,
		const glm::vec2&									destination,
		const std::function<bool(const KnownWorldEntity&)>& candidateFilter
	) {
		std::optional<KnownWorldEntity> best;
		float							minTotalTrip = std::numeric_limits<float>::max();

		for (const auto& [key, entity] : memory.knownWorldEntities) {
			if (!candidateFilter(entity)) {
				continue;
			}

			// totalTrip = distance(start, entity) + distance(entity, destination)
			float legOne = glm::distance(fromPosition, entity.position);
			float legTwo = glm::distance(entity.position, destination);
			float totalTrip = legOne + legTwo;

			if (totalTrip < minTotalTrip) {
				minTotalTrip = totalTrip;
				best = entity;
			}
		}

		return best;
	}

	size_t countKnownWithCapability(
		const Memory& memory,
		const engine::assets::AssetRegistry& /*registry*/,
		engine::assets::CapabilityType capability
	) {
		// O(1) count using capability index
		return memory.countWithCapability(capability);
	}

	std::optional<float>
	findNutritionAtPosition(const Memory& memory, const engine::assets::AssetRegistry& registry, const glm::vec2& targetPos) {
		// Search for edible entities near the target position
		const auto& edibleKeys = memory.getEntitiesWithCapability(engine::assets::CapabilityType::Edible);

		constexpr float kPositionTolerance = 0.5F;
		constexpr float kToleranceSq = kPositionTolerance * kPositionTolerance;

		for (uint64_t key : edibleKeys) {
			const auto* entity = memory.getWorldEntity(key);
			if (entity == nullptr) {
				continue;
			}

			// Check if this entity is at the target position
			glm::vec2 diff = entity->position - targetPos;
			if (glm::dot(diff, diff) < kToleranceSq) {
				// Found entity at target - get its nutrition value
				const std::string& defName = registry.getDefName(entity->defNameId);
				const auto*		   assetDef = registry.getDefinition(defName);
				if (assetDef != nullptr && assetDef->capabilities.edible.has_value()) {
					return assetDef->capabilities.edible->nutrition;
				}
			}
		}

		return std::nullopt;
	}

} // namespace ecs
