#include "NavigationSystem.h"
#include "NavigationSystem.test.h"

#include "AIDecisionSystem.h"
#include "ActionSystem.h"
#include "MovementSystem.h"
#include "PhysicsSystem.h"
#include "StaticRectCollisionSystem.h"
#include "../World.h"
#include "../components/Action.h"
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
#include <world/chunk/ChunkSampleResult.h>
#include <world/BiomeWeights.h>
#include <worldgen/sampling/RiverNetwork2D.h>
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

	// Ground the nav mesh holds clear: on it, and neither water nor a tree in it. Tests that watch flora
	// come into and leave the mesh read it, where isValidPosition reads trees from the placement data.
	bool meshClear(const NavigationSystem& sys, glm::vec2 meters) {
		const geometry::nav::PatchableMesh& mesh = sys.navMesh();
		const std::int32_t					tri	 = mesh.locate(nav::toMm(meters));
		return tri >= 0 && geometry::nav::terrainTraversable(mesh.triangles()[static_cast<std::size_t>(tri)]);
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
		tree.setCollision(0, {.type = CollisionShapeType::Rect, .halfExtentsMeters = {halfExtent, halfExtent}});
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

	// A mover at `center` whose disc covers the square of `halfExtentMeters` around it, so the ground
	// and placed obstacles there are meshed. It is not a colonist: nothing but nav acts on it.
	EntityID meshAround(World& world, glm::vec2 center, float halfExtentMeters) {
		const EntityID e = world.createEntity();
		world.addComponent<Position>(e, Position{center});
		world.addComponent<AgentRadius>(e, AgentRadius{});
		Memory memory;
		memory.sightRadius = std::max(0.0F, halfExtentMeters * 1.4143F - NavigationSystem::kSightBufferMeters);
		world.addComponent<Memory>(e, std::move(memory));
		return e;
	}

	class NavigationSystemTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			ConstructionRegistry::Get().clear();
			ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder()));
		}
		void TearDown() override { ConstructionRegistry::Get().clear(); }

		// The shared land chunks, every one of them meshed by a mover at the origin (meshAround).
		static void wireArea(World& world, NavigationSystem& sys) {
			sys.setChunkManager(landChunks());
			meshAround(world, {0.0F, 0.0F}, 1536.0F);
		}
	};

	EntityID addColonist(World& world, glm::vec2 at, const char* name) {
		const EntityID e = world.createEntity();
		world.addComponent<Position>(e, Position{at});
		world.addComponent<Colonist>(e, Colonist{name});
		world.addComponent<Task>(e, Task{});
		world.addComponent<AgentRadius>(e, AgentRadius{});
		return e;
	}

} // namespace

// A query before any mesh is built returns nullopt; reaches() can't prove unreachable.
TEST_F(NavigationSystemTest, NoMeshBeforeBuildReturnsNullopt) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();

	EXPECT_FALSE(sys.hasMesh());
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).route.has_value());
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
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).route.has_value());
}

// A path from outside to inside succeeds THROUGH a built door.
TEST_F(NavigationSystemTest, PathThroughDoorSucceeds) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);

	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	std::optional<std::vector<glm::vec2>> path = sys.requestPath(kOutside, kInside, kAgentRadius).route;
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
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);

	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).route.has_value());
}

// No opening at all also blocks the path.
TEST_F(NavigationSystemTest, NoOpeningBlocksPath) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);

	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).route.has_value());
}

// Open ground routes; a closed room built over the goal blocks it on the next tick; a door
// cut in the south wall opens it again on the tick after that.
TEST_F(NavigationSystemTest, WallBuiltBlocksAndDoorOpens) {
	ConstructionWorld cw;

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).route.has_value()) << "open ground routes";

	const SegmentId south = buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);
	step(world, sys);
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).route.has_value()) << "the walls block in the tick they are seen";
	EXPECT_FALSE(isReachable(sys, kOutside, kInside, kAgentRadius));

	const OpeningId op = cw.addOpening(south, 0.5F, "Door", "Wood");
	ASSERT_NE(op, kInvalidOpening);
	ASSERT_TRUE(cw.setOpeningState(op, FoundationState::Built));
	step(world, sys);
	EXPECT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).route.has_value()) << "the door opens the room";
	EXPECT_TRUE(isReachable(sys, kOutside, kInside, kAgentRadius));
}

// An open (door) room is pathable; setting the door back to Blueprint makes the band solid
// again and re-seals the room.
TEST_F(NavigationSystemTest, RebuildSealsRoomWhenDoorRemoved) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);

	settle(sys);
	ASSERT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).route.has_value()) << "door room should be pathable";

	const std::uint64_t before = cw.version();
	ASSERT_FALSE(cw.openings().empty());
	for (const auto& op : cw.openings()) {
		ASSERT_TRUE(cw.setOpeningState(op.id, FoundationState::Blueprint));
	}
	ASSERT_NE(cw.version(), before);

	settle(sys);
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).route.has_value()) << "blueprinting the door never re-sealed the room";
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

	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built after deferred wiring";
	EXPECT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).route.has_value());
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
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	EXPECT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).route.has_value()) << "truth query should route through the real door";

	const std::unordered_set<std::uint64_t> noSegments;
	const std::unordered_set<std::uint64_t> noOpenings;
	const geometry::nav::BeliefFilter		blindBelief{&noSegments, &noOpenings};
	EXPECT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius, blindBelief).route.has_value())
		<< "blind belief should optimistically route through the unseen wall";

	const std::unordered_set<std::uint64_t> knownWalls = allSegmentIds(cw);
	const geometry::nav::BeliefFilter		walledBelief{&knownWalls, &noOpenings};
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius, walledBelief).route.has_value())
		<< "knowing the walls (but not the door) should block the route";

	std::unordered_set<std::uint64_t> knownDoors;
	for (const auto& op : cw.openings()) {
		knownDoors.insert(static_cast<std::uint64_t>(op.id));
	}
	ASSERT_FALSE(knownDoors.empty());
	const geometry::nav::BeliefFilter doorBelief{&knownWalls, &knownDoors};
	EXPECT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius, doorBelief).route.has_value())
		<< "knowing the walls and the door should route through it";
}

// A goal in a room whose walls the colonist knows (and not its door) is a definite no from the search
// that starts at the goal: it runs dry inside the room in a few dozen expansions, and the search from
// the start, which would flood all the ground outside, never runs. The route says the same.
TEST_F(NavigationSystemTest, KnownWalledRoomIsADefiniteNoFromTheGoalSide) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	settle(sys);

	const std::unordered_set<std::uint64_t> knownWalls = allSegmentIds(cw);
	const std::unordered_set<std::uint64_t> noOpenings;
	const geometry::nav::BeliefFilter		walledBelief{&knownWalls, &noOpenings};
	sys.resetNavQueryStats();
	const WalkAnswer answer = sys.walk(sys.reachFrom(kOutside, kAgentRadius, walledBelief), kInside);
	EXPECT_FALSE(answer.walk.has_value());
	EXPECT_FALSE(answer.far) << "a sealed room is no, not far";
	EXPECT_EQ(sys.navQueryStats().probeAnswered, 1U);
	EXPECT_EQ(sys.navQueryStats().forwardSearches, 0U) << "the search from the start ran";
	EXPECT_LE(sys.navQueryStats().probeExpansions, 40U) << "the room took more than a few dozen expansions";

	const NavigationSystem::PathAnswer path = sys.requestPath(kOutside, kInside, kAgentRadius, walledBelief);
	EXPECT_FALSE(path.route.has_value());
	EXPECT_FALSE(path.gaveUp);
	EXPECT_EQ(sys.navQueryStats().forwardSearches, 0U);
}

// With budgets small enough that the walk into a room whose walls are known (its door isn't) is far,
// the request asks the goal-side search and one search from the start carried to the far route
// budget, each to its budget and no further, and says it gave up. A goal outside the room with the
// same budgets is far too, and that one search routes it.
TEST_F(NavigationSystemTest, FarRequestSpendsAtMostItsTwoBudgets) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	constexpr std::uint32_t kProbe	= 1;
	constexpr std::uint32_t kStart	= 3;
	constexpr std::uint32_t kFarRoute = 10;
	sys.setReachBudgets(kProbe, kStart, kFarRoute);

	const std::unordered_set<std::uint64_t> knownWalls = allSegmentIds(cw);
	const std::unordered_set<std::uint64_t> noOpenings;
	const geometry::nav::BeliefFilter		walledBelief{&knownWalls, &noOpenings};
	ASSERT_TRUE(sys.walk(sys.reachFrom(kOutside, kAgentRadius, walledBelief), kInside).far);
	sys.resetNavQueryStats();
	const NavigationSystem::PathAnswer path = sys.requestPath(kOutside, kInside, kAgentRadius, walledBelief);
	EXPECT_FALSE(path.route.has_value());
	EXPECT_TRUE(path.gaveUp);
	const auto& stats = sys.navQueryStats();
	// A search may expand one past its cap (it still tries a goal popped at the cap).
	EXPECT_LE(stats.probeExpansions, kProbe + 1);
	EXPECT_EQ(stats.forwardSearches, 1U);
	EXPECT_LE(stats.forwardExpansions, kFarRoute + 1);
	EXPECT_EQ(stats.totalQueries, 1U) << "a search that gave up wasn't counted";
	EXPECT_EQ(stats.totalNodesExpanded, stats.forwardExpansions);

	sys.setReachBudgets(kProbe, kStart, NavigationSystem::kFarRouteExpansions);
	const glm::vec2 away{30.0F, 20.0F};
	ASSERT_TRUE(sys.walk(sys.reachFrom(kOutside, kAgentRadius), away).far);
	const NavigationSystem::PathAnswer routed = sys.requestPath(kOutside, away, kAgentRadius);
	EXPECT_TRUE(routed.route.has_value());
	EXPECT_FALSE(routed.gaveUp);
}

// The walk found by the search from the goal is the walk: its length is the search from the start's,
// and the same both ways, round the room and through its door.
TEST_F(NavigationSystemTest, WalkLengthIsTheSameBothWays) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	settle(sys);

	std::uint32_t seed = 5;
	const auto	  unit = [&seed] {
		   seed = seed * 1664525U + 1013904223U;
		   return static_cast<float>(seed >> 8) / static_cast<float>(1U << 24);
	};
	std::vector<std::pair<glm::vec2, glm::vec2>> pairs{{kOutside, kInside}, {{-6.0F, 1.5F}, {10.0F, 1.5F}}, {{2.0F, 6.0F}, kInside}};
	for (int i = 0; i < 24; ++i) {
		pairs.push_back({{-20.0F + 40.0F * unit(), -20.0F + 40.0F * unit()}, {-20.0F + 40.0F * unit(), -20.0F + 40.0F * unit()}});
	}
	int walked = 0;
	for (const auto& [a, b] : pairs) {
		const WalkAnswer there = sys.walk(sys.reachFrom(a, kAgentRadius), b);
		const WalkAnswer back  = sys.walk(sys.reachFrom(b, kAgentRadius), a);
		ASSERT_FALSE(there.far || back.far);
		ASSERT_EQ(there.walk.has_value(), back.walk.has_value()) << a.x << "," << a.y << " to " << b.x << "," << b.y;
		const geometry::nav::PathResult forward =
			geometry::nav::pathThrough(sys.structuralMesh().view(), nav::toMm(a), nav::toMm(b), sys.reachFrom(a, kAgentRadius).radiusMm, {});
		ASSERT_EQ(forward.reachable, there.walk.has_value());
		if (!there.walk.has_value()) {
			continue;
		}
		++walked;
		const float forwardMeters = static_cast<float>(forward.cost / 1000.0);
		EXPECT_NEAR(*there.walk, forwardMeters, 0.002F) << "the goal-side walk is the start-side one";
		EXPECT_NEAR(*there.walk, *back.walk, 0.002F) << a.x << "," << a.y << " to " << b.x << "," << b.y;
	}
	EXPECT_GT(walked, 20);
}

// --- isReachable semantics ---------------------------------------------------
//
// false = DEFINITELY unreachable (sound), true = MAYBE reachable. No mesh => always true.

TEST_F(NavigationSystemTest, IsReachableTrueForReachableGoal) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	EXPECT_TRUE(isReachable(sys, kOutside, kInside, kAgentRadius));
	EXPECT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).route.has_value());
}

TEST_F(NavigationSystemTest, IsReachableFalseForWalledOffGoal) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	EXPECT_FALSE(isReachable(sys, kOutside, kInside, kAgentRadius));
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).route.has_value());
}

TEST_F(NavigationSystemTest, IsReachableTrueWhenNoMesh) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();

	ASSERT_FALSE(sys.hasMesh());
	EXPECT_TRUE(isReachable(sys, kOutside, kInside, kAgentRadius));
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).route.has_value());
}

// Off meshed ground: a start can't be proven unreachable from, even walled in; a goal can't be
// proven unreachable while its chunk is loaded (its ground is on the way), and is unreachable in
// ground that isn't loaded either.
TEST_F(NavigationSystemTest, IsReachableForUnmeshedGoalDependsOnItsChunk) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	EXPECT_FALSE(isReachable(sys, kOutside, kInside, kAgentRadius)) << "baseline: the closed room blocks";
	EXPECT_TRUE(isReachable(sys, kUnmeshed, kInside, kAgentRadius)) << "an unmeshed start can't be proven to reach nothing";
	EXPECT_FALSE(isReachable(sys, kOutside, kUnmeshed, kAgentRadius)) << "a goal in ground neither meshed nor loaded";

	const std::unique_ptr<world::ChunkManager> wider = readyChunks(landSampler(), {0.0F, 0.0F}, 3);
	sys.setChunkManager(wider.get()); // chunk (3, 0) is loaded now, its ground not meshed yet
	const glm::vec2 edge{1530.0F, 10.0F};	// chunk (2, 0), meshed
	const glm::vec2 coming{1560.0F, 10.0F}; // chunk (3, 0), 30 m on
	ASSERT_TRUE(sys.inSimArea(edge));
	ASSERT_FALSE(sys.inSimArea(coming));
	EXPECT_TRUE(isReachable(sys, edge, coming, kAgentRadius)) << "a goal whose ground is on the way";
}

