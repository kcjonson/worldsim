#pragma once

// NavigationSystem - owns the cached navmesh and answers path queries (Nav B3).
//
// One mesh for the whole loaded region. Each frame the system decides whether the
// world changed (a construction edit bumped ConstructionWorld::version(), or the
// set of fully-placed loaded chunks moved) and, if so, rebuilds the mesh OFF the
// main thread. The expensive triangulation (geometry::nav::buildNavMesh) runs on a
// std::async worker; the cheap-but-not-thread-safe extraction that reads live game
// state (engine::nav::buildInput, which walks ConstructionWorld -- NOT thread-safe)
// runs ON the main thread, producing a self-contained NavMeshInput the worker owns
// by value. While a rebuild is in flight the OLD mesh keeps serving queries, so a
// path query never blocks on a build and never races the extraction.
//
// Queries (requestPath / isReachable) are synchronous against the current cached
// mesh and honor the agent's disc radius through geometry::nav::pathThrough.

#include "../ISystem.h"

#include <nav/NavMesh.h>

#include <world/chunk/ChunkCoordinate.h>

#include <cstdint>
#include <future>
#include <glm/vec2.hpp>
#include <optional>
#include <unordered_set>
#include <vector>

namespace engine::assets {
	class PlacementExecutor;
}

namespace engine::construction {
	class ConstructionWorld;
}

namespace engine::world {
	class ChunkManager;
}

namespace ecs {

/// Caches a navmesh built from live world data and serves path queries.
///
/// Priority 51: below the AI/goal band (those sit at 50-60) so a mesh swapped in
/// this frame is queryable by the goal systems the SAME frame, yet still above any
/// system that might mutate the world mid-frame. The actual rebuild is async, so
/// this slot only governs when the just-finished mesh becomes visible to queries.
class NavigationSystem : public ISystem {
  public:
	NavigationSystem() = default;
	~NavigationSystem() override;

	NavigationSystem(const NavigationSystem&) = delete;
	NavigationSystem& operator=(const NavigationSystem&) = delete;

	void update(float deltaTime) override;

	[[nodiscard]] int		  priority() const override { return 51; }
	[[nodiscard]] const char* name() const override { return "NavigationSystem"; }

	// --- Resource injection (setter style; no heavy ctor deps) ----------------

	void setChunkManager(engine::world::ChunkManager* chunkManager) { m_chunkManager = chunkManager; }

	// Only fully-placed chunks feed the build; same signature VisionSystem uses.
	void setPlacementData(
		engine::assets::PlacementExecutor*						  executor,
		const std::unordered_set<engine::world::ChunkCoordinate>* processedChunks) {
		m_placement = executor;
		m_processedChunks = processedChunks;
	}

	void setConstructionWorld(const engine::construction::ConstructionWorld* world) { m_constructionWorld = world; }

	// --- Queries (synchronous, against the current cached mesh) ---------------

	// Taut polyline in world meters from start to goal for a disc agent of the
	// given radius, or nullopt when there is no mesh yet or the goal is unreachable
	// (off-mesh or disconnected). Agent clearance is honored: the radius is passed
	// straight through to geometry::nav::pathThrough.
	[[nodiscard]] std::optional<std::vector<glm::vec2>>
	requestPath(glm::vec2 startMeters, glm::vec2 goalMeters, float agentRadiusMeters) const;

	// True if a path of the given clearance exists from start to goal.
	[[nodiscard]] bool isReachable(glm::vec2 startMeters, glm::vec2 goalMeters, float agentRadiusMeters) const;

	// The current cached mesh (for the later debug overlay). Empty until hasMesh().
	[[nodiscard]] const geometry::nav::NavMesh& mesh() const { return m_mesh; }
	[[nodiscard]] bool						  hasMesh() const { return !m_mesh.triangles.empty(); }

  private:
	// True if the snapshotted inputs differ from what the current mesh was built
	// from (version moved, or the processed-and-loaded chunk set changed).
	[[nodiscard]] bool needsRebuild(const std::unordered_set<engine::world::ChunkCoordinate>& currentChunks) const;

	// The chunk coords that are both loaded AND fully placed -- the set the build
	// actually consumes.
	[[nodiscard]] std::unordered_set<engine::world::ChunkCoordinate> currentBuildableChunks() const;

	engine::world::ChunkManager*							  m_chunkManager = nullptr;
	engine::assets::PlacementExecutor*						  m_placement = nullptr;
	const std::unordered_set<engine::world::ChunkCoordinate>* m_processedChunks = nullptr;
	const engine::construction::ConstructionWorld*			  m_constructionWorld = nullptr;

	geometry::nav::NavMesh m_mesh; // the current queryable mesh (empty = none yet)

	// In-flight async build. Valid only while a rebuild is running.
	std::future<geometry::nav::NavMesh> m_future;

	// What the in-flight build (or, when no build is in flight, the current mesh)
	// was snapshotted from. A sentinel of UINT64_MAX means "nothing built yet" so
	// the first real version (0 on a fresh ConstructionWorld) triggers a build.
	std::uint64_t									   m_builtVersion = UINT64_MAX;
	std::unordered_set<engine::world::ChunkCoordinate> m_builtChunks;
	bool											   m_haveBuiltOnce = false;
};

} // namespace ecs
