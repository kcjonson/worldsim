#include "NavigationSystem.h"
#include "NavigationSystem.test.h"

#include "AIDecisionSystem.h"
#include "../World.h"
#include "../components/Colonist.h"
#include "../components/DecisionTrace.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/Movement.h"
#include "../components/NavPath.h"
#include "../components/Needs.h"
#include "../components/SourceReachMemo.h"
#include "../components/Task.h"
#include "../components/Transform.h"

#include <assets/RecipeRegistry.h>

#include "nav/NavCoords.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <assets/placement/SpatialIndex.h>
#include <construction/ConstructionWorld.h>

#include <nav/PathQuery.h>

#include <world/chunk/ChunkManager.h>
#include <world/chunk/SyntheticWorldSampler.h>
#include <worldgen/sampling/PondNetwork2D.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

using namespace ecs;
using construction::ConstructionWorld;
using construction::FoundationState;
using construction::kInvalidFoundation;
using construction::kInvalidOpening;
using construction::OpeningId;
using construction::SegmentCommitResult;
using construction::SegmentId;
using assets::AssetDefinition;
using assets::AssetRegistry;
using assets::CollisionShapeType;
using assets::ConstructionRegistry;
using assets::PlacedEntity;
using assets::PlacementExecutor;
using assets::SpatialIndex;
using world::ChunkCoordinate;
using geometry::Vec2i64;

namespace {

	// Project root from __FILE__: this file lives at <root>/libs/sim/ecs/systems/.
	std::filesystem::path projectRoot() {
		std::filesystem::path p = __FILE__;
		return p.parent_path().parent_path().parent_path().parent_path().parent_path();
	}

	std::string constructionConfigFolder() {
		return (projectRoot() / "assets" / "config" / "construction").string();
	}

	// Build a built wall segment (every created sub-segment marked Built).
	SegmentId buildWall(ConstructionWorld& cw, Vec2i64 a, Vec2i64 b) {
		SegmentCommitResult r = cw.commitSegment(a, b, "Wood", "Standard", kInvalidFoundation);
		EXPECT_TRUE(r.ok());
		for (SegmentId id : r.createdSegments) {
			cw.setSegmentState(id, FoundationState::Built);
		}
		return r.id;
	}

	// A 4x3 m room of built walls; the south wall hosts an opening at t=0.5 when
	// requested. Returns the south wall's segment id so callers can mutate it later.
	SegmentId buildRoom(ConstructionWorld& cw, bool withOpening, bool pathableOpening) {
		SegmentId south = buildWall(cw, {0, 0}, {4000, 0});
		buildWall(cw, {4000, 0}, {4000, 3000});
		buildWall(cw, {4000, 3000}, {0, 3000});
		buildWall(cw, {0, 3000}, {0, 0});
		if (withOpening) {
			OpeningId op = cw.addOpening(south, 0.5F, pathableOpening ? "Door" : "Window", "Wood");
			EXPECT_NE(op, kInvalidOpening);
			EXPECT_TRUE(cw.setOpeningState(op, FoundationState::Built));
		}
		return south;
	}

	// Both meshes pass their full consistency check.
	void expectMeshesValid(const NavigationSystem& sys) {
		EXPECT_EQ(sys.navMesh().validate(), "") << "nav mesh";
		EXPECT_EQ(sys.structuralMesh().validate(), "") << "structural mesh";
	}

	// Settle, then check both meshes.
	void settle(NavigationSystem& sys) {
		sys.finishPendingBuilds();
		expectMeshesValid(sys);
	}

	// One budgeted tick of the world (the update, then the job its endTick hands over), then check
	// both meshes.
	void step(World& world, const NavigationSystem& sys) {
		world.update(0.016F);
		expectMeshesValid(sys);
	}

	// Standard query endpoints: just outside the south wall, and room center.
	const glm::vec2 kOutside{2.0F, -1.0F};
	const glm::vec2 kInside{2.0F, 1.5F};
	constexpr float kAgentRadius = 0.3F; // 0.3 m radius fits the 0.9 m door

	// Far beyond every chunk the fixtures load (radius 2 around the origin: -1024..1536 m).
	const glm::vec2 kUnmeshed{5000.0F, 5000.0F};

	constexpr uint64_t kWorldSeed = 7;

	std::unique_ptr<world::IWorldSampler> landSampler() {
		return std::make_unique<world::SyntheticWorldSampler>(kWorldSeed, world::SyntheticMap{});
	}

	// Mostly land with one pond: a blob of water centred at (40, 40), radius 8 m wobbled by its
	// rim sinusoids to 5.5..10.5 m (8 m due west, so its west shore is x = 32).
	constexpr worldgen::PondNetwork2D::Pond kPatchPond{.cx = 40.0, .cy = 40.0, .radius = 8.0F, .phaseA = 0.0F, .phaseB = 0.0F, .depth = 120};

	std::unique_ptr<world::IWorldSampler> waterPatchSampler() {
		return std::make_unique<world::SyntheticWorldSampler>(kWorldSeed, world::SyntheticMap{.pond = kPatchPond});
	}

	// The pond's least rim radius, which is how far its center is from the nearest shore.
	float patchPondNearestShore() {
		double least = kPatchPond.radius * 2.0;
		for (int i = 0; i < 3600; ++i) {
			least = std::min(least, worldgen::PondNetwork2D::rimRadiusAt(kPatchPond, 6.283185307179586 * i / 3600.0));
		}
		return static_cast<float>(least);
	}

	// All land but one pond centred on the chunk corner (0, 0), so it spans the four chunks
	// around it. Each chunk clips its navRing piece to its own square, and the pieces meet at
	// the corner.
	std::unique_ptr<world::IWorldSampler> cornerPondSampler() {
		return std::make_unique<world::SyntheticWorldSampler>(
			kWorldSeed,
			world::SyntheticMap{.pond = worldgen::PondNetwork2D::Pond{.cx = 0.0, .cy = 0.0, .radius = 10.0F, .phaseA = 0.7F, .phaseB = 2.1F, .depth = 120}}
		);
	}

	std::unique_ptr<world::ChunkManager> readyChunks(std::unique_ptr<world::IWorldSampler> sampler, glm::vec2 at = {0.0F, 0.0F}, int radius = 2) {
		auto chunks = std::make_unique<world::ChunkManager>(std::move(sampler));
		chunks->setLoadRadius(radius);
		chunks->setUnloadRadius(radius + 2);
		chunks->update({at.x, at.y});
		chunks->finishPendingGeneration();
		return chunks;
	}

	// The ready radius-2 chunk region around the origin. Generating it is most of a test's setup
	// cost and NavigationSystem writes nothing in it, so each sampler's region is generated once
	// per process and shared by every test that wires it.
	world::ChunkManager* landChunks() {
		static const std::unique_ptr<world::ChunkManager> chunks = readyChunks(landSampler());
		return chunks.get();
	}

	world::ChunkManager* waterPatchChunks() {
		static const std::unique_ptr<world::ChunkManager> chunks = readyChunks(waterPatchSampler());
		return chunks.get();
	}

	// A blocking tree definition with a square collider of the given half extent.
	void registerTree(const std::string& defName, float halfExtent) {
		AssetDefinition tree;
		tree.defName					 = defName;
		tree.collision.type				 = CollisionShapeType::Rect;
		tree.collision.halfExtentsMeters = {halfExtent, halfExtent};
		AssetRegistry::Get().registerTestDefinition(tree);
	}

	// Stores `positions` as placed entities of `defName`, each in its own chunk's index, and
	// marks those chunks processed.
	void placeTrees(PlacementExecutor& executor, std::unordered_set<ChunkCoordinate>& processed, const std::string& defName,
					const std::vector<glm::vec2>& positions) {
		std::unordered_map<ChunkCoordinate, assets::AsyncChunkPlacementResult> byChunk;
		for (const glm::vec2 p : positions) {
			const ChunkCoordinate coord = world::worldToChunk({p.x, p.y});
			auto&				  r		= byChunk[coord];
			r.coord						= coord;
			PlacedEntity e;
			e.defName  = defName;
			e.position = p;
			r.spatialIndex.insert(e);
		}
		for (auto& [coord, result] : byChunk) {
			executor.storeChunkResult(std::move(result));
			processed.insert(coord);
		}
	}

	class NavigationSystemTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			ConstructionRegistry::Get().clear();
			ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder()));
		}
		void TearDown() override { ConstructionRegistry::Get().clear(); }

		// Chunks plus a 60 m viewport at the origin, which asks for flora there.
		static void wireArea(NavigationSystem& sys) {
			sys.setChunkManager(landChunks());
			pushArea(sys, {0, 0}, 60000);
		}

		static void pushArea(NavigationSystem& sys, geometry::Vec2i64 centerMm, std::int64_t halfExtentMm) {
			const glm::vec2 centerM{static_cast<float>(centerMm.x) / 1000.0F, static_cast<float>(centerMm.y) / 1000.0F};
			const float		halfM = static_cast<float>(halfExtentMm) / 1000.0F;
			sys.setViewportRect(centerM, glm::vec2{halfM, halfM});
		}

	};

	EntityID addColonist(World& world, glm::vec2 at, const char* name) {
		const EntityID e = world.createEntity();
		world.addComponent<Position>(e, Position{at});
		world.addComponent<Colonist>(e, Colonist{name});
		return e;
	}

} // namespace

// A query before any mesh is built returns nullopt; reaches() can't prove unreachable.
TEST_F(NavigationSystemTest, NoMeshBeforeBuildReturnsNullopt) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();

	EXPECT_FALSE(sys.hasMesh());
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
	EXPECT_TRUE(isReachable(sys, kOutside, kInside, kAgentRadius));
}

// update() with nothing wired is a safe no-op, and a query still returns nullopt.
TEST_F(NavigationSystemTest, UnwiredUpdateIsNoOp) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();

	for (int i = 0; i < 10; ++i) {
		world.update(0.016F);
	}
	EXPECT_FALSE(sys.hasMesh());
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
}

