#pragma once

// NavigationSystem - owns the per-region navmeshes and answers path queries (Nav B3).
//
// The sim area follows the THINGS THAT NEED SIMULATION (colonists) plus WHERE THE
// PLAYER IS LOOKING (the viewport), not the camera alone. Each ECS tick the system
// reads colonist positions (Position + Colonist) and the viewport rect pushed by the
// scene as plain data, builds a square AABB per driver (center +/- kSimRadiusMm) plus
// the viewport AABB, and clusters overlapping AABBs into merged bounding rectangles.
// Each cluster is a SimulationRegion with its own fine nav mesh. Disjoint drivers ->
// separate regions; overlapping drivers -> one merged region.
//
// The build ingests only the obstacles inside a region's rect, so a forested world
// (tens of thousands of loaded trees) still builds fast because a region holds only
// ~1-2k. Per region the expensive triangulation (geometry::nav::buildNavMesh) runs on
// a std::async worker; the cheap-but-not-thread-safe extraction that reads live game
// state (engine::nav::buildInput, which walks ConstructionWorld -- NOT thread-safe)
// runs ON the main thread, producing a self-contained NavMeshInput the worker owns by
// value. While a region's rebuild is in flight its OLD mesh keeps serving queries.
//
// Self-gating: nav generation is OFF the render clock. A region rebuilds only when its
// driver nears the region edge (a margin inside the rect), its obstacle inputs change
// (construction version or the in-area processed-chunk set), or it is brand new. A
// slow/stationary colonist with the camera held still keeps every gate shut -> no
// rebuild. Panning/zooming the camera moves only the viewport region, never a
// colonist's region.
//
// Queries (requestPath / isReachable / isOnMesh / nearestPathablePoint / inSimArea)
// dispatch by position: select the region whose rect contains the query point, then
// query that region's mesh. requestPath holds (returns nullopt) when start and goal
// fall in different regions or the goal lies outside all regions -- long-range routing
// across unsimulated space is Phase 2.

#include "../ISystem.h"

#include <core/Vec2i64.h>

#include <nav/NavMesh.h>
#include <nav/PathQuery.h>
#include <nav/RraCache.h>

#include <world/chunk/ChunkCoordinate.h>

#include <algorithm>
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

/// A square world-mm AABB in absolute coordinates. Used for the per-driver squares,
/// the viewport square, and the merged region rects. int64 because absolute world mm
/// can exceed float's exact-integer range -- a colonist 1 km out is 1e6 mm, well past
/// float's 2^24 exact limit, so the clustering math must not lose mm precision.
struct SimAabb {
	std::int64_t minX = 0;
	std::int64_t minY = 0;
	std::int64_t maxX = 0;
	std::int64_t maxY = 0;

	[[nodiscard]] geometry::Vec2i64 center() const { return {(minX + maxX) / 2, (minY + maxY) / 2}; }
	[[nodiscard]] std::int64_t		halfExtentX() const { return (maxX - minX) / 2; }
	[[nodiscard]] std::int64_t		halfExtentY() const { return (maxY - minY) / 2; }

	[[nodiscard]] bool contains(geometry::Vec2i64 p) const {
		return p.x >= minX && p.x <= maxX && p.y >= minY && p.y <= maxY;
	}
	// Overlap test (touching counts as overlap so adjacent squares merge).
	[[nodiscard]] bool overlaps(const SimAabb& o) const {
		return minX <= o.maxX && maxX >= o.minX && minY <= o.maxY && maxY >= o.minY;
	}
	void unionWith(const SimAabb& o) {
		minX = std::min(minX, o.minX);
		minY = std::min(minY, o.minY);
		maxX = std::max(maxX, o.maxX);
		maxY = std::max(maxY, o.maxY);
	}
};

/// Cluster a set of square AABBs into merged bounding rectangles: any two that overlap
/// (directly or transitively) collapse into their shared bounding box; disjoint inputs
/// stay separate. Order-independent. Exposed (free function) so the clustering is unit-
/// testable without a live ECS world.
[[nodiscard]] std::vector<SimAabb> clusterAabbs(const std::vector<SimAabb>& boxes);

