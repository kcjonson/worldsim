#include "NavigationSystem.h"
#include "NavigationSystem.test.h"

#include "AIDecisionSystem.h"
#include "MovementSystem.h"
#include "PhysicsSystem.h"
#include "StaticRectCollisionSystem.h"
#include "../World.h"
#include "../components/AgentRadius.h"
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

// Off meshed ground: a start can't be proven unreachable from, even walled in; a goal can't be
// proven unreachable while its chunk is loaded (its ground is on the way), and is unreachable in
// ground that isn't loaded either.
TEST_F(NavigationSystemTest, IsReachableForUnmeshedGoalDependsOnItsChunk) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	EXPECT_FALSE(isReachable(sys, kOutside, kInside, kAgentRadius)) << "baseline: the closed room blocks";
	EXPECT_TRUE(isReachable(sys, kUnmeshed, kInside, kAgentRadius)) << "an unmeshed start can't be proven to reach nothing";
	EXPECT_FALSE(isReachable(sys, kOutside, kUnmeshed, kAgentRadius)) << "a goal in ground neither meshed nor loaded";

	const std::unique_ptr<world::ChunkManager> wider = readyChunks(landSampler(), {0.0F, 0.0F}, 3);
	sys.setChunkManager(wider.get()); // chunk (3, 0) is loaded now, its ground not meshed yet
	const glm::vec2 coming{1700.0F, 10.0F};
	ASSERT_FALSE(sys.inSimArea(coming));
	EXPECT_TRUE(isReachable(sys, kOutside, coming, kAgentRadius)) << "a goal whose ground is on the way";
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

// --- Path query instrumentation -----------------------------------------------

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
	EXPECT_FALSE(sys.isValidPosition(insideMin)) << "the 1 m view still asks for flora within 30 m";
	EXPECT_TRUE(sys.isValidPosition(outsideMin)) << "and no farther";

	pushArea(sys, {0, 0}, 2000000);
	settle(sys);
	EXPECT_FALSE(sys.isValidPosition(insideMax)) << "the 2 km view asks for flora within 64 m";
	EXPECT_TRUE(sys.isValidPosition(outsideMax)) << "and no farther";
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
		return static_cast<std::size_t>(std::count_if(trees.begin(), trees.end(), [&](glm::vec2 t) { return !sys.isValidPosition(t); }));
	};
	step(world, sys);
	ASSERT_TRUE(sys.inSimArea({1.0F, 1.0F})) << "the viewport's chunk is meshed first";
	EXPECT_EQ(inMesh(), NavigationSystem::kFloraRingsPerTick) << "the first tick takes in one budget of flora";
	EXPECT_FALSE(sys.isValidPosition({1.5F, 1.5F})) << "nearest the view centre first";
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
	ASSERT_TRUE(sys.isValidPosition(treeAt));
	const std::uint64_t before = sys.generation();

	placeTrees(executor, processed, "Test_NavLatePlaceTree", {treeAt});
	settle(sys);
	EXPECT_FALSE(sys.isValidPosition(treeAt)) << "the late tree is in the mesh";
	EXPECT_EQ(sys.generation(), before) << "flora landing marks routes one by one, never every route";
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

// A tree in a room's doorway is walked through at its price; felling it opens the doorway again and
// patches only around the tree: every triangle more than 10 m from it keeps its id and its corners.
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
	const auto through = sys.requestPath(kOutside, kInside, kAgentRadius);
	ASSERT_TRUE(through.has_value()) << "the tree is priced, not a seal";
	NavPath throughPath;
	throughPath.waypoints = *through;
	throughPath.valid	  = true;
	EXPECT_TRUE(sys.routeCrossesPricedFaces(throughPath)) << "through the tree in the doorway";
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
	sys.resetNavQueryStats();

	ASSERT_TRUE(executor.removeEntity(world::worldToChunk({treeAt.x, treeAt.y}), treeAt, "Test_NavFellTree"));
	step(world, sys);
	EXPECT_EQ(sys.navQueryStats().navPatches, 1u) << "the felled tree is patched out on the next tick";
	EXPECT_FALSE(sys.isBuilding());
	EXPECT_TRUE(sys.isValidPosition(treeAt)) << "the trunk's ground is walkable";
	const auto open = sys.requestPath(kOutside, kInside, kAgentRadius);
	ASSERT_TRUE(open.has_value());
	NavPath openPath;
	openPath.waypoints = *open;
	openPath.valid	   = true;
	EXPECT_FALSE(sys.routeCrossesPricedFaces(openPath)) << "the doorway is open ground";
	EXPECT_FALSE(sys.isValidPosition({30.0F, 30.0F})) << "the other tree stays";
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
TEST_F(NavigationSystemTest, GenerationAndReachKeyIgnoreFloraPatches) {
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
	ASSERT_TRUE(sys.isValidPosition(treeAt)) << "the felled tree was patched out";
	EXPECT_EQ(sys.generation(), generation) << "a flora patch moved the generation";
	EXPECT_EQ(sys.reachFrom({0.0F, 0.0F}, kAgentRadius).startRegionGeneration, before.startRegionGeneration)
		<< "a flora patch moved the reach key";

	placeTrees(executor, processed, "Test_NavReachKeyTree", {{-14.0F, 10.0F}}); // late placement in chunk (-1, 0)
	step(world, sys);
	ASSERT_FALSE(sys.isValidPosition({-14.0F, 10.0F})) << "the late tree is in";
	EXPECT_EQ(sys.generation(), generation);
	EXPECT_EQ(sys.reachFrom({0.0F, 0.0F}, kAgentRadius).startRegionGeneration, before.startRegionGeneration);

	buildWall(cw, {20000, 20000}, {22000, 20000});
	step(world, sys);
	EXPECT_NE(sys.generation(), generation) << "a wall moves the generation";
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
		return static_cast<std::size_t>(std::count_if(trees.begin(), trees.end(), [&](glm::vec2 t) { return !sys.isValidPosition(t); }));
	};
	ASSERT_EQ(inMesh(lost), lost.size());

	// The view moves to the other chunk and the first chunk's placement is dropped.
	executor.unloadChunk({0, 0});
	processed.erase(ChunkCoordinate{0, 0});
	placeTrees(executor, processed, "Test_NavLostTree", ahead);
	sys.setViewportRect({-228.0F, 214.0F}, {60.0F, 60.0F});
	sys.resetNavQueryStats();
	step(world, sys);
	EXPECT_GT(inMesh(ahead), 0u) << "adds went ahead of the backlog";
	EXPECT_LE(inMesh(ahead), NavigationSystem::kFloraRingsPerTick);
	EXPECT_LT(inMesh(lost), lost.size()) << "removals drained alongside";
	EXPECT_GE(inMesh(lost), lost.size() - NavigationSystem::kFloraRemovalsPerTick) << "under their own budget";
	EXPECT_LE(sys.navQueryStats().navPatches, 2 * NavigationSystem::kFloraCellsPerTick) << "a few cells each way";

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
	ASSERT_FALSE(sys.isValidPosition(leaving));
	ASSERT_TRUE(sys.isValidPosition(arriving));

	executor.unloadChunk({-1, 0});
	processed.erase(ChunkCoordinate{-1, 0});
	const std::size_t size = processed.size();
	placeTrees(executor, processed, "Test_NavSwapTree", {arriving});
	ASSERT_EQ(processed.size(), size + 1);
	settle(sys);
	EXPECT_TRUE(sys.isValidPosition(leaving)) << "the leaving chunk's flora is gone";
	EXPECT_FALSE(sys.isValidPosition(arriving)) << "the arriving chunk's flora is in";
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
	ASSERT_FALSE(sys.isValidPosition(treeAt)) << "the tree is in";

	// The camera moves a chunk east: (0, 0) unloads, as GameScene drops its placement.
	chunks->update({768.0F, 256.0F});
	chunks->finishPendingGeneration();
	ASSERT_EQ(chunks->getChunk({0, 0}), nullptr);
	executor.unloadChunk({0, 0});
	processed.erase(ChunkCoordinate{0, 0});
	settle(sys);
	EXPECT_TRUE(sys.inSimArea(treeAt)) << "the unloaded chunk's ground stays";
	EXPECT_TRUE(sys.isValidPosition(treeAt)) << "its flora left with its placement";
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
	EXPECT_FALSE(sys.isValidPosition(water)) << "the water ring must not be a walkable face";
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