// generation() bumps on the tick that first meshes ground an anchor holds and on every later patch
// there.
TEST_F(NavigationSystemTest, GenerationBumpsOnPatch) {
	ConstructionWorld cw;
	SegmentId		  south = buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	addColonist(world, kOutside, "Anchor");
	wireArea(world, sys);
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
	EXPECT_TRUE(first.onGround);
	EXPECT_TRUE(sys.sharesStartRegion(first, {100.0F, 0.0F})) << "meshed ground";
	EXPECT_TRUE(sys.sharesStartRegion(first, {300.0F, 0.0F})) << "meshed ground, however far";
	EXPECT_FALSE(sys.sharesStartRegion(first, kUnmeshed));
	EXPECT_FALSE(sys.reachFrom(kUnmeshed, kAgentRadius).onGround);

	buildWall(cw, {10000, 10000}, {11000, 10000});
	step(world, sys);
	const NavigationSystem::Reach after = sys.reachFrom({0.0F, 0.0F}, kAgentRadius);
	EXPECT_EQ(after.startCell, first.startCell);
	EXPECT_NE(after.structuralEpoch, first.structuralEpoch) << "the patch left the reach key unchanged";
}

// --- Path query instrumentation -----------------------------------------------

TEST_F(NavigationSystemTest, NavQueryStatsPopulatedOnPath) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	sys.resetNavQueryStats();
	EXPECT_EQ(sys.navQueryStats().totalQueries, 0u);

	ASSERT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).route.has_value());

	const NavigationSystem::NavQueryStats& s = sys.navQueryStats();
	EXPECT_EQ(s.totalQueries, 1u) << "one reachable solve recorded";
	EXPECT_GT(s.lastNodesExpanded, 0) << "a real solve expands at least one node";
	EXPECT_EQ(s.totalNodesExpanded, static_cast<std::uint64_t>(s.lastNodesExpanded));
	EXPECT_GT(s.lastPeakOpenSet, 0);

	ASSERT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).route.has_value());
	EXPECT_EQ(sys.navQueryStats().totalQueries, 2u) << "cumulative query count moves";
	EXPECT_GE(sys.navQueryStats().totalNodesExpanded, s.totalNodesExpanded);
}

// --- Flora ---------------------------------------------------------------------

// A mover meshes the 16 m cells its disc of radius R (sight plus kSightBufferMeters) meets, so a
// longer sight distance meshes a larger disc. From (8, 8), R = 46 m reaches the cell [48, 64) and not
// [64, 80); a sight of 70 m (R = 86 m) reaches [80, 96) and not [96, 112).
TEST_F(NavigationSystemTest, MoverWithLongerSightMeshesALargerDisc) {
	ASSERT_EQ(kDefaultSightRadius + NavigationSystem::kSightBufferMeters, 46.0F);
	registerTree("Test_NavSightTree", 0.4F);
	const glm::vec2 inDefault{52.0F, 8.0F};
	const glm::vec2 pastDefault{68.0F, 8.0F};
	const glm::vec2 inLong{90.0F, 8.0F};
	const glm::vec2 pastLong{100.0F, 8.0F};

	auto meshedWithSight = [&](float sight) {
		PlacementExecutor					executor(AssetRegistry::Get());
		std::unordered_set<ChunkCoordinate> processed;
		placeTrees(executor, processed, "Test_NavSightTree", {inDefault, pastDefault, inLong, pastLong});
		ConstructionWorld cw;
		World			  world;
		NavigationSystem& sys = world.registerSystem<NavigationSystem>();
		sys.setChunkManager(landChunks());
		sys.setPlacementData(&executor, &processed);
		sys.setConstructionWorld(&cw);
		const EntityID mover = addColonist(world, {8.0F, 8.0F}, "Lookout");
		Memory		   memory;
		memory.sightRadius = sight;
		world.addComponent<Memory>(mover, std::move(memory));
		settle(sys);
		std::vector<bool> in;
		for (const glm::vec2 t : {inDefault, pastDefault, inLong, pastLong}) {
			in.push_back(!meshClear(sys, t));
		}
		return in;
	};
	EXPECT_EQ(meshedWithSight(kDefaultSightRadius), (std::vector<bool>{true, false, false, false}));
	EXPECT_EQ(meshedWithSight(70.0F), (std::vector<bool>{true, true, true, false}));
}

// Flora enters at most kDetailRingsPerTick rings a tick, the cells in the order the mover's disc
// wanted them, and finishPendingBuilds takes in the rest.
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
	sys.setChunkManager(landChunks());
	addColonist(world, {15.0F, 15.0F}, "Walker");
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);

	auto inMesh = [&] {
		return static_cast<std::size_t>(std::count_if(trees.begin(), trees.end(), [&](glm::vec2 t) { return !meshClear(sys, t); }));
	};
	step(world, sys);
	ASSERT_TRUE(sys.inSimArea({1.0F, 1.0F})) << "the mover's chunks are meshed on the first tick";
	EXPECT_EQ(inMesh(), NavigationSystem::kDetailRingsPerTick) << "the first tick takes in one budget of flora";
	EXPECT_FALSE(meshClear(sys, {1.5F, 1.5F})) << "the first cell wanted goes in first";
	step(world, sys);
	EXPECT_EQ(inMesh(), 2 * NavigationSystem::kDetailRingsPerTick);
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
	wireArea(world, sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	const glm::vec2 treeAt{10.0F, 10.0F};
	ASSERT_TRUE(meshClear(sys, treeAt));
	const std::uint64_t before = sys.generation();

	placeTrees(executor, processed, "Test_NavLatePlaceTree", {treeAt});
	settle(sys);
	EXPECT_FALSE(meshClear(sys, treeAt)) << "the late tree is in the mesh";
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
	wireArea(world, sys);
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
	wireArea(world, sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	const auto through = sys.requestPath(kOutside, kInside, kAgentRadius).route;
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
	EXPECT_TRUE(meshClear(sys, treeAt)) << "the trunk's ground is walkable";
	const auto open = sys.requestPath(kOutside, kInside, kAgentRadius).route;
	ASSERT_TRUE(open.has_value());
	NavPath openPath;
	openPath.waypoints = *open;
	openPath.valid	   = true;
	EXPECT_FALSE(sys.routeCrossesPricedFaces(openPath)) << "the doorway is open ground";
	EXPECT_FALSE(meshClear(sys, {30.0F, 30.0F})) << "the other tree stays";
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
	addColonist(world, {0.0F, 0.0F}, "Anchor");
	wireArea(world, sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	const NavigationSystem::Reach before	 = sys.reachFrom({0.0F, 0.0F}, kAgentRadius);
	const std::uint64_t			  generation = sys.generation();

	ASSERT_TRUE(executor.removeEntity(world::worldToChunk({treeAt.x, treeAt.y}), treeAt, "Test_NavReachKeyTree"));
	step(world, sys);
	ASSERT_TRUE(meshClear(sys, treeAt)) << "the felled tree was patched out";
	EXPECT_EQ(sys.generation(), generation) << "a flora patch moved the generation";
	EXPECT_EQ(sys.reachFrom({0.0F, 0.0F}, kAgentRadius).structuralEpoch, before.structuralEpoch)
		<< "a flora patch moved the reach key";

	placeTrees(executor, processed, "Test_NavReachKeyTree", {{-14.0F, 10.0F}}); // late placement in chunk (-1, 0)
	step(world, sys);
	ASSERT_FALSE(meshClear(sys, {-14.0F, 10.0F})) << "the late tree is in";
	EXPECT_EQ(sys.generation(), generation);
	EXPECT_EQ(sys.reachFrom({0.0F, 0.0F}, kAgentRadius).structuralEpoch, before.structuralEpoch);

	buildWall(cw, {20000, 20000}, {22000, 20000});
	step(world, sys);
	EXPECT_NE(sys.generation(), generation) << "a wall moves the generation";
	EXPECT_NE(sys.reachFrom({0.0F, 0.0F}, kAgentRadius).structuralEpoch, before.structuralEpoch)
		<< "a wall moves the reach key";
}

// Many placed obstacles leaving the world at once (felled) queue for removal. That backlog drains
// under its own budget and never holds back the obstacles entering around a mover that arrives.
TEST_F(NavigationSystemTest, FelledBacklogDoesntHoldBackAdds) {
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
	ASSERT_GT(lost.size(), NavigationSystem::kDetailRemovalsPerTick);
	placeTrees(executor, processed, "Test_NavLostTree", lost);
	placeTrees(executor, processed, "Test_NavLostTree", ahead);

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	const EntityID mover = meshAround(world, {228.0F, 214.0F}, 40.0F);
	settle(sys);
	auto inMesh = [&](const std::vector<glm::vec2>& trees) {
		return static_cast<std::size_t>(
			std::count_if(trees.begin(), trees.end(), [&](glm::vec2 t) { return sys.inSimArea(t) && !meshClear(sys, t); }));
	};
	ASSERT_EQ(inMesh(lost), lost.size());
	ASSERT_EQ(inMesh(ahead), 0u) << "no disc meets the other trees yet";

	// Every tree round the mover is felled as it moves on to the other trees.
	for (const glm::vec2 t : lost) {
		ASSERT_TRUE(executor.removeEntity(world::worldToChunk({t.x, t.y}), t, "Test_NavLostTree"));
	}
	world.getComponent<Position>(mover)->value = {-228.0F, 214.0F};
	sys.resetNavQueryStats();
	std::size_t ticks = 0;
	for (; ticks < 4 && inMesh(ahead) == 0; ++ticks) { // the empty cells wanted first take their scans
		step(world, sys);
	}
	EXPECT_GT(inMesh(ahead), 0u) << "adds went ahead of the backlog";
	EXPECT_LE(inMesh(ahead), NavigationSystem::kDetailRingsPerTick);
	EXPECT_GT(inMesh(lost), 0u) << "while it was still draining";
	EXPECT_LT(inMesh(lost), lost.size()) << "removals drained alongside";
	EXPECT_LE(lost.size() - inMesh(lost), ticks * NavigationSystem::kDetailRemovalsPerTick) << "under their own budget";
	EXPECT_LE(sys.navQueryStats().navPatches, ticks * 2 * NavigationSystem::kDetailCellsPerTick + 1) << "a few cells each way and the new chunk's ground";

	settle(sys);
	EXPECT_EQ(inMesh(lost), 0u);
	EXPECT_EQ(inMesh(ahead), ahead.size());
}

// One chunk's placement leaves and another's arrives in the same tick: the leaving chunk's obstacles
// stay in the mesh, and the arriving chunk's are scanned.
TEST_F(NavigationSystemTest, PlacementSwapInOneTickKeepsAndScans) {
	registerTree("Test_NavSwapTree", 0.4F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						leaving{-20.0F, 10.0F}; // chunk (-1, 0)
	const glm::vec2						arriving{20.0F, 10.0F}; // chunk (0, 0)
	placeTrees(executor, processed, "Test_NavSwapTree", {leaving});

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_FALSE(meshClear(sys, leaving));
	ASSERT_TRUE(meshClear(sys, arriving));

	executor.unloadChunk({-1, 0});
	processed.erase(ChunkCoordinate{-1, 0});
	const std::size_t size = processed.size();
	placeTrees(executor, processed, "Test_NavSwapTree", {arriving});
	ASSERT_EQ(processed.size(), size + 1);
	settle(sys);
	EXPECT_FALSE(meshClear(sys, leaving)) << "the leaving chunk's obstacles stay";
	EXPECT_FALSE(meshClear(sys, arriving)) << "the arriving chunk's flora is in";
}

// A query sees exactly the patches gathered through the previous tick: update() gathers, the job
// starts at endTick, and only then does a query see it.
TEST_F(NavigationSystemTest, QueriesSeePatchesFromEndTickOn) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	addColonist(world, kOutside, "Anchor");
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).route.has_value());
	const std::uint64_t generation = sys.generation();

	buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);
	sys.update(0.016F);
	EXPECT_TRUE(sys.isBuilding()) << "the room is gathered";
	EXPECT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).route.has_value()) << "but not applied within its own tick";
	EXPECT_EQ(sys.generation(), generation);

	sys.endTick();
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).route.has_value()) << "applied once the tick ended";
	EXPECT_EQ(sys.generation(), generation + 1);
	EXPECT_FALSE(sys.isBuilding());
	expectMeshesValid(sys);
}

// --- One mesh ------------------------------------------------------------------

// WOR-644: two colonists far apart. Their ground is one mesh, so a route between them exists and
// reaches() agrees.
TEST_F(NavigationSystemTest, ColonistsFarApartRouteToEachOther) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setConstructionWorld(&cw);
	const glm::vec2 a{10.0F, 10.0F};
	const glm::vec2 b{150.0F, 20.0F};
	addColonist(world, a, "A");
	addColonist(world, b, "B");

	settle(sys);
	EXPECT_TRUE(sys.requestPath(a, b, kAgentRadius).route.has_value());
	EXPECT_TRUE(isReachable(sys, a, b, kAgentRadius));
	EXPECT_EQ(sys.builtRegions().size(), 1u);
}

