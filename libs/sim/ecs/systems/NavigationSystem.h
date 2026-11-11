#pragma once

// NavigationSystem - the game's one nav mesh and the queries over it
// (nav-mesh-variable-resolution.md, steps 4 and decision 24).
//
// Two persistent geometry::nav::PatchableMesh instances cover every chunk that has been loaded:
//  - navMesh: each ready chunk's square and water, built walls and doors, and flora around each
//    mover (Position + Colonist) and the viewport. Paths, on-mesh and walkability, recovery
//    snaps and nearby placement read it.
//  - structural: the same without flora. Reachability (its forests), placement buildability and
//    the landing search read it, so neither the camera nor a felled tree changes what is reachable.
// Neither mesh is ever rebuilt. Each update() diffs what should be in them against what is and
// gathers the difference as patches, in a fixed order, under per-tick count and vertex budgets
// (never a time budget, so which tick a ring enters doesn't depend on frame time):
//  1. Felled trees (PlacementExecutor::takeRemovals) and the flora of chunks whose placement
//     went away are queued for removal.
//  2. Walls and doors, diffed by content whenever ConstructionWorld::version moves, all at once.
//  3. Ground: ready chunks whose ground is missing, nearest a mover or the view first, one patch
//     per chunk per mesh: open chunks share a tick up to kGroundVerticesPerTick ring vertices, a
//     chunk with water goes alone. A chunk whose terrain changes has its water re-derived. A chunk
//     that unloads keeps its ground, up to kKeptUnloadedChunks of them; past that the one unloaded
//     longest ago without walls or doors loses it, one chunk a tick, on a tick no ground arrives.
//  4. Flora: the rects scanned for colliders in kFloraCellMm cells; up to kFloraRemovalsPerTick
//     removals and kFloraRingsPerTick adds, one patch per cell, adds nearest a rect centre first.
// endTick() hands the tick's patches to two worker threads, one per mesh, after every system has
// run; each also refreshes its widths or rebuilds its forests. The next update() or the first
// query waits for them, so a query always sees exactly the patches gathered through the previous
// tick, however long the workers took, and nothing reads a mesh while it is patched. The join
// drops the RRA* caches and bumps generation(). Points on meshed ground route to each other
// wherever they are.

#include "../ISystem.h"

#include <core/Vec2i64.h>

#include <nav/NavMesh.h>
#include <nav/PatchableMesh.h>
#include <nav/PathQuery.h>
#include <nav/RraCache.h>

#include <world/chunk/ChunkCoordinate.h>

#include <cstdint>
#include <functional>
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
}

namespace construction {
	class ConstructionWorld;
}

namespace world {
	class ChunkManager;
}

namespace ecs {

/// Holds the nav mesh and its structural layer, patched from live world data, and serves path
/// and placement queries.
///
/// Priority 51: below the AI/goal band (those sit at 50-60), so the patches the previous tick
/// gathered are joined before the goal systems query, and above any system that might mutate
/// the world mid-frame.
class NavigationSystem : public ISystem {
  public:
	// `floraRingBudget` caps the flora rings the nav mesh holds; see kFloraRingBudget.
	explicit NavigationSystem(std::size_t floraRingBudget = kFloraRingBudget);
	~NavigationSystem() override;

	NavigationSystem(const NavigationSystem&) = delete;
	NavigationSystem& operator=(const NavigationSystem&) = delete;

	void update(float deltaTime) override;
	void endTick() override;

	[[nodiscard]] int		  priority() const override { return 51; }
	[[nodiscard]] const char* name() const override { return "NavigationSystem"; }

	// --- Resource injection (setter style; no heavy ctor deps) ----------------

	void setChunkManager(world::ChunkManager* manager) { chunkManager = manager; }

