#pragma once

// Vision System for Colonist Observation
// Updates colonist Memory components by observing nearby world entities.
// Also discovers shore tiles (land adjacent to water) that can fulfill needs.
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
/// Also scans chunks for shore tiles (land adjacent to water) with Drinkable capability.
/// Priority: 45 (runs early, before needs decay and AI decisions)
///
/// Performance: Throttled to run every N frames (default 5) since colonists don't
/// move fast enough to need per-frame vision updates.
class VisionSystem : public ISystem {
  public:
	void update(float deltaTime) override;

	[[nodiscard]] int priority() const override { return 45; }
	[[nodiscard]] const char* name() const override { return "Vision"; }

	/// Set how often vision updates run (default: every 5 frames)
	/// At 60fps, 5 frames = 12 vision updates/second, which is plenty
	void setUpdateInterval(uint32_t frames) { m_updateInterval = frames; }

	/// Set the placement executor and processed chunks for entity queries
	/// Must be called before update() can function
	void setPlacementData(
		engine::assets::PlacementExecutor*						  executor,
		const std::unordered_set<engine::world::ChunkCoordinate>* processedChunks) {
		m_placementExecutor = executor;
		m_processedChunks = processedChunks;
	}

	/// Set the chunk manager for terrain tile queries (shore discovery)
	void setChunkManager(engine::world::ChunkManager* chunkManager) { m_chunkManager = chunkManager; }

  private:
	/// Ensure synthetic terrain definitions are registered (called once on first update)
	void ensureTerrainDefinitionsRegistered();

	engine::assets::PlacementExecutor*							  m_placementExecutor = nullptr;
	const std::unordered_set<engine::world::ChunkCoordinate>*	  m_processedChunks = nullptr;
	engine::world::ChunkManager*								  m_chunkManager = nullptr;

	// Cached defNameId for shore tiles (registered on first update)
	uint32_t m_shoreTileDefNameId = 0;
	uint8_t	 m_shoreTileCapabilityMask = 0;
	bool	 m_terrainDefsRegistered = false;

	// Throttling: only update every N frames to reduce CPU overhead
	uint32_t m_frameCounter = 0;
	uint32_t m_updateInterval = 5; // Default: update every 5 frames (12x/sec at 60fps)
};

} // namespace ecs