// A path from outside to inside succeeds THROUGH a built door.
TEST_F(NavigationSystemTest, PathThroughDoorSucceeds) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);

	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	std::optional<std::vector<glm::vec2>> path = sys.requestPath(kOutside, kInside, kAgentRadius);
	ASSERT_TRUE(path.has_value());
	EXPECT_GE(path->size(), 2u);
	EXPECT_TRUE(isReachable(sys, kOutside, kInside, kAgentRadius));
}

// A window (non-pathable opening) leaves the wall solid: no path.
TEST_F(NavigationSystemTest, WindowBlocksPath) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/false);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);

	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
}

// No opening at all also blocks the path.
TEST_F(NavigationSystemTest, NoOpeningBlocksPath) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);

	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
}

// Open ground routes; a closed room built over the goal blocks it on the next tick; a door
// cut in the south wall opens it again on the tick after that.
TEST_F(NavigationSystemTest, WallBuiltBlocksAndDoorOpens) {
	ConstructionWorld cw;

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value()) << "open ground routes";

	const SegmentId south = buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);
	step(world, sys);
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value()) << "the walls block in the tick they are seen";
	EXPECT_FALSE(isReachable(sys, kOutside, kInside, kAgentRadius));

	const OpeningId op = cw.addOpening(south, 0.5F, "Door", "Wood");
	ASSERT_NE(op, kInvalidOpening);
	ASSERT_TRUE(cw.setOpeningState(op, FoundationState::Built));
	step(world, sys);
	EXPECT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value()) << "the door opens the room";
	EXPECT_TRUE(isReachable(sys, kOutside, kInside, kAgentRadius));
}

// An open (door) room is pathable; setting the door back to Blueprint makes the band solid
// again and re-seals the room.
TEST_F(NavigationSystemTest, RebuildSealsRoomWhenDoorRemoved) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);

	settle(sys);
	ASSERT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value()) << "door room should be pathable";

	const std::uint64_t before = cw.version();
	ASSERT_FALSE(cw.openings().empty());
	for (const auto& op : cw.openings()) {
		ASSERT_TRUE(cw.setOpeningState(op.id, FoundationState::Blueprint));
	}
	ASSERT_NE(cw.version(), before);

	settle(sys);
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value()) << "blueprinting the door never re-sealed the room";
}

// Pumping update() before anything is wired latches nothing; once wired the meshes build.
TEST_F(NavigationSystemTest, ConstructionWorldWiredAfterUpdatesStillBuilds) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();

	for (int i = 0; i < 10; ++i) {
		world.update(0.016F);
	}
	ASSERT_FALSE(sys.hasMesh());

	wireArea(sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built after deferred wiring";
	EXPECT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
}

// --- Belief-filtered queries -------------------------------------------------
//
// Belief is applied at query time against the one truth mesh. A colonist that knows nothing
// routes optimistically (unseen walls absent); one that knows the enclosing walls (but not a
// door) is blocked.

namespace {
	std::unordered_set<std::uint64_t> allSegmentIds(const ConstructionWorld& cw) {
		std::unordered_set<std::uint64_t> ids;
		for (const auto& seg : cw.segments()) {
			ids.insert(static_cast<std::uint64_t>(seg.id));
		}
		return ids;
	}
} // namespace

TEST_F(NavigationSystemTest, BeliefFilterGatesQueryVsTruth) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	EXPECT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value()) << "truth query should route through the real door";

	const std::unordered_set<std::uint64_t> noSegments;
	const std::unordered_set<std::uint64_t> noOpenings;
	const geometry::nav::BeliefFilter		blindBelief{&noSegments, &noOpenings};
	EXPECT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius, blindBelief).has_value())
		<< "blind belief should optimistically route through the unseen wall";

	const std::unordered_set<std::uint64_t> knownWalls = allSegmentIds(cw);
	const geometry::nav::BeliefFilter		walledBelief{&knownWalls, &noOpenings};
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius, walledBelief).has_value())
		<< "knowing the walls (but not the door) should block the route";

	std::unordered_set<std::uint64_t> knownDoors;
	for (const auto& op : cw.openings()) {
		knownDoors.insert(static_cast<std::uint64_t>(op.id));
	}
	ASSERT_FALSE(knownDoors.empty());
	const geometry::nav::BeliefFilter doorBelief{&knownWalls, &knownDoors};
	EXPECT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius, doorBelief).has_value())
		<< "knowing the walls and the door should route through it";
}

// --- isReachable semantics ---------------------------------------------------
//
// false = DEFINITELY unreachable (sound), true = MAYBE reachable. No mesh => always true.

TEST_F(NavigationSystemTest, IsReachableTrueForReachableGoal) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	EXPECT_TRUE(isReachable(sys, kOutside, kInside, kAgentRadius));
	EXPECT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
}

TEST_F(NavigationSystemTest, IsReachableFalseForWalledOffGoal) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	EXPECT_FALSE(isReachable(sys, kOutside, kInside, kAgentRadius));
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
}

TEST_F(NavigationSystemTest, IsReachableTrueWhenNoMesh) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();

	ASSERT_FALSE(sys.hasMesh());
	EXPECT_TRUE(isReachable(sys, kOutside, kInside, kAgentRadius));
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
}

// A goal or start off meshed ground can't be proven unreachable, even with the start walled in.
TEST_F(NavigationSystemTest, IsReachableTrueForUnmeshedGoal) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	EXPECT_FALSE(isReachable(sys, kOutside, kInside, kAgentRadius)) << "baseline: the closed room blocks";
	EXPECT_TRUE(isReachable(sys, kInside, kUnmeshed, kAgentRadius)) << "an unmeshed goal can't be proven unreachable";
	EXPECT_TRUE(isReachable(sys, kUnmeshed, kInside, kAgentRadius)) << "nor from an unmeshed start";
}

// generation() bumps on the tick that first meshes ground and on every later patch.
TEST_F(NavigationSystemTest, GenerationBumpsOnPatch) {
	ConstructionWorld cw;
	SegmentId		  south = buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);

	EXPECT_EQ(sys.generation(), 0u) << "no mesh yet => generation 0";
	settle(sys);
	const std::uint64_t afterFirst = sys.generation();
	EXPECT_GT(afterFirst, 0u);

	OpeningId op = cw.addOpening(south, 0.5F, "Door", "Wood");
	ASSERT_NE(op, kInvalidOpening);
	ASSERT_TRUE(cw.setOpeningState(op, FoundationState::Built));
	step(world, sys);
	EXPECT_EQ(sys.generation(), afterFirst + 1) << "one tick of patches, one bump";
	step(world, sys);
	EXPECT_EQ(sys.generation(), afterFirst + 1) << "a tick with nothing to patch leaves it";
}

// A Reach on meshed ground has the one region, keyed by generation(): a patch moves its key.
TEST_F(NavigationSystemTest, ReachKeyMovesWithAPatch) {
	ConstructionWorld cw;

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setConstructionWorld(&cw);
	addColonist(world, {0.0F, 0.0F}, "A");
	settle(sys);

	const NavigationSystem::Reach first = sys.reachFrom({0.0F, 0.0F}, kAgentRadius);
	EXPECT_EQ(first.startRegion, 0);
	EXPECT_EQ(first.startRegionId, 0);
	EXPECT_TRUE(sys.sharesStartRegion(first, {300.0F, 0.0F})) << "all meshed ground is one region";
	EXPECT_FALSE(sys.sharesStartRegion(first, kUnmeshed));
	EXPECT_EQ(sys.reachFrom(kUnmeshed, kAgentRadius).startRegion, -1);

	buildWall(cw, {10000, 10000}, {11000, 10000});
	step(world, sys);
	const NavigationSystem::Reach after = sys.reachFrom({0.0F, 0.0F}, kAgentRadius);
	EXPECT_EQ(after.startRegionId, first.startRegionId);
	EXPECT_NE(after.startRegionGeneration, first.startRegionGeneration) << "the patch left the reach key unchanged";
}

// --- RRA* heuristic wiring + instrumentation (P3.5) --------------------------

TEST_F(NavigationSystemTest, NavQueryStatsPopulatedOnPath) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	sys.resetNavQueryStats();
	EXPECT_EQ(sys.navQueryStats().totalQueries, 0u);

	ASSERT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());

	const NavigationSystem::NavQueryStats& s = sys.navQueryStats();
	EXPECT_EQ(s.totalQueries, 1u) << "one reachable solve recorded";
	EXPECT_GT(s.lastNodesExpanded, 0) << "a real solve expands at least one node";
	EXPECT_EQ(s.totalNodesExpanded, static_cast<std::uint64_t>(s.lastNodesExpanded));
	EXPECT_GT(s.lastPeakOpenSet, 0);

	ASSERT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
	EXPECT_EQ(sys.navQueryStats().totalQueries, 2u) << "cumulative query count moves";
	EXPECT_GE(sys.navQueryStats().totalNodesExpanded, s.totalNodesExpanded);
}

// A query that locates its goal creates that goal's RRA* cache; a nav patch drops them all,
// since a patched triangle id can hold a different triangle.
TEST_F(NavigationSystemTest, PatchClearsRraCaches) {
	ConstructionWorld cw;
	SegmentId		  south = buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	settle(sys);

	ASSERT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
	EXPECT_EQ(sys.rraCacheCount(), 1u);

	for (const auto& op : cw.openings()) {
		ASSERT_TRUE(cw.setOpeningState(op.id, FoundationState::Blueprint));
	}
	(void)south;
	step(world, sys);
	EXPECT_EQ(sys.rraCacheCount(), 0u) << "the patch dropped the stale caches";
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
}

