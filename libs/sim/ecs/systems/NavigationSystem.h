#pragma once

// NavigationSystem - the game's one nav mesh and the queries over it
// (nav-mesh-variable-resolution.md, steps 4 and decision 24).
//
// Two persistent geometry::nav::PatchableMesh instances cover every chunk that has been loaded:
//  - navMesh: each ready chunk's square and water, built walls and doors, and detail (placed obstacles) under the
//    viewport. Paths, on-mesh and walkability, recovery snaps and nearby placement read it.
//  - structural: the same without detail. Reachability (its forests), placement buildability and
//    the landing search read it, so neither the camera nor a removed placed obstacle changes what is reachable.
// Neither mesh is ever rebuilt. Each update() diffs what should be in them against what is and
// gathers the difference as patches, in a fixed order, under per-tick count and vertex budgets
// (never a time budget, so which tick a ring enters doesn't depend on frame time):
//  1. Removed placed obstacles (PlacementExecutor::takeRemovals) and the detail of chunks whose placement
//     went away are queued for removal.
//  2. Walls and doors, diffed by content whenever ConstructionWorld::version moves, all at once.
//  3. Ground: ready chunks whose ground is missing, nearest a mover or the view first, one patch
//     per chunk per mesh: open chunks share a tick up to kGroundVerticesPerTick ring vertices, a
//     chunk with water goes alone. A chunk whose terrain changes has its water re-derived. A chunk
//     that unloads keeps its ground. Ground a mover (a colonist, a live route, a waiting goal) has
//     ever held is never dropped; of ground only the camera held, up to kCameraOnlyUnloadedChunks
//     unloaded chunks are kept, and past that the one unloaded longest ago holding no construction
//     loses it, one chunk a tick, on a tick no ground arrives.
//  4. Detail: the rects scanned for colliders in kDetailCellMm cells; up to kDetailRemovalsPerTick
//     removals and kDetailRingsPerTick adds, one patch per cell, adds nearest a rect centre first.
// endTick() hands the tick's patches to two worker threads, one per mesh, after every system has
// run; each also refreshes its widths or rebuilds its forests. The next update() or the first
// query waits for them, so a query always sees exactly the patches gathered through the previous
// tick, however long the workers took, and nothing reads a mesh while it is patched. Points on
// meshed ground route to each other wherever they are.
//
// Detail is priced, not walled, for movement (spec 4.4, 4.7): a placed obstacle in the nav mesh costs
// kDetailPassMultiplier times its length to walk through, on camera or off, so a mover standing in
// one is on valid ground and its route can take it out. Detail decides only what collision and the
// forward snap do. Placement, spawning, drops and construction still treat a placed obstacle as blocked
// (isValidPosition and the walkers built on it).
//
// Staleness (spec 4.6): generation() moves only when a structural job lands (ground in or out,
// water re-derived, walls or doors), which can change any route. Detail landing instead marks
// stale just the routes whose rest crosses the new obstacles; detail leaving only opens ground and
// marks nobody.

#include "../EntityID.h"
#include "../ISystem.h"
#include "../components/SourceReachMemo.h"

#include <core/RectMm.h>
#include <core/Vec2i64.h>

#include <nav/NavMesh.h>
#include <nav/PatchableMesh.h>
#include <nav/PathQuery.h>

#include <world/chunk/ChunkCoordinate.h>
#include <world/chunk/ChunkManager.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <glm/vec2.hpp>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace assets {
	class PlacementExecutor;
	struct PlacedEntity;
}

namespace construction {
	class ConstructionWorld;
}

namespace world {
	class ChunkManager;
}

namespace ecs {

struct NavPath;

/// Holds the nav mesh and its structural layer, patched from live world data, and serves path
/// and placement queries.
///
/// Priority 51: below the AI/goal band (those sit at 50-60), so the patches the previous tick
/// gathered are joined before the goal systems query, and above any system that might mutate
/// the world mid-frame.
class NavigationSystem : public ISystem {
  public:
	// `detailRingBudget` caps the detail rings the nav mesh holds; see kDetailRingBudget.
	explicit NavigationSystem(std::size_t detailRingBudget = kDetailRingBudget);
	~NavigationSystem() override;

	NavigationSystem(const NavigationSystem&) = delete;
	NavigationSystem& operator=(const NavigationSystem&) = delete;

	void update(float deltaTime) override;
	void endTick() override;

	[[nodiscard]] int		  priority() const override { return 51; }
	[[nodiscard]] const char* name() const override { return "NavigationSystem"; }

	// --- Resource injection (setter style; no heavy ctor deps) ----------------

	void setChunkManager(world::ChunkManager* manager) { chunkManager = manager; }

	// The viewport rect (world meters) for this tick, the one thing that asks for detail. GameScene
	// pushes it every frame with the camera's visible rect expanded by kViewportMargin.
	void setViewportRect(glm::vec2 centerMeters, glm::vec2 halfExtentMeters) {
		viewportCenterM = centerMeters;
		viewportHalfM	= halfExtentMeters;
		haveViewport	= true;
	}

	// Append the anchors: every mover the AI drives (Position + Task), the rest of every live route (its
	// remaining waypoints, with points added so no two are more than half the anchor load range
	// apart), and the goal of every task waiting for a mesh. The scene hands them to
	// ChunkManager::update, which keeps their chunks loaded, and their chunks' ground is meshed
	// ahead of the camera's and never dropped.
	void appendAnchors(std::vector<world::WorldPosition>& out) const;

	// Detail comes from `executor`'s chunk indices, and only for chunks in `processed` (the
	// chunks that have finished entity placement; null reads every indexed chunk as finished).
	// Removals the executor made before this call are discarded.
	void setPlacementData(assets::PlacementExecutor* executor, const std::unordered_set<world::ChunkCoordinate>* processed);

