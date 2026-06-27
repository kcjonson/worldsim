#pragma once

// GameWorldState - Shared state for passing initialized world between scenes.
// Used by GameLoadingScene to pre-load chunks and entity placement,
// then transferred to GameScene for gameplay.

#include <assets/placement/PlacementExecutor.h>
#include <ecs/components/Colony.h>
#include <world/camera/WorldCamera.h>
#include <world/chunk/ChunkCoordinate.h>
#include <world/chunk/ChunkManager.h>
#include <world/rendering/ChunkRenderer.h>
#include <world/rendering/EntityRenderer.h>

#include <glm/vec2.hpp>

#include <memory>
#include <unordered_set>

namespace world_sim {

	/// Holds initialized world state for transfer between loading and game scenes.
	/// Uses static SetPending()/Take() pattern for scene-to-scene handoff.
	struct GameWorldState {
		std::unique_ptr<engine::world::ChunkManager>	   chunkManager;
		std::unique_ptr<engine::world::WorldCamera>		   camera;
		std::unique_ptr<engine::world::ChunkRenderer>	   renderer;
		std::unique_ptr<engine::world::EntityRenderer>	   entityRenderer;
		std::unique_ptr<engine::assets::PlacementExecutor> placementExecutor;

		/// Tracks which chunks have completed entity placement
		std::unordered_set<engine::world::ChunkCoordinate> processedChunks;

		uint64_t worldSeed = 0;

		/// Colonist drop point (2D world meters): dry land beside clean water,
		/// chosen by findRiverbankSpawn. Defaults to the landing origin. GameScene
		/// snaps this to a cleared, on-mesh walkable tile center before spawning.
		glm::vec2 spawnPosition{0.0F, 0.0F};

		/// The colony: home anchor for the session. GameScene sets colony.originPosition
		/// to the cleared, on-mesh clearing center once at landing. Single source of truth
		/// for "where home is" -- the AI off-mesh recovery and any future home-relative
		/// mechanic read it from here. Not persisted yet: there is no gameplay-session save
		/// (only the planet itself round-trips via PlanetIO), so for now it is a durable
		/// in-session value. Wiring it into a session save is a follow-up.
		ecs::Colony colony;

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
