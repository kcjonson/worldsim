#include "NavigationSystem.h"

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
#include "nav/NavInputBuilder.h"

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
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <map>
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

	// Ground the nav mesh holds clear: on it, and neither water nor a tree in it (what isValidPosition
	// reads), straight from the mesh.
	bool meshClear(const NavigationSystem& sys, glm::vec2 meters) {
		const geometry::nav::PatchableMesh& mesh = sys.navMesh();
		const std::int32_t					tri	 = mesh.locate(nav::toMm(meters));
		return tri >= 0 && geometry::nav::terrainTraversable(mesh.triangles()[static_cast<std::size_t>(tri)]);
	}

	// The mesh passes its full consistency check.
	void expectMeshValid(const NavigationSystem& sys) {
		EXPECT_EQ(sys.navMesh().validate(), "") << "nav mesh";
	}

	// Settle, then check the mesh.
	void settle(NavigationSystem& sys) {
		sys.finishPendingBuilds();
		expectMeshValid(sys);
	}

	// One budgeted tick of the world (the update, then the job its endTick hands over), then check
	// both meshes.
	void step(World& world, const NavigationSystem& sys) {
		world.update(0.016F);
		expectMeshValid(sys);
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

	// The route by ticket from `from` to `to`, ticking nav alone (nobody moves) until it is answered:
	// its waypoints when routed, else none.
	std::optional<std::vector<glm::vec2>> routeBetween(NavigationSystem& sys, glm::vec2 from, glm::vec2 to,
													   const geometry::nav::BeliefFilter& belief = {}, float radius = kAgentRadius) {
		constexpr int		   kLimit = 600;
		const nav::RouteTicket ticket = sys.requestRoute(from, std::array{to}, radius, belief);
		for (int i = 0; i < kLimit; ++i) {
			if (std::optional<NavigationSystem::RouteAnswer> answer = sys.takeRouteAnswer(ticket)) {
				if (answer->outcome != nav::RouteOutcome::Routed) {
					return std::nullopt;
				}
				return std::move(answer->waypoints);
			}
			sys.update(0.016F);
			sys.endTick();
		}
		ADD_FAILURE() << "no answer in " << kLimit << " ticks";
		sys.cancelRoute(ticket);
		return std::nullopt;
	}

} // namespace

// Before any mesh is built there is no route and no known way.
TEST_F(NavigationSystemTest, NoMeshBeforeBuildReturnsNullopt) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();

	EXPECT_FALSE(sys.hasMesh());
	EXPECT_FALSE(routeBetween(sys, kOutside, kInside).has_value());
	EXPECT_FALSE(sys.connected(kOutside, kInside));
}

// update() with nothing wired is a safe no-op, and a query still returns nullopt.
TEST_F(NavigationSystemTest, UnwiredUpdateIsNoOp) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();

	for (int i = 0; i < 10; ++i) {
		world.update(0.016F);
	}
	EXPECT_FALSE(sys.hasMesh());
	EXPECT_FALSE(routeBetween(sys, kOutside, kInside).has_value());
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

	std::optional<std::vector<glm::vec2>> path = routeBetween(sys, kOutside, kInside);
	ASSERT_TRUE(path.has_value());
	EXPECT_GE(path->size(), 2u);
	EXPECT_TRUE(sys.connected(kOutside, kInside));
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
	EXPECT_FALSE(routeBetween(sys, kOutside, kInside).has_value());
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
	EXPECT_FALSE(routeBetween(sys, kOutside, kInside).has_value());
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
	ASSERT_TRUE(routeBetween(sys, kOutside, kInside).has_value()) << "open ground routes";

	const SegmentId south = buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);
	step(world, sys);
	EXPECT_FALSE(routeBetween(sys, kOutside, kInside).has_value()) << "the walls block in the tick they are seen";

	const OpeningId op = cw.addOpening(south, 0.5F, "Door", "Wood");
	ASSERT_NE(op, kInvalidOpening);
	ASSERT_TRUE(cw.setOpeningState(op, FoundationState::Built));
	step(world, sys);
	EXPECT_TRUE(routeBetween(sys, kOutside, kInside).has_value()) << "the door opens the room";
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
	ASSERT_TRUE(routeBetween(sys, kOutside, kInside).has_value()) << "door room should be pathable";

	const std::uint64_t before = cw.version();
	ASSERT_FALSE(cw.openings().empty());
	for (const auto& op : cw.openings()) {
		ASSERT_TRUE(cw.setOpeningState(op.id, FoundationState::Blueprint));
	}
	ASSERT_NE(cw.version(), before);

	settle(sys);
	EXPECT_FALSE(routeBetween(sys, kOutside, kInside).has_value()) << "blueprinting the door never re-sealed the room";
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
	EXPECT_TRUE(routeBetween(sys, kOutside, kInside).has_value());
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

	EXPECT_TRUE(routeBetween(sys, kOutside, kInside).has_value()) << "truth query should route through the real door";

	const std::unordered_set<std::uint64_t> noSegments;
	const std::unordered_set<std::uint64_t> noOpenings;
	const geometry::nav::BeliefFilter		blindBelief{&noSegments, &noOpenings};
	EXPECT_TRUE(routeBetween(sys, kOutside, kInside, blindBelief).has_value())
		<< "blind belief should optimistically route through the unseen wall";

	const std::unordered_set<std::uint64_t> knownWalls = allSegmentIds(cw);
	const geometry::nav::BeliefFilter		walledBelief{&knownWalls, &noOpenings};
	EXPECT_FALSE(routeBetween(sys, kOutside, kInside, walledBelief).has_value())
		<< "knowing the walls (but not the door) should block the route";

	std::unordered_set<std::uint64_t> knownDoors;
	for (const auto& op : cw.openings()) {
		knownDoors.insert(static_cast<std::uint64_t>(op.id));
	}
	ASSERT_FALSE(knownDoors.empty());
	const geometry::nav::BeliefFilter doorBelief{&knownWalls, &knownDoors};
	EXPECT_TRUE(routeBetween(sys, kOutside, kInside, doorBelief).has_value())
		<< "knowing the walls and the door should route through it";
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

// --- Flora ---------------------------------------------------------------------

// A mover meshes the 16 m cells whose nearest point is within R (sight plus kSightBufferMeters) of
// where it stands, so a longer sight distance meshes a larger disc. From x = 8, R = 46 m reaches the
// cell [48, 64) (40 m) and not [64, 80) (56 m); a sight of 70 m (R = 86 m) reaches [80, 96) (72 m)
// and not [96, 112) (88 m).
TEST_F(NavigationSystemTest, MoverWithLongerSightMeshesALargerDisc) {
	ASSERT_EQ(kDefaultSightRadius + NavigationSystem::kSightBufferMeters, 46.0F);
	registerTree("Test_NavSightTree", 0.4F);
	const glm::vec2 inDefault{52.0F, 8.0F};
	const glm::vec2 pastDefault{68.0F, 8.0F};
	const glm::vec2 inLong{88.0F, 8.0F};
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

// A footprint over a tree is buildable (the tree is cleared, not built round) while the nav mesh
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
	EXPECT_TRUE(sys.isAreaBuildable(footprint)) << "the tree is clearable, not a blocker";
	EXPECT_TRUE(sys.isPointBuildable(treeAt));
}

// A tree in a room's doorway seals it for a route; felling it opens the doorway again and patches only
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
	wireArea(world, sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	EXPECT_FALSE(routeBetween(sys, kOutside, kInside).has_value()) << "a route went through the tree in the doorway";

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
	EXPECT_TRUE(routeBetween(sys, kOutside, kInside).has_value()) << "the doorway is still shut";
	EXPECT_FALSE(meshClear(sys, {30.0F, 30.0F})) << "the other tree stays";
	for (const Kept& k : far) {
		ASSERT_TRUE(mesh.triangleLive(k.id)) << "triangle " << k.id;
		for (int i = 0; i < 3; ++i) {
			EXPECT_EQ(mesh.vertices()[mesh.triangles()[static_cast<std::size_t>(k.id)].v[i]], k.corners[i]) << "triangle " << k.id;
		}
	}
}

// A flora patch (a tree felled, late placement) leaves generation(); a wall moves it.
TEST_F(NavigationSystemTest, GenerationIgnoresFloraPatches) {
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
	const std::uint64_t generation = sys.generation();

	ASSERT_TRUE(executor.removeEntity(world::worldToChunk({treeAt.x, treeAt.y}), treeAt, "Test_NavReachKeyTree"));
	step(world, sys);
	ASSERT_TRUE(meshClear(sys, treeAt)) << "the felled tree was patched out";
	EXPECT_EQ(sys.generation(), generation) << "a flora patch moved the generation";

	placeTrees(executor, processed, "Test_NavReachKeyTree", {{-14.0F, 10.0F}}); // late placement in chunk (-1, 0)
	step(world, sys);
	ASSERT_FALSE(meshClear(sys, {-14.0F, 10.0F})) << "the late tree is in";
	EXPECT_EQ(sys.generation(), generation);

	buildWall(cw, {20000, 20000}, {22000, 20000});
	step(world, sys);
	EXPECT_NE(sys.generation(), generation) << "a wall moves the generation";
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
	const std::uint64_t generation = sys.generation();

	buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);
	sys.update(0.016F);
	EXPECT_TRUE(sys.isBuilding()) << "the room is gathered";
	EXPECT_EQ(sys.generation(), generation) << "but not applied within its own tick";

	sys.endTick();
	EXPECT_EQ(sys.generation(), generation + 1) << "applied once the tick ended";
	EXPECT_FALSE(sys.isBuilding());
	expectMeshValid(sys);
}

// --- One mesh ------------------------------------------------------------------

// WOR-644: two colonists far apart. Their ground is one mesh, so a route between them exists and
// connected() agrees.
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
	EXPECT_TRUE(routeBetween(sys, a, b).has_value());
	EXPECT_TRUE(sys.connected(a, b));
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
	EXPECT_EQ(sys.navMesh().triangles().size(), slots);
	EXPECT_EQ(sys.generation(), generation);
}

namespace {
	// The camera's chunks and the anchors' together, loaded, as GameScene loads them each frame.
	void loadFor(world::ChunkManager& chunks, const NavigationSystem& sys, glm::vec2 camera) {
		std::vector<world::ChunkAnchor> anchors;
		sys.appendAnchors(anchors);
		chunks.update({camera.x, camera.y}, anchors);
		chunks.finishPendingGeneration();
	}

	// Mesh edges on the line x = xMm between y loMm and hiMm, each counted once.
	struct LineEdges {
		int boundary		   = 0; // the mesh ends there
		int landConstrained	   = 0; // floor on both sides, on a ring edge
		int blockedConstrained = 0; // blocked terrain on both sides, on a ring edge
	};

	LineEdges edgesOnLine(const geometry::nav::PatchableMesh& mesh, std::int64_t xMm, std::int64_t loMm, std::int64_t hiMm) {
		LineEdges out;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const geometry::nav::NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
			for (std::size_t e = 0; e < 3; ++e) {
				const Vec2i64& a = mesh.vertices()[tri.v[e]];
				const Vec2i64& b = mesh.vertices()[tri.v[(e + 1) % 3]];
				if (a.x != xMm || b.x != xMm || std::min(a.y, b.y) < loMm || std::max(a.y, b.y) > hiMm) {
					continue;
				}
				const std::int32_t across = tri.neighbor[e];
				if (across < 0) {
					++out.boundary;
					continue;
				}
				if (across < t || mesh.edgeSources(t, static_cast<int>(e)).empty()) {
					continue;
				}
				const geometry::nav::NavTriangle& other = mesh.triangles()[static_cast<std::size_t>(across)];
				if (geometry::nav::isFloorFace(tri) && geometry::nav::isFloorFace(other)) {
					++out.landConstrained;
				} else if (!geometry::nav::terrainTraversable(tri) && !geometry::nav::terrainTraversable(other)) {
					++out.blockedConstrained;
				}
			}
		}
		return out;
	}
} // namespace

// A chunk arriving next to meshed ground joins it: a path crosses the border. The chunk comes because
// a mover walked up to it, with the camera still. Before, its side is the mesh's boundary; after, the
// side shared by the two squares is no edge the mesh keeps.
TEST_F(NavigationSystemTest, ArrivingChunkJoinsItsNeighbour) {
	auto chunks = std::make_unique<world::ChunkManager>(landSampler());
	chunks->setLoadRadius(0);
	chunks->setUnloadRadius(8);
	const glm::vec2 camera{256.0F, 256.0F};

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	sys.setConstructionWorld(&cw);
	const EntityID mover = meshAround(world, {300.0F, 100.0F}, 30.0F); // holds and wants (0, 0) only
	loadFor(*chunks, sys, camera);
	settle(sys);
	const glm::vec2 west{500.0F, 100.0F};
	const glm::vec2 east{530.0F, 100.0F};
	ASSERT_TRUE(sys.inSimArea(west));
	ASSERT_FALSE(sys.inSimArea(east));
	ASSERT_EQ(chunks->getChunk({1, 0}), nullptr);
	EXPECT_FALSE(routeBetween(sys, west, east).has_value());
	EXPECT_EQ(edgesOnLine(sys.navMesh(), 512000, 0, 512000).boundary, 8) << "the side, in its eight pieces";

	world.getComponent<Position>(mover)->value = {512.0F, 100.0F};
	step(world, sys); // wants (1, 0)
	loadFor(*chunks, sys, camera);
	settle(sys);
	EXPECT_TRUE(routeBetween(sys, west, east).has_value()) << "the path crosses x = 512 m";
	EXPECT_TRUE(sys.connected(west, east));
	const LineEdges shared = edgesOnLine(sys.navMesh(), 512000, 0, 512000);
	EXPECT_EQ(shared.boundary, 0);
	EXPECT_EQ(shared.landConstrained, 0) << "a constrained edge between land on the shared side";
}

namespace {
	// Placed obstacles every 1.6 m, 44 across and 30 up, centred on `gapOrigin`: the gaps between
	// them lie on x = gapOrigin.x + 1.6 i and y = gapOrigin.y + 1.6 j.
	std::vector<glm::vec2> postField(glm::vec2 gapOrigin) {
		std::vector<glm::vec2> posts;
		for (int i = -22; i < 22; ++i) {
			for (int j = -15; j < 15; ++j) {
				posts.push_back(gapOrigin + glm::vec2{0.8F + 1.6F * static_cast<float>(i), 0.8F + 1.6F * static_cast<float>(j)});
			}
		}
		return posts;
	}

	constexpr float		  kPostHalf = 0.25F;
	constexpr const char* kPost		= "Test_NavBorderPost";

