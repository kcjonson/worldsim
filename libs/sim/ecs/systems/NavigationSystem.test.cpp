#include "NavigationSystem.h"

#include "AIDecisionSystem.h"
#include "ActionSystem.h"
#include "MovementSystem.h"
#include "PhysicsSystem.h"
#include "MeshBand.test.h"
#include "PlacedNothing.test.h"
#include "StaticRectCollisionSystem.h"
#include "TimeSystem.h"
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

#include <nav/LimitPolygon.h>
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
#include <numbers>
#include <optional>
#include <set>
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

	// A placed obstacle stands at `meters` in the mesh: on meshed ground, and not clear there. Off the
	// mesh reads false, so ground nobody has seen never reads as holding one.
	bool obstacleMeshed(const NavigationSystem& sys, glm::vec2 meters) { return sys.inSimArea(meters) && !meshClear(sys, meters); }

	// What a mover at `at` sees, the disc of `sight` round him, lies in meshed ground: where he stands
	// and his sight's edge, sampled at four times a limit's sides.
	::testing::AssertionResult sightMeshed(const NavigationSystem& sys, glm::vec2 at, float sight) {
		constexpr std::size_t kSamples = 4 * geometry::nav::kLimitSides;
		if (!sys.inSimArea(at)) {
			return ::testing::AssertionFailure() << "he stands off the mesh at (" << at.x << ", " << at.y << ")";
		}
		for (std::size_t k = 0; k < kSamples; ++k) {
			const float		angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(k) / static_cast<float>(kSamples);
			const glm::vec2 seen  = at + sight * glm::vec2{std::cos(angle), std::sin(angle)};
			if (!sys.inSimArea(seen)) {
				return ::testing::AssertionFailure() << "from (" << at.x << ", " << at.y << ") he sees (" << seen.x << ", " << seen.y << "), off the mesh";
			}
		}
		return ::testing::AssertionSuccess();
	}

	// A built wall stands at `meters` in the mesh.
	bool wallAt(const NavigationSystem& sys, glm::vec2 meters) {
		const geometry::nav::PatchableMesh& mesh = sys.navMesh();
		const std::int32_t					tri	 = mesh.locate(nav::toMm(meters));
		return tri >= 0 && mesh.triangles()[static_cast<std::size_t>(tri)].faceBlocker > 0;
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
	// A colonist's R: his limit holds the disc of this round where he stands.
	constexpr float kColonistRange = kDefaultSightRadius + NavigationSystem::kSightBufferMeters;

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
	// marks those chunks processed; every other fixture chunk with no index yet is placed with
	// nothing (placeNothingElsewhere).
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
		placeNothingElsewhere(executor, &processed);
	}

	// A still mover at `center` whose limit holds the disc of `rangeMeters` round it, so the ground and
	// placed obstacles there are meshed. It is not a colonist: nothing but nav acts on it.
	EntityID meshDisc(World& world, glm::vec2 center, float rangeMeters) {
		const EntityID e = world.createEntity();
		world.addComponent<Position>(e, Position{center});
		world.addComponent<AgentRadius>(e, AgentRadius{});
		Memory memory;
		memory.sightRadius = std::max(0.0F, rangeMeters - NavigationSystem::kSightBufferMeters);
		world.addComponent<Memory>(e, std::move(memory));
		return e;
	}

	// A still mover whose limit holds the square of `halfExtentMeters` round `center`.
	EntityID meshAround(World& world, glm::vec2 center, float halfExtentMeters) { return meshDisc(world, center, halfExtentMeters * 1.4143F); }

	// The shared land chunks span [-1024, 1536) m on each axis: their middle, and a range whose
	// limit's obstacle query (its circumradius plus kFootprintReachMeters) stays inside them.
	constexpr glm::vec2 kAreaCentre{256.0F, 256.0F};
	constexpr float		kAreaRangeMeters = 1240.0F;

	class NavigationSystemTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			ConstructionRegistry::Get().clear();
			ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder()));
		}
		void TearDown() override { ConstructionRegistry::Get().clear(); }

		// The shared land chunks, nearly all of them meshed by one still mover in their middle, whose
		// limit and its obstacle query stay inside them.
		static void wireArea(World& world, NavigationSystem& sys) {
			sys.setChunkManager(landChunks());
			meshDisc(world, kAreaCentre, kAreaRangeMeters);
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

// A mover meshes the limit holding the disc of R (sight plus kSightBufferMeters) round where it
// stands, so a longer sight distance meshes a larger disc. From x = 8, R = 46 m holds a tree 44 m
// off and not one 60 m off; a sight of 70 m (R = 86 m) holds one 80 m off and not one 92 m off.
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
			in.push_back(obstacleMeshed(sys, t));
		}
		return in;
	};
	EXPECT_EQ(meshedWithSight(kDefaultSightRadius), (std::vector<bool>{true, false, false, false}));
	EXPECT_EQ(meshedWithSight(70.0F), (std::vector<bool>{true, true, true, false}));
}

// Flora enters at most kDetailRingsPerTick rings a tick, hanging over unseen ground, and the limit's
// ground goes in with the last of them: never ahead of its obstacles.
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
		return static_cast<std::size_t>(std::count_if(trees.begin(), trees.end(), [&](glm::vec2 t) { return obstacleMeshed(sys, t); }));
	};
	const std::size_t ticks = (trees.size() + NavigationSystem::kDetailRingsPerTick - 1) / NavigationSystem::kDetailRingsPerTick;
	ASSERT_GT(ticks, 1u);
	for (std::size_t tick = 1; tick < ticks; ++tick) {
		step(world, sys);
		EXPECT_FALSE(sys.inSimArea({15.0F, 15.0F})) << "tick " << tick << ": the ground waits for its obstacles";
		EXPECT_TRUE(sys.isBuilding());
	}
	step(world, sys);
	EXPECT_TRUE(sys.inSimArea({15.0F, 15.0F}));
	EXPECT_EQ(inMesh(), trees.size()) << "the ground lands with every obstacle on it";

	settle(sys);
	EXPECT_FALSE(sys.isBuilding());
	EXPECT_EQ(inMesh(), trees.size());
}