	// Walls are diffed against this world from the next update on.
	void setConstructionWorld(const construction::ConstructionWorld* world);

	// --- Queries (each waits for the patches in flight first) -------------------

	// Taut polyline in world meters from start to goal for a disc agent of the given radius, or
	// nullopt when either end is off meshed ground or no route exists.
	//
	// `belief` is applied at query time against the truth mesh: a default (empty) BeliefFilter
	// routes over truth, a filter built from a colonist's known segments/openings routes over
	// what that colonist REMEMBERS. The filter holds pointers into the caller's sets and is
	// consumed synchronously.
	//
	// The route is planned over whatever ground is meshed, for anyone, at any distance. The structural
	// walk under the same belief comes first, as a reach walk (walk()) asks it but with the search from
	// the start carried on to kFarRouteExpansions: where it finds none, neither would the nav search, so
	// there is none. A walk found within the reach budgets is near: the nav search runs (navExpansionCap),
	// and when it gives up in priced ground the route is the structural walk, through the detail at its
	// price. A walk found past them is far: there is no nav search, and that walk is the route, through
	// the detail at its price like the fallback's. When the search runs out of steps there is no route
	// and `gaveUp` says the search gave up, which is not "no route".
	struct PathAnswer {
		std::optional<std::vector<glm::vec2>> route;
		bool								  gaveUp = false;
	};
	[[nodiscard]] PathAnswer requestPath(glm::vec2 startMeters, glm::vec2 goalMeters, float agentRadiusMeters,
										 geometry::nav::BeliefFilter belief = {}) const;
	// The nav search's expansion cap, kNavExpansionFloor and kNavExpansionsPerStructural by default
	// (navExpansionCap); tests set a small one to make it give up.
	void setNavExpansionCap(std::uint32_t floor, std::uint32_t perStructural) {
		navExpansionFloor		   = floor;
		navExpansionsPerStructural = perStructural;
	}
	// The nav search's cap behind a near structural walk whose search took `walkSteps` expansions:
	// the floor plus the per-step allowance, never past kNavExpansionCeiling.
	[[nodiscard]] std::uint32_t navExpansionCap(std::uint32_t walkSteps) const;
	// The budgets of a far question: the goal-side and start-side structural searches and the start-side
	// search a route request carries on, kReachProbeExpansions, kReachExpansionCap and kFarRouteExpansions
	// by default; tests set small ones to make a goal far.
	void setReachBudgets(std::uint32_t probe, std::uint32_t forward, std::uint32_t farRoute) {
		reachProbeExpansions = probe;
		reachExpansionCap	 = forward;
		farRouteExpansions	 = farRoute;
	}

	// What a colonist can reach, and how far it walks there: a walk on the structural mesh (no
	// detail, so the camera changes nothing) under the colonist's belief, over all meshed ground,
	// bounded by the walk asked about. reachFrom notes the start once; reaches() and walk() walk to
	// one goal each.
	//   A start off meshed ground: every goal "maybe" (true; the straight line).
	//   A goal off meshed ground: "maybe" while its chunk is loaded (its ground is on the way, and a
	//   route to it waits for it), unreachable otherwise.
	//   Otherwise three answers: a length, "no", or far. A search from the goal toward the start
	//   (kReachProbeExpansions) answers first: a goal sealed in a pocket runs it dry at once, a definite
	//   no, and one that finds the start has the walk. Only then the search from the start
	//   (kReachExpansionCap); when both give up, the goal is far, which is never "no" (WalkAnswer).
	// startCell (the start, kReachCellMm to a step) and structuralEpoch (moves with every structural
	// job, anywhere) together say when a walk from here still holds. A Reach is used within one
	// frame; the belief's sets must outlive it.
	struct Reach {
		bool						onGround = false;
		geometry::Vec2i64			startMm{0, 0};
		std::int64_t				radiusMm		= 0;
		std::int64_t				startCell		= 0;
		std::uint64_t				structuralEpoch = 0;
		// Every chunk within the anchor load range of the start has its ground meshed. Until then a
		// walk that fails may only be waiting on it: "not now", which is never remembered.
		bool						settled = true;
		geometry::nav::BeliefFilter belief;
	};
	[[nodiscard]] Reach reachFrom(glm::vec2 startMeters, float agentRadiusMeters, geometry::nav::BeliefFilter belief = {}) const;
	[[nodiscard]] bool	reaches(const Reach& reach, glm::vec2 goalMeters) const;
	// The walk from the reach's start to the goal in metres, no longer than maxWalk, or none when it
	// is unreachable or every walk there is longer than maxWalk, or far when the searches gave up.
	[[nodiscard]] WalkAnswer walk(const Reach& reach, glm::vec2 goalMeters, float maxWalk = std::numeric_limits<float>::infinity()) const;
	// Both ends are on meshed ground: a walk between them is the colonist's to remember
	// (SourceReachMemo).
	[[nodiscard]] bool sharesStartRegion(const Reach& reach, glm::vec2 goalMeters) const;
	// The stamp behind a route that failed at `anchor`: the structural content (walls, doors, water)
	// of the chunks within twice the anchor load range of it. Ground arriving or leaving doesn't move
	// it, so nothing the camera loads or drops does.
	[[nodiscard]] std::uint64_t verdictStamp(glm::vec2 anchor) const;
	// The structural content of the chunks in the box spanning `from` and `to`, grown by the anchor
	// load range for a route that bends out of it: what a failed route to a far target was true for.
	[[nodiscard]] std::uint64_t spanStamp(glm::vec2 from, glm::vec2 to) const;
	// `here` is within the anchor load range of `anchor`, per axis: where a failed route recorded at
	// the anchor still holds.
	[[nodiscard]] bool nearAnchor(glm::vec2 anchor, glm::vec2 here) const;
	// The anchor load range of the chunk manager wired in (ChunkManager::kDefaultAnchorLoadRangeMeters
	// with none).
	[[nodiscard]] float anchorLoadRangeMeters() const;
	// A reach's start to this step, for remembering verdicts while a colonist moves a little.
	static constexpr std::int64_t kReachCellMm = 2000;
	// The search budgets, chosen from measurements (nav-gate-result-2.md in the nav notes, 2026-11-11,
	// NavScale.bench.cpp's BM_SearchReach, BM_SearchCases and BM_DefaultBudgets on the Quick Start
	// planet's chunks): a structural step costs 0.29-0.32 us on the main thread, 0.56 us at worst.
	// Change them from new measurements, not by feel.
	//
	// The search from the goal expands at most this many triangles: 0.6 ms, 1.1 ms at worst. Every
	// dead end measured runs dry well inside it (a sealed room in 2, compound rooms in 67-482).
	static constexpr std::uint32_t kReachProbeExpansions = 2000;
	// The search from the start expands at most this many: 1.6 ms, 2.8 ms at worst (with the probe a
	// reach walk stays under 4 ms). Real ground reaches 1.1 km at the median within it; a goal past it
	// is far, which a route request then settles.
	static constexpr std::uint32_t kReachExpansionCap = 5000;
	// A route request carries the search from the start on to this many: 19 ms, 34 ms at worst, once
	// per far target. Every real far goal measured to 10 km needed at most 41,006.
	static constexpr std::uint32_t kFarRouteExpansions = 60000;
	// The nav search behind a near structural walk: the floor plus kNavExpansionsPerStructural for each
	// step of the search that found the walk, up to the ceiling (navExpansionCap). A priced nav step in
	// dense forest costs 0.45 us, 1.48 us at worst.
	// Floor: the dense-forest worst (7,090 steps at 2 km, 5,938 at 500 m) with the far budget's 1.5x headroom.
	static constexpr std::uint32_t kNavExpansionFloor = 11000;
	// Ceiling: 34 ms (the accepted far stall) less both reach searches at worst (7,000 x 0.56 us = 3.9 ms)
	// is 30 ms, 20,300 forest steps at 1.48 us.
	static constexpr std::uint32_t kNavExpansionCeiling		   = 20000;
	static constexpr std::uint32_t kNavExpansionsPerStructural = 16;
	// A route's builtNavVersion once marked stale (detail landed across it, no clear point ahead): a
	// value generation() never takes, so no re-stamp reads it as current.
	static constexpr std::uint64_t kStaleRoute = std::numeric_limits<std::uint64_t>::max();

