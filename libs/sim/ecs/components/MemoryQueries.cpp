#include "MemoryQueries.h"

#include "assets/AssetRegistry.h"

#include <glm/geometric.hpp>
#include <utils/Log.h>

#include <cmath>
#include <limits>
#include <utility>

namespace ecs {

	std::vector<KnownWorldEntity>
	findKnownWithCapability(const Memory& memory, const assets::AssetRegistry& /*registry*/, assets::CapabilityType capability) {
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

	std::optional<KnownWorldEntity> findNearestReachableWithCapability(
		const Memory&										memory,
		assets::CapabilityType								capability,
		const glm::vec2&									fromPosition,
		const std::function<bool(const KnownWorldEntity&)>& accept,
		const std::function<bool(const glm::vec2&)>&		reachable,
		size_t												maxReachChecks
	) {
		const auto& entityKeys = memory.getEntitiesWithCapability(capability);

		// One pass per reachability check, each taking the nearest candidate ordered after the one the
		// previous pass rejected. The usual case (the nearest is reachable) is a single pass with no
		// candidate buffer, and `accept` only runs on a candidate that would beat the pass's best.
		std::optional<std::pair<float, uint64_t>> rejected;
		for (size_t check = 0; check < maxReachChecks; ++check) {
			const KnownWorldEntity*	   next = nullptr;
			std::pair<float, uint64_t> nextOrder{std::numeric_limits<float>::max(), 0};
			for (uint64_t key : entityKeys) {
				const auto* entity = memory.getWorldEntity(key);
				if (entity == nullptr) {
					continue;
				}
				const glm::vec2					 diff = entity->position - fromPosition;
				const std::pair<float, uint64_t> order{glm::dot(diff, diff), key};
				if ((rejected.has_value() && order <= *rejected) || (next != nullptr && order >= nextOrder)) {
					continue;
				}
				if (!accept(*entity)) {
					continue;
				}
				next = entity;
				nextOrder = order;
			}
			if (next == nullptr) {
				return std::nullopt;
			}
			if (!reachable || reachable(next->position)) {
				return *next;
			}
			rejected = nextOrder;
		}
		return std::nullopt;
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

	size_t countKnownWithCapability(const Memory& memory, const assets::AssetRegistry& /*registry*/, assets::CapabilityType capability) {
		// O(1) count using capability index
		return memory.countWithCapability(capability);
	}

	std::optional<float> findNutritionAtPosition(const Memory& memory, const assets::AssetRegistry& registry, const glm::vec2& targetPos) {
		// Search for edible entities near the target position
		const auto& edibleKeys = memory.getEntitiesWithCapability(assets::CapabilityType::Edible);

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