// The cache map is bounded: many DISTINCT goal triangles must not grow it past the cap. A grid
// of pillars fragments the mesh so a spread of goal points lands on > 64 distinct triangles.
TEST_F(NavigationSystemTest, RraCacheMapIsBounded) {
	ConstructionWorld cw;
	buildWall(cw, {0, 0}, {14000, 0});
	buildWall(cw, {14000, 0}, {14000, 14000});
	buildWall(cw, {14000, 14000}, {0, 14000});
	buildWall(cw, {0, 14000}, {0, 0});
	for (int px = 0; px < 5; ++px) {
		for (int py = 0; py < 5; ++py) {
			const std::int64_t cx = 2500 + px * 2200;
			const std::int64_t cy = 2500 + py * 2200;
			buildWall(cw, {cx, cy}, {cx + 400, cy});
			buildWall(cw, {cx + 400, cy}, {cx + 400, cy + 400});
			buildWall(cw, {cx + 400, cy + 400}, {cx, cy + 400});
			buildWall(cw, {cx, cy + 400}, {cx, cy});
		}
	}

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	const glm::vec2					 start{1.0F, 1.0F};
	std::unordered_set<std::int32_t> distinctGoalTris;
	for (int gx = 0; gx < 13; ++gx) {
		for (int gy = 0; gy < 13; ++gy) {
			const glm::vec2	   goal{0.7F + static_cast<float>(gx), 0.7F + static_cast<float>(gy)};
			const std::int32_t t = sys.navMesh().locate(nav::toMm(goal));
			if (t >= 0) {
				distinctGoalTris.insert(t);
			}
			(void)sys.requestPath(start, goal, kAgentRadius);
		}
	}

	EXPECT_LE(sys.rraCacheCount(), 64u) << "the cache map must stay within its cap under goal churn";
	EXPECT_GT(distinctGoalTris.size(), 64u) << "test setup must produce > 64 distinct goal triangles to exercise the cap";
}

// --- Flora ---------------------------------------------------------------------

// The viewport's flora rect is clamped: a 1 m request still covers kMinSimHalfExtentMm, and a
// 2 km request stops at kMaxSimHalfExtentMm. Flora enters in whole 16 m cells, so each clamp is
// pinned to within one cell: 30 m reaches the cell [16, 32) and not [32, 48); 64 m reaches
// [64, 80) and not [80, 96).
TEST_F(NavigationSystemTest, ViewportFloraRectIsClamped) {
	ASSERT_EQ(NavigationSystem::kMinSimHalfExtentMm, 30000);
	ASSERT_EQ(NavigationSystem::kMaxSimHalfExtentMm, 64000);
	registerTree("Test_NavClampTree", 0.4F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						insideMin{20.0F, 5.0F};
	const glm::vec2						outsideMin{33.0F, 5.0F};
	const glm::vec2						insideMax{70.0F, 5.0F};
	const glm::vec2						outsideMax{81.0F, 5.0F};
	placeTrees(executor, processed, "Test_NavClampTree", {insideMin, outsideMin, insideMax, outsideMax});

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);

	pushArea(sys, {0, 0}, 1000);
	settle(sys);
	EXPECT_FALSE(sys.isOnMesh(insideMin)) << "the 1 m view still asks for flora within 30 m";
	EXPECT_TRUE(sys.isOnMesh(outsideMin)) << "and no farther";

	pushArea(sys, {0, 0}, 2000000);
	settle(sys);
	EXPECT_FALSE(sys.isOnMesh(insideMax)) << "the 2 km view asks for flora within 64 m";
	EXPECT_TRUE(sys.isOnMesh(outsideMax)) << "and no farther";
}

// Flora enters at most kFloraRingsPerTick rings a tick, nearest the view first, and
// finishPendingBuilds takes in the rest.
TEST_F(NavigationSystemTest, FloraEntersWithinTheBudget) {
	registerTree("Test_NavBudgetTree", 0.3F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	std::vector<glm::vec2>				trees;
	for (int i = 0; i < 10; ++i) {
		for (int j = 0; j < 10; ++j) {
			trees.push_back({1.5F + 3.0F * static_cast<float>(i), 1.5F + 3.0F * static_cast<float>(j)});
		}
	}
	placeTrees(executor, processed, "Test_NavBudgetTree", trees);

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);

	auto inMesh = [&] {
		return static_cast<std::size_t>(std::count_if(trees.begin(), trees.end(), [&](glm::vec2 t) { return !sys.isOnMesh(t); }));
	};
	step(world, sys);
	ASSERT_TRUE(sys.inSimArea({1.0F, 1.0F})) << "the viewport's chunk is meshed first";
	EXPECT_EQ(inMesh(), NavigationSystem::kFloraRingsPerTick) << "the first tick takes in one budget of flora";
	EXPECT_FALSE(sys.isOnMesh({1.5F, 1.5F})) << "nearest the view centre first";
	step(world, sys);
	EXPECT_EQ(inMesh(), 2 * NavigationSystem::kFloraRingsPerTick);
	EXPECT_TRUE(sys.isBuilding());

	settle(sys);
	EXPECT_FALSE(sys.isBuilding());
	EXPECT_EQ(inMesh(), trees.size());
}

// Placement finishing after the ground is meshed brings its flora in.
TEST_F(NavigationSystemTest, LatePlacementBringsItsFloraIn) {
	registerTree("Test_NavLatePlaceTree", 0.4F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	const glm::vec2 treeAt{10.0F, 10.0F};
	ASSERT_TRUE(sys.isOnMesh(treeAt));
	const std::uint64_t before = sys.generation();

	placeTrees(executor, processed, "Test_NavLatePlaceTree", {treeAt});
	settle(sys);
	EXPECT_GT(sys.generation(), before);
	EXPECT_FALSE(sys.isOnMesh(treeAt)) << "the late tree is in the mesh";
}

// A tree is not in the structural mesh, so a footprint over it is buildable while the nav mesh
// carves it.
TEST_F(NavigationSystemTest, FootprintOverTreeIsBuildableButNotOnMesh) {
	registerTree("Test_NavBuildableTree", 0.4F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						treeAt{10.0F, 10.0F};
	placeTrees(executor, processed, "Test_NavBuildableTree", {treeAt});

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "initial navmesh never built";

	const std::vector<glm::vec2> footprint{
		{treeAt.x - 0.3F, treeAt.y - 0.3F},
		{treeAt.x + 0.3F, treeAt.y - 0.3F},
		{treeAt.x + 0.3F, treeAt.y + 0.3F},
		{treeAt.x - 0.3F, treeAt.y + 0.3F},
	};
	EXPECT_FALSE(sys.isAreaWalkable(footprint)) << "the nav mesh carves the tree";
	EXPECT_TRUE(sys.isAreaBuildable(footprint)) << "structural: the tree is clearable, not a blocker";
	EXPECT_TRUE(sys.isPointBuildable(treeAt));
}

// A tree in a room's doorway seals the room; felling it opens the doorway again and patches only
// around the tree: every triangle more than 10 m from it keeps its id and its corners.
TEST_F(NavigationSystemTest, FelledTreePatchesOnlyItsHole) {
	registerTree("Test_NavFellTree", 0.45F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						treeAt{2.0F, 0.65F}; // just inside the 0.9 m door at x = 2
	placeTrees(executor, processed, "Test_NavFellTree", {treeAt, {30.0F, 30.0F}});

	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value()) << "the tree seals the doorway";
	EXPECT_TRUE(isReachable(sys, kOutside, kInside, kAgentRadius)) << "reachability ignores flora";

	struct Kept {
		std::int32_t			   id;
		std::array<Vec2i64, 3>	   corners;
	};
	const geometry::nav::PatchableMesh& mesh = sys.navMesh();
	const Vec2i64						tree = nav::toMm(treeAt);
	std::vector<Kept>					far;
	for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
		if (!mesh.triangleLive(t)) {
			continue;
		}
		Kept k{t, {}};
		bool near = false;
		for (int i = 0; i < 3; ++i) {
			k.corners[i] = mesh.vertices()[mesh.triangles()[static_cast<std::size_t>(t)].v[i]];
			near		 = near || std::max(std::llabs(k.corners[i].x - tree.x), std::llabs(k.corners[i].y - tree.y)) < 10000;
		}
		if (!near) {
			far.push_back(k);
		}
	}
	ASSERT_GT(far.size(), 10u);
	const std::uint64_t before = sys.generation();

	ASSERT_TRUE(executor.removeEntity(world::worldToChunk({treeAt.x, treeAt.y}), treeAt, "Test_NavFellTree"));
	step(world, sys);
	EXPECT_EQ(sys.generation(), before + 1) << "the felled tree is patched out on the next tick";
	EXPECT_FALSE(sys.isBuilding());
	EXPECT_TRUE(sys.isOnMesh(treeAt)) << "the trunk's ground is walkable";
	EXPECT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value()) << "the doorway opens";
	EXPECT_FALSE(sys.isOnMesh({30.0F, 30.0F})) << "the other tree stays";
	for (const Kept& k : far) {
		ASSERT_TRUE(mesh.triangleLive(k.id)) << "triangle " << k.id;
		for (int i = 0; i < 3; ++i) {
			EXPECT_EQ(mesh.vertices()[mesh.triangles()[static_cast<std::size_t>(k.id)].v[i]], k.corners[i]) << "triangle " << k.id;
		}
	}
}