	// True when `meters` is on meshed ground (inside a ready chunk that has been meshed), water
	// and walls included.
	[[nodiscard]] bool inSimArea(glm::vec2 meters) const;

	// Ground a mover may stand on: a face of the nav mesh a route can cross, which is anything but
	// water, placed obstacles included (they are priced, not walled); any wall face reads open
	// (terrain-optimistic). False off meshed ground. Lets a path failure tell a stranded colonist
	// (recover by snapping back onto land) from one merely walled off.
	[[nodiscard]] bool isOnMesh(glm::vec2 meters) const;

	// Canonical world-position validity for spawn/placement/teleport/drops: ground a mover may stand
	// on that is clear, so not water and not inside a placed obstacle's collider (read from the placement data,
	// whether or not it is in the mesh); any wall face reads open. Validity
	// is owned by the runtime nav mesh; the 3D world source is load-time data and is not consulted.
	// This is the ONE predicate every world-positioning path goes through; do not reintroduce
	// ad-hoc terrain/isWaterAt checks elsewhere.
	[[nodiscard]] bool isValidPosition(glm::vec2 meters) const;

	// Whole-FOOTPRINT validity for an area structure (a foundation polygon). True IFF EVERY part
	// of the closed polygon -- vertices, edges, and interior -- passes isValidPosition: every
	// edge sampled at <= kFootprintSampleStepMm and the interior on a grid of the same pitch. A
	// ring of fewer than 3 points validates only its points.
	[[nodiscard]] bool isAreaWalkable(const std::vector<glm::vec2>& polygonMeters) const;

	// Whole-CENTERLINE validity for a linear structure: both endpoints plus every sample along
	// a->b at <= kFootprintSampleStepMm pass isValidPosition.
	[[nodiscard]] bool isSegmentWalkable(glm::vec2 aMeters, glm::vec2 bMeters) const;

	// Every consecutive (pts[i], pts[i+1]) edge passes isSegmentWalkable. One point validates
	// just it; empty is trivially true. Used for the wall tool's chain and the /api/dev/walls verb.
	[[nodiscard]] bool isPolylineWalkable(const std::vector<glm::vec2>& ptsMeters) const;

	// Whole-FOOTPRINT BUILDABILITY: isAreaWalkable's sampling against the structural mesh, where
	// placed obstacles are absent (ConstructionSystem turns them into clear tasks) and every wall
	// face reads open (wall conflicts are ConstructionValidator's job). Only water and ground
	// that isn't meshed block. The placement gate for the build tool.
	[[nodiscard]] bool isAreaBuildable(const std::vector<glm::vec2>& polygonMeters) const;

	// Single-point buildability, the per-vertex / live-cursor counterpart of isAreaBuildable.
	[[nodiscard]] bool isPointBuildable(glm::vec2 meters) const;

