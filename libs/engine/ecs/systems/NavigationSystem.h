#pragma once

// NavigationSystem - owns the cached navmesh and answers path queries (Nav B3).
//
// One mesh for a SIMULATION AREA: a square AABB in world-absolute mm that tracks
// the camera viewport. The build ingests only the obstacles inside that area, so a
// forested world (tens of thousands of loaded trees) still builds fast because the
// area holds only ~1-2k. Building over every loaded+processed chunk hung the
// off-thread triangulation; scoping to the area is the fix.
//
// The caller (GameScene) owns the camera and viewport dimensions and pushes the
// desired area each frame via setSimulationArea(centerMm, halfExtentMm). The half-
// extent is the camera's visible half-diagonal expanded by a margin (~1.3x) for
// scroll/zoom headroom. NavigationSystem clamps the half-extent to
// [kMinSimHalfExtentMm, kMaxSimHalfExtentMm] and to the loaded-chunk extent, then
// rebuilds when the BUILT area drifts: center moved > kRecenterThresholdMm OR
// clamped half-extent changed by > kSizeChangeThreshold (20%). Per-frame camera-
// lerp deltas stay well under those thresholds so no thrash occurs.
//
// Each frame the system decides whether the world changed (a construction edit
// bumped ConstructionWorld::version(), or this is the first build) and, if so,
// rebuilds the mesh OFF the main thread. The expensive triangulation
// (geometry::nav::buildNavMesh) runs on a std::async worker; the cheap-but-not-
// thread-safe extraction that reads live game state (engine::nav::buildInput, which
// walks ConstructionWorld -- NOT thread-safe) runs ON the main thread, producing a
// self-contained NavMeshInput the worker owns by value. While a rebuild is in flight
// the OLD mesh keeps serving queries, so a path query never blocks on a build and
// never races the extraction.
//
// Queries (requestPath / isReachable) are synchronous against the current cached
// mesh and honor the agent's disc radius through geometry::nav::pathThrough.

#include "../ISystem.h"