// A flora patch (a tree felled, late placement) moves generation() but not the reach key, which
// counts structural patches only: a colonist's remembered unreachable goals stay remembered while
// flora drains. A wall moves it.
TEST_F(NavigationSystemTest, ReachKeyIgnoresFloraPatches) {
	registerTree("Test_NavReachKeyTree", 0.4F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						treeAt{10.0F, 10.0F};
	placeTrees(executor, processed, "Test_NavReachKeyTree", {treeAt});

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	const NavigationSystem::Reach before	 = sys.reachFrom({0.0F, 0.0F}, kAgentRadius);
	const std::uint64_t			  generation = sys.generation();

	ASSERT_TRUE(executor.removeEntity(world::worldToChunk({treeAt.x, treeAt.y}), treeAt, "Test_NavReachKeyTree"));
	step(world, sys);
	ASSERT_EQ(sys.generation(), generation + 1) << "the felled tree was patched out";
	EXPECT_EQ(sys.reachFrom({0.0F, 0.0F}, kAgentRadius).startRegionGeneration, before.startRegionGeneration)
		<< "a flora patch moved the reach key";

	placeTrees(executor, processed, "Test_NavReachKeyTree", {{-14.0F, 10.0F}}); // late placement in chunk (-1, 0)
	step(world, sys);
	ASSERT_FALSE(sys.isOnMesh({-14.0F, 10.0F})) << "the late tree is in";
	EXPECT_EQ(sys.reachFrom({0.0F, 0.0F}, kAgentRadius).startRegionGeneration, before.startRegionGeneration);

	buildWall(cw, {20000, 20000}, {22000, 20000});
	step(world, sys);
	EXPECT_NE(sys.reachFrom({0.0F, 0.0F}, kAgentRadius).startRegionGeneration, before.startRegionGeneration)
		<< "a wall moves the reach key";
}

// A chunk whose placement goes away queues all its flora for removal. That backlog drains under its
// own budget and never holds back the flora entering ahead of the view.
TEST_F(NavigationSystemTest, LostChunkRemovalsDontHoldBackAdds) {
	registerTree("Test_NavLostTree", 0.3F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	std::vector<glm::vec2>				lost;
	std::vector<glm::vec2>				ahead;
	for (int i = 0; i < 20; ++i) {
		for (int j = 0; j < 10; ++j) {
			lost.push_back({200.0F + 3.0F * static_cast<float>(i), 200.0F + 3.0F * static_cast<float>(j)});
			ahead.push_back({-200.0F - 3.0F * static_cast<float>(i), 200.0F + 3.0F * static_cast<float>(j)});
		}
	}
	ASSERT_GT(lost.size(), NavigationSystem::kFloraRemovalsPerTick);
	placeTrees(executor, processed, "Test_NavLostTree", lost);

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	sys.setViewportRect({228.0F, 214.0F}, {60.0F, 60.0F});
	settle(sys);
	auto inMesh = [&](const std::vector<glm::vec2>& trees) {
		return static_cast<std::size_t>(std::count_if(trees.begin(), trees.end(), [&](glm::vec2 t) { return !sys.isOnMesh(t); }));
	};
	ASSERT_EQ(inMesh(lost), lost.size());

	// The view moves to the other chunk and the first chunk's placement is dropped.
	executor.unloadChunk({0, 0});
	processed.erase(ChunkCoordinate{0, 0});
	placeTrees(executor, processed, "Test_NavLostTree", ahead);
	sys.setViewportRect({-228.0F, 214.0F}, {60.0F, 60.0F});
	step(world, sys);
	EXPECT_EQ(inMesh(ahead), NavigationSystem::kFloraRingsPerTick) << "adds went ahead of the backlog";
	EXPECT_EQ(inMesh(lost), lost.size() - NavigationSystem::kFloraRemovalsPerTick) << "removals drained under their own budget";

	settle(sys);
	EXPECT_EQ(inMesh(lost), 0u);
	EXPECT_EQ(inMesh(ahead), ahead.size());
}

// One chunk's placement leaves and another's arrives in the same tick, so the processed set keeps
// its size: the arriving chunk's flora is still scanned.
TEST_F(NavigationSystemTest, PlacementSwapInOneTickIsScanned) {
	registerTree("Test_NavSwapTree", 0.4F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						leaving{-20.0F, 10.0F}; // chunk (-1, 0)
	const glm::vec2						arriving{20.0F, 10.0F}; // chunk (0, 0)
	placeTrees(executor, processed, "Test_NavSwapTree", {leaving});

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_FALSE(sys.isOnMesh(leaving));
	ASSERT_TRUE(sys.isOnMesh(arriving));

	executor.unloadChunk({-1, 0});
	processed.erase(ChunkCoordinate{-1, 0});
	const std::size_t size = processed.size();
	placeTrees(executor, processed, "Test_NavSwapTree", {arriving});
	ASSERT_EQ(processed.size(), size + 1);
	settle(sys);
	EXPECT_TRUE(sys.isOnMesh(leaving)) << "the leaving chunk's flora is gone";
	EXPECT_FALSE(sys.isOnMesh(arriving)) << "the arriving chunk's flora is in";
}

// A query sees exactly the patches gathered through the previous tick: update() gathers, the job
// starts at endTick, and only then does a query see it.
TEST_F(NavigationSystemTest, QueriesSeePatchesFromEndTickOn) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
	const std::uint64_t generation = sys.generation();

	buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);
	sys.update(0.016F);
	EXPECT_TRUE(sys.isBuilding()) << "the room is gathered";
	EXPECT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value()) << "but not applied within its own tick";
	EXPECT_EQ(sys.generation(), generation);

	sys.endTick();
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value()) << "applied once the tick ended";
	EXPECT_EQ(sys.generation(), generation + 1);
	EXPECT_FALSE(sys.isBuilding());
	expectMeshesValid(sys);
}

// --- One mesh ------------------------------------------------------------------

// WOR-644: two colonists far apart, the camera elsewhere and then over one of them. Their
// ground is one mesh, so a path between them exists and reaches() agrees.
TEST_F(NavigationSystemTest, ColonistsFarApartRouteToEachOther) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setConstructionWorld(&cw);
	const glm::vec2 a{0.0F, 0.0F};
	const glm::vec2 b{150.0F, 20.0F};
	addColonist(world, a, "A");
	addColonist(world, b, "B");

	pushArea(sys, {-300000, 300000}, 40000);
	settle(sys);
	EXPECT_TRUE(sys.requestPath(a, b, kAgentRadius).has_value()) << "camera elsewhere";
	EXPECT_TRUE(isReachable(sys, a, b, kAgentRadius));

	pushArea(sys, {140000, 0}, 40000);
	settle(sys);
	EXPECT_TRUE(sys.requestPath(a, b, kAgentRadius).has_value()) << "camera over one colonist";
	EXPECT_TRUE(isReachable(sys, a, b, kAgentRadius));
	EXPECT_EQ(sys.builtRegions().size(), 1u);
}

// A chunk arriving next to meshed ground joins it: a path crosses the border.
TEST_F(NavigationSystemTest, ArrivingChunkJoinsItsNeighbour) {
	auto chunks = std::make_unique<world::ChunkManager>(landSampler());
	chunks->setLoadRadius(0);
	chunks->setUnloadRadius(8);
	chunks->update({256.0F, 256.0F});
	chunks->finishPendingGeneration();

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	sys.setConstructionWorld(&cw);
	settle(sys);
	const glm::vec2 west{500.0F, 100.0F};
	const glm::vec2 east{530.0F, 100.0F};
	ASSERT_TRUE(sys.inSimArea(west));
	ASSERT_FALSE(sys.inSimArea(east));
	EXPECT_FALSE(sys.requestPath(west, east, kAgentRadius).has_value());

	chunks->update({768.0F, 256.0F});
	chunks->finishPendingGeneration();
	settle(sys);
	EXPECT_TRUE(sys.requestPath(west, east, kAgentRadius).has_value()) << "the path crosses x = 512 m";
	EXPECT_TRUE(isReachable(sys, west, east, kAgentRadius));
}

// A chunk that unloads keeps its ground: still meshed and routable, its flora gone with its
// placement, and nothing rebuilt when it comes back with the same terrain.
TEST_F(NavigationSystemTest, UnloadedChunkKeepsItsGround) {
	registerTree("Test_NavUnloadTree", 0.4F);
	auto chunks = std::make_unique<world::ChunkManager>(landSampler());
	chunks->setLoadRadius(0);
	chunks->setUnloadRadius(0);
	chunks->update({256.0F, 256.0F});
	chunks->finishPendingGeneration();
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						treeAt{256.0F, 256.0F};
	placeTrees(executor, processed, "Test_NavUnloadTree", {treeAt});

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	sys.setViewportRect({256.0F, 256.0F}, {40.0F, 40.0F});
	settle(sys);
	ASSERT_FALSE(sys.isOnMesh(treeAt)) << "the tree is in";

	// The camera moves a chunk east: (0, 0) unloads, as GameScene drops its placement.
	chunks->update({768.0F, 256.0F});
	chunks->finishPendingGeneration();
	ASSERT_EQ(chunks->getChunk({0, 0}), nullptr);
	executor.unloadChunk({0, 0});
	processed.erase(ChunkCoordinate{0, 0});
	settle(sys);
	EXPECT_TRUE(sys.inSimArea(treeAt)) << "the unloaded chunk's ground stays";
	EXPECT_TRUE(sys.isOnMesh(treeAt)) << "its flora left with its placement";
	EXPECT_TRUE(sys.requestPath({100.0F, 100.0F}, {700.0F, 300.0F}, kAgentRadius).has_value()) << "routable across it";

	// Back with the same terrain version: no ground patch.
	const std::uint64_t before = sys.generation();
	chunks->update({256.0F, 256.0F});
	chunks->finishPendingGeneration();
	settle(sys);
	EXPECT_EQ(sys.generation(), before) << "a chunk returning with the same terrain is not meshed again";
}