	// `meters` itself when it is walkable in truth on the structural mesh (water and built walls
	// block, placed obstacles don't), else the nearest point that is, searched outward over the
	// mesh from `meters`. nullopt when `meters` is off meshed ground.
	[[nodiscard]] std::optional<glm::vec2> nearestTerrainWalkablePoint(glm::vec2 meters) const;

	// Where a landing party of `count` stands, judged on the structural mesh like
	// nearestTerrainWalkablePoint.
	struct GroupSpawn {
		glm::vec2			   center; // the group's anchor (clearing center, colony origin)
		std::vector<glm::vec2> points; // one per member, in order
	};
	// First brings walls and the ground of the ready chunks the search can touch into the meshes
	// (the landing runs before the first update), and waits for it. The center is the nearest
	// point to `drop` (searched outward in rings) whose whole member ring -- `count` points spaced
	// evenly on a circle of `ringRadiusMeters`, or the center alone for one member -- is clear
	// ground, so the ring never straddles a shore. Clear means walkable at the point and at
	// `clearanceMeters` around it. Every member then stands on its ring point when that point is
	// clear and at least `minSeparationMeters` from the members already placed, else on the
	// nearest spot around it that is. With no chunks wired, the center is `drop` and the points
	// are the raw ring.
	[[nodiscard]] GroupSpawn groupSpawnPoints(glm::vec2 drop, std::size_t count, float ringRadiusMeters, float minSeparationMeters,
											  float clearanceMeters);

	// Sampling pitch (mm) for the whole-footprint / whole-centerline walkability checks.
	// 0.5 m is fine enough to catch a ~1-tile water sliver between two on-land vertices.
	static constexpr std::int64_t kFootprintSampleStepMm = 500; // 0.5 m

	// Nearest clear ground (isValidPosition) to `meters`, searched outward over the mesh from it,
	// or nullopt when it is off meshed ground or no face is clear. Moves a work target off a collider
	// to the ground beside it, and places things.
	[[nodiscard]] std::optional<glm::vec2> nearestPathablePoint(glm::vec2 meters) const;

	// Where a colonist at `from` stands to work a target at `target` (a placed entity): when a
	// collider is placed there, the point just outside it nearest `from` that is clear ground on the
	// structural mesh, its disc clear of every collider, else (an obstacle among others) the nearest that is
	// at least ground a mover crosses; with no collider, the target itself when it is clear ground,
	// else the nearest that is. The same on camera and off. nullopt off meshed ground, or with every
	// point round the collider in water or a wall.
	[[nodiscard]] std::optional<glm::vec2> standPointBeside(glm::vec2 target, glm::vec2 from, float agentRadiusMeters) const;

	// Like nearestPathablePoint (clear ground), but the returned point also clears the BUILT-wall
	// collision bands for an agent of radius `agentRadiusMeters` (the halfThickness + radius
	// clearance WallCollisionSystem enforces), so the two systems never push a snapped colonist back
	// and forth (the colonist-wall-trap bug). Falls back to the nearest band-clear triangle interior
	// when the closest pocket is too thin to hold the agent. Snaps a stranded colonist, and a move
	// order clicked on water or a placed obstacle.
	[[nodiscard]] std::optional<glm::vec2> nearestPathablePoint(glm::vec2 meters, float agentRadiusMeters) const;

	// A clear point (isValidPosition) at least `minDistMeters` away from `origin`, used to place a thing a short
	// distance OFF the spot that produced it. Deterministic: a single candidate at
	// origin + minDist*dir, then a ring of fixed angles at that radius, then the nearest walkable
	// point pushed out to minDist when that stays on mesh. `preferredDir` biases the first
	// candidate; a zero vector falls back to {1,0}. nullopt only when `origin` is off meshed
	// ground or no face is walkable.
	[[nodiscard]] std::optional<glm::vec2>
	findValidPositionNear(glm::vec2 origin, float minDistMeters = 1.0F, glm::vec2 preferredDir = {0.0F, 0.0F}) const;

	// Where detail is on (spec 4.4): detail cells (kDetailCellMm) the viewport rect has come within
	// kDetailOnMarginMm of, kept on until it moves more than kDetailKeepMarginMm away, so a view edge
	// resting on a cell boundary doesn't flip it. Updated once per update() from the viewport, no
	// mesh involved; off everywhere without a viewport. Detail pushes movers out only where it is on.
	[[nodiscard]] bool detailOnAt(glm::vec2 meters) const;

	// True when `path` is a route requestPath gave that walks through detail at its price, on purpose:
	// collision lets its mover through the obstacles and the forward snap leaves it alone while it
	// follows it. Known for a route from the request that made it until its mover drops it.
	[[nodiscard]] bool routeCrossesPricedFaces(const NavPath& path) const;
	// The line comes within `radius` (mm) of a placed obstacle in the nav mesh (its outline, exactly): how a route
	// that isn't the nav search's own (the structural walk it falls back to) learns whether it crosses
	// detail.
	[[nodiscard]] bool passesPlacedObstacle(const std::vector<geometry::Vec2i64>& points, std::int64_t radius) const;

	// True when `entity`, placed in `chunk`, is a detail ring in the nav mesh queries see. Collision
	// pushes out of a placed obstacle only then, so the forward snap has had its turn first and the
	// push and the mesh never disagree about it. Read between update() and endTick().
	[[nodiscard]] bool placedObstacleInNavMesh(world::ChunkCoordinate chunk, const assets::PlacedEntity& entity) const;

	// True once any ground is meshed.
	[[nodiscard]] bool hasMesh() const;

	// True while anything is queued or gathered and not yet joined: ground, detail adds or
	// removals, patches waiting for endTick.
	[[nodiscard]] bool isBuilding() const;