// Placement finishing late never leaves seen ground without its obstacles: a limit whose chunks have
// no placement yet does not land, and once placement arrives it lands with every obstacle of its
// disc in the mesh.
TEST_F(NavigationSystemTest, LatePlacementBringsItsFloraIn) {
	registerTree("Test_NavLatePlaceTree", 0.4F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	addColonist(world, {0.0F, 0.0F}, "Lookout");
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	const glm::vec2 treeAt{10.0F, 10.0F};
	EXPECT_FALSE(sys.hasMesh()) << "no ground before its placement";
	EXPECT_FALSE(sys.inSimArea(treeAt));

	placeTrees(executor, processed, "Test_NavLatePlaceTree", {treeAt});
	settle(sys);
	EXPECT_TRUE(sys.inSimArea(treeAt + glm::vec2{2.0F, 0.0F}));
	EXPECT_TRUE(obstacleMeshed(sys, treeAt)) << "the late tree landed with its ground";
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

// A flora patch (a tree felled) leaves generation(); a wall moves it.
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
	meshAround(world, {228.0F, 214.0F}, 40.0F);
	settle(sys);
	auto inMesh = [&](const std::vector<glm::vec2>& trees) {
		return static_cast<std::size_t>(
			std::count_if(trees.begin(), trees.end(), [&](glm::vec2 t) { return sys.inSimArea(t) && !meshClear(sys, t); }));
	};
	ASSERT_EQ(inMesh(lost), lost.size());
	ASSERT_EQ(inMesh(ahead), 0u) << "no disc meets the other trees yet";

	// Every tree round the mover is felled as another mover appears by the other trees: his first
	// limit, not urgent, under the budgets.
	for (const glm::vec2 t : lost) {
		ASSERT_TRUE(executor.removeEntity(world::worldToChunk({t.x, t.y}), t, "Test_NavLostTree"));
	}
	meshAround(world, {-228.0F, 214.0F}, 40.0F);
	sys.resetNavQueryStats();
	// The new limit's obstacles go in ahead of its ground, hanging over unseen ground.
	auto gathered = [&](const std::vector<glm::vec2>& trees) {
		return static_cast<std::size_t>(std::count_if(trees.begin(), trees.end(), [&](glm::vec2 t) {
			return sys.placedObstacleInNavMesh(world::worldToChunk({t.x, t.y}), PlacedEntity{.defName = "Test_NavLostTree", .position = t});
		}));
	};
	step(world, sys);
	EXPECT_GT(inMesh(lost), 0u) << "while it was still draining";
	EXPECT_EQ(lost.size() - inMesh(lost), NavigationSystem::kDetailRemovalsPerTick) << "removals drained alongside, under their own budget";
	EXPECT_EQ(sys.navQueryStats().navPatches, 2u) << "one patch of removals and one of the limit's obstacles";
	step(world, sys); // its update collects the first tick's job
	EXPECT_EQ(gathered(ahead), NavigationSystem::kDetailRingsPerTick) << "adds went ahead of the backlog";

	settle(sys);
	EXPECT_EQ(inMesh(lost), 0u);
	EXPECT_EQ(inMesh(ahead), ahead.size());
}

// One chunk's placement leaves and another's arrives in the same tick: the obstacles already in the
// mesh stay, none goes in twice when its chunk's placement comes back, and a limit served after the
// swap finds the arriving chunk's obstacles.
TEST_F(NavigationSystemTest, PlacementSwapInOneTickKeepsAndScans) {
	registerTree("Test_NavSwapTree", 0.4F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						leaving{-20.0F, 10.0F}; // chunk (-1, 0)
	const glm::vec2						arriving{40.0F, 10.0F}; // chunk (0, 0)
	placeTrees(executor, processed, "Test_NavSwapTree", {leaving});

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	addColonist(world, {-10.0F, 10.0F}, "First");
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	settle(sys);
	ASSERT_TRUE(obstacleMeshed(sys, leaving));
	ASSERT_FALSE(sys.inSimArea(arriving));

	executor.unloadChunk({-1, 0});
	processed.erase(ChunkCoordinate{-1, 0});
	placeTrees(executor, processed, "Test_NavSwapTree", {arriving});
	settle(sys);
	EXPECT_TRUE(obstacleMeshed(sys, leaving)) << "the leaving chunk's obstacles stay";

	// The leaving chunk's placement comes back, and a second mover's limit holds both trees.
	placeTrees(executor, processed, "Test_NavSwapTree", {leaving});
	addColonist(world, {10.0F, 10.0F}, "Second");
	settle(sys);
	EXPECT_TRUE(obstacleMeshed(sys, arriving)) << "the arriving chunk's flora is in";
	const geometry::nav::PatchableMesh& mesh = sys.navMesh();
	const std::vector<geometry::nav::RingId>& cover = mesh.cover(mesh.locate(nav::toMm(leaving)));
	EXPECT_EQ(std::count_if(cover.begin(), cover.end(), [](geometry::nav::RingId id) { return id != geometry::nav::kGroundToken; }), 1)
		<< "the tree that came back went in once";
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

// WOR-644: two colonists far apart, with unseen ground between them. The mesh ends at what they
// have seen, so there is no known way; once a mover has walked from one to the other, their
// ground is one mesh, a route between them exists, and connected() agrees.
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
	ASSERT_TRUE(sys.inSimArea(a));
	ASSERT_TRUE(sys.inSimArea(b));
	EXPECT_FALSE(sys.inSimArea((a + b) * 0.5F)) << "nobody has seen the ground between them";
	EXPECT_FALSE(sys.connected(a, b));
	EXPECT_FALSE(routeBetween(sys, a, b).has_value());

	meshBand(world, {a, b});
	settle(sys);
	EXPECT_TRUE(routeBetween(sys, a, b).has_value());
	EXPECT_TRUE(sys.connected(a, b));
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

// A limit arriving over the edge of meshed ground joins it: a path crosses where the first limit
// ended. The limit comes because a mover walked on. Before, the first limit's side is the mesh's
// boundary; after, where the two overlap, no edge the mesh keeps has land on both sides.
TEST_F(NavigationSystemTest, ArrivingChunkJoinsItsNeighbour) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setConstructionWorld(&cw);
	constexpr float kRange = 40.0F;
	const EntityID	mover  = meshDisc(world, {300.0F, 100.0F}, kRange);
	settle(sys);
	const glm::vec2 west{330.0F, 100.0F};
	const glm::vec2 east{360.0F, 100.0F};
	ASSERT_TRUE(sys.inSimArea(west));
	ASSERT_FALSE(sys.inSimArea(east));
	EXPECT_FALSE(routeBetween(sys, west, east).has_value());
	// Just inside the first limit's eastern corner, which is the mesh's edge.
	const glm::vec2 corner{300.0F + static_cast<float>(geometry::nav::limitCircumradius(nav::toMm(glm::vec2{kRange, 0.0F}).x)) / 1000.0F - 0.5F, 100.0F};
	ASSERT_TRUE(sys.inSimArea(corner));
	ASSERT_FALSE(sys.inSimArea(corner + glm::vec2{1.0F, 0.0F}));

	world.getComponent<Position>(mover)->value = {340.0F, 100.0F};
	settle(sys);
	EXPECT_TRUE(routeBetween(sys, west, east).has_value()) << "the path crosses where the first limit ended";
	EXPECT_TRUE(sys.connected(west, east));
	EXPECT_TRUE(sys.inSimArea(corner + glm::vec2{1.0F, 0.0F}));
	const geometry::nav::PatchableMesh& mesh		 = sys.navMesh();
	int									landBetween = 0;
	for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
		if (!mesh.triangleLive(t)) {
			continue;
		}
		for (int e = 0; e < 3; ++e) {
			const std::int32_t across = mesh.triangles()[static_cast<std::size_t>(t)].neighbor[static_cast<std::size_t>(e)];
			landBetween += across >= 0 && !mesh.edgeSources(t, e).empty() && geometry::nav::isFloorFace(mesh.triangles()[static_cast<std::size_t>(t)]) &&
								   geometry::nav::isFloorFace(mesh.triangles()[static_cast<std::size_t>(across)])
							   ? 1
							   : 0;
		}
	}
	EXPECT_EQ(landBetween, 0) << "a constrained edge between land where the limits overlap";
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

// The same route through a post field is as long when the field is meshed by two overlapping limits,
// each ending inside it, as when one limit holds all of it: where the limits meet adds no detour.
TEST_F(NavigationSystemTest, RouteThroughPostsAcrossABorderIsAsLongAsInsideAChunk) {
	registerTree(kPost, kPostHalf);
	const auto routeLength = [](glm::vec2 gapOrigin, bool twoLimits) {
		PlacementExecutor					executor(AssetRegistry::Get());
		std::unordered_set<ChunkCoordinate> processed;
		placeTrees(executor, processed, kPost, postField(gapOrigin));
		ConstructionWorld cw;
		World			  world;
		NavigationSystem& sys = world.registerSystem<NavigationSystem>();
		sys.setChunkManager(landChunks());
		sys.setPlacementData(&executor, &processed);
		sys.setConstructionWorld(&cw);
		if (twoLimits) {
			meshDisc(world, gapOrigin - glm::vec2{30.0F, 0.0F}, 50.0F);
			meshDisc(world, gapOrigin + glm::vec2{30.0F, 0.0F}, 50.0F);
		} else {
			meshDisc(world, gapOrigin, 70.0F);
		}
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
	const float across = routeLength({256.0F, 100.0F}, true);
	const float inside = routeLength({256.0F, 100.0F}, false);
	ASSERT_GT(inside, 0.0F) << "no route inside one limit";
	ASSERT_GT(across, 0.0F) << "no route where two limits meet";
	EXPECT_NEAR(across, inside, 0.01F);
}

// A post field where two overlapping limits meet meshes like the interior: once the second limit is
// in, the floor triangles over where the first limit ended are no longer than those over a line of
// the field inside the first limit alone.
TEST_F(NavigationSystemTest, BorderInAPostFieldMeshesLikeTheInterior) {
	registerTree(kPost, kPostHalf);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	placeTrees(executor, processed, kPost, postField({512.0F, 100.0F}));

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);
	constexpr float kRange = 50.0F;
	meshDisc(world, {470.0F, 100.0F}, kRange);
	settle(sys);
	ASSERT_FALSE(sys.inSimArea({530.0F, 100.0F}));

	// Where the first limit's eastern corner was, and 16 gaps west of the field's middle.
	const std::int64_t	   kBorderMm   = 470000 + geometry::nav::limitCircumradius(nav::toMm(glm::vec2{kRange, 0.0F}).x) - 500;
	constexpr std::int64_t kInteriorMm = 486400;
	constexpr std::int64_t kLoMm	   = 84000;
	constexpr std::int64_t kHiMm	   = 116000;

	meshDisc(world, {540.0F, 100.0F}, kRange);
	settle(sys);
	ASSERT_TRUE(sys.inSimArea({530.0F, 100.0F}));
	const double interior = longestEdgeAcross(sys.navMesh(), kInteriorMm, kLoMm, kHiMm);
	const double border	  = longestEdgeAcross(sys.navMesh(), kBorderMm, kLoMm, kHiMm);
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
	settle(sys);
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

	// The mover walks east, the camera with it: (0, 0) unloads, and GameScene drops its placement.
	world.getComponent<Position>(mover)->value = {900.0F, 256.0F};
	load({900.0F, 256.0F});
	settle(sys); // the mover's limit where it now stands
	ASSERT_EQ(chunks->getChunk({0, 0}), nullptr);
	executor.unloadChunk({0, 0});
	processed.erase(ChunkCoordinate{0, 0});
	sys.resetNavQueryStats();
	settle(sys);
	EXPECT_TRUE(sys.inSimArea(treeAt)) << "the unloaded chunk's ground stays";
	EXPECT_FALSE(meshClear(sys, treeAt)) << "and so do its obstacles";
	EXPECT_EQ(sys.navQueryStats().navPatches, 0u) << "unloading patched the nav mesh";
	EXPECT_TRUE(routeBetween(sys, treeAt - glm::vec2{20.0F, 0.0F}, treeAt + glm::vec2{20.0F, 0.0F}).has_value()) << "an order routes across it";

	// Back, with its placement placed again.
	const std::size_t	slotsAfterWalk = sys.navMesh().liveTriangleCount();
	const std::uint64_t before		   = sys.generation();
	world.getComponent<Position>(mover)->value = {490.0F, 256.0F};
	load({256.0F, 256.0F});
	ASSERT_NE(chunks->getChunk({0, 0}), nullptr);
	placeTrees(executor, processed, "Test_NavUnloadTree", {treeAt});
	settle(sys);
	EXPECT_EQ(sys.navQueryStats().navPatches, 0u) << "the reload patched the nav mesh";
	EXPECT_EQ(sys.generation(), before);
	EXPECT_EQ(sys.navMesh().liveTriangleCount(), slotsAfterWalk) << "nothing added twice";
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
	// a 4 m pond every 10 m within kFordX + 40 m of the origin along x, and across it as far as a band
	// walked along the near bank sees (meshBand), so the mesh is fine everywhere a search to the far
	// bank goes and the way round the ford takes thousands of steps.
	template <int kFordX> class PondFieldSampler : public world::IWorldSampler {
	  public:
		PondFieldSampler() {
			constexpr double kRiverY = 6.0;
			segments.push_back({.x0 = -2048.0, .y0 = kRiverY, .x1 = static_cast<double>(kFordX), .y1 = kRiverY, .halfWidth0 = 3.0F, .halfWidth1 = 3.0F,
								.s0 = 0.0, .s1 = 2048.0 + static_cast<double>(kFordX)});
			const int reach	 = (kFordX + 40) / 10;
			const int across = static_cast<int>((kDefaultSightRadius + NavigationSystem::kSightBufferMeters) / 10.0F) + 1;
			for (int j = -across; j <= across; ++j) {
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

// The far bank of a river whose ford is 150 m east, among ponds, is about 15,000 steps round, over a
// near bank walked to the ford. The route by ticket finds it, round the ford, and every leg of the
// route is clear of water.
TEST_F(NavigationSystemTest, FarGoalIsRoutedRoundTheFord) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	ConstructionWorld cw;
	sys.setChunkManager(pondFieldChunks<150>());
	meshBand(world, {kPondStart, {150.0F, kPondStart.y}});
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

// The ford 1 km east is about 2 km round through the ponds, over a near bank walked to it, more than
// one route slice of search: a route search has no step limit, so it carries on over the ticks it
// needs and finds the way.
TEST_F(NavigationSystemTest, FordFarAroundIsRoutedOverSeveralSlices) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	ConstructionWorld cw;
	sys.setChunkManager(pondFieldChunks<1000>());
	meshBand(world, {kPondStart, {1000.0F, kPondStart.y}});
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

// inSimArea is the union of the limits that went in: true within the range of either of two
// overlapping limits, across the four chunks round the origin, and false past both limits'
// circumradius, a metre past the edge along an axis and along a diagonal included, and on a loaded
// chunk no limit meets.
TEST_F(NavigationSystemTest, InSimAreaIsTheUnionOfLimits) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setConstructionWorld(&cw);
	constexpr float kRange = 40.0F;
	const glm::vec2 a{0.0F, 0.0F};
	const glm::vec2 b{kRange * 1.25F, kRange * 0.5F};
	meshDisc(world, a, kRange);
	meshDisc(world, b, kRange);
	settle(sys);
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	const float circumradius = static_cast<float>(geometry::nav::limitCircumradius(nav::toMm(glm::vec2{kRange, 0.0F}).x)) / 1000.0F;
	for (float x = a.x - 1.5F * kRange; x <= b.x + 1.5F * kRange; x += 2.0F) {
		for (float y = a.y - 1.5F * kRange; y <= b.y + 1.5F * kRange; y += 2.0F) {
			const glm::vec2 p{x, y};
			const float		da = glm::distance(p, a);
			const float		db = glm::distance(p, b);
			if (da < kRange || db < kRange) {
				EXPECT_TRUE(sys.inSimArea(p)) << "(" << x << ", " << y << ") within a limit's range";
			} else if (da > circumradius && db > circumradius) {
				EXPECT_FALSE(sys.inSimArea(p)) << "(" << x << ", " << y << ") past both limits";
			}
		}
	}
	EXPECT_FALSE(sys.inSimArea(a - glm::vec2{circumradius + 1.0F, 0.0F})) << "a metre past the edge along an axis";
	EXPECT_FALSE(sys.inSimArea(a - glm::vec2{circumradius + 1.0F} * 0.70710678F)) << "a metre past the edge along a diagonal";
	ASSERT_NE(landChunks()->getChunk(world::worldToChunk({1000.0F, -900.0F})), nullptr);
	EXPECT_FALSE(sys.inSimArea({1000.0F, -900.0F})) << "a loaded chunk no limit meets";
	EXPECT_FALSE(sys.inSimArea(kUnmeshed));
}

// The landing meshes one limit of kLandingSightMeters round the drop, with the placed obstacles in
// it, before any mover: ground within that radius is in, and nothing past the limit's circumradius.
TEST_F(NavigationSystemTest, LandingMeshesOnlyTheDropLimit) {
	registerTree("Test_NavLandingObstacle", 0.4F);
	const glm::vec2						drop{100.0F, 100.0F};
	const float							circumradius =
		static_cast<float>(geometry::nav::limitCircumradius(nav::toMm(glm::vec2{NavigationSystem::kLandingSightMeters, 0.0F}).x)) / 1000.0F;
	const glm::vec2						seen	= drop + glm::vec2{NavigationSystem::kLandingSightMeters - 10.0F, 0.0F};
	const glm::vec2						unseen	= drop - glm::vec2{0.0F, circumradius + 10.0F};
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	placeTrees(executor, processed, "Test_NavLandingObstacle", {seen, unseen});
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setPlacementData(&executor, &processed);
	const NavigationSystem::GroupSpawn spawn = sys.groupSpawnPoints(drop, 1, 0.0F, 0.0F, kAgentRadius);
	ASSERT_EQ(spawn.points.size(), 1U);
	expectMeshValid(sys);

	for (int k = 0; k < 8; ++k) {
		const float		angle = 6.2831853F * static_cast<float>(k) / 8.0F;
		const glm::vec2 dir{std::cos(angle), std::sin(angle)};
		EXPECT_TRUE(sys.inSimArea(drop + dir * (NavigationSystem::kLandingSightMeters - 1.0F))) << "direction " << k;
		EXPECT_FALSE(sys.inSimArea(drop + dir * (circumradius + 1.0F))) << "direction " << k;
	}
	EXPECT_TRUE(obstacleMeshed(sys, seen)) << "the obstacle in the landing limit came in with it";
	EXPECT_FALSE(sys.inSimArea(unseen));
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

// Water crossing a chunk corner arrives as one piece per chunk, the pieces touching at the corner,
// and the land around them has a boundary pinched there. That land must still be walkable (WOR-493),
// and each piece's water blocks up to the shared corner.
TEST_F(NavigationSystemTest, LandAroundWaterPinchedAtAChunkCornerIsValid) {
	auto chunks = std::make_unique<world::ChunkManager>(cornerPondSampler());
	chunks->setLoadRadius(0);
	chunks->setUnloadRadius(8);
	const std::array<ChunkCoordinate, 4> round{ChunkCoordinate{0, 0}, ChunkCoordinate{-1, -1}, ChunkCoordinate{0, -1}, ChunkCoordinate{-1, 0}};
	for (const ChunkCoordinate coord : round) {
		constexpr float kSide = static_cast<float>(world::kChunkSize);
		chunks->update({(static_cast<float>(coord.x) + 0.5F) * kSide, (static_cast<float>(coord.y) + 0.5F) * kSide});
	}
	chunks->finishPendingGeneration();

	for (const ChunkCoordinate coord : round) {
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
	for (const glm::vec2 water : {glm::vec2{3.0F, 3.0F}, glm::vec2{-3.0F, -3.0F}, glm::vec2{3.0F, -3.0F}, glm::vec2{-3.0F, 3.0F}}) {
		EXPECT_FALSE(meshClear(sys, water)) << "the pond at (" << water.x << ", " << water.y << ") must not be";
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
// two water rings' edges and stays constrained; the ground of two overlapping limits, one each side
// of the border, leaves no edge of its own along it.
TEST_F(NavigationSystemTest, WaterStretchOfABorderStaysConstrained) {
	auto			  chunks = readyChunks(cornerPondSampler(), {0.0F, 0.0F}, 1);
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	sys.setConstructionWorld(&cw);
	meshDisc(world, {-30.0F, 0.0F}, 50.0F);
	meshDisc(world, {30.0F, 0.0F}, 50.0F);
	settle(sys);
	ASSERT_TRUE(sys.inSimArea({-70.0F, 0.0F}) && sys.inSimArea({70.0F, 0.0F}) && sys.connected({0.0F, 30.0F}, {0.0F, -30.0F}));

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

// On walkable ground with a wall built across the route ahead, the AI replans from where it stands: a
// route stamped with the new generation that goes round the wall.
TEST_F(ForwardSnapTest, RouteBlockedAheadIsReplannedFromWhereItStands) {
	AIDecisionSystem& ai = world.registerSystem<AIDecisionSystem>(AssetRegistry::Get(), assets::RecipeRegistry::Get(), 1234u);
	ai.setNavigationSystem(sys);
	const glm::vec2 at{-16.0F, 20.0F};
	const glm::vec2 goal{-4.0F, 20.0F};
	const glm::vec2 across{-10.0F, 20.0F};
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
	ASSERT_EQ(path.waypoints.size(), 2u) << "straight through where the wall will stand";

	// Routes are planned over what the colonist knows: he sees the wall go up.
	const SegmentId wall = buildWall(cw, nav::toMm(across - glm::vec2{0.0F, 3.0F}), nav::toMm(across + glm::vec2{0.0F, 3.0F}));
	world.getComponent<Memory>(colonist)->rememberSegment(wall);
	for (int i = 0; i < 10; ++i) {
		step(world, *sys);
	}
	ASSERT_TRUE(wallAt(*sys, across)) << "the wall across the route is in";
	EXPECT_EQ(world.getComponent<Position>(colonist)->value, at);
	ASSERT_TRUE(path.valid);
	EXPECT_EQ(path.builtNavVersion, sys->generation()) << "not replanned";
	EXPECT_EQ(path.waypoints.front(), at) << "replanned from where it stands";
	for (std::size_t k = 0; k + 1 < path.waypoints.size(); ++k) {
		for (int i = 0; i <= 20; ++i) {
			const glm::vec2 p = path.waypoints[k] + (path.waypoints[k + 1] - path.waypoints[k]) * (static_cast<float>(i) / 20.0F);
			EXPECT_TRUE(meshClear(*sys, p) && !wallAt(*sys, p)) << "the new route crosses the wall at (" << p.x << ", " << p.y << ")";
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

// A colonist walking a move order is shoved off his route into a placed obstacle beside it. He is
// moved forward along his route, keeps the order, never steps back and gets there.
TEST_F(NavigationSystemTest, WalkerShovedIntoAnObstacleKeepsItsTask) {
	{
		registerTree("Test_NavWalkerTree", 0.4F);
		PlacementExecutor					executor(AssetRegistry::Get());
		std::unordered_set<ChunkCoordinate> processed;
		const glm::vec2						treeAt{12.0F, 6.5F}; // clear of the straight route along y = 5
		placeTrees(executor, processed, "Test_NavWalkerTree", {treeAt});
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

		ASSERT_FALSE(meshClear(nav, treeAt)) << "the obstacle came in with the walker's ground";

		constexpr float kTick = 0.1F;
		Position&		position = *world.getComponent<Position>(colonist);
		while (position.value.x < treeAt.x - 0.2F) {
			world.update(kTick);
		}
		ASSERT_LT(std::abs(position.value.y - start.y), 0.05F) << "walking the straight route";

		const Task& task = *world.getComponent<Task>(colonist);
		position.value.y = treeAt.y;
		ASSERT_FALSE(nav.isValidPosition(position.value)) << "shoved into the obstacle";
		float lastX = position.value.x;

		for (int i = 0; i < 100 && glm::distance(position.value, goal) > 0.3F; ++i) {
			world.update(kTick);
			expectMeshValid(nav);
			ASSERT_GE(position.value.x, lastX - 1e-4F) << "tick " << i << ": moved back";
			ASSERT_EQ(task.targetPosition, goal) << "tick " << i << ": the move order was dropped";
			lastX = position.value.x;
		}
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

// Two movers whose limits overlap add each placed obstacle once: every obstacle in the ground they
// saw is in, those both limits meet among them, and taking each out once leaves its ground clear. Each
// limit is one patch, its obstacles riding with its ground.
TEST_F(NavigationSystemTest, OverlappingLimitsAddEachObstacleOnce) {
	registerTree("Test_NavOverlapTree", 0.3F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	std::vector<glm::vec2>				trees;
	for (float x = -88.0F; x < 120.0F; x += 8.0F) {
		for (float y = -88.0F; y < 120.0F; y += 8.0F) {
			trees.push_back({x + 0.5F, y + 0.5F});
		}
	}
	placeTrees(executor, processed, "Test_NavOverlapTree", trees);

	const glm::vec2 a{8.0F, 8.0F};
	const glm::vec2 b{30.0F, 12.0F};
	const float		r	 = kDefaultSightRadius + NavigationSystem::kSightBufferMeters;
	const auto		both = std::count_if(trees.begin(), trees.end(), [&](glm::vec2 t) { return glm::distance(a, t) < r && glm::distance(b, t) < r; });
	ASSERT_GT(both, 0) << "the limits overlap";

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
	EXPECT_EQ(stats.groundWaterWallPatches, 2u) << "one ground patch per limit";
	EXPECT_EQ(stats.navPatches, 2u) << "the obstacles rode with their limits' ground";
	std::size_t seen = 0;
	for (const glm::vec2 t : trees) {
		if (glm::distance(a, t) < r || glm::distance(b, t) < r) {
			++seen;
			EXPECT_TRUE(obstacleMeshed(sys, t)) << "the obstacle at (" << t.x << ", " << t.y << ")";
		}
	}
	ASSERT_GT(seen, static_cast<std::size_t>(both));

	for (const glm::vec2 t : trees) {
		ASSERT_TRUE(executor.removeEntity(world::worldToChunk({t.x, t.y}), t, "Test_NavOverlapTree"));
	}
	settle(sys);
	for (const glm::vec2 t : trees) {
		if (sys.inSimArea(t)) {
			EXPECT_TRUE(meshClear(sys, t)) << "a second copy of the obstacle at (" << t.x << ", " << t.y << ") stayed";
		}
	}
}

// --- Ground edge cases ---------------------------------------------------------------

// A meshed chunk whose terrain is rebuilt has its water taken out and put back in one tick, leaving
// the same mesh: the pond still blocks, the land around it is walkable, no triangle is gained or lost.
TEST_F(NavigationSystemTest, TerrainChangeOnMeshedChunkRederivesItsWater) {
	// The anchor's limit, near the origin, reaches into the chunks round (0, 0).
	const std::unique_ptr<world::ChunkManager> chunks = readyChunks(waterPatchSampler(), {256.0F, 256.0F}, 1);
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

// Placed obstacles taken out of the world in one tick, in three groups metres apart, come out of
// the mesh in that tick's one removal patch, touching no ground or wall.
TEST_F(NavigationSystemTest, RemovalsInATickArePatchedOnce) {
	registerTree("Test_NavCellTree", 0.3F);
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	std::vector<glm::vec2>				trees;
	for (const float x : {4.0F, 20.0F, 36.0F}) {
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
	step(world, sys); // the job the first gathered is joined
	ASSERT_FALSE(sys.isBuilding());
	EXPECT_EQ(sys.navQueryStats().navPatches, 1u);
	EXPECT_EQ(sys.navQueryStats().groundWaterWallPatches, 0u) << "a removal patched ground or walls";
	EXPECT_TRUE(std::all_of(trees.begin(), trees.end(), [&](glm::vec2 t) { return meshClear(sys, t); })) << "all removed";
}

// A placed obstacle felled while the limit it stands in waits its turn never enters the mesh: the
// limit lands without it, and its ground there is walkable.
TEST_F(NavigationSystemTest, FelledWhileItsLimitWaitsNeverEnters) {
	registerTree("Test_NavWaitFellTree", 0.4F);
	const std::array<glm::vec2, 2> movers{glm::vec2{-200.0F, 100.0F}, glm::vec2{200.0F, 100.0F}};
	const std::array<glm::vec2, 2> trees{movers[0] + glm::vec2{10.0F, 0.0F}, movers[1] + glm::vec2{10.0F, 0.0F}};
	PlacementExecutor				executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	placeTrees(executor, processed, "Test_NavWaitFellTree", std::vector<glm::vec2>(trees.begin(), trees.end()));

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setPlacementData(&executor, &processed);
	for (const glm::vec2 at : movers) {
		addColonist(world, at, "Mover");
	}
	ASSERT_EQ(NavigationSystem::kLimitsPerTick, 1U);
	step(world, sys);
	ASSERT_NE(sys.inSimArea(movers[0]), sys.inSimArea(movers[1])) << "one limit a tick";
	const std::size_t waiting = sys.inSimArea(movers[0]) ? 1 : 0;
	ASSERT_TRUE(sys.isBuilding());
	ASSERT_TRUE(executor.removeEntity(world::worldToChunk({trees[waiting].x, trees[waiting].y}), trees[waiting], "Test_NavWaitFellTree"));

	sys.resetNavQueryStats();
	step(world, sys);
	EXPECT_TRUE(sys.inSimArea(movers[waiting])) << "the waiting limit went in";
	EXPECT_EQ(sys.navQueryStats().navPatches, 1U) << "its limit, and no removal";
	EXPECT_TRUE(meshClear(sys, trees[waiting])) << "the felled obstacle entered";
	EXPECT_FALSE(sys.placedObstacleInNavMesh(world::worldToChunk({trees[waiting].x, trees[waiting].y}),
											 PlacedEntity{.defName = "Test_NavWaitFellTree", .position = trees[waiting]}));
	EXPECT_TRUE(obstacleMeshed(sys, trees[1 - waiting])) << "the other stays";
	EXPECT_FALSE(sys.isBuilding());
}

// A placed obstacle felled where no limit has reached changes nothing: no patch, nothing left waiting,
// and a mover arriving later meshes the ground without it.
TEST_F(NavigationSystemTest, FelledOutsideSeenGroundChangesNothing) {
	registerTree("Test_NavUnseenFellTree", 0.4F);
	const glm::vec2						seenTree{10.0F, 10.0F};
	const glm::vec2						unseenTree{800.0F, 100.0F};
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	placeTrees(executor, processed, "Test_NavUnseenFellTree", {seenTree, unseenTree});

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setPlacementData(&executor, &processed);
	addColonist(world, {0.0F, 0.0F}, "Here");
	settle(sys);
	ASSERT_TRUE(obstacleMeshed(sys, seenTree));
	ASSERT_FALSE(sys.inSimArea(unseenTree));
	const std::uint64_t epoch = sys.navMesh().epoch();

	ASSERT_TRUE(executor.removeEntity(world::worldToChunk({unseenTree.x, unseenTree.y}), unseenTree, "Test_NavUnseenFellTree"));
	sys.resetNavQueryStats();
	step(world, sys);
	EXPECT_EQ(sys.navQueryStats().navPatches, 0U);
	EXPECT_EQ(sys.navMesh().epoch(), epoch);
	EXPECT_FALSE(sys.isBuilding()) << "an entry was left behind";

	addColonist(world, unseenTree - glm::vec2{10.0F, 0.0F}, "There");
	settle(sys);
	EXPECT_TRUE(meshClear(sys, unseenTree)) << "the felled obstacle came in with the ground";
	EXPECT_TRUE(obstacleMeshed(sys, seenTree));
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
			placeNothingElsewhere(executor, &processed);
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

// --- Staleness: a blocker marks only the routes it lands on -------------------------------------

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

// A wall built across one route marks it stale, and so does one built beside a route, its segment
// never touching the route, whose band comes within the mover's radius of it; a route passing
// farther from the wall's band, or a few metres off its end, stays current through the generation
// the wall moves.
TEST_F(NavigationSystemTest, WallLandingMarksOnlyTheRoutesItCrosses) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	const float		radius = AgentRadius{}.radiusMeters;
	const float		halfThickness = 0.1F; // a Standard wall is 0.2 m thick
	const float		wallX		  = 16.0F;
	const EntityID	across		  = routedMover(world, sys, {1.0F, 8.0F}, {31.0F, 8.0F});
	const EntityID	beside		  = routedMover(world, sys, {wallX - halfThickness - radius * 0.5F, 0.5F}, {wallX - halfThickness - radius * 0.5F, 15.0F});
	const EntityID	clear		  = routedMover(world, sys, {wallX + halfThickness + radius * 2.0F, 0.5F}, {wallX + halfThickness + radius * 2.0F, 15.0F});
	const EntityID	offTheEnd	  = routedMover(world, sys, {1.0F, 18.0F}, {31.0F, 18.0F});
	const std::uint64_t generation = sys.generation();

	buildWall(cw, nav::toMm({wallX, 4.0F}), nav::toMm({wallX, 12.0F}));
	step(world, sys);
	step(world, sys);
	ASSERT_TRUE(wallAt(sys, {wallX, 8.0F})) << "the wall is in";
	EXPECT_GT(sys.generation(), generation);
	EXPECT_TRUE(stale(world, sys, across)) << "a wall across the route didn't mark it";
	EXPECT_TRUE(stale(world, sys, beside)) << "a wall within the mover's radius of the route didn't mark it";
	EXPECT_FALSE(stale(world, sys, clear)) << "a wall clear of the route by more than the mover's radius marked it";
	EXPECT_FALSE(stale(world, sys, offTheEnd)) << "a wall 6 m off the route marked it";
}

// A placed obstacle back in the world over ground already meshed (felled, then regenerated as its
// chunk reloads) comes in with the next limit that reaches past the meshed ground over it, and marks
// the routes it lands across as a wall does: one crossing it goes stale and is replanned round it from
// where its mover stands; one clear of it stays current.
TEST_F(NavigationSystemTest, ObstacleBackOverMeshedGroundMarksOnlyTheRoutesItCrosses) {
	registerTree("Test_NavRegrownObstacle", 1.0F);
	const glm::vec2						obstacleAt{10.0F, 0.0F};
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	placeTrees(executor, processed, "Test_NavRegrownObstacle", {obstacleAt});

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setPlacementData(&executor, &processed);
	constexpr float kMeshedRange = 2.0F * kColonistRange;
	meshDisc(world, {0.0F, 0.0F}, kMeshedRange);
	settle(sys);
	ASSERT_TRUE(obstacleMeshed(sys, obstacleAt));
	ASSERT_TRUE(executor.removeEntity(world::worldToChunk({obstacleAt.x, obstacleAt.y}), obstacleAt, "Test_NavRegrownObstacle"));
	settle(sys);
	ASSERT_TRUE(meshClear(sys, obstacleAt));

	const glm::vec2 crossFrom = obstacleAt - glm::vec2{0.0F, 6.0F};
	const glm::vec2 crossTo	  = obstacleAt + glm::vec2{0.0F, 6.0F};
	const EntityID	across	  = routedMover(world, sys, crossFrom, crossTo);
	const EntityID	clear	  = routedMover(world, sys, crossFrom - glm::vec2{20.0F, 0.0F}, crossTo - glm::vec2{20.0F, 0.0F});
	settle(sys);
	ASSERT_FALSE(stale(world, sys, across)) << "their own limits lie in meshed ground";
	ASSERT_FALSE(stale(world, sys, clear));

	placeTrees(executor, processed, "Test_NavRegrownObstacle", {obstacleAt});
	addColonist(world, obstacleAt + glm::vec2{0.9F * kColonistRange, 0.0F}, "Past the edge");
	settle(sys);
	ASSERT_TRUE(obstacleMeshed(sys, obstacleAt)) << "the next limit over it brought it in";
	EXPECT_TRUE(stale(world, sys, across)) << "it landed across a route and left it current";
	EXPECT_FALSE(stale(world, sys, clear)) << "it marked a route 20 m off";
	const auto replanned = routeBetween(sys, crossFrom, crossTo);
	ASSERT_TRUE(replanned.has_value());
	EXPECT_GT(lengthOf(*replanned), glm::distance(crossFrom, crossTo)) << "the new route goes round it";
}

namespace {
	// The placed obstacle rings whose cover holds the mesh's triangle at `meters`.
	std::vector<geometry::nav::RingId> placedRingsAt(const NavigationSystem& sys, glm::vec2 meters) {
		const geometry::nav::PatchableMesh& mesh = sys.navMesh();
		const std::int32_t					tri	 = mesh.locate(nav::toMm(meters));
		std::vector<geometry::nav::RingId>	rings;
		if (tri < 0) {
			return rings;
		}
		for (const geometry::nav::RingId r : mesh.cover(tri)) {
			if (r != geometry::nav::kGroundToken && mesh.ring(r).polygon.provenanceId == nav::kProvenancePlaced) {
				rings.push_back(r);
			}
		}
		return rings;
	}
} // namespace

// A placed obstacle whose footprint crosses the edge of the seen area blocks inside the union and adds
// no triangle outside it; once a later limit covers the rest, the whole footprint blocks, under the one
// ring it went in as.
TEST_F(NavigationSystemTest, ObstacleStraddlingTheEdgeBlocksInsideOnly) {
	registerTree("Test_NavStraddlingObstacle", 1.5F);
	const glm::vec2				 lookout{0.0F, 0.0F};
	const std::vector<Vec2i64> outline =
		nav::visibilityLimit(nav::toMm(lookout), static_cast<std::int64_t>(kColonistRange * static_cast<float>(geometry::kMillimetersPerMeter))).ring;
	const glm::vec2 edgeAt	= 0.5F * (nav::toMeters(outline[0]) + nav::toMeters(outline[1]));
	const glm::vec2 outward = glm::normalize(edgeAt - lookout);
	const glm::vec2 inside	= edgeAt - 0.75F * outward;
	const glm::vec2 outside = edgeAt + 0.75F * outward;
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	placeTrees(executor, processed, "Test_NavStraddlingObstacle", {edgeAt});

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setPlacementData(&executor, &processed);
	addColonist(world, lookout, "Lookout");
	settle(sys);
	ASSERT_TRUE(sys.inSimArea(inside));
	EXPECT_TRUE(obstacleMeshed(sys, inside)) << "it blocks inside the seen area";
	EXPECT_FALSE(sys.inSimArea(outside)) << "it added mesh outside the seen area";
	const std::vector<geometry::nav::RingId> ring = placedRingsAt(sys, inside);
	ASSERT_EQ(ring.size(), 1U);

	addColonist(world, lookout + kColonistRange * outward, "Beyond");
	settle(sys);
	EXPECT_TRUE(obstacleMeshed(sys, outside)) << "the rest of its footprint blocks once seen";
	EXPECT_EQ(placedRingsAt(sys, inside), ring);
	EXPECT_EQ(placedRingsAt(sys, outside), ring) << "it went in again";
}

namespace {
	// The disc of `r` round `p` is clear ground: `p`, and points round two rims (half `r` and `r`), each
	// pass isValidPosition, so it is in no water or placed obstacle and inside the mesh's edge.
	bool discClear(const NavigationSystem& sys, glm::vec2 p, float r) {
		constexpr int kRimPoints = 32;
		if (!sys.isValidPosition(p)) {
			return false;
		}
		for (const float f : {0.5F, 1.0F}) {
			for (int k = 0; k < kRimPoints; ++k) {
				const float angle = 6.2831853F * static_cast<float>(k) / static_cast<float>(kRimPoints);
				if (!sys.isValidPosition(p + f * r * glm::vec2{std::cos(angle), std::sin(angle)})) {
					return false;
				}
			}
		}
		return true;
	}
} // namespace

// A move order clicked on a placed obstacle snaps its goal to ground beside it where the mover's whole
// disc stands clear: the walker goes round on a route that crosses no placed obstacle, stops beside it
// and stays there.
TEST_F(NavigationSystemTest, MoveOrderOntoAPlacedObstacleEndsBesideIt) {
	registerTree("Test_NavOrderObstacle", 0.4F);
	const glm::vec2 obstacleAt{10.0F, 5.0F};
	WalkRig			rig(obstacleAt);
	placeTrees(rig.executor, rig.processed, "Test_NavOrderObstacle", {obstacleAt});
	rig.start();
	const EntityID	colonist = rig.walker({2.0F, 5.0F}, obstacleAt);
	const glm::vec2 goal	 = rig.world.getComponent<Task>(colonist)->targetPosition;
	EXPECT_TRUE(discClear(rig.nav, goal, kAgentRadius)) << "the mover's disc at the goal overlaps the placed obstacle";
	EXPECT_LT(glm::distance(goal, obstacleAt), 1.0F) << "beside it";

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

// A point inside the mesh's edge by less than a mover's radius snaps to where his disc stands inside
// it; a point off the mesh has nowhere to snap to.
TEST_F(NavigationSystemTest, NearestPathablePointStandsClearOfTheMeshEdge) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	meshDisc(world, {0.0F, 0.0F}, kColonistRange);
	settle(sys);
	// Through the middle of an edge of the limit's polygon, which lies kColonistRange out.
	const float		angle = 3.14159265F / static_cast<float>(geometry::nav::kLimitSides);
	const glm::vec2 out{std::cos(angle), std::sin(angle)};
	const glm::vec2 nearEdge = out * (kColonistRange - kAgentRadius * 0.5F);
	ASSERT_TRUE(sys.isValidPosition(nearEdge));
	ASSERT_FALSE(discClear(sys, nearEdge, kAgentRadius)) << "the point's disc already stands inside";

	const std::optional<glm::vec2> stand = sys.nearestPathablePoint(nearEdge, kAgentRadius);
	ASSERT_TRUE(stand.has_value());
	EXPECT_TRUE(discClear(sys, *stand, kAgentRadius));
	EXPECT_LT(glm::distance(*stand, nearEdge), kAgentRadius) << "not the nearest such point";
	EXPECT_FALSE(sys.nearestPathablePoint(out * (kColonistRange + kAgentRadius * 4.0F), kAgentRadius).has_value()) << "off the mesh";
}

// A route through a wall that was already there isn't news when another wall is built off the
// route: only the walls just built are checked.
TEST_F(NavigationSystemTest, OldWallOnARouteIsNoNewsWhenAnotherLands) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	const glm::vec2 old{1.0F, 8.0F};
	buildWall(cw, nav::toMm(old - glm::vec2{3.0F, 0.0F}), nav::toMm(old + glm::vec2{3.0F, 0.0F}));
	settle(sys);
	ASSERT_TRUE(wallAt(sys, old)) << "the old wall is in";
	const EntityID		mover	   = routedMover(world, sys, {1.0F, 0.5F}, {1.0F, 15.0F});
	const std::uint64_t generation = sys.generation();

	buildWall(cw, {12000, 1000}, {12000, 15000});
	step(world, sys);
	step(world, sys);
	ASSERT_TRUE(wallAt(sys, {12.0F, 8.0F})) << "the new wall is in";
	ASSERT_GT(sys.generation(), generation);
	EXPECT_FALSE(stale(world, sys, mover)) << "a wall built off the route replanned it";
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
// with placed obstacles in it, all of it walked out from Home: the mesh covers both ends wherever they
// are, so it gets a route and walks to the water, and never holds for an off-area endpoint (the old
// regions' failure).
TEST_F(NavigationSystemTest, FarColonistReachesWaterItKnows) {
	registerTree("Test_NavFarTree", 0.3F);
	WalkRig rig({0.0F, 0.0F});
	std::vector<glm::vec2> trees;
	for (float x = 120.0F; x < 190.0F; x += 7.0F) {
		trees.push_back({x, -3.0F + std::fmod(x, 6.0F)});
	}
	placeTrees(rig.executor, rig.processed, "Test_NavFarTree", trees);
	const glm::vec2 home{0.0F, 0.0F};
	const glm::vec2 water{200.0F, 0.0F};
	addColonist(rig.world, home, "Home");
	meshBand(rig.world, {home, water});
	rig.start();

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

// The twin with nothing walked out from Home: the water 90 m on lies in no limit, so no known way
// joins him to it. The thirst option is not Selected, he never sets off for it, and he never waits
// for ground there (none is coming).
TEST_F(NavigationSystemTest, FarColonistDoesNotHoldForWaterNobodyWalkedTo) {
	WalkRig			rig({0.0F, 0.0F});
	const glm::vec2 water{200.0F, 0.0F};
	addColonist(rig.world, {0.0F, 0.0F}, "Home");
	rig.start();

	const EntityID colonist = needyColonist(rig, {110.0F, 0.0F}, NeedType::Thirst, 10.0F);
	rig.world.getComponent<Memory>(colonist)->rememberWorldEntity(
		water, kTestWaterDefId, static_cast<std::uint16_t>(1U << static_cast<std::size_t>(assets::CapabilityType::Drinkable)));
	const Task& task = *rig.world.getComponent<Task>(colonist);
	for (int i = 0; i < 40; ++i) {
		rig.world.update(0.1F);
		ASSERT_NE(task.navState, NavState::AwaitingMesh) << "tick " << i << ": held for ground that is not coming";
		ASSERT_NE(task.targetPosition, water) << "tick " << i << ": set off for water it has no known way to";
	}
	ASSERT_FALSE(rig.nav.inSimArea(water));
	const EvaluatedOption* thirst = nullptr;
	for (const EvaluatedOption& option : rig.world.getComponent<DecisionTrace>(colonist)->options) {
		if (option.taskType == TaskType::FulfillNeed && option.needType == NeedType::Thirst) {
			thirst = &option;
		}
	}
	ASSERT_NE(thirst, nullptr);
	EXPECT_NE(thirst->status, OptionStatus::Selected);
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

// A colonist put on ground no mover has seen (what the dev teleport does) has it meshed under him on
// the next tick: his limit there is urgent, so it goes in outside every budget.
TEST_F(NavigationSystemTest, TeleportOntoUnseenGroundMeshesOnArrival) {
	WalkRig rig({0.0F, 0.0F});
	rig.start();
	const EntityID colonist = needyColonist(rig, {0.0F, 0.0F}, NeedType::Thirst, 100.0F);
	rig.world.update(0.1F);
	const glm::vec2 unseen{300.0F, 300.0F};
	ASSERT_FALSE(rig.nav.inSimArea(unseen));
	rig.world.getComponent<Position>(colonist)->value = unseen;
	rig.world.update(0.1F);
	const glm::vec2 at = rig.world.getComponent<Position>(colonist)->value;
	EXPECT_TRUE(rig.nav.isValidPosition(at)) << "not meshed a tick after the teleport";
	EXPECT_TRUE(rig.nav.inSimArea(at + glm::vec2{kDefaultSightRadius, 0.0F})) << "what he sees is not meshed";
}

// A walker's route is restamped as ground lands round it, never asked for again: his own limits go
// in as he walks past what the lookout meshed, and a lookout's far off, and the order is his only
// request.
TEST_F(NavigationSystemTest, WalkingRouteIsNotAskedAgainAsLimitsLand) {
	WalkRig				rig({0.0F, 0.0F});
	rig.start();
	const std::uint64_t asked	   = rig.nav.routeStats().requests;
	const std::uint64_t generation = rig.nav.generation();
	const glm::vec2		goal{20.0F, 0.0F};
	const EntityID		colonist = rig.walker({-20.0F, 0.0F}, goal);
	const auto			at		 = [&rig, colonist] { return rig.world.getComponent<Position>(colonist)->value; };
	for (int i = 0; i < 600 && glm::distance(at(), goal) > 0.3F; ++i) {
		if (i == 10) {
			meshDisc(rig.world, {0.0F, 300.0F}, kColonistRange);
		}
		rig.world.update(0.1F);
	}
	EXPECT_LT(glm::distance(at(), goal), 0.3F) << "never arrived";
	EXPECT_GE(rig.nav.generation() - generation, 3U) << "too little ground landed during the walk to tell";
	EXPECT_EQ(rig.nav.routeStats().requests - asked, 1U) << "his route was asked for again as ground landed";
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

// Knowing nearer water in ground no mover wants (so it isn't coming) and a farther source on ground
// walked to, the colonist takes the farther.
TEST_F(NavigationSystemTest, FartherReachableWaterBeatsNearerWaterInGroundThatIsntComing) {
	const glm::vec2 at{1450.0F, 10.0F};
	const glm::vec2 gone{1540.0F, 10.0F};	// chunk (3, 0), beyond the loaded radius and every limit, 90 m off
	const glm::vec2 meshed{1350.0F, 10.0F}; // chunk (2, 0), 100 m off
	WalkRig			rig(meshed);
	meshBand(rig.world, {meshed, at});
	rig.start();
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
	// Wider than the walker's own limit, so the ground he would take is meshed already.
	meshDisc(world, {0.0F, 0.0F}, 2.0F * (kDefaultSightRadius + NavigationSystem::kSightBufferMeters));
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

// A route a wall was built across is stale; an unrelated wall job, which restamps the routes it
// doesn't cross, leaves it stale.
TEST_F(NavigationSystemTest, StaleRouteStaysStaleThroughAnUnrelatedWallJob) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	settle(sys);
	const EntityID hit	  = routedMover(world, sys, {1.0F, 40.0F}, {31.0F, 40.0F});
	const EntityID missed = routedMover(world, sys, {1.0F, 10.0F}, {31.0F, 10.0F});
	buildWall(cw, {10000, 37000}, {10000, 43000});
	step(world, sys);
	settle(sys);
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
	placeNothingElsewhere(executor, &processed);

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
	// The reach of the limit's obstacle query for its R (the limit's circumradius plus
	// kFootprintReachMeters), from up to a retake step away.
	const auto reach = [](float r) {
		return static_cast<float>(geometry::nav::limitCircumradius(nav::toMm(glm::vec2{r, 0.0F}).x)) / 1000.0F + NavigationSystem::kFootprintReachMeters +
			   static_cast<float>(NavigationSystem::kLimitRetakeStepMm) / 1000.0F;
	};
	EXPECT_EQ(anchors[0].position.x, 10.0F);
	EXPECT_EQ(anchors[0].rangeMeters, reach(kDefaultSightRadius + NavigationSystem::kSightBufferMeters));
	EXPECT_EQ(anchors[1].position.x, 20.0F);
	EXPECT_EQ(anchors[1].rangeMeters, reach(200.0F + NavigationSystem::kSightBufferMeters)) << "R is never clamped";
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
	// Placed obstacles no field mover has seen, and a still lookout beside them (lateLookout, sight 0)
	// whose limit holds them only once his sight grows to kLateSight. That is short of urgent: what he
	// then sees stays inside his first limit, so the wider limit is one a search's hold keeps waiting.
	const glm::vec2 kLateTrees{300.0F, 300.0F};
	constexpr float kLateSight = NavigationSystem::kSightBufferMeters - static_cast<float>(NavigationSystem::kLimitRetakeStepMm) / 1000.0F - 1.0F;
	const glm::vec2 kFarMover  = kLateTrees + glm::vec2{NavigationSystem::kSightBufferMeters + kLateSight - 1.0F, 0.0F};

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

	// The still lookout at kFarMover, his ground meshed: it stops short of kLateTrees.
	EntityID lateLookout(RouteField& field) {
		const EntityID lookout = meshDisc(field.world, kFarMover, NavigationSystem::kSightBufferMeters);
		field.sys.finishPendingBuilds();
		EXPECT_TRUE(field.sys.inSimArea(kFarMover));
		EXPECT_FALSE(field.sys.inSimArea(kLateTrees));
		return lookout;
	}

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

// While a search spans ticks, only limits that are not urgent wait: a still mover whose sight grows
// short of urgent takes a limit that waits, while a mover's first limit, a felled obstacle, and a
// wall go in at the normal rate. Once it has answered, what waited goes in, all
// of it.
TEST_F(NavigationSystemTest, WhileARouteSpansTicksOnlyLimitsInsideServedGroundWait) {
	RouteField		  field;
	NavigationSystem& sys	   = field.sys;
	const EntityID	  lookout  = lateLookout(field);
	const nav::RouteTicket ticket = sys.requestRoute(kBarStart, std::array{kBarGoal}, kAgentRadius);
	field.tick();
	ASSERT_EQ(sys.routeStats().ticksHeld, 0U) << "nothing has run across a tick yet";

	meshAround(field.world, kFarGround, 10.0F);
	field.world.getComponent<Memory>(lookout)->sightRadius = kLateSight;
	const glm::vec2 felled = fieldTree(30, 30);
	ASSERT_FALSE(meshClear(sys, felled));
	field.fell(felled);
	const glm::vec2 wallMid{132.0F, 60.0F};
	buildWall(field.cw, mm({130.0F, 60.0F}), mm({134.0F, 60.0F}));
	field.tick();
	ASSERT_EQ(sys.routeStats().ticksHeld, 1U) << "the search ran in the last slice and goes on";
	EXPECT_TRUE(meshClear(sys, felled)) << "the felled obstacle is out at once";
	EXPECT_TRUE(wallAt(sys, wallMid)) << "the wall is in at once";
	EXPECT_TRUE(sys.inSimArea(kFarGround)) << "a mover's first limit goes in";
	EXPECT_FALSE(sys.inSimArea(kLateTrees)) << "the still lookout's wider limit waits";

	std::optional<RouteAnswer> answer;
	for (int ticks = 0; ticks < 60 && !answer.has_value(); ++ticks) {
		field.tick();
		answer = sys.takeRouteAnswer(ticket);
		if (!answer.has_value()) {
			EXPECT_FALSE(sys.inSimArea(kLateTrees)) << "tick " << ticks;
		}
	}
	ASSERT_TRUE(answer.has_value());
	EXPECT_EQ(answer->outcome, nav::RouteOutcome::Routed);
	for (int i = 0; i < 60 && sys.isBuilding(); ++i) {
		field.tick();
	}
	EXPECT_FALSE(sys.isBuilding()) << "the held limit went in";
	for (int k = 0; k < 5; ++k) {
		EXPECT_TRUE(obstacleMeshed(sys, kLateTrees + glm::vec2{2.0F * static_cast<float>(k), 0.0F})) << "held obstacle " << k;
	}
}

// Each search holds the limits of movers inside their served ground for kMaxHeldAdditionTicks of its
// life, from the first tick it is still running after a slice, and a restart does not count again.
// Long searches asked back to back over several ticks hold until the last one's window closes and not
// a tick longer, though every one of them still runs; then the held limit goes in.
TEST_F(NavigationSystemTest, EachSearchHoldsLimitsForItsOwnWindowOnly) {
	RouteField		  field;
	NavigationSystem& sys	  = field.sys;
	const EntityID	  lookout = lateLookout(field);
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
			field.world.getComponent<Memory>(lookout)->sightRadius = kLateSight;
		}
		if (update == 20) {
			field.fell(fieldTree(2, 2)); // beside every flood's start: each restarts
		}
		field.tick();
		if (update >= 3 && update <= kLastHeld) {
			ASSERT_FALSE(sys.inSimArea(kLateTrees)) << "update " << update;
		}
	}
	EXPECT_EQ(sys.routeStats().ticksHeld, static_cast<std::uint64_t>(kLastHeld - 1));
	EXPECT_EQ(sys.routeStats().longestHold, static_cast<std::uint32_t>(kLastHeld - 1));

	for (int i = 0; i < 60 && sys.isBuilding(); ++i) {
		field.tick();
	}
	EXPECT_FALSE(sys.isBuilding()) << "the held limit went in";
	for (int k = 0; k < 5; ++k) {
		EXPECT_TRUE(obstacleMeshed(sys, kLateTrees + glm::vec2{2.0F * static_cast<float>(k), 0.0F})) << "held obstacle " << k;
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

// A mover walking out of meshed ground at the fastest game speed while searches hold every limit
// that is not urgent: each tick what he sees is meshed, because his limit goes in the tick it turns
// urgent, through the hold.
TEST_F(NavigationSystemTest, UrgentLimitLandsThroughAHold) {
	RouteField		  field;
	NavigationSystem& sys = field.sys;
	TimeSystem&		  time = field.world.registerSystem<TimeSystem>();
	time.setSpeed(GameSpeed::VeryFast);
	const float		stride = MovementTarget{}.speed * time.effectiveTimeScale() * kTick;
	const glm::vec2 from{64.0F, 130.0F}; // beside the field, his limit reaching past its meshed ground
	const EntityID	walker = field.world.createEntity();
	field.world.addComponent<Position>(walker, Position{from});
	field.world.addComponent<AgentRadius>(walker, AgentRadius{});
	sys.finishPendingBuilds();
	ASSERT_TRUE(sightMeshed(sys, from, kDefaultSightRadius));

	const auto flood = [&] {
		for (int k = 0; k < 8; ++k) {
			static_cast<void>(sys.requestRoute(gapPoint(2, 2), kInRoom, kAgentRadius));
		}
	};
	flood();
	field.tick();
	field.tick();
	ASSERT_GT(sys.routeStats().ticksHeld, 0U);
	sys.resetNavQueryStats();

	Position& position = *field.world.getComponent<Position>(walker);
	for (int tick = 0; position.value.y < from.y + 4.0F * NavigationSystem::kSightBufferMeters; ++tick) {
		if (tick % 30 == 0) {
			flood();
		}
		const std::uint64_t held = sys.routeStats().ticksHeld;
		position.value.y += stride;
		field.tick();
		ASSERT_EQ(sys.routeStats().ticksHeld, held + 1) << "tick " << tick << ": nothing held";
		ASSERT_TRUE(sightMeshed(sys, position.value, kDefaultSightRadius)) << "tick " << tick;
	}
	EXPECT_GT(sys.navQueryStats().urgentLimits, 0U);
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

// A start on no meshed ground and in no limit on its way is no known way, at once, with no search:
// nothing says ground is coming there, so nothing waits for it.
TEST_F(NavigationSystemTest, RouteFromGroundNotComingIsNoKnownWay) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	settle(sys);
	const nav::RouteTicket			 ticket = sys.requestRoute(kUnmeshed, std::array{glm::vec2{10.0F, 10.0F}}, kAgentRadius);
	const std::optional<RouteAnswer> answer = sys.takeRouteAnswer(ticket);
	ASSERT_TRUE(answer.has_value()) << "answered at once";
	EXPECT_EQ(answer->outcome, nav::RouteOutcome::NoKnownWay);
	EXPECT_EQ(answer->expansions, 0);
	EXPECT_EQ(answer->patchEpoch, sys.navMesh().epoch());
}

namespace {
	// The outcome of a route asked between ticks, answered at once.
	nav::RouteOutcome outcomeAtOnce(NavigationSystem& sys, glm::vec2 from, glm::vec2 to) {
		const nav::RouteTicket			 ticket = sys.requestRoute(from, std::array{to}, kAgentRadius);
		const std::optional<RouteAnswer> answer = sys.takeRouteAnswer(ticket);
		EXPECT_TRUE(answer.has_value()) << "not answered at once";
		return answer.has_value() ? answer->outcome : nav::RouteOutcome::Routed;
	}
} // namespace

// A start or goal off meshed ground inside a limit on its way in (gathered this tick, or queued behind
// the per-tick budget) waits for it (AwaitingMesh); one in no limit is no known way. Once the limits
// land, the same goals route.
TEST_F(NavigationSystemTest, GoalInQueuedLimitAwaitsMesh) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	const glm::vec2 home{0.0F, 0.0F};
	meshDisc(world, home, kColonistRange);
	settle(sys);

	const glm::vec2 east{kColonistRange, 0.0F};
	const glm::vec2 north{0.0F, kColonistRange};
	meshDisc(world, east, kColonistRange);
	meshDisc(world, north, kColonistRange);
	const glm::vec2 eastGoal  = east + glm::vec2{kColonistRange * 0.75F, 0.0F};
	const glm::vec2 northGoal = north + glm::vec2{0.0F, kColonistRange * 0.75F};
	const glm::vec2 nowhere{-2.0F * kColonistRange, -2.0F * kColonistRange};
	for (const glm::vec2 p : {eastGoal, northGoal, nowhere}) {
		ASSERT_FALSE(sys.inSimArea(p)) << p.x << ", " << p.y;
	}

	sys.update(0.016F);
	EXPECT_EQ(outcomeAtOnce(sys, home, eastGoal), nav::RouteOutcome::AwaitingMesh);
	EXPECT_EQ(outcomeAtOnce(sys, home, northGoal), nav::RouteOutcome::AwaitingMesh);
	EXPECT_EQ(outcomeAtOnce(sys, eastGoal, home), nav::RouteOutcome::AwaitingMesh) << "a start in ground on its way";
	EXPECT_EQ(outcomeAtOnce(sys, home, nowhere), nav::RouteOutcome::NoKnownWay) << "a goal in no limit";
	EXPECT_EQ(outcomeAtOnce(sys, nowhere, home), nav::RouteOutcome::NoKnownWay) << "a start in no limit";
	sys.endTick();

	settle(sys);
	EXPECT_TRUE(routeBetween(sys, home, eastGoal).has_value());
	EXPECT_TRUE(routeBetween(sys, home, northGoal).has_value());
	EXPECT_EQ(outcomeAtOnce(sys, home, nowhere), nav::RouteOutcome::NoKnownWay);
}

// A goal waiting in a queued limit whose mover walks off before it goes in: the limit can never go in
// and is dropped, and awaitedGroundEpoch moves with no ground landing, so the waiter asks again and
// reads no known way. Nothing waits for ground that is not coming.
TEST_F(NavigationSystemTest, DroppedLimitReleasesItsWaiters) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	const glm::vec2 home{0.0F, 0.0F};
	meshDisc(world, home, kColonistRange);
	settle(sys);

	for (std::size_t i = 0; i < NavigationSystem::kLimitsPerTick; ++i) { // this tick's limit budget
		meshDisc(world, {kColonistRange, kColonistRange * static_cast<float>(i)}, kColonistRange);
	}
	const glm::vec2 away{600.0F, 0.0F};
	const EntityID	walker = meshDisc(world, away, kColonistRange);
	const glm::vec2 goal   = away + glm::vec2{kColonistRange * 0.5F, 0.0F};
	sys.update(0.016F);
	sys.endTick();
	const std::uint64_t generation = sys.generation();
	const std::uint64_t epoch	   = sys.awaitedGroundEpoch();
	ASSERT_EQ(outcomeAtOnce(sys, home, goal), nav::RouteOutcome::AwaitingMesh) << "the walker's limit is still queued";

	world.getComponent<Position>(walker)->value = {-600.0F, 0.0F};
	sys.update(0.016F);
	EXPECT_EQ(sys.generation(), generation) << "no ground went in";
	EXPECT_NE(sys.awaitedGroundEpoch(), epoch) << "the dropped limit released nobody";
	EXPECT_EQ(outcomeAtOnce(sys, home, goal), nav::RouteOutcome::NoKnownWay);
	sys.endTick();
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

// Nav's snap and the collision system run together on three movers: one on a route, set down on
// unseen ground inside a placed obstacle that comes in with that ground; one whose route ends inside a
// placed obstacle (nothing clear ahead); one with no route and a velocity, set down on unseen ground
// inside another. None ends a tick further back along its route or heading than it started it; the
// one with nothing clear ahead stays, is held (collision doesn't push it), and steps out, not behind
// its leg, once no new route has come.
TEST_F(ForwardSnapTest, SnapAndCollisionNeverTakeAMoverBack) {
	auto& collision = world.registerSystem<StaticRectCollisionSystem>();
	collision.setPlacementData(&executor);
	collision.setDetailSource(sys);
	// Chunks (-1, 0) and (-1, -1), past the ground the fixture meshed.
	const glm::vec2 onRouteTree{-200.0F, 20.0F};
	const glm::vec2 movingTree{-180.0F, -20.0F};
	ASSERT_FALSE(sys->inSimArea(onRouteTree));
	ASSERT_FALSE(sys->inSimArea(movingTree));
	placeTrees(executor, processed, "Test_NavSnapTree", {onRouteTree, movingTree});
	const EntityID	onRoute	  = routedColonist(world, onRouteTree + glm::vec2{-0.3F, 0.0F}, {onRouteTree - glm::vec2{4.0F, 0.0F}, onRouteTree + glm::vec2{4.0F, 0.0F}});
	const EntityID	noneAhead = routedColonist(world, kTree + glm::vec2{-0.3F, 0.0F}, {{6.0F, 10.0F}, {9.9F, 10.0F}});
	const EntityID	moving	  = routedColonist(world, movingTree + glm::vec2{-0.3F, 0.0F}, {});
	world.addComponent<Velocity>(moving, Velocity{{2.0F, 0.0F}});

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
				if (at[k] < 0 && obstacleMeshed(sys, trees[k])) {
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

// A mover that appears has his limit's ground and every placed obstacle in it land on one tick: none
// is seen before the ground under it, and the ground never lands ahead of one.
TEST_F(NavigationSystemTest, SpawnedMoversObstaclesLandWithHisGround) {
	const std::vector<int> at	= spawnedMoverLandings();
	constexpr std::size_t  kOwn = 24; // the middle of 7 x 7
	ASSERT_GE(at[kOwn], 0);
	for (std::size_t k = 0; k < at.size(); ++k) {
		if (at[k] >= 0) {
			EXPECT_EQ(at[k], at[kOwn]) << "tree " << k;
		}
	}
}

// The same appearance twice lands every tree on the same tick.
TEST_F(NavigationSystemTest, SameAppearanceLandsOnTheSameTicks) {
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
			in.push_back(obstacleMeshed(sys, t));
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

// A still mover in open ground meshes his limit and nothing else: its corners are the mesh's only
// vertices, so the mesh ends at his limit with no point the game did not put there.
TEST_F(NavigationSystemTest, StillMoverMeshIsHisLimit) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setConstructionWorld(&cw);
	const glm::vec2 at{200.0F, 200.0F};
	addColonist(world, at, "Still");
	settle(sys);
	const float								  r = kDefaultSightRadius + NavigationSystem::kSightBufferMeters;
	const std::vector<Vec2i64>				  corners = geometry::nav::limitPolygon(nav::toMm(at), nav::toMm(glm::vec2{r, 0.0F}).x);
	const geometry::nav::PatchableMesh&		  mesh	  = sys.navMesh();
	std::set<std::pair<std::int64_t, std::int64_t>> used;
	for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
		if (mesh.triangleLive(t)) {
			for (const std::int32_t v : mesh.triangles()[static_cast<std::size_t>(t)].v) {
				const Vec2i64& p = mesh.vertices()[static_cast<std::size_t>(v)];
				used.insert({p.x, p.y});
			}
		}
	}
	std::set<std::pair<std::int64_t, std::int64_t>> expected;
	for (const Vec2i64& c : corners) {
		expected.insert({c.x, c.y});
	}
	EXPECT_EQ(used, expected);
}

// A mover walking at 2 m/s through ground thick with placed obstacles, under the default budgets: each
// one ahead of him is in the mesh before he comes within his sight of it, every one within R minus
// kLimitRetakeStepMm of his path is in, and the mesh ends short of anything farther from his path
// than his limit's circumradius.
TEST_F(NavigationSystemTest, WalkingMoverMeshesAheadOfHisSight) {
	registerTree("Test_NavWalkTree", 0.3F);
	const float kStep	  = static_cast<float>(NavigationSystem::kLimitRetakeStepMm) / 1000.0F;
	const float kR		  = kDefaultSightRadius + NavigationSystem::kSightBufferMeters;
	const float kFarthest = static_cast<float>(geometry::nav::limitCircumradius(nav::toMm(glm::vec2{kR, 0.0F}).x)) / 1000.0F + 1.0F;
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
		landed[k] = obstacleMeshed(sys, trees[k]);
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
			landed[k] = obstacleMeshed(sys, trees[k]);
			ASSERT_TRUE(landed[k] || glm::distance(p, trees[k]) > kDefaultSightRadius)
				<< "the tree at (" << trees[k].x << ", " << trees[k].y << ") is in sight at x = " << p.x << " and not meshed";
		}
	}
	settle(sys);
	for (std::size_t k = 0; k < trees.size(); ++k) {
		const glm::vec2 t		 = trees[k];
		const float		fromPath = glm::distance(t, glm::vec2{std::clamp(t.x, from.x, to.x), from.y});
		if (fromPath <= kR - kStep) {
			EXPECT_TRUE(obstacleMeshed(sys, t)) << "the obstacle at (" << t.x << ", " << t.y << ") within R of the path";
		}
		if (fromPath > kFarthest) {
			EXPECT_FALSE(sys.inSimArea(t)) << "the obstacle at (" << t.x << ", " << t.y << ") " << fromPath << " m from the path";
		}
	}
}

namespace {
	// A colonist walking `route` (from its first point) under MovementSystem, his task Moving.
	EntityID routeWalker(World& world, const std::vector<glm::vec2>& route) {
		const EntityID e = addColonist(world, route.front(), "Walker");
		world.addComponent<Velocity>(e, Velocity{});
		world.addComponent<MovementTarget>(e, MovementTarget{.target = route.back(), .active = true});
		world.getComponent<Task>(e)->state = TaskState::Moving;
		NavPath path;
		path.waypoints = route;
		path.current   = 1;
		path.valid	   = true;
		world.addComponent<NavPath>(e, std::move(path));
		return e;
	}
} // namespace

// Many movers walking through placed obstacles, at a walk and at the fastest game speed, never see
// past the mesh: checked every tick for each. At speed there are more limits than the per-tick
// budgets take, and the ones that fall behind go in as they turn urgent.
TEST_F(NavigationSystemTest, WalkersNeverSeePastTheMesh) {
	registerTree("Test_NavCrowdWalkTree", 0.3F);
	constexpr int	kWalkers = 16;
	constexpr float kLane	 = 30.0F;
	constexpr float kTick	 = 1.0F / 60.0F;
	const float		r		 = kDefaultSightRadius + NavigationSystem::kSightBufferMeters;
	std::vector<glm::vec2> trees;
	for (float x = 2.0F; x < 280.0F; x += 4.0F) {
		for (float y = 2.0F; y < kLane * static_cast<float>(kWalkers + 1); y += 4.0F) {
			trees.push_back({x, y});
		}
	}

	const auto walk = [&](GameSpeed speed, float distance) -> std::uint64_t {
		PlacementExecutor					executor(AssetRegistry::Get());
		std::unordered_set<ChunkCoordinate> processed;
		placeTrees(executor, processed, "Test_NavCrowdWalkTree", trees);
		World			  world;
		NavigationSystem& sys  = world.registerSystem<NavigationSystem>();
		TimeSystem&		  time = world.registerSystem<TimeSystem>();
		time.setSpeed(speed);
		sys.setChunkManager(landChunks());
		sys.setPlacementData(&executor, &processed);
		std::vector<EntityID> walkers;
		for (int i = 0; i < kWalkers; ++i) {
			walkers.push_back(addColonist(world, {r, kLane * static_cast<float>(i + 1)}, "Walker"));
		}
		settle(sys);
		sys.resetNavQueryStats();
		const float stride = MovementTarget{}.speed * time.effectiveTimeScale() * kTick;
		for (int tick = 0; static_cast<float>(tick) * stride < distance; ++tick) {
			for (const EntityID w : walkers) {
				world.getComponent<Position>(w)->value.x += stride;
			}
			world.update(kTick);
			for (const EntityID w : walkers) {
				const ::testing::AssertionResult meshed = sightMeshed(sys, world.getComponent<Position>(w)->value, kDefaultSightRadius);
				if (!meshed) {
					ADD_FAILURE() << "tick " << tick << ": " << meshed.message();
					return 0;
				}
			}
		}
		return sys.navQueryStats().urgentLimits;
	};
	walk(GameSpeed::Normal, 2.0F * NavigationSystem::kSightBufferMeters);
	EXPECT_GT(walk(GameSpeed::VeryFast, 8.0F * NavigationSystem::kSightBufferMeters), 0U) << "nothing fell behind the budgets";
}

// A mover whose urgent limit cannot go in (its chunk's placement is not ready) stops where what he
// sees still lies in his last limit in the mesh, a step past where it turned urgent: he neither
// slides nor steps back, and keeps his task and route. When the placement lands, so does the limit,
// and he walks on along the same route to its end.
TEST_F(NavigationSystemTest, MoverWhoseUrgentLimitCannotLandStopsShortAndWalksOn) {
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	placeNothingElsewhere(executor, &processed);
	const ChunkCoordinate late{1, 0};
	processed.erase(late);
	const float edge = static_cast<float>(world::kChunkSize); // chunk (1, 0) starts here

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	world.registerSystem<MovementSystem>();
	world.registerSystem<PhysicsSystem>();
	world.registerSystem<TimeSystem>().setSpeed(GameSpeed::VeryFast);
	sys.setChunkManager(landChunks());
	sys.setPlacementData(&executor, &processed);
	const glm::vec2				 from{edge - 100.0F, 100.0F};
	const std::vector<glm::vec2> route{from, {edge + 60.0F, 100.0F}};
	const EntityID				 walker = routeWalker(world, route);
	settle(sys);

	// The limits whose obstacle query stays short of the edge go in; the last such centre is within a
	// step short of that, and he stops R minus his sight past it.
	const float r		= kDefaultSightRadius + NavigationSystem::kSightBufferMeters;
	const float step	= static_cast<float>(NavigationSystem::kLimitRetakeStepMm) / 1000.0F;
	const float reach	= static_cast<float>(geometry::nav::limitCircumradius(nav::toMm(glm::vec2{r, 0.0F}).x)) / 1000.0F + NavigationSystem::kFootprintReachMeters;
	const float stride	= MovementTarget{}.speed * world.getSystem<TimeSystem>().effectiveTimeScale() / 60.0F;
	const float stopMin = edge - reach - step - stride + (r - kDefaultSightRadius);
	const float stopMax = edge - reach + (r - kDefaultSightRadius);

	const Position& position = *world.getComponent<Position>(walker);
	const NavPath&	path	 = *world.getComponent<NavPath>(walker);
	float			last	 = position.value.x;
	int				still	 = 0;
	for (int tick = 0; tick < 600 && still < 60; ++tick) {
		world.update(1.0F / 60.0F);
		ASSERT_TRUE(sightMeshed(sys, position.value, kDefaultSightRadius)) << "tick " << tick;
		ASSERT_GE(position.value.x, last) << "tick " << tick << ": stepped back";
		ASSERT_EQ(position.value.y, from.y) << "tick " << tick << ": slid";
		still = position.value.x == last ? still + 1 : 0;
		last  = position.value.x;
	}
	ASSERT_EQ(still, 60) << "he never stopped";
	EXPECT_GE(last, stopMin);
	EXPECT_LT(last, stopMax);
	EXPECT_EQ(world.getComponent<Velocity>(walker)->value, glm::vec2(0.0F, 0.0F));
	EXPECT_EQ(world.getComponent<Task>(walker)->state, TaskState::Moving);
	EXPECT_TRUE(path.valid);
	EXPECT_EQ(path.waypoints, route);
	EXPECT_EQ(path.current, 1U);

	processed.insert(late);
	for (int tick = 0; tick < 600 && world.getComponent<Task>(walker)->state == TaskState::Moving; ++tick) {
		world.update(1.0F / 60.0F);
		ASSERT_TRUE(sightMeshed(sys, position.value, kDefaultSightRadius)) << "tick " << tick;
		ASSERT_GE(position.value.x, last) << "tick " << tick << ": stepped back";
		last = position.value.x;
	}
	EXPECT_EQ(world.getComponent<Task>(walker)->state, TaskState::Arrived);
	EXPECT_EQ(path.waypoints, route) << "walked on along the same route";
}

// A mover walking in from the edge of ground another mover's limit meshed: his first limits reach
// past it and go in as his own, and once he is deep inside, the mesh holds his limits as he takes
// them. None goes urgent and he never stops, though his own last limit in the mesh falls far behind.
TEST_F(NavigationSystemTest, MoverInsideGroundOthersMeshedIsNeitherUrgentNorStopped) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	world.registerSystem<MovementSystem>();
	world.registerSystem<PhysicsSystem>();
	TimeSystem& time = world.registerSystem<TimeSystem>();
	time.setSpeed(GameSpeed::VeryFast);
	sys.setChunkManager(landChunks());
	const float r = kDefaultSightRadius + NavigationSystem::kSightBufferMeters;
	meshDisc(world, kAreaCentre, 6.0F * r);
	settle(sys);
	sys.resetNavQueryStats();
	const std::vector<glm::vec2> route{kAreaCentre - glm::vec2{5.5F * r, 0.0F}, kAreaCentre - glm::vec2{2.5F * r, 0.0F}};
	const EntityID				 walker = routeWalker(world, route);
	settle(sys);
	ASSERT_GT(sys.navQueryStats().groundWaterWallPatches, 0U) << "his first limit reaches past the meshed ground";
	sys.resetNavQueryStats();

	const float		stride	 = MovementTarget{}.speed * time.effectiveTimeScale() / 60.0F;
	const Position& position = *world.getComponent<Position>(walker);
	for (int tick = 0; tick < 600 && world.getComponent<Task>(walker)->state == TaskState::Moving; ++tick) {
		const float before = position.value.x;
		world.update(1.0F / 60.0F);
		if (before < route.back().x - stride) {
			ASSERT_GT(position.value.x - before, 0.5F * stride) << "tick " << tick << ": held back";
		}
	}
	EXPECT_EQ(world.getComponent<Task>(walker)->state, TaskState::Arrived);
	EXPECT_EQ(sys.navQueryStats().urgentLimits, 0U);
}

// Limits that are not urgent go in kLimitsPerTick a tick, first in, first out.
TEST_F(NavigationSystemTest, LimitsThatAreNotUrgentGoInUnderTheirBudget) {
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	const float			   apart = 4.0F * (kDefaultSightRadius + NavigationSystem::kSightBufferMeters);
	std::vector<glm::vec2> at;
	for (int k = 0; k < 4; ++k) {
		at.push_back({apart * static_cast<float>(k), 0.0F});
		meshDisc(world, at.back(), kDefaultSightRadius + NavigationSystem::kSightBufferMeters);
	}
	for (std::size_t tick = 1; tick <= at.size(); ++tick) {
		step(world, sys);
		const std::size_t landed = static_cast<std::size_t>(std::count_if(at.begin(), at.end(), [&](glm::vec2 p) { return sys.inSimArea(p); }));
		EXPECT_EQ(landed, std::min(at.size(), tick * NavigationSystem::kLimitsPerTick)) << "tick " << tick;
		for (std::size_t k = 0; k < landed; ++k) {
			EXPECT_TRUE(sys.inSimArea(at[k])) << "out of order at tick " << tick;
		}
	}
}

// A limit that can never go in (its mover moved on and nothing holds its chunk) is dropped: it holds
// back nothing behind it, and its ground stays unmeshed once its chunk's placement is ready.
TEST_F(NavigationSystemTest, StrandedLimitIsDroppedAndBlocksNothing) {
	PlacementExecutor					executor(AssetRegistry::Get());
	std::unordered_set<ChunkCoordinate> processed;
	placeNothingElsewhere(executor, &processed);
	const ChunkCoordinate late{-2, 0};
	processed.erase(late);

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(landChunks());
	sys.setPlacementData(&executor, &processed);
	const glm::vec2 first{-800.0F, 100.0F};
	const glm::vec2 second{800.0F, 100.0F};
	const EntityID	mover = addColonist(world, first, "Mover");
	step(world, sys);
	step(world, sys);
	ASSERT_FALSE(sys.inSimArea(first)) << "its placement is not ready";

	world.getComponent<Position>(mover)->value = second;
	step(world, sys);
	EXPECT_TRUE(sys.inSimArea(second)) << "the limit behind the stranded one went in on the next tick";
	processed.insert(late);
	settle(sys);
	EXPECT_FALSE(sys.inSimArea(first)) << "the stranded limit was dropped";
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
	const glm::vec2 walkedTo{400.0F, 400.0F};
	meshBand(rig.world, {pocket, walkedTo});
	settle(rig.nav);
	ASSERT_TRUE(rig.nav.isValidPosition(pocket));
	ASSERT_FALSE(rig.nav.isValidPosition(trees.front())) << "the square is in the mesh";
	const glm::vec2 open{80.0F, 100.0F};
	EXPECT_TRUE(rig.nav.connected(open, {120.0F, 90.0F}));
	EXPECT_TRUE(rig.nav.connected(open, walkedTo)) << "the ground walked to is meshed";
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
	meshBand(world, {{100.0F, 240.0F}, {300.0F, 100.0F}});
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
	// Limits three ranges apart, in chunks (0, 0) and (1, 0); the walker's limit from between overlaps both.
	constexpr float kRange = 40.0F;
	const glm::vec2 west{440.0F, 256.0F};
	const glm::vec2 east	= west + glm::vec2{3.0F * kRange, 0.0F};
	const glm::vec2 between = west + glm::vec2{1.5F * kRange, 0.0F};
	const EntityID	walker	= meshDisc(world, west, kRange);
	meshDisc(world, east, kRange);
	loadFor(*chunks, sys, camera);
	settle(sys);
	ASSERT_TRUE(sys.inSimArea(west));
	ASSERT_TRUE(sys.inSimArea(east));
	ASSERT_FALSE(sys.inSimArea(between));
	EXPECT_FALSE(sys.connected(west, east));

	world.getComponent<Position>(walker)->value = between;
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

// A limit with more placed obstacles than a tick's budget brings them in over several ticks ahead of
// its ground, hanging over unseen ground: they close nothing anyone can reach, so the epoch stays
// until the ground comes.
TEST_F(NavigationSystemTest, ObstaclesAheadOfTheirGroundLeaveTheEpoch) {
	registerTree("Test_NavEpochObstacle", 0.4F);
	WalkRig rig({10.0F, 10.0F});
	settle(rig.nav);
	const glm::vec2		   far{200.0F, 10.0F};
	std::vector<glm::vec2> obstacles; // nine to a row, 1.2 m apart
	for (std::size_t k = 0; k <= 2 * NavigationSystem::kDetailRingsPerTick; ++k) {
		obstacles.push_back(far + glm::vec2{1.2F * static_cast<float>(k % 9) - 4.8F, 1.2F * static_cast<float>(k / 9) - 4.8F});
	}
	placeTrees(rig.executor, rig.processed, "Test_NavEpochObstacle", obstacles);
	const std::uint64_t before = rig.nav.connectivityEpoch();
	meshDisc(rig.world, far, NavigationSystem::kSightBufferMeters);
	int hanging = 0;
	for (int tick = 0; tick < 10 && !rig.nav.inSimArea(far); ++tick) {
		step(rig.world, rig.nav);
		if (!rig.nav.inSimArea(far)) {
			++hanging;
			EXPECT_EQ(rig.nav.connectivityEpoch(), before) << "tick " << tick;
		}
	}
	ASSERT_TRUE(rig.nav.inSimArea(far));
	EXPECT_GE(hanging, 2) << "the obstacles came in over ticks ahead of the ground";
	for (const glm::vec2 t : obstacles) {
		EXPECT_TRUE(obstacleMeshed(rig.nav, t)) << "the obstacle at (" << t.x << ", " << t.y << ")";
	}
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