// The same scenario twice gives the same meshes, slot for slot, and the same paths.
TEST_F(NavigationSystemTest, SameScenarioSameMeshesAndPaths) {
	registerTree("Test_NavDeterminismTree", 0.35F);
	struct Run {
		world::ChunkManager*				 chunks = waterPatchChunks();
		PlacementExecutor					 executor{AssetRegistry::Get()};
		std::unordered_set<ChunkCoordinate>	 processed;
		ConstructionWorld					 cw;
		World								 world;
		NavigationSystem&					 sys = world.registerSystem<NavigationSystem>();
	};
	std::vector<glm::vec2> trees;
	for (int i = 0; i < 12; ++i) {
		for (int j = 0; j < 12; ++j) {
			trees.push_back({-20.0F + 5.3F * static_cast<float>(i), -20.0F + 5.1F * static_cast<float>(j)});
		}
	}
	const std::vector<std::pair<glm::vec2, glm::vec2>> pairs{
		{{-15.0F, -15.0F}, {45.0F, 52.0F}}, {{1.0F, 1.0F}, {28.0F, 40.0F}}, {{-18.0F, 30.0F}, {30.0F, -18.0F}}, {{2.0F, -1.0F}, {2.0F, 1.5F}}};

	auto play = [&](Run& r) {
		placeTrees(r.executor, r.processed, "Test_NavDeterminismTree", trees);
		r.sys.setChunkManager(r.chunks);
		r.sys.setPlacementData(&r.executor, &r.processed);
		r.sys.setConstructionWorld(&r.cw);
		addColonist(r.world, {0.0F, 0.0F}, "A");
		r.sys.setViewportRect({20.0F, 20.0F}, {40.0F, 30.0F});
		for (int i = 0; i < 40; ++i) {
			r.world.update(0.016F);
		}
		buildRoom(r.cw, true, true);
		r.executor.removeEntity(world::worldToChunk({trees[30].x, trees[30].y}), trees[30], "Test_NavDeterminismTree");
		for (int i = 0; i < 40; ++i) {
			r.world.update(0.016F);
		}
		r.sys.finishPendingBuilds();
		expectMeshesValid(r.sys);
	};
	Run first;
	Run second;
	play(first);
	play(second);

	for (const auto& [a, b] : {std::pair{&first.sys.navMesh(), &second.sys.navMesh()}, std::pair{&first.sys.structuralMesh(), &second.sys.structuralMesh()}}) {
		ASSERT_EQ(a->triangles().size(), b->triangles().size());
		ASSERT_EQ(a->liveTriangleCount(), b->liveTriangleCount());
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(a->triangles().size()); ++t) {
			ASSERT_EQ(a->triangleLive(t), b->triangleLive(t)) << "slot " << t;
			if (!a->triangleLive(t)) {
				continue;
			}
			const geometry::nav::NavTriangle& ta = a->triangles()[static_cast<std::size_t>(t)];
			const geometry::nav::NavTriangle& tb = b->triangles()[static_cast<std::size_t>(t)];
			for (int i = 0; i < 3; ++i) {
				EXPECT_EQ(a->vertices()[ta.v[i]], b->vertices()[tb.v[i]]) << "slot " << t;
				EXPECT_EQ(ta.neighbor[i], tb.neighbor[i]) << "slot " << t;
				EXPECT_EQ(ta.edgePairWidthMm[i], tb.edgePairWidthMm[i]) << "slot " << t;
				EXPECT_EQ(ta.edgeOpening[i], tb.edgeOpening[i]) << "slot " << t;
			}
			EXPECT_EQ(ta.faceBlocker, tb.faceBlocker) << "slot " << t;
			EXPECT_EQ(ta.faceOpening, tb.faceOpening) << "slot " << t;
		}
	}
	for (const auto& [from, to] : pairs) {
		const auto pa = first.sys.requestPath(from, to, kAgentRadius);
		const auto pb = second.sys.requestPath(from, to, kAgentRadius);
		ASSERT_EQ(pa.has_value(), pb.has_value());
		if (pa.has_value()) {
			EXPECT_EQ(*pa, *pb);
		}
	}
	EXPECT_EQ(first.sys.generation(), second.sys.generation());
}

// --- Kept-unloaded ground ---------------------------------------------------------

namespace {
	// A camera that loads one chunk at a time (load 0, unload 0), stepping east along chunk row 0.
	struct ChunkWalk {
		std::unique_ptr<world::ChunkManager> chunks = std::make_unique<world::ChunkManager>(landSampler());

		ChunkWalk() {
			chunks->setLoadRadius(0);
			chunks->setUnloadRadius(0);
		}

		static glm::vec2 centre(int i) { return {256.0F + 512.0F * static_cast<float>(i), 256.0F}; }

		void stand(int i) {
			chunks->update({centre(i).x, centre(i).y});
			chunks->finishPendingGeneration();
		}
	};

	std::size_t meshedChunks(const NavigationSystem& sys, int count) {
		std::size_t meshed = 0;
		for (int i = 0; i < count; ++i) {
			meshed += sys.inSimArea(ChunkWalk::centre(i)) ? 1 : 0;
		}
		return meshed;
	}
} // namespace

// Ground stays meshed for at most kKeptUnloadedChunks unloaded chunks; past that the ones unloaded
// longest ago lose it.
TEST_F(NavigationSystemTest, KeptUnloadedGroundIsBounded) {
	ChunkWalk		  walk;
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(walk.chunks.get());
	sys.setConstructionWorld(&cw);

	const int steps = static_cast<int>(NavigationSystem::kKeptUnloadedChunks) + 4;
	for (int i = 0; i < steps; ++i) {
		walk.stand(i);
		settle(sys);
	}
	EXPECT_EQ(meshedChunks(sys, steps), NavigationSystem::kKeptUnloadedChunks + 1) << "the loaded chunk and the kept ones";
	for (int i = 0; i < 3; ++i) {
		EXPECT_FALSE(sys.inSimArea(ChunkWalk::centre(i))) << "chunk " << i << " was unloaded longest ago";
	}
	EXPECT_TRUE(sys.inSimArea(ChunkWalk::centre(steps - 1 - static_cast<int>(NavigationSystem::kKeptUnloadedChunks))));

	// A dropped chunk that loads again meshes again.
	walk.stand(0);
	settle(sys);
	EXPECT_TRUE(sys.inSimArea(ChunkWalk::centre(0)));
}

// A chunk with walls on it keeps its ground however long ago it unloaded.
TEST_F(NavigationSystemTest, WalledChunkKeepsItsGround) {
	ChunkWalk		  walk;
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(walk.chunks.get());
	sys.setConstructionWorld(&cw);

	walk.stand(0);
	buildWall(cw, {200000, 200000}, {204000, 200000});
	settle(sys);
	const int steps = static_cast<int>(NavigationSystem::kKeptUnloadedChunks) + 4;
	for (int i = 1; i < steps; ++i) {
		walk.stand(i);
		settle(sys);
	}
	EXPECT_TRUE(sys.inSimArea(ChunkWalk::centre(0))) << "the walled chunk kept its ground";
	EXPECT_FALSE(sys.inSimArea(ChunkWalk::centre(1))) << "the next oldest went in its place";
	EXPECT_EQ(meshedChunks(sys, steps), NavigationSystem::kKeptUnloadedChunks + 1);
}

// Going back to a chunk whose ground was kept patches nothing.
TEST_F(NavigationSystemTest, RevisitInsideTheBudgetMeshesNothing) {
	ChunkWalk		  walk;
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(walk.chunks.get());
	sys.setConstructionWorld(&cw);

	walk.stand(0);
	settle(sys);
	walk.stand(1);
	settle(sys);
	ASSERT_EQ(walk.chunks->getChunk({0, 0}), nullptr) << "chunk 0 unloaded";
	const std::uint64_t generation = sys.generation();

	walk.stand(0);
	settle(sys);
	EXPECT_EQ(sys.generation(), generation) << "the revisit patched something";
	EXPECT_TRUE(sys.inSimArea(ChunkWalk::centre(0)));
	EXPECT_TRUE(sys.inSimArea(ChunkWalk::centre(1))) << "and the chunk just left is kept";
}

// A tick that patches the structural mesh several times (a chunk's ground and a wall) moves the
// reach key once.
TEST_F(NavigationSystemTest, ReachKeyMovesOncePerStructuralJob) {
	auto chunks = std::make_unique<world::ChunkManager>(landSampler());
	chunks->setLoadRadius(0);
	chunks->setUnloadRadius(8);
	chunks->update({256.0F, 256.0F});
	chunks->finishPendingGeneration();
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	sys.setConstructionWorld(&cw);
	settle(sys);
	const std::uint64_t key = sys.reachFrom({256.0F, 256.0F}, kAgentRadius).startRegionGeneration;

	chunks->update({768.0F, 256.0F});
	chunks->finishPendingGeneration();
	buildWall(cw, {100000, 100000}, {104000, 100000});
	buildWall(cw, {100000, 110000}, {104000, 110000});
	step(world, sys);
	ASSERT_TRUE(sys.inSimArea({768.0F, 256.0F})) << "the new chunk came in this tick";
	EXPECT_EQ(sys.reachFrom({256.0F, 256.0F}, kAgentRadius).startRegionGeneration, key + 1);
}

// --- Meshed ground: inSimArea, validity, water ---------------------------------

TEST_F(NavigationSystemTest, InSimAreaFalseBeforeBuild) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setConstructionWorld(&cw);
	pushArea(sys, {0, 0}, 60000);

	ASSERT_FALSE(sys.hasMesh());
	EXPECT_FALSE(sys.inSimArea({0.0F, 0.0F}));
}

// inSimArea is true on every ready chunk's ground, far from the view, and false off it.
TEST_F(NavigationSystemTest, InSimAreaCoversEveryMeshedChunk) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setConstructionWorld(&cw);
	pushArea(sys, {0, 0}, 60000);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	EXPECT_TRUE(sys.inSimArea({0.0F, 0.0F}));
	EXPECT_TRUE(sys.inSimArea({1000.0F, -900.0F})) << "a loaded chunk far from the view is meshed";
	EXPECT_FALSE(sys.inSimArea(kUnmeshed));
}

TEST_F(NavigationSystemTest, IsValidPositionTrueOnWalkableFaceFalseOnWaterAndOffMesh) {
	ConstructionWorld cw;

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(waterPatchChunks());
	pushArea(sys, {0, 0}, 60000);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	const glm::vec2 land{2.0F, 2.0F};
	ASSERT_TRUE(sys.inSimArea(land));
	EXPECT_TRUE(sys.isValidPosition(land)) << "a walkable land face must be a valid position";

	const glm::vec2 water{40.0F, 40.0F};
	ASSERT_TRUE(sys.inSimArea(water)) << "water is meshed ground";
	EXPECT_FALSE(sys.isOnMesh(water)) << "the water ring must not be a walkable face";
	EXPECT_FALSE(sys.isValidPosition(water));

	ASSERT_FALSE(sys.inSimArea(kUnmeshed));
	EXPECT_FALSE(sys.isValidPosition(kUnmeshed)) << "a point off meshed ground must be invalid";
}