	// Gathers every queue with no budget, applies it and waits, repeating until nothing is left:
	// the same patches update() would apply over several ticks. It reads only chunks that are
	// ready now, so a caller settles chunk generation and placement first. For tests and the
	// headless sim, never the game.
	void finishPendingBuilds();

	// Monotonic count of joins that patched the structural mesh (ground, water, walls, doors): the
	// changes that can move any route. A stored NavPath stamps it at plan time so the replan loop
	// sees "the mesh changed under me", and a deferred route waits on it. Detail changes don't move
	// it; the routes they touch are marked stale one by one (see the file comment).
	[[nodiscard]] std::uint64_t generation() const;

	// Cumulative instrumentation since the last resetNavQueryStats(): the path searches, and the
	// patches the joined jobs applied to each mesh.
	struct NavQueryStats {
		std::uint64_t totalQueries		 = 0;
		std::uint64_t totalNodesExpanded = 0;
		std::int64_t  lastNodesExpanded	 = 0;
		std::int64_t  lastPeakOpenSet	 = 0;
		std::uint64_t navPatches		 = 0;
		std::uint64_t structuralPatches	 = 0;
		// Structural walks (reach and requestPath's first step): those the goal-side search answered,
		// those that went on to the start-side search, the expansions of each, and those that were far.
		std::uint64_t structuralWalks	   = 0;
		std::uint64_t probeAnswered		   = 0;
		std::uint64_t forwardSearches	   = 0;
		std::uint64_t probeExpansions	   = 0;
		std::uint64_t forwardExpansions	   = 0;
		std::uint64_t farWalks			   = 0;
		std::uint64_t routeSearchesGaveUp  = 0;
		// Nav searches behind near walks, their expansions, and those that hit their cap.
		std::uint64_t navSearches		   = 0;
		std::uint64_t navSearchExpansions  = 0;
		std::uint64_t navSearchesCapped	   = 0;
	};
	[[nodiscard]] const NavQueryStats& navQueryStats() const { return navStats; }
	void							   resetNavQueryStats() { navStats = {}; }


	// The meshes, for the debug overlay and tests. Dead triangle slots must be skipped
	// (PatchableMesh::triangleLive). A reference is good until the next update() or endTick().
	[[nodiscard]] const geometry::nav::PatchableMesh& navMesh() const;
	[[nodiscard]] const geometry::nav::PatchableMesh& structuralMesh() const;

	// The meshed ground as one square, or none before any ground is meshed.
	struct RegionView {
		geometry::Vec2i64 center;
		std::int64_t	  halfExtent;
	};
	[[nodiscard]] std::vector<RegionView> builtRegions() const;

	// The detail rect: the viewport's, each half extent clamped to [kMinSimHalfExtentMm, kMaxSimHalfExtentMm].
	static constexpr std::int64_t kMinSimHalfExtentMm = 30000; // 30 m
	static constexpr std::int64_t kMaxSimHalfExtentMm = 64000; // 64 m
	// Margin GameScene applies to the camera's visible half extents before setViewportRect.
	static constexpr float kViewportMargin = 1.3F;

	// Count budgets per tick, from sim-benchmarks (NavigationSystem.bench.cpp). Detail adds are
	// held so a tick's detail job stays near kJobBudgetMs on the worker; removals are cheaper per
	// ring and mostly a lost chunk's detail far from every mover, so they get their own, larger,
	// budget and never hold adds back.
	static constexpr double		 kJobBudgetMs		   = 2.0;
	static constexpr std::size_t kDetailRingsPerTick	   = 32;
	static constexpr std::size_t kDetailRemovalsPerTick = 128;
	// And in cells (one patch each): a patch's width refresh covers its cell and around it, about
	// a millisecond for a 16 m cell densely placed, which outweighs its rings. Adds and removals
	// each touch at most this many cells a tick.
	static constexpr std::size_t kDetailCellsPerTick = 2;
	// Open chunks (no water) share a tick until their squares' vertices would pass this; a chunk
	// with water always goes alone. Eight open chunks (32 vertices each at kMaxBoundsEdgeMm): five
	// measured 3.7 ms against 6.3 for the dearest shoreline chunk alone, so eight stay under it.
	static constexpr std::size_t kGroundVerticesPerTick = 256;
	// Unloaded chunks whose ground only the camera ever held (no mover, live route or waiting goal
	// did) that stay meshed, so going back to them meshes nothing. Beyond it, the one unloaded
	// longest ago that holds nothing of the ConstructionWorld (a wall, opening or foundation,
	// blueprint or built) loses its ground. Ground a mover has held is never dropped.
	static constexpr std::size_t kCameraOnlyUnloadedChunks = 16;
	// Detail turns on within this of the viewport rect GameScene pushes (which already carries
	// kViewportMargin), and stays on until the rect is this much farther away.
	static constexpr std::int64_t kDetailOnMarginMm	  = 8000;
	static constexpr std::int64_t kDetailKeepMarginMm = 32000;
	// Detail rings the nav mesh holds before the cells longest out of the view's rect lose theirs
	// (down to 7/8 of it, so the trim runs now and then rather than every tick). Measured at about
	// 6.4 KB a ring at main's forest density, so 40,000 is about 250 MB (nav-gate-result-2.md).
	// Never trims where detail is on, inside the detail keep rect, or in a cell the view wants.
	static constexpr std::size_t kDetailRingBudget = 40000;
	// A stranded mover is moved forward along its route to the first point this far apart that is
	// walkable with its clearance.
	static constexpr std::int64_t kSnapStepMm = 100;
	// A mover farther than this from the leg of its route it is on (moved by something other than
	// walking it: a teleport, a long shove) has its route marked stale, so it is replanned from
	// where it stands rather than steered straight back at a waypoint through whatever lies between.
	static constexpr std::int64_t kOffRouteMm = 2000;
	// What walking a metre through a placed obstacle costs, in metres of open ground: 50
	// makes a 0.6 m obstacle cost a 30 m detour, so a mover goes round one when it can and through
	// one only when the way round is longer than that.
	static constexpr double kDetailPassMultiplier = 50.0;
	// Detail is scanned in square cells of this side, aligned to chunk squares; a cell is scanned
	// once and stays scanned until its chunk's placement goes away.
	static constexpr std::int64_t kDetailCellMm = 16000;
	// Chunk sides enter both meshes split to this (decision 8 in the spec): eight pieces a side,
	// which is what an open chunk's ground costs to patch in and how many triangle slots it adds.
	static constexpr std::int64_t kMaxBoundsEdgeMm = 64000;