	// The viewport rect (world meters) for this tick, which asks for flora detail like a mover
	// does. GameScene pushes it every frame with the camera's visible rect expanded by
	// kViewportMargin. Without one, only movers ask.
	void setViewportRect(glm::vec2 centerMeters, glm::vec2 halfExtentMeters) {
		viewportCenterM = centerMeters;
		viewportHalfM	= halfExtentMeters;
		haveViewport	= true;
	}

	// Append the positions of the movers (Position + Colonist) that ask for flora detail.
	// The same set anchors chunk streaming: the scene hands it to ChunkManager::update.
	void appendMoverPositions(std::vector<world::WorldPosition>& out) const;

	// Flora comes from `executor`'s chunk indices, and only for chunks in `processed` (the
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
	[[nodiscard]] std::optional<std::vector<glm::vec2>>
	requestPath(glm::vec2 startMeters, glm::vec2 goalMeters, float agentRadiusMeters, geometry::nav::BeliefFilter belief = {}) const;

	// Sound reachability pre-filter on the structural mesh, split at the start so many goals from
	// one start cost one start-side climb: reachFrom locates the start and finds its reach root in
	// the forest (geometry::nav::reachFrom), reaches() tests a goal with a locate and an O(log n)
	// subtree check.
	//   false => DEFINITELY unreachable (disconnected component or bottleneck < disc).
	//   true  => MAYBE reachable. Caller runs requestPath for certainty.
	// A start or goal off meshed ground answers true ("can't prove unreachable").
	//
	// startRegion is 0 when the start is on meshed ground, -1 otherwise; startRegionId is 0 then,
	// and startRegionGeneration counts the joins that patched the structural mesh, so flora
	// arriving or leaving never moves it. A Reach is used within one frame; the belief's sets must
	// outlive it.
	struct Reach {
		int							startRegion			  = -1;
		std::int32_t				startRegionId		  = -1;
		std::uint64_t				startRegionGeneration = 0;
		geometry::nav::ReachFrom	from;
		geometry::nav::BeliefFilter belief;
	};
	[[nodiscard]] Reach reachFrom(glm::vec2 startMeters, float agentRadiusMeters, geometry::nav::BeliefFilter belief = {}) const;
	[[nodiscard]] bool	reaches(const Reach& reach, glm::vec2 goalMeters) const;
	// The start is on meshed ground and so is the goal: reaches() consults the mesh for it.
	[[nodiscard]] bool sharesStartRegion(const Reach& reach, glm::vec2 goalMeters) const;

	// True when `meters` is on meshed ground (inside a ready chunk that has been meshed), water
	// and walls included.
	[[nodiscard]] bool inSimArea(glm::vec2 meters) const;

	// True when `meters` lies on a walkable face of the nav mesh: not water, not a tree; any wall
	// face reads open (terrain-optimistic). False off meshed ground. Lets a path failure tell a
	// stranded colonist (recover by snapping back onto land) from one merely walled off.
	[[nodiscard]] bool isOnMesh(glm::vec2 meters) const;

	// Canonical world-position validity for spawn/placement/teleport: exactly isOnMesh. Validity
	// is owned by the runtime nav mesh; the 3D world source is load-time data and is not consulted.
	// This is the ONE predicate every world-positioning path goes through; do not reintroduce
	// ad-hoc terrain/isWaterAt checks elsewhere.
	[[nodiscard]] bool isValidPosition(glm::vec2 meters) const { return isOnMesh(meters); }

	// Whole-FOOTPRINT validity for an area structure (a foundation polygon). True IFF EVERY part
	// of the closed polygon -- vertices, edges, and interior -- sits on walkable nav mesh: every
	// edge sampled at <= kFootprintSampleStepMm and the interior on a grid of the same pitch. A
	// ring of fewer than 3 points validates only its points.
	[[nodiscard]] bool isAreaWalkable(const std::vector<glm::vec2>& polygonMeters) const;

	// Whole-CENTERLINE validity for a linear structure: both endpoints plus every sample along
	// a->b at <= kFootprintSampleStepMm on walkable mesh.
	[[nodiscard]] bool isSegmentWalkable(glm::vec2 aMeters, glm::vec2 bMeters) const;

