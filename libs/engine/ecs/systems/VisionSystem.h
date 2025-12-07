#pragma once

// Vision System for Colonist Observation
// Updates colonist Memory components by observing nearby world entities.
// See /docs/design/game-systems/colonists/memory.md for design details.

#include "../ISystem.h"

#include <world/chunk/ChunkCoordinate.h>

#include <unordered_set>

namespace engine::assets {
	class PlacementExecutor;
}

namespace ecs {

/// Updates colonist Memory by observing nearby world entities.
/// Queries PlacementExecutor for PlacedEntities within each colonist's sight radius.
/// Priority: 45 (runs early, before needs decay and AI decisions)
class VisionSystem : public ISystem {
  public:
	void update(float deltaTime) override;

	[[nodiscard]] int priority() const override { return 45; }

	/// Set the placement executor and processed chunks for entity queries
	/// Must be called before update() can function
	void setPlacementData(
		engine::assets::PlacementExecutor*					   executor,
		const std::unordered_set<engine::world::ChunkCoordinate>* processedChunks) {
		m_placementExecutor = executor;
		m_processedChunks = processedChunks;
	}

  private:
	engine::assets::PlacementExecutor*								m_placementExecutor = nullptr;
	const std::unordered_set<engine::world::ChunkCoordinate>*		m_processedChunks = nullptr;
};

} // namespace ecs
