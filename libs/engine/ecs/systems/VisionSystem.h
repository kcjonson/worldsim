#pragma once

// Vision System for Colonist Observation
// Updates colonist Memory components by observing nearby world entities.
// Also discovers terrain features (water tiles) that can fulfill needs.
// See /docs/design/game-systems/colonists/memory.md for design details.

#include "../ISystem.h"

#include <world/chunk/ChunkCoordinate.h>

#include <unordered_set>

namespace engine::assets {
	class PlacementExecutor;
}

namespace engine::world {
	class ChunkManager;
}

namespace ecs {

/// Updates colonist Memory by observing nearby world entities and terrain features.
/// Queries PlacementExecutor for PlacedEntities within each colonist's sight radius.
/// Also scans chunks for water tiles and registers them with Drinkable capability.
/// Priority: 45 (runs early, before needs decay and AI decisions)
class VisionSystem : public ISystem {
  public:
	void update(float deltaTime) override;

	[[nodiscard]] int priority() const override { return 45; }

	/// Set the placement executor and processed chunks for entity queries
	/// Must be called before update() can function
	void setPlacementData(
		engine::assets::PlacementExecutor*						  executor,
		const std::unordered_set<engine::world::ChunkCoordinate>* processedChunks) {
		m_placementExecutor = executor;
		m_processedChunks = processedChunks;
	}

	/// Set the chunk manager for terrain tile queries (water discovery)
	void setChunkManager(engine::world::ChunkManager* chunkManager) { m_chunkManager = chunkManager; }

  private:
	/// Ensure synthetic terrain definitions are registered (called once on first update)
	void ensureTerrainDefinitionsRegistered();

	engine::assets::PlacementExecutor*							  m_placementExecutor = nullptr;
	const std::unordered_set<engine::world::ChunkCoordinate>*	  m_processedChunks = nullptr;
	engine::world::ChunkManager*								  m_chunkManager = nullptr;

	// Cached defNameId for water tiles (registered on first update)
	uint32_t m_waterTileDefNameId = 0;
	uint8_t	 m_waterTileCapabilityMask = 0;
	bool	 m_terrainDefsRegistered = false;
};

} // namespace ecs