	// The longest edge of a floor triangle meeting the segment x = xMm, y loMm..hiMm.
	double longestEdgeAcross(const geometry::nav::PatchableMesh& mesh, std::int64_t xMm, std::int64_t loMm, std::int64_t hiMm) {
		double longest = 0.0;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const geometry::nav::NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
			if (!geometry::nav::isFloorFace(tri)) {
				continue;
			}
			const std::array<Vec2i64, 3> p{mesh.vertices()[tri.v[0]], mesh.vertices()[tri.v[1]], mesh.vertices()[tri.v[2]]};
			// The triangle's span along the line, from where its edges meet it.
			double spanLo = std::numeric_limits<double>::max();
			double spanHi = std::numeric_limits<double>::lowest();
			for (std::size_t i = 0; i < 3; ++i) {
				const Vec2i64& a = p[i];
				const Vec2i64& b = p[(i + 1) % 3];
				if (std::min(a.x, b.x) > xMm || std::max(a.x, b.x) < xMm) {
					continue;
				}
				const double ys[2] = {static_cast<double>(a.y), static_cast<double>(b.y)};
				const double y	   = a.x == b.x ? ys[0] : ys[0] + (ys[1] - ys[0]) * static_cast<double>(xMm - a.x) / static_cast<double>(b.x - a.x);
				spanLo			   = std::min({spanLo, y, a.x == b.x ? ys[1] : y});
				spanHi			   = std::max({spanHi, y, a.x == b.x ? ys[1] : y});
			}
			if (spanLo > static_cast<double>(hiMm) || spanHi < static_cast<double>(loMm)) {
				continue;
			}
			for (std::size_t i = 0; i < 3; ++i) {
				const double dx = static_cast<double>(p[(i + 1) % 3].x - p[i].x);
				const double dy = static_cast<double>(p[(i + 1) % 3].y - p[i].y);
				longest			= std::max(longest, std::sqrt(dx * dx + dy * dy));
			}
		}
		return longest;
	}
} // namespace

// The same route through a post field is as long when the field straddles a chunk border as when the
// field lies inside one chunk: the border adds no detour.
TEST_F(NavigationSystemTest, RouteThroughPostsAcrossABorderIsAsLongAsInsideAChunk) {
	registerTree(kPost, kPostHalf);
	const auto routeLength = [](glm::vec2 gapOrigin) {
		PlacementExecutor					executor(AssetRegistry::Get());
		std::unordered_set<ChunkCoordinate> processed;
		placeTrees(executor, processed, kPost, postField(gapOrigin));
		ConstructionWorld cw;
		World			  world;
		NavigationSystem& sys = world.registerSystem<NavigationSystem>();
		sys.setChunkManager(landChunks());
		sys.setPlacementData(&executor, &processed);
		sys.setConstructionWorld(&cw);
		meshAround(world, gapOrigin - glm::vec2{12.0F, 0.0F}, 50.0F);
		settle(sys);
		const std::optional<std::vector<glm::vec2>> route =
			routeBetween(sys, gapOrigin + glm::vec2{-9.6F, -4.8F}, gapOrigin + glm::vec2{9.6F, 6.4F});
		float length = -1.0F;
		if (route.has_value()) {
			length = 0.0F;
			for (std::size_t i = 1; i < route->size(); ++i) {
				length += glm::distance((*route)[i - 1], (*route)[i]);
			}
		}
		return length;
	};
	const float across = routeLength({512.0F, 100.0F});
	const float inside = routeLength({256.0F, 100.0F});
	ASSERT_GT(inside, 0.0F) << "no route inside chunk (0, 0)";
	ASSERT_GT(across, 0.0F) << "no route across x = 512 m";
	EXPECT_NEAR(across, inside, 0.01F);
}

// A mover in a post field beside a chunk border meshes both chunks: the floor triangles over the
// border are no longer than those over a line of the field inside the chunk. While only one of the
// two is meshed its side is the mesh's boundary, cut every 64 m, and they are.
TEST_F(NavigationSystemTest, BorderInAPostFieldMeshesLikeTheInterior) {
	registerTree(kPost, kPostHalf);
	auto chunks = std::make_unique<world::ChunkManager>(landSampler());
	chunks->setLoadRadius(0);
	chunks->setUnloadRadius(8);
	const glm::vec2 camera{256.0F, 256.0F};
	chunks->update({camera.x, camera.y});
	chunks->finishPendingGeneration();
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	placeTrees(executor, processed, kPost, postField({512.0F, 100.0F}));

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	meshAround(world, {500.0F, 100.0F}, 50.0F);
	settle(sys);
	ASSERT_FALSE(sys.inSimArea({530.0F, 100.0F}));

	constexpr std::int64_t kBorderMm  = 512000;
	constexpr std::int64_t kInteriorMm = 486400; // 16 gaps west of the border
	constexpr std::int64_t kLoMm	   = 84000;
	constexpr std::int64_t kHiMm	   = 116000;
	const double		   oneSided	   = longestEdgeAcross(sys.navMesh(), kBorderMm, kLoMm, kHiMm);
	const double		   bound	   = 1.25 * longestEdgeAcross(sys.navMesh(), kInteriorMm, kLoMm, kHiMm);
	EXPECT_GT(oneSided, bound) << "one chunk meshed: its side holds the triangles along it to 64 m pieces";

	loadFor(*chunks, sys, camera);
	settle(sys);
	ASSERT_TRUE(sys.inSimArea({530.0F, 100.0F}));
	const double interior = longestEdgeAcross(sys.navMesh(), kInteriorMm, kLoMm, kHiMm);
	const double border	  = longestEdgeAcross(sys.navMesh(), kBorderMm, kLoMm, kHiMm);
	RecordProperty("oneSidedLongestMm", std::to_string(oneSided));
	RecordProperty("borderLongestMm", std::to_string(border));
	RecordProperty("interiorLongestMm", std::to_string(interior));
	ASSERT_GT(interior, 0.0);
	EXPECT_LE(border, 1.25 * interior);
}

// A wanted chunk whose mover left before it loaded is not meshed when only the camera loads it; it is
// when a mover comes back for it.
TEST_F(NavigationSystemTest, WantedChunkTheCameraLoadsWaitsForAMover) {
	registerTree("Test_NavWaitingTree", 0.4F);
	auto chunks = std::make_unique<world::ChunkManager>(landSampler());
	chunks->setLoadRadius(0);
	chunks->setUnloadRadius(8);
	chunks->update({256.0F, 256.0F});
	chunks->finishPendingGeneration();
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						treeAt{525.0F, 100.0F}; // chunk (1, 0)
	placeTrees(executor, processed, "Test_NavWaitingTree", {treeAt});

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	const EntityID mover = meshAround(world, {500.0F, 100.0F}, 30.0F); // wants (1, 0), which isn't loaded
	settle(sys);
	ASSERT_FALSE(sys.inSimArea({530.0F, 100.0F}));

	world.getComponent<Position>(mover)->value = {100.0F, 100.0F}; // holds (0, 0) only
	step(world, sys);
	chunks->update({768.0F, 256.0F}); // the camera alone loads (1, 0)
	chunks->finishPendingGeneration();
	ASSERT_NE(chunks->getChunk({1, 0}), nullptr);
	sys.resetNavQueryStats();
	for (int i = 0; i < 4; ++i) {
		step(world, sys);
	}
	settle(sys);
	EXPECT_FALSE(sys.inSimArea({530.0F, 100.0F})) << "the camera meshed its ground";
	EXPECT_EQ(sys.navQueryStats().navPatches, 0u) << "the camera patched the nav mesh";

	world.getComponent<Position>(mover)->value = {500.0F, 100.0F};
	settle(sys);
	EXPECT_TRUE(sys.inSimArea({530.0F, 100.0F})) << "the mover came back and its ground came in";
	EXPECT_FALSE(meshClear(sys, treeAt)) << "and the tree in it";
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
	EXPECT_TRUE(routeBetween(sys, {100.0F, 100.0F}, {700.0F, 300.0F}).has_value()) << "an order routes across it";

	// Back, with its placement placed again.
	const std::uint64_t before = sys.generation();
	world.getComponent<Position>(mover)->value = {490.0F, 256.0F};
	load({256.0F, 256.0F});
	ASSERT_NE(chunks->getChunk({0, 0}), nullptr);
	placeTrees(executor, processed, "Test_NavUnloadTree", {treeAt});
	settle(sys);
	EXPECT_EQ(sys.navQueryStats().navPatches, 0u) << "the reload patched the nav mesh";
	EXPECT_EQ(sys.generation(), before);
	EXPECT_EQ(sys.navMesh().liveTriangleCount(), slots) << "nothing added twice";
	EXPECT_FALSE(meshClear(sys, treeAt));
}

// The same scenario twice gives the same mesh, slot for slot, and the same paths.
TEST_F(NavigationSystemTest, SameScenarioSameMeshAndPaths) {
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
		expectMeshValid(r.sys);
	};
	Run first;
	Run second;
	play(first);
	play(second);

	const geometry::nav::PatchableMesh& a = first.sys.navMesh();
	const geometry::nav::PatchableMesh& b = second.sys.navMesh();
	ASSERT_EQ(a.triangles().size(), b.triangles().size());
	ASSERT_EQ(a.liveTriangleCount(), b.liveTriangleCount());
	for (std::int32_t t = 0; t < static_cast<std::int32_t>(a.triangles().size()); ++t) {
		ASSERT_EQ(a.triangleLive(t), b.triangleLive(t)) << "slot " << t;
		if (!a.triangleLive(t)) {
			continue;
		}
		const geometry::nav::NavTriangle& ta = a.triangles()[static_cast<std::size_t>(t)];
		const geometry::nav::NavTriangle& tb = b.triangles()[static_cast<std::size_t>(t)];
		for (int i = 0; i < 3; ++i) {
			EXPECT_EQ(a.vertices()[ta.v[i]], b.vertices()[tb.v[i]]) << "slot " << t;
			EXPECT_EQ(ta.neighbor[i], tb.neighbor[i]) << "slot " << t;
			EXPECT_EQ(ta.edgePairWidthMm[i], tb.edgePairWidthMm[i]) << "slot " << t;
			EXPECT_EQ(ta.edgeOpening[i], tb.edgeOpening[i]) << "slot " << t;
		}
		EXPECT_EQ(ta.faceBlocker, tb.faceBlocker) << "slot " << t;
		EXPECT_EQ(ta.faceOpening, tb.faceOpening) << "slot " << t;
	}
	for (const auto& [from, to] : pairs) {
		const auto pa = routeBetween(first.sys, from, to);
		const auto pb = routeBetween(second.sys, from, to);
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
	// a 4 m pond every 10 m within kFordX + 40 m of the origin, so the mesh is fine everywhere a
	// search to the far bank goes and the way round the ford takes thousands of steps.
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

// The far bank of a river whose ford is 150 m east, among ponds, is about 15,000 steps round. The
// route by ticket finds it, round the ford, and every leg of the route is clear of water.
TEST_F(NavigationSystemTest, FarGoalIsRoutedRoundTheFord) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	ConstructionWorld cw;
	sys.setChunkManager(pondFieldChunks<150>());
	meshAround(world, {0.0F, 0.0F}, 1536.0F);
	sys.setConstructionWorld(&cw);
	settle(sys);

	const std::optional<std::vector<glm::vec2>> path = routeBetween(sys, kPondStart, kPondAcross);
	ASSERT_TRUE(path.has_value()) << "no route round the ford";
	const std::vector<glm::vec2>& route = *path;
	EXPECT_LT(glm::distance(route.back(), kPondAcross), 0.01F);
	EXPECT_GT(std::max_element(route.begin(), route.end(), [](glm::vec2 a, glm::vec2 b) { return a.x < b.x; })->x, 150.0F)
		<< "the route doesn't go round the ford";
	for (std::size_t i = 1; i < route.size(); ++i) {
		EXPECT_TRUE(sys.isSegmentWalkable(route[i - 1], route[i])) << "leg " << i << " crosses water";
	}
}

// The ford 400 m east is about 800 m round through the ponds, more than one route slice of search: a
// route search has no step limit, so it carries on over the ticks it needs and finds the way.
TEST_F(NavigationSystemTest, FordFarAroundIsRoutedOverSeveralSlices) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	ConstructionWorld cw;
	sys.setChunkManager(pondFieldChunks<400>());
	meshAround(world, {0.0F, 0.0F}, 1536.0F);
	sys.setConstructionWorld(&cw);
	settle(sys);

	const nav::RouteTicket						ticket = sys.requestRoute(kPondStart, std::array{kPondAcross}, kAgentRadius);
	std::optional<NavigationSystem::RouteAnswer> answer;
	for (int i = 0; i < 600 && !answer.has_value(); ++i) {
		sys.update(0.016F);
		sys.endTick();
		answer = sys.takeRouteAnswer(ticket);
	}
	ASSERT_TRUE(answer.has_value());
	EXPECT_EQ(answer->outcome, nav::RouteOutcome::Routed);
	EXPECT_GT(answer->expansions, static_cast<std::int64_t>(nav::kRouteWorkPerSlice)) << "it fit in one slice: move the ford farther";
	EXPECT_LT(glm::distance(answer->waypoints.back(), kPondAcross), 0.01F);
}

// generation() moves once for a tick that brings ground into a chunk, and once for a tick that
// brings walls; a quiet tick after them leaves it.
TEST_F(NavigationSystemTest, GenerationMovesOncePerGroundOrWallJob) {
	auto chunks = std::make_unique<world::ChunkManager>(landSampler());
	chunks->setLoadRadius(0);
	chunks->setUnloadRadius(8);
	const glm::vec2	  camera{256.0F, 256.0F};
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	sys.setConstructionWorld(&cw);
	const EntityID mover = meshAround(world, {256.0F, 256.0F}, 30.0F); // holds and wants (0, 0) only
	loadFor(*chunks, sys, camera);
	settle(sys);
	ASSERT_FALSE(sys.inSimArea({768.0F, 256.0F}));
	const std::uint64_t key = sys.generation();

	// The mover walks into (1, 0), which loads round it.
	world.getComponent<Position>(mover)->value = {768.0F, 256.0F};
	loadFor(*chunks, sys, camera);
	step(world, sys);
	ASSERT_TRUE(sys.inSimArea({768.0F, 256.0F})) << "the new chunk came in";
	const std::uint64_t grounded = sys.generation();
	EXPECT_EQ(grounded, key + 1) << "ground arriving";

	buildWall(cw, {700000, 100000}, {704000, 100000});
	buildWall(cw, {700000, 110000}, {704000, 110000});
	step(world, sys);
	const std::uint64_t walled = sys.generation();
	EXPECT_EQ(walled, grounded + 1) << "walls";
	step(world, sys);
	EXPECT_EQ(sys.generation(), walled);
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
	EXPECT_FALSE(sys.connected({-30.0F, 0.0F}, {0.5F, 0.5F})) << "the pond's middle is water on every side";
	EXPECT_TRUE(routeBetween(sys, {-30.0F, 0.0F}, {30.0F, 0.0F}).has_value()) << "around the pond, across both borders";
}