// The camera has no bearing on the mesh: GameScene loads the chunks round the camera, and with no
// mover there, panning onto them and away adds no ground and no placed obstacles.
TEST_F(NavigationSystemTest, CameraMoveWithNoMoverNearbyMeshesNothing) {
	registerTree("Test_NavCameraTree", 0.4F);
	auto chunks = std::make_unique<world::ChunkManager>(landSampler());
	chunks->setLoadRadius(0);
	chunks->setUnloadRadius(0);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						homeTree{20.0F, 20.0F};
	const glm::vec2						farTree{1300.0F, 300.0F}; // chunk (2, 0)
	placeTrees(executor, processed, "Test_NavCameraTree", {homeTree, farTree});

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	addColonist(world, {10.0F, 10.0F}, "Home");
	std::vector<world::ChunkAnchor> anchors;
	const auto						frame = [&](glm::vec2 camera) {
		 anchors.clear();
		 sys.appendAnchors(anchors);
		 chunks->update({camera.x, camera.y}, anchors);
		 chunks->finishPendingGeneration();
		 step(world, sys);
	};
	frame({10.0F, 10.0F});
	settle(sys);
	ASSERT_FALSE(meshClear(sys, homeTree));
	const std::size_t	slots	   = sys.navMesh().triangles().size();
	const std::uint64_t generation = sys.generation();

	sys.resetNavQueryStats();
	frame({1300.0F, 300.0F});
	frame({1300.0F, 300.0F});
	settle(sys);
	ASSERT_NE(chunks->getChunk({2, 0}), nullptr) << "the camera loaded the far chunk";
	EXPECT_FALSE(sys.inSimArea(farTree)) << "its ground is not meshed";
	frame({800.0F, -700.0F});
	frame({10.0F, 10.0F});
	settle(sys);
	EXPECT_EQ(sys.navQueryStats().navPatches, 0u) << "panning patched nothing";
	EXPECT_EQ(sys.navQueryStats().structuralPatches, 0u);
	EXPECT_EQ(sys.navMesh().triangles().size(), slots);
	EXPECT_EQ(sys.generation(), generation);
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
	meshAround(world, {512.0F, 100.0F}, 30.0F); // wants (1, 0), which isn't loaded yet
	settle(sys);
	const glm::vec2 west{500.0F, 100.0F};
	const glm::vec2 east{530.0F, 100.0F};
	ASSERT_TRUE(sys.inSimArea(west));
	ASSERT_FALSE(sys.inSimArea(east));
	EXPECT_FALSE(sys.requestPath(west, east, kAgentRadius).route.has_value());

	chunks->update({768.0F, 256.0F});
	chunks->finishPendingGeneration();
	settle(sys);
	EXPECT_TRUE(sys.requestPath(west, east, kAgentRadius).route.has_value()) << "the path crosses x = 512 m";
	EXPECT_TRUE(isReachable(sys, west, east, kAgentRadius));
}

// A chunk that unloads keeps its ground and its placed obstacles: still meshed and routable round
// them. When it loads again, with its placement placed again, nothing is patched and no obstacle is
// added twice.
TEST_F(NavigationSystemTest, UnloadedChunkKeepsItsGroundAndObstaclesAndReloadPatchesNothing) {
	registerTree("Test_NavUnloadTree", 0.4F);
	auto chunks = std::make_unique<world::ChunkManager>(landSampler());
	chunks->setLoadRadius(0);
	chunks->setUnloadRadius(0);
	chunks->update({256.0F, 256.0F});
	chunks->finishPendingGeneration();
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						treeAt{500.0F, 256.0F};
	placeTrees(executor, processed, "Test_NavUnloadTree", {treeAt});

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	const EntityID mover = addColonist(world, {490.0F, 256.0F}, "Walker"); // its disc meets chunks (0, 0) and (1, 0)
	std::vector<world::ChunkAnchor> anchors;
	const auto						load = [&](glm::vec2 camera) {
		 anchors.clear();
		 sys.appendAnchors(anchors);
		 chunks->update({camera.x, camera.y}, anchors);
		 chunks->finishPendingGeneration();
	};
	load({256.0F, 256.0F});
	ASSERT_NE(chunks->getChunk({1, 0}), nullptr);
	settle(sys);
	ASSERT_FALSE(meshClear(sys, treeAt)) << "the tree is in";
	ASSERT_TRUE(sys.inSimArea({530.0F, 256.0F}));
	const std::size_t slots = sys.navMesh().liveTriangleCount();

	// The mover walks east, the camera with it: (0, 0) unloads, and GameScene drops its placement.
	world.getComponent<Position>(mover)->value = {900.0F, 256.0F};
	load({900.0F, 256.0F});
	ASSERT_EQ(chunks->getChunk({0, 0}), nullptr);
	executor.unloadChunk({0, 0});
	processed.erase(ChunkCoordinate{0, 0});
	sys.resetNavQueryStats();
	settle(sys);
	EXPECT_TRUE(sys.inSimArea(treeAt)) << "the unloaded chunk's ground stays";
	EXPECT_FALSE(meshClear(sys, treeAt)) << "and so do its obstacles";
	EXPECT_EQ(sys.navQueryStats().navPatches, 0u) << "unloading patched the nav mesh";
	EXPECT_EQ(sys.navQueryStats().structuralPatches, 0u) << "unloading patched the structural mesh";
	EXPECT_TRUE(sys.requestPath({100.0F, 100.0F}, {700.0F, 300.0F}, kAgentRadius).route.has_value()) << "an order routes across it";

	// Back, with its placement placed again.
	const std::uint64_t before = sys.generation();
	world.getComponent<Position>(mover)->value = {490.0F, 256.0F};
	load({256.0F, 256.0F});
	ASSERT_NE(chunks->getChunk({0, 0}), nullptr);
	placeTrees(executor, processed, "Test_NavUnloadTree", {treeAt});
	settle(sys);
	EXPECT_EQ(sys.navQueryStats().navPatches, 0u) << "the reload patched the nav mesh";
	EXPECT_EQ(sys.navQueryStats().structuralPatches, 0u) << "the reload patched the structural mesh";
	EXPECT_EQ(sys.generation(), before);
	EXPECT_EQ(sys.navMesh().liveTriangleCount(), slots) << "nothing added twice";
	EXPECT_FALSE(meshClear(sys, treeAt));
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
		addColonist(r.world, {20.0F, 20.0F}, "B");
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
		const auto pa = first.sys.requestPath(from, to, kAgentRadius).route;
		const auto pb = second.sys.requestPath(from, to, kAgentRadius).route;
		ASSERT_EQ(pa.has_value(), pb.has_value());
		if (pa.has_value()) {
			EXPECT_EQ(*pa, *pb);
		}
	}
	EXPECT_EQ(first.sys.generation(), second.sys.generation());
}

// --- Chunks loading and unloading ---------------------------------------------------

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
} // namespace

namespace {
	// A river along y = 6 m from far west to kFordX metres east (its ford), on ground strewn with ponds:
	// a 4 m pond every 10 m within kFordX + 40 m of the origin, so the structural mesh is fine
	// everywhere a search to the far bank goes and the way round the ford takes thousands of steps.
	template <int kFordX> class PondFieldSampler : public world::IWorldSampler {
	  public:
		PondFieldSampler() {
			constexpr double kRiverY = 6.0;
			segments.push_back({.x0 = -2048.0, .y0 = kRiverY, .x1 = static_cast<double>(kFordX), .y1 = kRiverY, .halfWidth0 = 3.0F, .halfWidth1 = 3.0F,
								.s0 = 0.0, .s1 = 2048.0 + static_cast<double>(kFordX)});
			const int reach = (kFordX + 40) / 10;
			for (int j = -reach; j <= reach; ++j) {
				for (int i = -reach; i <= reach; ++i) {
					const double x = 10.0 * i + 5.0;
					const double y = 10.0 * j + 5.0;
					if (std::abs(y - kRiverY) < 8.0) {
						continue;
					}
					segments.push_back({.x0 = x - 2.0, .y0 = y, .x1 = x + 2.0, .y1 = y, .halfWidth0 = 1.5F, .halfWidth1 = 1.5F, .s0 = 0.0, .s1 = 4.0});
				}
			}
		}
		[[nodiscard]] world::ChunkSampleResult sampleChunk(world::ChunkCoordinate) const override {
			world::ChunkSampleResult r = world::makeUniformChunkSampleResult(world::BiomeWeights::single(world::Biome::TemperateGrassland), 1.0F);
			r.riverSegments			   = segments;
			return r;
		}
		[[nodiscard]] float	   sampleElevation(world::WorldPosition) const override { return 1.0F; }
		[[nodiscard]] uint64_t getWorldSeed() const override { return kWorldSeed; }

	  private:
		std::vector<worldgen::RiverNetwork2D::Segment> segments;
	};

	template <int kFordX> world::ChunkManager* pondFieldChunks() {
		static const std::unique_ptr<world::ChunkManager> chunks = readyChunks(std::make_unique<PondFieldSampler<kFordX>>());
		return chunks.get();
	}

	const glm::vec2 kPondStart{0.0F, 0.0F};
	const glm::vec2 kPondAcross{0.0F, 10.0F}; // across the river, 10 m in a straight line
} // namespace

// With the shipped budgets: the far bank of a river whose ford is 150 m east, among ponds, is about
// 15,000 structural steps round, past the reach searches (2,000 from the goal, 5,000 from the start):
// walk() says far. Its one search from the start, carried to 60,000, routes it, round the ford, and
// every leg of the route is clear of water.
TEST_F(NavigationSystemTest, FarGoalWithinTheRouteBudgetIsRoutedRoundTheFord) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	ConstructionWorld cw;
	sys.setChunkManager(pondFieldChunks<150>());
	meshAround(world, {0.0F, 0.0F}, 1536.0F);
	sys.setConstructionWorld(&cw);
	settle(sys);

	sys.resetNavQueryStats();
	const WalkAnswer walk = sys.walk(sys.reachFrom(kPondStart, kAgentRadius), kPondAcross);
	EXPECT_TRUE(walk.far) << "the reach searches found it within their budgets";
	EXPECT_EQ(sys.navQueryStats().probeExpansions, NavigationSystem::kReachProbeExpansions);
	EXPECT_EQ(sys.navQueryStats().forwardExpansions, NavigationSystem::kReachExpansionCap);

	sys.resetNavQueryStats();
	const NavigationSystem::PathAnswer path = sys.requestPath(kPondStart, kPondAcross, kAgentRadius);
	ASSERT_TRUE(path.route.has_value()) << "the far route search found no route";
	EXPECT_FALSE(path.gaveUp);
	EXPECT_EQ(sys.navQueryStats().forwardSearches, 1U) << "the search from the start ran more than once";
	EXPECT_EQ(sys.navQueryStats().navSearches, 0U);
	EXPECT_GT(sys.navQueryStats().forwardExpansions, NavigationSystem::kReachExpansionCap);
	EXPECT_LT(sys.navQueryStats().forwardExpansions, NavigationSystem::kFarRouteExpansions);
	const std::vector<glm::vec2>& route = *path.route;
	EXPECT_LT(glm::distance(route.back(), kPondAcross), 0.01F);
	EXPECT_GT(std::max_element(route.begin(), route.end(), [](glm::vec2 a, glm::vec2 b) { return a.x < b.x; })->x, 150.0F)
		<< "the route doesn't go round the ford";
	for (std::size_t i = 1; i < route.size(); ++i) {
		EXPECT_TRUE(sys.isSegmentWalkable(route[i - 1], route[i])) << "leg " << i << " crosses water";
	}
}

// With the shipped budgets: the ford 400 m east is about 800 m round through the ponds, past the far
// route budget's 60,000 steps too. The request gives up and says so, which is not "no route", after
// at most the two budgets.
TEST_F(NavigationSystemTest, FarGoalPastTheRouteBudgetGivesUpAndSaysSo) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	ConstructionWorld cw;
	sys.setChunkManager(pondFieldChunks<400>());
	meshAround(world, {0.0F, 0.0F}, 1536.0F);
	sys.setConstructionWorld(&cw);
	settle(sys);

	EXPECT_TRUE(sys.walk(sys.reachFrom(kPondStart, kAgentRadius), kPondAcross).far);
	sys.resetNavQueryStats();
	const NavigationSystem::PathAnswer path = sys.requestPath(kPondStart, kPondAcross, kAgentRadius);
	EXPECT_FALSE(path.route.has_value());
	EXPECT_TRUE(path.gaveUp) << "a search that ran out read as no route";
	EXPECT_EQ(sys.navQueryStats().routeSearchesGaveUp, 1U);
	EXPECT_EQ(sys.navQueryStats().forwardSearches, 1U);
	EXPECT_LE(sys.navQueryStats().forwardExpansions, NavigationSystem::kFarRouteExpansions + 1);
	EXPECT_EQ(sys.navQueryStats().totalQueries, 1U);
}