#include <core/Vec2i64.h>

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

	void setChunkManager(engine::world::ChunkManager* manager) { chunkManager = manager; }

	// Push the desired simulation area for this frame. GameScene calls this every
	// frame after camera->update() using the camera's visible rect expanded by
	// kViewportMargin. NavigationSystem clamps the half-extent and rebuilds when the
	// built area has drifted far enough (center > kRecenterThresholdMm or size
	// changed > kSizeChangeThreshold). Without a pushed area no build runs.
	void setSimulationArea(geometry::Vec2i64 centerMm, std::int64_t halfExtentMm) {
		requestedCenter = centerMm;
		requestedHalfExtent = halfExtentMm;
		haveRequestedArea = true;
	}

	// Only fully-placed chunks feed the build; same signature VisionSystem uses. The
	// processed-chunks set gates which chunks are queried for flora/water during the
	// area sweep (a half-placed chunk in the area contributes nothing rather than a
	// partial obstacle set).
	void setPlacementData(
		engine::assets::PlacementExecutor*						  executor,
		const std::unordered_set<engine::world::ChunkCoordinate>* processed) {
		placement = executor;
		processedChunks = processed;
	}

	void setConstructionWorld(const engine::construction::ConstructionWorld* world) { constructionWorld = world; }

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
	[[nodiscard]] const geometry::nav::NavMesh& mesh() const { return navMesh; }
	[[nodiscard]] bool						  hasMesh() const { return !navMesh.triangles.empty(); }

	// Monotonic counter bumped each time a freshly built mesh is swapped in. A stored
	// NavPath stamps this at plan time so the replan loop can detect "the world rebuilt
	// under me" without inspecting the mesh. Stays 0 until the first mesh lands.
	[[nodiscard]] std::uint64_t generation() const { return meshGeneration; }

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
	[[nodiscard]] const NavQueryStats& navQueryStats() const { return navStats; }
	void						   resetNavQueryStats() { navStats = {}; }

	// Number of live RRA* reverse-search caches (one per distinct goal triangle). For
	// tests/overlay: confirms the cap and the generation-bump invalidation. Bounded by
	// kMaxRraCaches.
	[[nodiscard]] std::size_t rraCacheCount() const { return rraCaches.size(); }

	// Public constants referenced by GameScene (viewport margin) and tests (clamp bounds).
	//
	// Minimum half-extent: large enough to cover typical indoor rooms and short outdoor
	// paths even at maximum zoom-in.
	static constexpr std::int64_t kMinSimHalfExtentMm = 30000; // 30 m
	// Maximum half-extent: bounds the build cost. Kept small for now because a 200 m
	// area in a dense forest is thousands of trees and stalls the off-thread build
	// (zoom-out pegged CPU). The planned visual-size obstacle LOD -- cull sub-pixel
	// obstacles when zoomed out -- will let this grow again while staying cheap; until
	// then, zoom-out beyond this caps the fine mesh (beeline beyond).
	static constexpr std::int64_t kMaxSimHalfExtentMm = 64000; // 64 m
	// Camera travel (mm) that triggers an area re-center.
	static constexpr std::int64_t kRecenterThresholdMm = 20000; // 20 m
	// Margin applied to the camera's visible half-diagonal for scroll/zoom headroom.
	// GameScene multiplies the half-diagonal by this before calling setSimulationArea.
	static constexpr float kViewportMargin = 1.3F;

  private:
	// True if a rebuild is due: first build, world version change, or the built area
	// has drifted far enough from the requested area.
	[[nodiscard]] bool needsRebuild(std::int64_t clampedHalfExtent) const;

	// Clamp the requested half-extent to [min, max] and to the loaded-chunk extent.
	[[nodiscard]] std::int64_t clampHalfExtent(std::int64_t requested) const;

	// Half-extent fractional change that triggers a rebuild (0.20 = 20%).
	static constexpr double kSizeChangeThreshold = 0.20;

	engine::world::ChunkManager*							  chunkManager = nullptr;
	engine::assets::PlacementExecutor*						  placement = nullptr;
	const std::unordered_set<engine::world::ChunkCoordinate>* processedChunks = nullptr;
	const engine::construction::ConstructionWorld*			  constructionWorld = nullptr;

	geometry::nav::NavMesh navMesh; // the current queryable mesh (empty = none yet)

	// In-flight async build. Valid only while a rebuild is running.
	std::future<geometry::nav::NavMesh> future;

	// The ConstructionWorld::version() the in-flight build (or, when none is running,
	// the current mesh) was snapshotted from. UINT64_MAX means "nothing built yet" so
	// the first real version (0 on a fresh ConstructionWorld) triggers a build.
	std::uint64_t builtVersion = UINT64_MAX;
	bool		  haveBuiltOnce = false;

	// The requested simulation area pushed by the caller this frame. haveRequestedArea
	// is false until the first setSimulationArea call.
	geometry::Vec2i64 requestedCenter{0, 0};
	std::int64_t	  requestedHalfExtent = 0;
	bool			  haveRequestedArea = false;

	// The area the most-recently-launched build was snapshotted from. Compared each
	// frame to decide whether the drift is large enough to warrant a new build.
	geometry::Vec2i64 builtCenter{0, 0};
	std::int64_t	  builtHalfExtent = 0;

	// Bumped on every mesh swap-in (see generation()). Drives NavPath staleness for the
	// replan loop independently of ConstructionWorld::version() (which the system doesn't
	// expose and which a query-side consumer shouldn't depend on).
	std::uint64_t meshGeneration = 0;

	// Resumable RRA* reverse-search caches, keyed by GOAL TRIANGLE index. One reverse
	// search per goal serves every agent (belief- and radius-agnostic, built on the
	// width-unfiltered terrain graph), so many colonists heading to the same goal share
	// one search. Mutable because requestPath is const but lazily fills/resumes the cache.
	//
	// LIFECYCLE. Key: the goal triangle id in the CURRENT mesh. Triangle indices are
	// invalidated by a mesh rebuild, so the whole map is CLEARED wherever meshGeneration
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
	mutable std::unordered_map<std::int32_t, geometry::nav::RraCache> rraCaches;

	mutable NavQueryStats navStats;
};

} // namespace ecs
