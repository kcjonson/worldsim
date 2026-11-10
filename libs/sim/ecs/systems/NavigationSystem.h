#pragma once

// NavigationSystem - owns the per-region navmeshes and answers path queries (Nav B3).
//
// Regions have an explicit owner. COLONIST regions follow the colonists: each colonist drives a
// square (position +/- kSimRadiusMm), and overlapping squares cluster into one region while the
// cluster fits the max region size; nothing else moves, resizes or rebuilds them. The CAMERA
// region follows the viewport rect the scene pushes as plain data. It is never merged with a
// colonist region; the two may overlap. Each region has its own fine nav mesh.
//
// The build ingests only the obstacles inside a region's rect, so a forested world
// (tens of thousands of loaded trees) still builds fast because a region holds only
// ~1-2k. Per region the expensive triangulation (geometry::nav::buildNavMesh) runs on
// a std::async worker; the cheap-but-not-thread-safe extraction that reads live game
// state (nav::buildInput, which walks ConstructionWorld -- NOT thread-safe)
// runs ON the main thread, producing a self-contained NavMeshInput the worker owns by
// value. While a region's rebuild is in flight its OLD mesh keeps serving queries.
//
// Self-gating: nav generation is OFF the render clock. A region rebuilds only when one of
// its drivers nears the region edge (a margin inside the rect), its obstacle inputs change
// (construction version, the in-area chunks' terrain polygon versions, or their
// processed-placement state), or it is brand new. A stationary colonist with the camera held
// still keeps every gate shut. Panning/zooming moves only the camera region.
//
// Query dispatch. Colonist queries (requestPath, reachFrom/reaches/coversGoal/stagingPointToward,
// inSimArea, isOnMesh, nearestPathablePoint, isBuildingAround) consult colonist regions only, so
// where the camera is never changes a colonist's answer. Where colonist regions overlap, a point
// or pair is answered by the built regions holding it whose meshes reflect the newest world edits
// (MeshStamp), in id order: a stale mesh never overrides a fresher one. Placement checks
// (isValidPosition, the walkable area/segment/polyline family, findValidPositionNear) consult
// every region, colonist regions first, so the player can place where he looks. No region holding
// both ends of a pair -> no route; long-range routing across regions is Phase 2.

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

namespace assets {
	class PlacementExecutor;
}

namespace construction {
	class ConstructionWorld;
}

namespace world {
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

/// Who a region exists for; see the dispatch rule at the top of this file.
enum class RegionOwner : std::uint8_t { Colonists, Camera };

/// The world edits a served mesh reflects. Both counters only grow, so a greater stamp is a mesh
/// built from a later world.
struct MeshStamp {
	std::uint64_t constructionVersion = 0;
	std::uint64_t removalEpoch		  = 0;

	auto operator<=>(const MeshStamp&) const = default;
};

/// A region wanted this tick: its rect, the driver points that fell in it (colonist positions, or
/// the viewport centre) and whose it is.
struct DesiredRegion {
	SimAabb						   rect;
	std::vector<geometry::Vec2i64> drivers;
	RegionOwner					   owner = RegionOwner::Colonists;
};

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