	// Every consecutive (pts[i], pts[i+1]) edge passes isSegmentWalkable. One point validates
	// just it; empty is trivially true. Used for the wall tool's chain and the /api/dev/walls verb.
	[[nodiscard]] bool isPolylineWalkable(const std::vector<glm::vec2>& ptsMeters) const;

	// Whole-FOOTPRINT BUILDABILITY: isAreaWalkable's sampling against the structural mesh, where
	// trees and rocks are absent (ConstructionSystem turns them into clear tasks) and every wall
	// face reads open (wall conflicts are ConstructionValidator's job). Only water and ground
	// that isn't meshed block. The placement gate for the build tool.
	[[nodiscard]] bool isAreaBuildable(const std::vector<glm::vec2>& polygonMeters) const;

	// Single-point buildability, the per-vertex / live-cursor counterpart of isAreaBuildable.
	[[nodiscard]] bool isPointBuildable(glm::vec2 meters) const;

	// `meters` itself when it is walkable in truth on the structural mesh (water and built walls
	// block, trees and rocks don't), else the nearest point that is, searched outward over the
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

	// Nearest walkable point to `meters`, searched outward over the mesh from it, or nullopt when
	// it is off meshed ground or no face is walkable. Snaps a stranded (off-mesh, on meshed
	// ground) colonist back onto pathable ground.
	[[nodiscard]] std::optional<glm::vec2> nearestPathablePoint(glm::vec2 meters) const;

	// Like nearestPathablePoint, but the returned point also clears the BUILT-wall collision bands
	// for an agent of radius `agentRadiusMeters` (the halfThickness + radius clearance
	// WallCollisionSystem enforces), so the two systems never push a snapped colonist back and
	// forth (the colonist-wall-trap bug). Falls back to the nearest band-clear walkable triangle
	// interior when the closest pocket is too thin to hold the agent.
	[[nodiscard]] std::optional<glm::vec2> nearestPathablePoint(glm::vec2 meters, float agentRadiusMeters) const;

	// A walkable point at least `minDistMeters` away from `origin`, used to place a thing a short
	// distance OFF the spot that produced it. Deterministic: a single candidate at
	// origin + minDist*dir, then a ring of fixed angles at that radius, then the nearest walkable
	// point pushed out to minDist when that stays on mesh. `preferredDir` biases the first
	// candidate; a zero vector falls back to {1,0}. nullopt only when `origin` is off meshed
	// ground or no face is walkable.
	[[nodiscard]] std::optional<glm::vec2>
	findValidPositionNear(glm::vec2 origin, float minDistMeters = 1.0F, glm::vec2 preferredDir = {0.0F, 0.0F}) const;

	// Where detail is on (spec 4.4): flora cells (kFloraCellMm) the viewport rect has come within
	// kDetailOnMarginMm of, kept on until it moves more than kDetailKeepMarginMm away, so a view edge
	// resting on a cell boundary doesn't flip it. Updated once per update() from the viewport, no
	// mesh involved; off everywhere without a viewport. Flora pushes movers out only where it is on.
	[[nodiscard]] bool detailOnAt(glm::vec2 meters) const;

	// True once any ground is meshed.
	[[nodiscard]] bool hasMesh() const;

	// True while anything is queued or gathered and not yet joined: ground, flora adds or
	// removals, patches waiting for endTick.
	[[nodiscard]] bool isBuilding() const;

	// Gathers every queue with no budget, applies it and waits, repeating until nothing is left:
	// the same patches update() would apply over several ticks. It reads only chunks that are
	// ready now, so a caller settles chunk generation and placement first. For tests and the
	// headless sim, never the game.
	void finishPendingBuilds();