// Only the diagonal chunks around a corner are loaded, so water crossing that corner arrives as
// two pieces touching at the corner alone, and the land around them has a pinched boundary. That
// land must still be walkable (WOR-493), and each piece's water blocks up to the shared corner.
TEST_F(NavigationSystemTest, LandAroundWaterPinchedAtAChunkCornerIsValid) {
	auto chunks = std::make_unique<world::ChunkManager>(cornerPondSampler());
	chunks->setLoadRadius(0);
	chunks->setUnloadRadius(8);
	chunks->update({100.0F, 100.0F});	// chunk (0, 0)
	chunks->update({-100.0F, -100.0F}); // chunk (-1, -1)
	chunks->finishPendingGeneration();
	ASSERT_EQ(chunks->getChunk({0, -1}), nullptr);
	ASSERT_EQ(chunks->getChunk({-1, 0}), nullptr);

	for (const ChunkCoordinate coord : {ChunkCoordinate{0, 0}, ChunkCoordinate{-1, -1}}) {
		const world::Chunk* chunk = chunks->getChunk(coord);
		ASSERT_NE(chunk, nullptr);
		ASSERT_TRUE(chunk->isReady());
		bool touchesCorner = false;
		for (const world::TerrainRing& ring : chunk->terrainPolygons().navRings) {
			touchesCorner = touchesCorner || (ring.blocksMovement && std::find(ring.ring.begin(), ring.ring.end(), Vec2i64{0, 0}) != ring.ring.end());
		}
		ASSERT_TRUE(touchesCorner) << "chunk (" << coord.x << ", " << coord.y << ") has no water piece ending at (0, 0)";
	}

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	pushArea(sys, {5330, 4200}, 64000);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	for (const glm::vec2 land : {glm::vec2{15.0F, 15.0F}, glm::vec2{-15.0F, -15.0F}, glm::vec2{40.0F, 30.0F}, glm::vec2{-30.0F, -20.0F}}) {
		EXPECT_TRUE(sys.isValidPosition(land)) << "land at (" << land.x << ", " << land.y << ") must be a valid position";
	}
	for (const glm::vec2 water : {glm::vec2{3.0F, 3.0F}, glm::vec2{-3.0F, -3.0F}}) {
		EXPECT_FALSE(sys.isValidPosition(water)) << "the pond at (" << water.x << ", " << water.y << ") must not be";
	}
	for (const glm::vec2 unloaded : {glm::vec2{3.0F, -3.0F}, glm::vec2{-3.0F, 3.0F}}) {
		EXPECT_FALSE(sys.inSimArea(unloaded)) << "a chunk never loaded has no ground";
	}
}

// Two neighbours' water meets along their shared side: the pond across x = 0 and y = 0, with all
// four chunks around it meshed, blocks on both sides of each border and leaves no land seam.
TEST_F(NavigationSystemTest, WaterAcrossChunkBordersHasNoSeam) {
	auto			  chunks = readyChunks(cornerPondSampler(), {0.0F, 0.0F}, 1);
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	sys.setConstructionWorld(&cw);
	settle(sys);

	for (const float along : {-3.0F, -1.0F, 1.0F, 3.0F}) {
		for (const float off : {-0.002F, 0.0F, 0.002F}) {
			EXPECT_FALSE(sys.isValidPosition({off, along})) << "seam on x = 0 at y = " << along;
			EXPECT_FALSE(sys.isValidPosition({along, off})) << "seam on y = 0 at x = " << along;
		}
	}
	EXPECT_FALSE(isReachable(sys, {-30.0F, 0.0F}, {0.5F, 0.5F}, kAgentRadius)) << "the pond's middle is water on every side";
	EXPECT_TRUE(sys.requestPath({-30.0F, 0.0F}, {30.0F, 0.0F}, kAgentRadius).has_value()) << "around the pond, across both borders";
}

// --- Landing and placement on the structural mesh -------------------------------

// Before any mover or view, nothing is meshed; once the ground is in, a drop point in the water
// moves to the nearest shore, and a land point stays put.
TEST_F(NavigationSystemTest, NearestTerrainWalkablePointLeavesWater) {

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	EXPECT_FALSE(sys.nearestTerrainWalkablePoint({40.0F, 40.0F}).has_value()) << "no ground meshed";
	sys.setChunkManager(waterPatchChunks());
	settle(sys);

	const glm::vec2				   land{2.0F, 2.0F};
	const std::optional<glm::vec2> stay = sys.nearestTerrainWalkablePoint(land);
	ASSERT_TRUE(stay.has_value());
	EXPECT_EQ(*stay, land);

	const float					   shore = patchPondNearestShore();
	const std::optional<glm::vec2> moved = sys.nearestTerrainWalkablePoint({40.0F, 40.0F});
	ASSERT_TRUE(moved.has_value());
	const float d = glm::distance(*moved, glm::vec2{40.0F, 40.0F});
	EXPECT_GT(d, shore - 0.5F) << "the snap must leave the water";
	EXPECT_LT(d, shore + 1.5F) << "the snap must land on the nearest shore, not somewhere far";
	const std::optional<glm::vec2> again = sys.nearestTerrainWalkablePoint(*moved);
	ASSERT_TRUE(again.has_value());
	EXPECT_EQ(*again, *moved) << "the snapped point is itself walkable terrain";
}

// A party landing whose drop is in the water beside a shore: groupSpawnPoints meshes the ground
// it needs itself (the landing runs before the first update), and every member spawns on walkable
// ground of its own, apart from the others, and near the drop.
TEST_F(NavigationSystemTest, GroupSpawnPointsFromWaterDropAllLandOnWalkableGround) {

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(waterPatchChunks());

	// The pond's west shore is x = 32; (34, 40) is ~2 m in from it.
	const glm::vec2	 drop{34.0F, 40.0F};
	constexpr size_t kCrew		 = 6;
	constexpr float	 kRing		 = 1.5F;
	constexpr float	 kSeparation = 0.8F;
	constexpr float	 kClearance	 = 0.3F;
	const NavigationSystem::GroupSpawn spawn = sys.groupSpawnPoints(drop, kCrew, kRing, kSeparation, kClearance);
	expectMeshesValid(sys);
	ASSERT_EQ(spawn.points.size(), kCrew);
	ASSERT_NE(sys.nearestTerrainWalkablePoint(drop), std::optional<glm::vec2>(drop)) << "the drop must be in the water";

	ConstructionWorld cw;
	sys.setConstructionWorld(&cw);
	pushArea(sys, {40000, 40000}, 40000);
	settle(sys);

	EXPECT_TRUE(sys.isValidPosition(spawn.center)) << "the clearing center is on land";
	for (std::size_t i = 0; i < spawn.points.size(); ++i) {
		const glm::vec2 p = spawn.points[i];
		EXPECT_TRUE(sys.isValidPosition(p)) << "member " << i << " at (" << p.x << ", " << p.y << ") is not walkable";
		for (int k = 0; k < 8; ++k) {
			const float angle = 6.2831853F * static_cast<float>(k) / 8.0F;
			EXPECT_TRUE(sys.isValidPosition(p + glm::vec2{std::cos(angle), std::sin(angle)} * kClearance)) << "member " << i << "'s disc reaches the water";
		}
		for (std::size_t j = 0; j < i; ++j) {
			EXPECT_GE(glm::distance(p, spawn.points[j]), kSeparation - 1e-4F) << "members " << j << " and " << i << " stack";
		}
		EXPECT_LT(glm::distance(p, drop), 8.0F) << "member " << i << " landed far away";
	}

	const NavigationSystem::GroupSpawn crowd = sys.groupSpawnPoints(drop, 12, kRing, kSeparation, kClearance);
	ASSERT_EQ(crowd.points.size(), 12U);
	for (std::size_t i = 0; i < crowd.points.size(); ++i) {
		EXPECT_TRUE(sys.isValidPosition(crowd.points[i]));
		for (std::size_t j = 0; j < i; ++j) {
			EXPECT_GE(glm::distance(crowd.points[i], crowd.points[j]), kSeparation - 1e-4F);
		}
	}
}

// nearestTerrainWalkablePoint reads truth on the structural mesh: a point dead-center in a solid
// wall span is pushed out past the half-thickness; a point in a pathable door stays.
TEST_F(NavigationSystemTest, NearestTerrainWalkablePointStepsOutOfBuiltWallBand) {
	constexpr float kWallHalfThickness = 0.10F; // Wood/Standard, from materials.xml

	ConstructionWorld cw;
	SegmentId		  wall = buildWall(cw, {0, 0}, {6000, 0});
	OpeningId		  door = cw.addOpening(wall, 0.5F, "Door", "Wood"); // t=0.5 -> x=3.0 m
	ASSERT_NE(door, kInvalidOpening);
	ASSERT_TRUE(cw.setOpeningState(door, FoundationState::Built));

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setConstructionWorld(&cw);
	settle(sys);

	const glm::vec2				   solid = {1.0F, 0.0F};
	const std::optional<glm::vec2> moved = sys.nearestTerrainWalkablePoint(solid);
	ASSERT_TRUE(moved.has_value());
	EXPECT_GE(std::abs(moved->y - solid.y), kWallHalfThickness) << "the snap must clear the solid wall's band";

	const glm::vec2				   atDoor	   = {3.0F, 0.0F};
	const std::optional<glm::vec2> throughDoor = sys.nearestTerrainWalkablePoint(atDoor);
	ASSERT_TRUE(throughDoor.has_value());
	EXPECT_EQ(*throughDoor, atDoor) << "a pathable door opening must stay walkable";
}

