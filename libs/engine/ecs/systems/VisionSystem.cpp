#include "VisionSystem.h"

#include "../GlobalTaskRegistry.h"
#include "../World.h"
#include "../components/Appearance.h"
#include "../components/Knowledge.h"
#include "../components/Memory.h"
#include "../components/Transform.h"

#include "assets/AssetDefinition.h"
#include "assets/AssetRegistry.h"
#include "assets/RecipeRegistry.h"
#include "assets/placement/PlacementExecutor.h"
#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkManager.h"

#include <utils/Log.h>

#include <cmath>

namespace ecs {

	namespace {
		// Synthetic definition name for shore tiles (land adjacent to water)
		// Shore tiles are where colonists stand to drink from water
		constexpr const char* kShoreTileDefName = "Terrain_Shore";

		/// Get the TaskType for an entity based on its capabilities
		/// Returns None if no work task should be generated
		[[nodiscard]] TaskType getTaskTypeForCapabilities(uint8_t capabilityMask) {
			// Priority order: Carryable (haul) > Harvestable (gather)
			// Only work-related capabilities generate tasks
			if ((capabilityMask & (1 << static_cast<uint8_t>(engine::assets::CapabilityType::Carryable))) != 0) {
				return TaskType::Haul;
			}
			if ((capabilityMask & (1 << static_cast<uint8_t>(engine::assets::CapabilityType::Harvestable))) != 0) {
				return TaskType::Gather;
			}
			// Drinkable, Edible, Sleepable, Toilet → These are need fulfillment, not work tasks
			// They're handled separately by AIDecisionSystem
			return TaskType::None;
		}

		/// Notify the GlobalTaskRegistry about a discovered entity (if it generates work)
		void notifyTaskRegistry(
			EntityID colonist,
			uint64_t worldEntityKey,
			uint32_t defNameId,
			uint8_t	 capabilityMask,
			const glm::vec2& position,
			float currentTime
		) {
			TaskType taskType = getTaskTypeForCapabilities(capabilityMask);
			if (taskType == TaskType::None) {
				return; // No work task for this entity
			}

			GlobalTaskRegistry::Get().onEntityDiscovered(colonist, worldEntityKey, defNameId, position, taskType, currentTime);
		}

		/// Check if learning a new defNameId unlocks any recipes
		/// @param knowledge The colonist's knowledge (after learning)
		/// @param newlyLearnedId The defNameId that was just learned
		/// @param registry Asset registry for ID lookups
		/// @param recipeRegistry Recipe registry to check recipes
		/// @return Label of newly unlocked recipe, or empty if none
		std::string checkForRecipeUnlock(
			const Knowledge& knowledge,
			uint32_t newlyLearnedId,
			const engine::assets::AssetRegistry& registry,
			const engine::assets::RecipeRegistry& recipeRegistry
		) {
			// Only check non-innate recipes (innate recipes are always available)
			// A recipe is "newly unlocked" when the colonist now knows all inputs,
			// and the newly-learned item was the final missing input
			for (const auto& [defName, recipe] : recipeRegistry.allRecipes()) {
				if (recipe.innate) {
					continue;
				}

				// Check if this recipe has the newly learned item as an input
				bool hasNewInput = false;
				std::vector<uint32_t> inputIds;
				for (const auto& input : recipe.inputs) {
					uint32_t inputId = registry.getDefNameId(input.defName);
					inputIds.push_back(inputId);
					if (inputId == newlyLearnedId) {
						hasNewInput = true;
					}
				}

				// Skip recipes that don't involve the newly learned item
				if (!hasNewInput) {
					continue;
				}

				// Check if all inputs are now known
				if (knowledge.knowsAll(inputIds)) {
					return recipe.label;
				}
			}
			return "";
		}
	} // namespace