	// Monotonic count of joins that patched either mesh. A stored NavPath stamps it at plan time
	// so the replan loop sees "the mesh changed under me", and a deferred route waits on it.
	[[nodiscard]] std::uint64_t generation() const;

	// Cumulative A* instrumentation since the last resetNavQueryStats() (P3.5).
	struct NavQueryStats {
		std::uint64_t totalQueries		 = 0;
		std::uint64_t totalNodesExpanded = 0;
		std::int64_t  lastNodesExpanded	 = 0;
		std::int64_t  lastPeakOpenSet	 = 0;
	};
	[[nodiscard]] const NavQueryStats& navQueryStats() const { return navStats; }
	void							   resetNavQueryStats() { navStats = {}; }

	// Live RRA* reverse-search caches (one per goal triangle), dropped on every nav-mesh patch.
	[[nodiscard]] std::size_t rraCacheCount() const;

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

	// Flora detail rects. A mover asks for its square (center +/- kSimRadiusMm); the viewport for
	// its rect with each half extent clamped to [kMinSimHalfExtentMm, kMaxSimHalfExtentMm].
	static constexpr std::int64_t kMinSimHalfExtentMm = 30000; // 30 m
	static constexpr std::int64_t kMaxSimHalfExtentMm = 64000; // 64 m
	static constexpr std::int64_t kSimRadiusMm		  = kMinSimHalfExtentMm;
	// Margin GameScene applies to the camera's visible half extents before setViewportRect.
	static constexpr float kViewportMargin = 1.3F;

	// Count budgets per tick, from sim-benchmarks (NavigationSystem.bench.cpp). Flora adds are
	// held so a tick's flora job stays near kJobBudgetMs on the worker; removals are cheaper per
	// ring and mostly a lost chunk's flora far from every mover, so they get their own, larger,
	// budget and never hold adds back.
	static constexpr double		 kJobBudgetMs		   = 2.0;
	static constexpr std::size_t kFloraRingsPerTick	   = 32;
	static constexpr std::size_t kFloraRemovalsPerTick = 128;
	// Open chunks (no water) share a tick until their squares' vertices would pass this; a chunk
	// with water always goes alone. Two open chunks (128 vertices each), so a tick's worst ground
	// job is no longer than one chunk with shoreline at today's costs; three measured longer.
	static constexpr std::size_t kGroundVerticesPerTick = 256;
	// Unloaded chunks whose ground stays meshed, so going back to them meshes nothing. Beyond
	// it, the chunk unloaded longest ago without walls or doors loses its ground.
	static constexpr std::size_t kKeptUnloadedChunks = 16;
	// Detail turns on within this of the viewport rect GameScene pushes (which already carries
	// kViewportMargin), and stays on until the rect is this much farther away.
	static constexpr std::int64_t kDetailOnMarginMm	  = 8000;
	static constexpr std::int64_t kDetailKeepMarginMm = 32000;
	// Flora rings the nav mesh holds before the cells longest out of every wanted rect lose theirs
	// (down to 7/8 of it, so the trim runs now and then rather than every tick). Section 6's
	// estimate is about 1.6 KB a tree, so 40,000 is about 64 MB. Never trims where detail is on,
	// inside the detail keep rect, or in a cell a mover or the view wants.
	static constexpr std::size_t kFloraRingBudget = 40000;
	// A stranded mover is moved forward along its route to the first point this far apart that is
	// walkable with its clearance.
	static constexpr std::int64_t kSnapStepMm = 100;
	// Flora is scanned in square cells of this side, aligned to chunk squares; a cell is scanned
	// once and stays scanned until its chunk's placement goes away.
	static constexpr std::int64_t kFloraCellMm = 16000;
	// Chunk sides enter each mesh split to this (decision 8 in the spec). The structural mesh's
	// spacing sets its triangle count, and so its forest rebuild, apart from the nav mesh's.
	static constexpr std::int64_t kNavMaxBoundsEdgeMm		 = 16000;
	static constexpr std::int64_t kStructuralMaxBoundsEdgeMm = 16000;

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

