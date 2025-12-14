#include "ToiletLocationFinder.h"

#include "MemoryQueries.h"
#include "Transform.h"

#include "assets/AssetRegistry.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/ChunkManager.h"
#include "world/chunk/TileAdjacency.h"

#include <utils/Log.h>

#include <cmath>
#include <limits>
#include <vector>

namespace ecs {

namespace {

/// Grid spacing for candidate sampling (meters)
constexpr float kSampleSpacing = 3.0F;

/// Surface ID for water (must match Surface::Water enum value)
constexpr uint8_t kWaterSurfaceId = 4;

/// Scoring weights
constexpr float kBioPileClusterBonus = 10.0F;   // Bonus per nearby BioPile
constexpr float kBioPileClusterRadius = 8.0F;   // Distance to consider "nearby"
constexpr float kFoodProximityPenalty = 20.0F;  // Penalty per nearby food source
constexpr float kFoodAvoidanceRadius = 15.0F;   // Distance to avoid from food

/// Collect positions of known waste entities (bio piles) from memory
[[nodiscard]] std::vector<glm::vec2> collectBioPilePositions(
	const Memory& memory,
	const engine::assets::AssetRegistry& registry
) {
	std::vector<glm::vec2> positions;
	auto wastes = findKnownWithCapability(memory, registry, engine::assets::CapabilityType::Waste);
	for (const auto& entity : wastes) {
		positions.push_back(entity.position);
	}
	return positions;
}

/// Collect positions of known edible entities from memory
[[nodiscard]] std::vector<glm::vec2> collectFoodPositions(
	const Memory& memory,
	const engine::assets::AssetRegistry& registry
) {
	std::vector<glm::vec2> positions;
	auto edibles = findKnownWithCapability(memory, registry, engine::assets::CapabilityType::Edible);
	for (const auto& entity : edibles) {
		positions.push_back(entity.position);
	}
	return positions;
}

/// Check if a tile at the given world position is valid for toilet use
[[nodiscard]] bool isValidToiletTile(
	const glm::vec2& pos,
	const engine::world::ChunkManager& chunkManager
) {
	using namespace engine::world;

	// Convert to chunk coordinate and local tile
	WorldPosition worldPos{pos.x, pos.y};
	ChunkCoordinate chunkCoord = worldToChunk(worldPos);
	auto [localX, localY] = worldToLocalTile(worldPos);

	// Get the chunk
	const Chunk* chunk = chunkManager.getChunk(chunkCoord);
	if (chunk == nullptr || !chunk->isReady()) {
		return false;  // Chunk not loaded or not ready
	}

	// Get tile data
	const TileData& tile = chunk->getTile(localX, localY);

	// Rule 1: Must NOT be water
	if (tile.surface == Surface::Water) {
		return false;
	}

	// Rule 2: Must NOT be adjacent to water (shore tiles rejected)
	if (TileAdjacency::hasAdjacentSurface(tile.adjacency, kWaterSurfaceId)) {
		return false;
	}

	return true;
}

/// Calculate score for a candidate position
[[nodiscard]] float scorePosition(
	const glm::vec2& candidate,
	const glm::vec2& colonistPos,
	const std::vector<glm::vec2>& bioPilePositions,
	const std::vector<glm::vec2>& foodPositions
) {
	// Base score: prefer closer positions (mild preference)
	float distance = glm::length(candidate - colonistPos);
	float score = 100.0F - distance;  // Closer is slightly better

	// Bonus for clustering near existing BioPiles
	for (const auto& pilePos : bioPilePositions) {
		float distToPile = glm::length(candidate - pilePos);
		if (distToPile < kBioPileClusterRadius) {
			// Bonus inversely proportional to distance (closer = more bonus)
			float proximity = 1.0F - (distToPile / kBioPileClusterRadius);
			score += kBioPileClusterBonus * proximity;
		}
	}

	// Penalty for proximity to food sources
	for (const auto& foodPos : foodPositions) {
		float distToFood = glm::length(candidate - foodPos);
		if (distToFood < kFoodAvoidanceRadius) {
			// Penalty inversely proportional to distance (closer = more penalty)
			float proximity = 1.0F - (distToFood / kFoodAvoidanceRadius);
			score -= kFoodProximityPenalty * proximity;
		}
	}

	return score;
}

} // namespace

std::optional<glm::vec2> findToiletLocation(
	const glm::vec2& colonistPos,
	const engine::world::ChunkManager& chunkManager,
	World& /*ecsWorld*/,
	const Memory& memory,
	const engine::assets::AssetRegistry& registry,
	float searchRadius
) {
	// Collect known BioPile and food positions for scoring (from memory)
	auto bioPilePositions = collectBioPilePositions(memory, registry);
	auto foodPositions = collectFoodPositions(memory, registry);

	// Track best candidate
	std::optional<glm::vec2> bestPosition;
	float bestScore = -std::numeric_limits<float>::infinity();

	// Sample positions in a grid pattern within search radius
	int gridSize = static_cast<int>(std::ceil(searchRadius / kSampleSpacing));

	for (int dy = -gridSize; dy <= gridSize; ++dy) {
		for (int dx = -gridSize; dx <= gridSize; ++dx) {
			glm::vec2 candidate = colonistPos + glm::vec2(
				static_cast<float>(dx) * kSampleSpacing,
				static_cast<float>(dy) * kSampleSpacing
			);

			// Check if within circular search radius
			float dist = glm::length(candidate - colonistPos);
			if (dist > searchRadius) {
				continue;
			}

			// Validate tile (not water, not shore)
			if (!isValidToiletTile(candidate, chunkManager)) {
				continue;
			}

			// Score this position
			float score = scorePosition(candidate, colonistPos, bioPilePositions, foodPositions);

			if (score > bestScore) {
				bestScore = score;
				bestPosition = candidate;
			}
		}
	}

	if (bestPosition.has_value()) {
		LOG_DEBUG(
			Engine,
			"[ToiletLocation] Found location at (%.1f, %.1f) with score %.1f (%.0f BioPiles, %.0f food sources nearby)",
			bestPosition->x,
			bestPosition->y,
			bestScore,
			static_cast<float>(bioPilePositions.size()),
			static_cast<float>(foodPositions.size())
		);
	} else {
		LOG_DEBUG(
			Engine,
			"[ToiletLocation] No valid location found within %.0fm of (%.1f, %.1f)",
			searchRadius,
			colonistPos.x,
			colonistPos.y
		);
	}

	return bestPosition;
}

} // namespace ecs