	void VisionSystem::ensureTerrainDefinitionsRegistered() {
		if (m_terrainDefsRegistered) {
			return;
		}

		auto& registry = engine::assets::AssetRegistry::Get();

		// Create capability mask for shore (Drinkable - colonists drink AT the shore)
		m_shoreTileCapabilityMask = (1 << static_cast<uint8_t>(engine::assets::CapabilityType::Drinkable));

		// Register synthetic shore tile definition
		m_shoreTileDefNameId = registry.registerSyntheticDefinition(kShoreTileDefName, m_shoreTileCapabilityMask);

		m_terrainDefsRegistered = true;
	}

	void VisionSystem::update(float /*deltaTime*/) {
		// Throttle: only run every N frames to reduce CPU overhead
		// Colonists move ~2-3 tiles/second, so 12 updates/sec is plenty
		m_frameCounter++;
		if (m_frameCounter < m_updateInterval) {
			return;
		}
		m_frameCounter = 0;

		if (m_placementExecutor == nullptr || m_processedChunks == nullptr) {
			return;
		}

		// Register terrain definitions on first update
		ensureTerrainDefinitionsRegistered();

		// Chunk size in world units (meters, since kTileSize = 1.0F)
		constexpr float kChunkWorldSize = static_cast<float>(engine::world::kChunkSize);

		auto& registry = engine::assets::AssetRegistry::Get();
		auto& recipeRegistry = engine::assets::RecipeRegistry::Get();

		// Iterate all entities with Position and Memory components
		for (auto [entity, pos, memory] : world->view<Position, Memory>()) {
			// Get optional Knowledge component for permanent discovery tracking
			auto* knowledge = world->getComponent<Knowledge>(entity);

			// Calculate bounding box of vision in world coordinates
			float minX = pos.value.x - memory.sightRadius;
			float maxX = pos.value.x + memory.sightRadius;
			float minY = pos.value.y - memory.sightRadius;
			float maxY = pos.value.y + memory.sightRadius;

			// Convert to chunk coordinate range
			int32_t chunkMinX = static_cast<int32_t>(std::floor(minX / kChunkWorldSize));
			int32_t chunkMaxX = static_cast<int32_t>(std::floor(maxX / kChunkWorldSize));
			int32_t chunkMinY = static_cast<int32_t>(std::floor(minY / kChunkWorldSize));
			int32_t chunkMaxY = static_cast<int32_t>(std::floor(maxY / kChunkWorldSize));

			float sightRadiusSq = memory.sightRadius * memory.sightRadius;

			// Query each potentially visible chunk for placed entities
			for (int32_t cy = chunkMinY; cy <= chunkMaxY; ++cy) {
				for (int32_t cx = chunkMinX; cx <= chunkMaxX; ++cx) {
					engine::world::ChunkCoordinate coord{cx, cy};

					// Only query processed chunks for placed entities
					if (m_processedChunks->find(coord) == m_processedChunks->end()) {
						continue;
					}

					const auto* chunkIndex = m_placementExecutor->getChunkIndex(coord);
					if (chunkIndex == nullptr) {
						continue;
					}

					// Query entities within sight radius from this chunk
					auto nearbyEntities = chunkIndex->queryRadius(pos.value, memory.sightRadius);

					// Remember each discovered entity
					for (const auto* placedEntity : nearbyEntities) {
						// Get defNameId and capability mask for registry notification
						uint32_t defNameId = registry.getDefNameId(placedEntity->defName);
						if (defNameId == 0) {
							continue;
						}
						uint8_t capabilityMask = registry.getCapabilityMask(defNameId);

						// Remember in colonist's memory
						memory.rememberWorldEntity(placedEntity->position, defNameId, capabilityMask);

						// Notify task registry for work-related entities
						uint64_t worldEntityKey = hashWorldEntity(placedEntity->position, defNameId);
						notifyTaskRegistry(entity, worldEntityKey, defNameId, capabilityMask, placedEntity->position, 0.0F);

						// Update permanent knowledge if Knowledge component exists
						if (knowledge != nullptr && knowledge->learn(defNameId)) {
							// New discovery - check for recipe unlocks
							std::string unlockedRecipe = checkForRecipeUnlock(*knowledge, defNameId, registry, recipeRegistry);
							if (!unlockedRecipe.empty() && m_onRecipeDiscovery) {
								m_onRecipeDiscovery(unlockedRecipe);
							}
						}
					}
				}
			}

			// Scan for ECS entities with Appearance (e.g., bio piles created by ActionSystem)
			// These are runtime-spawned entities, as opposed to those placed during world generation
			for (auto [otherEntity, otherPos, appearance] : world->view<Position, Appearance>()) {
				// Don't "see" yourself
				if (otherEntity == entity) {
					continue;
				}

				// Check if within sight radius
				float dx = otherPos.value.x - pos.value.x;
				float dy = otherPos.value.y - pos.value.y;
				if (dx * dx + dy * dy <= sightRadiusSq) {
					// Get defNameId and capability mask from registry
					uint32_t defNameId = registry.getDefNameId(appearance.defName);
					if (defNameId != 0) {
						uint8_t capabilityMask = registry.getCapabilityMask(defNameId);
						memory.rememberWorldEntity(otherPos.value, defNameId, capabilityMask);

						// Notify task registry for work-related entities
						uint64_t worldEntityKey = hashWorldEntity(otherPos.value, defNameId);
						notifyTaskRegistry(entity, worldEntityKey, defNameId, capabilityMask, otherPos.value, 0.0F);

						// Update permanent knowledge if Knowledge component exists
						if (knowledge != nullptr && knowledge->learn(defNameId)) {
							// New discovery - check for recipe unlocks
							std::string unlockedRecipe = checkForRecipeUnlock(*knowledge, defNameId, registry, recipeRegistry);
							if (!unlockedRecipe.empty() && m_onRecipeDiscovery) {
								m_onRecipeDiscovery(unlockedRecipe);
							}
						}
					}
				}
			}

			// Scan for shore tiles using pre-cached shore tile positions
			// Shore tiles are pre-computed during chunk generation for O(N) lookup
			// instead of iterating all ~3600 tiles in vision range every frame
			if (m_chunkManager != nullptr && m_shoreTileDefNameId != 0) {
				for (int32_t cy = chunkMinY; cy <= chunkMaxY; ++cy) {
					for (int32_t cx = chunkMinX; cx <= chunkMaxX; ++cx) {
						engine::world::ChunkCoordinate coord{cx, cy};

						const auto* chunk = m_chunkManager->getChunk(coord);
						if (chunk == nullptr || !chunk->isReady()) {
							continue;
						}

						auto origin = chunk->worldOrigin();

						// Use cached shore tiles instead of iterating all tiles
						for (const auto& [localX, localY] : chunk->getShoreTiles()) {
							glm::vec2 shoreWorldPos{
								origin.x + static_cast<float>(localX) + 0.5F, origin.y + static_cast<float>(localY) + 0.5F
							};

							// Check if within sight radius
							float dx = shoreWorldPos.x - pos.value.x;
							float dy = shoreWorldPos.y - pos.value.y;
							if (dx * dx + dy * dy <= sightRadiusSq) {
								memory.rememberWorldEntity(shoreWorldPos, m_shoreTileDefNameId, m_shoreTileCapabilityMask);

								// Update permanent knowledge for shore tiles
								if (knowledge != nullptr && knowledge->learn(m_shoreTileDefNameId)) {
									// New discovery - check for recipe unlocks (unlikely for shore tiles, but consistent)
									std::string unlockedRecipe = checkForRecipeUnlock(*knowledge, m_shoreTileDefNameId, registry, recipeRegistry);
									if (!unlockedRecipe.empty() && m_onRecipeDiscovery) {
										m_onRecipeDiscovery(unlockedRecipe);
									}
								}
							}
						}
					}
				}
			}
		}
	}

} // namespace ecs