// A colonist standing in water (off the ground a mover may stand on, on meshed ground) is snapped
// back onto land.
TEST_F(NavigationSystemTest, OffMeshColonistSnappedOntoMesh) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& nav = world.registerSystem<NavigationSystem>();
	nav.setChunkManager(waterPatchChunks());
	nav.setConstructionWorld(&cw);
	nav.setViewportRect({40.0F, 40.0F}, {60.0F, 60.0F});
	settle(nav);
	ASSERT_TRUE(nav.hasMesh()) << "navmesh never built";

	AIDecisionSystem& ai = world.registerSystem<AIDecisionSystem>(assets::AssetRegistry::Get(), assets::RecipeRegistry::Get(), 1234u);
	ai.setNavigationSystem(&nav);
	ai.setColonyOrigin(glm::vec2{0.0F, 0.0F});

	const glm::vec2 pond{40.0F, 40.0F};
	ASSERT_TRUE(nav.inSimArea(pond));
	ASSERT_FALSE(nav.isOnMesh(pond)) << "the colonist must start in the water";

	EntityID colonist = spawnRecoverableColonist(world, pond);
	ai.update(0.016F);

	const auto* pos = world.getComponent<Position>(colonist);
	ASSERT_NE(pos, nullptr);
	EXPECT_NE(pos->value, pond) << "recovery must move the stranded colonist";
	EXPECT_TRUE(nav.isOnMesh(pos->value)) << "the colonist must land on ground it may stand on";
}