// Each chunk's water arrives clipped to its square, so where the pond crosses x = 0 the border is
// two water rings' edges and stays constrained; along its land stretches the shared side is gone.
TEST_F(NavigationSystemTest, WaterStretchOfABorderStaysConstrained) {
	auto			  chunks = readyChunks(cornerPondSampler(), {0.0F, 0.0F}, 1);
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	sys.setConstructionWorld(&cw);
	meshAround(world, {0.0F, 0.0F}, 60.0F);
	settle(sys);

	const LineEdges border = edgesOnLine(sys.navMesh(), 0, -512000, 512000);
	EXPECT_GT(border.blockedConstrained, 0) << "the water stretch";
	EXPECT_EQ(border.landConstrained, 0) << "a land stretch";
	EXPECT_EQ(border.boundary, 0);
}

// --- Landing and placement -------------------------------------------------------

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
	expectMeshValid(sys);
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

// nearestTerrainWalkablePoint reads truth on the mesh: a point dead-center in a solid
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

// A colonist standing in a tree stands on the tree's face of the mesh, which the forward snap moves
// him out of, not off the mesh: the stranded recovery leaves him where he is.
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
	const auto routed = [&] {
		const NavPath* p = world.getComponent<NavPath>(colonist);
		return p != nullptr && p->valid && p->builtNavVersion == sys->generation();
	};
	for (int i = 0; i < 10 && !routed(); ++i) {
		step(world, *sys);
	}
	ASSERT_TRUE(routed()) << "no route";
	const NavPath& path = *world.getComponent<NavPath>(colonist);
	ASSERT_EQ(path.waypoints.size(), 2u) << "straight through where the tree will stand";

	placeTrees(executor, processed, "Test_NavSnapTree", {across});
	for (int i = 0; i < 10; ++i) {
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

// Any mover is snapped, not only a colonist: an entity with a position and a radius and nothing
// else, inside the tree on a route, moves forward along it like a colonist does.
TEST_F(ForwardSnapTest, MoverThatIsNotAColonistMovesForward) {
	const glm::vec2 at{9.7F, 10.0F};
	const EntityID	mover = world.createEntity();
	world.addComponent<Position>(mover, Position{at});
	world.addComponent<AgentRadius>(mover, AgentRadius{});
	NavPath path;
	path.waypoints = {{6.0F, 10.0F}, {14.0F, 10.0F}};
	path.current   = 1;
	path.valid	   = true;
	world.addComponent<NavPath>(mover, std::move(path));
	step(world, *sys);
	const glm::vec2 now = world.getComponent<Position>(mover)->value;
	EXPECT_TRUE(meshClear(*sys, now));
	EXPECT_FLOAT_EQ(now.y, 10.0F) << "on the route";
	EXPECT_GT(now.x, kTree.x + 0.8F + kAgentRadius) << "past the tree, ahead";
}

// Inside a tree with no route but moving east, it steps out ahead of itself, though the nearest
// clear ground is west, behind it.
TEST_F(ForwardSnapTest, MovingWithoutRouteNeverStepsBehind) {
	const glm::vec2 at{9.7F, 10.0F};
	const EntityID	colonist = routedColonist(world, at, {});
	world.addComponent<Velocity>(colonist, Velocity{{2.0F, 0.0F}});
	step(world, *sys);
	const glm::vec2 now = world.getComponent<Position>(colonist)->value;
	EXPECT_NE(now, at) << "snapped out of the tree";
	EXPECT_TRUE(meshClear(*sys, now));
	EXPECT_GT(now.x, at.x - 1e-3F) << "behind it";
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
			expectMeshValid(nav);
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
	// The gap between where the mover stands and the tree's cell, each axis.
	const auto meets = [r](glm::vec2 at, glm::vec2 tree) {
		const auto gap = [](float p, float t) {
			const float lo = std::floor(t / 16.0F) * 16.0F;
			return std::max({0.0F, lo - p, p - (lo + 16.0F)});
		};
		return std::hypot(gap(at.x, tree.x), gap(at.y, tree.y)) <= r;
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
	EXPECT_EQ(stats.groundWaterWallPatches, 4u) << "the four chunks round the origin, once each";
	EXPECT_EQ(stats.navPatches - stats.groundWaterWallPatches, wantedCells) << "one patch per wanted cell";
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
	const std::int32_t	navTriangles = sys.navMesh().liveTriangleCount();
	const std::uint64_t generation	 = sys.generation();

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
	EXPECT_EQ(sys.navQueryStats().groundWaterWallPatches, 0u) << "a removal patched ground or walls";
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
		const auto path = routeBetween(sys, {x - 5.0F, 10.0F}, {x + 5.0F, 10.0F});
		return path.has_value() && path->size() == 2;
	};
	ASSERT_FALSE(straight(10.0F)) << "the first world's wall is in";
	ASSERT_TRUE(straight(30.0F));

	sys.setConstructionWorld(&second);
	settle(sys);
	EXPECT_TRUE(straight(10.0F)) << "the old world's wall is gone";
	EXPECT_FALSE(straight(30.0F)) << "the new world's wall is in";
}

// --- Placed obstacles block routes ----------------------------------------------------------

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

// A goal ringed by trees so close no 0.6 m disc gets between them has no known way: the route
// answers so, and a walker ordered there never walks through the ring, collision and all.
TEST_F(NavigationSystemTest, GoalRingedByTreesHasNoKnownWay) {
	registerTree("Test_NavRingTree", 0.3F);
	const glm::vec2 goal{20.0F, 20.0F};
	const glm::vec2 from{10.0F, 20.0F};
	WalkRig			rig(goal);
	placeTrees(rig.executor, rig.processed, "Test_NavRingTree", treeRing(goal, 3.0F));
	settle(rig.nav);
	ASSERT_FALSE(meshClear(rig.nav, treeRing(goal, 3.0F).front())) << "the ring is in";
	const nav::RouteTicket						 ticket = rig.nav.requestRoute(from, std::array{goal}, kAgentRadius);
	std::optional<NavigationSystem::RouteAnswer> answer;
	for (int i = 0; i < 60 && !answer.has_value(); ++i) {
		answer = rig.nav.takeRouteAnswer(ticket);
		rig.nav.update(0.016F);
		rig.nav.endTick();
	}
	ASSERT_TRUE(answer.has_value());
	EXPECT_EQ(answer->outcome, nav::RouteOutcome::NoKnownWay);

	rig.start();
	const EntityID	colonist = rig.walker(from, goal);
	const Position& position = *rig.world.getComponent<Position>(colonist);
	for (int i = 0; i < 100; ++i) {
		rig.world.update(0.1F);
		ASSERT_GT(glm::distance(position.value, goal), 3.0F) << "walked into the ring at tick " << i;
	}
}

// Hedges are gone round, never through: a short one with a way round a few metres off, and one too
// long for that, whose way round is long.
TEST_F(NavigationSystemTest, HedgesAreGoneRoundNeverThrough) {
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

	const auto round = routeBetween(sys, {17.0F, 5.0F}, {23.0F, 5.0F});
	ASSERT_TRUE(round.has_value());
	EXPECT_GT(lengthOf(*round), 7.0F);
	EXPECT_LT(lengthOf(*round), 20.0F);

	const auto longWay = routeBetween(sys, {37.0F, 30.0F}, {43.0F, 30.0F});
	ASSERT_TRUE(longWay.has_value());
	EXPECT_GT(lengthOf(*longWay), 50.0F) << "through the long hedge";
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
	EXPECT_FALSE(rig.nav.connected({0.0F, 0.0F}, kUnmeshed));

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

// Knowing nearer water in ground no mover wants (so it isn't coming) and a farther source on meshed
// ground, the colonist takes the farther.
TEST_F(NavigationSystemTest, FartherReachableWaterBeatsNearerWaterInGroundThatIsntComing) {
	WalkRig rig({1300.0F, 0.0F});
	rig.start();
	const glm::vec2 at{1450.0F, 10.0F};
	const glm::vec2 gone{1540.0F, 10.0F};	// chunk (3, 0), beyond the loaded radius and every disc, 90 m off
	const glm::vec2 meshed{1350.0F, 10.0F}; // chunk (2, 0), 100 m off
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
	const auto routed = [&] {
		const NavPath* path = world.getComponent<NavPath>(colonist);
		return path != nullptr && path->valid;
	};
	for (int i = 0; i < 10 && !routed(); ++i) {
		world.update(0.05F);
	}
	ASSERT_TRUE(routed()) << "no route east";

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

// A colonist 292 m from chunk (1, 0), so his anchor does not keep it. The camera loads the chunk,
// which meshes nothing, and then wanders 20 chunks east. Neither moves generation().
TEST_F(NavigationSystemTest, CameraComingAndGoingLeavesTheGeneration) {
	auto chunks = std::make_unique<world::ChunkManager>(landSampler());
	chunks->setLoadRadius(0);
	chunks->setUnloadRadius(0);
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	sys.setConstructionWorld(&cw);
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
	const std::uint64_t generation = sys.generation();

	stand(1);
	ASSERT_NE(chunks->getChunk({1, 0}), nullptr) << "the camera loaded chunk (1, 0)";
	ASSERT_FALSE(sys.inSimArea({768.0F, 256.0F})) << "and meshed it";
	EXPECT_EQ(sys.generation(), generation);
	for (int i = 2; i < 22; ++i) {
		stand(i);
	}
	ASSERT_TRUE(sys.inSimArea(colonistAt)) << "the colonist's own ground stays";
	EXPECT_EQ(sys.generation(), generation) << "the camera moved the generation";
}

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

// A collider set off its entity's centre and mirrored by a negative scale is meshed on the mirrored side.
TEST_F(NavigationSystemTest, MirroredColliderIsMeshedOnItsMirroredSide) {
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
	EXPECT_FALSE(sys.isValidPosition({19.2F, 20.0F})) << "the mirrored collider is in the mesh";
	EXPECT_TRUE(sys.isValidPosition({20.8F, 20.0F})) << "and not where the unmirrored one would be";
}

// A sapling 0.04 m across, 0.2 m off the straight line, is gone round however narrow: the route
// bends round it, and no leg passes as near its trunk as the straight line does.
TEST_F(NavigationSystemTest, NarrowTrunkIsGoneRound) {
	registerTree("Test_NavSapling", 0.02F);
	const glm::vec2 sapling{20.03F, 20.2F};
	WalkRig			rig({20.0F, 20.0F});
	placeTrees(rig.executor, rig.processed, "Test_NavSapling", {sapling});
	settle(rig.nav);
	ASSERT_FALSE(meshClear(rig.nav, sapling)) << "the sapling is in the mesh";
	const auto route = routeBetween(rig.nav, {2.0F, 20.0F}, {38.0F, 20.0F});
	ASSERT_TRUE(route.has_value());
	EXPECT_GT(route->size(), 2U) << "straight past the sapling";
	for (std::size_t k = 1; k < route->size(); ++k) {
		const glm::vec2 a  = (*route)[k - 1];
		const glm::vec2 ab = (*route)[k] - a;
		const float		t  = std::clamp(glm::dot(sapling - a, ab) / std::max(glm::dot(ab, ab), 1e-9F), 0.0F, 1.0F);
		EXPECT_GT(glm::distance(a + t * ab, sapling), 0.25F) << "leg " << k << " passes through the sapling";
	}
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

// Two shore points of a pond in forest at the game's density: A, ringed by six mature trunks so close
// no 0.6 m disc gets between them, and B, open. Placed obstacles block, so a route to A has no known
// way, and a route to whichever is nearer on foot goes to B.
TEST_F(NavigationSystemTest, ShorePointRingedByTrunksHasNoKnownWay) {
	{
		const std::string treeDef = "Test_NavShoreTree";
		registerAgedTree(treeDef);

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
		for (int k = 0; k < 6; ++k) {
			const float angle = 6.2831853F * static_cast<float>(k) / 6.0F;
			trees.emplace_back(shoreA + 0.95F * glm::vec2{std::cos(angle), std::sin(angle)}, std::uint8_t{1});
		}
		for (float x = pond.x - 30.0F; x < pond.x + 30.0F; x += 3.54F) {
			for (float y = pond.y - 30.0F; y < pond.y + 30.0F; y += 3.54F) {
				const glm::vec2		p{x + std::fmod(y * 0.37F, 1.4F), y + std::fmod(x * 0.53F, 1.4F)};
				const std::uint32_t h = static_cast<std::uint32_t>(std::abs(p.x * 131.0F + p.y * 7.0F));
				if (glm::distance(p, pond) < rim + 1.5F || glm::distance(p, shoreA) < 2.0F || glm::distance(p, shoreB) < 2.0F) {
					continue;
				}
				trees.emplace_back(p, static_cast<std::uint8_t>(h % 100 < 15 ? 0U : h % 100 < 75 ? 1U : 2U));
			}
		}

		WalkRig rig(pond, waterPatchChunks());
		placeAgedTrees(rig.executor, rig.processed, treeDef, trees);
		settle(rig.nav);
		ASSERT_TRUE(rig.nav.isValidPosition(shoreA)) << "the shore point itself is clear ground";
		ASSERT_FALSE(meshClear(rig.nav, trees.front().first)) << "the ring is in the mesh";

		const glm::vec2 start{pond.x + 25.0F, pond.y - 4.0F};
		const auto		answer = [&](std::span<const glm::vec2> goals) {
			 const nav::RouteTicket ticket = rig.nav.requestRoute(start, goals, kAgentRadius);
			 for (int k = 0; k < 60; ++k) {
				 if (std::optional<NavigationSystem::RouteAnswer> a = rig.nav.takeRouteAnswer(ticket)) {
					 return a;
				 }
				 rig.nav.update(0.016F);
				 rig.nav.endTick();
			 }
			 return std::optional<NavigationSystem::RouteAnswer>{};
		};
		const std::optional<NavigationSystem::RouteAnswer> toA = answer(std::array{shoreA});
		ASSERT_TRUE(toA.has_value());
		EXPECT_EQ(toA->outcome, nav::RouteOutcome::NoKnownWay) << "a route got inside A's ring";
		const std::optional<NavigationSystem::RouteAnswer> toEither = answer(std::array{shoreA, shoreB});
		ASSERT_TRUE(toEither.has_value());
		ASSERT_EQ(toEither->outcome, nav::RouteOutcome::Routed);
		EXPECT_EQ(toEither->goalIndex, 1) << "routed to A";
	}
}

// --- Routes by ticket (requestRoute, takeRouteAnswer, cancelRoute) -------------------------------

namespace {

	using geometry::nav::SearchArgs;
	using geometry::nav::SearchState;
	using RouteAnswer = NavigationSystem::RouteAnswer;

	constexpr float kTick = 0.016F;

	// A field of trees 112 m square, a tree every 1.6 m, so a long search takes several slices.
	constexpr float		  kFieldLo		 = 8.0F;
	constexpr float		  kFieldPitch	 = 1.6F;
	constexpr int		  kFieldCount	 = 70;
	constexpr float		  kFieldTreeHalf = 0.25F;
	constexpr const char* kFieldTree	 = "Test_NavRouteFieldTree";

	// Between four trees, at least 1.1 m from each.
	glm::vec2 gapPoint(int i, int j) {
		return {kFieldLo + kFieldPitch * static_cast<float>(i), kFieldLo + kFieldPitch * static_cast<float>(j)};
	}
	glm::vec2 fieldTree(int i, int j) {
		return gapPoint(i, j) + glm::vec2{0.5F * kFieldPitch};
	}

	// A built wall across the field along y = 64 m, from x = 20.8 m to 112 m: points either side
	// of its middle are 3.2 m apart in a line and over 80 m apart on foot.
	const glm::vec2 kBarWest{20.8F, 64.0F};
	const glm::vec2 kBarEast{112.0F, 64.0F};
	const glm::vec2 kBarStart = gapPoint(35, 34);
	const glm::vec2 kBarGoal  = gapPoint(35, 36);
	// A 4 x 3 m room of built walls in the field's north-east, no opening; trees kept clear of it.
	const glm::vec2				 kRoomLo{96.0F, 96.0F};
	const glm::vec2				 kRoomHi{100.0F, 99.0F};
	const std::vector<glm::vec2> kInRoom{{97.0F, 97.5F}, {99.0F, 97.5F}};
	// Ground no field mover meshes: chunk (1, 0).
	const glm::vec2 kFarGround{700.0F, 64.0F};
	// Trees in cells no field mover wants.
	const glm::vec2 kLateTrees{300.0F, 300.0F};
	// A mover here meshing a 40 m square (meshAround) has kNearTrees in its own cell and kLateTrees
	// two and three cells off, away from it.
	const glm::vec2 kFarMover{344.0F, 303.0F};
	const glm::vec2 kNearTrees{340.0F, 300.0F};

	Vec2i64 mm(glm::vec2 meters) {
		return nav::toMm(meters);
	}

	struct RouteField {
		PlacementExecutor					executor{AssetRegistry::Get()};
		std::unordered_set<ChunkCoordinate> processed;
		ConstructionWorld					cw;
		World								world;
		NavigationSystem&					sys;
		SegmentId							roomSouth = 0;

		RouteField()
			: sys(world.registerSystem<NavigationSystem>()) {
			registerTree(kFieldTree, kFieldTreeHalf);
			std::vector<glm::vec2> trees;
			for (int j = 0; j < kFieldCount; ++j) {
				for (int i = 0; i < kFieldCount; ++i) {
					const glm::vec2 p = fieldTree(i, j);
					if (p.x > kRoomLo.x - 1.2F && p.x < kRoomHi.x + 1.2F && p.y > kRoomLo.y - 1.2F && p.y < kRoomHi.y + 1.2F) {
						continue;
					}
					trees.push_back(p);
				}
			}
			for (int k = 0; k < 5; ++k) {
				trees.push_back(kLateTrees + glm::vec2{2.0F * static_cast<float>(k), 0.0F});
			}
			trees.push_back(kNearTrees);
			trees.push_back(kNearTrees + glm::vec2{2.0F, 0.0F});
			placeTrees(executor, processed, kFieldTree, trees);
			buildWall(cw, mm(kBarWest), mm(kBarEast));
			roomSouth = buildWall(cw, mm(kRoomLo), mm({kRoomHi.x, kRoomLo.y}));
			buildWall(cw, mm({kRoomHi.x, kRoomLo.y}), mm(kRoomHi));
			buildWall(cw, mm(kRoomHi), mm({kRoomLo.x, kRoomHi.y}));
			buildWall(cw, mm({kRoomLo.x, kRoomHi.y}), mm(kRoomLo));
			sys.setChunkManager(landChunks());
			sys.setPlacementData(&executor, &processed);
			sys.setConstructionWorld(&cw);
			meshAround(world, {64.0F, 64.0F}, 64.0F);
			sys.finishPendingBuilds();
		}

		void tick() { world.update(kTick); }

		void fell(glm::vec2 tree) { ASSERT_TRUE(executor.removeEntity(world::worldToChunk({tree.x, tree.y}), tree, kFieldTree)); }

		// Ticks until the answer is takeable, counting them.
		std::optional<RouteAnswer> await(nav::RouteTicket ticket, int& ticks, int limit = 60) {
			for (int i = 0; i < limit; ++i) {
				tick();
				++ticks;
				if (std::optional<RouteAnswer> answer = sys.takeRouteAnswer(ticket)) {
					return answer;
				}
			}
			return std::nullopt;
		}
	};

	// The search the route queue runs for a request, run alone to its end on the mesh as it is now.
	SearchState searchAlone(const NavigationSystem& sys, glm::vec2 start, const std::vector<glm::vec2>& goals) {
		SearchArgs args{.start = mm(start), .agentRadiusMm = 300, .belief = geometry::nav::copyBelief({})};
		for (const glm::vec2 g : goals) {
			args.goals.push_back(mm(g));
		}
		SearchState search(std::move(args));
		search.step(sys.navMesh().view(), geometry::nav::kRunToEnd);
		return search;
	}

	std::vector<Vec2i64> pointsOf(const RouteAnswer& answer) {
		std::vector<Vec2i64> out;
		for (const glm::vec2 p : answer.waypoints) {
			out.push_back(mm(p));
		}
		return out;
	}

	bool wallAt(const NavigationSystem& sys, glm::vec2 meters) {
		const geometry::nav::PatchableMesh& mesh = sys.navMesh();
		const std::int32_t					tri	 = mesh.locate(mm(meters));
		return tri >= 0 && mesh.triangles()[static_cast<std::size_t>(tri)].faceBlocker > 0;
	}

	// What one answer said, without the epochs (process-wide counters, so two runs never share them).
	struct TakenAnswer {
		int					 tick	= 0;
		nav::RouteTicket	 ticket = 0;
		nav::RouteOutcome	 outcome{};
		std::int32_t		 goalIndex	= -1;
		std::vector<Vec2i64> points;
		std::uint32_t		 restarts	= 0;
		std::int64_t		 expansions = 0;
		bool				 operator==(const TakenAnswer&) const = default;
	};

	TakenAnswer taken(int tick, nav::RouteTicket ticket, const RouteAnswer& a) {
		return {tick, ticket, a.outcome, a.goalIndex, pointsOf(a), a.restarts, a.expansions};
	}

} // namespace

// A route by ticket is the walk a direct search of the mesh finds: through a door, from the start to
// the goal, the same length.
TEST_F(NavigationSystemTest, RouteByTicketIsTheDirectSearch) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	settle(sys);

	const geometry::nav::PathResult walked =
		geometry::nav::pathThrough(sys.navMesh().view(), mm(kOutside), mm(kInside), static_cast<std::int64_t>(std::llround(kAgentRadius * 1000.0F)), {});
	ASSERT_TRUE(walked.reachable);
	const nav::RouteTicket	   ticket = sys.requestRoute(kOutside, std::array{kInside}, kAgentRadius);
	std::optional<RouteAnswer> answer;
	for (int i = 0; i < 4 && !answer.has_value(); ++i) {
		world.update(kTick);
		answer = sys.takeRouteAnswer(ticket);
	}
	ASSERT_TRUE(answer.has_value());
	ASSERT_EQ(answer->outcome, nav::RouteOutcome::Routed);
	EXPECT_EQ(answer->goalIndex, 0);
	EXPECT_EQ(mm(answer->waypoints.front()), mm(kOutside));
	EXPECT_EQ(mm(answer->waypoints.back()), mm(kInside));
	EXPECT_NEAR(answer->lengthMeters, static_cast<float>(walked.cost / 1000.0), 0.01F);
	EXPECT_NEAR(answer->lengthMeters, lengthOf(answer->waypoints), 0.002F);
	EXPECT_EQ(answer->restarts, 0U);
}

// A short search requested between ticks is takeable after the second update, not before.
TEST_F(NavigationSystemTest, ShortRouteIsTakeableTwoTicksAfterTheRequest) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	settle(sys);

	const nav::RouteTicket ticket = sys.requestRoute({10.0F, 10.0F}, std::array{glm::vec2{40.0F, 25.0F}}, kAgentRadius);
	EXPECT_FALSE(sys.takeRouteAnswer(ticket).has_value()) << "nothing before the first slice";
	world.update(kTick);
	EXPECT_FALSE(sys.takeRouteAnswer(ticket).has_value()) << "searched in this tick's slice, delivered at the next update";
	world.update(kTick);
	const std::optional<RouteAnswer> answer = sys.takeRouteAnswer(ticket);
	ASSERT_TRUE(answer.has_value());
	EXPECT_EQ(answer->outcome, nav::RouteOutcome::Routed);
	EXPECT_FALSE(sys.takeRouteAnswer(ticket).has_value()) << "taken once";
	EXPECT_EQ(sys.routeStats().slices, 1U);
	EXPECT_EQ(sys.routeStats().answered, 1U);
}

// A placed obstacle blocks a route: it goes round, never through it. The funnel turns a corner on
// points a radius off it, so a leg between two of them may pass a little under a radius from the
// corner (at most by a radius times 1 - cos 45 degrees).
TEST_F(NavigationSystemTest, RouteByTicketGoesRoundAPlacedObstacle) {
	registerTree("Test_NavRouteRoundTree", 1.0F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						treeAt{20.0F, 20.0F};
	placeTrees(executor, processed, "Test_NavRouteRoundTree", {treeAt});
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setPlacementData(&executor, &processed);
	settle(sys);
	ASSERT_FALSE(meshClear(sys, treeAt));

	const glm::vec2		   from{15.0F, 20.0F};
	const glm::vec2		   to{25.0F, 20.0F};
	const nav::RouteTicket ticket = sys.requestRoute(from, std::array{to}, kAgentRadius);
	world.update(kTick);
	world.update(kTick);
	const std::optional<RouteAnswer> answer = sys.takeRouteAnswer(ticket);
	ASSERT_TRUE(answer.has_value());
	ASSERT_EQ(answer->outcome, nav::RouteOutcome::Routed);
	EXPECT_GT(answer->lengthMeters, 10.05F) << "bent round the obstacle";
	for (std::size_t i = 1; i < answer->waypoints.size(); ++i) {
		for (int k = 0; k <= 100; ++k) {
			const glm::vec2 p	  = glm::mix(answer->waypoints[i - 1], answer->waypoints[i], static_cast<float>(k) / 100.0F);
			const glm::vec2 apart = glm::max(glm::abs(p - treeAt) - glm::vec2{1.0F}, glm::vec2{0.0F});
			EXPECT_GE(glm::length(apart), kAgentRadius * 0.7F) << "the disc enters the obstacle at (" << p.x << ", " << p.y << ")";
		}
	}
}

// Of several goals the answer is the nearest on foot: one 1.5 m away in a line but round a room's
// walls to its door loses to one 4.5 m away in the open.
TEST_F(NavigationSystemTest, NearestOfRouteIsTheNearestOnFoot) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	settle(sys);

	const glm::vec2				 from{2.0F, 4.0F};
	const std::vector<glm::vec2> goals{{2.0F, 2.5F}, {6.5F, 4.0F}};
	const nav::RouteTicket		 ticket = sys.requestRoute(from, goals, kAgentRadius);
	world.update(kTick);
	world.update(kTick);
	const std::optional<RouteAnswer> answer = sys.takeRouteAnswer(ticket);
	ASSERT_TRUE(answer.has_value());
	ASSERT_EQ(answer->outcome, nav::RouteOutcome::Routed);
	EXPECT_EQ(answer->goalIndex, 1);
	EXPECT_EQ(mm(answer->waypoints.back()), mm(goals[1]));
}

// A search longer than a slice carries on over ticks and answers what it would run alone.
TEST_F(NavigationSystemTest, LongRouteSpansTicksAndAnswers) {
	RouteField						 field;
	const nav::RouteTicket			 ticket = field.sys.requestRoute(kBarStart, std::array{kBarGoal}, kAgentRadius);
	int								 ticks	= 0;
	const std::optional<RouteAnswer> answer = field.await(ticket, ticks);
	ASSERT_TRUE(answer.has_value());
	ASSERT_EQ(answer->outcome, nav::RouteOutcome::Routed);
	EXPECT_GT(answer->expansions, static_cast<std::int64_t>(nav::kRouteWorkPerSlice));
	EXPECT_GT(ticks, 2) << "more than one slice";
	EXPECT_GT(answer->lengthMeters, 80.0F) << "round the bar";
	EXPECT_EQ(answer->restarts, 0U);
	const SearchState alone = searchAlone(field.sys, kBarStart, {kBarGoal});
	EXPECT_EQ(pointsOf(*answer), alone.result().points);
	EXPECT_EQ(answer->expansions, alone.expansions());
}

// While a search spans ticks, only placed obstacles in cells away from every mover wait: a mover's
// own cell and the eight touching it, new ground, a felled obstacle, and a wall go in at the normal
// rate. Once it has answered, what waited goes in, all of it.
TEST_F(NavigationSystemTest, WhileARouteSpansTicksOnlyCellsAwayFromMoversWait) {
	RouteField			   field;
	NavigationSystem&	   sys	  = field.sys;
	const nav::RouteTicket ticket = sys.requestRoute(kBarStart, std::array{kBarGoal}, kAgentRadius);
	field.tick();
	ASSERT_EQ(sys.routeStats().ticksHeld, 0U) << "nothing has run across a tick yet";

	meshAround(field.world, kFarGround, 10.0F);
	meshAround(field.world, kFarMover, 40.0F);
	const glm::vec2 felled = fieldTree(30, 30);
	ASSERT_FALSE(meshClear(sys, felled));
	ASSERT_TRUE(meshClear(sys, kNearTrees));
	field.fell(felled);
	const glm::vec2 wallMid{132.0F, 60.0F};
	buildWall(field.cw, mm({130.0F, 60.0F}), mm({134.0F, 60.0F}));
	field.tick();
	ASSERT_EQ(sys.routeStats().ticksHeld, 1U) << "the search ran in the last slice and goes on";
	EXPECT_TRUE(meshClear(sys, felled)) << "the felled obstacle is out at once";
	EXPECT_TRUE(wallAt(sys, wallMid)) << "the wall is in at once";
	EXPECT_TRUE(sys.inSimArea(kFarGround)) << "new ground goes in";
	EXPECT_FALSE(meshClear(sys, kNearTrees)) << "the mover's own cell goes in";
	EXPECT_FALSE(meshClear(sys, kNearTrees + glm::vec2{2.0F, 0.0F})) << "the mover's own cell goes in";
	EXPECT_TRUE(meshClear(sys, kLateTrees)) << "cells away from movers wait";

	std::optional<RouteAnswer> answer;
	for (int ticks = 0; ticks < 60 && !answer.has_value(); ++ticks) {
		field.tick();
		answer = sys.takeRouteAnswer(ticket);
		if (!answer.has_value()) {
			EXPECT_TRUE(meshClear(sys, kLateTrees)) << "tick " << ticks;
		}
	}
	ASSERT_TRUE(answer.has_value());
	EXPECT_EQ(answer->outcome, nav::RouteOutcome::Routed);
	for (int i = 0; i < 60 && sys.isBuilding(); ++i) {
		field.tick();
	}
	EXPECT_FALSE(sys.isBuilding()) << "the held cells went in until none waited";
	for (int k = 0; k < 5; ++k) {
		EXPECT_FALSE(meshClear(sys, kLateTrees + glm::vec2{2.0F * static_cast<float>(k), 0.0F})) << "held obstacle " << k;
	}
}

// Each search holds the cells away from movers for kMaxHeldAdditionTicks of its life, from the first
// tick it is still running after a slice, and a restart does not count again. Long searches asked
// back to back over several ticks hold until the last one's window closes and not a tick longer,
// though every one of them still runs; then the held cells go in, tick after tick, until none wait.
TEST_F(NavigationSystemTest, EachSearchHoldsCellsAwayFromMoversForItsOwnWindowOnly) {
	RouteField		  field;
	NavigationSystem& sys = field.sys;
	// Floods of the field (the sealed room: no known way), eight before each of the first updates.
	// Eight first turns fit in a slice, so those asked before update u are running after its slice
	// and open their windows at update u + 1.
	constexpr int				  kBatches	= 8;
	constexpr int				  kPerBatch = 8;
	constexpr int				  kLastHeld = kBatches + 1 + static_cast<int>(NavigationSystem::kMaxHeldAdditionTicks) - 1;
	std::vector<nav::RouteTicket> floods;
	for (int update = 1; update <= kLastHeld + 1; ++update) {
		for (int k = 0; update <= kBatches && k < kPerBatch; ++k) {
			floods.push_back(sys.requestRoute(gapPoint(2, 2), kInRoom, kAgentRadius));
		}
		if (update == 3) {
			meshAround(field.world, kFarMover, 40.0F);
		}
		if (update == 20) {
			field.fell(fieldTree(2, 2)); // beside every flood's start: each restarts
		}
		field.tick();
		if (update >= 3 && update <= kLastHeld) {
			ASSERT_TRUE(meshClear(sys, kLateTrees)) << "update " << update;
		}
	}
	EXPECT_EQ(sys.routeStats().ticksHeld, static_cast<std::uint64_t>(kLastHeld - 1));
	EXPECT_EQ(sys.routeStats().longestHold, static_cast<std::uint32_t>(kLastHeld - 1));

	for (int i = 0; i < 60 && sys.isBuilding(); ++i) {
		field.tick();
	}
	EXPECT_FALSE(sys.isBuilding()) << "the held cells went in until none waited";
	for (int k = 0; k < 5; ++k) {
		EXPECT_FALSE(meshClear(sys, kLateTrees + glm::vec2{2.0F * static_cast<float>(k), 0.0F})) << "held obstacle " << k;
	}
	EXPECT_EQ(sys.routeStats().ticksHeld, static_cast<std::uint64_t>(kLastHeld - 1)) << "past their windows, running searches hold nothing";
	for (const nav::RouteTicket t : floods) {
		ASSERT_TRUE(sys.routeTicketLive(t));
		ASSERT_FALSE(sys.takeRouteAnswer(t).has_value()) << "every flood still runs";
	}

	std::size_t answered = 0;
	for (int i = 0; i < 200 && answered < floods.size(); ++i) {
		field.tick();
		for (const nav::RouteTicket t : floods) {
			if (const std::optional<RouteAnswer> answer = sys.takeRouteAnswer(t)) {
				++answered;
				EXPECT_EQ(answer->outcome, nav::RouteOutcome::NoKnownWay);
				EXPECT_GE(answer->restarts, 1U) << "the felled obstacle restarted it";
			}
		}
	}
	EXPECT_EQ(answered, floods.size());
	EXPECT_EQ(sys.routeStats().ticksHeld, static_cast<std::uint64_t>(kLastHeld - 1));
}

// A patch that changes a triangle a running search has read restarts it, and the answer is the
// one for the mesh as it now stands.
TEST_F(NavigationSystemTest, PatchUnderARunningRouteRestartsIt) {
	RouteField			   field;
	NavigationSystem&	   sys	  = field.sys;
	const nav::RouteTicket ticket = sys.requestRoute(kBarStart, std::array{kBarGoal}, kAgentRadius);
	field.tick();
	field.fell(fieldTree(34, 33));
	field.fell(fieldTree(35, 33));
	int								 ticks	= 1;
	const std::optional<RouteAnswer> answer = field.await(ticket, ticks);
	ASSERT_TRUE(answer.has_value());
	ASSERT_EQ(answer->outcome, nav::RouteOutcome::Routed);
	EXPECT_GE(answer->restarts, 1U);
	EXPECT_EQ(sys.routeStats().restarts, answer->restarts);
	const SearchState alone = searchAlone(sys, kBarStart, {kBarGoal});
	EXPECT_EQ(pointsOf(*answer), alone.result().points);
	EXPECT_EQ(answer->expansions, alone.expansions());
}

// A door that opens the sealed room while a search for the room runs is seen by it: the room is
// routed to, through the door.
TEST_F(NavigationSystemTest, DoorAddedWhileARouteRunsIsSeen) {
	RouteField			   field;
	NavigationSystem&	   sys	  = field.sys;
	const nav::RouteTicket ticket = sys.requestRoute(gapPoint(2, 2), kInRoom, kAgentRadius);
	field.tick();
	const OpeningId door = field.cw.addOpening(field.roomSouth, 0.5F, "Door", "Wood");
	ASSERT_NE(door, kInvalidOpening);
	ASSERT_TRUE(field.cw.setOpeningState(door, FoundationState::Built));
	int								 ticks	= 1;
	const std::optional<RouteAnswer> answer = field.await(ticket, ticks);
	ASSERT_TRUE(answer.has_value());
	ASSERT_EQ(answer->outcome, nav::RouteOutcome::Routed) << "the door is in the answer";
	EXPECT_GE(answer->restarts, 1U);
	const glm::vec2 end = answer->waypoints.back();
	EXPECT_TRUE(end.x > kRoomLo.x && end.x < kRoomHi.x && end.y > kRoomLo.y && end.y < kRoomHi.y);
}

// The same room with no door: the search explores all the start can reach and answers no known way.
TEST_F(NavigationSystemTest, SealedRoomRouteExploresAndAnswersNoKnownWay) {
	RouteField						 field;
	const nav::RouteTicket			 ticket = field.sys.requestRoute(gapPoint(2, 2), kInRoom, kAgentRadius);
	int								 ticks	= 0;
	const std::optional<RouteAnswer> answer = field.await(ticket, ticks);
	ASSERT_TRUE(answer.has_value());
	EXPECT_EQ(answer->outcome, nav::RouteOutcome::NoKnownWay);
	EXPECT_GT(answer->expansions, static_cast<std::int64_t>(nav::kRouteWorkPerSlice));
	EXPECT_EQ(answer->goalIndex, -1);
	EXPECT_TRUE(answer->waypoints.empty());
}

// A cancelled request is never answered, before or during its search, and its search stops.
TEST_F(NavigationSystemTest, CancelledRouteIsNeverAnswered) {
	RouteField		  field;
	NavigationSystem& sys = field.sys;

	const nav::RouteTicket before = sys.requestRoute(kBarStart, std::array{kBarGoal}, kAgentRadius);
	sys.cancelRoute(before);
	const nav::RouteTicket during = sys.requestRoute(kBarStart, std::array{kBarGoal}, kAgentRadius);
	field.tick();
	sys.cancelRoute(during);
	const std::uint64_t slices = sys.routeStats().slices;
	for (int i = 0; i < 10; ++i) {
		field.tick();
		EXPECT_FALSE(sys.takeRouteAnswer(before).has_value());
		EXPECT_FALSE(sys.takeRouteAnswer(during).has_value());
	}
	EXPECT_EQ(sys.routeStats().slices, slices + 1) << "no slice after the one the cancel found running";

	const nav::RouteTicket atOnce = sys.requestRoute(kUnmeshed, std::array{kBarGoal}, kAgentRadius);
	sys.cancelRoute(atOnce);
	EXPECT_FALSE(sys.takeRouteAnswer(atOnce).has_value());
	EXPECT_EQ(sys.routeStats().answered, 0U);
}

// The outstanding count is the tickets requested and not yet taken or cancelled, at once or searched.
TEST_F(NavigationSystemTest, OutstandingTicketsAreCounted) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	settle(sys);
	const nav::RouteTicket atOnce	= sys.requestRoute(kUnmeshed, std::array{kOutside}, kAgentRadius);
	const nav::RouteTicket searched = sys.requestRoute(kOutside, std::array{glm::vec2{30.0F, 20.0F}}, kAgentRadius);
	const nav::RouteTicket dropped	= sys.requestRoute(kOutside, std::array{glm::vec2{-30.0F, 20.0F}}, kAgentRadius);
	EXPECT_EQ(sys.routeStats().outstanding, 3U);
	ASSERT_TRUE(sys.takeRouteAnswer(atOnce).has_value());
	EXPECT_EQ(sys.routeStats().outstanding, 2U);
	sys.cancelRoute(dropped);
	EXPECT_EQ(sys.routeStats().outstanding, 1U);
	std::optional<RouteAnswer> answer;
	for (int i = 0; i < 10 && !answer.has_value(); ++i) {
		sys.update(kTick);
		sys.endTick();
		EXPECT_EQ(sys.routeStats().outstanding, 1U) << "an answer waiting to be taken is still out";
		answer = sys.takeRouteAnswer(searched);
	}
	ASSERT_TRUE(answer.has_value());
	EXPECT_EQ(sys.routeStats().outstanding, 0U);
}

// An answer nobody takes (its asker is gone) is dropped kUntakenAnswerTicks updates after it became
// takeable, not one sooner: answered at once or searched, nothing of it stays.
TEST_F(NavigationSystemTest, AnswerNotTakenWithinItsTicksIsDropped) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	settle(sys);
	const nav::RouteTicket atOnce	= sys.requestRoute(kUnmeshed, std::array{kOutside}, kAgentRadius);
	const nav::RouteTicket searched = sys.requestRoute(kOutside, std::array{glm::vec2{30.0F, 20.0F}}, kAgentRadius);
	const auto			   tick		= [&] {
		  sys.update(kTick);
		  sys.endTick();
	};
	for (std::uint64_t i = 1; i < NavigationSystem::kUntakenAnswerTicks; ++i) {
		tick();
	}
	EXPECT_EQ(sys.routeStats().outstanding, 2U) << "dropped before its ticks were up";
	EXPECT_EQ(sys.routeStats().dropped, 0U);
	EXPECT_TRUE(sys.routeTicketLive(atOnce));
	tick();
	EXPECT_EQ(sys.routeStats().outstanding, 1U) << "the answer given at once was kept past its ticks";
	EXPECT_EQ(sys.routeStats().dropped, 1U);
	EXPECT_FALSE(sys.routeTicketLive(atOnce)) << "dropped";
	EXPECT_TRUE(sys.routeTicketLive(searched));
	EXPECT_FALSE(sys.takeRouteAnswer(atOnce).has_value());
	for (int i = 0; i < 4; ++i) {
		tick();
	}
	EXPECT_EQ(sys.routeStats().outstanding, 0U) << "the searched answer was kept past its ticks";
	EXPECT_EQ(sys.routeStats().dropped, 2U);
	EXPECT_FALSE(sys.routeTicketLive(searched)) << "dropped";
	EXPECT_FALSE(sys.takeRouteAnswer(searched).has_value());
	EXPECT_EQ(sys.routeStats().answered, 0U);
}

