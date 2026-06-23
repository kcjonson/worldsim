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
#include <nav/PathQuery.h>
#include <nav/RraCache.h>

#include <world/chunk/ChunkCoordinate.h>

#include <cstdint>
#include <future>
#include <glm/vec2.hpp>
#include <optional>
#include <unordered_map>
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
	//
	// `belief` is applied at query time against the single shared truth mesh (not a
	// second mesh): a default (empty) BeliefFilter is a TRUTH query that routes exactly
	// as before, while a filter built from a colonist's known segments/openings routes
	// over what that colonist REMEMBERS (unseen walls absent, seen walls blocking). The
	// filter holds pointers into the caller's sets and is consumed synchronously, so
	// there is no lifetime hazard.
	[[nodiscard]] std::optional<std::vector<glm::vec2>>
	requestPath(glm::vec2 startMeters, glm::vec2 goalMeters, float agentRadiusMeters,
				geometry::nav::BeliefFilter belief = {}) const;

	// Sound reachability pre-filter: delegates to geometry::nav::reachable (O(log n)
	// component + bottleneck check) rather than building a full path.
	//
	// Semantics are asymmetric by design:
	//   false => DEFINITELY unreachable (off-mesh endpoint, disconnected component, or
	//            widest bottleneck < disc diameter). A false never hides a real path.
	//   true  => MAYBE reachable (over-approximation). Caller must run requestPath for
	//            certainty if it needs the actual polyline.
	//
	// No-mesh policy: when hasMesh() is false this returns true ("can't prove
	// unreachable"). Colonists legitimately beeline while the mesh is building or
	// when operating outdoors; a goal-validity pre-filter must not reject everything
	// during that window.
	[[nodiscard]] bool isReachable(glm::vec2 startMeters, glm::vec2 goalMeters, float agentRadiusMeters,
								   geometry::nav::BeliefFilter belief = {}) const;

	// The current cached mesh (for the later debug overlay). Empty until hasMesh().
	[[nodiscard]] const geometry::nav::NavMesh& mesh() const { return m_mesh; }
	[[nodiscard]] bool						  hasMesh() const { return !m_mesh.triangles.empty(); }

	// Monotonic counter bumped each time a freshly built mesh is swapped in. A stored
	// NavPath stamps this at plan time so the replan loop can detect "the world rebuilt
	// under me" without inspecting the mesh. Stays 0 until the first mesh lands.
	[[nodiscard]] std::uint64_t generation() const { return m_generation; }

	// Cumulative A* instrumentation since the last resetNavQueryStats() (P3.5). Lets a
	// dev overlay / HTTP endpoint VERIFY the RRA* heuristic cuts expansions at runtime.
	// totalQueries counts requestPath calls that actually ran the A* (a mesh existed and
	// the result was reachable -- where nodesExpanded is meaningful); lastNodesExpanded
	// / lastPeakOpenSet are the most recent such query's counts. A full NavOverlay/HTTP
	// surface is a follow-up; this accessor is the read hook for it.
	struct NavQueryStats {
		std::uint64_t totalQueries		= 0;
		std::uint64_t totalNodesExpanded = 0;
		std::int64_t  lastNodesExpanded = 0;
		std::int64_t  lastPeakOpenSet	= 0;
	};
	[[nodiscard]] const NavQueryStats& navQueryStats() const { return m_navStats; }
	void						   resetNavQueryStats() { m_navStats = {}; }

	// Number of live RRA* reverse-search caches (one per distinct goal triangle). For
	// tests/overlay: confirms the cap and the generation-bump invalidation. Bounded by
	// kMaxRraCaches.
	[[nodiscard]] std::size_t rraCacheCount() const { return m_rraCaches.size(); }

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

	// Bumped on every mesh swap-in (see generation()). Drives NavPath staleness for the
	// replan loop independently of ConstructionWorld::version() (which the system doesn't
	// expose and which a query-side consumer shouldn't depend on).
	std::uint64_t m_generation = 0;

	// Resumable RRA* reverse-search caches, keyed by GOAL TRIANGLE index. One reverse
	// search per goal serves every agent (belief- and radius-agnostic, built on the
	// width-unfiltered terrain graph), so many colonists heading to the same goal share
	// one search. Mutable because requestPath is const but lazily fills/resumes the cache.
	//
	// LIFECYCLE. Key: the goal triangle id in the CURRENT mesh. Triangle indices are
	// invalidated by a mesh rebuild, so the whole map is CLEARED wherever m_generation
	// bumps (the mesh swap in update()). BOUND: a churn of distinct goals must not grow
	// the map without limit, so when it would exceed kMaxRraCaches we clear it wholesale
	// (simplest sound policy -- the caches are cheap to rebuild on demand, and a flat
	// clear keeps no stale entry; a smarter LRU is a possible later refinement).
	//
	// THREADING: NavigationSystem queries run on the single-threaded main loop, so the
	// mutable cache map and stats need no locking. (The async work is the MESH BUILD,
	// which produces a value the main thread swaps in under update(); queries never touch
	// the in-flight build.)
	static constexpr std::size_t				   kMaxRraCaches = 64;
	mutable std::unordered_map<std::int32_t, geometry::nav::RraCache> m_rraCaches;

	mutable NavQueryStats m_navStats;
};

} // namespace ecs