// Every structural job moves the reach key, wherever it lands (an unbounded walk can find what any of
// them changed): ground arriving in a far chunk, then walls; a quiet tick after them leaves it.
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
	meshAround(world, {512.0F, 256.0F}, 300.0F); // wants (1, 0), which isn't loaded yet
	settle(sys);
	const std::uint64_t key = sys.reachFrom({256.0F, 256.0F}, kAgentRadius).structuralEpoch;

	chunks->update({768.0F, 256.0F});
	chunks->finishPendingGeneration();
	step(world, sys);
	ASSERT_TRUE(sys.inSimArea({768.0F, 256.0F})) << "the new chunk came in";
	const std::uint64_t grounded = sys.reachFrom({256.0F, 256.0F}, kAgentRadius).structuralEpoch;
	EXPECT_NE(grounded, key) << "ground arriving far away";

	buildWall(cw, {100000, 100000}, {104000, 100000});
	buildWall(cw, {100000, 110000}, {104000, 110000});
	step(world, sys);
	const std::uint64_t walled = sys.reachFrom({256.0F, 256.0F}, kAgentRadius).structuralEpoch;
	EXPECT_NE(walled, grounded) << "walls";
	step(world, sys);
	EXPECT_EQ(sys.reachFrom({256.0F, 256.0F}, kAgentRadius).structuralEpoch, walled);
}

// --- Meshed ground: inSimArea, validity, water ---------------------------------

TEST_F(NavigationSystemTest, InSimAreaFalseBeforeBuild) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setConstructionWorld(&cw);
	meshAround(world, {0.0F, 0.0F}, 60.0F);

	ASSERT_FALSE(sys.hasMesh());
	EXPECT_FALSE(sys.inSimArea({0.0F, 0.0F}));
}

// inSimArea is true on the ground of every chunk a mover's disc meets, and false on a chunk that is
// loaded (as the camera loads chunks) but no disc meets.
TEST_F(NavigationSystemTest, InSimAreaCoversEveryChunkAMoverWants) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setConstructionWorld(&cw);
	meshAround(world, {0.0F, 0.0F}, 60.0F);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	for (const glm::vec2 corner : {glm::vec2{10.0F, 10.0F}, glm::vec2{-10.0F, 10.0F}, glm::vec2{10.0F, -10.0F}, glm::vec2{-10.0F, -10.0F}}) {
		EXPECT_TRUE(sys.inSimArea(corner)) << "each of the four chunks round the mover";
	}
	ASSERT_NE(landChunks()->getChunk(world::worldToChunk({1000.0F, -900.0F})), nullptr);
	EXPECT_FALSE(sys.inSimArea({1000.0F, -900.0F})) << "a loaded chunk no mover's disc meets";
	EXPECT_FALSE(sys.inSimArea(kUnmeshed));
}

TEST_F(NavigationSystemTest, IsValidPositionTrueOnWalkableFaceFalseOnWaterAndOffMesh) {
	ConstructionWorld cw;

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(waterPatchChunks());
	meshAround(world, {0.0F, 0.0F}, 60.0F);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	const glm::vec2 land{2.0F, 2.0F};
	ASSERT_TRUE(sys.inSimArea(land));
	EXPECT_TRUE(meshClear(sys, land)) << "a walkable land face must be a valid position";

	const glm::vec2 water{40.0F, 40.0F};
	ASSERT_TRUE(sys.inSimArea(water)) << "water is meshed ground";
	EXPECT_FALSE(meshClear(sys, water)) << "the water ring must not be a walkable face";
	EXPECT_FALSE(meshClear(sys, water));

	ASSERT_FALSE(sys.inSimArea(kUnmeshed));
	EXPECT_FALSE(meshClear(sys, kUnmeshed)) << "a point off meshed ground must be invalid";
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
	meshAround(world, {5.33F, 4.2F}, 64.0F);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	for (const glm::vec2 land : {glm::vec2{15.0F, 15.0F}, glm::vec2{-15.0F, -15.0F}, glm::vec2{40.0F, 30.0F}, glm::vec2{-30.0F, -20.0F}}) {
		EXPECT_TRUE(meshClear(sys, land)) << "land at (" << land.x << ", " << land.y << ") must be a valid position";
	}
	for (const glm::vec2 water : {glm::vec2{3.0F, 3.0F}, glm::vec2{-3.0F, -3.0F}}) {
		EXPECT_FALSE(meshClear(sys, water)) << "the pond at (" << water.x << ", " << water.y << ") must not be";
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
	meshAround(world, {0.0F, 0.0F}, 60.0F);
	settle(sys);

	for (const float along : {-3.0F, -1.0F, 1.0F, 3.0F}) {
		for (const float off : {-0.002F, 0.0F, 0.002F}) {
			EXPECT_FALSE(meshClear(sys, {off, along})) << "seam on x = 0 at y = " << along;
			EXPECT_FALSE(meshClear(sys, {along, off})) << "seam on y = 0 at x = " << along;
		}
	}
	EXPECT_FALSE(isReachable(sys, {-30.0F, 0.0F}, {0.5F, 0.5F}, kAgentRadius)) << "the pond's middle is water on every side";
	EXPECT_TRUE(sys.requestPath({-30.0F, 0.0F}, {30.0F, 0.0F}, kAgentRadius).route.has_value()) << "around the pond, across both borders";
}

// --- Landing and placement on the structural mesh -------------------------------

// Before any mover, nothing is meshed; once the ground is in, a drop point in the water
// moves to the nearest shore, and a land point stays put.
TEST_F(NavigationSystemTest, NearestTerrainWalkablePointLeavesWater) {

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	EXPECT_FALSE(sys.nearestTerrainWalkablePoint({40.0F, 40.0F}).has_value()) << "no ground meshed";
	sys.setChunkManager(waterPatchChunks());
	meshAround(world, {40.0F, 40.0F}, 40.0F);
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
	meshAround(world, {40.0F, 40.0F}, 40.0F);
	settle(sys);

	EXPECT_TRUE(meshClear(sys, spawn.center)) << "the clearing center is on land";
	for (std::size_t i = 0; i < spawn.points.size(); ++i) {
		const glm::vec2 p = spawn.points[i];
		EXPECT_TRUE(meshClear(sys, p)) << "member " << i << " at (" << p.x << ", " << p.y << ") is not walkable";
		for (int k = 0; k < 8; ++k) {
			const float angle = 6.2831853F * static_cast<float>(k) / 8.0F;
			EXPECT_TRUE(meshClear(sys, p + glm::vec2{std::cos(angle), std::sin(angle)} * kClearance)) << "member " << i << "'s disc reaches the water";
		}
		for (std::size_t j = 0; j < i; ++j) {
			EXPECT_GE(glm::distance(p, spawn.points[j]), kSeparation - 1e-4F) << "members " << j << " and " << i << " stack";
		}
		EXPECT_LT(glm::distance(p, drop), 8.0F) << "member " << i << " landed far away";
	}

	const NavigationSystem::GroupSpawn crowd = sys.groupSpawnPoints(drop, 12, kRing, kSeparation, kClearance);
	ASSERT_EQ(crowd.points.size(), 12U);
	for (std::size_t i = 0; i < crowd.points.size(); ++i) {
		EXPECT_TRUE(meshClear(sys, crowd.points[i]));
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
	meshAround(world, {0.0F, 0.0F}, 30.0F);
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
	void wireWaterPatch(World& world, NavigationSystem& sys, ConstructionWorld& cw) {
		sys.setChunkManager(waterPatchChunks());
		meshAround(world, {0.0F, 0.0F}, 60.0F);
		sys.setConstructionWorld(&cw);
	}
} // namespace

TEST_F(NavigationSystemTest, IsAreaWalkableTrueWhenWhollyOnLand) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys	 = world.registerSystem<NavigationSystem>();
	wireWaterPatch(world, sys, cw);
	settle(sys);

	const std::vector<glm::vec2> onLand{{4.0F, 4.0F}, {8.0F, 4.0F}, {8.0F, 8.0F}, {4.0F, 8.0F}};
	for (const auto& v : onLand) {
		ASSERT_TRUE(meshClear(sys, v));
	}
	EXPECT_TRUE(sys.isAreaWalkable(onLand)) << "a footprint wholly on land must be walkable";
}

TEST_F(NavigationSystemTest, IsAreaWalkableFalseWhenFootprintSpansWater) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys	 = world.registerSystem<NavigationSystem>();
	wireWaterPatch(world, sys, cw);
	settle(sys);

	const std::vector<glm::vec2> spanning{{28.0F, 38.0F}, {52.0F, 38.0F}, {52.0F, 42.0F}, {28.0F, 42.0F}};
	ASSERT_TRUE(meshClear(sys, spanning[0]));
	ASSERT_TRUE(meshClear(sys, spanning[1]));
	ASSERT_FALSE(meshClear(sys, {40.0F, 40.0F}));
	EXPECT_FALSE(sys.isAreaWalkable(spanning)) << "a footprint spanning water must be rejected";
}

TEST_F(NavigationSystemTest, IsAreaWalkableFalseWhenFootprintEnclosesWaterPond) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys	 = world.registerSystem<NavigationSystem>();
	wireWaterPatch(world, sys, cw);
	settle(sys);

	const std::vector<glm::vec2> enclosing{{28.0F, 28.0F}, {52.0F, 28.0F}, {52.0F, 52.0F}, {28.0F, 52.0F}};
	for (const auto& v : enclosing) {
		ASSERT_TRUE(meshClear(sys, v));
	}
	ASSERT_FALSE(meshClear(sys, {40.0F, 40.0F}));
	EXPECT_FALSE(sys.isAreaWalkable(enclosing)) << "a footprint enclosing a water pond must be rejected";
}

// Water is geography, not a clearable entity, so it blocks buildability too.
TEST_F(NavigationSystemTest, WaterBlocksBuildabilityUnlikeTrees) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys	 = world.registerSystem<NavigationSystem>();
	wireWaterPatch(world, sys, cw);
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
	wireWaterPatch(world, sys, cw);
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
	EXPECT_FALSE(meshClear(sys, {0.0F, 0.0F}));
}

// Dev-spawn refusal contract: every dev verb that places an entity guards on isValidPosition and,
// on false, creates NOTHING (DevCommandHandler::devSpawn's guard, exercised here).
TEST_F(NavigationSystemTest, DevSpawnGuardRefusesOffMeshCreatesOnValid) {
	ConstructionWorld cw;

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	auto guardedSpawn = [&](glm::vec2 at) -> bool {
		if (!meshClear(sys, at)) {
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
	meshAround(world, {40.0F, 40.0F}, 60.0F);
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
	wireArea(world, nav);
	nav.setPlacementData(&executor, &processed);
	nav.setConstructionWorld(&cw);
	settle(nav);

	AIDecisionSystem& ai = world.registerSystem<AIDecisionSystem>(assets::AssetRegistry::Get(), assets::RecipeRegistry::Get(), 1234u);
	ai.setNavigationSystem(&nav);
	ai.setColonyOrigin(glm::vec2{0.0F, 0.0F});
	ASSERT_FALSE(meshClear(nav, treeAt)) << "the tree is not clear ground";
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
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	const float kWallHalfThickness = 0.10F;
	const float kClearance		   = kWallHalfThickness + kAgentRadius;

	const glm::vec2				   nearWall{2.0F, 0.15F};
	const std::optional<glm::vec2> clearSnap = sys.nearestPathablePoint(nearWall, kAgentRadius);
	ASSERT_TRUE(clearSnap.has_value());
	EXPECT_TRUE(meshClear(sys, *clearSnap));
	EXPECT_GE(std::abs(clearSnap->y), kClearance) << "band-aware snap landed inside the wall collision band";
	EXPECT_LT(std::abs(nearWall.y), kClearance) << "test setup: the input point should start inside the band";
}

// A colonist off meshed ground is left exactly where it stands.
TEST_F(NavigationSystemTest, UnmeshedColonistNotRelocated) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World			  world;
	NavigationSystem& nav = world.registerSystem<NavigationSystem>();
	wireArea(world, nav);
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
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	settle(sys);

	const glm::vec2 origin{2.0F, 2.0F};
	ASSERT_TRUE(meshClear(sys, origin));

	const float					   minDist = 1.5F;
	const std::optional<glm::vec2> result  = sys.findValidPositionNear(origin, minDist);
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(meshClear(sys, *result));
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
	wireWaterPatch(world, sys, cw);
	settle(sys);

	const glm::vec2 origin{40.0F, 40.0F};
	ASSERT_TRUE(sys.inSimArea(origin));
	ASSERT_FALSE(meshClear(sys, origin));

	const float					   minDist = 1.0F;
	const std::optional<glm::vec2> result  = sys.findValidPositionNear(origin, minDist);
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(meshClear(sys, *result));
	EXPECT_GE(glm::distance(origin, *result), minDist - 0.01F);
}

// The push branch: a 2200 m ring from the pond clears all meshed ground (the farthest corner of
// the loaded chunks is 2115 m away), so the nearest shore is pushed out to minDist, lands off the
// mesh, and the shore point itself comes back instead.
TEST_F(NavigationSystemTest, FindValidPositionNearPushedFallbackStaysOnMesh) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys	 = world.registerSystem<NavigationSystem>();
	wireWaterPatch(world, sys, cw);
	settle(sys);

	const glm::vec2 origin{40.0F, 40.0F};
	const float		minDist = 2200.0F;
	ASSERT_FALSE(meshClear(sys, origin));
	const std::optional<glm::vec2> nearest = sys.findValidPositionNear(origin, 0.0F);
	ASSERT_TRUE(nearest.has_value());
	ASSERT_LT(glm::distance(origin, *nearest), minDist);
	ASSERT_FALSE(meshClear(sys, origin + (*nearest - origin) / glm::distance(origin, *nearest) * minDist)) << "the push must land off the mesh";

	const std::optional<glm::vec2> result = sys.findValidPositionNear(origin, minDist);
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(meshClear(sys, *result)) << "the pushed fallback must be re-validated on mesh";
}