	// A placed entity by the identity PlacementExecutor removes it by. Ordered chunk first, so
	// one chunk's entries are a contiguous range.
	struct FloraKey {
		world::ChunkCoordinate chunk;
		std::uint32_t		   xBits = 0;
		std::uint32_t		   yBits = 0;
		std::string			   defName;
		bool				   operator<(const FloraKey& o) const;
	};
	struct PendingFlora {
		geometry::nav::NavInputPolygon ring;
		std::int64_t				   cell = 0;
	};
	// A flora ring in the nav mesh and the scan cell that holds its entity.
	struct FloraRing {
		RingId		 ring = geometry::nav::kNoRing;
		std::int64_t cell = 0;
	};

	// A rect's flora cells, inclusive.
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
	void gather(std::size_t addBudget, std::size_t removeBudget, std::size_t groundVertexBudget);
	// Hand `gathered` to the workers. No-op when it is empty.
	void submit();
	// Wait for the workers and, once per job, publish what it changed (generation counters, RRA*
	// caches). Every query and every mesh accessor goes through it.
	void join() const;
	// Hand the joined jobs' ring and door ids back to the bookkeeping that gathered them.
	void collect();
	// Everything gathered so far applied and collected.
	void settle();

	[[nodiscard]] std::vector<Rect> wantedRects() const;
	// The viewport rect, half extents clamped to [kMinSimHalfExtentMm, kMaxSimHalfExtentMm].
	[[nodiscard]] Rect viewportRect() const;
	void			   updateDetail();
	// Spec 4.7: a mover inside flora walks forward along its route to clear ground, or to the
	// nearest walkable point without one.
	void snapMovers();
	void trimFlora(const std::vector<Rect>& rects);
	void							queueRemovedFlora();
	void							dropFloraOfLostChunks();
	[[nodiscard]] bool				floraChunkUsable(world::ChunkCoordinate coord) const;
	void							gatherWalls();
	// Ground in and out: new and reloaded chunks are noted, a chunk whose terrain changed has its
	// water re-derived, then chunks go in, nearest a rect first, within `vertexBudget`, and chunks
	// past the kept-unloaded budget come out. Only chunks meeting `within` go in when it is given,
	// and nothing comes out.
	void						gatherGround(std::size_t vertexBudget, const std::vector<Rect>& rects, const Rect* within);
	void						noteLoadedChunks();
	std::size_t					dropUnloadedGround(std::size_t maxChunks);
	[[nodiscard]] bool			hasStructures(world::ChunkCoordinate coord) const;
	[[nodiscard]] bool			chunkGroundMeshed(world::ChunkCoordinate coord) const;
	void scanFlora(const std::vector<Rect>& rects);
	void gatherFlora(std::size_t addBudget, std::size_t removeBudget, const std::vector<Rect>& rects);

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

	std::map<FloraKey, FloraRing>				floraInMesh;
	std::map<FloraKey, PendingFlora>			floraAdds;
	std::vector<FloraRing>						floraRemovals;
	std::unordered_set<std::int64_t>			scannedCells;
	std::set<world::ChunkCoordinate, ChunkLess> floraChunks; // chunks with a scanned cell
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

	mutable std::uint64_t meshGeneration	   = 0;
	mutable std::uint64_t structuralGeneration = 0;

	mutable std::unordered_map<std::int32_t, geometry::nav::RraCache> rraCaches;
	static constexpr std::size_t									   kMaxRraCaches = 64;

	mutable NavQueryStats navStats;

	std::size_t								floraRingBudget;
	std::uint64_t							tickCount = 0;
	std::unordered_set<std::int64_t>		detailCells;
	std::optional<Rect>						detailFor; // the viewport rect detailCells were made for
	std::unordered_map<std::int64_t, std::uint64_t> cellLastWanted; // tickCount when a scan last wanted the cell
};

} // namespace ecs
