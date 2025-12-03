#pragma once

// GameWorldState - Shared state for passing initialized world between scenes.
// Used by GameLoadingScene to pre-load chunks and entity placement,
// then transferred to GameScene for gameplay.

#include <assets/placement/PlacementExecutor.h>
#include <world/camera/WorldCamera.h>
#include <world/chunk/ChunkCoordinate.h>
#include <world/chunk/ChunkManager.h>
#include <world/rendering/ChunkRenderer.h>
#include <world/rendering/EntityRenderer.h>

#include <memory>
#include <unordered_set>

namespace world_sim {

	/// Holds initialized world state for transfer between loading and game scenes.
	/// Uses static SetPending()/Take() pattern for scene-to-scene handoff.
	struct GameWorldState {
		std::unique_ptr<engine::world::ChunkManager>	m_chunkManager;
		std::unique_ptr<engine::world::WorldCamera>		m_camera;
		std::unique_ptr<engine::world::ChunkRenderer>	m_renderer;
		std::unique_ptr<engine::world::EntityRenderer>	m_entityRenderer;
		std::unique_ptr<engine::assets::PlacementExecutor> m_placementExecutor;

		/// Tracks which chunks have completed entity placement
		std::unordered_set<engine::world::ChunkCoordinate> m_processedChunks;

		uint64_t m_worldSeed = 0;

		/// Store pending state (call from GameLoadingScene when done)
		static void SetPending(std::unique_ptr<GameWorldState> state);

		/// Take pending state (call from GameScene::onEnter)
		/// Returns nullptr if no pending state
		static std::unique_ptr<GameWorldState> Take();

		/// Check if there's pending state without taking it
		static bool HasPending();

	  private:
		static std::unique_ptr<GameWorldState> s_pending;
	};

} // namespace world_sim