TEST_F(NavigationSystemTest, FindValidPositionNearIsDeterministic) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
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
	wireArea(world, sys);
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
	wireArea(world, sys);
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
	addColonist(world, {6.0F, 6.0F}, "Anchor");
	sys.setConstructionWorld(&cw);
	wireArea(world, sys);
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
			sys->setChunkManager(landChunks());
			meshAround(world, {0.0F, 0.0F}, 60.0F);
			sys->setPlacementData(&executor, &processed);
			sys->setConstructionWorld(&cw);
			placeTrees(executor, processed, "Test_NavSnapTree", {kTree});
			settle(*sys);
			ASSERT_FALSE(meshClear(*sys, kTree));
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
	ASSERT_FALSE(meshClear(*sys, across)) << "the tree across the route is in";
	EXPECT_EQ(world.getComponent<Position>(colonist)->value, at);
	ASSERT_TRUE(path.valid);
	EXPECT_EQ(path.builtNavVersion, sys->generation()) << "not replanned";
	EXPECT_EQ(path.waypoints.front(), at) << "replanned from where it stands";
	for (std::size_t k = 0; k + 1 < path.waypoints.size(); ++k) {
		for (int i = 0; i <= 20; ++i) {
			const glm::vec2 p = path.waypoints[k] + (path.waypoints[k + 1] - path.waypoints[k]) * (static_cast<float>(i) / 20.0F);
			EXPECT_TRUE(meshClear(*sys, p)) << "the new route crosses the tree at (" << p.x << ", " << p.y << ")";
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
	EXPECT_TRUE(meshClear(*sys, now));
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
	EXPECT_TRUE(meshClear(*sys, now));
	EXPECT_LT(glm::distance(now, at), 1.5F) << "the nearest walkable point, not a far one";
}

// A colonist walking a move order has a tree land on it: its chunk's placement arrives as it walks,
// and its own disc brings the tree into the mesh. It is moved forward along its route, keeps the
// order, never steps back and gets there; collision leaves it alone until the mesh has the tree and
// the forward snap has moved it.
TEST_F(NavigationSystemTest, WalkerUnderLandingTreeKeepsItsTask) {
	{
		registerTree("Test_NavWalkerTree", 0.4F);
		PlacementExecutor					executor(AssetRegistry::Get());
		std::unordered_set<ChunkCoordinate> processed;
		ConstructionWorld					cw;
		World								world;
		NavigationSystem&					nav = world.registerSystem<NavigationSystem>();
		nav.setChunkManager(landChunks());
		nav.setPlacementData(&executor, &processed);
		nav.setConstructionWorld(&cw);
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
		settle(nav);
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
		step(world, nav);
		ASSERT_FALSE(nav.isValidPosition(position.value)) << "the walker stands in the tree, not pushed out of it";
		ASSERT_GE(position.value.x, lastX - 1e-4F) << "moved back as the tree landed";
		lastX = position.value.x;

		for (int i = 0; i < 100 && glm::distance(position.value, goal) > 0.3F; ++i) {
			world.update(kTick);
			expectMeshesValid(nav);
			ASSERT_GE(position.value.x, lastX - 1e-4F) << "tick " << i << ": moved back";
			ASSERT_EQ(task.targetPosition, goal) << "tick " << i << ": the move order was dropped";
			lastX = position.value.x;
		}
		EXPECT_FALSE(meshClear(nav, treeAt)) << "the walker's disc brought the tree in";
		EXPECT_LT(glm::distance(position.value, goal), 0.3F) << "never got there";
	}
}

// --- Nothing leaves ------------------------------------------------------------------

// There is no cap on the placed obstacles the mesh holds and no way to trim it: the constructor
// takes no budget and the budget constant is gone. A mover walking the length of a 2.4 km strip of
// forest leaves behind it every tree it ever brought in, and the count only grows, tick by tick.
TEST_F(NavigationSystemTest, NothingIsRemovedHoweverMuchIsMeshed) {
	static_assert(!std::is_constructible_v<NavigationSystem, std::size_t>, "the mesh takes no obstacle budget");
	static_assert(![]<typename T>() { return requires { T::kDetailRingBudget; }; }.template operator()<NavigationSystem>(),
				  "the mesh has no obstacle cap");
	registerTree("Test_NavStripTree", 0.3F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	std::vector<glm::vec2>				trees; // four by four in every 16 m cell of the row y in [0, 16)
	for (float x = -960.0F; x < 1440.0F; x += 4.0F) {
		for (float y = 2.0F; y < 16.0F; y += 4.0F) {
			trees.push_back({x + 2.0F, y});
		}
	}
	placeTrees(executor, processed, "Test_NavStripTree", trees);

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	const EntityID mover = addColonist(world, {-950.0F, 8.0F}, "Walker");
	settle(sys);

	const auto		  inMesh = [&](glm::vec2 t) { return sys.inSimArea(t) && !meshClear(sys, t); };
	std::vector<bool> was(trees.size());
	std::size_t		  count = 0;
	for (Position& position = *world.getComponent<Position>(mover); position.value.x < 1430.0F; position.value.x += 4.0F) {
		world.update(0.016F);
		std::size_t now = 0;
		for (std::size_t k = 0; k < trees.size(); ++k) {
			const bool in = inMesh(trees[k]);
			ASSERT_TRUE(in || !was[k]) << "the tree at x = " << trees[k].x << " left, mover at " << position.value.x;
			was[k] = in;
			now += in ? 1 : 0;
		}
		ASSERT_GE(now, count);
		count = now;
	}
	settle(sys);
	EXPECT_EQ(std::count(was.begin(), was.end(), true), static_cast<std::ptrdiff_t>(trees.size())) << "the whole strip is meshed";
	EXPECT_TRUE(inMesh(trees.front())) << "the far end the mover started at";
}

// Placed obstacles arrive ahead of a mover walking dense ground: at 2 m/s, every tick, every tree
// within 3 m of him is already in the mesh.
TEST_F(NavigationSystemTest, ObstaclesArriveAheadOfAWalkingMover) {
	registerTree("Test_NavAheadTree", 0.3F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	std::vector<glm::vec2>				trees; // every 3 m, a 60 m band along the walk, rows 1.5 m either side of it
	for (float x = 1.5F; x < 160.0F; x += 3.0F) {
		for (float y = 71.5F; y < 130.0F; y += 3.0F) {
			trees.push_back({x, y});
		}
	}
	placeTrees(executor, processed, "Test_NavAheadTree", trees);

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	const EntityID mover = addColonist(world, {10.0F, 100.0F}, "Walker");
	settle(sys);

	constexpr float kTick = 1.0F / 60.0F;
	Position&		position = *world.getComponent<Position>(mover);
	for (int tick = 0; position.value.x < 100.0F; ++tick) {
		position.value.x += 2.0F * kTick;
		world.update(kTick);
		for (const glm::vec2 t : trees) {
			if (glm::distance(t, position.value) < 3.0F) {
				ASSERT_FALSE(meshClear(sys, t)) << "tick " << tick << ": the tree at (" << t.x << ", " << t.y << ") isn't in yet";
			}
		}
	}
}

// Two movers whose discs overlap want each cell once: the obstacles of a cell both discs meet are
// added in one patch, never twice, and each chunk's ground comes in once.
TEST_F(NavigationSystemTest, OverlappingDiscsAddEachCellOnce) {
	registerTree("Test_NavOverlapTree", 0.3F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	std::vector<glm::vec2>				trees; // one in the middle of every 16 m cell round the origin
	for (float x = -88.0F; x < 120.0F; x += 16.0F) {
		for (float y = -88.0F; y < 120.0F; y += 16.0F) {
			trees.push_back({x, y});
		}
	}
	placeTrees(executor, processed, "Test_NavOverlapTree", trees);

	const glm::vec2 a{8.0F, 8.0F};
	const glm::vec2 b{30.0F, 12.0F};
	const float		r = kDefaultSightRadius + NavigationSystem::kSightBufferMeters;
	const auto		meets = [r](glm::vec2 centre, glm::vec2 tree) {
		 const glm::vec2 lo{std::floor(tree.x / 16.0F) * 16.0F, std::floor(tree.y / 16.0F) * 16.0F};
		 const glm::vec2 nearest = glm::clamp(centre, lo, lo + glm::vec2{16.0F});
		 return glm::distance(centre, nearest) <= r;
	};
	const auto wantedCells = static_cast<std::uint64_t>(
		std::count_if(trees.begin(), trees.end(), [&](glm::vec2 t) { return meets(a, t) || meets(b, t); }));
	const auto eitherAlone = static_cast<std::uint64_t>(std::count_if(trees.begin(), trees.end(), [&](glm::vec2 t) { return meets(a, t); })) +
							 static_cast<std::uint64_t>(std::count_if(trees.begin(), trees.end(), [&](glm::vec2 t) { return meets(b, t); }));
	ASSERT_LT(wantedCells, eitherAlone) << "the discs overlap";

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	addColonist(world, a, "A");
	addColonist(world, b, "B");
	settle(sys);
	const NavigationSystem::NavQueryStats& stats = sys.navQueryStats();
	EXPECT_EQ(stats.structuralPatches, 4u) << "the four chunks round the origin, once each";
	EXPECT_EQ(stats.navPatches - stats.structuralPatches, wantedCells) << "one patch per wanted cell";
	for (const glm::vec2 t : trees) {
		EXPECT_EQ(!meshClear(sys, t), meets(a, t) || meets(b, t)) << "the tree at (" << t.x << ", " << t.y << ")";
	}
}

// --- Ground edge cases ---------------------------------------------------------------

// A meshed chunk whose terrain is rebuilt has its water taken out and put back in one tick, leaving
// the same mesh: the pond still blocks, the land around it is walkable, no triangle is gained or lost.
TEST_F(NavigationSystemTest, TerrainChangeOnMeshedChunkRederivesItsWater) {
	const std::unique_ptr<world::ChunkManager> chunks = readyChunks(waterPatchSampler(), {256.0F, 256.0F}, 0);
	ConstructionWorld						   cw;
	World									   world;
	NavigationSystem&						   sys = world.registerSystem<NavigationSystem>();
	addColonist(world, {20.0F, 20.0F}, "Anchor");
	sys.setChunkManager(chunks.get());
	sys.setConstructionWorld(&cw);
	settle(sys);
	const glm::vec2 pond{40.0F, 40.0F};
	const glm::vec2 land{20.0F, 20.0F};
	ASSERT_FALSE(meshClear(sys, pond));
	ASSERT_TRUE(meshClear(sys, land));
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
	EXPECT_FALSE(meshClear(sys, pond)) << "the pond still blocks";
	EXPECT_TRUE(meshClear(sys, land));
	EXPECT_EQ(sys.navMesh().liveTriangleCount(), navTriangles);
	EXPECT_EQ(sys.structuralMesh().liveTriangleCount(), structuralTriangles);
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
	meshAround(world, {40.0F, 40.0F}, 60.0F);
	settle(sys);
	ASSERT_FALSE(meshClear(sys, {40.0F, 40.0F})) << "the pond";

	const glm::vec2 at{30.8F, 40.0F};
	const EntityID	colonist = routedColonist(world, at, {{26.0F, 40.0F}, {48.0F, 40.0F}, {54.0F, 40.0F}});
	world.getComponent<NavPath>(colonist)->builtNavVersion = sys.generation();
	step(world, sys);
	EXPECT_EQ(world.getComponent<Position>(colonist)->value, at);
	EXPECT_NE(world.getComponent<NavPath>(colonist)->builtNavVersion, sys.generation()) << "the route isn't stale";
}

// Felled flora in several cells comes out one patch per cell, so each patch's width refresh covers
// its own cell and not the box around all of them.
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
	wireArea(world, sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(std::none_of(trees.begin(), trees.end(), [&](glm::vec2 t) { return meshClear(sys, t); }));

	for (const glm::vec2 t : trees) {
		ASSERT_TRUE(executor.removeEntity({0, 0}, t, "Test_NavCellTree"));
	}
	sys.resetNavQueryStats();
	step(world, sys);
	for (int i = 0; i < 4 && sys.isBuilding(); ++i) { // kDetailCellsPerTick cells a tick
		step(world, sys);
	}
	ASSERT_FALSE(sys.isBuilding());
	EXPECT_EQ(sys.navQueryStats().navPatches, 3u);
	EXPECT_EQ(sys.navQueryStats().structuralPatches, 0u) << "flora is in the nav mesh only";
	EXPECT_TRUE(std::all_of(trees.begin(), trees.end(), [&](glm::vec2 t) { return meshClear(sys, t); })) << "all removed";
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
	wireArea(world, sys);
	sys.setConstructionWorld(&first);
	settle(sys);
	const auto straight = [&](float x) {
		const auto path = sys.requestPath({x - 5.0F, 10.0F}, {x + 5.0F, 10.0F}, kAgentRadius).route;
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
	// The world a walking colonist needs: nav over the shared land chunks, meshed round `meshAt` (and
	// round every walker), the real AI on move orders, movement, physics and flora collision.
	struct WalkRig {
		PlacementExecutor					executor{AssetRegistry::Get()};
		std::unordered_set<ChunkCoordinate> processed;
		ConstructionWorld					cw;
		World								world;
		NavigationSystem&					nav = world.registerSystem<NavigationSystem>();
		AIDecisionSystem*					ai	= nullptr;

		explicit WalkRig(glm::vec2 meshAt, world::ChunkManager* chunks = landChunks()) {
			nav.setChunkManager(chunks);
			nav.setPlacementData(&executor, &processed);
			nav.setConstructionWorld(&cw);
			meshAround(world, meshAt, 30.0F);
		}

		// Registers the AI and the movers once the trees placed so far are in.
		void start() {
			settle(nav);
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
	meshAround(world, {30.0F, 30.0F}, 60.0F);
	settle(sys);

	const auto round =sys.requestPath({17.0F, 5.0F}, {23.0F, 5.0F}, kAgentRadius).route;
	ASSERT_TRUE(round.has_value());
	EXPECT_FALSE(sys.routeCrossesPricedFaces(routeOf(*round))) << "round the short hedge";
	EXPECT_GT(lengthOf(*round), 7.0F);
	EXPECT_LT(lengthOf(*round), 20.0F);

	const auto through = sys.requestPath({37.0F, 30.0F}, {43.0F, 30.0F}, kAgentRadius).route;
	ASSERT_TRUE(through.has_value());
	EXPECT_TRUE(sys.routeCrossesPricedFaces(routeOf(*through))) << "through the long hedge";
	EXPECT_LT(lengthOf(*through), 7.0F);
}

// The reach pre-check and the path agree on a goal sealed in by trees, wherever the camera is: both
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
	wireArea(world, sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_FALSE(meshClear(sys, treeRing(goal, 3.0F).front())) << "the ring is in";
	EXPECT_TRUE(sys.reaches(sys.reachFrom({10.0F, 20.0F}, kAgentRadius), goal));
	EXPECT_TRUE(sys.requestPath({10.0F, 20.0F}, goal, kAgentRadius).route.has_value());
}

// --- Staleness: flora marks only the routes it lands on -------------------------------------

namespace {
	// A mover that asks for no flora of its own, on a straight route stamped current.
	EntityID routedMover(World& world, const NavigationSystem& nav, glm::vec2 from, glm::vec2 to) {
		const EntityID e = world.createEntity();
		world.addComponent<Position>(e, Position{from});
		world.addComponent<AgentRadius>(e, AgentRadius{});
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
	wireArea(world, sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	const EntityID missed = routedMover(world, sys, {1.0F, 20.0F}, {31.0F, 20.0F});
	const EntityID hit	  = routedMover(world, sys, {1.0F, 40.0F}, {31.0F, 40.0F});
	const std::uint64_t generation = sys.generation();

	placeTrees(executor, processed, "Test_NavMarkTree", {{10.0F, 26.0F}, {10.0F, 40.0F}});
	settle(sys);
	step(world, sys); // the update that reads what landed
	ASSERT_FALSE(meshClear(sys, {10.0F, 26.0F})) << "the trees are in";
	EXPECT_EQ(sys.generation(), generation);
	EXPECT_FALSE(stale(world, sys, missed)) << "a tree 6 m off the route replanned it";
	EXPECT_TRUE(stale(world, sys, hit)) << "a tree across the route didn't";
}

// A mover walks along a stand of trees that streams into the mesh as it goes: the mover whose
// route crosses the stand is marked stale, the one whose route is far from any tree never is, and
// the generation never moves.
TEST_F(NavigationSystemTest, StreamingFloraMarksOnlyRoutesItTouches) {
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
	const EntityID walker = addColonist(world, {60.0F, 8.0F}, "Walker");
	settle(sys);
	ASSERT_TRUE(meshClear(sys, stand[5])) << "the stand isn't in yet";
	const EntityID crossing = routedMover(world, sys, {199.0F, -50.0F}, {199.0F, 30.0F});
	const EntityID clear	= routedMover(world, sys, {150.0F, 200.0F}, {270.0F, 200.0F});
	const std::uint64_t generation = sys.generation();

	bool clearMarked = false;
	for (Position& position = *world.getComponent<Position>(walker); position.value.x <= 300.0F; position.value.x += 2.0F) {
		step(world, sys);
		clearMarked = clearMarked || stale(world, sys, clear);
	}
	ASSERT_FALSE(meshClear(sys, {199.0F, 1.0F})) << "the stand streamed in";
	EXPECT_EQ(sys.generation(), generation);
	EXPECT_TRUE(stale(world, sys, crossing));
	EXPECT_FALSE(clearMarked);
}

// A move order clicked on a tree snaps its goal to clear ground beside the trunk: the walker goes
// round on a route that crosses no tree, stops beside it and stays there.
TEST_F(NavigationSystemTest, MoveOrderOntoATreeEndsBesideIt) {
	registerTree("Test_NavOrderTree", 0.4F);
	const glm::vec2 treeAt{10.0F, 5.0F};
	WalkRig			rig(treeAt);
	placeTrees(rig.executor, rig.processed, "Test_NavOrderTree", {treeAt});
	rig.start();
	const EntityID	colonist = rig.walker({2.0F, 5.0F}, treeAt);
	const glm::vec2 goal	 = rig.world.getComponent<Task>(colonist)->targetPosition;
	EXPECT_TRUE(rig.nav.isValidPosition(goal)) << "the goal is clear ground";
	EXPECT_LT(glm::distance(goal, treeAt), 1.0F) << "beside the trunk";
	EXPECT_FALSE(rig.pricedRoute(colonist)) << "no tree to cross";

	const Position&		   position = *rig.world.getComponent<Position>(colonist);
	std::vector<glm::vec2> settled;
	for (int i = 0; i < 120; ++i) {
		rig.world.update(0.1F);
		if (i >= 100) {
			settled.push_back(position.value);
		}
	}
	for (const glm::vec2 p : settled) {
		EXPECT_EQ(p, settled.front()) << "still moving";
	}
	EXPECT_LT(glm::distance(position.value, goal), 0.1F) << "stopped on its goal";
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
	meshAround(world, {8.0F, 8.0F}, 30.0F);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	const auto in = [&](glm::vec2 t) { return sys.inSimArea(t) && !meshClear(sys, t); };
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
	wireArea(world, sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	const EntityID mover = routedMover(world, sys, {15.65F, 0.5F}, {15.65F, 15.0F}); // cell x in [0, 16)

	const glm::vec2 edgeTree{16.05F, 8.0F}; // cell x in [16, 32), reaching back to 15.55
	placeTrees(executor, processed, "Test_NavEdgeTree", {edgeTree});
	step(world, sys);
	step(world, sys);
	ASSERT_FALSE(meshClear(sys, edgeTree)) << "the tree is in";
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
	EXPECT_TRUE(meshClear(*sys, now));
	EXPECT_LT(now.x, 10.9F - 0.1F - kAgentRadius) << "on its own side and out of the wall's band";
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

// Knowing that nearer water and a farther source on meshed ground, both in its reach square, the
// colonist takes the farther.
TEST_F(NavigationSystemTest, FartherReachableWaterBeatsNearerWaterInGroundThatIsntComing) {
	WalkRig rig({1500.0F, 0.0F});
	rig.start();
	const glm::vec2 at{1530.0F, 10.0F};
	const glm::vec2 gone{1560.0F, 10.0F};	// chunk (3, 0), beyond the loaded radius, 30 m off
	const glm::vec2 meshed{1450.0F, 10.0F}; // chunk (2, 0), 80 m off
	ASSERT_EQ(landChunks()->getChunk(world::worldToChunk({gone.x, gone.y})), nullptr);
	ASSERT_TRUE(rig.nav.inSimArea(meshed));

	const EntityID colonist = needyColonist(rig, at, NeedType::Thirst, 10.0F);
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

// --- What a structural change re-requests -----------------------------------------------------

// A wall built in an anchored chunk, far from a mover's route, moves generation() (so a goal waiting
// for its ground is asked again) but restamps the route, so nobody replans; a wall across the route
// makes that one stale. Chunks loading where no mover is (as the camera loads them) move neither.
TEST_F(NavigationSystemTest, StructuralChangesReRequestOnlyTheRoutesTheyCross) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setConstructionWorld(&cw);
	meshAround(world, {0.0F, 0.0F}, 30.0F);
	settle(sys);
	const EntityID		walker	   = routedMover(world, sys, {0.0F, 0.0F}, {60.0F, 0.0F});
	const std::uint64_t generation = sys.generation();

	const std::unique_ptr<world::ChunkManager> wider = readyChunks(landSampler(), {0.0F, 0.0F}, 3);
	sys.setChunkManager(wider.get());
	settle(sys);
	ASSERT_FALSE(sys.inSimArea({1700.0F, 10.0F})) << "a chunk no mover wants is meshed";
	EXPECT_EQ(sys.generation(), generation) << "chunks loading moved the generation";
	EXPECT_FALSE(stale(world, sys, walker));

	buildWall(cw, {20000, 40000}, {40000, 40000}); // 40 m off the route, in the walker's chunk
	step(world, sys);
	settle(sys);
	EXPECT_NE(sys.generation(), generation) << "a waiting goal here would be asked again";
	EXPECT_FALSE(stale(world, sys, walker)) << "a wall off the route replanned it";

	buildWall(cw, {30000, -5000}, {30000, 5000}); // across it
	step(world, sys);
	settle(sys);
	EXPECT_TRUE(stale(world, sys, walker)) << "a wall across the route didn't";
}

// --- Anchored ground stays -----------------------------------------------------------------------

// A player sends a colonist 700 m east over ground a lookout has meshed and since left, then the
// camera pans 3 km away and wanders over 20 chunks there. The colonist anchors the ground within his
// own range as he walks, so the chunk manager keeps it loaded and the nav mesh keeps it meshed, and
// he arrives.
TEST_F(NavigationSystemTest, LongMoveOrderKeepsItsGroundWhileTheCameraWanders) {
	auto chunks = std::make_unique<world::ChunkManager>(landSampler());
	chunks->setLoadRadius(1);
	chunks->setUnloadRadius(1);
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& nav = world.registerSystem<NavigationSystem>();
	nav.setChunkManager(chunks.get());
	nav.setConstructionWorld(&cw);
	std::vector<world::ChunkAnchor> anchors;
	glm::vec2						camera{768.0F, 256.0F}; // chunk (1, 0): chunks 0 to 2 load
	const auto						  stream = [&]() {
		 anchors.clear();
		 nav.appendAnchors(anchors);
		 chunks->update({camera.x, camera.y}, anchors);
		 chunks->finishPendingGeneration();
	};
	const glm::vec2 from{400.0F, 256.0F};
	const glm::vec2 to{1100.0F, 256.0F};
	const EntityID	colonist = spawnRecoverableColonist(world, from);
	world.addComponent<Colonist>(colonist, Colonist{"Walker"});
	world.addComponent<AgentRadius>(colonist, AgentRadius{kAgentRadius, 1.0F});
	const EntityID lookout = meshAround(world, {768.0F, 256.0F}, 400.0F);
	stream();
	settle(nav);
	world.destroyEntity(lookout);
	auto& ai = world.registerSystem<AIDecisionSystem>(AssetRegistry::Get(), assets::RecipeRegistry::Get(), 1234u);
	ai.setNavigationSystem(&nav);
	world.registerSystem<MovementSystem>();
	world.registerSystem<PhysicsSystem>();
	NeedsComponent& needs = *world.getComponent<NeedsComponent>(colonist);
	for (const NeedType need : {NeedType::Hunger, NeedType::Thirst, NeedType::Energy, NeedType::Bladder, NeedType::Digestion}) {
		needs.get(need).value = 100.0F;
	}
	ai.enterControl(colonist);
	ASSERT_TRUE(ai.issuePlayerMoveOrder(colonist, to).has_value());
	world.update(0.05F);
	ASSERT_TRUE(world.getComponent<NavPath>(colonist)->valid) << "no route east";

	std::unordered_set<std::int32_t> visited;
	Position&						 position = *world.getComponent<Position>(colonist);
	for (int tick = 0; tick < 12000 && glm::distance(position.value, to) > 0.3F; ++tick) {
		const int i = 6 + (tick / 100) % 20; // chunks 6 to 25 of row 0, 3 to 13 km east
		camera		= {256.0F + 512.0F * static_cast<float>(i), 256.0F};
		visited.insert(i);
		stream();
		world.update(0.05F);
		ASSERT_TRUE(nav.inSimArea({position.value.x + 20.0F, 256.0F}) || glm::distance(position.value, to) < 20.0F)
			<< "the ground ahead was dropped at tick " << tick;
	}
	EXPECT_EQ(visited.size(), 20u);
	EXPECT_LT(glm::distance(position.value, to), 0.3F) << "never arrived";
}

// --- What the camera can't move ---------------------------------------------------------------

// A failed route recorded at a spot 172 m from chunk (1, 0), the colonist since walked 120 m west
// (still near the spot): the chunk is in the band the anchor's stamp covers (256 m round the
// spot) but neither loaded nor kept by the colonist's own anchor (292 m off). The camera loads the
// chunk, which meshes nothing, and then wanders 20 chunks east. Neither moves the anchor's stamp or
// generation(); a wall built in the chunk while it is loaded does move the stamp.
TEST_F(NavigationSystemTest, CameraComingAndGoingLeavesTheVerdictStamp) {
	auto chunks = std::make_unique<world::ChunkManager>(landSampler());
	chunks->setLoadRadius(0);
	chunks->setUnloadRadius(0);
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	sys.setConstructionWorld(&cw);
	const glm::vec2 at{340.0F, 256.0F};
	const glm::vec2 colonistAt{220.0F, 256.0F};
	addColonist(world, colonistAt, "Keeper");
	std::vector<world::ChunkAnchor> anchors;
	const auto						  stand = [&](int i) {
		 anchors.clear();
		 sys.appendAnchors(anchors);
		 chunks->update({256.0F + 512.0F * static_cast<float>(i), 256.0F}, anchors);
		 chunks->finishPendingGeneration();
		 step(world, sys);
		 settle(sys);
	};
	stand(0);
	const std::uint64_t verdict	   = sys.verdictStamp(at);
	const std::uint64_t generation = sys.generation();

	stand(1);
	ASSERT_NE(chunks->getChunk({1, 0}), nullptr) << "the camera loaded chunk (1, 0)";
	ASSERT_FALSE(sys.inSimArea({768.0F, 256.0F})) << "and meshed it";
	EXPECT_EQ(sys.verdictStamp(at), verdict);
	EXPECT_EQ(sys.generation(), generation);
	for (int i = 2; i < 22; ++i) {
		stand(i);
	}
	ASSERT_TRUE(sys.inSimArea(colonistAt)) << "the colonist's own ground stays";
	EXPECT_EQ(sys.verdictStamp(at), verdict) << "the camera moved the stamp";
	EXPECT_EQ(sys.generation(), generation);

	stand(1);
	buildWall(cw, {700000, 200000}, {704000, 200000});
	stand(1);
	EXPECT_NE(sys.verdictStamp(at), verdict) << "a wall in the band";
}

// --- A search that gives up -------------------------------------------------------------------

// On camera a thick stand is in the nav mesh. With the expansion cap shrunk so the search gives up,
// the route is the structural walk through the stand at its price, and the colonist walks it and
// arrives.
TEST_F(NavigationSystemTest, GivenUpSearchWalksTheStructuralRouteThroughTheForest) {
	registerTree("Test_NavCapTree", 0.3F);
	WalkRig				   rig({20.0F, 20.0F});
	std::vector<glm::vec2> stand;
	for (float x = 10.0F; x < 30.0F; x += 1.5F) {
		for (float y = 2.0F; y < 38.0F; y += 1.5F) {
			stand.emplace_back(x, y);
		}
	}
	placeTrees(rig.executor, rig.processed, "Test_NavCapTree", stand);
	rig.start();
	ASSERT_FALSE(meshClear(rig.nav, stand[40])) << "the stand is in the mesh";
	rig.nav.setNavExpansionCap(1, 0);
	const glm::vec2 goal{38.0F, 20.5F};
	const EntityID	walker = rig.walker({2.0F, 20.5F}, goal);
	ASSERT_TRUE(rig.world.getComponent<NavPath>(walker)->valid) << "the gave-up search left no route";
	EXPECT_TRUE(rig.pricedRoute(walker)) << "the structural route is marked as crossing flora";
	const glm::vec2& at = rig.world.getComponent<Position>(walker)->value;
	for (int i = 0; i < 2000 && glm::distance(at, goal) > 0.3F; ++i) {
		rig.world.update(0.016F);
	}
	EXPECT_LT(glm::distance(at, goal), 0.3F) << "never arrived";

	const EntityID clearOfTrees = rig.walker({2.0F, 45.0F}, {38.0F, 45.0F});
	ASSERT_TRUE(rig.world.getComponent<NavPath>(clearOfTrees)->valid);
	EXPECT_FALSE(rig.pricedRoute(clearOfTrees)) << "a fallback route clear of the stand is marked as crossing flora";
}

// A far goal's route is the structural walk too, and is treated as the fallback is: with the reach
// budgets shrunk so the goal past the stand is far, the route goes through the stand, is marked as
// crossing flora (so collision lets the colonist through and the forward snap leaves it alone), and
// he arrives. One clear of the stand isn't marked.
TEST_F(NavigationSystemTest, FarRouteWalksTheStructuralRouteThroughTheForest) {
	registerTree("Test_NavFarTree", 0.3F);
	WalkRig				   rig({20.0F, 20.0F});
	std::vector<glm::vec2> stand;
	for (float x = 10.0F; x < 30.0F; x += 1.5F) {
		for (float y = 2.0F; y < 38.0F; y += 1.5F) {
			stand.emplace_back(x, y);
		}
	}
	placeTrees(rig.executor, rig.processed, "Test_NavFarTree", stand);
	rig.start();
	ASSERT_FALSE(meshClear(rig.nav, stand[40])) << "the stand is in the mesh";
	rig.nav.setReachBudgets(1, 1, NavigationSystem::kFarRouteExpansions);
	rig.nav.resetNavQueryStats();
	const glm::vec2 goal{70.0F, 20.5F}; // in another triangle of the structural mesh, so the walk searches
	const EntityID	walker = rig.walker({2.0F, 20.5F}, goal);
	ASSERT_GT(rig.nav.navQueryStats().farWalks, 0U) << "the goal wasn't far";
	ASSERT_TRUE(rig.world.getComponent<NavPath>(walker)->valid) << "the far route search left no route";
	EXPECT_TRUE(rig.pricedRoute(walker)) << "the far route isn't marked as crossing flora";
	const glm::vec2& at = rig.world.getComponent<Position>(walker)->value;
	for (int i = 0; i < 4000 && glm::distance(at, goal) > 0.3F; ++i) {
		rig.world.update(0.016F);
	}
	EXPECT_LT(glm::distance(at, goal), 0.3F) << "never arrived";

	const EntityID clearOfTrees = rig.walker({2.0F, 45.0F}, {70.0F, 45.0F});
	ASSERT_TRUE(rig.world.getComponent<NavPath>(clearOfTrees)->valid);
	EXPECT_FALSE(rig.pricedRoute(clearOfTrees)) << "a far route clear of the stand is marked as crossing flora";
}

// --- A replan keeps its first confinement ------------------------------------------------------

// --- Stale stays stale ---------------------------------------------------------------------------

// A route a tree landed across is stale; an unrelated wall job, which restamps the routes it doesn't
// cross, leaves it stale.
TEST_F(NavigationSystemTest, StaleRouteStaysStaleThroughAnUnrelatedWallJob) {
	registerTree("Test_NavStaleTree", 0.3F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	placeTrees(executor, processed, "Test_NavStaleTree", {{-20.0F, 40.0F}});

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	const EntityID hit	  = routedMover(world, sys, {1.0F, 40.0F}, {31.0F, 40.0F});
	const EntityID missed = routedMover(world, sys, {1.0F, 10.0F}, {31.0F, 10.0F});
	placeTrees(executor, processed, "Test_NavStaleTree", {{10.0F, 40.0F}});
	settle(sys);
	step(world, sys); // the update that reads what landed
	ASSERT_TRUE(stale(world, sys, hit));
	ASSERT_FALSE(stale(world, sys, missed));

	const std::uint64_t before = sys.generation();
	buildWall(cw, {-40000, -40000}, {-36000, -40000}); // far from both routes
	step(world, sys);
	settle(sys);
	ASSERT_NE(sys.generation(), before) << "the wall job is anchored";
	EXPECT_TRUE(stale(world, sys, hit)) << "the restamp read the stale mark as current";
	EXPECT_FALSE(stale(world, sys, missed));
}

// The clear-ground test and the mesh read one outline (nav::placedObstacleOutline): a collider set off its
// entity's centre and mirrored by a negative scale lands on the same side for both.
TEST_F(NavigationSystemTest, ClearGroundAndTheMeshAgreeOnAMirroredCollider) {
	AssetDefinition tree;
	tree.defName					 = "Test_NavMirroredTree";
	tree.setCollision(0, {.type = CollisionShapeType::Rect, .offsetMeters = {0.8F, 0.0F}, .halfExtentsMeters = {0.3F, 0.3F}});
	AssetRegistry::Get().registerTestDefinition(tree);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	assets::AsyncChunkPlacementResult	placed;
	placed.coord = world::worldToChunk({20.0F, 20.0F});
	PlacedEntity mirrored;
	mirrored.defName  = "Test_NavMirroredTree";
	mirrored.position = {20.0F, 20.0F};
	mirrored.scale	  = -1.0F;
	placed.spatialIndex.insert(mirrored);
	executor.storeChunkResult(std::move(placed));
	processed.insert(world::worldToChunk({20.0F, 20.0F}));

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	for (const glm::vec2 p : {glm::vec2{19.2F, 20.0F}, glm::vec2{20.8F, 20.0F}}) {
		EXPECT_EQ(sys.isValidPosition(p), meshClear(sys, p)) << "at (" << p.x << ", " << p.y << ")";
	}
	EXPECT_FALSE(meshClear(sys, {19.2F, 20.0F})) << "the mirrored collider is in the mesh";
}

// A sapling 0.04 m across, 0.2 m off a fallback route's line, is within the walker's radius of it:
// the route is marked as crossing flora however narrow the trunk.
TEST_F(NavigationSystemTest, FallbackRoutePastANarrowTrunkIsMarked) {
	registerTree("Test_NavSapling", 0.02F);
	WalkRig rig({20.0F, 20.0F});
	placeTrees(rig.executor, rig.processed, "Test_NavSapling", {{20.03F, 20.2F}});
	rig.start();
	ASSERT_FALSE(meshClear(rig.nav, {20.03F, 20.2F})) << "the sapling is in the mesh";
	rig.nav.setNavExpansionCap(1, 0);
	const EntityID walker = rig.walker({2.0F, 20.0F}, {38.0F, 20.0F});
	ASSERT_TRUE(rig.world.getComponent<NavPath>(walker)->valid);
	EXPECT_TRUE(rig.pricedRoute(walker)) << "the sapling was missed";
}

// The shipped near cap: the floor for a walk the goal-side search found at once, the floor plus 16 a
// step for a longer one, and the ceiling past that, however long the walk's search ran.
TEST_F(NavigationSystemTest, NearSearchCapAtTheDefaultBudgets) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	EXPECT_EQ(sys.navExpansionCap(0), NavigationSystem::kNavExpansionFloor);
	EXPECT_EQ(sys.navExpansionCap(100), NavigationSystem::kNavExpansionFloor + 100 * NavigationSystem::kNavExpansionsPerStructural);
	EXPECT_EQ(sys.navExpansionCap(NavigationSystem::kReachExpansionCap), NavigationSystem::kNavExpansionCeiling);
	EXPECT_EQ(sys.navExpansionCap(NavigationSystem::kFarRouteExpansions), NavigationSystem::kNavExpansionCeiling);
}

// At the shipped budgets: a goal in the middle of a 40 m thicket too tight for the disc, on camera.
// The structural walk is the straight line, found at once; the nav search runs into the thicket's
// priced ground, stops at its cap, and the route is the structural walk.
TEST_F(NavigationSystemTest, NearSearchFallsBackAtTheDefaultCap) {
	registerTree("Test_NavThicketTree", 0.3F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	std::vector<glm::vec2>				thicket;
	for (float x = 0.5F; x < 40.0F; x += 0.75F) {
		for (float y = 0.5F; y < 40.0F; y += 0.75F) {
			thicket.emplace_back(x, y);
		}
	}
	placeTrees(executor, processed, "Test_NavThicketTree", thicket);
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	meshAround(world, {20.0F, 20.0F}, 30.0F);
	settle(sys);
	ASSERT_FALSE(meshClear(sys, thicket[thicket.size() / 2])) << "the thicket isn't in the mesh";

	const glm::vec2 start{-10.0F, 20.2F};
	const glm::vec2 goal{20.2F, 20.2F};
	sys.resetNavQueryStats();
	const NavigationSystem::PathAnswer path = sys.requestPath(start, goal, kAgentRadius);
	ASSERT_TRUE(path.route.has_value()) << "a capped nav search left no route";
	EXPECT_FALSE(path.gaveUp);
	const NavigationSystem::NavQueryStats& stats = sys.navQueryStats();
	EXPECT_EQ(stats.navSearches, 1U);
	EXPECT_EQ(stats.navSearchesCapped, 1U) << "the nav search found its way within the cap: grow the thicket";
	EXPECT_GE(stats.navSearchExpansions, NavigationSystem::kNavExpansionFloor);
	EXPECT_LE(stats.navSearchExpansions, NavigationSystem::kNavExpansionCeiling + 1);
	EXPECT_EQ(path.route->size(), 2U) << "the route isn't the structural walk";
	EXPECT_LT(glm::distance(path.route->back(), goal), 0.01F);
}

// A far route's walk is the structural search's, which detail never changes: as the walker's own disc
// brings a stand across it into the mesh, the route is re-priced where it stands, never marked
// stale, so no request searches it again.
TEST_F(NavigationSystemTest, DetailLandingOnAFarRouteRepricesItWithoutASearch) {
	registerTree("Test_NavFarPanTree", 0.3F);
	WalkRig				   rig({200.0F, -30.0F}); // meshes chunk (0, 0)'s ground, not the stand

	std::vector<glm::vec2> stand;
	for (float x = 10.0F; x < 30.0F; x += 1.5F) {
		for (float y = 2.0F; y < 38.0F; y += 1.5F) {
			stand.emplace_back(x, y);
		}
	}
	placeTrees(rig.executor, rig.processed, "Test_NavFarPanTree", stand);
	rig.start();
	const glm::vec2 onRoute{19.0F, 20.0F};
	ASSERT_TRUE(meshClear(rig.nav, onRoute)) << "the stand is in before the walker is";
	rig.nav.setReachBudgets(1, 1, NavigationSystem::kFarRouteExpansions);
	rig.nav.resetNavQueryStats();
	const EntityID walker = rig.walker({2.0F, 20.5F}, {70.0F, 20.5F});
	const NavPath& path	  = *rig.world.getComponent<NavPath>(walker);
	ASSERT_TRUE(path.valid);
	ASSERT_EQ(rig.nav.navQueryStats().totalQueries, 1U);
	EXPECT_FALSE(rig.pricedRoute(walker));
	const std::vector<glm::vec2> route = path.waypoints;

	for (int i = 0; i < 60; ++i) {
		rig.world.update(0.016F);
	}
	ASSERT_FALSE(meshClear(rig.nav, onRoute)) << "the stand never streamed in";
	EXPECT_EQ(rig.nav.navQueryStats().totalQueries, 1U) << "detail landing on a far route searched it again";
	EXPECT_EQ(path.waypoints, route);
	EXPECT_EQ(path.builtNavVersion, rig.nav.generation()) << "the far route was marked stale";
	EXPECT_TRUE(rig.pricedRoute(walker)) << "the route through the stand isn't priced";
}

// --- Ground a mover has held stays ---------------------------------------------------------------

namespace {
	// A colonist (a mover, so an anchor) walking chunk to chunk along row 0 while the camera loads
	// only the chunk it stands over; the anchors go to the chunk manager each step as GameScene does.
	struct MoverWalk {
		ChunkWalk		  walk;
		ConstructionWorld cw;
		World			  world;
		NavigationSystem& sys	   = world.registerSystem<NavigationSystem>();
		EntityID		  mover	   = kInvalidEntity;
		glm::vec2		  cameraAt = ChunkWalk::centre(-40);

		MoverWalk() {
			sys.setChunkManager(walk.chunks.get());
			sys.setConstructionWorld(&cw);
			mover = addColonist(world, ChunkWalk::centre(-30), "Walker");
		}

		void step() {
			std::vector<world::ChunkAnchor> anchors;
			sys.appendAnchors(anchors);
			walk.chunks->update({cameraAt.x, cameraAt.y}, anchors);
			walk.chunks->finishPendingGeneration();
			settle(sys);
		}

		void moverTo(int i) {
			world.getComponent<Position>(mover)->value = ChunkWalk::centre(i);
			step();
		}

		void cameraTo(int i) {
			cameraAt = ChunkWalk::centre(i);
			step();
		}
	};
} // namespace

// A mover walks through 20 chunks and on: each keeps its ground after it unloads.
TEST_F(NavigationSystemTest, GroundAMoverHeldIsNeverDropped) {
	MoverWalk walk;
	for (int i = 0; i < 20; ++i) {
		walk.moverTo(i);
	}
	walk.moverTo(40);
	for (int i = 0; i < 20; ++i) {
		EXPECT_EQ(walk.walk.chunks->getChunk({i, 0}), nullptr) << "chunk " << i << " is still loaded";
		EXPECT_TRUE(walk.sys.inSimArea(ChunkWalk::centre(i))) << "chunk " << i << " lost its ground";
	}
}

// The camera loading chunk 5 meshes nothing; a mover walking through it does, and the chunk keeps
// that ground however far the camera wanders after, while the chunks it wanders over get none.
TEST_F(NavigationSystemTest, OnlyAMoverMeshesAChunkTheCameraLoads) {
	MoverWalk walk;
	walk.cameraTo(5);
	ASSERT_NE(walk.walk.chunks->getChunk({5, 0}), nullptr);
	EXPECT_FALSE(walk.sys.inSimArea(ChunkWalk::centre(5))) << "the camera meshed its chunk";
	walk.moverTo(5);
	ASSERT_TRUE(walk.sys.inSimArea(ChunkWalk::centre(5)));
	walk.moverTo(-30);
	for (int i = 6; i <= 26; ++i) {
		walk.cameraTo(i);
	}
	EXPECT_TRUE(walk.sys.inSimArea(ChunkWalk::centre(5))) << "a chunk a mover held was dropped";
	for (int i = 7; i <= 26; ++i) {
		EXPECT_FALSE(walk.sys.inSimArea(ChunkWalk::centre(i))) << "the camera meshed chunk " << i;
	}
}

// --- Anchors are movers, each with its own range -------------------------------------------------

// A mover is Position + AgentRadius. Its range is its sight distance plus the buffer: 30 m default
// sight without Memory, its own with. A Position + Task without AgentRadius is no mover.
TEST_F(NavigationSystemTest, AnchorsAreMoversWithSightPlusBuffer) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	const EntityID	  plain = world.createEntity();
	world.addComponent<Position>(plain, Position{{10.0F, 0.0F}});
	world.addComponent<AgentRadius>(plain, AgentRadius{});
	const EntityID farSighted = addColonist(world, {20.0F, 0.0F}, "Lookout");
	Memory		   memory;
	memory.sightRadius = 200.0F;
	world.addComponent<Memory>(farSighted, memory);
	const EntityID notMover = world.createEntity();
	world.addComponent<Position>(notMover, Position{{30.0F, 0.0F}});
	world.addComponent<Task>(notMover, Task{});

	std::vector<world::ChunkAnchor> anchors;
	sys.appendAnchors(anchors);
	ASSERT_EQ(anchors.size(), 2U);
	std::sort(anchors.begin(), anchors.end(), [](const auto& a, const auto& b) { return a.position.x < b.position.x; });
	EXPECT_EQ(anchors[0].position.x, 10.0F);
	EXPECT_EQ(anchors[0].rangeMeters, kDefaultSightRadius + NavigationSystem::kSightBufferMeters);
	EXPECT_EQ(anchors[0].rangeMeters, 46.0F);
	EXPECT_EQ(anchors[1].position.x, 20.0F);
	EXPECT_EQ(anchors[1].rangeMeters, 216.0F) << "R is never clamped";
}

// A colonist walking a long route whose goal waits for its ground: only the colonist anchors, so the
// chunks under the route's rest and at the goal are not held by those alone.
TEST_F(NavigationSystemTest, RouteRestAndWaitingGoalHoldNoChunk) {
	World			  world;
	NavigationSystem& sys	   = world.registerSystem<NavigationSystem>();
	const glm::vec2	  at	   = {256.0F, 256.0F};
	const glm::vec2	  goal	   = {5000.0F, 256.0F};
	const EntityID	  colonist = addColonist(world, at, "Walker");
	Task&			  task	   = *world.getComponent<Task>(colonist);
	task.state				   = TaskState::Moving;
	task.navState			   = NavState::AwaitingMesh;
	task.targetPosition		   = goal;
	NavPath path;
	path.waypoints = {at, {2600.0F, 256.0F}, goal};
	path.current   = 1;
	path.valid	   = true;
	world.addComponent<NavPath>(colonist, path);

	std::vector<world::ChunkAnchor> anchors;
	sys.appendAnchors(anchors);
	ASSERT_EQ(anchors.size(), 1U);
	EXPECT_EQ(anchors[0].position.x, at.x);

	world::ChunkManager chunks(landSampler());
	chunks.setLoadRadius(0);
	chunks.setUnloadRadius(0);
	chunks.update({-100000.0F, -100000.0F}, anchors);
	EXPECT_NE(chunks.getChunk({0, 0}), nullptr);
	EXPECT_EQ(chunks.getChunk({5, 0}), nullptr) << "the route's rest held a chunk";
	EXPECT_EQ(chunks.getChunk({9, 0}), nullptr) << "the waiting goal held a chunk";
	chunks.finishPendingGeneration();
}

// --- A shore point among trunks ------------------------------------------------------------------

namespace {
	// The game's forest since the forest-floor change: a tree about every 3.5 m (80 per 1,000 m2),
	// each sapling, mature or veteran by the oak's weights, with that class's trunk rect.
	void registerAgedTree(const std::string& defName) {
		AssetDefinition tree;
		tree.defName	= defName;
		tree.ageClasses = {{.name = "sapling", .weight = 0.15F}, {.name = "mature", .weight = 0.6F}, {.name = "veteran", .weight = 0.25F}};
		const float halfExtents[] = {0.125F, 0.225F, 0.36F};
		for (std::uint8_t a = 0; a < 3; ++a) {
			tree.setCollision(a, {.type = CollisionShapeType::Rect, .halfExtentsMeters = {halfExtents[a], halfExtents[a]}});
		}
		AssetRegistry::Get().registerTestDefinition(tree);
	}

	void placeAgedTrees(PlacementExecutor& executor, std::unordered_set<ChunkCoordinate>& processed, const std::string& defName,
						const std::vector<std::pair<glm::vec2, std::uint8_t>>& trees) {
		std::unordered_map<ChunkCoordinate, assets::AsyncChunkPlacementResult> byChunk;
		for (const auto& [p, ageClass] : trees) {
			const ChunkCoordinate coord = world::worldToChunk({p.x, p.y});
			auto&				  r		= byChunk[coord];
			r.coord						= coord;
			PlacedEntity e;
			e.defName		  = defName;
			e.position		  = p;
			e.traits.ageClass = ageClass;
			r.spatialIndex.insert(e);
		}
		for (auto& [coord, result] : byChunk) {
			executor.storeChunkResult(std::move(result));
			processed.insert(coord);
		}
	}
} // namespace

// A thirsty colonist beside the pond, in forest at the game's density, knowing two shore points each
// ringed by six mature trunks so close no 0.6 m disc gets between them. Trees are priced, not walled,
// so a shore point is never sealed: he gets a route, walks, drinks and his thirst is restored, and he
// never switches between the two targets.
TEST_F(NavigationSystemTest, ShorePointsRingedByTrunksAreReachedAndDrunkAt) {
	{
		const std::string treeDef = "Test_NavShoreTree";
		registerAgedTree(treeDef);
		AssetDefinition water;
		water.defName = "Water";
		water.label	  = "Water";
		AssetRegistry::Get().registerTestDefinition(water);

		const glm::vec2 pond{static_cast<float>(kPatchPond.cx), static_cast<float>(kPatchPond.cy)};
		const float		rimA = static_cast<float>(worldgen::PondNetwork2D::rimRadiusAt(kPatchPond, 0.0));
		const float		rimB = static_cast<float>(worldgen::PondNetwork2D::rimRadiusAt(kPatchPond, 0.7853981633974483));
		float			rim	 = 0.0F;
		for (int i = 0; i < 360; ++i) {
			rim = std::max(rim, static_cast<float>(worldgen::PondNetwork2D::rimRadiusAt(kPatchPond, 6.283185307179586 * i / 360.0)));
		}
		const glm::vec2 shoreA = pond + (rimA + 1.0F) * glm::vec2{1.0F, 0.0F};
		const glm::vec2 shoreB = pond + (rimB + 1.0F) * glm::vec2{0.7071F, 0.7071F};
		std::vector<std::pair<glm::vec2, std::uint8_t>> trees;
		for (const glm::vec2 shore : {shoreA, shoreB}) {
			for (int k = 0; k < 6; ++k) {
				const float angle = 6.2831853F * static_cast<float>(k) / 6.0F;
				trees.emplace_back(shore + 0.95F * glm::vec2{std::cos(angle), std::sin(angle)}, std::uint8_t{1});
			}
		}
		for (float x = pond.x - 30.0F; x < pond.x + 30.0F; x += 3.54F) {
			for (float y = pond.y - 30.0F; y < pond.y + 30.0F; y += 3.54F) {
				const glm::vec2		p{x + std::fmod(y * 0.37F, 1.4F), y + std::fmod(x * 0.53F, 1.4F)};
				const std::uint32_t h = static_cast<std::uint32_t>(std::abs(p.x * 131.0F + p.y * 7.0F));
				if (glm::distance(p, pond) < rim + 1.5F || glm::distance(p, shoreA) < 2.0F || glm::distance(p, shoreB) < 2.0F) {
					continue;
				}
				trees.emplace_back(p, std::uint8_t{h % 100 < 15 ? 0U : h % 100 < 75 ? 1U : 2U});
			}
		}

		WalkRig rig(pond, waterPatchChunks());
		placeAgedTrees(rig.executor, rig.processed, treeDef, trees);
		rig.start();
		rig.world.registerSystem<ActionSystem>(7U);
		ASSERT_EQ(rig.nav.isValidPosition(shoreA), true) << "the shore point itself is clear ground";
		ASSERT_FALSE(meshClear(rig.nav, trees.front().first)) << "the ring is in the mesh";

		const glm::vec2 start{pond.x + 25.0F, pond.y - 4.0F};
		const EntityID	colonist = needyColonist(rig, start, NeedType::Thirst, 8.0F);
		rig.world.addComponent<Action>(colonist, Action{});
		const uint32_t waterId = AssetRegistry::Get().getDefNameId("Water");
		const auto	   mask	   = static_cast<uint16_t>(1U << static_cast<size_t>(assets::CapabilityType::Drinkable));
		for (const glm::vec2 shore : {shoreA, shoreB}) {
			rig.world.getComponent<Memory>(colonist)->rememberWorldEntity(shore, waterId, mask);
		}

		int						 targetChanges = 0;
		std::optional<glm::vec2> lastTarget;
		bool					 routed = false;
		const NeedsComponent&	 needs	= *rig.world.getComponent<NeedsComponent>(colonist);
		for (int i = 0; i < 3000 && needs.get(NeedType::Thirst).value < 50.0F; ++i) {
			rig.world.update(0.05F);
			const Task& task = *rig.world.getComponent<Task>(colonist);
			if (task.type == TaskType::FulfillNeed && task.needToFulfill == NeedType::Thirst) {
				if (!lastTarget.has_value() || glm::distance(*lastTarget, task.targetPosition) > 0.01F) {
					++targetChanges;
					lastTarget = task.targetPosition;
				}
				const NavPath* path = rig.world.getComponent<NavPath>(colonist);
				routed				= routed || (path != nullptr && path->valid);
				EXPECT_NE(task.navState, NavState::CantFindWayTo) << "the route to (" << task.targetPosition.x << ", " << task.targetPosition.y
																  << ") failed at tick " << i;
			}
		}
		EXPECT_TRUE(routed) << "never got a route";
		EXPECT_GT(needs.get(NeedType::Thirst).value, 50.0F) << "never drank; it stands "
														   << glm::distance(rig.world.getComponent<Position>(colonist)->value, shoreA) << " m from A";
		EXPECT_EQ(targetChanges, 1) << "it switched between the shore points";
	}
}