  private:
	using RingId = geometry::nav::RingId;
	using DoorId = geometry::nav::DoorId;

	struct RingPair {
		RingId nav		  = geometry::nav::kNoRing;
		RingId structural = geometry::nav::kNoRing;
	};

	struct ChunkLess {
		bool operator()(const world::ChunkCoordinate& a, const world::ChunkCoordinate& b) const {
			return a.y != b.y ? a.y < b.y : a.x < b.x;
		}
	};

	struct Rect {
		geometry::Vec2i64 lo;
		geometry::Vec2i64 hi;
	};

	// A ready chunk the system has seen, and its ground: kNoRing until gathered.
	struct ChunkGround {
		RingPair			  square;
		std::vector<RingPair> water;
		std::uint32_t		  terrainVersion = 0;
		bool				  loaded		 = true;
		std::uint64_t		  unloadedAt	 = 0; // order of unloading
	};

	struct WallRing {
		geometry::nav::NavInputPolygon polygon;
		RingPair					   ids;
	};
	struct WallDoor {
		geometry::nav::DoorPortal portal;
		DoorId					  nav		 = -1;
		DoorId					  structural = -1;
	};

	// A placed entity by the identity PlacementExecutor removes it by, with the scan cell its
	// position falls in. Ordered chunk first, then cell, so one chunk's entries are a contiguous
	// range and so are one cell's.
	struct DetailKey {
		world::ChunkCoordinate chunk;
		std::int64_t		   cell	 = 0;
		std::uint32_t		   xBits = 0;
		std::uint32_t		   yBits = 0;
		std::string			   defName;
		bool				   operator<(const DetailKey& o) const;
	};
	struct PendingDetail {
		geometry::nav::NavInputPolygon ring;
		std::int64_t				   cell = 0;
	};
	// A detail ring in the nav mesh and the scan cell that holds its entity.
	struct DetailRing {
		RingId		 ring = geometry::nav::kNoRing;
		std::int64_t cell = 0;
	};

	// A rect's detail cells, inclusive.
	struct CellRange {
		std::int64_t minX = 0;
		std::int64_t minY = 0;
		std::int64_t maxX = 0;
		std::int64_t maxY = 0;
		bool		 operator==(const CellRange&) const = default;
	};

	// One mesh's share of a tick: its patches in order, door tag edits, then the width refresh
	// (nav) or forest rebuild (structural). Filled on the main thread, run on that mesh's worker,
	// read back on the main thread after the join.
	struct MeshJob {
		std::vector<geometry::nav::PatchEdits>				  patches;
		// Per patch, on the main thread after the join: the ids of its added rings and its time.
		std::vector<std::function<void(const std::vector<RingId>&, double)>> onPatched;
		std::vector<geometry::nav::DoorPortal>								 doorAdds;
		std::vector<DoorId>													 doorRemoves;
		std::function<void(const std::vector<DoorId>&)>						 onDoorsAdded;
		std::vector<std::pair<glm::vec2, glm::vec2>>						 detailAddBoxes; // each added obstacle's box, metres
		// Structural only: the chunks the job changes, those whose ground arrives, the boxes where it
		// closes ground (a wall, new water, ground leaving), and whether it touches a chunk an anchor holds.
		std::vector<world::ChunkCoordinate>				  structuralChunks;
		std::vector<world::ChunkCoordinate>				  groundChunks;
		std::vector<std::pair<glm::vec2, glm::vec2>>		  blockingBoxes;
		bool												  anchored = false;

		std::vector<std::vector<RingId>> added;
		std::vector<double>				 patchMs;
		std::vector<DoorId>				 doorsAdded;
		double							 ms		  = 0.0; // the whole job
		double							 finishMs = 0.0; // widths or forests

		[[nodiscard]] bool empty() const { return patches.empty() && doorAdds.empty() && doorRemoves.empty(); }
	};

	class Worker;

	static void runJob(geometry::nav::PatchableMesh& mesh, MeshJob& job, bool forests);

	// One tick's patches with the given budgets into the gathered jobs; finishPendingBuilds runs
	// it unbudgeted.
	void gather(std::size_t addBudget, std::size_t removeBudget, std::size_t cellBudget, std::size_t groundVertexBudget);
	// Hand `gathered` to the workers. No-op when it is empty.
	void submit();
	// Wait for the workers and, once per job, publish what it changed (the generation counters).
	// Every query and every mesh accessor goes through it.
	void join() const;
	// Hand the joined jobs' ring and door ids back to the bookkeeping that gathered them.
	void collect();
	// Everything gathered so far applied and collected.
	void settle();

