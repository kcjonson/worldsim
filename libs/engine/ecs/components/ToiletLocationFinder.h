#pragma once

// Toilet Location Selection for Colonist AI
// Free function to find suitable outdoor locations for biological needs.
// See /docs/design/game-systems/colonists/needs.md for spec (lines 66-71).
//
// Hard-coded rules:
// 1. Must be outdoors (not water tile)
// 2. NOT adjacent to water (shore tiles rejected)
// 3. PREFER near existing waste (clustering bonus)
// 4. AVOID proximity to food sources (penalty)

#include "Memory.h"

#include <glm/vec2.hpp>
#include <optional>

namespace engine::world {
	class ChunkManager;
}

namespace engine::assets {
	class AssetRegistry;
}

namespace ecs {

	class World;

	/// Find a suitable toilet location near the given position.
	/// Searches for a valid tile that:
	/// - Is not water
	/// - Is not adjacent to water (shore)
	/// - Prefers clustering near existing BioPiles (from memory)
	/// - Avoids proximity to food sources (from memory)
	///
	/// @param colonistPos Current colonist world position
	/// @param chunkManager For terrain tile queries (surface, adjacency)
	/// @param ecsWorld Reserved for future use (currently unused)
	/// @param memory Colonist memory for querying known bio piles and food sources
	/// @param registry Asset registry for capability lookups
	/// @param searchRadius Maximum distance to search (meters)
	/// @return Suitable location, or nullopt if none found (caller should fallback)
	[[nodiscard]] std::optional<glm::vec2> findToiletLocation(
		const glm::vec2&						 colonistPos,
		const engine::world::ChunkManager&		 chunkManager,
		World&									 ecsWorld,
		const Memory&							 memory,
		const engine::assets::AssetRegistry&	 registry,
		float									 searchRadius = 25.0F);

} // namespace ecs
