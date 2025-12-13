#include "MemoryQueries.h"

#include "assets/AssetRegistry.h"

#include <utils/Log.h>
#include <glm/geometric.hpp>

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

		// Log capability search for Drinkable (for debugging coordinate issues)
		const bool isDrinkableSearch = (capability == engine::assets::CapabilityType::Drinkable);
		static int s_drinkSearchCount = 0;
		if (isDrinkableSearch && !entityKeys.empty() && s_drinkSearchCount++ < 5) {
			LOG_INFO(
				Engine,
				"[DrinkSearch] from=(%.1f, %.1f), candidates=%zu",
				fromPosition.x,
				fromPosition.y,
				entityKeys.size()
			);
		}

		int candidateIndex = 0;
		for (uint64_t key : entityKeys) {
			const auto* entity = memory.getWorldEntity(key);
			if (entity != nullptr) {
				glm::vec2 diff = entity->position - fromPosition;
				float	  distSq = glm::dot(diff, diff); // Squared distance avoids sqrt

				// DEBUG: Log each drinkable candidate with direction info
				if (isDrinkableSearch) {
					float dist = std::sqrt(distSq);
					// Calculate cardinal direction (for human-readable debugging)
					const char* direction = "UNKNOWN";
					if (std::abs(diff.x) > std::abs(diff.y)) {
						direction = (diff.x > 0) ? "EAST" : "WEST";
					} else {
						// NOTE: In our world, +Y = SOUTH (Y-down convention)
						direction = (diff.y > 0) ? "SOUTH" : "NORTH";
					}

					LOG_DEBUG(
						Engine,
						"[MemoryQuery]   #%d: pos=(%.2f, %.2f), diff=(%.2f, %.2f), dist=%.2f, dir=%s%s",
						candidateIndex,
						entity->position.x,
						entity->position.y,
						diff.x,
						diff.y,
						dist,
						direction,
						(distSq < minDistSq) ? " *NEAREST*" : ""
					);
				}

				if (distSq < minDistSq) {
					minDistSq = distSq;
					nearest = *entity;
				}
				candidateIndex++;
			}
		}

		// Log final selection for drinkable searches (for debugging coordinate issues)
		if (isDrinkableSearch && nearest.has_value() && s_drinkSearchCount <= 5) {
			LOG_INFO(
				Engine,
				"[DrinkTarget] target=(%.1f, %.1f) dist=%.1f",
				nearest->position.x,
				nearest->position.y,
				std::sqrt(minDistSq)
			);
		}

		return nearest;
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