	[[nodiscard]] std::vector<Rect> wantedRects() const;
	// The anchors as of this tick, and the chunks they hold (within the anchor load range).
	void							collectAnchors();
	[[nodiscard]] bool				anchorHolds(world::ChunkCoordinate coord) const;
	// A structural change in the chunks meeting [lo, hi] (metres). Every one moves generation() when
	// an anchor holds one of those chunks, and one that closes ground leaves the routes it crosses
	// stale (restampRoutes). A change of content (a wall, a door, a chunk's water) is counted against
	// the chunks for the reach stamps; ground arriving or leaving is not, since ground in a colonist's
	// square is never dropped and ground outside it changes no walk of its.
	enum class StructuralChange { Opens, Closes, GroundArrives, GroundLeaves };
	void noteStructural(glm::vec2 lo, glm::vec2 hi, StructuralChange change);
	// The chunk's ground is in the meshes (gathered and joined).
	[[nodiscard]] bool groundMeshed(world::ChunkCoordinate coord) const;
	// Every chunk whose interior meets the box's interior has its ground meshed. The same "interiors
	// meet" rule picks the chunks a stamp reads (forChunksMeeting), so a chunk that only touches a
	// box's edge never enters either.
	[[nodiscard]] bool groundMeshedThrough(const geometry::RectMm& box) const;
	static void		   forChunksMeeting(const geometry::RectMm& box, const std::function<void(world::ChunkCoordinate)>& visit);
	// As a join moves generation(), the routes current before it that no closing box crosses are
	// stamped current.
	void restampRoutes(const std::vector<std::pair<glm::vec2, glm::vec2>>& closed) const;
	// The structural content of the chunks the box touches, and with withGround which of them have
	// their ground meshed.
	[[nodiscard]] std::uint64_t			  chunkStamp(const geometry::RectMm& box) const;
	// The structural walk from start to goal, three ways (see Reach): the goal-side search, then the
	// start-side one to `forwardCap`. `gaveUp` when both ran out of steps (to walk(), far); else
	// `walk` holds the answer, its points from start to goal. `expansions` is both searches';
	// `walkExpansions` is the one whose answer `walk` is.
	struct StructuralWalk {
		geometry::nav::PathResult walk;
		bool					  gaveUp		 = false;
		bool					  probeAnswered	 = false;
		std::uint32_t			  expansions	 = 0;
		std::uint32_t			  walkExpansions = 0;
	};
	[[nodiscard]] StructuralWalk structuralWalk(const geometry::Vec2i64& startMm, const geometry::Vec2i64& goalMm, std::int64_t radius,
												const geometry::nav::BeliefFilter& belief, double maxCost, std::uint32_t forwardCap) const;
	[[nodiscard]] static geometry::RectMm boxAround(const geometry::Vec2i64& centerMm, std::int64_t halfMm);
	[[nodiscard]] std::int64_t			  anchorLoadRangeMm() const;
	// The viewport rect, half extents clamped to [kMinSimHalfExtentMm, kMaxSimHalfExtentMm].
	[[nodiscard]] Rect viewportRect() const;
	void			   updateDetail();
	// The collider of the placed obstacle nearest `meters` that holds it, if any.
	// A placed obstacle's collider: its outline's centre and the distance from there to its farthest
	// corner. Tested against the same outline the nav mesh's detail ring has (nav::placedObstacleOutline), in a
	// scratch ring that keeps its storage, so no allocation once warm.
	struct PlacedObstacle {
		glm::vec2 centre{0.0F, 0.0F};
		float	  extent = 0.0F;
	};
	[[nodiscard]] std::optional<PlacedObstacle> placedObstacleAt(glm::vec2 meters, float reachMeters = 0.0F) const;
	// The BeliefFilter a movement query runs with: `belief` with detail priced (the one place).
	[[nodiscard]] static geometry::nav::BeliefFilter pricedFor(geometry::nav::BeliefFilter belief);
	// Marks stale the routes whose rest comes within the mover's radius of a placed obstacle that landed in
	// the joined jobs; a route from a structural walk is re-priced instead (structuralRoutes).
	void markCrossedRoutes();
	// Marks stale the routes whose movers stand more than kOffRouteMm off the leg they are on.
	void markStrayedRoutes();
	// A mover whose task stands on a route failure stamped unsettled (navState CantFindWayTo,
	// SourceReachMemo::kUnsettledStamp) decides again (requestDecision) on the update whose join lands
	// the ground that settles its reach: ground in a chunk within
	// the anchor load range of it, after which every such chunk is meshed. Run after the join, before
	// the AI. Waiting goals need nothing here: their ground landing moves generation(), and the AI
	// retries them on that.
	void wakeUnsettledFailures();
	void forgetDroppedRoutes();
	[[nodiscard]] static std::uint64_t routeKey(const std::vector<glm::vec2>& waypoints);
	using RouteSet = std::unordered_map<std::uint64_t, std::vector<glm::vec2>>;
	[[nodiscard]] static bool holds(const RouteSet& routes, const std::vector<glm::vec2>& waypoints);
	static void				  note(RouteSet& routes, const std::vector<glm::vec2>& waypoints, bool in);
	// Spec 4.7: a mover inside detail moves forward along its route to the first point walkable in
	// truth, never past a wall or water on the way; with none, it stays and its route goes stale.
	// Without a route it steps out to the nearest spot within 4 m, ring by ring, where its disc is
	// clear and the straight step there crosses no wall or water; with none, it stays.
	void snapMovers();
	// Over budget, queues the cells to trim, oldest out of a wanted rect first, then takes them
	// out a few a tick: at most `removeBudget` rings' worth.
	void trimDetail(const std::vector<Rect>& rects, std::size_t removeBudget);
	void							queueRemovedDetail();
	void							dropDetailOfLostChunks();
	[[nodiscard]] bool				detailChunkUsable(world::ChunkCoordinate coord) const;
	void							gatherWalls();
	// Ground in and out: new and reloaded chunks are noted, a chunk whose terrain changed has its
	// water re-derived, then chunks go in, nearest a rect first, within `vertexBudget`, and chunks
	// past the kept-unloaded budget come out. Only chunks meeting `within` go in when it is given,
	// and nothing comes out.
	void						gatherGround(std::size_t vertexBudget, const std::vector<Rect>& rects, const Rect* within);
	void						noteLoadedChunks();
	std::size_t					dropUnloadedGround(std::size_t maxChunks);
	[[nodiscard]] bool			holdsConstruction(world::ChunkCoordinate coord) const;
	[[nodiscard]] bool			chunkGroundMeshed(world::ChunkCoordinate coord) const;
	void scanDetail(const std::vector<Rect>& rects);
	void gatherDetail(std::size_t addBudget, std::size_t removeBudget, std::size_t cellBudget, const std::vector<Rect>& rects);