	void setChunkManager(world::ChunkManager* manager) { chunkManager = manager; }

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
		assets::PlacementExecutor*						  executor,
		const std::unordered_set<world::ChunkCoordinate>* processed) {
		placement = executor;
		processedChunks = processed;
	}

	void setConstructionWorld(const construction::ConstructionWorld* world) { constructionWorld = world; }

	// --- Queries (synchronous, dispatched to the region containing the point) -

	// Taut polyline in world meters from start to goal for a disc agent of the given
	// radius, or nullopt when no built colonist region's rect holds BOTH endpoints (long-range
	// routing across regions is Phase 2) or no such region's mesh has a route. Colonist regions
	// can overlap (a region keeps its rect until a driver nears the edge), so a pair is routed
	// through the regions holding both whose MeshStamp is the newest among them, in ascending id,
	// until one finds a route. Agent clearance is honored: the radius is passed straight through
	// to geometry::nav::pathThrough.
	//
	// `belief` is applied at query time against the region's truth mesh (not a second
	// mesh): a default (empty) BeliefFilter routes over truth, while a filter built from
	// a colonist's known segments/openings routes over what that colonist REMEMBERS. The
	// filter holds pointers into the caller's sets and is consumed synchronously.
	[[nodiscard]] std::optional<std::vector<glm::vec2>>
	requestPath(glm::vec2 startMeters, glm::vec2 goalMeters, float agentRadiusMeters,
				geometry::nav::BeliefFilter belief = {}) const;

	// Sound reachability pre-filter, split at the start so many goals from one start cost one
	// start-side climb per region: reachFrom locates the start in every built colonist region whose
	// rect holds it and finds its reach root in each one's forest (geometry::nav::reachFrom);
	// reaches() tests a goal against those of them that also hold it and carry the newest MeshStamp
	// among them, with a locate and an O(log n) subtree check, so it agrees with requestPath.
	//
	// Semantics are asymmetric by design:
	//   false => DEFINITELY unreachable (a region holding the start covers the goal and none of
	//            those lets it through: disconnected component or bottleneck < disc).
	//   true  => MAYBE reachable (over-approximation). Caller runs requestPath for certainty.
	//
	// Beyond-reach policy: when no region covers the start, or no region holding the start covers
	// the goal, reaches() returns true ("can't prove unreachable"). Without a mesh requestNavPath
	// holds and retries after each build; a goal beyond the start's regions (off every region, or
	// only in a region the colonist isn't in) is walked toward, an edge leg at a time, until a region
	// holding him covers it or the legs stop getting him closer.
	//
	// A Reach is used within one frame (the next update() may move or drop regions); the belief's
	// sets must outlive it. For a goal coversGoal() admits, reaches() depends only on the regions
	// in `regions` (their meshes, rects and the start's reach roots), which identify it across frames.
	struct RegionReach {
		int						 index = -1; // into the region list this frame
		std::int32_t			 id = -1;	 // stable across frames, unlike the index
		std::uint64_t			 meshGeneration = 0;
		MeshStamp				 stamp;
		SimAabb					 rect; // the rect its mesh covers
		geometry::nav::ReachFrom from;
	};
	struct Reach {
		std::vector<RegionReach>	regions; // every built colonist region holding the start, by ascending id
		geometry::nav::BeliefFilter belief;
	};
	// Fills `out`, reusing its storage.
	void				reachFrom(glm::vec2 startMeters, float agentRadiusMeters, geometry::nav::BeliefFilter belief,
								  Reach& out) const;
	[[nodiscard]] bool	reaches(const Reach& reach, glm::vec2 goalMeters) const;
	// Does some region holding the start also hold this goal? Only for such a goal does reaches()
	// consult a mesh, and only such a goal is routed to directly; any other is walked toward.
	[[nodiscard]] bool coversGoal(const Reach& reach, glm::vec2 goalMeters) const;

	// For a goal no region holding the start covers: where to walk to get closer. For each region
	// holding the start, the goal clamped kStagingInsetMm inside its rect (so a colonist standing there
	// is within kEdgeMarginMm and its region recenters toward the goal), then back along the line to
	// the start, every kStagingSampleStepMm, to the first point the start reaches; the candidate nearest
	// the goal wins. nullopt when none lies kStagingMinLegMm or more from the start: no step toward the
	// goal is possible from here. Bounded: one reach test per sample, a few hundred at most.
	[[nodiscard]] std::optional<glm::vec2> stagingPointToward(const Reach& reach, glm::vec2 startMeters,
															  glm::vec2 goalMeters) const;
	static constexpr std::int64_t kStagingInsetMm	   = 2000; // 2 m
	static constexpr std::int64_t kStagingSampleStepMm = 1000; // 1 m
	static constexpr std::int64_t kStagingMinLegMm	   = 500;  // 0.5 m
	// LOD seam predicate: true when `meters` falls inside some BUILT colonist region rect. A
	// colonist outside every one holds (LOD1 coarse routing is Phase 2). False when no colonist
	// region has been built yet.
	[[nodiscard]] bool inSimArea(glm::vec2 meters) const;

	// Is a build in flight for a colonist region whose rect (as launched) holds this point? A
	// colonist standing at his region's edge waits for it, since its swap re-plans his route.
	[[nodiscard]] bool isBuildingAround(glm::vec2 meters) const;

	// True when `meters` lies on walkable mesh in the colonist region that answers for it (the
	// newest-stamped built colonist region holding it, lowest id first). False when it is off a
	// walkable face there (e.g. a colonist that beelined into a water hole) or when no colonist
	// region covers it. Lets a path failure distinguish a stranded colonist (recover by snapping
	// back onto land) from one merely walled off.
	[[nodiscard]] bool isOnMesh(glm::vec2 meters) const;

	// Canonical world-position validity for spawn/placement/teleport. A position is VALID
	// IFF it sits on a WALKABLE NAV FACE in ANY active sim region holding it, of either owner, so
	// the player can place in the camera region.
	//
	// CONSTRAINT: validity is owned entirely by the runtime nav mesh. The 3D world/terrain
	// source is load-time / chunk-generation data and is NOT consulted here. A position
	// outside every active region's mesh is simply NOT placeable for now: you cannot place
	// outside an active mesh yet. (A future Phase 2 coarse global geography mesh will extend
	// this to unsimulated space.) This is the ONE predicate every world-positioning path
	// goes through; do not reintroduce ad-hoc terrain/isWaterAt checks elsewhere.
	[[nodiscard]] bool isValidPosition(glm::vec2 meters) const;

	// Whole-FOOTPRINT validity for an area structure (a foundation polygon). True IFF
	// EVERY part of the closed polygon -- vertices, edges, and interior -- sits on
	// walkable nav mesh. A footprint whose corners are on land but that spans a river,
	// or whose edge/interior clips a water hole or off-mesh gap, is REJECTED. Built on
	// the same per-point authority as isValidPosition (locateTriangle + terrainTraversable):
	// it samples every edge at <= kFootprintSampleStepMm and the interior on a grid of the
	// same pitch, requiring all samples to be walkable. No world/terrain/water source reads.
	// `polygonMeters` is the ring in world meters (no repeated closing vertex needed). A
	// ring of fewer than 3 points validates only its points. This is the ONE area predicate
	// for placement; the UI build tool and the dev verbs both go through it.
	[[nodiscard]] bool isAreaWalkable(const std::vector<glm::vec2>& polygonMeters) const;

	// Whole-CENTERLINE validity for a linear structure (a wall segment / chain edge). True
	// IFF the segment a->b lies entirely on walkable mesh: both endpoints plus every sample
	// along it at <= kFootprintSampleStepMm. The chain variant requires every consecutive
	// pair to pass. Wall thickness is not separately modeled here (the centerline at fine
	// spacing is the gate); same per-point authority and sampling pitch as isAreaWalkable.
	[[nodiscard]] bool isSegmentWalkable(glm::vec2 aMeters, glm::vec2 bMeters) const;

	// Whole-chain variant of isSegmentWalkable: every consecutive (pts[i], pts[i+1]) edge
	// must lie entirely on walkable mesh. A chain of one point validates just that point;
	// empty is trivially true. Used for the wall tool's full chain and the /api/dev/walls verb.
	[[nodiscard]] bool isPolylineWalkable(const std::vector<glm::vec2>& ptsMeters) const;

	// Whole-FOOTPRINT BUILDABILITY for an area structure. Like isAreaWalkable, but validated against
	// a TERRAIN-ONLY mesh (geography + built walls, no tree/rock entity obstacles) built on demand
	// over the footprint's local area. Clearable entities (trees/rocks) read as buildable here
	// (ConstructionSystem turns them into clear tasks) and so does every wall face; only water and
	// off-mesh gaps block. This is the placement gate for the build tool -- wall conflicts are
	// ConstructionValidator's job, not this predicate. isAreaWalkable stays the pure on-mesh
	// predicate; falls back to it when the nav build inputs aren't wired (headless/tests).
	[[nodiscard]] bool isAreaBuildable(const std::vector<glm::vec2>& polygonMeters) const;

	// Single-point buildability: the per-vertex / live-cursor counterpart of isAreaBuildable, against
	// the same cached terrain-only mesh. True over clearable entities (trees/rocks) and over a built
	// wall face; false only on water or an off-mesh gap. Falls back to isValidPosition when the build inputs
	// aren't wired (headless/tests).
	[[nodiscard]] bool isPointBuildable(glm::vec2 meters) const;

	// `meters` itself when it is walkable on the terrain-only mesh (water and built walls
	// block, trees and rocks don't), else the nearest point that is, within the cached
	// terrain area around it. Needs only the chunk manager, so it answers before any sim
	// region exists: the landing uses it to move a drop point off the water. nullopt when
	// the chunks aren't wired or the area holds no walkable ground.
	[[nodiscard]] std::optional<glm::vec2> nearestTerrainWalkablePoint(glm::vec2 meters) const;

	// Where a landing party of `count` stands, judged on the terrain-only mesh like
	// nearestTerrainWalkablePoint (so it answers before any sim region exists).
	struct GroupSpawn {
		glm::vec2			   center; // the group's anchor (clearing center, colony origin)
		std::vector<glm::vec2> points; // one per member, in order
	};
	// The center is the nearest point to `drop` (searched outward in rings) whose whole
	// member ring -- `count` points spaced evenly on a circle of `ringRadiusMeters`, or the
	// center alone for one member -- is clear ground, so the ring never straddles a shore.
	// Clear means walkable at the point and at `clearanceMeters` around it (an agent's disc
	// fits). Every member then stands on its ring point when that point is clear and at least
	// `minSeparationMeters` from the members already placed, else on the nearest spot around
	// it that is; each point is valid on its own. With no chunks wired (headless), the center
	// is `drop` and the points are the raw ring.
	[[nodiscard]] GroupSpawn groupSpawnPoints(glm::vec2 drop, std::size_t count, float ringRadiusMeters,
											  float minSeparationMeters, float clearanceMeters) const;

	// Sampling pitch (mm) for the whole-footprint / whole-centerline walkability checks.
	// 0.5 m is fine enough to catch a ~1-tile water sliver between two on-land vertices.
	// Footprints are small, so even a dense interior grid at this pitch is a few hundred
	// O(1)-ish locateTriangle probes -- cheap enough for a per-frame live preview.
	static constexpr std::int64_t kFootprintSampleStepMm = 500; // 0.5 m

	// Nearest walkable point in the colonist region that answers for `meters` (as isOnMesh), or
	// nullopt when no colonist region covers it / that region has no walkable floor. Used to snap
	// a stranded (off-mesh, in-region) colonist back onto pathable ground.
	[[nodiscard]] std::optional<glm::vec2> nearestPathablePoint(glm::vec2 meters) const;

	// Like nearestPathablePoint, but the returned point also clears the BUILT-wall
	// collision bands for an agent of radius `agentRadiusMeters` (the same
	// halfThickness + radius clearance WallCollisionSystem enforces). Recovery snaps
	// MUST use this: the bare nearest-walkable point sits on the walkable-face edge,
	// only halfThickness from a wall centerline, i.e. INSIDE the collision band -- so
	// WallCollisionSystem would shove the just-snapped colonist straight back off-mesh,
	// and the two systems oscillate (the colonist-wall-trap bug). This finds a point
	// that is BOTH on a walkable face AND outside every built band, so neither system
	// has anything left to correct. Falls back to the nearest band-clear walkable
	// triangle interior when the closest pocket is too thin to hold the agent (so a
	// colonist trapped in a sub-clearance sliver recovers to the roomy side). nullopt
	// only when no region covers `meters` or the region has no walkable floor at all.
	[[nodiscard]] std::optional<glm::vec2> nearestPathablePoint(glm::vec2 meters, float agentRadiusMeters) const;

	// A walkable point at least `minDistMeters` away from `origin`, used to place a thing a
	// short distance OFF the spot that produced it (a crafted furniture box coming out of a
	// station, an item that must not drop into a river). Returns meters, like
	// nearestPathablePoint; deterministic (no RNG, a fixed angle set with a fixed start, a
	// deterministic tiebreak) so the same inputs always yield the same point.
	//
	// `preferredDir` biases the first candidate (e.g. away from the crafting colonist so a
	// run of boxes fans out instead of stacking); a zero vector falls back to {1,0} so the
	// result stays canonical. The search: a single candidate at origin + minDist*dir, then a
	// ring of fixed angles at that radius, then a fallback that pushes the nearest walkable
	// point out to minDist. The mesh searched is the first built region holding `origin`, colonist
	// regions before the camera's, then newest stamp; candidates are checked with isValidPosition.
	// nullopt only when `origin` is outside every region or that region has no walkable floor at
	// all -- whenever any walkable floor exists, the result is non-null.
	[[nodiscard]] std::optional<glm::vec2>
	findValidPositionNear(glm::vec2 origin, float minDistMeters = 1.0F, glm::vec2 preferredDir = {0.0F, 0.0F}) const;

	// True when ANY region has a built, non-empty mesh. (A region whose first build is
	// still in flight does not count until it lands.)
	[[nodiscard]] bool hasMesh() const;

	// True while any region's build is in flight. update() launches every build its gates
	// call for before it returns, so false right after an update means the system has
	// settled: with the same inputs, no later update changes a mesh. Tests use it to tell
	// a build still landing from one that will never start.
	[[nodiscard]] bool isBuilding() const;

	// Monotonic count of mesh swaps across all regions. A stored NavPath stamps this at plan
	// time so the replan loop can detect "the world rebuilt under me", and a deferred route
	// waits on it to move. Stays 0 until the first mesh lands. Every region's swap bumps it,
	// so no rebuild goes unseen; the stamp is conservative across regions, which only costs
	// an occasional redundant replan.
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
		RegionOwner					  owner;
		std::uint64_t				  meshGeneration; // this region's own swap count
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
	// Half-extent of a colonist's own sim square (center +/- this).
	static constexpr std::int64_t kSimRadiusMm = kMinSimHalfExtentMm; // 30 m
	// Margin applied to the camera's visible half-diagonal for scroll/zoom headroom.
	// GameScene multiplies the half-diagonal by this before calling setViewportRect.
	static constexpr float kViewportMargin = 1.3F;

	// The colonist regions wanted for these colonist positions. Each colonist's square joins every
	// cluster it overlaps (directly or through others) as long as the merged rect stays within
	// 2 * kMaxSimHalfExtentMm on both axes; a square that would push a cluster past that starts or
	// joins another, overlapping one. So no cluster is ever clamped, and every colonist sits at
	// least kSimRadiusMm inside his cluster's rect. Positions are taken in sorted order, so the
	// result depends only on the set of positions.
	[[nodiscard]] static std::vector<DesiredRegion> clusterColonists(std::vector<geometry::Vec2i64> positions);

  private:
	// Per-cluster navmesh + its build state.
	struct SimulationRegion {
		std::int32_t			 id		   = 0;	  // stable across rebuilds; RRA cache key prefix
		RegionOwner				 owner	   = RegionOwner::Colonists;
		geometry::Vec2i64		 center{0, 0};	  // center (mm) of the last build launched; the served mesh may be older
		std::int64_t			 halfExtent = 0;  // half-extent (mm) of the last build launched, likewise
		geometry::nav::NavMesh	 navMesh;		  // current queryable mesh (empty = building)
		std::future<geometry::nav::NavMesh> future; // in-flight build (valid only while running)
		std::uint64_t			 builtVersion			 = UINT64_MAX; // ConstructionWorld::version at launch
		std::uint64_t			 builtAreaChunkSignature = 0;		   // areaChunkSignature at launch
		std::uint64_t			 builtRemovalEpoch		 = UINT64_MAX; // PlacementExecutor::removalEpoch at launch
		std::uint64_t			 meshGeneration			 = 0;		   // bumped on this region's mesh swap
		MeshStamp				 meshStamp;							   // the world edits the served mesh reflects
		// The rect the served mesh was built for (set at its swap), and the one the build in flight was
		// launched for. Mesh queries use meshRect: center/halfExtent move at launch, before the mesh does.
		SimAabb					 meshRect;
		SimAabb					 launchedRect;

		[[nodiscard]] bool hasMesh() const { return !navMesh.triangles.empty(); }
		[[nodiscard]] SimAabb rect() const {
			return {center.x - halfExtent, center.y - halfExtent, center.x + halfExtent, center.y + halfExtent};
		}
	};

	// The desired regions this tick: the colonist clusters (clusterColonists) and, when a viewport
	// was pushed, the camera's own square.
	[[nodiscard]] std::vector<DesiredRegion> computeDesiredRegions() const;

	// Diff desired regions against current ones of the same owner: a desired region whose drivers
	// all sit comfortably (>= kEdgeMarginMm) inside an existing region keeps that region untouched
	// (self-gate -- no rebuild unless its obstacles changed); otherwise recenter/resize the
	// best-overlapping region and rebuild, or spawn a new one. A region whose build is still
	// running recenters once it lands. Regions no longer wanted are dropped. Launches/relaunches
	// async builds where needed.
	void reconcileRegions(const std::vector<DesiredRegion>& desired);

	// Drain any finished region builds (swap meshes in, bump generations, clear that
	// region's RRA caches).
	void drainFinishedBuilds();

	// Move a dropped region's in-flight build into retiringBuilds. First erases any
	// already-finished entries; if the list is still at kMaxRetiringBuilds, waits on the
	// oldest and removes it before appending. Bounds retiringBuilds without blocking the
	// common case -- regions are few, so the cap only bites under pathological camera
	// churn (drop a region, spawn a new one, repeat, every frame).
	void retireBuild(std::future<geometry::nav::NavMesh> build);

	// Launch (or relaunch) the async build for `region` over its current center/extent.
	// Snapshots input on the main thread; the worker owns it by value.
	void launchBuild(SimulationRegion& region);

	// Build a TERRAIN-ONLY navmesh (border + water + walls, no flora entities) over the square area
	// centered at `center` with half-extent `radius` (world mm), for placement validation. Runs
	// synchronously on the caller's (main) thread -- buildInput reads the non-thread-safe
	// ConstructionWorld -- but the footprint-sized area keeps it cheap. Requires chunkManager and
	// constructionWorld; callers guard for null.
	[[nodiscard]] geometry::nav::NavMesh buildTerrainOnlyMesh(geometry::Vec2i64 center, std::int64_t radius) const;

	// The cached terrain-only placement mesh, (re)built to cover [minMm, maxMm] and kept current with
	// the construction world. Reused across a drawing gesture (many vertex/cursor probes plus the
	// commit re-check); rebuilt only when the query leaves the cached area or a wall changes. Requires
	// chunkManager and constructionWorld.
	[[nodiscard]] const geometry::nav::NavMesh& terrainMeshCovering(geometry::Vec2i64 minMm, geometry::Vec2i64 maxMm) const;

	// True if `region` must (re)build: never built, world version changed, a driver
	// nears its edge (handled by the caller before this), or areaChunkSignature changed
	// (an in-area chunk's terrain rings or placement state).
	[[nodiscard]] bool regionObstaclesChanged(const SimulationRegion& region) const;

	// Clamp a requested half-extent to [min, max] and to the loaded-chunk extent.
	[[nodiscard]] std::int64_t clampHalfExtent(std::int64_t requested) const;

	// Hash of what the area's obstacles depend on per chunk (nav::areaChunkRange):
	// each chunk's terrain polygon version (nav::waterSignature) and whether it
	// has finished entity placement. Either part is left out when its source isn't wired.
	[[nodiscard]] std::uint64_t areaChunkSignature(geometry::Vec2i64 center, std::int64_t halfExtent) const;

	// Which regions a query consults: colonist regions only, or every region with colonist ones first.
	enum class RegionScope : std::uint8_t { Colonists, All };

	// Index of the built region that answers a point query at `mm`, or -1: among the built regions in
	// `scope` whose mesh rect holds it, colonist before camera, then the newest MeshStamp, then the
	// lowest id.
	[[nodiscard]] int regionAt(geometry::Vec2i64 mm, RegionScope scope) const;

	// Margin (mm) inside a region rect: a driver closer than this to the edge triggers a
	// recenter. Keeps a colonist comfortably away from the off-mesh boundary.
	static constexpr std::int64_t kEdgeMarginMm = 8000; // 8 m
	// Half-extent fractional change that triggers a region resize (0.20 = 20%). A zoom
	// in/out past this rebuilds the region at the new size; small jitter stays gated.
	static constexpr double kSizeChangeThreshold = 0.20;

	world::ChunkManager*							  chunkManager	   = nullptr;
	assets::PlacementExecutor*						  placement		   = nullptr;
	const std::unordered_set<world::ChunkCoordinate>* processedChunks  = nullptr;
	const construction::ConstructionWorld*			  constructionWorld = nullptr;

	std::vector<SimulationRegion> regions;
	std::int32_t				  nextRegionId = 0;

	// Cap on retiringBuilds. Regions are few, so this only fills under pathological
	// camera churn (drop a region, spawn a new one, repeat, every frame); 2 is enough
	// headroom for normal play to never hit it.
	static constexpr std::size_t kMaxRetiringBuilds = 2;

	// Builds of dropped regions still running. A std::async future blocks in its
	// destructor, so they are kept here until they finish instead of stalling the frame.
	// Bounded to kMaxRetiringBuilds by retireBuild(), which waits on the oldest once the
	// cap is hit rather than letting the list grow without limit.
	std::vector<std::future<geometry::nav::NavMesh>> retiringBuilds;

	// Viewport rect pushed by the scene (world meters). haveViewport is false until the
	// first setViewportRect call (headless tests run colonist-only).
	glm::vec2 viewportCenterM{0.0F, 0.0F};
	glm::vec2 viewportHalfM{0.0F, 0.0F};
	bool	  haveViewport = false;

	// Bumped on every region's mesh swap; see generation().
	std::uint64_t meshGeneration = 0;

	// Cached terrain-only mesh for placement buildability (isAreaBuildable / isPointBuildable). Built
	// on demand over a generous area so a whole drawing gesture reuses one build; invalidated when a
	// query falls outside it or the construction world changes (walls). Mutable: filled by const
	// queries. Main-thread only (DrawingSystem), like the buildInput it wraps.
	static constexpr std::int64_t  kTerrainCacheHalfExtentMm = 48000; // 48 m default gesture area
	static constexpr std::int64_t  kTerrainCacheMarginMm	   = 4000;	 // slack so the footprint sits inside
	mutable geometry::nav::NavMesh terrainMesh_;
	mutable geometry::Vec2i64	   terrainMeshCenter_{0, 0};
	mutable std::int64_t		   terrainMeshHalfExtent_		   = 0;
	mutable std::uint64_t		   terrainMeshConstructionVersion_ = UINT64_MAX;
	mutable std::uint64_t		   terrainMeshChunkSignature_	   = UINT64_MAX;

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