// A colonist standing in a tree stands on ground a route can start on (the tree is priced, not a
// wall), so the stranded recovery leaves it where it is.
TEST_F(NavigationSystemTest, ColonistInATreeIsNotStranded) {
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

	AIDecisionSystem& ai = world.registerSystem<AIDecisionSystem>(assets::AssetRegistry::Get(), assets::RecipeRegistry::Get(), 1234u);
	ai.setNavigationSystem(&nav);
	ai.setColonyOrigin(glm::vec2{0.0F, 0.0F});
	ASSERT_FALSE(nav.isValidPosition(treeAt)) << "the tree is not clear ground";
	ASSERT_TRUE(nav.isOnMesh(treeAt)) << "but a mover may stand in it";

	EntityID colonist = spawnRecoverableColonist(world, treeAt);
	ai.update(0.016F);
	EXPECT_EQ(world.getComponent<Position>(colonist)->value, treeAt);
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
	EXPECT_TRUE(sys.isValidPosition(*clearSnap));
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
	ASSERT_TRUE(sys.isValidPosition(origin));

	const float					   minDist = 1.5F;
	const std::optional<glm::vec2> result  = sys.findValidPositionNear(origin, minDist);
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(sys.isValidPosition(*result));
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
	ASSERT_FALSE(sys.isValidPosition(origin));

	const float					   minDist = 1.0F;
	const std::optional<glm::vec2> result  = sys.findValidPositionNear(origin, minDist);
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(sys.isValidPosition(*result));
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
	ASSERT_FALSE(sys.isValidPosition(origin));
	const std::optional<glm::vec2> nearest = sys.findValidPositionNear(origin, 0.0F);
	ASSERT_TRUE(nearest.has_value());
	ASSERT_LT(glm::distance(origin, *nearest), minDist);
	ASSERT_FALSE(sys.isValidPosition(origin + (*nearest - origin) / glm::distance(origin, *nearest) * minDist)) << "the push must land off the mesh";

	const std::optional<glm::vec2> result = sys.findValidPositionNear(origin, minDist);
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(sys.isValidPosition(*result)) << "the pushed fallback must be re-validated on mesh";
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

// --- Detail -----------------------------------------------------------------------

// Detail turns on for the cells within kDetailOnMarginMm of the viewport rect and stays on until the
// rect is more than kDetailKeepMarginMm away, so a view edge resting near a cell boundary never
// flips it. Off everywhere without a viewport.
TEST_F(NavigationSystemTest, DetailTurnsOnNearTheViewAndOffFarFromIt) {
	ASSERT_EQ(NavigationSystem::kDetailOnMarginMm, 8000);
	ASSERT_EQ(NavigationSystem::kDetailKeepMarginMm, 32000);
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	world.update(0.016F);
	EXPECT_FALSE(sys.detailOnAt({0.0F, 0.0F})) << "no viewport, no detail";

	// A 30 m view at the origin: on reaches 38 m, so the cell [32, 48) is on and [48, 64) is not.
	sys.setViewportRect({0.0F, 0.0F}, {30.0F, 30.0F});
	world.update(0.016F);
	EXPECT_TRUE(sys.detailOnAt({0.0F, 0.0F}));
	EXPECT_TRUE(sys.detailOnAt({40.0F, 0.0F}));
	EXPECT_FALSE(sys.detailOnAt({50.0F, 0.0F}));

	// 20 m east turns [48, 64) on; back at the origin it is within the keep margin (62 m), so it
	// stays on, however often the view rocks between the two.
	for (int i = 0; i < 4; ++i) {
		sys.setViewportRect({20.0F, 0.0F}, {30.0F, 30.0F});
		world.update(0.016F);
		EXPECT_TRUE(sys.detailOnAt({50.0F, 0.0F}));
		sys.setViewportRect({0.0F, 0.0F}, {30.0F, 30.0F});
		world.update(0.016F);
		EXPECT_TRUE(sys.detailOnAt({50.0F, 0.0F})) << "the cell flipped off within the keep margin";
	}

	// 40 m west the keep rect ends at 22 m: the cells past it go off, the ones within it stay.
	sys.setViewportRect({-40.0F, 0.0F}, {30.0F, 30.0F});
	world.update(0.016F);
	EXPECT_FALSE(sys.detailOnAt({50.0F, 0.0F}));
	EXPECT_FALSE(sys.detailOnAt({40.0F, 0.0F}));
	EXPECT_TRUE(sys.detailOnAt({10.0F, 0.0F}));
}

// --- Forward snap -------------------------------------------------------------------

namespace {
	// A colonist (it asks for flora) on a route, or none when `route` is empty.
	EntityID routedColonist(World& world, glm::vec2 at, const std::vector<glm::vec2>& route, std::size_t current = 1) {
		const EntityID e = addColonist(world, at, "Snap");
		if (!route.empty()) {
			NavPath path;
			path.waypoints = route;
			path.current   = current;
			path.valid	   = true;
			world.addComponent<NavPath>(e, std::move(path));
		}
		return e;
	}

	class ForwardSnapTest : public NavigationSystemTest {
	  protected:
		void SetUp() override {
			NavigationSystemTest::SetUp();
			registerTree("Test_NavSnapTree", 0.8F);
			sys = &world.registerSystem<NavigationSystem>();
			wireArea(*sys);
			sys->setPlacementData(&executor, &processed);
			sys->setConstructionWorld(&cw);
			placeTrees(executor, processed, "Test_NavSnapTree", {kTree});
			settle(*sys);
			ASSERT_FALSE(sys->isValidPosition(kTree));
		}

		static constexpr glm::vec2 kTree{10.0F, 10.0F};

		PlacementExecutor					executor{AssetRegistry::Get()};
		std::unordered_set<ChunkCoordinate> processed;
		ConstructionWorld					cw;
		World								world;
		NavigationSystem*					sys = nullptr;
	};
} // namespace

// On walkable ground with a route clear of flora, nothing moves and the route is kept.
TEST_F(ForwardSnapTest, ClearRouteOnWalkableGroundIsKept) {
	const glm::vec2 at{0.0F, 0.0F};
	const EntityID	colonist = routedColonist(world, at, {at, {5.0F, 0.0F}});
	step(world, *sys);
	EXPECT_EQ(world.getComponent<Position>(colonist)->value, at);
	EXPECT_EQ(world.getComponent<NavPath>(colonist)->current, 1u);
}

// On walkable ground with a tree arriving across the route ahead, the snap leaves it where it stands
// and the AI replans from there: a route stamped with the new generation that goes round the tree.
TEST_F(ForwardSnapTest, RouteBlockedAheadIsReplannedFromWhereItStands) {
	AIDecisionSystem& ai = world.registerSystem<AIDecisionSystem>(AssetRegistry::Get(), assets::RecipeRegistry::Get(), 1234u);
	ai.setNavigationSystem(sys);
	const glm::vec2 at{-16.0F, 20.0F};
	const glm::vec2 goal{-4.0F, 20.0F};
	const glm::vec2 across{-10.0F, 20.0F}; // chunk (-1, 0), whose placement arrives late
	const EntityID	colonist = spawnRecoverableColonist(world, at);
	world.addComponent<Colonist>(colonist, Colonist{"Replanner"});
	ai.enterControl(colonist);
	ASSERT_TRUE(ai.issuePlayerMoveOrder(colonist, goal).has_value());
	const NavPath& path = *world.getComponent<NavPath>(colonist);
	ASSERT_TRUE(path.valid);
	ASSERT_EQ(path.waypoints.size(), 2u) << "straight through where the tree will stand";

	placeTrees(executor, processed, "Test_NavSnapTree", {across});
	for (int i = 0; i < 3; ++i) {
		step(world, *sys);
	}
	ASSERT_FALSE(sys->isValidPosition(across)) << "the tree across the route is in";
	EXPECT_EQ(world.getComponent<Position>(colonist)->value, at);
	ASSERT_TRUE(path.valid);
	EXPECT_EQ(path.builtNavVersion, sys->generation()) << "not replanned";
	EXPECT_EQ(path.waypoints.front(), at) << "replanned from where it stands";
	for (std::size_t k = 0; k + 1 < path.waypoints.size(); ++k) {
		for (int i = 0; i <= 20; ++i) {
			const glm::vec2 p = path.waypoints[k] + (path.waypoints[k + 1] - path.waypoints[k]) * (static_cast<float>(i) / 20.0F);
			EXPECT_TRUE(sys->isValidPosition(p)) << "the new route crosses the tree at (" << p.x << ", " << p.y << ")";
		}
	}
}

// Inside a tree with a route, it moves to the first point along the rest of the route that is
// walkable with its clearance: ahead of where it stood, never behind.
TEST_F(ForwardSnapTest, InsideTreeWithRouteMovesForward) {
	const glm::vec2 at{9.7F, 10.0F};
	const EntityID	colonist = routedColonist(world, at, {{6.0F, 10.0F}, {14.0F, 10.0F}, {14.0F, 16.0F}});
	step(world, *sys);
	const glm::vec2 now = world.getComponent<Position>(colonist)->value;
	EXPECT_TRUE(sys->isValidPosition(now));
	EXPECT_FLOAT_EQ(now.y, 10.0F) << "on the route";
	EXPECT_GT(now.x, kTree.x + 0.8F + kAgentRadius) << "past the tree with its clearance";
	EXPECT_LT(now.x, kTree.x + 0.8F + kAgentRadius + 0.3F) << "the first clear point, not a later one";
	EXPECT_EQ(world.getComponent<NavPath>(colonist)->current, 1u);
}

// A wall the mover hasn't seen runs across its route just past the tree. No point before the wall
// clears, and nothing past it is a candidate, so it stays where it is and its route goes stale.
TEST_F(ForwardSnapTest, WallBeyondTheTreeStopsTheWalk) {
	buildWall(cw, {11300, 7000}, {11300, 13000});
	settle(*sys);
	const glm::vec2 at{9.7F, 10.0F};
	const EntityID	colonist = routedColonist(world, at, {{6.0F, 10.0F}, {14.0F, 10.0F}, {14.0F, 16.0F}});
	world.getComponent<NavPath>(colonist)->builtNavVersion = sys->generation();
	step(world, *sys);
	EXPECT_EQ(world.getComponent<Position>(colonist)->value, at);
	EXPECT_EQ(world.getComponent<NavPath>(colonist)->current, 1u);
	EXPECT_NE(world.getComponent<NavPath>(colonist)->builtNavVersion, sys->generation()) << "the route isn't stale";
}

// The rest of the route lies inside the tree: no point ahead clears, so it stays and its route goes
// stale; the nearest walkable point, which may be behind it, is only for a mover with no route.
TEST_F(ForwardSnapTest, RouteEndingInsideTheTreeStays) {
	const glm::vec2 at{9.7F, 10.0F};
	const EntityID	colonist = routedColonist(world, at, {{6.0F, 10.0F}, {9.9F, 10.0F}});
	world.getComponent<NavPath>(colonist)->builtNavVersion = sys->generation();
	step(world, *sys);
	EXPECT_EQ(world.getComponent<Position>(colonist)->value, at);
	EXPECT_NE(world.getComponent<NavPath>(colonist)->builtNavVersion, sys->generation()) << "the route isn't stale";
}

// Inside a tree with no route, it goes to the nearest walkable point.
TEST_F(ForwardSnapTest, InsideTreeWithoutRouteTakesNearestWalkable) {
	const glm::vec2 at{9.7F, 10.0F};
	const EntityID	colonist = routedColonist(world, at, {});
	step(world, *sys);
	const glm::vec2 now = world.getComponent<Position>(colonist)->value;
	EXPECT_TRUE(sys->isValidPosition(now));
	EXPECT_LT(glm::distance(now, at), 1.5F) << "the nearest walkable point, not a far one";
}

namespace {
	// A colonist walking a move order has a tree land on it, the view arriving over it either a tick
	// later or in the same tick. It is moved forward along its route, keeps the order, never steps
	// back and gets there.
	void walkUnderLandingTree(bool viewArrivesWithTree) {
		registerTree("Test_NavWalkerTree", 0.4F);
		PlacementExecutor					executor(AssetRegistry::Get());
		std::unordered_set<ChunkCoordinate> processed;
		ConstructionWorld					cw;
		World								world;
		NavigationSystem&					nav = world.registerSystem<NavigationSystem>();
		nav.setChunkManager(landChunks());
		nav.setPlacementData(&executor, &processed);
		nav.setConstructionWorld(&cw);
		const glm::vec2 away{-300.0F, 300.0F};
		nav.setViewportRect(away, {30.0F, 30.0F});
		settle(nav);

		AIDecisionSystem& ai = world.registerSystem<AIDecisionSystem>(AssetRegistry::Get(), assets::RecipeRegistry::Get(), 1234u);
		ai.setNavigationSystem(&nav);
		world.registerSystem<MovementSystem>();
		world.registerSystem<PhysicsSystem>();
		auto& collision = world.registerSystem<StaticRectCollisionSystem>();
		collision.setPlacementData(&executor);
		collision.setDetailSource(&nav);

		const glm::vec2 start{2.0F, 5.0F};
		const glm::vec2 goal{22.0F, 5.0F};
		const EntityID	colonist = spawnRecoverableColonist(world, start);
		world.addComponent<Colonist>(colonist, Colonist{"Walker"});
		world.addComponent<AgentRadius>(colonist, AgentRadius{kAgentRadius, 1.0F});
		NeedsComponent& needs = *world.getComponent<NeedsComponent>(colonist);
		for (const NeedType need : {NeedType::Hunger, NeedType::Thirst, NeedType::Energy, NeedType::Bladder, NeedType::Digestion}) {
			needs.get(need).value = 100.0F;
		}
		ai.enterControl(colonist);
		ASSERT_TRUE(ai.issuePlayerMoveOrder(colonist, goal).has_value());

		constexpr float kTick = 0.1F;
		const Position& position = *world.getComponent<Position>(colonist);
		while (position.value.x < 6.0F) {
			world.update(kTick);
		}
		ASSERT_LT(std::abs(position.value.y - start.y), 0.05F) << "walking the straight route";

		// The tree lands just ahead; the walker steps into it this tick, and the job brings it into the
		// mesh at endTick.
		const glm::vec2 treeAt{position.value.x + 0.25F, start.y};
		const Task&		task  = *world.getComponent<Task>(colonist);
		float			lastX = position.value.x;
		placeTrees(executor, processed, "Test_NavWalkerTree", {treeAt});
		if (viewArrivesWithTree) {
			nav.setViewportRect(position.value, {30.0F, 30.0F});
		}
		step(world, nav);
		ASSERT_FALSE(nav.isValidPosition(position.value)) << "the walker stands in the tree, not pushed out of it";
		ASSERT_GE(position.value.x, lastX - 1e-4F) << "moved back as the tree landed";
		lastX = position.value.x;

		nav.setViewportRect(position.value, {30.0F, 30.0F});
		for (int i = 0; i < 100 && glm::distance(position.value, goal) > 0.3F; ++i) {
			world.update(kTick);
			expectMeshesValid(nav);
			ASSERT_GE(position.value.x, lastX - 1e-4F) << "tick " << i << ": moved back";
			ASSERT_EQ(task.targetPosition, goal) << "tick " << i << ": the move order was dropped";
			lastX = position.value.x;
		}
		EXPECT_TRUE(nav.detailOnAt(treeAt));
		EXPECT_LT(glm::distance(position.value, goal), 0.3F) << "never got there";
	}
} // namespace

TEST_F(NavigationSystemTest, WalkerUnderTreeKeepsItsTaskWhenTheViewArrivesLater) {
	walkUnderLandingTree(false);
}

// The placement lands in the tick the view arrives, before the tree is in the nav mesh: collision
// leaves the walker alone until the mesh has the tree and the forward snap has moved it.
TEST_F(NavigationSystemTest, WalkerUnderTreeKeepsItsTaskWhenTheViewArrivesWithIt) {
	walkUnderLandingTree(true);
}

// --- Flora budget -------------------------------------------------------------------

// Panning the view along a strip of forest: the nav mesh holds at most the budget (plus one tick's
// adds, which come in after the trim), nothing inside the detail keep rect ever leaves, the strip
// far behind does, and going back to ground the view has just left adds nothing.
TEST_F(NavigationSystemTest, FloraBudgetHoldsWhilePanning) {
	constexpr std::size_t kBudget = 400;
	registerTree("Test_NavPanTree", 0.3F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	std::vector<glm::vec2>				trees; // four by four in every 16 m cell of the row y in [0, 16)
	for (float x = -960.0F; x < 1440.0F; x += 4.0F) {
		for (float y = 2.0F; y < 16.0F; y += 4.0F) {
			trees.push_back({x + 2.0F, y});
		}
	}
	placeTrees(executor, processed, "Test_NavPanTree", trees);

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>(kBudget);
	sys.setChunkManager(landChunks());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	const auto view = [&](float x) { sys.setViewportRect({x, 8.0F}, {30.0F, 30.0F}); };
	view(-900.0F);
	settle(sys);

	const auto inMesh = [&](glm::vec2 t) { return sys.inSimArea(t) && !sys.isValidPosition(t); };
	std::vector<bool> was(trees.size());
	for (std::size_t k = 0; k < trees.size(); ++k) {
		was[k] = inMesh(trees[k]);
	}
	const float keep = 30.0F + static_cast<float>(NavigationSystem::kDetailKeepMarginMm) / 1000.0F;
	for (float x = -900.0F; x <= 1400.0F; x += 16.0F) {
		view(x);
		for (int tick = 0; tick < 3; ++tick) {
			step(world, sys);
			std::size_t count = 0;
			for (std::size_t k = 0; k < trees.size(); ++k) {
				const bool now = inMesh(trees[k]);
				count += now ? 1 : 0;
				if (was[k] && !now) {
					ASSERT_GT(std::abs(trees[k].x - x), keep) << "a tree inside the keep rect left, view at " << x;
				}
				was[k] = now;
			}
			ASSERT_LE(count, kBudget + NavigationSystem::kFloraRingsPerTick) << "view at " << x;
		}
	}
	EXPECT_FALSE(inMesh(trees.front())) << "the far end of the pan was trimmed";

	for (int tick = 0; tick < 3; ++tick) {
		step(world, sys);
	}
	const std::uint64_t generation = sys.generation();
	view(1400.0F - 6.0F * 16.0F);
	for (int tick = 0; tick < 3; ++tick) {
		step(world, sys);
	}
	EXPECT_EQ(sys.generation(), generation) << "the revisit patched the mesh";
}

// --- Ground edge cases ---------------------------------------------------------------

// A meshed chunk whose terrain is rebuilt has its water taken out and put back in one tick, leaving
// the same mesh: the pond still blocks, the land around it is walkable, no triangle is gained or lost.
TEST_F(NavigationSystemTest, TerrainChangeOnMeshedChunkRederivesItsWater) {
	const std::unique_ptr<world::ChunkManager> chunks = readyChunks(waterPatchSampler(), {256.0F, 256.0F}, 0);
	ConstructionWorld						   cw;
	World									   world;
	NavigationSystem&						   sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	sys.setConstructionWorld(&cw);
	settle(sys);
	const glm::vec2 pond{40.0F, 40.0F};
	const glm::vec2 land{20.0F, 20.0F};
	ASSERT_FALSE(sys.isValidPosition(pond));
	ASSERT_TRUE(sys.isValidPosition(land));
	const std::int32_t	navTriangles		= sys.navMesh().liveTriangleCount();
	const std::int32_t	structuralTriangles = sys.structuralMesh().liveTriangleCount();
	const std::uint64_t generation			= sys.generation();

	world::Chunk* chunk = chunks->getChunk({0, 0});
	ASSERT_NE(chunk, nullptr);
	const std::uint32_t version = chunk->terrainPolygons().version;
	chunk->generate();
	ASSERT_EQ(chunk->terrainPolygons().version, version + 1);

	step(world, sys);
	EXPECT_EQ(sys.generation(), generation + 1) << "re-derived in one tick";
	EXPECT_FALSE(sys.isBuilding());
	EXPECT_FALSE(sys.isValidPosition(pond)) << "the pond still blocks";
	EXPECT_TRUE(sys.isValidPosition(land));
	EXPECT_EQ(sys.navMesh().liveTriangleCount(), navTriangles);
	EXPECT_EQ(sys.structuralMesh().liveTriangleCount(), structuralTriangles);
}

// A chunk loses its ground while its flora is still queued for removal (three ticks' worth, and the
// ground goes within two, the first tick being the next chunk's arrival): the flora drains after the
// ground under its budget, every tick's mesh is valid and nothing is left building.
TEST_F(NavigationSystemTest, GroundDroppedWhileItsFloraRemovalsAreQueued) {
	registerTree("Test_NavDropTree", 0.3F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	std::vector<glm::vec2>				trees;
	for (int i = 0; i < 20; ++i) {
		for (int j = 0; j < 20; ++j) {
			trees.push_back({ChunkWalk::centre(0).x - 14.25F + 1.5F * static_cast<float>(i), ChunkWalk::centre(0).y - 14.25F + 1.5F * static_cast<float>(j)});
		}
	}
	ASSERT_GT(trees.size(), 3 * NavigationSystem::kFloraRemovalsPerTick);
	placeTrees(executor, processed, "Test_NavDropTree", trees);

	ChunkWalk		  walk;
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(walk.chunks.get());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	const auto stand = [&](int i) {
		walk.stand(i);
		sys.setViewportRect(ChunkWalk::centre(i), {30.0F, 30.0F});
	};

	// Chunks 1..kKeptUnloadedChunks get walls, so they keep their ground and chunk 0 is the one to
	// go the moment it unloads.
	const int walled = static_cast<int>(NavigationSystem::kKeptUnloadedChunks);
	for (int i = 1; i <= walled; ++i) {
		const glm::vec2 c = ChunkWalk::centre(i);
		buildWall(cw, nav::toMm(c - glm::vec2{2.0F, 0.0F}), nav::toMm(c + glm::vec2{2.0F, 0.0F}));
		stand(i);
		settle(sys);
	}
	stand(0);
	settle(sys);
	ASSERT_EQ(std::count_if(trees.begin(), trees.end(), [&](glm::vec2 t) { return !sys.isValidPosition(t); }), static_cast<std::ptrdiff_t>(trees.size()));

	stand(walled + 1);
	step(world, sys);
	step(world, sys);
	EXPECT_FALSE(sys.inSimArea(ChunkWalk::centre(0))) << "chunk 0's ground went";
	EXPECT_TRUE(sys.isBuilding()) << "while its flora was still draining";
	for (int i = 0; i < 4; ++i) {
		step(world, sys);
	}
	EXPECT_FALSE(sys.isBuilding());
	for (int i = 1; i <= walled; ++i) {
		EXPECT_TRUE(sys.inSimArea(ChunkWalk::centre(i))) << "walled chunk " << i;
	}
}

// Water past the tree: the pond's west shore is at x = 32 and the tree ends just short of it, too
// close for the mover's clearance. The walk stops at the water, so the mover stays and its route goes
// stale rather than landing in or across the pond.
TEST_F(NavigationSystemTest, WaterBeyondTheTreeStopsTheSnap) {
	registerTree("Test_NavShoreTree", 0.8F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						treeAt{31.0F, 40.0F};
	placeTrees(executor, processed, "Test_NavShoreTree", {treeAt});

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(waterPatchChunks());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	sys.setViewportRect({40.0F, 40.0F}, {60.0F, 60.0F});
	settle(sys);
	ASSERT_FALSE(sys.isValidPosition({40.0F, 40.0F})) << "the pond";

	const glm::vec2 at{30.8F, 40.0F};
	const EntityID	colonist = routedColonist(world, at, {{26.0F, 40.0F}, {48.0F, 40.0F}, {54.0F, 40.0F}});
	world.getComponent<NavPath>(colonist)->builtNavVersion = sys.generation();
	step(world, sys);
	EXPECT_EQ(world.getComponent<Position>(colonist)->value, at);
	EXPECT_NE(world.getComponent<NavPath>(colonist)->builtNavVersion, sys.generation()) << "the route isn't stale";
}

// A chunk holding only construction blueprints (a wall and a foundation, nothing built) keeps its
// ground however long ago it unloaded, like a walled one.
TEST_F(NavigationSystemTest, BlueprintChunkKeepsItsGround) {
	ChunkWalk		  walk;
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(walk.chunks.get());
	sys.setConstructionWorld(&cw);

	walk.stand(0);
	ASSERT_TRUE(cw.commitSegment({200000, 200000}, {204000, 200000}, "Wood", "Standard", kInvalidFoundation).ok());
	settle(sys);
	walk.stand(1);
	ASSERT_TRUE(cw.commitFoundation(geometry::Ring{{700000, 200000}, {704000, 200000}, {704000, 204000}, {700000, 204000}}, "Wood").ok());
	settle(sys);
	const int steps = static_cast<int>(NavigationSystem::kKeptUnloadedChunks) + 5;
	for (int i = 2; i < steps; ++i) {
		walk.stand(i);
		settle(sys);
	}
	EXPECT_TRUE(sys.inSimArea(ChunkWalk::centre(0))) << "the blueprint wall's chunk kept its ground";
	EXPECT_TRUE(sys.inSimArea(ChunkWalk::centre(1))) << "the blueprint foundation's chunk kept its ground";
	EXPECT_FALSE(sys.inSimArea(ChunkWalk::centre(2))) << "the oldest without construction went";
}

// A lost chunk's flora comes out one patch per cell, so each patch's width refresh covers its own
// cell and not the box around all of them.
TEST_F(NavigationSystemTest, RemovalsArePatchedPerCell) {
	registerTree("Test_NavCellTree", 0.3F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	std::vector<glm::vec2>				trees;
	for (const float x : {4.0F, 20.0F, 36.0F}) { // three cells of chunk (0, 0)
		for (int i = 0; i < 4; ++i) {
			trees.push_back({x + 2.0F * static_cast<float>(i), 6.0F});
		}
	}
	placeTrees(executor, processed, "Test_NavCellTree", trees);

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(std::none_of(trees.begin(), trees.end(), [&](glm::vec2 t) { return sys.isValidPosition(t); }));

	executor.unloadChunk({0, 0});
	processed.erase(ChunkCoordinate{0, 0});
	sys.resetNavQueryStats();
	step(world, sys);
	for (int i = 0; i < 4 && sys.isBuilding(); ++i) { // kFloraCellsPerTick cells a tick
		step(world, sys);
	}
	ASSERT_FALSE(sys.isBuilding());
	EXPECT_EQ(sys.navQueryStats().navPatches, 3u);
	EXPECT_EQ(sys.navQueryStats().structuralPatches, 0u) << "flora is in the nav mesh only";
	EXPECT_TRUE(std::all_of(trees.begin(), trees.end(), [&](glm::vec2 t) { return sys.isValidPosition(t); })) << "all removed";
}

// Swapping the ConstructionWorld for another at the same version still re-reads the walls: the old
// world's wall leaves the mesh and the new one's comes in.
TEST_F(NavigationSystemTest, ConstructionWorldSwapRereadsWalls) {
	ConstructionWorld first;
	ConstructionWorld second;
	buildWall(first, {10000, 0}, {10000, 20000});
	buildWall(second, {30000, 0}, {30000, 20000});
	ASSERT_EQ(first.version(), second.version()) << "the swap has to be caught by more than the version";

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&first);
	settle(sys);
	const auto straight = [&](float x) {
		const auto path = sys.requestPath({x - 5.0F, 10.0F}, {x + 5.0F, 10.0F}, kAgentRadius);
		return path.has_value() && path->size() == 2;
	};
	ASSERT_FALSE(straight(10.0F)) << "the first world's wall is in";
	ASSERT_TRUE(straight(30.0F));

	sys.setConstructionWorld(&second);
	settle(sys);
	EXPECT_TRUE(straight(10.0F)) << "the old world's wall is gone";
	EXPECT_FALSE(straight(30.0F)) << "the new world's wall is in";
}

// --- Priced flora (step 5 part 2) -------------------------------------------------------

namespace {
	// The world a walking colonist needs: nav over the shared land chunks, the real AI on move
	// orders, movement, physics and flora collision gated by detail.
	struct WalkRig {
		PlacementExecutor					executor{AssetRegistry::Get()};
		std::unordered_set<ChunkCoordinate> processed;
		ConstructionWorld					cw;
		World								world;
		NavigationSystem&					nav = world.registerSystem<NavigationSystem>();
		AIDecisionSystem*					ai	= nullptr;

		explicit WalkRig(glm::vec2 view) {
			nav.setChunkManager(landChunks());
			nav.setPlacementData(&executor, &processed);
			nav.setConstructionWorld(&cw);
			nav.setViewportRect(view, {30.0F, 30.0F});
		}

		// Registers the AI and the movers once the trees placed so far are in.
		void start() {
			settle(nav);
			world.update(0.016F); // detail follows the view from the first update
			ai = &world.registerSystem<AIDecisionSystem>(AssetRegistry::Get(), assets::RecipeRegistry::Get(), 1234u);
			ai->setNavigationSystem(&nav);
			world.registerSystem<MovementSystem>();
			world.registerSystem<PhysicsSystem>();
			auto& collision = world.registerSystem<StaticRectCollisionSystem>();
			collision.setPlacementData(&executor);
			collision.setDetailSource(&nav);
		}

		EntityID walker(glm::vec2 from, glm::vec2 to) {
			const EntityID colonist = spawnRecoverableColonist(world, from);
			world.addComponent<Colonist>(colonist, Colonist{"Walker"});
			world.addComponent<AgentRadius>(colonist, AgentRadius{kAgentRadius, 1.0F});
			NeedsComponent& needs = *world.getComponent<NeedsComponent>(colonist);
			for (const NeedType need : {NeedType::Hunger, NeedType::Thirst, NeedType::Energy, NeedType::Bladder, NeedType::Digestion}) {
				needs.get(need).value = 100.0F;
			}
			ai->enterControl(colonist);
			EXPECT_TRUE(ai->issuePlayerMoveOrder(colonist, to).has_value());
			return colonist;
		}

		[[nodiscard]] bool pricedRoute(EntityID colonist) {
			const NavPath* path = world.getComponent<NavPath>(colonist);
			return path != nullptr && nav.routeCrossesPricedFaces(*path);
		}
	};

	// Tree centres on a circle, close enough that no gap admits a 0.6 m disc.
	std::vector<glm::vec2> treeRing(glm::vec2 centre, float radius) {
		std::vector<glm::vec2> trees;
		const int			   count = static_cast<int>(std::ceil(6.2831853F * radius / 0.55F));
		for (int i = 0; i < count; ++i) {
			const float angle = 6.2831853F * static_cast<float>(i) / static_cast<float>(count);
			trees.push_back(centre + radius * glm::vec2{std::cos(angle), std::sin(angle)});
		}
		return trees;
	}

	NavPath routeOf(const std::vector<glm::vec2>& waypoints) {
		NavPath path;
		path.waypoints = waypoints;
		path.valid	   = true;
		return path;
	}

	float lengthOf(const std::vector<glm::vec2>& waypoints) {
		float length = 0.0F;
		for (std::size_t k = 1; k < waypoints.size(); ++k) {
			length += glm::distance(waypoints[k - 1], waypoints[k]);
		}
		return length;
	}
} // namespace

// A walker left standing in a tree off camera (no snap, no collision there) with a route the tree
// made stale: the AI replans from where it stands, which a priced tree allows, so the stranded
// recovery never fires; the new route walks out through the tree at its price and the walker
// carries on to its goal, its move order kept, never stepping back. It stands past the trunk's
// middle, so the cheapest way out is on ahead.
TEST_F(NavigationSystemTest, WalkerLeftInATreeReplansAndWalksOut) {
	registerTree("Test_NavLeftInTree", 0.4F);
	WalkRig rig({-300.0F, 300.0F});
	rig.start();
	const glm::vec2 goal{22.0F, 5.0F};
	const EntityID	colonist = rig.walker({2.0F, 5.0F}, goal);
	constexpr float kTick	 = 0.1F;
	const Position& position = *rig.world.getComponent<Position>(colonist);
	while (position.value.x < 6.0F) {
		rig.world.update(kTick);
	}

	const glm::vec2 treeAt{position.value.x + 0.05F, position.value.y};
	const Task&		task  = *rig.world.getComponent<Task>(colonist);
	float			lastX = position.value.x;
	bool			throughTheTree = false;
	placeTrees(rig.executor, rig.processed, "Test_NavLeftInTree", {treeAt});
	for (int i = 0; i < 100 && glm::distance(position.value, goal) > 0.3F; ++i) {
		rig.world.update(kTick);
		expectMeshesValid(rig.nav);
		ASSERT_GE(position.value.x, lastX - 1e-4F) << "tick " << i << ": moved back";
		ASSERT_EQ(task.targetPosition, goal) << "tick " << i << ": the move order was dropped";
		throughTheTree = throughTheTree || rig.pricedRoute(colonist);
		lastX		   = position.value.x;
	}
	EXPECT_FALSE(rig.nav.isValidPosition(treeAt)) << "the tree is in the mesh";
	EXPECT_FALSE(rig.nav.detailOnAt(treeAt));
	EXPECT_TRUE(throughTheTree) << "the replan walked out through the tree";
	EXPECT_LT(glm::distance(position.value, goal), 0.3F) << "never got there";
}

// A goal ringed by trees with detail on: the only way in is through them, so the route crosses
// priced faces, says so, and the walker gets there. Collision leaves it alone on the way: the walk
// is the same step for step with flora collision wired as without it.
TEST_F(NavigationSystemTest, GoalRingedByTreesIsReachedThrough) {
	registerTree("Test_NavRingTree", 0.3F);
	const glm::vec2 goal{20.0F, 20.0F};
	const auto		walk = [&](bool collision) {
		 WalkRig rig(goal);
		 placeTrees(rig.executor, rig.processed, "Test_NavRingTree", treeRing(goal, 3.0F));
		 rig.start();
		 if (!collision) {
			 rig.world.getSystem<StaticRectCollisionSystem>().setDetailSource(nullptr);
		 }
		 EXPECT_TRUE(rig.nav.detailOnAt(goal));
		 const EntityID colonist = rig.walker({10.0F, 20.0F}, goal);
		 EXPECT_TRUE(rig.pricedRoute(colonist)) << "the route goes through the ring";
		 std::vector<glm::vec2> steps;
		 const Position&		position = *rig.world.getComponent<Position>(colonist);
		 for (int i = 0; i < 100 && glm::distance(position.value, goal) > 0.3F; ++i) {
			 rig.world.update(0.1F);
			 steps.push_back(position.value);
		 }
		 return steps;
	};
	const std::vector<glm::vec2> with	 = walk(true);
	const std::vector<glm::vec2> without = walk(false);
	ASSERT_FALSE(with.empty());
	EXPECT_LT(glm::distance(with.back(), goal), 0.3F) << "never got through the ring";
	EXPECT_EQ(with, without) << "collision moved the walker";
}

// A hedge with a free way round a few metres off: going round is cheaper than a trunk at its
// price, so the route goes round and crosses nothing priced. A hedge too long to go round is
// walked through.
TEST_F(NavigationSystemTest, FreeWayRoundIsTakenWhenCheaper) {
	registerTree("Test_NavHedgeTree", 0.3F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	std::vector<glm::vec2>				shortHedge;
	std::vector<glm::vec2>				longHedge;
	for (float y = 2.0F; y <= 8.0F; y += 0.5F) {
		shortHedge.push_back({20.0F, y});
	}
	for (float y = 2.0F; y <= 60.0F; y += 0.5F) {
		longHedge.push_back({40.0F, y});
	}
	std::vector<glm::vec2> trees = shortHedge;
	trees.insert(trees.end(), longHedge.begin(), longHedge.end());
	placeTrees(executor, processed, "Test_NavHedgeTree", trees);

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	sys.setViewportRect({30.0F, 30.0F}, {60.0F, 60.0F});
	settle(sys);

	const auto round = sys.requestPath({17.0F, 5.0F}, {23.0F, 5.0F}, kAgentRadius);
	ASSERT_TRUE(round.has_value());
	EXPECT_FALSE(sys.routeCrossesPricedFaces(routeOf(*round))) << "round the short hedge";
	EXPECT_GT(lengthOf(*round), 7.0F);
	EXPECT_LT(lengthOf(*round), 20.0F);

	const auto through = sys.requestPath({37.0F, 30.0F}, {43.0F, 30.0F}, kAgentRadius);
	ASSERT_TRUE(through.has_value());
	EXPECT_TRUE(sys.routeCrossesPricedFaces(routeOf(*through))) << "through the long hedge";
	EXPECT_LT(lengthOf(*through), 7.0F);
}

// The reach pre-check and the path agree on a goal sealed in by trees, on camera or off: both
// say it can be reached (decision 22's "reachable, then no path" is gone).
TEST_F(NavigationSystemTest, ReachAndPathAgreeOnAGoalSealedByFlora) {
	registerTree("Test_NavSealTree", 0.3F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						goal{20.0F, 20.0F};
	placeTrees(executor, processed, "Test_NavSealTree", treeRing(goal, 3.0F));

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	for (const glm::vec2 view : {goal, glm::vec2{-400.0F, -400.0F}}) {
		sys.setViewportRect(view, {30.0F, 30.0F});
		step(world, sys);
		EXPECT_TRUE(sys.reaches(sys.reachFrom({10.0F, 20.0F}, kAgentRadius), goal));
		EXPECT_TRUE(sys.requestPath({10.0F, 20.0F}, goal, kAgentRadius).has_value()) << "view at " << view.x;
	}
}

// Trimming a cell's flora off camera never changes whether a pair across it is reachable or has a
// path; it only straightens routes, so a length stays the same or gets shorter, within what two
// triangulations of the same ground may differ by (2% and two radii).
TEST_F(NavigationSystemTest, TrimmingFloraOnlyShortensRoutes) {
	registerTree("Test_NavTrimTree", 0.3F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	std::vector<glm::vec2>				here;
	std::vector<glm::vec2>				there;
	for (int i = 0; i < 8; ++i) {
		for (int j = 0; j < 8; ++j) {
			here.push_back({4.0F + 4.0F * static_cast<float>(i) + (j % 2 == 0 ? 0.0F : 1.5F), 4.0F + 4.0F * static_cast<float>(j)});
			there.push_back({300.0F + 4.0F * static_cast<float>(i), 300.0F + 4.0F * static_cast<float>(j)});
		}
	}
	std::vector<glm::vec2> trees = here;
	trees.insert(trees.end(), there.begin(), there.end());
	placeTrees(executor, processed, "Test_NavTrimTree", trees);

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>(std::size_t{40});
	sys.setChunkManager(landChunks());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	sys.setViewportRect({-600.0F, -600.0F}, {30.0F, 30.0F});
	const EntityID mover = addColonist(world, {20.0F, 20.0F}, "Keeper");
	settle(sys);
	ASSERT_TRUE(std::none_of(here.begin(), here.end(), [&](glm::vec2 t) { return sys.isValidPosition(t); })) << "the trees are in";

	std::vector<std::pair<glm::vec2, glm::vec2>> pairs;
	std::uint32_t								 seed = 11;
	const auto									 unit = [&seed] {
		 seed = seed * 1664525U + 1013904223U;
		 return static_cast<float>(seed >> 8) / static_cast<float>(1U << 24);
	};
	for (int i = 0; i < 40; ++i) {
		pairs.push_back({{1.0F + 36.0F * unit(), 1.0F + 36.0F * unit()}, {1.0F + 36.0F * unit(), 1.0F + 36.0F * unit()}});
	}
	struct Answer {
		bool					 reaches = false;
		std::optional<std::vector<glm::vec2>> path;
	};
	const auto answers = [&] {
		std::vector<Answer> out;
		for (const auto& [a, b] : pairs) {
			out.push_back({sys.reaches(sys.reachFrom(a, kAgentRadius), b), sys.requestPath(a, b, kAgentRadius)});
		}
		return out;
	};
	const std::vector<Answer> before = answers();

	world.getComponent<Position>(mover)->value = {316.0F, 316.0F};
	settle(sys);
	settle(sys); // the trim runs as the next gather finds the mesh over its budget
	ASSERT_TRUE(std::all_of(here.begin(), here.end(), [&](glm::vec2 t) { return sys.isValidPosition(t); })) << "the trees were trimmed";
	const std::vector<Answer> after = answers();

	int identical = 0;
	int compared  = 0;
	for (std::size_t i = 0; i < pairs.size(); ++i) {
		EXPECT_EQ(after[i].reaches, before[i].reaches) << "pair " << i;
		ASSERT_EQ(after[i].path.has_value(), before[i].path.has_value()) << "pair " << i;
		if (!before[i].path.has_value()) {
			continue;
		}
		++compared;
		identical += *after[i].path == *before[i].path ? 1 : 0;
		const float was = lengthOf(*before[i].path);
		EXPECT_LE(lengthOf(*after[i].path), was * 1.02F + 2.0F * kAgentRadius) << "pair " << i;
	}
	EXPECT_GT(compared, 30);
	::testing::Test::RecordProperty("identical", identical);
	::testing::Test::RecordProperty("compared", compared);
}

// --- Staleness: flora marks only the routes it lands on -------------------------------------

namespace {
	// A mover that asks for no flora of its own, on a straight route stamped current.
	EntityID routedMover(World& world, const NavigationSystem& nav, glm::vec2 from, glm::vec2 to) {
		const EntityID e = world.createEntity();
		world.addComponent<Position>(e, Position{from});
		NavPath path	   = routeOf({from, to});
		path.current	   = 1;
		path.builtNavVersion = nav.generation();
		world.addComponent<NavPath>(e, std::move(path));
		return e;
	}

	bool stale(World& world, const NavigationSystem& nav, EntityID e) {
		return world.getComponent<NavPath>(e)->builtNavVersion != nav.generation();
	}
} // namespace

// A tree landing a few metres off a mover's route, in the same flora cell, leaves the route alone;
// one landing across it marks it stale.
TEST_F(NavigationSystemTest, TreeLandingMarksOnlyTheRoutesItCrosses) {
	registerTree("Test_NavMarkTree", 0.3F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	placeTrees(executor, processed, "Test_NavMarkTree", {{-20.0F, 40.0F}}); // chunk (-1, 0), so (0, 0) arrives later

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	const EntityID missed = routedMover(world, sys, {1.0F, 20.0F}, {31.0F, 20.0F});
	const EntityID hit	  = routedMover(world, sys, {1.0F, 40.0F}, {31.0F, 40.0F});
	const std::uint64_t generation = sys.generation();

	placeTrees(executor, processed, "Test_NavMarkTree", {{10.0F, 26.0F}, {10.0F, 40.0F}});
	step(world, sys);
	step(world, sys);
	ASSERT_FALSE(sys.isValidPosition({10.0F, 26.0F})) << "the trees are in";
	EXPECT_EQ(sys.generation(), generation);
	EXPECT_FALSE(stale(world, sys, missed)) << "a tree 6 m off the route replanned it";
	EXPECT_TRUE(stale(world, sys, hit)) << "a tree across the route didn't";
}

// The view pans over a stand of trees that streams into the mesh as it goes: the mover whose
// route crosses the stand is marked stale, the one whose route is far from any tree never is, and
// the generation never moves.
TEST_F(NavigationSystemTest, PanningFloraMarksOnlyRoutesItTouches) {
	registerTree("Test_NavPanMarkTree", 0.3F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	std::vector<glm::vec2>				stand;
	for (float x = 160.0F; x < 260.0F; x += 3.0F) {
		for (float y = 0.0F; y < 16.0F; y += 3.0F) {
			stand.push_back({x, y + 1.0F});
		}
	}
	placeTrees(executor, processed, "Test_NavPanMarkTree", stand);

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	sys.setViewportRect({0.0F, -300.0F}, {30.0F, 30.0F});
	settle(sys);
	ASSERT_TRUE(sys.isValidPosition(stand[5])) << "the stand isn't in yet";
	const EntityID crossing = routedMover(world, sys, {199.0F, -10.0F}, {199.0F, 30.0F});
	const EntityID clear	= routedMover(world, sys, {150.0F, 200.0F}, {270.0F, 200.0F});
	const std::uint64_t generation = sys.generation();

	bool clearMarked = false;
	for (float x = 120.0F; x <= 300.0F; x += 2.0F) {
		sys.setViewportRect({x, 8.0F}, {30.0F, 30.0F});
		step(world, sys);
		clearMarked = clearMarked || stale(world, sys, clear);
	}
	ASSERT_FALSE(sys.isValidPosition({199.0F, 1.0F})) << "the stand streamed in";
	EXPECT_EQ(sys.generation(), generation);
	EXPECT_TRUE(stale(world, sys, crossing));
	EXPECT_FALSE(clearMarked);
}

// A move order clicked on a tree snaps its goal to clear ground beside the trunk, on camera and
// off: the walker goes round on a route that crosses no tree, stops beside it and stays there.
TEST_F(NavigationSystemTest, MoveOrderOntoATreeEndsBesideIt) {
	registerTree("Test_NavOrderTree", 0.4F);
	const glm::vec2 treeAt{10.0F, 5.0F};
	for (const bool onCamera : {true, false}) {
		const char* where = onCamera ? "on camera" : "off camera";
		WalkRig		rig(onCamera ? treeAt : glm::vec2{-300.0F, 300.0F});
		placeTrees(rig.executor, rig.processed, "Test_NavOrderTree", {treeAt});
		addColonist(rig.world, {0.0F, 0.0F}, "Keeper"); // brings the tree into the mesh off camera
		rig.start();
		const EntityID	colonist = rig.walker({2.0F, 5.0F}, treeAt);
		const glm::vec2 goal	 = rig.world.getComponent<Task>(colonist)->targetPosition;
		EXPECT_TRUE(rig.nav.isValidPosition(goal)) << where << ": the goal is clear ground";
		EXPECT_LT(glm::distance(goal, treeAt), 1.0F) << where << ": beside the trunk";
		EXPECT_FALSE(rig.pricedRoute(colonist)) << where << ": no tree to cross";

		const Position&		   position = *rig.world.getComponent<Position>(colonist);
		std::vector<glm::vec2> settled;
		for (int i = 0; i < 120; ++i) {
			rig.world.update(0.1F);
			if (i >= 100) {
				settled.push_back(position.value);
			}
		}
		for (const glm::vec2 p : settled) {
			EXPECT_EQ(p, settled.front()) << where << ": still moving";
		}
		EXPECT_LT(glm::distance(position.value, goal), 0.1F) << where << ": stopped on its goal";
	}
}

// A route through a tree that was already there isn't news when another tree lands in the same
// cell off the route: only the trees that just landed are checked.
TEST_F(NavigationSystemTest, OldTreeOnARouteIsNoNewsWhenAnotherLands) {
	registerTree("Test_NavOldTree", 0.3F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						old{1.0F, 8.0F};
	std::vector<glm::vec2>				trees{old};
	for (int i = 0; i < 70; ++i) { // after the old one in key order: they land over three ticks, the last after the route is planned
		trees.push_back({12.0F + 0.05F * static_cast<float>(i), 1.0F + 0.2F * static_cast<float>(i)});
	}
	placeTrees(executor, processed, "Test_NavOldTree", trees);

	const std::unique_ptr<world::ChunkManager> chunks = readyChunks(landSampler(), {256.0F, 256.0F}, 0); // one chunk: its ground lands at once
	ConstructionWorld						   cw;
	World									   world;
	NavigationSystem&						   sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	sys.setViewportRect({8.0F, 8.0F}, {30.0F, 30.0F});
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	const auto in = [&](glm::vec2 t) { return sys.inSimArea(t) && !sys.isValidPosition(t); };
	for (int i = 0; i < 20 && !in(old); ++i) {
		step(world, sys);
	}
	ASSERT_TRUE(in(old)) << "the old tree is in";
	step(world, sys); // its landing is taken note of, and the next batch lands
	ASSERT_FALSE(in(trees.back())) << "the last trees are still to come";
	const EntityID mover = routedMover(world, sys, {1.0F, 0.5F}, {1.0F, 15.0F});
	step(world, sys);
	step(world, sys);
	ASSERT_TRUE(in(trees.back())) << "the rest landed";
	EXPECT_FALSE(stale(world, sys, mover)) << "a tree landing off the route replanned it";
}

// A tree just inside a cell's edge overhangs into the next cell by more than a mover's radius: a
// route there, which never enters the tree's cell, is still marked when the tree lands.
TEST_F(NavigationSystemTest, TreeOverhangingItsCellMarksARouteNextDoor) {
	registerTree("Test_NavEdgeTree", 0.45F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	placeTrees(executor, processed, "Test_NavEdgeTree", {{-20.0F, 40.0F}}); // chunk (-1, 0), so (0, 0) arrives later

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	const EntityID mover = routedMover(world, sys, {15.65F, 0.5F}, {15.65F, 15.0F}); // cell x in [0, 16)

	const glm::vec2 edgeTree{16.05F, 8.0F}; // cell x in [16, 32), reaching back to 15.55
	placeTrees(executor, processed, "Test_NavEdgeTree", {edgeTree});
	step(world, sys);
	step(world, sys);
	ASSERT_FALSE(sys.isValidPosition(edgeTree)) << "the tree is in";
	EXPECT_TRUE(stale(world, sys, mover)) << "the tree lies across the route";
}

// An idle colonist (no route) a tree lands on, beside a wall: the snap takes it to clear ground on
// its own side of the wall and out of the wall's band, never into or past the wall.
TEST_F(ForwardSnapTest, IdleColonistBesideAWallStaysOnItsSide) {
	buildWall(cw, {10900, 5000}, {10900, 15000});
	settle(*sys);
	const glm::vec2 at{10.6F, 10.0F}; // in the tree (9.15 to 10.85), the wall 0.3 m east
	const EntityID	colonist = routedColonist(world, at, {});
	world.addComponent<AgentRadius>(colonist, AgentRadius{kAgentRadius, 1.0F});
	step(world, *sys);
	const glm::vec2 now = world.getComponent<Position>(colonist)->value;
	EXPECT_NE(now, at) << "snapped out of the tree";
	EXPECT_TRUE(sys->isValidPosition(now));
	EXPECT_LT(now.x, 10.9F - 0.1F - kAgentRadius) << "on its own side and out of the wall's band";
}

// A wall whose centreline crosses a chunk with no vertex in it holds that chunk's ground like one
// that ends there.
TEST_F(NavigationSystemTest, WallCrossingAChunkKeepsItsGround) {
	ChunkWalk		  walk;
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(walk.chunks.get());
	sys.setConstructionWorld(&cw);

	walk.stand(1);
	buildWall(cw, nav::toMm(ChunkWalk::centre(0)), nav::toMm(ChunkWalk::centre(2))); // through chunk 1, ends in 0 and 2
	settle(sys);
	const int steps = static_cast<int>(NavigationSystem::kKeptUnloadedChunks) + 5;
	for (int i = 3; i < 3 + steps; ++i) {
		walk.stand(i);
		settle(sys);
	}
	EXPECT_TRUE(sys.inSimArea(ChunkWalk::centre(1))) << "the chunk the wall crosses kept its ground";
}

// --- Colonists away from the others, and colonists moved off their routes -------------------

namespace {
	constexpr std::uint32_t kTestWaterDefId = 1002;

	// A colonist the real AI drives on its own (no move order): needs met but the one given.
	EntityID needyColonist(WalkRig& rig, glm::vec2 at, NeedType need, float value) {
		const EntityID	colonist = spawnRecoverableColonist(rig.world, at);
		rig.world.addComponent<Colonist>(colonist, Colonist{"Far"});
		rig.world.addComponent<AgentRadius>(colonist, AgentRadius{kAgentRadius, 1.0F});
		NeedsComponent& needs = *rig.world.getComponent<NeedsComponent>(colonist);
		for (const NeedType n : {NeedType::Hunger, NeedType::Thirst, NeedType::Energy, NeedType::Bladder, NeedType::Digestion}) {
			needs.get(n).value = n == need ? value : 100.0F;
		}
		return colonist;
	}
} // namespace

// A thirsty colonist 110 m from the others and the view, knowing water 90 m on across open ground
// with trees in it: the mesh covers both ends wherever they are, so it gets a route and walks to the
// water, and never holds for an off-area endpoint (the old regions' failure).
TEST_F(NavigationSystemTest, FarColonistReachesWaterItKnows) {
	registerTree("Test_NavFarTree", 0.3F);
	WalkRig rig({0.0F, 0.0F});
	std::vector<glm::vec2> trees;
	for (float x = 120.0F; x < 190.0F; x += 7.0F) {
		trees.push_back({x, -3.0F + std::fmod(x, 6.0F)});
	}
	placeTrees(rig.executor, rig.processed, "Test_NavFarTree", trees);
	addColonist(rig.world, {0.0F, 0.0F}, "Home");
	rig.start();

	const glm::vec2 water{200.0F, 0.0F};
	const EntityID	colonist = needyColonist(rig, {110.0F, 0.0F}, NeedType::Thirst, 10.0F);
	rig.world.getComponent<Memory>(colonist)->rememberWorldEntity(
		water, kTestWaterDefId, static_cast<std::uint16_t>(1U << static_cast<std::size_t>(assets::CapabilityType::Drinkable)));

	const Position& position = *rig.world.getComponent<Position>(colonist);
	const Task&		task	 = *rig.world.getComponent<Task>(colonist);
	float			nearest	 = glm::distance(position.value, water);
	bool			routed	 = false;
	for (int i = 0; i < 800 && nearest > 0.5F; ++i) {
		rig.world.update(0.1F);
		ASSERT_NE(task.navState, NavState::AwaitingMesh) << "tick " << i << ": held for an off-area endpoint";
		routed	= routed || (task.state == TaskState::Moving && task.targetPosition == water);
		nearest = std::min(nearest, glm::distance(position.value, water));
	}
	EXPECT_TRUE(routed) << "it never set off for the water";
	EXPECT_LT(nearest, 0.5F) << "never got to the water";
}

// A colonist walking a move order is moved 60 m (a dev teleport sets Position and nothing else). Its
// route is marked stale and replanned from where it now stands, so it walks to its goal instead of
// steering straight back at its old waypoint; with a wall it knows between the two it goes round.
TEST_F(NavigationSystemTest, ColonistMovedOffItsRouteReplansFromWhereItIs) {
	for (const bool wallBetween : {false, true}) {
		const char* where = wallBetween ? "wall between" : "open ground";
		WalkRig		rig({0.0F, 0.0F});
		if (wallBetween) {
			buildWall(rig.cw, {30000, -20000}, {30000, 50000}); // between the jump and the old route
		}
		rig.start();
		const glm::vec2 goal{20.0F, 5.0F};
		const EntityID	colonist = rig.walker({0.0F, 5.0F}, goal);
		for (const std::uint64_t id : allSegmentIds(rig.cw)) {
			rig.world.getComponent<Memory>(colonist)->rememberSegment(id);
		}
		Position& position = *rig.world.getComponent<Position>(colonist);
		for (int i = 0; i < 20; ++i) {
			rig.world.update(0.1F);
		}
		position.value = {50.0F, 30.0F}; // what the dev teleport does
		const Task& task = *rig.world.getComponent<Task>(colonist);
		for (int i = 0; i < 600 && glm::distance(position.value, goal) > 0.3F; ++i) {
			rig.world.update(0.1F);
			if (wallBetween) {
				ASSERT_FALSE(std::abs(position.value.x - 30.0F) < 0.15F && position.value.y > -20.0F && position.value.y < 50.0F)
					<< where << ", tick " << i << ": in the wall at " << position.value.x << ", " << position.value.y;
			}
		}
		EXPECT_EQ(task.targetPosition, goal) << where << ": the order was dropped";
		EXPECT_LT(glm::distance(position.value, goal), 0.3F) << where << ": never got back to its goal";
	}
}

// --- Goals in ground that isn't coming ------------------------------------------------------

namespace {
	const EvaluatedOption* thirstOption(World& world, EntityID colonist) {
		for (const EvaluatedOption& option : world.getComponent<DecisionTrace>(colonist)->options) {
			if (option.taskType == TaskType::FulfillNeed && option.needType == NeedType::Thirst) {
				return &option;
			}
		}
		return nullptr;
	}

	void rememberWater(World& world, EntityID colonist, glm::vec2 at) {
		world.getComponent<Memory>(colonist)->rememberWorldEntity(
			at, kTestWaterDefId, static_cast<std::uint16_t>(1U << static_cast<std::size_t>(assets::CapabilityType::Drinkable)));
	}
} // namespace

// A thirsty colonist whose only known water lies in ground that is neither meshed nor loaded (a
// chunk dropped long ago, or never visited): the water reads unreachable, so the colonist doesn't
// hold for it; the thirst option finds no reachable source, as for any unreachable one.
TEST_F(NavigationSystemTest, WaterInGroundThatIsntComingIsSkipped) {
	WalkRig rig({0.0F, 0.0F});
	rig.start();
	ASSERT_FALSE(rig.nav.inSimArea(kUnmeshed));
	ASSERT_EQ(landChunks()->getChunk(world::worldToChunk({kUnmeshed.x, kUnmeshed.y})), nullptr) << "not loaded";
	EXPECT_FALSE(rig.nav.reaches(rig.nav.reachFrom({0.0F, 0.0F}, kAgentRadius), kUnmeshed));

	const EntityID colonist = needyColonist(rig, {0.0F, 0.0F}, NeedType::Thirst, 10.0F);
	rememberWater(rig.world, colonist, kUnmeshed);
	const Task& task = *rig.world.getComponent<Task>(colonist);
	for (int i = 0; i < 20; ++i) {
		rig.world.update(0.1F);
		ASSERT_NE(task.navState, NavState::AwaitingMesh) << "tick " << i << ": held for water in ground that isn't coming";
		ASSERT_NE(task.targetPosition, kUnmeshed) << "tick " << i;
	}
	const EvaluatedOption* thirst = thirstOption(rig.world, colonist);
	ASSERT_NE(thirst, nullptr);
	EXPECT_NE(thirst->status, OptionStatus::Selected) << "no reachable source";
}

// Knowing that nearer water and a farther source on meshed ground, the colonist takes the farther.
TEST_F(NavigationSystemTest, FartherReachableWaterBeatsNearerWaterInGroundThatIsntComing) {
	WalkRig rig({1000.0F, 0.0F});
	rig.start();
	const glm::vec2 gone{1700.0F, 0.0F};   // chunk (3, 0), beyond the loaded radius
	const glm::vec2 meshed{-900.0F, 0.0F}; // chunk (-2, 0)
	ASSERT_EQ(landChunks()->getChunk(world::worldToChunk({gone.x, gone.y})), nullptr);
	ASSERT_TRUE(rig.nav.inSimArea(meshed));

	const EntityID colonist = needyColonist(rig, {1000.0F, 0.0F}, NeedType::Thirst, 10.0F);
	rememberWater(rig.world, colonist, gone);
	rememberWater(rig.world, colonist, meshed);
	rig.world.update(0.1F);
	const EvaluatedOption* thirst = thirstOption(rig.world, colonist);
	ASSERT_NE(thirst, nullptr);
	EXPECT_EQ(thirst->status, OptionStatus::Selected);
	ASSERT_TRUE(thirst->targetPosition.has_value());
	EXPECT_EQ(*thirst->targetPosition, meshed);
	EXPECT_NE(rig.world.getComponent<Task>(colonist)->navState, NavState::AwaitingMesh);
}