	world::ChunkManager*							  chunkManager		= nullptr;
	assets::PlacementExecutor*						  placement			= nullptr;
	const std::unordered_set<world::ChunkCoordinate>* processedChunks	= nullptr;
	const construction::ConstructionWorld*			  constructionWorld = nullptr;

	glm::vec2 viewportCenterM{0.0F, 0.0F};
	glm::vec2 viewportHalfM{0.0F, 0.0F};
	bool	  haveViewport = false;

	geometry::nav::PatchableMesh detailMesh;
	geometry::nav::PatchableMesh structural;

	std::map<world::ChunkCoordinate, ChunkGround, ChunkLess> ground;
	std::size_t												  groundPending = 0; // chunks left to come in or go out
	std::uint64_t											  groundVersion = 0; // moves whenever ground comes in, changes or goes out
	std::uint64_t											  unloadTicker	= 0;

	std::uint64_t		  wallsVersion = UINT64_MAX;
	std::vector<WallRing> walls;
	std::vector<WallDoor> doors;

	std::map<DetailKey, DetailRing>				detailInMesh;
	std::map<DetailKey, PendingDetail>			detailAdds;
	std::vector<DetailRing>						detailRemovals;
	std::unordered_set<std::int64_t>			scannedCells;
	std::set<world::ChunkCoordinate, ChunkLess> detailChunks; // chunks with a scanned cell
	std::vector<CellRange>						scannedRanges;
	std::uint64_t								scannedGround = 0;
	std::unordered_set<world::ChunkCoordinate>	scannedProcessed;

	// The patches gathered and not yet handed to the workers, and the ones they are running or
	// have finished but not yet been collected.
	MeshJob gatheredNav;
	MeshJob gatheredStructural;
	MeshJob runningNav;
	MeshJob runningStructural;
	bool		 collected = true;
	mutable bool running   = false;
	mutable bool published = true;

	std::unique_ptr<Worker> navWorker;
	std::unique_ptr<Worker> structuralWorker;

	mutable std::uint64_t structuralGeneration = 0;
	// Every structural job joined, wherever it lands: what a walk can find may have changed (Reach).
	mutable std::uint64_t structuralEpoch = 0;
	// Structural changes per chunk, for the reach stamps, counted at the join.
	mutable std::map<world::ChunkCoordinate, std::uint64_t, ChunkLess> chunkStructuralVersion;
	// The chunks whose ground the joins since the last update landed (wakeUnsettledFailures).
	mutable std::vector<world::ChunkCoordinate> groundLanded;
	mutable std::set<world::ChunkCoordinate, ChunkLess>				 unmeshedReported; // logged once each (groundMeshedThrough)
	mutable geometry::Ring												 colliderScratch;  // placedObstacleAt
	std::vector<glm::vec2>												 anchors;
	std::set<world::ChunkCoordinate, ChunkLess>							 anchorChunks;
	// Every chunk an anchor has held, ever: its ground is never dropped (dropUnloadedGround).
	std::set<world::ChunkCoordinate, ChunkLess> moverHeld;
	// Routes requestPath gave that cross priced faces, by routeKey, each kept whole so a key that two
	// routes share never answers for the wrong one. The latest answer for a route stands, and each
	// update() keeps only the routes some mover holds, so it never outgrows the movers and one
	// tick's queries.
	mutable RouteSet pricedRoutes;
	// Routes requestPath took from a structural walk (a far route, or the walk a near search fell back
	// to), kept as pricedRoutes are. The structural mesh holds no detail, so detail landing on one
	// changes nothing a new request would find: it is re-priced where it stands, never marked stale.
	mutable RouteSet structuralRoutes;
	std::uint32_t									 navExpansionFloor			= kNavExpansionFloor;
	std::uint32_t									 navExpansionsPerStructural = kNavExpansionsPerStructural;
	std::uint32_t									 reachProbeExpansions		= kReachProbeExpansions;
	std::uint32_t									 reachExpansionCap			= kReachExpansionCap;
	std::uint32_t									 farRouteExpansions			= kFarRouteExpansions;
	// The boxes of the placed obstacles that landed in the joined jobs, for markCrossedRoutes.
	std::vector<std::pair<glm::vec2, glm::vec2>> detailLanded;


	mutable NavQueryStats navStats;

	std::size_t								detailRingBudget;
	std::uint64_t							tickCount = 0;
	std::unordered_set<std::int64_t>		detailCells;
	std::optional<Rect>						detailFor; // the viewport rect detailCells were made for
	std::unordered_map<std::int64_t, std::uint64_t> cellLastWanted; // tickCount when a scan last wanted the cell
	std::vector<std::int64_t>						 trimQueue; // cells to trim, the next at the back
};

} // namespace ecs