// groupSpawnPoints brings built walls in with the ground, so no member lands inside a band even
// with the drop centered on the wall and no update run yet.
TEST_F(NavigationSystemTest, GroupSpawnPointsNeverLandsInsideBuiltWallBand) {
	constexpr float kWallHalfThickness = 0.10F;

	ConstructionWorld cw;
	buildWall(cw, {5000, -8000}, {5000, 8000});

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setConstructionWorld(&cw);

	const glm::vec2	 drop{5.0F, 0.0F};
	constexpr size_t kCrew		 = 8;
	constexpr float	 kRing		 = 1.0F;
	constexpr float	 kSeparation = 0.4F;
	constexpr float	 kClearance	 = 0.3F;
	const NavigationSystem::GroupSpawn spawn = sys.groupSpawnPoints(drop, kCrew, kRing, kSeparation, kClearance);
	ASSERT_EQ(spawn.points.size(), kCrew);

	auto insideBand = [&](glm::vec2 p) { return std::abs(p.x - 5.0F) < kWallHalfThickness; };
	EXPECT_FALSE(insideBand(spawn.center)) << "the clearing center must not sit inside the wall";
	for (std::size_t i = 0; i < spawn.points.size(); ++i) {
		const glm::vec2 p = spawn.points[i];
		EXPECT_FALSE(insideBand(p)) << "member " << i << " at (" << p.x << ", " << p.y << ") landed inside the wall";
		for (int k = 0; k < 8; ++k) {
			const float angle = 6.2831853F * static_cast<float>(k) / 8.0F;
			EXPECT_FALSE(insideBand(p + glm::vec2{std::cos(angle), std::sin(angle)} * kClearance)) << "member " << i << "'s clearance disc reaches into the wall";
		}
	}
}

// --- Whole-footprint walkability: isAreaWalkable / isSegmentWalkable -------------
//
// Vertex-only validity is insufficient: a footprint can have every corner on land yet span water,
// or fully enclose a pond. All use waterPatchSampler()'s pond at (40,40), at most 10.5 m from its
// center to any shore.

namespace {
	void wireWaterPatch(NavigationSystem& sys, ConstructionWorld& cw) {
		sys.setChunkManager(waterPatchChunks());
		sys.setViewportRect(glm::vec2{0.0F, 0.0F}, glm::vec2{60.0F, 60.0F});
		sys.setConstructionWorld(&cw);
	}
} // namespace

TEST_F(NavigationSystemTest, IsAreaWalkableTrueWhenWhollyOnLand) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys	 = world.registerSystem<NavigationSystem>();
	wireWaterPatch(sys, cw);
	settle(sys);

	const std::vector<glm::vec2> onLand{{4.0F, 4.0F}, {8.0F, 4.0F}, {8.0F, 8.0F}, {4.0F, 8.0F}};
	for (const auto& v : onLand) {
		ASSERT_TRUE(sys.isValidPosition(v));
	}
	EXPECT_TRUE(sys.isAreaWalkable(onLand)) << "a footprint wholly on land must be walkable";
}

TEST_F(NavigationSystemTest, IsAreaWalkableFalseWhenFootprintSpansWater) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys	 = world.registerSystem<NavigationSystem>();
	wireWaterPatch(sys, cw);
	settle(sys);

	const std::vector<glm::vec2> spanning{{28.0F, 38.0F}, {52.0F, 38.0F}, {52.0F, 42.0F}, {28.0F, 42.0F}};
	ASSERT_TRUE(sys.isValidPosition(spanning[0]));
	ASSERT_TRUE(sys.isValidPosition(spanning[1]));
	ASSERT_FALSE(sys.isValidPosition({40.0F, 40.0F}));
	EXPECT_FALSE(sys.isAreaWalkable(spanning)) << "a footprint spanning water must be rejected";
}

TEST_F(NavigationSystemTest, IsAreaWalkableFalseWhenFootprintEnclosesWaterPond) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys	 = world.registerSystem<NavigationSystem>();
	wireWaterPatch(sys, cw);
	settle(sys);

	const std::vector<glm::vec2> enclosing{{28.0F, 28.0F}, {52.0F, 28.0F}, {52.0F, 52.0F}, {28.0F, 52.0F}};
	for (const auto& v : enclosing) {
		ASSERT_TRUE(sys.isValidPosition(v));
	}
	ASSERT_FALSE(sys.isValidPosition({40.0F, 40.0F}));
	EXPECT_FALSE(sys.isAreaWalkable(enclosing)) << "a footprint enclosing a water pond must be rejected";
}

// Water is geography, not a clearable entity, so it blocks buildability too.
TEST_F(NavigationSystemTest, WaterBlocksBuildabilityUnlikeTrees) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys	 = world.registerSystem<NavigationSystem>();
	wireWaterPatch(sys, cw);
	settle(sys);

	const std::vector<glm::vec2> spanning{{28.0F, 38.0F}, {52.0F, 38.0F}, {52.0F, 42.0F}, {28.0F, 42.0F}};
	const std::vector<glm::vec2> enclosing{{28.0F, 28.0F}, {52.0F, 28.0F}, {52.0F, 52.0F}, {28.0F, 52.0F}};
	const std::vector<glm::vec2> onLand{{4.0F, 4.0F}, {8.0F, 4.0F}, {8.0F, 8.0F}, {4.0F, 8.0F}};

	EXPECT_FALSE(sys.isAreaBuildable(spanning));
	EXPECT_FALSE(sys.isAreaBuildable(enclosing));
	EXPECT_FALSE(sys.isPointBuildable({40.0F, 40.0F}));
	EXPECT_TRUE(sys.isAreaBuildable(onLand));
	EXPECT_TRUE(sys.isPointBuildable({6.0F, 6.0F}));
	EXPECT_FALSE(sys.isPointBuildable(kUnmeshed)) << "ground that isn't meshed is not buildable";
}

TEST_F(NavigationSystemTest, IsSegmentAndPolylineWalkableCatchWaterCrossing) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys	 = world.registerSystem<NavigationSystem>();
	wireWaterPatch(sys, cw);
	settle(sys);

	EXPECT_TRUE(sys.isSegmentWalkable({4.0F, 4.0F}, {8.0F, 4.0F}));
	EXPECT_FALSE(sys.isSegmentWalkable({28.0F, 40.0F}, {52.0F, 40.0F})) << "a segment crossing water must be rejected";
	const std::vector<glm::vec2> chain{{4.0F, 4.0F}, {8.0F, 4.0F}, {28.0F, 40.0F}, {52.0F, 40.0F}};
	EXPECT_FALSE(sys.isPolylineWalkable(chain));
	const std::vector<glm::vec2> landChain{{4.0F, 4.0F}, {8.0F, 4.0F}, {8.0F, 8.0F}};
	EXPECT_TRUE(sys.isPolylineWalkable(landChain));
}

TEST_F(NavigationSystemTest, IsValidPositionFalseBeforeAnyMesh) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();

	ASSERT_FALSE(sys.hasMesh());
	EXPECT_FALSE(sys.isValidPosition({0.0F, 0.0F}));
}

// Dev-spawn refusal contract: every dev verb that places an entity guards on isValidPosition and,
// on false, creates NOTHING (DevCommandHandler::devSpawn's guard, exercised here).
TEST_F(NavigationSystemTest, DevSpawnGuardRefusesOffMeshCreatesOnValid) {
	ConstructionWorld cw;

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	auto guardedSpawn = [&](glm::vec2 at) -> bool {
		if (!sys.isValidPosition(at)) {
			return false;
		}
		const EntityID e = world.createEntity();
		world.addComponent<Position>(e, Position{at});
		return true;
	};
	auto entityCount = [&] {
		std::size_t n = 0;
		for (auto row : world.view<Position>()) {
			(void)row;
			++n;
		}
		return n;
	};

	const std::size_t before = entityCount();
	EXPECT_FALSE(guardedSpawn(kUnmeshed)) << "off-mesh dev-spawn must be refused";
	EXPECT_EQ(entityCount(), before);
	EXPECT_TRUE(guardedSpawn({2.0F, 2.0F})) << "on-mesh dev-spawn must be accepted";
	EXPECT_EQ(entityCount(), before + 1);
}

// --- Memory::beliefVersion bump semantics ------------------------------------

TEST(MemoryBeliefVersionTest, RememberAndForgetBumpVersion) {
	Memory				mem;
	const std::uint64_t v0 = mem.beliefVersion;

	EXPECT_TRUE(mem.rememberSegment(7));
	const std::uint64_t v1 = mem.beliefVersion;
	EXPECT_GT(v1, v0) << "first discovery should bump";

	EXPECT_FALSE(mem.rememberSegment(7));
	EXPECT_EQ(mem.beliefVersion, v1) << "re-remembering a known id must not bump";

	EXPECT_TRUE(mem.rememberOpening(3));
	const std::uint64_t v2 = mem.beliefVersion;
	EXPECT_GT(v2, v1) << "opening discovery should bump";

	EXPECT_FALSE(mem.rememberOpening(3));
	EXPECT_EQ(mem.beliefVersion, v2) << "re-remembering a known opening must not bump";

	mem.forgetSegment(7);
	const std::uint64_t v3 = mem.beliefVersion;
	EXPECT_GT(v3, v2) << "forgetting a known segment should bump";

	mem.forgetSegment(7);
	EXPECT_EQ(mem.beliefVersion, v3) << "forgetting an absent id must not bump";

	mem.clear();
	EXPECT_GT(mem.beliefVersion, v3) << "clear() should bump (monotonic), never reset";
}

// --- NavPath staleness predicate ---------------------------------------------

namespace {
	// Mirror of the predicate in AIDecisionSystem::update's replan-on-discovery loop.
	bool navPathStale(const NavPath& path, std::uint64_t beliefVersion, std::uint64_t navGeneration) {
		return path.builtBeliefVersion != beliefVersion || path.builtNavVersion != navGeneration;
	}
} // namespace