// A start on no meshed ground has its own answer, at once, with no search.
TEST_F(NavigationSystemTest, RouteFromOffTheMeshSaysSo) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	settle(sys);
	const nav::RouteTicket			 ticket = sys.requestRoute(kUnmeshed, std::array{glm::vec2{10.0F, 10.0F}}, kAgentRadius);
	const std::optional<RouteAnswer> answer = sys.takeRouteAnswer(ticket);
	ASSERT_TRUE(answer.has_value()) << "answered at once";
	EXPECT_EQ(answer->outcome, nav::RouteOutcome::StartOffMesh);
	EXPECT_EQ(answer->expansions, 0);
	EXPECT_EQ(answer->patchEpoch, sys.navMesh().epoch());
}

// A goal in a pocket of meshed ground sealed by placed obstacles, or off meshed ground, is no known
// way at once: no search, no slice.
TEST_F(NavigationSystemTest, RouteToAnotherComponentIsNoKnownWayAtOnce) {
	registerTree("Test_NavPocketTree", 0.6F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						pocket{200.0F, 20.0F};
	std::vector<glm::vec2>				ring;
	for (int k = -3; k <= 3; ++k) {
		const auto d = static_cast<float>(k);
		ring.insert(ring.end(), {pocket + glm::vec2{d, -3.0F}, pocket + glm::vec2{d, 3.0F}});
		if (k > -3 && k < 3) {
			ring.insert(ring.end(), {pocket + glm::vec2{-3.0F, d}, pocket + glm::vec2{3.0F, d}});
		}
	}
	placeTrees(executor, processed, "Test_NavPocketTree", ring);
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setPlacementData(&executor, &processed);
	settle(sys);
	ASSERT_TRUE(meshClear(sys, pocket)) << "the pocket is meshed ground";

	for (const glm::vec2 goal : {pocket, kUnmeshed}) {
		const nav::RouteTicket			 ticket = sys.requestRoute({180.0F, 20.0F}, std::array{goal}, kAgentRadius);
		const std::optional<RouteAnswer> answer = sys.takeRouteAnswer(ticket);
		ASSERT_TRUE(answer.has_value()) << "answered at once";
		EXPECT_EQ(answer->outcome, nav::RouteOutcome::NoKnownWay);
		EXPECT_EQ(answer->expansions, 0);
		EXPECT_EQ(answer->labelEpoch, sys.navMesh().components().epoch());
	}
	world.update(kTick);
	world.update(kTick);
	EXPECT_EQ(sys.routeStats().slices, 0U);
	EXPECT_EQ(sys.routeStats().answeredAtOnce, 2U);
}

// The at-once label check stands each end where the search does: a goal on the edge of a placed
// obstacle, located in the obstacle's face, stands on the free face beside it and is routed to; a
// goal deep inside one is no known way at once.
TEST_F(NavigationSystemTest, GoalOnAPlacedObstaclesEdgeIsRoutedToOneInsideIsNot) {
	registerTree("Test_NavEdgeTree", 1.0F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						centre{200.0F, 20.0F};
	placeTrees(executor, processed, "Test_NavEdgeTree", {centre});
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setPlacementData(&executor, &processed);
	settle(sys);
	ASSERT_FALSE(meshClear(sys, centre)) << "the obstacle is in the mesh";

	PlacedEntity obstacle;
	obstacle.defName = "Test_NavEdgeTree";
	obstacle.position = centre;
	const std::optional<geometry::nav::NavInputPolygon> ring = nav::placedObstacleRing(obstacle, AssetRegistry::Get());
	ASSERT_TRUE(ring.has_value());
	const glm::vec2 start{190.0F, 20.0F};
	int				locatedInside = 0;
	for (std::size_t i = 0; i < ring->ring.size(); ++i) {
		const Vec2i64&	a	 = ring->ring[i];
		const Vec2i64&	b	 = ring->ring[(i + 1) % ring->ring.size()];
		const glm::vec2 goal = nav::toMeters(Vec2i64{(a.x + b.x) / 2, (a.y + b.y) / 2});
		if (meshClear(sys, goal)) {
			continue; // located on the free side already: nothing to stand it on
		}
		++locatedInside;
		const nav::RouteTicket			 ticket = sys.requestRoute(start, std::array{goal}, kAgentRadius);
		std::optional<RouteAnswer>		 answer = sys.takeRouteAnswer(ticket);
		EXPECT_FALSE(answer.has_value()) << "refused at once: (" << goal.x << ", " << goal.y << ")";
		for (int i = 0; i < 10 && !answer.has_value(); ++i) {
			world.update(kTick);
			answer = sys.takeRouteAnswer(ticket);
		}
		ASSERT_TRUE(answer.has_value());
		EXPECT_EQ(answer->outcome, nav::RouteOutcome::Routed) << "(" << goal.x << ", " << goal.y << ")";
	}
	ASSERT_GT(locatedInside, 0) << "no edge point located in the obstacle's face; the test checks nothing";

	const nav::RouteTicket			 ticket = sys.requestRoute(start, std::array{centre + glm::vec2{0.2F, 0.3F}}, kAgentRadius);
	const std::optional<RouteAnswer> answer = sys.takeRouteAnswer(ticket);
	ASSERT_TRUE(answer.has_value()) << "answered at once";
	EXPECT_EQ(answer->outcome, nav::RouteOutcome::NoKnownWay);
	EXPECT_EQ(answer->expansions, 0);
}

// routeTicketLive tells a ticket nav will still answer from one it never will: live while searched
// and while its answer waits, dead once taken or cancelled, and for a ticket never issued.
// (Dropped tickets: AnswerNotTakenWithinItsTicksIsDropped.)
TEST_F(NavigationSystemTest, DeadTicketIsToldFromOneNotReady) {
	RouteField		  field;
	NavigationSystem& sys = field.sys;
	EXPECT_FALSE(sys.routeTicketLive(nav::kNoRouteTicket));
	EXPECT_FALSE(sys.routeTicketLive(12345)) << "never issued";

	const nav::RouteTicket searched = sys.requestRoute(kBarStart, std::array{kBarGoal}, kAgentRadius);
	const nav::RouteTicket atOnce	= sys.requestRoute(kUnmeshed, std::array{kBarGoal}, kAgentRadius);
	const nav::RouteTicket cancelled = sys.requestRoute(kBarStart, std::array{kBarGoal}, kAgentRadius);
	EXPECT_TRUE(sys.routeTicketLive(atOnce)) << "its answer waits";
	field.tick();
	sys.cancelRoute(cancelled);
	EXPECT_FALSE(sys.routeTicketLive(cancelled));
	EXPECT_FALSE(sys.takeRouteAnswer(searched).has_value());
	EXPECT_TRUE(sys.routeTicketLive(searched)) << "not ready yet, still searched";
	int								 ticks	= 1;
	const std::optional<RouteAnswer> answer = field.await(searched, ticks);
	ASSERT_TRUE(answer.has_value());
	EXPECT_FALSE(sys.routeTicketLive(searched)) << "taken";
	ASSERT_TRUE(sys.takeRouteAnswer(atOnce).has_value());
	EXPECT_FALSE(sys.routeTicketLive(atOnce)) << "taken";
	EXPECT_FALSE(sys.routeTicketLive(cancelled));
}

namespace {

	// A scripted run on the route field: requests, a cancel, a felled tree, a wall, and a door on
	// fixed ticks; every answer with the tick it was taken on.
	std::vector<TakenAnswer> scriptedRouteRun() {
		RouteField					  field;
		NavigationSystem&			  sys = field.sys;
		std::vector<nav::RouteTicket> tickets;
		std::vector<TakenAnswer>	  answers;
		for (int t = 0; t < 40; ++t) {
			switch (t) {
				case 0:
					tickets.push_back(sys.requestRoute(kBarStart, std::array{kBarGoal}, kAgentRadius));
					tickets.push_back(sys.requestRoute(gapPoint(5, 5), std::array{gapPoint(12, 9)}, kAgentRadius));
					tickets.push_back(sys.requestRoute(gapPoint(2, 2), kInRoom, kAgentRadius));
					tickets.push_back(sys.requestRoute(gapPoint(60, 10), std::array{gapPoint(10, 60)}, kAgentRadius));
					break;
				case 1:
					sys.cancelRoute(tickets[3]);
					field.fell(fieldTree(33, 33));
					break;
				case 2: buildWall(field.cw, mm({40.0F, 30.0F}), mm({40.0F, 34.0F})); break;
				case 3: {
					const OpeningId door = field.cw.addOpening(field.roomSouth, 0.5F, "Door", "Wood");
					field.cw.setOpeningState(door, FoundationState::Built);
					tickets.push_back(sys.requestRoute(gapPoint(64, 64), std::array{gapPoint(3, 50), gapPoint(50, 3)}, kAgentRadius));
					break;
				}
				default: break;
			}
			field.tick();
			for (const nav::RouteTicket ticket : tickets) {
				if (std::optional<RouteAnswer> a = sys.takeRouteAnswer(ticket)) {
					answers.push_back(taken(t, ticket, *a));
				}
			}
		}
		return answers;
	}

} // namespace

// The same requests and world changes on the same ticks give the same answers on the same ticks,
// whatever the threads did.
TEST_F(NavigationSystemTest, SameRouteScriptSameAnswersOnTheSameTicks) {
	const std::vector<TakenAnswer> first  = scriptedRouteRun();
	const std::vector<TakenAnswer> second = scriptedRouteRun();
	EXPECT_EQ(first.size(), 4U) << "every request but the cancelled one answered";
	EXPECT_TRUE(first == second);
}

namespace {

	struct Lcg {
		std::uint32_t state;
		std::uint32_t next(std::uint32_t below) {
			state = state * 1664525U + 1013904223U;
			return (state >> 8U) % below;
		}
	};

	// Requests, cancels, felled trees, and walls landing every tick, then a drain until every request
	// still standing is answered. Each routed answer starts at its start and ends at its goal.
	std::vector<TakenAnswer> stressRouteRun() {
		RouteField		  field;
		NavigationSystem& sys = field.sys;
		Lcg				  rng{12345U};
		struct Asked {
			glm::vec2			   start;
			std::vector<glm::vec2> goals;
		};
		std::map<nav::RouteTicket, Asked> open;
		std::vector<TakenAnswer>		  answers;
		const auto						  takeAll = [&](int t) {
			   for (auto it = open.begin(); it != open.end();) {
				   std::optional<RouteAnswer> a = sys.takeRouteAnswer(it->first);
				   if (!a.has_value()) {
					   ++it;
					   continue;
				   }
				   if (a->outcome == nav::RouteOutcome::Routed) {
					   EXPECT_EQ(mm(a->waypoints.front()), mm(it->second.start));
					   EXPECT_EQ(mm(a->waypoints.back()), mm(it->second.goals[static_cast<std::size_t>(a->goalIndex)]));
				   }
				   answers.push_back(taken(t, it->first, *a));
				   it = open.erase(it);
			   }
		};
		for (int t = 0; t < 40; ++t) {
			for (int k = 0; k < 6; ++k) {
				Asked				asked{gapPoint(1 + static_cast<int>(rng.next(68)), 1 + static_cast<int>(rng.next(68))), {}};
				const std::uint32_t goals = 1 + rng.next(3);
				for (std::uint32_t g = 0; g < goals; ++g) {
					asked.goals.push_back(gapPoint(1 + static_cast<int>(rng.next(68)), 1 + static_cast<int>(rng.next(68))));
				}
				const nav::RouteTicket ticket = sys.requestRoute(asked.start, asked.goals, kAgentRadius);
				open.emplace(ticket, std::move(asked));
			}
			if (rng.next(2) == 0) {
				auto it = open.begin();
				std::advance(it, rng.next(static_cast<std::uint32_t>(open.size())));
				sys.cancelRoute(it->first);
				open.erase(it);
			}
			const glm::vec2 tree = fieldTree(static_cast<int>(rng.next(kFieldCount)), static_cast<int>(rng.next(kFieldCount)));
			if (!meshClear(sys, tree)) {
				field.fell(tree);
			}
			if (t % 5 == 4) {
				const glm::vec2 at = gapPoint(2 + static_cast<int>(rng.next(60)), 2 + static_cast<int>(rng.next(60)));
				buildWall(field.cw, mm(at), mm(at + glm::vec2{3.2F, 0.0F}));
			}
			field.tick();
			takeAll(t);
		}
		for (int t = 40; t < 400 && !open.empty(); ++t) {
			field.tick();
			takeAll(t);
		}
		EXPECT_TRUE(open.empty()) << open.size() << " requests never answered";
		return answers;
	}

} // namespace

// Many requests and patches every tick with the slice on its worker, run three times: the same
// answers on the same ticks each time. A data race would most likely show here as a difference or a
// crash; passing is not proof there is none.
TEST_F(NavigationSystemTest, RouteStressRunsRepeatExactly) {
	const std::vector<TakenAnswer> first = stressRouteRun();
	EXPECT_GT(first.size(), 150U);
	for (int run = 0; run < 2; ++run) {
		EXPECT_TRUE(stressRouteRun() == first) << "run " << run + 2;
	}
}

// --- The snap and collision, one rule ------------------------------------------------------------

// Nav's snap and the collision system run together on three movers: one on a route a tree lands on,
// one whose route ends inside a tree (nothing clear ahead), one with no route and a velocity whose
// tree lands on it. None ends a tick further back along its route or heading than it started it; the
// one with nothing clear ahead stays, is held (collision doesn't push it), and steps out, not behind
// its leg, once no new route has come.
TEST_F(ForwardSnapTest, SnapAndCollisionNeverTakeAMoverBack) {
	auto& collision = world.registerSystem<StaticRectCollisionSystem>();
	collision.setPlacementData(&executor);
	collision.setDetailSource(sys);
	const glm::vec2 onRouteTree{-10.0F, 20.0F}; // chunk (-1, 0), placed now
	const glm::vec2 movingTree{20.0F, -20.0F};	// chunk (0, -1), placed now
	const EntityID	onRoute	  = routedColonist(world, onRouteTree + glm::vec2{-0.3F, 0.0F}, {{-14.0F, 20.0F}, {-6.0F, 20.0F}});
	const EntityID	noneAhead = routedColonist(world, kTree + glm::vec2{-0.3F, 0.0F}, {{6.0F, 10.0F}, {9.9F, 10.0F}});
	const EntityID	moving	  = routedColonist(world, movingTree + glm::vec2{-0.3F, 0.0F}, {});
	world.addComponent<Velocity>(moving, Velocity{{2.0F, 0.0F}});
	placeTrees(executor, processed, "Test_NavSnapTree", {onRouteTree, movingTree});

	const std::array<EntityID, 3> movers{onRoute, noneAhead, moving};
	std::array<float, 3>		  was{};
	for (std::size_t k = 0; k < movers.size(); ++k) {
		was[k] = world.getComponent<Position>(movers[k])->value.x; // every route and heading runs +x
	}
	const glm::vec2 stuckAt = world.getComponent<Position>(noneAhead)->value;
	bool			held	= false;
	for (int tick = 0; tick < 6; ++tick) {
		step(world, *sys);
		held = held || sys->snapHolds(noneAhead);
		for (std::size_t k = 0; k < movers.size(); ++k) {
			const float x = world.getComponent<Position>(movers[k])->value.x;
			EXPECT_GE(x, was[k] - 1e-4F) << "mover " << k << " went back on tick " << tick;
			was[k] = x;
		}
	}
	ASSERT_FALSE(meshClear(*sys, onRouteTree)) << "the trees landed";
	ASSERT_FALSE(meshClear(*sys, movingTree));
	EXPECT_TRUE(held) << "the snap held the mover with nothing clear ahead";
	EXPECT_EQ(world.getComponent<NavPath>(noneAhead)->builtNavVersion, NavigationSystem::kStaleRoute);
	for (const EntityID mover : movers) {
		EXPECT_TRUE(meshClear(*sys, world.getComponent<Position>(mover)->value)) << "still inside a tree";
	}
	EXPECT_LT(glm::distance(world.getComponent<Position>(noneAhead)->value, stuckAt), 1.5F) << "the nearest spot not behind it";
}

// --- What a mover wants, and in what order ---------------------------------------------------------

// A mover's disc is taken again where it stands once it has moved: a mover that appeared at one
// corner of a cell and stands at the opposite one has every placed obstacle within R of where it
// stands meshed. Each corner of the cell in turn.
TEST_F(NavigationSystemTest, DiscCoversRWhereTheMoverStands) {
	registerTree("Test_NavCornerTree", 0.3F);
	const float					   r = kDefaultSightRadius + NavigationSystem::kSightBufferMeters;
	const glm::vec2				   lo{192.0F, 192.0F}; // the cell [192, 208) each way
	const std::array<glm::vec2, 4> corners{glm::vec2{lo.x + 0.01F, lo.y + 0.01F}, glm::vec2{lo.x + 15.99F, lo.y + 0.01F},
										   glm::vec2{lo.x + 15.99F, lo.y + 15.99F}, glm::vec2{lo.x + 0.01F, lo.y + 15.99F}};
	for (std::size_t k = 0; k < corners.size(); ++k) {
		const glm::vec2		   entered	= corners[k];
		const glm::vec2		   standing = corners[(k + 2) % corners.size()];
		std::vector<glm::vec2> trees; // round where it stands, 1 m inside R
		for (int i = 0; i < 32; ++i) {
			const float angle = 6.2831853F * static_cast<float>(i) / 32.0F;
			trees.push_back(standing + (r - 1.0F) * glm::vec2{std::cos(angle), std::sin(angle)});
		}
		PlacementExecutor					executor(AssetRegistry::Get());
		std::unordered_set<ChunkCoordinate> processed;
		placeTrees(executor, processed, "Test_NavCornerTree", trees);
		ConstructionWorld cw;
		World			  world;
		NavigationSystem& sys = world.registerSystem<NavigationSystem>();
		sys.setChunkManager(landChunks());
		sys.setPlacementData(&executor, &processed);
		sys.setConstructionWorld(&cw);
		const EntityID mover = world.createEntity(); // R = 46 m
		world.addComponent<Position>(mover, Position{entered});
		world.addComponent<AgentRadius>(mover, AgentRadius{});
		settle(sys);
		world.getComponent<Position>(mover)->value = standing;
		settle(sys);
		for (const glm::vec2 t : trees) {
			EXPECT_FALSE(meshClear(sys, t)) << "entered at corner " << k << ": the tree at (" << t.x << ", " << t.y << ")";
		}
	}
}

namespace {
	// One tree 4 m off the middle of each cell within `out` cells of the cell holding `at`, row by row.
	std::vector<glm::vec2> treePerCell(glm::vec2 at, int out) {
		const glm::vec2		   middle{std::floor(at.x / 16.0F) * 16.0F + 8.0F, std::floor(at.y / 16.0F) * 16.0F + 8.0F};
		std::vector<glm::vec2> trees;
		for (int dy = -out; dy <= out; ++dy) {
			for (int dx = -out; dx <= out; ++dx) {
				trees.push_back(middle + glm::vec2{16.0F * static_cast<float>(dx) - 4.0F, 16.0F * static_cast<float>(dy) - 4.0F});
			}
		}
		return trees;
	}

	// The tick each tree entered the nav mesh, ticking the world under its budgets; -1 for one that
	// hadn't by `ticks`.
	std::vector<int> landingTicks(World& world, NavigationSystem& sys, const std::vector<glm::vec2>& trees, int ticks) {
		std::vector<int> at(trees.size(), -1);
		for (int tick = 0; tick < ticks && std::count(at.begin(), at.end(), -1) > 0; ++tick) {
			step(world, sys);
			for (std::size_t k = 0; k < trees.size(); ++k) {
				if (at[k] < 0 && !meshClear(sys, trees[k])) {
					at[k] = tick;
				}
			}
		}
		return at;
	}

	// A plain mover (R = 46 m) appears at (200, 200) with a tree in every cell of the 7 x 7 block round
	// its own: the tick each lands, in treePerCell order (its own cell in the middle).
	std::vector<int> spawnedMoverLandings() {
		registerTree("Test_NavNearTree", 0.3F);
		const glm::vec2						at{200.0F, 200.0F};
		const std::vector<glm::vec2>		trees = treePerCell(at, 3);
		PlacementExecutor					executor(AssetRegistry::Get());
		std::unordered_set<ChunkCoordinate> processed;
		placeTrees(executor, processed, "Test_NavNearTree", trees);
		ConstructionWorld cw;
		World			  world;
		NavigationSystem& sys = world.registerSystem<NavigationSystem>();
		sys.setChunkManager(landChunks());
		sys.setPlacementData(&executor, &processed);
		sys.setConstructionWorld(&cw);
		const EntityID mover = world.createEntity();
		world.addComponent<Position>(mover, Position{at});
		world.addComponent<AgentRadius>(mover, AgentRadius{});
		return landingTicks(world, sys, trees, 200);
	}
} // namespace

// A mover that appears has its own cell meshed first, then the eight touching it, then the rest of
// its disc.
TEST_F(NavigationSystemTest, SpawnedMoversOwnCellIsServedFirst) {
	const std::vector<int> at	= spawnedMoverLandings();
	constexpr std::size_t  kOwn = 24; // the middle of 7 x 7
	// Of the 7 x 7, the four corners and the eight beside them lie past R (their nearest points 56.6
	// and 46.2 m away); the rest land.
	ASSERT_EQ(std::count(at.begin(), at.end(), -1), 12);
	int touching = 0;
	int rest	 = std::numeric_limits<int>::max();
	for (std::size_t k = 0; k < at.size(); ++k) {
		if (at[k] < 0) {
			continue;
		}
		EXPECT_LE(at[kOwn], at[k]) << "tree " << k << " before the mover's own cell";
		const int dx = static_cast<int>(k % 7) - 3;
		const int dy = static_cast<int>(k / 7) - 3;
		if (std::abs(dx) <= 1 && std::abs(dy) <= 1) {
			touching = std::max(touching, at[k]);
		} else {
			rest = std::min(rest, at[k]);
		}
	}
	EXPECT_LE(touching, rest) << "a far cell before one touching the mover's";
	EXPECT_LT(at[kOwn], rest);
}

// The same appearance twice lands every tree on the same tick.
TEST_F(NavigationSystemTest, NearestFirstOrderRepeatsExactly) {
	EXPECT_EQ(spawnedMoverLandings(), spawnedMoverLandings());
}

namespace {
	// Which of `trees` are in the mesh once a plain mover (R = 46 m) standing still at `at` settles.
	std::vector<bool> meshedRoundStillMover(glm::vec2 at, const std::vector<glm::vec2>& trees) {
		registerTree("Test_NavRoundTree", 0.3F);
		PlacementExecutor					executor(AssetRegistry::Get());
		std::unordered_set<ChunkCoordinate> processed;
		placeTrees(executor, processed, "Test_NavRoundTree", trees);
		ConstructionWorld cw;
		World			  world;
		NavigationSystem& sys = world.registerSystem<NavigationSystem>();
		sys.setChunkManager(landChunks());
		sys.setPlacementData(&executor, &processed);
		sys.setConstructionWorld(&cw);
		const EntityID mover = world.createEntity();
		world.addComponent<Position>(mover, Position{at});
		world.addComponent<AgentRadius>(mover, AgentRadius{});
		settle(sys);
		std::vector<bool> in;
		for (const glm::vec2 t : trees) {
			in.push_back(!meshClear(sys, t));
		}
		return in;
	}
} // namespace

// A still mover meshes a disc round where it stands: placed obstacles 40 m off along each axis and
// along each diagonal are in, and those 45 m east or west and 45 m north or south (64 m off) are not.
TEST_F(NavigationSystemTest, StillMoverMeshesADiscNotASquare) {
	const glm::vec2		   at{200.0F, 200.0F};
	const float			   d = 40.0F / std::sqrt(2.0F);
	std::vector<glm::vec2> within{at + glm::vec2{40.0F, 0.0F}, at + glm::vec2{-40.0F, 0.0F}, at + glm::vec2{0.0F, 40.0F}, at + glm::vec2{0.0F, -40.0F},
								  at + glm::vec2{d, d},		   at + glm::vec2{-d, d},		 at + glm::vec2{d, -d},		  at + glm::vec2{-d, -d}};
	const std::vector<glm::vec2> corners{at + glm::vec2{45.0F, 45.0F}, at + glm::vec2{-45.0F, 45.0F}, at + glm::vec2{45.0F, -45.0F},
										 at + glm::vec2{-45.0F, -45.0F}};
	std::vector<glm::vec2> trees = within;
	trees.insert(trees.end(), corners.begin(), corners.end());
	const std::vector<bool> in = meshedRoundStillMover(at, trees);
	for (std::size_t k = 0; k < trees.size(); ++k) {
		EXPECT_EQ(in[k], k < within.size()) << "the tree at (" << trees[k].x << ", " << trees[k].y << ")";
	}
}

// The cells meshed round a still mover taper: of the 7 x 7 block round its cell, the middle row is
// meshed end to end and the top and bottom rows only their middle three.
TEST_F(NavigationSystemTest, StillMoversMeshedCellsTaper) {
	const glm::vec2				 at{200.0F, 200.0F};
	const std::vector<glm::vec2> trees = treePerCell(at, 3);
	const std::vector<bool>		 in	   = meshedRoundStillMover(at, trees);
	auto						 row   = [&](int r) { return std::count(in.begin() + r * 7, in.begin() + r * 7 + 7, true); };
	EXPECT_EQ(row(0), 3);
	EXPECT_EQ(row(3), 7);
	EXPECT_EQ(row(6), 3);
}

// A mover walking at 2 m/s through ground thick with placed obstacles, under the default budgets: each
// one ahead of him is in the mesh before he comes within his sight (30 m) of it, every one within R
// minus kDiscRetakeStepMm of his path is in, and none farther from it than R plus a cell's diagonal
// plus kDiscRetakeStepMm.
TEST_F(NavigationSystemTest, WalkingMoverMeshesAheadOfHisSight) {
	registerTree("Test_NavWalkTree", 0.3F);
	const float						kStep	  = static_cast<float>(NavigationSystem::kDiscRetakeStepMm) / 1000.0F;
	const float						kR		  = kDefaultSightRadius + NavigationSystem::kSightBufferMeters;
	const float						kFarthest = kR + 16.0F * std::sqrt(2.0F) + kStep;
	const glm::vec2					from{100.0F, 200.0F};
	const glm::vec2					to{260.0F, 200.0F};
	constexpr float					kSpeed = 2.0F;
	constexpr float					kTick  = 0.016F;
	std::vector<glm::vec2>			trees;
	for (int i = 0; i <= 50; ++i) {
		for (const float off : {4.0F, 12.0F, 28.0F, 44.0F, 60.0F, 76.0F}) {
			trees.push_back({8.0F * static_cast<float>(i), from.y + off});
			trees.push_back({8.0F * static_cast<float>(i), from.y - off});
		}
	}
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	placeTrees(executor, processed, "Test_NavWalkTree", trees);
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	const EntityID mover = addColonist(world, from, "Walker");
	settle(sys);

	std::vector<bool> landed(trees.size(), false);
	for (std::size_t k = 0; k < trees.size(); ++k) {
		landed[k] = !meshClear(sys, trees[k]);
	}
	glm::vec2 p = from;
	for (int tick = 0; p.x < to.x; ++tick) {
		p.x = std::min(to.x, p.x + kSpeed * kTick);
		world.getComponent<Position>(mover)->value = p;
		world.update(kTick);
		for (std::size_t k = 0; k < trees.size(); ++k) {
			if (landed[k] || glm::distance(p, trees[k]) > kR) {
				continue;
			}
			landed[k] = !meshClear(sys, trees[k]);
			ASSERT_TRUE(landed[k] || glm::distance(p, trees[k]) > kDefaultSightRadius)
				<< "the tree at (" << trees[k].x << ", " << trees[k].y << ") is in sight at x = " << p.x << " and not meshed";
		}
	}
	settle(sys);
	for (std::size_t k = 0; k < trees.size(); ++k) {
		const glm::vec2 t		 = trees[k];
		const float		fromPath = glm::distance(t, glm::vec2{std::clamp(t.x, from.x, to.x), from.y});
		if (fromPath <= kR - kStep) {
			EXPECT_FALSE(meshClear(sys, t)) << "the tree at (" << t.x << ", " << t.y << ") within R of the path";
		}
		if (fromPath > kFarthest) {
			EXPECT_TRUE(meshClear(sys, t)) << "the tree at (" << t.x << ", " << t.y << ") " << fromPath << " m from the path";
		}
	}
}

// An ordinary mover appears a tick after one with a very large R, inside its disc: the ordinary
// mover's own cell and those touching it are meshed within a few ticks, not behind the large disc's
// thousands of cells queued before them.
TEST_F(NavigationSystemTest, LargeDiscDoesNotDelayANearbyMover) {
	registerTree("Test_NavCrowdTree", 0.3F);
	const glm::vec2						at{350.0F, 350.0F}; // 495 m out, behind some 700 of the large disc's cells in chunk (0, 0)
	const std::vector<glm::vec2>		trees = treePerCell(at, 1);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	placeTrees(executor, processed, "Test_NavCrowdTree", trees);
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	const EntityID large = world.createEntity();
	world.addComponent<Position>(large, Position{{0.0F, 0.0F}});
	world.addComponent<AgentRadius>(large, AgentRadius{});
	Memory memory;
	memory.sightRadius = 500.0F; // R = 516 m, about 3,300 cells
	world.addComponent<Memory>(large, std::move(memory));
	step(world, sys); // the large disc is queued first
	const EntityID ordinary = world.createEntity();
	world.addComponent<Position>(ordinary, Position{at});
	world.addComponent<AgentRadius>(ordinary, AgentRadius{});

	const std::vector<int> landed = landingTicks(world, sys, trees, 40);
	for (std::size_t k = 0; k < trees.size(); ++k) {
		EXPECT_GE(landed[k], 0) << "tree " << k << " waited behind the large disc";
		EXPECT_LE(landed[k], 12) << "tree " << k;
	}
}

// --- Connectivity (connected, connectivityEpoch) and position checks on the real mesh -------------

namespace {
	// One river along y = 256 m, 6 m wide, across every chunk: its banks meet nowhere.
	class RiverAcrossSampler : public world::IWorldSampler {
	  public:
		[[nodiscard]] world::ChunkSampleResult sampleChunk(world::ChunkCoordinate) const override {
			world::ChunkSampleResult r = world::makeUniformChunkSampleResult(world::BiomeWeights::single(world::Biome::TemperateGrassland), 1.0F);
			r.riverSegments.push_back({.x0 = -4096.0, .y0 = 256.0, .x1 = 4096.0, .y1 = 256.0, .halfWidth0 = 3.0F, .halfWidth1 = 3.0F, .s0 = 0.0, .s1 = 8192.0});
			return r;
		}
		[[nodiscard]] float	   sampleElevation(world::WorldPosition) const override { return 1.0F; }
		[[nodiscard]] uint64_t getWorldSeed() const override { return kWorldSeed; }
	};

	world::ChunkManager* riverAcrossChunks() {
		static const std::unique_ptr<world::ChunkManager> chunks = readyChunks(std::make_unique<RiverAcrossSampler>(), {256.0F, 256.0F}, 0);
		return chunks.get();
	}

	// Trees on the sides of a square 6 m across round `centre`, one a metre; `gapAt` leaves out the
	// east side's tree that many metres north of the centre (none when it is outside -2..2).
	std::vector<glm::vec2> treeSquare(glm::vec2 centre, int gapAt = 99) {
		std::vector<glm::vec2> trees;
		for (int k = -3; k <= 3; ++k) {
			const auto d = static_cast<float>(k);
			trees.insert(trees.end(), {centre + glm::vec2{d, -3.0F}, centre + glm::vec2{d, 3.0F}});
			if (k > -3 && k < 3) {
				trees.push_back(centre + glm::vec2{-3.0F, d});
				if (k != gapAt) {
					trees.push_back(centre + glm::vec2{3.0F, d});
				}
			}
		}
		return trees;
	}
} // namespace

// connected is "same label on meshed ground": true across open ground; false across water no meshed
// ground crosses, into a pocket placed obstacles seal for zero width, into a placed obstacle, and to
// a point off meshed ground.
TEST_F(NavigationSystemTest, ConnectedIsTheSameLabelOnMeshedGround) {
	registerTree("Test_NavConnectedTree", 0.6F);
	const glm::vec2 pocket{100.0F, 100.0F};
	WalkRig			rig(pocket);
	const std::vector<glm::vec2> trees = treeSquare(pocket);
	placeTrees(rig.executor, rig.processed, "Test_NavConnectedTree", trees);
	settle(rig.nav);
	ASSERT_TRUE(rig.nav.isValidPosition(pocket));
	ASSERT_FALSE(rig.nav.isValidPosition(trees.front())) << "the square is in the mesh";
	const glm::vec2 open{80.0F, 100.0F};
	EXPECT_TRUE(rig.nav.connected(open, {120.0F, 90.0F}));
	EXPECT_TRUE(rig.nav.connected(open, {400.0F, 400.0F})) << "the whole chunk's ground is meshed";
	EXPECT_FALSE(rig.nav.connected(open, pocket)) << "the square seals the pocket";
	EXPECT_FALSE(rig.nav.connected(pocket, open));
	EXPECT_TRUE(rig.nav.connected(pocket, pocket + glm::vec2{1.0F, 1.0F}));
	EXPECT_FALSE(rig.nav.connected(open, trees.front())) << "a point in a placed obstacle has no label";
	EXPECT_FALSE(rig.nav.connected(open, kUnmeshed));
	EXPECT_FALSE(rig.nav.connected(kUnmeshed, open));

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(riverAcrossChunks());
	meshAround(world, {100.0F, 256.0F}, 30.0F);
	settle(sys);
	const glm::vec2 south{100.0F, 250.0F};
	const glm::vec2 north{100.0F, 262.0F};
	ASSERT_TRUE(sys.isValidPosition(south));
	ASSERT_TRUE(sys.isValidPosition(north));
	ASSERT_FALSE(sys.isValidPosition({100.0F, 256.0F})) << "the river is in";
	EXPECT_FALSE(sys.connected(south, north)) << "no meshed ground crosses the river";
	EXPECT_TRUE(sys.connected(south, {300.0F, 100.0F}));
}

// The pair that defines the contract: through a gap narrower than a colonist, connected is true (zero
// width passes) and a colonist's route is no known way; a mover narrower than the gap routes.
TEST_F(NavigationSystemTest, GapNarrowerThanAColonistIsConnectedButHasNoRoute) {
	registerTree("Test_NavGapTree", 0.8F); // a metre apart, 0.4 m between the two beside the gap
	const glm::vec2 pocket{100.0F, 100.0F};
	WalkRig			rig(pocket);
	placeTrees(rig.executor, rig.processed, "Test_NavGapTree", treeSquare(pocket, 0));
	settle(rig.nav);
	const glm::vec2 outside{110.0F, 100.0F};
	ASSERT_TRUE(rig.nav.isValidPosition(pocket));
	ASSERT_TRUE(rig.nav.isValidPosition(pocket + glm::vec2{3.0F, 0.0F})) << "the gap is open ground";
	ASSERT_FALSE(rig.nav.isValidPosition(pocket + glm::vec2{3.0F, 0.3F})) << "0.4 m wide";
	EXPECT_TRUE(rig.nav.connected(outside, pocket));
	EXPECT_FALSE(routeBetween(rig.nav, outside, pocket).has_value()) << "a colonist got through the gap";
	EXPECT_TRUE(routeBetween(rig.nav, outside, pocket, {}, 0.1F).has_value()) << "a mover 0.2 m wide fits it";
}

// Two patches of mesh that do not touch are not connected; on the tick the ground between lands they
// are, and the epoch moves on that tick.
TEST_F(NavigationSystemTest, ConnectedJoinsOnTheTickTheGapIsMeshed) {
	auto chunks = std::make_unique<world::ChunkManager>(landSampler());
	chunks->setLoadRadius(0);
	chunks->setUnloadRadius(8);
	const glm::vec2 camera{256.0F, 256.0F};

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	sys.setConstructionWorld(&cw);
	const glm::vec2 west{300.0F, 256.0F};
	const glm::vec2 east{1300.0F, 256.0F};
	const glm::vec2 between{768.0F, 256.0F};
	const EntityID	walker = meshAround(world, west, 30.0F); // holds and wants (0, 0) only
	meshAround(world, east, 30.0F);							 // and (2, 0)
	loadFor(*chunks, sys, camera);
	settle(sys);
	ASSERT_TRUE(sys.inSimArea(west));
	ASSERT_TRUE(sys.inSimArea(east));
	ASSERT_FALSE(sys.inSimArea(between));
	EXPECT_FALSE(sys.connected(west, east));

	world.getComponent<Position>(walker)->value = between; // wants (1, 0)
	std::uint64_t epoch	 = sys.connectivityEpoch();
	bool		  joined = false;
	for (int tick = 0; tick < 20 && !joined; ++tick) {
		step(world, sys);
		loadFor(*chunks, sys, camera);
		joined = sys.connected(west, east);
		EXPECT_EQ(joined, sys.inSimArea(between)) << "tick " << tick;
		if (joined) {
			EXPECT_NE(sys.connectivityEpoch(), epoch) << "the epoch stayed on the tick the ground joined";
		}
		epoch = sys.connectivityEpoch();
	}
	EXPECT_TRUE(joined);
}

// A placed obstacle landing in open ground only closes ground: the epoch stays.
TEST_F(NavigationSystemTest, ObstacleLandingInOpenGroundLeavesTheEpoch) {
	registerTree("Test_NavEpochTree", 0.4F);
	const glm::vec2 treeAt{10.0F, 10.0F};
	WalkRig			rig(treeAt);
	settle(rig.nav);
	ASSERT_TRUE(rig.nav.isValidPosition(treeAt));
	const std::uint64_t before = rig.nav.connectivityEpoch();
	placeTrees(rig.executor, rig.processed, "Test_NavEpochTree", {treeAt});
	settle(rig.nav);
	ASSERT_FALSE(rig.nav.isValidPosition(treeAt)) << "the tree is in";
	EXPECT_EQ(rig.nav.connectivityEpoch(), before);
}

// Every position check answers from the mesh after a chunk and its placement data unload: the obstacle
// still blocks, the stand point is beside it and standable, and building reads the ground beneath.
TEST_F(NavigationSystemTest, PositionChecksHoldAfterTheChunkUnloads) {
	registerTree("Test_NavUnloadedTree", 0.4F);
	auto chunks = std::make_unique<world::ChunkManager>(landSampler());
	chunks->setLoadRadius(0);
	chunks->setUnloadRadius(0);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						treeAt{500.0F, 256.0F};
	placeTrees(executor, processed, "Test_NavUnloadedTree", {treeAt});

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	const EntityID mover = addColonist(world, {490.0F, 256.0F}, "Walker");
	loadFor(*chunks, sys, {256.0F, 256.0F});
	settle(sys);
	ASSERT_FALSE(sys.isValidPosition(treeAt)) << "the tree is in";

	world.getComponent<Position>(mover)->value = {900.0F, 256.0F};
	loadFor(*chunks, sys, {900.0F, 256.0F});
	ASSERT_EQ(chunks->getChunk({0, 0}), nullptr);
	executor.unloadChunk({0, 0});
	processed.erase(ChunkCoordinate{0, 0});
	settle(sys);

	EXPECT_FALSE(sys.isValidPosition(treeAt)) << "the obstacle still blocks";
	const glm::vec2				   from{480.0F, 256.0F};
	const std::optional<glm::vec2> stand = sys.standPointBeside(treeAt, from, kAgentRadius);
	ASSERT_TRUE(stand.has_value());
	EXPECT_TRUE(sys.isValidPosition(*stand));
	EXPECT_GT(glm::distance(*stand, treeAt), 0.4F * 1.414F) << "outside the obstacle";
	EXPECT_LT(glm::distance(*stand, treeAt), 1.2F) << "beside it";
	EXPECT_LT(stand->x, treeAt.x) << "on the side it is asked from";
	EXPECT_TRUE(sys.connected(*stand, from)) << "asked from where a mover would stand";

	const std::optional<glm::vec2> nearest = sys.nearestPathablePoint(treeAt);
	ASSERT_TRUE(nearest.has_value());
	EXPECT_TRUE(sys.isValidPosition(*nearest));
	EXPECT_LT(glm::distance(*nearest, treeAt), 4.0F);
	const std::optional<glm::vec2> placed = sys.findValidPositionNear(treeAt, 0.0F);
	ASSERT_TRUE(placed.has_value());
	EXPECT_TRUE(sys.isValidPosition(*placed));
	EXPECT_TRUE(sys.isPointBuildable(treeAt)) << "a placed obstacle is cleared to build, not built round";
}

// Building and the landing read the ground beneath a placed obstacle: buildable on land, and water
// still where the obstacle stands in water.
TEST_F(NavigationSystemTest, BuildingReadsTheGroundBeneathAPlacedObstacle) {
	registerTree("Test_NavWaterTree", 0.4F);
	const glm::vec2 inWater{40.0F, 40.0F};
	const glm::vec2 onLand{20.0F, 40.0F};
	WalkRig			rig(inWater, waterPatchChunks());
	placeTrees(rig.executor, rig.processed, "Test_NavWaterTree", {inWater, onLand});
	settle(rig.nav);
	ASSERT_FALSE(rig.nav.isValidPosition(onLand)) << "the tree is in";
	const geometry::nav::PatchableMesh& mesh = rig.nav.navMesh();
	ASSERT_EQ(mesh.triangles()[static_cast<std::size_t>(mesh.locate(nav::toMm(inWater)))].faceBlocker, nav::kProvenancePlaced)
		<< "the tree's face, not the water's, is on top";
	EXPECT_TRUE(rig.nav.isPointBuildable(onLand));
	EXPECT_EQ(rig.nav.nearestTerrainWalkablePoint(onLand), std::optional<glm::vec2>(onLand));
	EXPECT_FALSE(rig.nav.isPointBuildable(inWater)) << "water under a placed obstacle is still water";
	EXPECT_NE(rig.nav.nearestTerrainWalkablePoint(inWater), std::optional<glm::vec2>(inWater));
}

// A target ringed by placed obstacles has nowhere to stand beside it: standPointBeside says so rather
// than offer a point inside a neighbour. A lone one has a stand point.
TEST_F(NavigationSystemTest, StandPointBesideARingedTargetIsNone) {
	registerTree("Test_NavRingedTarget", 0.3F);
	const glm::vec2		   target{20.0F, 20.0F};
	const glm::vec2		   lone{30.0F, 20.0F};
	WalkRig				   rig(target);
	std::vector<glm::vec2> trees = treeRing(target, 0.9F);
	trees.insert(trees.end(), {target, lone});
	placeTrees(rig.executor, rig.processed, "Test_NavRingedTarget", trees);
	settle(rig.nav);
	ASSERT_FALSE(rig.nav.isValidPosition(target)) << "the target is in";
	EXPECT_FALSE(rig.nav.standPointBeside(target, {10.0F, 20.0F}, kAgentRadius).has_value());
	const std::optional<glm::vec2> beside = rig.nav.standPointBeside(lone, {10.0F, 20.0F}, kAgentRadius);
	ASSERT_TRUE(beside.has_value());
	EXPECT_TRUE(rig.nav.isValidPosition(*beside));
}

// Where no ground is meshed nothing is valid, before any mesh and off the meshed ground after.
TEST_F(NavigationSystemTest, NoMeshedGroundIsNeverValid) {
	WalkRig						 rig({10.0F, 10.0F});
	const std::vector<glm::vec2> square{kUnmeshed, kUnmeshed + glm::vec2{1.0F, 0.0F}, kUnmeshed + glm::vec2{1.0F, 1.0F}, kUnmeshed + glm::vec2{0.0F, 1.0F}};
	const auto					 nothingValid = [&](const NavigationSystem& nav) {
		  EXPECT_FALSE(nav.isValidPosition(kUnmeshed));
		  EXPECT_FALSE(nav.isPointBuildable(kUnmeshed));
		  EXPECT_FALSE(nav.isAreaBuildable(square));
		  EXPECT_FALSE(nav.standPointBeside(kUnmeshed, kUnmeshed, kAgentRadius).has_value());
		  EXPECT_FALSE(nav.nearestPathablePoint(kUnmeshed).has_value());
		  EXPECT_FALSE(nav.nearestPathablePoint(kUnmeshed, kAgentRadius).has_value());
		  EXPECT_FALSE(nav.nearestTerrainWalkablePoint(kUnmeshed).has_value());
		  EXPECT_FALSE(nav.findValidPositionNear(kUnmeshed).has_value());
		  EXPECT_FALSE(nav.connected(kUnmeshed, kUnmeshed));
	};
	nothingValid(rig.nav);
	settle(rig.nav);
	ASSERT_TRUE(rig.nav.isValidPosition({10.0F, 10.0F}));
	nothingValid(rig.nav);
}