/// Caches per-region navmeshes built from live world data and serves path queries.
///
/// Priority 51: below the AI/goal band (those sit at 50-60) so a mesh swapped in this
/// frame is queryable by the goal systems the SAME frame, yet still above any system
/// that might mutate the world mid-frame. The actual rebuild is async, so this slot
/// only governs when a just-finished mesh becomes visible to queries.
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

	// Push the viewport rect (world meters) for this tick. GameScene calls this every
	// frame after camera->update() with the camera's visible rect expanded by
	// kViewportMargin. This is PLAIN DATA: the scene no longer drives the navmesh -- the
	// system decides internally, off the render clock, whether the viewport region needs
	// (re)building. Without a pushed viewport the area is colonist-only.
	void setViewportRect(glm::vec2 centerMeters, glm::vec2 halfExtentMeters) {
		viewportCenterM	   = centerMeters;
		viewportHalfM	   = halfExtentMeters;
		haveViewport	   = true;
	}

	// Same signature VisionSystem uses. `processed` is the set of chunks that have
	// finished entity placement; it grows as spawn-ring chunks complete after the first
	// build. It does NOT gate the gather (the area sweep reads chunk readiness directly
	// through the ChunkManager); instead it drives the per-region rebuild gate -- when a
	// chunk overlapping a BUILT region joins (or leaves) this set, that region's in-area
	// obstacle set changed and its mesh must rebuild.
	void setPlacementData(
		engine::assets::PlacementExecutor*						  executor,
		const std::unordered_set<engine::world::ChunkCoordinate>* processed) {
		placement = executor;
		processedChunks = processed;
	}

	void setConstructionWorld(const engine::construction::ConstructionWorld* world) { constructionWorld = world; }

	// --- Queries (synchronous, dispatched to the region containing the point) -

	// Taut polyline in world meters from start to goal for a disc agent of the given
	// radius, or nullopt when there is no region covering BOTH endpoints, the endpoints
	// fall in DIFFERENT regions (long-range routing is Phase 2), or the goal is
	// unreachable within the shared region. Agent clearance is honored: the radius is
	// passed straight through to geometry::nav::pathThrough.
	//
	// `belief` is applied at query time against the region's truth mesh (not a second
	// mesh): a default (empty) BeliefFilter routes over truth, while a filter built from
	// a colonist's known segments/openings routes over what that colonist REMEMBERS. The
	// filter holds pointers into the caller's sets and is consumed synchronously.
	[[nodiscard]] std::optional<std::vector<glm::vec2>>
	requestPath(glm::vec2 startMeters, glm::vec2 goalMeters, float agentRadiusMeters,
				geometry::nav::BeliefFilter belief = {}) const;

	// Sound reachability pre-filter against the region containing both endpoints:
	// delegates to geometry::nav::reachable (O(log n) component + bottleneck check).
	//
	// Semantics are asymmetric by design:
	//   false => DEFINITELY unreachable (disconnected component or bottleneck < disc).
	//   true  => MAYBE reachable (over-approximation). Caller runs requestPath for certainty.
	//
	// No-region / cross-region policy: when no single region covers both endpoints this
	// returns true ("can't prove unreachable"). Colonists legitimately beeline while the
	// mesh is building or when operating outside any sim region; a goal-validity
	// pre-filter must not reject everything in that window.
	[[nodiscard]] bool isReachable(glm::vec2 startMeters, glm::vec2 goalMeters, float agentRadiusMeters,
								   geometry::nav::BeliefFilter belief = {}) const;

	// LOD seam predicate: true when `meters` falls inside some BUILT region rect. Outside
	// every built region the caller treats movement as the no-mesh beeline (LOD1 coarse
	// routing is Phase 2). False when no region has been built yet.
	[[nodiscard]] bool inSimArea(glm::vec2 meters) const;

	// True when `meters` lies on walkable mesh inside the region that contains it. False
	// when the point is off-mesh within its region (e.g. a colonist that beelined into a
	// water hole) or when no region covers it. Lets a path failure distinguish a stranded
	// colonist (recover by snapping back onto land) from one merely walled off.
	[[nodiscard]] bool isOnMesh(glm::vec2 meters) const;

	// Canonical world-position validity for spawn/placement/teleport. A position is VALID
	// IFF it sits on a WALKABLE NAV FACE inside an ACTIVE sim region -- exactly isOnMesh.
	//
	// CONSTRAINT: validity is owned entirely by the runtime nav mesh. The 3D world/terrain
	// source is load-time / chunk-generation data and is NOT consulted here. A position
	// outside every active region's mesh is simply NOT placeable for now: you cannot place
	// outside an active mesh yet. (A future Phase 2 coarse global geography mesh will extend
	// this to unsimulated space.) This is the ONE predicate every world-positioning path
	// goes through; do not reintroduce ad-hoc terrain/isWaterAt checks elsewhere.
	[[nodiscard]] bool isValidPosition(glm::vec2 meters) const { return isOnMesh(meters); }

	// Nearest walkable point in the region that contains `meters`, or nullopt when no
	// region covers it / that region has no walkable floor. Used to snap a stranded
	// (off-mesh, in-region) colonist back onto pathable ground.
	[[nodiscard]] std::optional<glm::vec2> nearestPathablePoint(glm::vec2 meters) const;

	// True when ANY region has a built, non-empty mesh. (A region whose first build is
	// still in flight does not count until it lands.)
	[[nodiscard]] bool hasMesh() const;

	// Monotonic counter, the MAX over all regions' per-region generations. A stored
	// NavPath stamps this at plan time so the replan loop can detect "the world rebuilt
	// under me". Stays 0 until the first mesh lands. (A per-region bump moves the max, so
	// any region rebuild is observed; the stamp is conservative across regions, which only
	// costs an occasional redundant replan, never a missed one.)
	[[nodiscard]] std::uint64_t generation() const { return meshGeneration; }

	// Number of regions currently tracked (building or built). For tests/overlay.
	[[nodiscard]] std::size_t regionCount() const { return regions.size(); }

	// Cumulative A* instrumentation since the last resetNavQueryStats() (P3.5).
	struct NavQueryStats {
		std::uint64_t totalQueries		 = 0;
		std::uint64_t totalNodesExpanded = 0;
		std::int64_t  lastNodesExpanded	 = 0;
		std::int64_t  lastPeakOpenSet	 = 0;
	};
	[[nodiscard]] const NavQueryStats& navQueryStats() const { return navStats; }
	void						   resetNavQueryStats() { navStats = {}; }

	// Number of live RRA* reverse-search caches across all regions (keyed by region id +
	// goal triangle). For tests/overlay: confirms the cap and generation-bump invalidation.
	[[nodiscard]] std::size_t rraCacheCount() const { return rraCaches.size(); }

	// --- Region access for the debug overlay -----------------------------------

	// A built region's mesh + rect, for the NavOverlay wireframe.
	struct RegionView {
		const geometry::nav::NavMesh* mesh;
		geometry::Vec2i64			  center;
		std::int64_t				  halfExtent;
	};
	// Snapshot of every region that currently has a non-empty mesh.
	[[nodiscard]] std::vector<RegionView> builtRegions() const;

	// Public constants referenced by GameScene (viewport margin) and tests (clamp bounds).
	//
	// Minimum half-extent: large enough to cover typical indoor rooms and short outdoor
	// paths even at maximum zoom-in.
	static constexpr std::int64_t kMinSimHalfExtentMm = 30000; // 30 m
	// Maximum half-extent: bounds the build cost. Kept small for now because a 200 m area
	// in a dense forest is thousands of trees and stalls the off-thread build.
	static constexpr std::int64_t kMaxSimHalfExtentMm = 64000; // 64 m
	// Half-extent of a colonist's own sim square (center +/- this). The square is the
	// minimum so the colonist always sits well inside a clamped region.
	static constexpr std::int64_t kSimRadiusMm = kMinSimHalfExtentMm; // 30 m
	// Driver travel (mm) that triggers a region re-center: a driver this far from the
	// region center recenters it. Below it the gate stays shut (hysteresis vs lerp noise).
	static constexpr std::int64_t kRecenterThresholdMm = 20000; // 20 m
	// Margin applied to the camera's visible half-diagonal for scroll/zoom headroom.
	// GameScene multiplies the half-diagonal by this before calling setViewportRect.
	static constexpr float kViewportMargin = 1.3F;

  private:
	// A clustered region this tick: the merged bounding rect plus the driver points
	// (colonist centers + viewport center) that fell in it. The driver points drive the
	// self-gate: a region is recentered only when one of its drivers nears the rect edge.
	struct DesiredRegion {
		SimAabb						   rect;
		std::vector<geometry::Vec2i64> drivers;
	};

	// Per-cluster navmesh + its build state. One per merged region this tick.
	struct SimulationRegion {
		std::int32_t			 id		   = 0;	  // stable across rebuilds; RRA cache key prefix
		geometry::Vec2i64		 center{0, 0};	  // BUILT center (mm)
		std::int64_t			 halfExtent = 0;  // BUILT half-extent (mm)
		geometry::nav::NavMesh	 navMesh;		  // current queryable mesh (empty = building)
		std::future<geometry::nav::NavMesh> future; // in-flight build (valid only while running)
		std::uint64_t			 builtVersion			 = UINT64_MAX; // ConstructionWorld::version at launch
		std::uint64_t			 builtAreaChunkSignature = 0;		   // processed-chunk hash at launch
		std::uint64_t			 meshGeneration			 = 0;		   // bumped on this region's mesh swap

		[[nodiscard]] bool hasMesh() const { return !navMesh.triangles.empty(); }
		[[nodiscard]] SimAabb rect() const {
			return {center.x - halfExtent, center.y - halfExtent, center.x + halfExtent, center.y + halfExtent};
		}
	};

	// Compute the desired regions this tick from colonist squares + the viewport square,
	// clustered. Each carries its merged rect and the driver points that fell in it.
	[[nodiscard]] std::vector<DesiredRegion> computeDesiredRegions() const;

	// Diff desired regions against current ones: a desired region whose drivers all sit
	// comfortably (>= kEdgeMarginMm) inside an overlapping existing region keeps that
	// region untouched (self-gate -- no rebuild unless its obstacles changed); otherwise
	// recenter/resize the best-overlapping region and rebuild, or spawn a new one. Regions
	// no longer wanted are dropped. Launches/relaunches async builds where needed.
	void reconcileRegions(const std::vector<DesiredRegion>& desired);

	// Drain any finished region builds (swap meshes in, bump generations, clear that
	// region's RRA caches).
	void drainFinishedBuilds();

	// Launch (or relaunch) the async build for `region` over its current center/extent.
	// Snapshots input on the main thread; the worker owns it by value.
	void launchBuild(SimulationRegion& region);

	// True if `region` must (re)build: never built, world version changed, a driver
	// nears its edge (handled by the caller before this), or its in-area processed-chunk
	// set changed.
	[[nodiscard]] bool regionObstaclesChanged(const SimulationRegion& region) const;

	// Clamp a requested half-extent to [min, max] and to the loaded-chunk extent.
	[[nodiscard]] std::int64_t clampHalfExtent(std::int64_t requested) const;

	// Order-independent hash of the processed chunks overlapping the square AABB centered
	// at `center` with `halfExtent`. Returns 0 when processedChunks is null.
	[[nodiscard]] std::uint64_t areaChunkSignature(geometry::Vec2i64 center, std::int64_t halfExtent) const;

	// Locate the index of the built region whose rect contains `meters`, or -1. When
	// rects overlap (a merge in progress) the first match wins; queries are well-defined
	// because a merged region covers the union, so any covering region answers correctly.
	[[nodiscard]] int regionContaining(glm::vec2 meters) const;

	// Margin (mm) inside a region rect: a driver closer than this to the edge triggers a
	// recenter. Keeps a colonist comfortably away from the off-mesh boundary.
	static constexpr std::int64_t kEdgeMarginMm = 8000; // 8 m
	// Half-extent fractional change that triggers a region resize (0.20 = 20%). A zoom
	// in/out past this rebuilds the region at the new size; small jitter stays gated.
	static constexpr double kSizeChangeThreshold = 0.20;

	engine::world::ChunkManager*							  chunkManager	   = nullptr;
	engine::assets::PlacementExecutor*						  placement		   = nullptr;
	const std::unordered_set<engine::world::ChunkCoordinate>* processedChunks  = nullptr;
	const engine::construction::ConstructionWorld*			  constructionWorld = nullptr;

	std::vector<SimulationRegion> regions;
	std::int32_t				  nextRegionId = 0;

	// Viewport rect pushed by the scene (world meters). haveViewport is false until the
	// first setViewportRect call (headless tests run colonist-only).
	glm::vec2 viewportCenterM{0.0F, 0.0F};
	glm::vec2 viewportHalfM{0.0F, 0.0F};
	bool	  haveViewport = false;

	// Max over all regions' meshGeneration; see generation().
	std::uint64_t meshGeneration = 0;

	// Resumable RRA* reverse-search caches, keyed by (regionId, goalTriangle). Triangle
	// indices are per-region and invalidated by a rebuild, so an entry is dropped when its
	// region's mesh swaps. Bounded by kMaxRraCaches (wholesale clear on overflow).
	struct RraKey {
		std::int32_t region;
		std::int32_t goalTri;
		bool operator==(const RraKey& o) const { return region == o.region && goalTri == o.goalTri; }
	};
	struct RraKeyHash {
		std::size_t operator()(const RraKey& k) const {
			return (static_cast<std::size_t>(static_cast<std::uint32_t>(k.region)) << 32)
				 ^ static_cast<std::size_t>(static_cast<std::uint32_t>(k.goalTri));
		}
	};
	static constexpr std::size_t kMaxRraCaches = 64;
	mutable std::unordered_map<RraKey, geometry::nav::RraCache, RraKeyHash> rraCaches;

	mutable NavQueryStats navStats;
};

} // namespace ecs