TEST(NavPathStalenessTest, DetectsBeliefAndNavDrift) {
	NavPath path;
	path.valid				= true;
	path.builtBeliefVersion = 5;
	path.builtNavVersion	= 2;

	EXPECT_FALSE(navPathStale(path, 5, 2));
	EXPECT_TRUE(navPathStale(path, 6, 2));
	EXPECT_TRUE(navPathStale(path, 5, 3));
	EXPECT_TRUE(navPathStale(path, 6, 3));
}

// --- Off-mesh recovery: AIDecisionSystem snaps a stranded colonist onto the mesh ---

namespace {
	// A minimal colonist with no Colonist tag, so it asks for no flora of its own.
	EntityID spawnRecoverableColonist(World& world, glm::vec2 at) {
		EntityID c = world.createEntity();
		world.addComponent<Position>(c, Position{at});
		world.addComponent<Velocity>(c, Velocity{{0.0F, 0.0F}});
		world.addComponent<MovementTarget>(c, MovementTarget{{0.0F, 0.0F}, 2.0F, false});
		world.addComponent<NeedsComponent>(c, NeedsComponent::createDefault());
		world.addComponent<Memory>(c, Memory{});
		world.addComponent<Inventory>(c, Inventory::createForColonist());
		world.addComponent<Task>(c, Task{});
		world.addComponent<DecisionTrace>(c, DecisionTrace{});
		world.addComponent<SourceReachMemo>(c, SourceReachMemo{});
		return c;
	}
} // namespace

// A colonist standing on a tree (a blocked face on meshed ground) is snapped onto the mesh.
TEST_F(NavigationSystemTest, OffMeshColonistSnappedOntoMesh) {
	registerTree("Test_NavRecoveryTree", 0.8F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						treeAt{10.0F, 10.0F};
	placeTrees(executor, processed, "Test_NavRecoveryTree", {treeAt});

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& nav = world.registerSystem<NavigationSystem>();
	wireArea(nav);
	nav.setPlacementData(&executor, &processed);
	nav.setConstructionWorld(&cw);
	settle(nav);
	ASSERT_TRUE(nav.hasMesh()) << "navmesh never built";

	AIDecisionSystem& ai = world.registerSystem<AIDecisionSystem>(assets::AssetRegistry::Get(), assets::RecipeRegistry::Get(), 1234u);
	ai.setNavigationSystem(&nav);
	ai.setColonyOrigin(glm::vec2{0.0F, 0.0F});

	ASSERT_TRUE(nav.inSimArea(treeAt));
	ASSERT_FALSE(nav.isOnMesh(treeAt)) << "the colonist must start on a blocked (tree) face";

	EntityID colonist = spawnRecoverableColonist(world, treeAt);
	ai.update(0.016F);

	const auto* pos = world.getComponent<Position>(colonist);
	ASSERT_NE(pos, nullptr);
	EXPECT_NE(pos->value, treeAt) << "recovery must move the off-mesh colonist";
	EXPECT_TRUE(nav.isOnMesh(pos->value)) << "the colonist must land on a walkable mesh face";
}

// The radius overload of nearestPathablePoint clears the BUILT-wall collision band, so the snap
// and WallCollisionSystem never oscillate (the colonist-wall-trap fix).
TEST_F(NavigationSystemTest, RecoverySnapClearsWallCollisionBand) {
	ConstructionWorld cw;
	buildWall(cw, {0, 0}, {4000, 0});

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	const float kWallHalfThickness = 0.10F;
	const float kClearance		   = kWallHalfThickness + kAgentRadius;

	const glm::vec2				   nearWall{2.0F, 0.15F};
	const std::optional<glm::vec2> clearSnap = sys.nearestPathablePoint(nearWall, kAgentRadius);
	ASSERT_TRUE(clearSnap.has_value());
	EXPECT_TRUE(sys.isOnMesh(*clearSnap));
	EXPECT_GE(std::abs(clearSnap->y), kClearance) << "band-aware snap landed inside the wall collision band";
	EXPECT_LT(std::abs(nearWall.y), kClearance) << "test setup: the input point should start inside the band";
}

// A colonist off meshed ground is left exactly where it stands.
TEST_F(NavigationSystemTest, UnmeshedColonistNotRelocated) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World			  world;
	NavigationSystem& nav = world.registerSystem<NavigationSystem>();
	wireArea(nav);
	nav.setConstructionWorld(&cw);
	settle(nav);
	ASSERT_TRUE(nav.hasMesh()) << "navmesh never built";

	AIDecisionSystem& ai = world.registerSystem<AIDecisionSystem>(assets::AssetRegistry::Get(), assets::RecipeRegistry::Get(), 1234u);
	ai.setNavigationSystem(&nav);
	ai.setColonyOrigin(glm::vec2{2.0F, 1.5F});

	ASSERT_FALSE(nav.inSimArea(kUnmeshed));
	EntityID colonist = spawnRecoverableColonist(world, kUnmeshed);
	ai.update(0.016F);

	const auto* pos = world.getComponent<Position>(colonist);
	ASSERT_NE(pos, nullptr);
	EXPECT_EQ(pos->value, kUnmeshed) << "a colonist off meshed ground must NOT be relocated";
}

// --- findValidPositionNear: place a thing a short distance off an origin --------

TEST_F(NavigationSystemTest, FindValidPositionNearOpenGroundReturnsPointAtDistance) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	settle(sys);

	const glm::vec2 origin{2.0F, 2.0F};
	ASSERT_TRUE(sys.isOnMesh(origin));

	const float					   minDist = 1.5F;
	const std::optional<glm::vec2> result  = sys.findValidPositionNear(origin, minDist);
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(sys.isOnMesh(*result));
	EXPECT_NE(*result, origin);
	const float dist = glm::distance(origin, *result);
	EXPECT_GE(dist, minDist - 0.01F);
	EXPECT_NEAR(dist, minDist, 0.01F) << "an open-ground hit sits exactly on the minDist ring";
}

// Origin in the pond, a 1 m ring wholly in the water: the fallback snaps to the nearest shore,
// still on mesh and at least minDist away.
TEST_F(NavigationSystemTest, FindValidPositionNearInPocketFallsBackToNearestWalkable) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys	 = world.registerSystem<NavigationSystem>();
	wireWaterPatch(sys, cw);
	settle(sys);

	const glm::vec2 origin{40.0F, 40.0F};
	ASSERT_TRUE(sys.inSimArea(origin));
	ASSERT_FALSE(sys.isOnMesh(origin));

	const float					   minDist = 1.0F;
	const std::optional<glm::vec2> result  = sys.findValidPositionNear(origin, minDist);
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(sys.isOnMesh(*result));
	EXPECT_GE(glm::distance(origin, *result), minDist - 0.01F);
}

// The push branch: a 2200 m ring from the pond clears all meshed ground (the farthest corner of
// the loaded chunks is 2115 m away), so the nearest shore is pushed out to minDist, lands off the
// mesh, and the shore point itself comes back instead.
TEST_F(NavigationSystemTest, FindValidPositionNearPushedFallbackStaysOnMesh) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys	 = world.registerSystem<NavigationSystem>();
	wireWaterPatch(sys, cw);
	settle(sys);

	const glm::vec2 origin{40.0F, 40.0F};
	const float		minDist = 2200.0F;
	ASSERT_FALSE(sys.isOnMesh(origin));
	const std::optional<glm::vec2> nearest = sys.findValidPositionNear(origin, 0.0F);
	ASSERT_TRUE(nearest.has_value());
	ASSERT_LT(glm::distance(origin, *nearest), minDist);
	ASSERT_FALSE(sys.isOnMesh(origin + (*nearest - origin) / glm::distance(origin, *nearest) * minDist)) << "the push must land off the mesh";

	const std::optional<glm::vec2> result = sys.findValidPositionNear(origin, minDist);
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(sys.isOnMesh(*result)) << "the pushed fallback must be re-validated on mesh";
}

TEST_F(NavigationSystemTest, FindValidPositionNearIsDeterministic) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	settle(sys);

	const glm::vec2				   origin{3.0F, 3.0F};
	const std::optional<glm::vec2> a = sys.findValidPositionNear(origin, 1.0F, {0.5F, 0.5F});
	const std::optional<glm::vec2> b = sys.findValidPositionNear(origin, 1.0F, {0.5F, 0.5F});
	ASSERT_TRUE(a.has_value());
	ASSERT_TRUE(b.has_value());
	EXPECT_EQ(*a, *b);
}

TEST_F(NavigationSystemTest, FindValidPositionNearHonorsPreferredDirOnOpenGround) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	settle(sys);

	const glm::vec2				   origin{2.0F, 2.0F};
	const float					   minDist = 2.0F;
	const std::optional<glm::vec2> result  = sys.findValidPositionNear(origin, minDist, {1.0F, 0.0F});
	ASSERT_TRUE(result.has_value());
	EXPECT_NEAR(result->x, origin.x + minDist, 0.001F);
	EXPECT_NEAR(result->y, origin.y, 0.001F);
}

TEST_F(NavigationSystemTest, FindValidPositionNearOffMeshedGroundReturnsNullopt) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	settle(sys);

	ASSERT_FALSE(sys.inSimArea(kUnmeshed));
	EXPECT_FALSE(sys.findValidPositionNear(kUnmeshed, 1.0F).has_value());
}

// With nothing pending finishPendingBuilds changes nothing; after an edit it returns with the
// patch applied.
TEST_F(NavigationSystemTest, FinishPendingBuildsIsANoOpUntilAnInputChanges) {
	ConstructionWorld cw;

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setConstructionWorld(&cw);
	wireArea(sys);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh());

	const std::uint64_t settled = sys.generation();
	settle(sys);
	EXPECT_FALSE(sys.isBuilding());
	EXPECT_EQ(sys.generation(), settled) << "nothing pending: nothing patched";

	buildWall(cw, {5000, 5000}, {8000, 5000});
	settle(sys);
	EXPECT_FALSE(sys.isBuilding());
	EXPECT_GT(sys.generation(), settled) << "the wall edit's patch landed before returning";
}
