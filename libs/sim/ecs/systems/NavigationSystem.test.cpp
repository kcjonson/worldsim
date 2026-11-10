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
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <set>
#include <string>
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
	// requested. Mirrors NavInputBuilder.test.cpp's end-to-end room. Returns the
	// south wall's segment id so callers can mutate it later.
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

	// First built region's mesh. These tests drive a single region, so the
	// first built region IS that region's mesh. Returns a shared empty mesh when none built.
	const geometry::nav::NavMesh& firstMesh(const NavigationSystem& sys) {
		static const geometry::nav::NavMesh kEmpty;
		const std::vector<NavigationSystem::RegionView> built = sys.builtRegions();
		return built.empty() ? kEmpty : *built.front().mesh;
	}

	// Mesh swaps of the camera's region. generation() counts colonist regions only.
	std::uint64_t cameraSwaps(const NavigationSystem& sys) {
		std::uint64_t swaps = 0;
		for (const NavigationSystem::RegionView& r : sys.builtRegions()) {
			if (r.owner == RegionOwner::Camera) {
				swaps += r.meshGeneration;
			}
		}
		return swaps;
	}

	// Standard query endpoints: just outside the south wall, and room center.
	const glm::vec2 kOutside{2.0F, -1.0F};
	const glm::vec2 kInside{2.0F, 1.5F};
	constexpr float kAgentRadius = 0.3F; // 0.3 m radius fits the 0.9 m door

	constexpr uint64_t kWorldSeed = 7;

	// All land: every tile is grassland, so the area build never invents water obstacles
	// around the origin room.
	std::unique_ptr<world::IWorldSampler> landSampler() {
		return std::make_unique<world::SyntheticWorldSampler>(kWorldSeed, world::SyntheticMap{});
	}

	// Mostly land with one pond: a blob of water centred at (40, 40), radius 8 m wobbled by its
	// rim sinusoids to 5.5..10.5 m (8 m due west, so its west shore is x = 32). Lets a test assert
	// isValidPosition is false on a real water face inside the region while a nearby land face is
	// valid.
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

	// All land but one pond centred on the chunk corner (0, 0), so it spans the four
	// chunks around it. Each chunk clips its navRing piece to its own square, and the
	// pieces meet at the corner: with only the diagonal chunks (0,0) and (-1,-1)
	// generated, the region's water is two pieces touching at one point.
	std::unique_ptr<world::IWorldSampler> cornerPondSampler() {
		return std::make_unique<world::SyntheticWorldSampler>(
			kWorldSeed,
			world::SyntheticMap{.pond = worldgen::PondNetwork2D::Pond{.cx = 0.0, .cy = 0.0, .radius = 10.0F, .phaseA = 0.7F, .phaseB = 2.1F, .depth = 120}}
		);
	}

	class NavigationSystemTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			ConstructionRegistry::Get().clear();
			ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder()));

			// A ready chunk region (all land) and a 60 m simulation area at the origin, so
			// the NavigationSystem build runs through the SIMULATION-AREA path. The origin
			// room (0..4 m x 0..3 m) sits well inside the area.
			m_chunks = std::make_unique<world::ChunkManager>(landSampler());
			m_chunks->setLoadRadius(2);
			m_chunks->setUnloadRadius(4);
			m_chunks->update({0.0F, 0.0F});
			m_chunks->finishPendingGeneration();
		}
		void TearDown() override { ConstructionRegistry::Get().clear(); }

		// Wire the area build (chunks + a 60 m colonist region at origin) onto a system in
		// addition to its ConstructionWorld, so the test exercises the simulation-region
		// path. Colonist drivers make it, since colonist queries consult colonist regions
		// only; the 3x5 m origin room sits well inside it.
		void wireArea(World& world, NavigationSystem& sys) {
			sys.setChunkManager(m_chunks.get());
			(void)driveColonistRegion(world, {0.0F, 0.0F}, 60.0F);
		}

		// Push an explicit viewport region (used by viewport-tracking tests). The viewport
		// is the region driver; center/half-extent are in mm, converted to the meters the
		// NavigationSystem API takes. A square region (equal X/Y half-extents).
		static void pushArea(NavigationSystem& sys, geometry::Vec2i64 centerMm, std::int64_t halfExtentMm) {
			const glm::vec2 centerM{static_cast<float>(centerMm.x) / 1000.0F,
									static_cast<float>(centerMm.y) / 1000.0F};
			const float halfM = static_cast<float>(halfExtentMm) / 1000.0F;
			sys.setViewportRect(centerM, glm::vec2{halfM, halfM});
		}

		std::unique_ptr<world::ChunkManager> m_chunks;
	};

} // namespace

// A query before any mesh is built returns nullopt (no inputs wired / first frame).
// isReachable returns true when there is no mesh: "can't prove unreachable."
// requestPath still returns nullopt because there is nothing to path over.
TEST_F(NavigationSystemTest, NoMeshBeforeBuildReturnsNullopt) {
	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();

	EXPECT_FALSE(sys.hasMesh());
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
	// No-mesh policy: isReachable cannot prove unreachable, so it returns true.
	EXPECT_TRUE(isReachable(sys, kOutside, kInside, kAgentRadius));
}

// update() with nothing wired is a safe no-op, and a query still returns nullopt.
TEST_F(NavigationSystemTest, UnwiredUpdateIsNoOp) {
	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();

	for (int i = 0; i < 10; ++i) {
		sys.update(0.016F);
	}
	EXPECT_FALSE(sys.hasMesh());
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
}

// A path from outside to inside succeeds THROUGH a built door.
TEST_F(NavigationSystemTest, PathThroughDoorSucceeds) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);

	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	std::optional<std::vector<glm::vec2>> path = sys.requestPath(kOutside, kInside, kAgentRadius);
	ASSERT_TRUE(path.has_value());
	EXPECT_GE(path->size(), 2u); // at least start..goal
	EXPECT_TRUE(isReachable(sys, kOutside, kInside, kAgentRadius));
}

// A window (non-pathable opening) leaves the wall solid: no path.
TEST_F(NavigationSystemTest, WindowBlocksPath) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/false);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);

	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
}

// No opening at all also blocks the path.
TEST_F(NavigationSystemTest, NoOpeningBlocksPath) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);

	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
}

// Rebuild-on-version-change: a closed room blocks the path; cutting a door bumps
// version(), and after the async rebuild lands the same query succeeds.
TEST_F(NavigationSystemTest, RebuildOnVersionChangeOpensPath) {
	ConstructionWorld cw;
	SegmentId south = buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);

	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "initial navmesh never built";
	ASSERT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value())
		<< "closed room should block the path";

	// Cut a door in the south wall. This bumps ConstructionWorld::version(), so the
	// next update() detects the change and launches a rebuild.
	const std::uint64_t before = cw.version();
	OpeningId op = cw.addOpening(south, 0.5F, "Door", "Wood");
	ASSERT_NE(op, kInvalidOpening);
	ASSERT_TRUE(cw.setOpeningState(op, FoundationState::Built));
	ASSERT_NE(cw.version(), before);

	sys.finishPendingBuilds();
	EXPECT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value()) << "door rebuild never opened the path";
}

// Rebuild the other direction: an open (door) room is pathable; setting the door
// opening back to Blueprint makes extractWalls treat the band as solid again, which
// bumps version() and should re-seal the room after the async rebuild lands.
TEST_F(NavigationSystemTest, RebuildSealsRoomWhenDoorRemoved) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);

	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "initial navmesh never built";
	ASSERT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value())
		<< "door room should be pathable";

	// Demote the door to Blueprint: a non-Built opening contributes nothing, so the
	// south band reverts to solid. This bumps version() and triggers a rebuild.
	const std::uint64_t before = cw.version();
	ASSERT_FALSE(cw.openings().empty());
	for (const auto& op : cw.openings()) {
		ASSERT_TRUE(cw.setOpeningState(op.id, FoundationState::Blueprint));
	}
	ASSERT_NE(cw.version(), before);

	sys.finishPendingBuilds();
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value()) << "blueprinting the door never re-sealed the room";
}

// Deferred wiring: pumping update() before the ConstructionWorld is set must NOT
// latch a mesh. Once the world is wired the first real build still happens.
// (Regression: an empty build used to latch haveBuiltOnce, after which no later
// version/chunk change would trigger the real build.)
TEST_F(NavigationSystemTest, ConstructionWorldWiredAfterUpdatesStillBuilds) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();

	// Frames with nothing wired must stay a no-op: no mesh latched.
	for (int i = 0; i < 10; ++i) {
		sys.update(0.016F);
	}
	ASSERT_FALSE(sys.hasMesh());

	// Wiring the world now must still trigger the first build and a path solve.
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built after deferred wiring";
	EXPECT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
}

// --- Belief-filtered queries -------------------------------------------------
//
// Belief is applied at query time against the one shared truth mesh. A colonist
// that knows nothing routes optimistically (unseen walls absent, path goes
// straight through); a colonist that knows the enclosing walls (but not a door)
// is blocked. Both queries hit the SAME mesh -- only the filter differs.

namespace {
	// Every wall segment id currently in the world, for seeding a "knows the walls"
	// belief filter.
	std::unordered_set<std::uint64_t> allSegmentIds(const ConstructionWorld& cw) {
		std::unordered_set<std::uint64_t> ids;
		for (const auto& seg : cw.segments()) {
			ids.insert(static_cast<std::uint64_t>(seg.id));
		}
		return ids;
	}
} // namespace

// Empty belief (knows nothing) routes through where the wall is; a belief that
// knows the enclosing walls (no door known) blocks the same query.
TEST_F(NavigationSystemTest, BeliefFilterGatesQueryVsTruth) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	// Truth query (default filter) routes through the actual door: a path exists.
	EXPECT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value())
		<< "truth query should route through the real door";

	// Belief: knows NOTHING. Empty sets => every wall is treated as unseen/absent, so
	// the optimistic route punches straight through where the south wall is.
	const std::unordered_set<std::uint64_t> noSegments;
	const std::unordered_set<std::uint64_t> noOpenings;
	const geometry::nav::BeliefFilter		blindBelief{&noSegments, &noOpenings};
	EXPECT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius, blindBelief).has_value())
		<< "blind belief should optimistically route through the unseen wall";

	// Belief: knows the enclosing walls but NOT the door opening. The seen walls block
	// and the door is unknown, so there is no believed route in.
	const std::unordered_set<std::uint64_t> knownWalls = allSegmentIds(cw);
	const geometry::nav::BeliefFilter		walledBelief{&knownWalls, &noOpenings};
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius, walledBelief).has_value())
		<< "knowing the walls (but not the door) should block the route";

	// Belief: knows the walls AND the door opening => the route opens back up, matching
	// truth. Confirms a known opening passes through a known wall.
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
// isReachable uses geometry::nav::reachable (O(log n)) rather than building a
// full path. Semantics: false = DEFINITELY unreachable (sound), true = MAYBE
// reachable (over-approximation). No-mesh => always true.

// Reachable goal agrees with requestPath (both true).
TEST_F(NavigationSystemTest, IsReachableTrueForReachableGoal) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	EXPECT_TRUE(isReachable(sys, kOutside, kInside, kAgentRadius));
	EXPECT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
}

// Walled-off goal: isReachable returns false, requestPath returns nullopt.
TEST_F(NavigationSystemTest, IsReachableFalseForWalledOffGoal) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	// reachable() should detect the disconnected component and return false.
	EXPECT_FALSE(isReachable(sys, kOutside, kInside, kAgentRadius));
	// requestPath must also return nullopt (short-circuit already there via pathThrough).
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
}

// No-mesh: isReachable returns true regardless of endpoints (can't prove unreachable).
TEST_F(NavigationSystemTest, IsReachableTrueWhenNoMesh) {
	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();

	ASSERT_FALSE(sys.hasMesh());
	EXPECT_TRUE(isReachable(sys, kOutside, kInside, kAgentRadius));
	// requestPath is still nullopt with no mesh.
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
}

// generation() bumps each time a freshly built mesh is swapped in.
TEST_F(NavigationSystemTest, GenerationBumpsOnMeshSwap) {
	ConstructionWorld cw;
	SegmentId south = buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);

	EXPECT_EQ(sys.generation(), 0u) << "no mesh yet => generation 0";
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "initial navmesh never built";
	const std::uint64_t afterFirst = sys.generation();
	EXPECT_GT(afterFirst, 0u) << "first mesh swap should bump generation";

	// A geometry change forces a rebuild; the swap-in bumps generation again.
	OpeningId op = cw.addOpening(south, 0.5F, "Door", "Wood");
	ASSERT_NE(op, kInvalidOpening);
	ASSERT_TRUE(cw.setOpeningState(op, FoundationState::Built));
	sys.finishPendingBuilds();
	EXPECT_GT(sys.generation(), afterFirst)
		<< "rebuild swap should bump generation again";
}

// Every region's mesh swap moves generation(), including one whose own swap count lags another
// region's. A stamped NavPath and a route deferred until a mesh covers it both key on it, so a swap
// that leaves it unchanged is a rebuild they never see.
TEST_F(NavigationSystemTest, GenerationMovesWhenALaggingRegionSwaps) {
	ConstructionWorld cw;

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(m_chunks.get());
	sys.setConstructionWorld(&cw);

	// Colonist A's region builds, then rebuilds on two wall edits, so its swap count runs ahead.
	EntityID a = world.createEntity();
	world.addComponent<Position>(a, Position{glm::vec2{0.0F, 0.0F}});
	world.addComponent<Colonist>(a, Colonist{"A"});
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "region A never built";
	for (std::int64_t edit = 0; edit < 2; ++edit) {
		const std::uint64_t before = sys.generation();
		buildWall(cw, {10000 + edit * 2000, 10000}, {11000 + edit * 2000, 10000});
		sys.finishPendingBuilds();
		ASSERT_NE(sys.generation(), before)
			<< "wall edit " << edit << " never rebuilt region A";
	}

	// Colonist B, 100 m away, gets a disjoint region whose first swap lands behind A's count.
	const std::uint64_t beforeB = sys.generation();
	EntityID b = world.createEntity();
	world.addComponent<Position>(b, Position{glm::vec2{100.0F, 0.0F}});
	world.addComponent<Colonist>(b, Colonist{"B"});
	sys.finishPendingBuilds();
	ASSERT_EQ(sys.builtRegions().size(), 2U) << "region B never built";
	EXPECT_NE(sys.generation(), beforeB) << "region B's first mesh swap left generation() unchanged";
}

// --- Colonist regions are independent of the camera --------------------------
//
// The camera's region is never merged with a colonist's, and colonist queries consult colonist
// regions only. Merged, a colonist's region was pinned by the camera and clamped: he couldn't walk
// past the far side of the view, and standing at its edge made it rebuild every time a build landed.

namespace {
	std::vector<NavigationSystem::RegionView> colonistRegions(const NavigationSystem& sys) {
		std::vector<NavigationSystem::RegionView> out;
		for (const NavigationSystem::RegionView& r : sys.builtRegions()) {
			if (r.owner == RegionOwner::Colonists) {
				out.push_back(r);
			}
		}
		return out;
	}

	EntityID addColonistDriver(World& world, glm::vec2 at) {
		const EntityID c = world.createEntity();
		world.addComponent<Position>(c, Position{at});
		world.addComponent<Colonist>(c, Colonist{"C"});
		return c;
	}

	// Settles the system and reports whether `regions` regions (the camera's too, when pushed) have a mesh.
	[[nodiscard]] bool settledWith(NavigationSystem& sys, std::size_t regions) {
		sys.finishPendingBuilds();
		return sys.builtRegions().size() == regions;
	}
} // namespace

// The same colonist and walls with no camera, the camera over him (zoomed out, so its square takes
// in his), on him, and far away: his region's rect and every colonist answer are the same.
TEST_F(NavigationSystemTest, ColonistAnswersAreTheSameWhereverTheCameraIs) {
	struct Answers {
		std::vector<std::pair<geometry::Vec2i64, std::int64_t>> rects;
		std::vector<std::optional<std::vector<glm::vec2>>>		paths;
		std::vector<bool>										reach;
		std::vector<bool>										covered;
		std::vector<bool>										inArea;
		std::vector<bool>										onMesh;
	};
	const glm::vec2				 colonistAt{10.0F, 0.0F};
	const std::vector<glm::vec2> points{kOutside, kInside, {35.0F, 5.0F}, {39.5F, 0.0F}, {70.0F, 0.0F}, {-25.0F, 20.0F}};

	auto run = [&](std::optional<std::pair<glm::vec2, float>> camera) {
		ConstructionWorld cw;
		buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);
		World			  world;
		NavigationSystem& sys = world.registerSystem<NavigationSystem>();
		sys.setChunkManager(m_chunks.get());
		sys.setConstructionWorld(&cw);
		if (camera.has_value()) {
			sys.setViewportRect(camera->first, {camera->second, camera->second});
		}
		(void)addColonistDriver(world, colonistAt);
		EXPECT_TRUE(settledWith(sys, camera.has_value() ? 2U : 1U));

		Answers a;
		for (const NavigationSystem::RegionView& r : colonistRegions(sys)) {
			a.rects.emplace_back(r.center, r.halfExtent);
		}
		for (const glm::vec2 from : points) {
			const NavigationSystem::Reach reach = reachOf(sys, from, kAgentRadius);
			a.inArea.push_back(sys.inSimArea(from));
			a.onMesh.push_back(sys.isOnMesh(from));
			for (const glm::vec2 to : points) {
				a.paths.push_back(sys.requestPath(from, to, kAgentRadius));
				a.reach.push_back(sys.reaches(reach, to));
				a.covered.push_back(sys.coversGoal(reach, to));
			}
		}
		return a;
	};

	const Answers none = run(std::nullopt);
	ASSERT_EQ(none.rects.size(), 1U);
	EXPECT_EQ(none.rects[0].first, nav::toMm(colonistAt));
	EXPECT_EQ(none.rects[0].second, NavigationSystem::kSimRadiusMm);
	for (const auto& camera : {std::pair{glm::vec2{40.0F, 0.0F}, 64.0F}, std::pair{glm::vec2{10.0F, 0.0F}, 30.0F},
							   std::pair{glm::vec2{-400.0F, 0.0F}, 30.0F}}) {
		const Answers with = run(camera);
		EXPECT_EQ(with.rects, none.rects) << "camera at " << camera.first.x;
		EXPECT_EQ(with.paths, none.paths) << "camera at " << camera.first.x;
		EXPECT_EQ(with.reach, none.reach) << "camera at " << camera.first.x;
		EXPECT_EQ(with.covered, none.covered) << "camera at " << camera.first.x;
		EXPECT_EQ(with.inArea, none.inArea) << "camera at " << camera.first.x;
		EXPECT_EQ(with.onMesh, none.onMesh) << "camera at " << camera.first.x;
	}
}

// Panning and zooming moves only the camera's region: the colonist's is never rebuilt or moved, and
// generation(), which colonist routes are stamped with, stays put.
TEST_F(NavigationSystemTest, MovingTheCameraNeverRebuildsAColonistRegion) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(m_chunks.get());
	sys.setConstructionWorld(&cw);
	sys.setViewportRect({0.0F, 0.0F}, {40.0F, 40.0F});
	(void)addColonistDriver(world, {0.0F, 0.0F});
	ASSERT_TRUE(settledWith(sys, 2U));
	const NavigationSystem::RegionView before	  = colonistRegions(sys).at(0);
	const std::uint64_t				   generation = sys.generation();
	std::set<std::pair<std::int32_t, std::uint64_t>> cameraMeshes;

	const std::vector<std::pair<glm::vec2, float>> views{
		{{20.0F, 0.0F}, 40.0F}, {{60.0F, 10.0F}, 64.0F}, {{-30.0F, -30.0F}, 100.0F}, {{0.0F, 0.0F}, 30.0F}, {{300.0F, 0.0F}, 50.0F}};
	for (const auto& [center, half] : views) {
		sys.setViewportRect(center, {half, half});
		sys.finishPendingBuilds();
		const std::vector<NavigationSystem::RegionView> now = colonistRegions(sys);
		ASSERT_EQ(now.size(), 1U);
		EXPECT_EQ(now[0].meshGeneration, before.meshGeneration) << "camera at " << center.x << " rebuilt his region";
		EXPECT_EQ(now[0].center, before.center);
		EXPECT_EQ(now[0].halfExtent, before.halfExtent);
		EXPECT_EQ(sys.generation(), generation) << "a camera-region swap moved the generation colonist routes key on";
		for (const NavigationSystem::RegionView& r : sys.builtRegions()) {
			if (r.owner == RegionOwner::Camera) {
				cameraMeshes.emplace(r.id, r.meshGeneration);
			}
		}
	}
	EXPECT_GT(cameraMeshes.size(), 2U) << "the camera region barely rebuilt; the test moved nothing";
}

// A colonist in his region's edge margin recenters it once, onto a rect he sits deep inside, and it
// doesn't rebuild again while he stands there. Three colonists in a 110 m chain (once one region,
// clamped, with the outer two in its margin) get regions that hold each of them deep inside.
TEST_F(NavigationSystemTest, ColonistAtHisRegionEdgeDoesNotKeepRebuildingIt) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(m_chunks.get());
	sys.setConstructionWorld(&cw);
	sys.setViewportRect({40.0F, 0.0F}, {64.0F, 64.0F});
	const EntityID colonist = addColonistDriver(world, {0.0F, 0.0F});
	ASSERT_TRUE(settledWith(sys, 2U));

	world.getComponent<Position>(colonist)->value = {25.0F, 0.0F}; // 5 m from the east edge
	sys.finishPendingBuilds();
	ASSERT_EQ(colonistRegions(sys).at(0).center, nav::toMm(glm::vec2{25.0F, 0.0F}));
	const std::uint64_t recentered = colonistRegions(sys).at(0).meshGeneration;
	for (int i = 0; i < 30; ++i) {
		world.getComponent<Position>(colonist)->value = {25.0F + 0.05F * static_cast<float>(i % 2), 0.0F};
		sys.update(0.0F);
		sys.finishPendingBuilds();
	}
	EXPECT_EQ(colonistRegions(sys).at(0).meshGeneration, recentered) << "his region kept rebuilding";

	(void)addColonistDriver(world, {80.0F, 0.0F});
	(void)addColonistDriver(world, {135.0F, 0.0F});
	sys.finishPendingBuilds();
	ASSERT_EQ(colonistRegions(sys).size(), 2U);
	const std::uint64_t settled = sys.generation();
	for (int i = 0; i < 30; ++i) {
		sys.update(0.0F);
		sys.finishPendingBuilds();
	}
	EXPECT_EQ(sys.generation(), settled) << "a region kept rebuilding with a colonist at its edge";
	for (const float x : {25.0F, 80.0F, 135.0F}) {
		EXPECT_TRUE(sys.inSimArea({x, 0.0F})) << "colonist at " << x << " is outside every colonist region";
	}
}

namespace {
	// Region builds that land only when the test says, so build order never depends on the clock.
	// Declare it after the World holding the system: it lands whatever is left when it goes, before
	// the system's destructor waits on them.
	class ManualBuilds {
	  public:
		ManualBuilds()						 = default;
		ManualBuilds(const ManualBuilds&)	 = delete;
		ManualBuilds& operator=(const ManualBuilds&) = delete;
		~ManualBuilds() { finishAll(); }

		[[nodiscard]] NavigationSystem::BuildRunner runner() {
			return [this](geometry::nav::NavMeshInput input, std::int32_t regionId) {
				pending.push_back({regionId, std::move(input), {}});
				return pending.back().done.get_future();
			};
		}
		void finish(std::int32_t regionId) {
			for (auto it = pending.begin(); it != pending.end();) {
				if (it->regionId == regionId) {
					it->done.set_value(geometry::nav::buildNavMesh(it->input));
					it = pending.erase(it);
				} else {
					++it;
				}
			}
		}
		void finishAll() {
			for (Pending& p : pending) {
				p.done.set_value(geometry::nav::buildNavMesh(p.input));
			}
			pending.clear();
		}

	  private:
		struct Pending {
			std::int32_t						 regionId;
			geometry::nav::NavMeshInput			 input;
			std::promise<geometry::nav::NavMesh> done;
		};
		std::vector<Pending> pending;
	};

	const NavigationSystem::RegionView& colonistRegionCentredAt(const std::vector<NavigationSystem::RegionView>& regions,
																 glm::vec2 center) {
		for (const NavigationSystem::RegionView& r : regions) {
			if (r.owner == RegionOwner::Colonists && r.center == nav::toMm(center)) {
				return r;
			}
		}
		ADD_FAILURE() << "no colonist region centred at (" << center.x << ", " << center.y << ")";
		static const NavigationSystem::RegionView kNone{};
		return kNone;
	}
} // namespace

// Two colonist regions overlap, and a room is built in the overlap with the goal inside it. Until
// both rebuild, one mesh has the room and the other doesn't: the answer is the fresh one's, never a
// route through the room's walls from the stale one, though the stale one has the lower id.
TEST_F(NavigationSystemTest, StaleOverlappingRegionNeverOverridesAFreshOne) {
	ConstructionWorld cw;
	World			  world;
	ManualBuilds	  builds;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setBuildRunner(builds.runner());
	sys.setChunkManager(m_chunks.get());
	sys.setConstructionWorld(&cw);
	(void)driveColonistRegion(world, {0.0F, 0.0F}, 60.0F); // A: -60..60
	(void)addColonistDriver(world, {85.0F, 0.0F});		   // B: 55..115, too far to cluster with A
	sys.update(0.0F);
	builds.finishAll();
	sys.update(0.0F);
	ASSERT_EQ(colonistRegions(sys).size(), 2U);
	const std::int32_t a = colonistRegionCentredAt(sys.builtRegions(), {0.0F, 0.0F}).id;
	const std::int32_t b = colonistRegionCentredAt(sys.builtRegions(), {85.0F, 0.0F}).id;
	ASSERT_LT(a, b) << "the stale region must be the one id order would try first";

	const glm::vec2 outside{57.5F, -4.0F};
	const glm::vec2 inside{57.5F, 0.0F};
	ASSERT_TRUE(sys.requestPath(outside, inside, kAgentRadius).has_value());
	ASSERT_EQ(reachOf(sys, outside, kAgentRadius).regions.size(), 2U) << "the start is in both regions";

	buildWall(cw, {55500, -2000}, {59500, -2000});
	buildWall(cw, {59500, -2000}, {59500, 2000});
	buildWall(cw, {59500, 2000}, {55500, 2000});
	buildWall(cw, {55500, 2000}, {55500, -2000});
	sys.update(0.0F); // both relaunch for the room
	builds.finish(b);
	sys.update(0.0F); // B's fresh mesh lands; A still serves the one without the room
	ASSERT_LT(colonistRegionCentredAt(sys.builtRegions(), {0.0F, 0.0F}).meshStamp,
			  colonistRegionCentredAt(sys.builtRegions(), {85.0F, 0.0F}).meshStamp);

	EXPECT_FALSE(sys.requestPath(outside, inside, kAgentRadius).has_value()) << "the stale mesh routed into the room";
	EXPECT_FALSE(isReachable(sys, outside, inside, kAgentRadius)) << "the stale mesh passed the pre-check";
	EXPECT_TRUE(sys.isOnMesh(inside)) << "the room's floor is walkable on the fresh mesh";

	builds.finish(a);
	sys.update(0.0F);
	EXPECT_EQ(colonistRegionCentredAt(sys.builtRegions(), {0.0F, 0.0F}).meshStamp,
			  colonistRegionCentredAt(sys.builtRegions(), {85.0F, 0.0F}).meshStamp);
	EXPECT_FALSE(sys.requestPath(outside, inside, kAgentRadius).has_value());
	EXPECT_FALSE(isReachable(sys, outside, inside, kAgentRadius));
}

// Two colonists' clusters merge: the survivor recenters onto both, and until its larger mesh lands
// the absorbed colonist's old region keeps serving him; then it goes.
TEST_F(NavigationSystemTest, MergedClusterKeepsTheAbsorbedColonistCoveredUntilTheSurvivorLands) {
	ConstructionWorld cw;
	World			  world;
	ManualBuilds	  builds;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setBuildRunner(builds.runner());
	sys.setChunkManager(m_chunks.get());
	sys.setConstructionWorld(&cw);
	(void)addColonistDriver(world, {0.0F, 0.0F});
	const EntityID second = addColonistDriver(world, {70.0F, 0.0F});
	sys.update(0.0F);
	builds.finishAll();
	sys.update(0.0F);
	ASSERT_EQ(colonistRegions(sys).size(), 2U);

	const glm::vec2 joined{45.0F, 0.0F}; // his square now overlaps the first's: one cluster
	world.getComponent<Position>(second)->value = joined;
	sys.update(0.0F);
	ASSERT_TRUE(sys.isBuilding()) << "the survivor's recenter is in flight";
	EXPECT_TRUE(sys.inSimArea(joined)) << "the absorbed colonist was left in no built region";
	EXPECT_FALSE(reachOf(sys, joined, kAgentRadius).regions.empty());
	EXPECT_EQ(colonistRegions(sys).size(), 2U);

	builds.finishAll();
	sys.update(0.0F);
	EXPECT_EQ(colonistRegions(sys).size(), 1U) << "his old region outlived the survivor's swap";
	EXPECT_TRUE(sys.inSimArea(joined));
	EXPECT_TRUE(sys.inSimArea({0.0F, 0.0F}));
}

// A region dropped in the same update as a lingering colonist region is judged: the keep/drop
// decision for every region is made before any is erased, so erasing the camera's old region (between
// the lingering one and the survivor) can't make the survivor read as unwanted and keep the
// lingering region past the survivor's swap.
TEST_F(NavigationSystemTest, LingeringRegionDropsWhenTheSurvivorLandsDespiteAnotherDrop) {
	ConstructionWorld cw;
	World			  world;
	ManualBuilds	  builds;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setBuildRunner(builds.runner());
	sys.setChunkManager(m_chunks.get());
	sys.setConstructionWorld(&cw);
	auto settle = [&] {
		sys.update(0.0F);
		builds.finishAll();
		sys.update(0.0F);
	};
	const EntityID second = addColonistDriver(world, {70.0F, 0.0F}); // the lingering region, first
	settle();
	sys.setViewportRect({-300.0F, 0.0F}, {30.0F, 30.0F}); // the camera's, second
	settle();
	(void)addColonistDriver(world, {0.0F, 0.0F}); // the survivor, third
	settle();
	ASSERT_EQ(sys.builtRegions().size(), 3U);

	const glm::vec2 joined{45.0F, 0.0F};
	world.getComponent<Position>(second)->value = joined; // the clusters merge; the survivor recenters
	sys.update(0.0F);
	ASSERT_EQ(colonistRegions(sys).size(), 2U) << "his old region lingers while the survivor builds";

	sys.setViewportRect({300.0F, 0.0F}, {30.0F, 30.0F}); // drops the camera's old region this update
	builds.finishAll();
	sys.update(0.0F); // the survivor lands holding him
	EXPECT_EQ(colonistRegions(sys).size(), 1U) << "the lingering region outlived the survivor's swap";
	EXPECT_TRUE(sys.inSimArea(joined));
	builds.finishAll();
}

// Staging leaves a metre of slack inside the 2 m a route goal needs, so a leg far from the origin
// (where meters are coarse floats) still routes, and with the start in the 2 m band at the edge the
// leg ends deeper in, where a route may end, even when the first staging sample is walled off.
TEST_F(NavigationSystemTest, StagingPointIsAlwaysARoutableGoal) {
	std::vector<glm::vec2> origins{{0.0F, 0.0F}};
	for (int i = 0; i < 8; ++i) {
		origins.push_back({20000.0F + 0.373F * static_cast<float>(i), -19999.0F - 0.611F * static_cast<float>(i)});
	}
	for (const glm::vec2 origin : origins) {
		auto chunks = std::make_unique<world::ChunkManager>(landSampler());
		chunks->setLoadRadius(1);
		chunks->setUnloadRadius(4);
		chunks->update({origin.x, origin.y});
		chunks->finishPendingGeneration();
		ConstructionWorld cw;
		World			  world;
		ManualBuilds	  builds;
		NavigationSystem& sys = world.registerSystem<NavigationSystem>();
		sys.setBuildRunner(builds.runner());
		sys.setChunkManager(chunks.get());
		sys.setConstructionWorld(&cw);
		// A sealed 1 m box 2 m inside the region's north-east corner, where a staging point with no
		// slack would land: the walk back from it toward a start in the 2 m band at the east edge
		// crosses only band points.
		const Vec2i64 box = nav::toMm(origin + glm::vec2{28.0F, 28.0F});
		buildWall(cw, {box.x - 500, box.y - 500}, {box.x + 500, box.y - 500});
		buildWall(cw, {box.x + 500, box.y - 500}, {box.x + 500, box.y + 500});
		buildWall(cw, {box.x + 500, box.y + 500}, {box.x - 500, box.y + 500});
		buildWall(cw, {box.x - 500, box.y + 500}, {box.x - 500, box.y - 500});
		const EntityID colonist = addColonistDriver(world, origin);
		sys.update(0.0F);
		builds.finishAll();
		sys.update(0.0F);
		ASSERT_TRUE(sys.inSimArea(origin));

		const NavigationSystem::RegionView region = colonistRegions(sys).at(0);
		const glm::vec2 centre = nav::toMeters(region.center);
		const glm::vec2 edge   = centre + glm::vec2{static_cast<float>(region.halfExtent) / 1000.0F, 0.0F}; // east edge
		const glm::vec2 goal   = origin + glm::vec2{100.0F, 200.0F};
		for (const glm::vec2 start : {origin, edge - glm::vec2{1.0F, 0.0F}}) { // the second is 1 m from the edge
			world.getComponent<Position>(colonist)->value = start;
			const NavigationSystem::Reach reach = reachOf(sys, start, kAgentRadius);
			ASSERT_FALSE(reach.regions.empty());
			ASSERT_FALSE(sys.coversGoal(reach, goal));
			const std::optional<glm::vec2> staging = sys.stagingPointToward(reach, start, goal);
			ASSERT_TRUE(staging.has_value()) << "from (" << start.x << ", " << start.y << ")";
			EXPECT_TRUE(sys.coversGoal(reach, *staging)) << "the staging point is no route goal";
			EXPECT_TRUE(sys.requestPath(start, *staging, kAgentRadius).has_value())
				<< "the router refused its own leg from (" << start.x << ", " << start.y << ")";
		}

		// The rule itself: a goal 1 m inside the rect isn't routed to, one 2.5 m inside is.
		const NavigationSystem::Reach reach = reachOf(sys, origin, kAgentRadius);
		EXPECT_FALSE(sys.coversGoal(reach, edge - glm::vec2{1.0F, 0.0F}));
		EXPECT_TRUE(sys.coversGoal(reach, edge - glm::vec2{2.5F, 0.0F}));
		EXPECT_FALSE(sys.requestPath(origin, edge - glm::vec2{1.0F, 0.0F}, kAgentRadius).has_value());
		EXPECT_TRUE(sys.requestPath(origin, edge - glm::vec2{2.5F, 0.0F}, kAgentRadius).has_value());
	}
}

// A click on water in the camera's region snaps to the shore beside it, against the camera's mesh.
TEST_F(NavigationSystemTest, NearestValidPositionSnapsAnOffFaceClickInTheCameraRegion) {
	auto chunks = std::make_unique<world::ChunkManager>(waterPatchSampler());
	chunks->setLoadRadius(2);
	chunks->setUnloadRadius(4);
	chunks->update({0.0F, 0.0F});
	chunks->finishPendingGeneration();
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	sys.setConstructionWorld(&cw);
	pushArea(sys, {40000, 40000}, 40000);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh());

	const glm::vec2 pond{40.0F, 40.0F};
	ASSERT_FALSE(sys.isValidPosition(pond));
	ASSERT_FALSE(sys.nearestPathablePoint(pond, kAgentRadius).has_value()) << "no colonist region: the colonist snap has nothing";
	const std::optional<glm::vec2> snapped = sys.nearestValidPosition(pond, kAgentRadius);
	ASSERT_TRUE(snapped.has_value());
	EXPECT_TRUE(sys.isValidPosition(*snapped));
	EXPECT_LT(glm::distance(*snapped, pond), kPatchPond.radius * 1.5F) << "snapped to a far shore";
	EXPECT_FALSE(sys.nearestValidPosition({400.0F, 400.0F}, kAgentRadius).has_value()) << "a click no region holds";
}

// The camera's region serves placement checks but no colonist query.

TEST_F(NavigationSystemTest, CameraRegionServesPlacementButNoColonistQuery) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(m_chunks.get());
	sys.setConstructionWorld(&cw);
	pushArea(sys, {0, 0}, 60000);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh());

	const glm::vec2 a{2.0F, 2.0F};
	const glm::vec2 b{10.0F, -5.0F};
	EXPECT_TRUE(sys.isValidPosition(a)) << "placement in the camera region";
	EXPECT_TRUE(sys.isAreaWalkable({{1.0F, 1.0F}, {3.0F, 1.0F}, {3.0F, 3.0F}}));
	EXPECT_TRUE(sys.findValidPositionNear(a, 1.0F).has_value());
	EXPECT_FALSE(sys.inSimArea(a));
	EXPECT_FALSE(sys.isOnMesh(a));
	EXPECT_FALSE(sys.requestPath(a, b, kAgentRadius).has_value());
	EXPECT_TRUE(reachOf(sys, a, kAgentRadius).regions.empty());
	EXPECT_FALSE(sys.nearestPathablePoint(a).has_value());
}

// A Reach names its start region by stable id and that region's own swap count, which is what the
// AI's SourceReachMemo keys on: another region's swap leaves it alone, a swap in its own region moves it.
TEST_F(NavigationSystemTest, ReachIdentifiesItsOwnRegionsMesh) {
	ConstructionWorld cw;

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(m_chunks.get());
	sys.setConstructionWorld(&cw);

	const glm::vec2 posA{0.0F, 0.0F};
	EntityID		a = world.createEntity();
	world.addComponent<Position>(a, Position{posA});
	world.addComponent<Colonist>(a, Colonist{"A"});
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "region A never built";
	const NavigationSystem::Reach first = reachOf(sys, posA, kAgentRadius);
	ASSERT_GE(first.regions.at(0).id, 0);

	EntityID b = world.createEntity();
	world.addComponent<Position>(b, Position{glm::vec2{100.0F, 0.0F}});
	world.addComponent<Colonist>(b, Colonist{"B"});
	sys.finishPendingBuilds();
	ASSERT_EQ(sys.builtRegions().size(), 2U) << "region B never built";
	const NavigationSystem::Reach afterB = reachOf(sys, posA, kAgentRadius);
	EXPECT_EQ(afterB.regions.at(0).id, first.regions.at(0).id);
	EXPECT_EQ(afterB.regions.at(0).meshGeneration, first.regions.at(0).meshGeneration) << "region B's swap touched region A's key";

	buildWall(cw, {10000, 10000}, {11000, 10000});
	sys.finishPendingBuilds();
	EXPECT_NE(reachOf(sys, posA, kAgentRadius).regions.at(0).meshGeneration, first.regions.at(0).meshGeneration)
		<< "region A's own swap left its key unchanged";
	EXPECT_EQ(reachOf(sys, posA, kAgentRadius).regions.at(0).id, first.regions.at(0).id);
}

// --- RRA* heuristic wiring + instrumentation (P3.5) --------------------------
//
// NavigationSystem owns the resumable RRA* caches (keyed by goal triangle) and
// passes them into pathThrough. The reverse search is belief-/radius-agnostic, so
// one cache per goal serves every query; the map is cleared on every mesh swap
// (triangle indices go stale) and bounded so a goal churn can't grow it forever.

// A reachable query populates the A* instrumentation, and the cumulative accessor
// advances. nodesExpanded must be > 0 for a real (non-trivial) solve.
TEST_F(NavigationSystemTest, NavQueryStatsPopulatedOnPath) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	sys.resetNavQueryStats();
	EXPECT_EQ(sys.navQueryStats().totalQueries, 0u);

	ASSERT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());

	const NavigationSystem::NavQueryStats& s = sys.navQueryStats();
	EXPECT_EQ(s.totalQueries, 1u) << "one reachable solve recorded";
	EXPECT_GT(s.lastNodesExpanded, 0) << "a real solve expands at least one node";
	EXPECT_EQ(s.totalNodesExpanded, static_cast<std::uint64_t>(s.lastNodesExpanded));
	EXPECT_GT(s.lastPeakOpenSet, 0);

	// A second reachable query advances the cumulative counters.
	ASSERT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
	EXPECT_EQ(sys.navQueryStats().totalQueries, 2u) << "cumulative query count moves";
	EXPECT_GE(sys.navQueryStats().totalNodesExpanded, s.totalNodesExpanded);
}

// A query creates a goal-keyed RRA* cache; a mesh rebuild (generation bump) clears
// the caches because triangle indices are stale, and queries stay correct across it.
TEST_F(NavigationSystemTest, MeshRebuildClearsRraCaches) {
	ConstructionWorld cw;
	SegmentId south = buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "initial navmesh never built";

	// Before: sealed room. A query that locates a goal triangle creates a cache for it
	// (even though the path itself is unreachable -- locate succeeds on the floor goal).
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value())
		<< "closed room blocks the path";
	EXPECT_GT(sys.rraCacheCount(), 0u) << "locating the goal triangle creates its cache";

	const std::uint64_t genBefore = sys.generation();

	// Cut a door: bumps version() -> async rebuild -> mesh swap clears the caches.
	OpeningId op = cw.addOpening(south, 0.5F, "Door", "Wood");
	ASSERT_NE(op, kInvalidOpening);
	ASSERT_TRUE(cw.setOpeningState(op, FoundationState::Built));

	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.generation() != genBefore && sys.requestPath(kOutside, kInside, kAgentRadius).has_value()) << "door rebuild never opened the path";
	EXPECT_GT(sys.generation(), genBefore) << "mesh swapped";

	// After the swap the path is reachable AND correct. The cache map was cleared on the
	// swap; the just-issued reachable query re-created exactly one entry for the goal.
	EXPECT_EQ(sys.rraCacheCount(), 1u)
		<< "rebuild clears the stale caches; the post-rebuild query re-created one";
}

// The cache map is bounded: many DISTINCT goal triangles must not grow it past the
// cap. A grid of pillars fragments the mesh into many triangles so a spread of goal
// points lands on > 64 distinct triangles, genuinely exercising the cap.
TEST_F(NavigationSystemTest, RraCacheMapIsBounded) {
	// 14x14 m room with a 5x5 grid of small wall-pillars: the interior triangulates into
	// many triangles, so a grid of goal points maps to many distinct goal triangles.
	ConstructionWorld cw;
	buildWall(cw, {0, 0}, {14000, 0});
	buildWall(cw, {14000, 0}, {14000, 14000});
	buildWall(cw, {14000, 14000}, {0, 14000});
	buildWall(cw, {0, 14000}, {0, 0});
	for (int px = 0; px < 5; ++px) {
		for (int py = 0; py < 5; ++py) {
			const std::int64_t cx = 2500 + px * 2200;
			const std::int64_t cy = 2500 + py * 2200;
			// A tiny square pillar (each wall segment built) -- an obstacle that splits the
			// surrounding floor into extra triangles.
			buildWall(cw, {cx, cy}, {cx + 400, cy});
			buildWall(cw, {cx + 400, cy}, {cx + 400, cy + 400});
			buildWall(cw, {cx + 400, cy + 400}, {cx, cy + 400});
			buildWall(cw, {cx, cy + 400}, {cx, cy});
		}
	}

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	// Collect the distinct goal triangles a fine grid of goal points maps to; this is the
	// number of cache entries an UNBOUNDED map would create.
	const glm::vec2					   start{1.0F, 1.0F};
	std::unordered_set<std::int32_t>   distinctGoalTris;
	for (int gx = 0; gx < 13; ++gx) {
		for (int gy = 0; gy < 13; ++gy) {
			const glm::vec2 goal{0.7F + static_cast<float>(gx), 0.7F + static_cast<float>(gy)};
			const std::int32_t t = geometry::nav::locateTriangle(firstMesh(sys), nav::toMm(goal));
			if (t >= 0) {
				distinctGoalTris.insert(t);
			}
			(void)sys.requestPath(start, goal, kAgentRadius);
		}
	}

	// The bound must hold regardless.
	EXPECT_LE(sys.rraCacheCount(), 64u) << "the cache map must stay within its cap under goal churn";
	// And the scenario must actually push past the cap (otherwise the bound is untested).
	EXPECT_GT(distinctGoalTris.size(), 64u)
		<< "test setup must produce > 64 distinct goal triangles to exercise the cap";
}

// --- Phase B: viewport-tracking simulation area (snap-on-scroll) -------------
//
// These tests drive setSimulationArea directly (GameScene does the camera->area
// math in production). Each test verifies that the rebuild trigger fires (or not)
// on the exact conditions described in the Phase B design:
//
//  * first push (no built area) -> rebuild
//  * zoom-out (larger half-extent, >20% change) -> rebuild, AABB grows
//  * zoom-in (smaller half-extent, >20% change) -> rebuild, AABB shrinks
//  * pan past kRecenterThresholdMm -> rebuild, center moves
//  * tiny delta below both thresholds -> NO rebuild (hysteresis)
//  * half-extent clamped to [kMinSimHalfExtentMm, kMaxSimHalfExtentMm]

// Helper: AABB of a mesh (min/max over the vertex list).
namespace {
	struct Aabb { std::int64_t minX, minY, maxX, maxY; };
	Aabb meshAabb(const geometry::nav::NavMesh& m) {
		Aabb a{INT64_MAX, INT64_MAX, INT64_MIN, INT64_MIN};
		for (const geometry::Vec2i64& v : m.vertices) {
			a.minX = std::min(a.minX, v.x);
			a.minY = std::min(a.minY, v.y);
			a.maxX = std::max(a.maxX, v.x);
			a.maxY = std::max(a.maxY, v.y);
		}
		return a;
	}
} // namespace

// Zoom-out: pushing a larger half-extent (well above 20% change) causes a
// rebuild and the resulting mesh AABB is strictly larger.
TEST_F(NavigationSystemTest, ZoomOutRebuildExpandsArea) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(m_chunks.get());
	sys.setConstructionWorld(&cw);

	// First push: 30 m (min clamp, covers origin room).
	pushArea(sys, {0, 0}, 30000);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "initial mesh never built";
	const std::uint64_t gen1 = cameraSwaps(sys);
	const Aabb aabb1 = meshAabb(firstMesh(sys));

	// Zoom out: 100 m (> 20% bigger than 30 m after clamping stays 100 m).
	pushArea(sys, {0, 0}, 100000);
	sys.finishPendingBuilds();
	const Aabb aabb2 = meshAabb(firstMesh(sys));
	EXPECT_TRUE((aabb2.maxX - aabb2.minX) > (aabb1.maxX - aabb1.minX)
		|| (aabb2.maxY - aabb2.minY) > (aabb1.maxY - aabb1.minY)) << "zoom-out rebuild should produce a larger mesh AABB";
	EXPECT_GT(cameraSwaps(sys), gen1) << "the camera region must swap on zoom-out rebuild";
}

// Zoom-in: pushing a smaller half-extent (>20% smaller) causes a rebuild and
// the mesh AABB shrinks.
TEST_F(NavigationSystemTest, ZoomInRebuildShrinksArea) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(m_chunks.get());
	sys.setConstructionWorld(&cw);

	// First push: 100 m.
	pushArea(sys, {0, 0}, 100000);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "initial mesh never built";
	const std::uint64_t gen1 = cameraSwaps(sys);
	const Aabb aabb1 = meshAabb(firstMesh(sys));

	// Zoom in: 50 m (50% of 100 m -> delta 50% > 20% threshold).
	pushArea(sys, {0, 0}, 50000);
	sys.finishPendingBuilds();
	const Aabb aabb2 = meshAabb(firstMesh(sys));
	EXPECT_TRUE((aabb2.maxX - aabb2.minX) < (aabb1.maxX - aabb1.minX)
		|| (aabb2.maxY - aabb2.minY) < (aabb1.maxY - aabb1.minY)) << "zoom-in rebuild should produce a smaller mesh AABB";
	EXPECT_GT(cameraSwaps(sys), gen1) << "the camera region must swap on zoom-in rebuild";
}

// Pan the viewport driver toward the region edge: once the driver crosses inside
// kEdgeMarginMm of the built edge, the region recenters and rebuilds. The self-gate
// is movement-tied -- a pan that keeps the driver comfortably inside does NOT rebuild
// (see PanWithinMarginNoRebuild below).
TEST_F(NavigationSystemTest, PanPastEdgeMarginTriggersRebuild) {
	ConstructionWorld cw; // empty: just terrain nav, origin area

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(m_chunks.get());
	sys.setConstructionWorld(&cw);

	// First build centered at origin with 60 m radius (built rect [-60 m, 60 m]).
	pushArea(sys, {0, 0}, 60000);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "initial mesh never built";
	const std::uint64_t gen1 = cameraSwaps(sys);

	// Pan the viewport center to 55 m: that is within kEdgeMarginMm (8 m) of the +60 m
	// edge, so the driver crosses the margin and the region must recenter/rebuild.
	pushArea(sys, {55000, 0}, 60000);
	sys.finishPendingBuilds();
	EXPECT_NE(cameraSwaps(sys), gen1)
		<< "pan past the edge margin must trigger a rebuild";
}

// Pan the viewport driver but keep it comfortably inside the built region (and the
// size unchanged): the movement-tied gate stays shut, NO rebuild. This is the camera-
// hysteresis case -- per-frame lerp noise and modest pans must not thrash the mesh.
TEST_F(NavigationSystemTest, PanWithinMarginNoRebuild) {
	ConstructionWorld cw;

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(m_chunks.get());
	sys.setConstructionWorld(&cw);

	pushArea(sys, {0, 0}, 60000);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "initial mesh never built";
	const std::uint64_t gen1 = cameraSwaps(sys);

	// Pan to 5 m (driver stays ~55 m from the +60 m edge, well past the 8 m margin) and a
	// 5% size change (< 20% threshold).
	pushArea(sys, {5000, 0}, 63000); // 63/60 = 1.05 -> 5% delta, driver at 5 m
	// update() launches any rebuild its gates call for before it returns, so one frame is
	// the whole check, however slow the runner.
	sys.update(0.0F);
	EXPECT_FALSE(sys.isBuilding()) << "a pan within the margin must NOT trigger a rebuild";
	EXPECT_EQ(cameraSwaps(sys), gen1);
}

// Half-extent clamped to kMinSimHalfExtentMm: requesting 1 m (below min) builds
// with the minimum extent and a mesh still lands.
TEST_F(NavigationSystemTest, HalfExtentClampedToMin) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(m_chunks.get());
	sys.setConstructionWorld(&cw);

	// Request 1 m: should be clamped to kMinSimHalfExtentMm (30 m).
	pushArea(sys, {0, 0}, 1000);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "clamped-to-min mesh never built";
	// The mesh should span at least the minimum area (30 m each side).
	const Aabb a = meshAabb(firstMesh(sys));
	EXPECT_GE(a.maxX - a.minX, NavigationSystem::kMinSimHalfExtentMm)
		<< "mesh should span at least kMinSimHalfExtentMm";
}

// Half-extent clamped to kMaxSimHalfExtentMm: requesting 2000 m (above max) builds
// with the maximum extent, not the absurd request.
TEST_F(NavigationSystemTest, HalfExtentClampedToMax) {
	ConstructionWorld cw;

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(m_chunks.get());
	sys.setConstructionWorld(&cw);

	// Request 2000 m (2 000 000 mm): should be clamped to kMaxSimHalfExtentMm (64 m).
	pushArea(sys, {0, 0}, 2000000);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "clamped-to-max mesh never built";
	// The mesh AABB should not exceed kMaxSimHalfExtentMm * 2 on any axis.
	const Aabb a = meshAabb(firstMesh(sys));
	const std::int64_t span = std::max(a.maxX - a.minX, a.maxY - a.minY);
	EXPECT_LE(span, NavigationSystem::kMaxSimHalfExtentMm * 2 + 2000) // +2 m tolerance for border
		<< "mesh AABB must not exceed the clamped max extent";
}

// --- In-area placement-completion rebuild (stationary-load regression) --------
//
// The gather reads chunk readiness directly, but a chunk's flora/water can finish
// placement AFTER the first build. needsRebuild keys off the processed-chunk set so
// a chunk that joins the set while overlapping the BUILT area forces a rebuild --
// otherwise a stationary player keeps a mesh missing every obstacle from spawn-ring
// chunks that placed late, until they pan/zoom. (Regression introduced by scoping
// the build to the simulation area; this is the fix.)
TEST_F(NavigationSystemTest, InAreaPlacementCompletionTriggersRebuild) {
	// A blocking tree def so its placement produces a real obstacle ring in the mesh.
	AssetRegistry&	reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName			   = "Test_NavLatePlaceTree";
	tree.collision.type			   = CollisionShapeType::Rect;
	tree.collision.halfExtentsMeters = {0.4F, 0.4F};
	reg.registerTestDefinition(tree);

	// Empty placement to start: no trees stored, no chunks in the processed set, so the
	// first build's in-area chunk signature is empty.
	PlacementExecutor					executor(reg);
	std::unordered_set<ChunkCoordinate> processed;

	ConstructionWorld cw; // empty: just terrain nav over the origin area

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys); // ChunkManager + 60 m area at origin (clamped to 64 m)
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);

	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "initial navmesh never built";
	const std::uint64_t gen1	  = sys.generation();
	const std::size_t	triBefore = firstMesh(sys).triangles.size();

	// Now a spawn-ring chunk finishes placement: a blocking tree well inside the area
	// (chunk (0,0), tile (10,10)) lands in the index and the chunk joins the processed
	// set. The camera has NOT moved and the construction version is unchanged, so the
	// signature flip is the ONLY thing that can trigger the rebuild.
	const glm::vec2		  treeAt{10.0F, 10.0F};
	const ChunkCoordinate coord = world::worldToChunk({treeAt.x, treeAt.y});
	SpatialIndex		  index;
	PlacedEntity		  pe;
	pe.defName	= "Test_NavLatePlaceTree";
	pe.position = treeAt;
	index.insert(pe);
	assets::AsyncChunkPlacementResult result;
	result.coord		= coord;
	result.spatialIndex = std::move(index);
	executor.storeChunkResult(std::move(result));
	processed.insert(coord);

	sys.finishPendingBuilds();
	ASSERT_NE(sys.generation(), gen1)
		<< "in-area placement completion must trigger a rebuild";
	EXPECT_GT(sys.generation(), gen1) << "generation must bump on the placement-driven rebuild";
	// The late tree is now carved in: the obstacle changed the triangulation.
	EXPECT_NE(firstMesh(sys).triangles.size(), triBefore)
		<< "the newly-placed in-area flora must change the mesh triangulation";
}

// The build-over-trees guard: a foundation footprint sitting on a tree is REFUSED by the runtime-mesh
// check (the tree carves a hole) but ALLOWED by isAreaBuildable / isPointBuildable, which validate
// against a terrain-only mesh so the tree becomes a clear task instead of blocking placement.
TEST_F(NavigationSystemTest, FootprintOverTreeIsBuildableButNotOnMesh) {
	AssetRegistry&	reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName					 = "Test_NavBuildableTree";
	tree.collision.type				 = CollisionShapeType::Rect;
	tree.collision.halfExtentsMeters = {0.4F, 0.4F};
	reg.registerTestDefinition(tree);

	PlacementExecutor					executor(reg);
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						treeAt{10.0F, 10.0F};
	const ChunkCoordinate				coord = world::worldToChunk({treeAt.x, treeAt.y});
	{
		SpatialIndex index;
		PlacedEntity pe;
		pe.defName	= "Test_NavBuildableTree";
		pe.position = treeAt;
		index.insert(pe);
		assets::AsyncChunkPlacementResult result;
		result.coord		= coord;
		result.spatialIndex = std::move(index);
		executor.storeChunkResult(std::move(result));
		processed.insert(coord);
	}

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);

	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "initial navmesh never built";

	// A small footprint square centered on the tree (inside the ~0.9 m carved hole).
	const std::vector<glm::vec2> footprint{
		{treeAt.x - 0.3F, treeAt.y - 0.3F},
		{treeAt.x + 0.3F, treeAt.y - 0.3F},
		{treeAt.x + 0.3F, treeAt.y + 0.3F},
		{treeAt.x - 0.3F, treeAt.y + 0.3F},
	};

	EXPECT_FALSE(sys.isAreaWalkable(footprint)) << "the runtime mesh carves the tree as a hole";
	EXPECT_TRUE(sys.isAreaBuildable(footprint)) << "terrain-only: the tree is clearable, not a blocker";
	EXPECT_TRUE(sys.isPointBuildable(treeAt)) << "the tree center is buildable (it becomes a clear task)";
}

// A destructive harvest fells a tree: it is removed from the live chunk index, which leaves
// the chunk membership and construction version UNCHANGED. The placement removal epoch is the
// only signal that the in-area obstacle set shrank, so it must drive the rebuild -- otherwise
// the felled trunk's hole lingers in the mesh and that tile never becomes walkable again.
TEST_F(NavigationSystemTest, InAreaEntityRemovalTriggersRebuildAndReclaimsHole) {
	AssetRegistry&	reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName					 = "Test_NavFellTree";
	tree.collision.type				 = CollisionShapeType::Rect;
	tree.collision.halfExtentsMeters = {0.4F, 0.4F};
	reg.registerTestDefinition(tree);

	// Start WITH the tree placed and its chunk processed, so the first mesh carves the trunk.
	PlacementExecutor					executor(reg);
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						treeAt{10.0F, 10.0F};
	const ChunkCoordinate				coord = world::worldToChunk({treeAt.x, treeAt.y});
	{
		SpatialIndex index;
		PlacedEntity pe;
		pe.defName	= "Test_NavFellTree";
		pe.position = treeAt;
		index.insert(pe);
		assets::AsyncChunkPlacementResult result;
		result.coord		= coord;
		result.spatialIndex = std::move(index);
		executor.storeChunkResult(std::move(result));
		processed.insert(coord);
	}

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);

	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "initial navmesh never built";
	const std::uint64_t gen1		 = sys.generation();
	const std::size_t	triWithTree	 = firstMesh(sys).triangles.size();

	// Fell the tree: remove it from the index. Chunk stays processed, construction version is
	// unchanged -- the removal epoch is the sole trigger for the rebuild.
	ASSERT_TRUE(executor.removeEntity(coord, treeAt, "Test_NavFellTree")) << "tree must be removed from the index";

	sys.finishPendingBuilds();
	ASSERT_NE(sys.generation(), gen1)
		<< "an in-area entity removal must trigger a rebuild (reclaim the trunk hole)";
	EXPECT_GT(sys.generation(), gen1) << "generation must bump on the removal-driven rebuild";
	// Hole reclaimed: the mesh with the tree gone differs from the mesh with it carved in.
	EXPECT_NE(firstMesh(sys).triangles.size(), triWithTree)
		<< "felling the in-area flora must change the mesh triangulation (the trunk hole is reclaimed)";

	// The felled tree's footprint is now walkable ground (locate succeeds + terrain-traversable).
	EXPECT_TRUE(sys.isOnMesh(treeAt)) << "the felled tree's tile must become walkable after the rebuild";
}

// --- Phase C: query LOD seam (inSimArea + isReachable off-area policy) -------
//
// inSimArea returns true for a point inside the BUILT area, false for a point
// outside, and false when no mesh has been built yet (no built area to test
// against). isReachable must return true ("can't prove unreachable") when either
// endpoint is outside the area, even when a mesh is present that would otherwise
// report the goal as disconnected.

// inSimArea returns false before any mesh is built.
TEST_F(NavigationSystemTest, InSimAreaFalseBeforeBuild) {
	ConstructionWorld cw;

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(m_chunks.get());
	sys.setConstructionWorld(&cw);
	// A colonist region wanted but not pumped -- no mesh built yet.
	(void)driveColonistRegion(world, {0.0F, 0.0F}, 60.0F);
	sys.update(0.0F);

	ASSERT_FALSE(sys.hasMesh());
	// Origin is inside the requested area but the built area doesn't exist yet.
	EXPECT_FALSE(sys.inSimArea({0.0F, 0.0F}));
}

// inSimArea returns true for a point inside the built area, false outside it.
TEST_F(NavigationSystemTest, InSimAreaTrueInsideFalseOutside) {
	ConstructionWorld cw;

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(m_chunks.get());
	sys.setConstructionWorld(&cw);

	// 60 m colonist region at origin: (-60 m, -60 m) to (+60 m, +60 m).
	(void)driveColonistRegion(world, {0.0F, 0.0F}, 60.0F);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	// A point well inside the area.
	EXPECT_TRUE(sys.inSimArea({0.0F, 0.0F}));
	EXPECT_TRUE(sys.inSimArea({30.0F, 30.0F}));  // exactly on the edge is <=, so inside
	// A point clearly outside the area (100 m from center, half-extent 60 m).
	EXPECT_FALSE(sys.inSimArea({100.0F, 0.0F}));
	EXPECT_FALSE(sys.inSimArea({0.0F, 100.0F}));
}

// --- isValidPosition: the canonical spawn/placement validity predicate ----------
//
// isValidPosition: a position is valid IFF it sits on a walkable
// nav face inside an active region. A walkable face is valid; a water face inside the
// region is invalid; a point no region covers is invalid (can't place off-mesh yet).

TEST_F(NavigationSystemTest, IsValidPositionTrueOnWalkableFaceFalseOnWaterAndOffMesh) {
	// Mostly-land world with a pond at (40,40) inside the origin chunk.
	auto chunks = std::make_unique<world::ChunkManager>(waterPatchSampler());
	chunks->setLoadRadius(2);
	chunks->setUnloadRadius(4);
	chunks->update({0.0F, 0.0F});
	chunks->finishPendingGeneration();

	ConstructionWorld cw; // empty: open terrain plus the pond obstacle

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	(void)driveColonistRegion(world, {0.0F, 0.0F}, 60.0F); // covers origin and the patch
	sys.setConstructionWorld(&cw);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	// A clearly-land point near origin sits on a walkable face: VALID.
	const glm::vec2 land{2.0F, 2.0F};
	ASSERT_TRUE(sys.inSimArea(land)) << "land point must be inside the region";
	EXPECT_TRUE(sys.isValidPosition(land)) << "a walkable land face must be a valid position";

	// A point inside the pond is off-mesh (water face): INVALID, but still in-region.
	const glm::vec2 water{40.0F, 40.0F};
	ASSERT_TRUE(sys.inSimArea(water)) << "the water point must be inside the region";
	EXPECT_FALSE(sys.isOnMesh(water)) << "the water ring must not be a walkable face";
	EXPECT_FALSE(sys.isValidPosition(water)) << "a water face must be an invalid position";

	// A point no region covers is invalid: you cannot place outside an active mesh yet.
	const glm::vec2 offMesh{500.0F, 500.0F};
	ASSERT_FALSE(sys.inSimArea(offMesh)) << "the far point must be outside every region";
	EXPECT_FALSE(sys.isValidPosition(offMesh)) << "a point off every region must be invalid";
}

// Chunks stream in one at a time, so a region can build while only the diagonal chunks
// around a corner are ready. Water crossing that corner then arrives as two clipped
// pieces touching at the corner alone, and the land around them has a pinched boundary.
// That land must still be walkable (WOR-493: the whole region read unwalkable).
TEST_F(NavigationSystemTest, LandAroundWaterPinchedAtAChunkCornerIsValid) {
	auto chunks = std::make_unique<world::ChunkManager>(cornerPondSampler());
	chunks->setLoadRadius(0);
	chunks->setUnloadRadius(8);
	chunks->update({100.0F, 100.0F});	// chunk (0, 0)
	chunks->update({-100.0F, -100.0F}); // chunk (-1, -1)
	chunks->finishPendingGeneration();
	ASSERT_EQ(chunks->getChunk({0, -1}), nullptr);
	ASSERT_EQ(chunks->getChunk({-1, 0}), nullptr);

	// The premise: each diagonal chunk's pond piece ends at the shared corner.
	for (const ChunkCoordinate coord : {ChunkCoordinate{0, 0}, ChunkCoordinate{-1, -1}}) {
		const world::Chunk* chunk = chunks->getChunk(coord);
		ASSERT_NE(chunk, nullptr);
		ASSERT_TRUE(chunk->isReady());
		bool touchesCorner = false;
		for (const world::TerrainRing& ring : chunk->terrainPolygons().navRings) {
			touchesCorner = touchesCorner || (ring.blocksMovement &&
											  std::find(ring.ring.begin(), ring.ring.end(), Vec2i64{0, 0}) != ring.ring.end());
		}
		ASSERT_TRUE(touchesCorner) << "chunk (" << coord.x << ", " << coord.y << ") has no water piece ending at (0, 0)";
	}

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	pushArea(sys, {5330, 4200}, 64000);
	sys.setConstructionWorld(&cw);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	for (const glm::vec2 land : {glm::vec2{15.0F, 15.0F}, glm::vec2{-15.0F, -15.0F}, glm::vec2{3.0F, -3.0F}, glm::vec2{-3.0F, 3.0F},
								 glm::vec2{40.0F, 30.0F}, glm::vec2{-30.0F, -20.0F}}) {
		EXPECT_TRUE(sys.isValidPosition(land)) << "land at (" << land.x << ", " << land.y << ") must be a valid position";
	}
	for (const glm::vec2 water : {glm::vec2{3.0F, 3.0F}, glm::vec2{-3.0F, -3.0F}}) {
		EXPECT_FALSE(sys.isValidPosition(water)) << "the pond at (" << water.x << ", " << water.y << ") must not be";
	}
}

// The landing snap: before any sim region or construction world exists, a drop point in
// the water moves to the nearest walkable terrain beside it, and a land point stays put.
TEST_F(NavigationSystemTest, NearestTerrainWalkablePointLeavesWaterBeforeAnyRegion) {
	auto chunks = std::make_unique<world::ChunkManager>(waterPatchSampler());
	chunks->setLoadRadius(2);
	chunks->setUnloadRadius(4);
	chunks->update({0.0F, 0.0F});
	chunks->finishPendingGeneration();

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	EXPECT_FALSE(sys.nearestTerrainWalkablePoint({40.0F, 40.0F}).has_value()) << "no chunks wired";
	sys.setChunkManager(chunks.get());
	ASSERT_FALSE(sys.hasMesh());

	const glm::vec2				   land{2.0F, 2.0F};
	const std::optional<glm::vec2> stay = sys.nearestTerrainWalkablePoint(land);
	ASSERT_TRUE(stay.has_value());
	EXPECT_EQ(*stay, land);

	// The snap from the pond's center lands on its nearest shore, the least rim radius away.
	const float					   shore = patchPondNearestShore();
	const std::optional<glm::vec2> moved = sys.nearestTerrainWalkablePoint({40.0F, 40.0F});
	ASSERT_TRUE(moved.has_value());
	const float dx = moved->x - 40.0F;
	const float dy = moved->y - 40.0F;
	const float d  = std::sqrt(dx * dx + dy * dy);
	EXPECT_GT(d, shore - 0.5F) << "the snap must leave the water";
	EXPECT_LT(d, shore + 1.5F) << "the snap must land on the nearest shore, not somewhere far";
	const std::optional<glm::vec2> again = sys.nearestTerrainWalkablePoint(*moved);
	ASSERT_TRUE(again.has_value());
	EXPECT_EQ(*again, *moved) << "the snapped point is itself walkable terrain";
}

// A party landing whose drop is in the water beside a shore: every member must spawn on
// walkable ground of its own (the 1.5 m ring must not straddle the shore), apart from the
// others, and near the drop. Checked against the runtime mesh built afterwards, the same
// isValidPosition the recovery snap would otherwise have to fix.
TEST_F(NavigationSystemTest, GroupSpawnPointsFromWaterDropAllLandOnWalkableGround) {
	auto chunks = std::make_unique<world::ChunkManager>(waterPatchSampler());
	chunks->setLoadRadius(2);
	chunks->setUnloadRadius(4);
	chunks->update({0.0F, 0.0F});
	chunks->finishPendingGeneration();

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());

	// The pond's west shore is x = 32; (34, 40) is ~2 m in from it.
	const glm::vec2	 drop{34.0F, 40.0F};
	constexpr size_t kCrew		 = 6;
	constexpr float	 kRing		 = 1.5F;
	constexpr float	 kSeparation = 0.8F;
	constexpr float	 kClearance	 = 0.3F;
	ASSERT_NE(sys.nearestTerrainWalkablePoint(drop), std::optional<glm::vec2>(drop)) << "the drop must be in the water";
	const NavigationSystem::GroupSpawn spawn = sys.groupSpawnPoints(drop, kCrew, kRing, kSeparation, kClearance);
	ASSERT_EQ(spawn.points.size(), kCrew);

	ConstructionWorld cw;
	sys.setConstructionWorld(&cw);
	pushArea(sys, {40000, 40000}, 40000);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	EXPECT_TRUE(sys.isValidPosition(spawn.center)) << "the clearing center is on land";
	for (std::size_t i = 0; i < spawn.points.size(); ++i) {
		const glm::vec2 p = spawn.points[i];
		EXPECT_TRUE(sys.isValidPosition(p)) << "member " << i << " at (" << p.x << ", " << p.y << ") is not walkable";
		for (int k = 0; k < 8; ++k) {
			const float angle = 6.2831853F * static_cast<float>(k) / 8.0F;
			EXPECT_TRUE(sys.isValidPosition(p + glm::vec2{std::cos(angle), std::sin(angle)} * kClearance))
				<< "member " << i << "'s disc reaches the water";
		}
		for (std::size_t j = 0; j < i; ++j) {
			const glm::vec2 d = p - spawn.points[j];
			EXPECT_GE(std::sqrt(d.x * d.x + d.y * d.y), kSeparation - 1e-4F) << "members " << j << " and " << i << " stack";
		}
		const glm::vec2 fromDrop = p - drop;
		EXPECT_LT(std::sqrt(fromDrop.x * fromDrop.x + fromDrop.y * fromDrop.y), 8.0F) << "member " << i << " landed far away";
	}

	// A larger crew than fits evenly on the ring still spreads out.
	const NavigationSystem::GroupSpawn crowd = sys.groupSpawnPoints(drop, 12, kRing, kSeparation, kClearance);
	ASSERT_EQ(crowd.points.size(), 12U);
	for (std::size_t i = 0; i < crowd.points.size(); ++i) {
		EXPECT_TRUE(sys.isValidPosition(crowd.points[i]));
		for (std::size_t j = 0; j < i; ++j) {
			const glm::vec2 d = crowd.points[i] - crowd.points[j];
			EXPECT_GE(std::sqrt(d.x * d.x + d.y * d.y), kSeparation - 1e-4F);
		}
	}
}

// nearestTerrainWalkablePoint must not report a point INSIDE a built wall's band as
// walkable: the terrain-only mesh treats a solid wall face as belief-gated (open under the
// most-optimistic terrain predicate), so the query needs the truth predicate instead. A point
// dead-center on a solid span is pushed out past the wall's half-thickness; a point centered on
// a pathable door opening is left alone.
TEST_F(NavigationSystemTest, NearestTerrainWalkablePointStepsOutOfBuiltWallBand) {
	constexpr float kWallHalfThickness = 0.10F; // Wood/Standard, from materials.xml

	ConstructionWorld cw;
	SegmentId		  wall = buildWall(cw, {0, 0}, {6000, 0});
	OpeningId		  door = cw.addOpening(wall, 0.5F, "Door", "Wood"); // t=0.5 -> x=3.0 m
	ASSERT_NE(door, kInvalidOpening);
	ASSERT_TRUE(cw.setOpeningState(door, FoundationState::Built));

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(m_chunks.get());
	sys.setConstructionWorld(&cw);

	// A solid span of the wall: the query point sits dead-center in the band.
	const glm::vec2				solid = {1.0F, 0.0F};
	const std::optional<glm::vec2> moved = sys.nearestTerrainWalkablePoint(solid);
	ASSERT_TRUE(moved.has_value());
	EXPECT_GE(std::abs(moved->y - solid.y), kWallHalfThickness)
		<< "the snap must clear the solid wall's band, not report a point inside it as walkable";

	// The door's opening: a point there is genuinely walkable in truth, so it must stay put.
	const glm::vec2				  atDoor  = {3.0F, 0.0F};
	const std::optional<glm::vec2> throughDoor = sys.nearestTerrainWalkablePoint(atDoor);
	ASSERT_TRUE(throughDoor.has_value());
	EXPECT_EQ(*throughDoor, atDoor) << "a pathable door opening must stay walkable";
}

// groupSpawnPoints must never place a landing-party member (or its clearance disc) inside a
// built wall's band, even when the drop is centered on the wall.
TEST_F(NavigationSystemTest, GroupSpawnPointsNeverLandsInsideBuiltWallBand) {
	constexpr float kWallHalfThickness = 0.10F; // Wood/Standard, from materials.xml

	ConstructionWorld cw;
	buildWall(cw, {5000, -8000}, {5000, 8000}); // a long solid wall along x = 5 m, no opening

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(m_chunks.get());
	sys.setConstructionWorld(&cw);

	const glm::vec2	 drop{5.0F, 0.0F}; // dead-center on the wall
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
			const float		angle = 6.2831853F * static_cast<float>(k) / 8.0F;
			const glm::vec2 disc  = p + glm::vec2{std::cos(angle), std::sin(angle)} * kClearance;
			EXPECT_FALSE(insideBand(disc)) << "member " << i << "'s clearance disc reaches into the wall";
		}
	}
}

// --- Whole-footprint walkability: isAreaWalkable / isSegmentWalkable -------------
//
// Vertex-only validity is insufficient: a footprint can have every corner on land yet
// span water, or fully enclose a water pond. These predicates sample edges + interior
// (isAreaWalkable) or the centerline (isSegmentWalkable) so any off-mesh part rejects.
// All use waterPatchSampler()'s pond at (40,40), at most 10.5 m from its center to any shore.

namespace {
	// Set up a system over the water-patch world + a 60 m origin region with a built mesh.
	// Returns the ChunkManager the caller must keep alive for the system's lifetime.
	std::unique_ptr<world::ChunkManager> wireWaterPatch(World& world, NavigationSystem& sys, ConstructionWorld& cw) {
		auto chunks = std::make_unique<world::ChunkManager>(waterPatchSampler());
		chunks->setLoadRadius(2);
		chunks->setUnloadRadius(4);
		chunks->update({0.0F, 0.0F});
		chunks->finishPendingGeneration();
		sys.setChunkManager(chunks.get());
		// 60 m colonist region at origin, as the existing water-patch test wires it: covers the
		// origin land AND the pond.
		(void)driveColonistRegion(world, {0.0F, 0.0F}, 60.0F);
		sys.setConstructionWorld(&cw);
		return chunks;
	}
} // namespace

TEST_F(NavigationSystemTest, IsAreaWalkableTrueWhenWhollyOnLand) {
	ConstructionWorld cw;
	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	auto chunks = wireWaterPatch(world, sys, cw);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	// A 4 m square wholly on land (well clear of the pond).
	const std::vector<glm::vec2> onLand{{4.0F, 4.0F}, {8.0F, 4.0F}, {8.0F, 8.0F}, {4.0F, 8.0F}};
	for (const auto& v : onLand) {
		ASSERT_TRUE(sys.isValidPosition(v)) << "each corner must be on walkable land for the test premise";
	}
	EXPECT_TRUE(sys.isAreaWalkable(onLand)) << "a footprint wholly on land must be walkable";
}

TEST_F(NavigationSystemTest, IsAreaWalkableFalseWhenFootprintSpansWater) {
	ConstructionWorld cw;
	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	auto chunks = wireWaterPatch(world, sys, cw);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	// Corners straddle the pond horizontally: x=28 and x=52 are LAND (12 m from its center,
	// past its 10.5 m rim), but the footprint's middle (and its top/bottom edges) crosses the water.
	// Vertex-only validation would WRONGLY accept this; the edge samples catch the water.
	const std::vector<glm::vec2> spanning{{28.0F, 38.0F}, {52.0F, 38.0F}, {52.0F, 42.0F}, {28.0F, 42.0F}};
	ASSERT_TRUE(sys.isValidPosition(spanning[0])) << "left corners must be on land (premise)";
	ASSERT_TRUE(sys.isValidPosition(spanning[1])) << "right corners must be on land (premise)";
	ASSERT_FALSE(sys.isValidPosition({40.0F, 40.0F})) << "the span's middle must be water (premise)";
	EXPECT_FALSE(sys.isAreaWalkable(spanning)) << "a footprint spanning water must be rejected";
}

TEST_F(NavigationSystemTest, IsAreaWalkableFalseWhenFootprintEnclosesWaterPond) {
	ConstructionWorld cw;
	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	auto chunks = wireWaterPatch(world, sys, cw);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	// A big square whose corners AND edges are all on land but that fully encloses the
	// pond. Only the INTERIOR grid sampling can catch this -- the edge pass alone would
	// pass. Corners at 28 and 52 are land; edges run along y=28/y=52 and x=28/x=52, all
	// outside the pond.
	const std::vector<glm::vec2> enclosing{{28.0F, 28.0F}, {52.0F, 28.0F}, {52.0F, 52.0F}, {28.0F, 52.0F}};
	for (const auto& v : enclosing) {
		ASSERT_TRUE(sys.isValidPosition(v)) << "each corner must be on land (premise)";
	}
	ASSERT_FALSE(sys.isValidPosition({40.0F, 40.0F})) << "the enclosed center must be water (premise)";
	EXPECT_FALSE(sys.isAreaWalkable(enclosing)) << "a footprint enclosing a water pond must be rejected";
}

// Water is geography, not a clearable entity, so it must block the terrain-only BUILDABLE checks too
// (unlike a tree, which is buildable-over). Guards the terrain cache: a footprint spanning or enclosing
// the pond is unbuildable, a point in the water is not a buildable vertex, and land is.
TEST_F(NavigationSystemTest, WaterBlocksBuildabilityUnlikeTrees) {
	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys	 = world.registerSystem<NavigationSystem>();
	auto			  chunks = wireWaterPatch(world, sys, cw);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	const std::vector<glm::vec2> spanning{{28.0F, 38.0F}, {52.0F, 38.0F}, {52.0F, 42.0F}, {28.0F, 42.0F}};
	const std::vector<glm::vec2> enclosing{{28.0F, 28.0F}, {52.0F, 28.0F}, {52.0F, 52.0F}, {28.0F, 52.0F}};
	const std::vector<glm::vec2> onLand{{4.0F, 4.0F}, {8.0F, 4.0F}, {8.0F, 8.0F}, {4.0F, 8.0F}};

	EXPECT_FALSE(sys.isAreaBuildable(spanning)) << "a footprint spanning water is not buildable";
	EXPECT_FALSE(sys.isAreaBuildable(enclosing)) << "a footprint enclosing a water pond is not buildable";
	EXPECT_FALSE(sys.isPointBuildable({40.0F, 40.0F})) << "a point in the water is not a buildable vertex";
	EXPECT_TRUE(sys.isAreaBuildable(onLand)) << "a footprint wholly on land is buildable";
	EXPECT_TRUE(sys.isPointBuildable({6.0F, 6.0F})) << "a point on land is a buildable vertex";
}

TEST_F(NavigationSystemTest, IsSegmentAndPolylineWalkableCatchWaterCrossing) {
	ConstructionWorld cw;
	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	auto chunks = wireWaterPatch(world, sys, cw);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	// A wall segment fully on land is walkable.
	EXPECT_TRUE(sys.isSegmentWalkable({4.0F, 4.0F}, {8.0F, 4.0F})) << "an all-land segment must be walkable";

	// A segment whose endpoints are land (x=28, x=52) but that crosses the pond
	// is rejected: the centerline samples land inside the water.
	EXPECT_FALSE(sys.isSegmentWalkable({28.0F, 40.0F}, {52.0F, 40.0F})) << "a segment crossing water must be rejected";

	// A chain with one good segment and one water-crossing segment is rejected as a whole.
	const std::vector<glm::vec2> chain{{4.0F, 4.0F}, {8.0F, 4.0F}, {28.0F, 40.0F}, {52.0F, 40.0F}};
	EXPECT_FALSE(sys.isPolylineWalkable(chain)) << "a chain with any water-crossing segment must be rejected";

	// An all-land chain passes.
	const std::vector<glm::vec2> landChain{{4.0F, 4.0F}, {8.0F, 4.0F}, {8.0F, 8.0F}};
	EXPECT_TRUE(sys.isPolylineWalkable(landChain)) << "an all-land chain must be walkable";
}

// Before any mesh is built nothing is placeable: isValidPosition is false everywhere.
TEST_F(NavigationSystemTest, IsValidPositionFalseBeforeAnyMesh) {
	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();

	ASSERT_FALSE(sys.hasMesh());
	EXPECT_FALSE(sys.isValidPosition({0.0F, 0.0F}));
}

// Dev-spawn refusal contract: every dev verb that places an entity guards on
// isValidPosition and, on false, creates NOTHING. This mirrors the exact guard in
// DevCommandHandler::devSpawn (the app binary has no test target, so the guard's
// decision is exercised here against a real NavigationSystem + World): an off-mesh
// position must leave the entity count unchanged; a valid one creates the entity.
TEST_F(NavigationSystemTest, DevSpawnGuardRefusesOffMeshCreatesOnValid) {
	ConstructionWorld cw; // empty: open walkable terrain over the origin region

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys); // ChunkManager (all land) + 60 m region at origin
	sys.setConstructionWorld(&cw);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	// The guard the dev verbs run before any createEntity. createNothing == refused.
	auto guardedSpawn = [&](glm::vec2 at) -> bool {
		if (!sys.isValidPosition(at)) {
			return false; // refused: create nothing
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

	// Off-mesh (no region covers it): refused, nothing created.
	const std::size_t before = entityCount();
	EXPECT_FALSE(guardedSpawn({500.0F, 500.0F})) << "off-mesh dev-spawn must be refused";
	EXPECT_EQ(entityCount(), before) << "a refused dev-spawn must create nothing";

	// On a walkable face: accepted, exactly one entity created.
	EXPECT_TRUE(guardedSpawn({2.0F, 2.0F})) << "on-mesh dev-spawn must be accepted";
	EXPECT_EQ(entityCount(), before + 1) << "an accepted dev-spawn creates exactly one entity";
}

// isReachable returns true for an off-area goal even when a mesh is present and
// the goal would be disconnected inside the area (because the mesh doesn't cover
// the goal's location -- a false negative must not occur).
TEST_F(NavigationSystemTest, IsReachableTrueForOffAreaGoal) {
	ConstructionWorld cw;
	// Closed room at origin: an in-area goal inside would return false (disconnected).
	buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	// Confirm in-area walled-off goal IS false (baseline: the closed room blocks it).
	EXPECT_FALSE(isReachable(sys, kOutside, kInside, kAgentRadius))
		<< "in-area closed-room goal should be false (baseline for this test)";

	// Now try a goal far outside the built area: must return true ("can't prove unreachable").
	const glm::vec2 offAreaGoal{200.0F, 200.0F}; // well outside 60 m area
	EXPECT_TRUE(isReachable(sys, kOutside, offAreaGoal, kAgentRadius))
		<< "off-area goal must return true (can't prove unreachable)";

	// Also true when START is off-area.
	const glm::vec2 offAreaStart{200.0F, 0.0F};
	EXPECT_TRUE(isReachable(sys, offAreaStart, kInside, kAgentRadius))
		<< "off-area start must return true (can't prove unreachable)";
}

// --- Memory::beliefVersion bump semantics ------------------------------------

// rememberSegment bumps beliefVersion only on a NEW id; re-remembering does not;
// forgetSegment bumps when it actually erases.
TEST(MemoryBeliefVersionTest, RememberAndForgetBumpVersion) {
	Memory mem;
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
//
// The replan-on-discovery loop fires when the stored stamps drift from the
// colonist's current beliefVersion or the NavigationSystem generation. This
// exercises the version-compare predicate directly (the full AIDecisionSystem
// wiring -- task assignment, vision discovery, movement -- is exercised in the
// sandbox, not here).

namespace {
	// Mirror of the predicate in AIDecisionSystem::update's replan-on-discovery loop.
	bool navPathStale(const NavPath& path, std::uint64_t beliefVersion, std::uint64_t navGeneration) {
		return path.builtBeliefVersion != beliefVersion || path.builtNavVersion != navGeneration;
	}
} // namespace

TEST(NavPathStalenessTest, DetectsBeliefAndNavDrift) {
	NavPath path;
	path.valid = true;
	path.builtBeliefVersion = 5;
	path.builtNavVersion = 2;

	// Same stamps => fresh, no repath.
	EXPECT_FALSE(navPathStale(path, 5, 2));

	// Belief advanced (a wall discovered) => stale.
	EXPECT_TRUE(navPathStale(path, 6, 2));

	// Mesh rebuilt under the colonist => stale.
	EXPECT_TRUE(navPathStale(path, 5, 3));

	// Both moved => stale.
	EXPECT_TRUE(navPathStale(path, 6, 3));
}

// --- Off-mesh recovery: AIDecisionSystem snaps a stranded colonist onto the mesh ---
//
// End-to-end exercise of the recovery at the top of AIDecisionSystem::update against a
// REAL NavigationSystem mesh: a colonist standing off a walkable face BUT INSIDE a sim
// region is snapped to the nearest walkable face. This is the common recovery branch
// (nearestPathablePoint succeeds); the last-resort landing-coords branch is covered as a
// pure predicate in AIDecisionSystem.test.cpp's OffMeshRecoveryResolutionTest.
//
// Helper: spawn a minimal colonist entity at `at` (no Colonist tag, so it does not drive
// its own region -- the colonist region wireArea makes is what covers it).
namespace {
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

TEST_F(NavigationSystemTest, OffMeshColonistSnappedOntoMesh) {
	// A blocking tree gives a common-knowledge terrain blocker (kProvenanceTree, faceBlocker
	// < 0) inside the region -- a face isOnMesh rejects (unlike a wall, which terrainTraversable
	// treats as open). Standing the colonist on the tree is the genuinely-stranded, in-region
	// case (the real bug was a colonist beelined onto a water/tree face before the mesh built).
	AssetRegistry&	reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName					 = "Test_NavRecoveryTree";
	tree.collision.type				 = CollisionShapeType::Rect;
	tree.collision.halfExtentsMeters = {0.8F, 0.8F};
	reg.registerTestDefinition(tree);

	PlacementExecutor					executor(reg);
	std::unordered_set<ChunkCoordinate> processed;
	const glm::vec2						treeAt{10.0F, 10.0F}; // well inside the 60 m origin region
	const ChunkCoordinate				coord = world::worldToChunk({treeAt.x, treeAt.y});
	SpatialIndex						index;
	PlacedEntity						pe;
	pe.defName	= "Test_NavRecoveryTree";
	pe.position = treeAt;
	index.insert(pe);
	assets::AsyncChunkPlacementResult placed;
	placed.coord		= coord;
	placed.spatialIndex = std::move(index);
	executor.storeChunkResult(std::move(placed));
	processed.insert(coord);

	ConstructionWorld cw; // empty: open terrain + the one tree obstacle

	World world;
	NavigationSystem& nav = world.registerSystem<NavigationSystem>();
	wireArea(world, nav);
	nav.setPlacementData(&executor, &processed);
	nav.setConstructionWorld(&cw);
	nav.finishPendingBuilds();
	ASSERT_TRUE(nav.hasMesh()) << "navmesh never built";

	AIDecisionSystem& ai = world.registerSystem<AIDecisionSystem>(
		assets::AssetRegistry::Get(), assets::RecipeRegistry::Get(), 1234u);
	ai.setNavigationSystem(&nav);
	ai.setColonyOrigin(glm::vec2{0.0F, 0.0F});

	// Stand the colonist ON the tree (a blocked terrain face): inside the built region (so
	// inSimArea is true) but off a walkable face (so isOnMesh is false). The gate admits this
	// stranded case; nearestPathablePoint finds the nearest walkable face and the colonist
	// snaps ONTO the mesh.
	const glm::vec2 offMesh = treeAt;
	ASSERT_TRUE(nav.inSimArea(offMesh)) << "test point must be inside the sim region";
	ASSERT_FALSE(nav.isOnMesh(offMesh)) << "the colonist must start on a blocked (tree) face";

	EntityID colonist = spawnRecoverableColonist(world, offMesh);
	ai.update(0.016F);

	const auto* pos = world.getComponent<Position>(colonist);
	ASSERT_NE(pos, nullptr);
	EXPECT_NE(pos->value, offMesh) << "recovery must move the in-region off-mesh colonist";
	EXPECT_TRUE(nav.isOnMesh(pos->value)) << "the colonist must land on a walkable mesh face";
}

// Band-aware recovery snap: the radius overload of nearestPathablePoint must return a
// point that clears the BUILT-wall collision band (the same halfThickness + radius
// WallCollisionSystem enforces). This is the colonist-wall-trap fix. A recovered point
// that sits inside the band is the one WallCollisionSystem ejects, and if that ejection
// lands off-mesh (a region edge or an adjacent carve), the snap and the push oscillate
// forever. The fix guarantees the recovered point is OUTSIDE the band, so the safety-net
// has nothing left to move.
TEST_F(NavigationSystemTest, RecoverySnapClearsWallCollisionBand) {
	ConstructionWorld cw;
	// One built Wood/Standard wall along the x-axis (centerline y=0). halfThickness 0.10 m;
	// for a 0.30 m agent the collision clearance is 0.40 m from the centerline.
	buildWall(cw, {0, 0}, {4000, 0});

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	const float kWallHalfThickness = 0.10F; // Wood/Standard, from materials.xml
	const float kClearance		   = kWallHalfThickness + kAgentRadius; // 0.40 m

	auto distToWall = [](glm::vec2 p) {
		// The wall centerline is the segment y=0, x in [0,4]; x stays in range here, so the
		// distance to it is just |y|.
		return std::abs(p.y);
	};

	// A query point right beside the wall, INSIDE the collision band (0.15 m from centerline
	// < 0.40 m clearance). nearestPathablePoint(.,radius) must move it clear of the band.
	const glm::vec2 nearWall{2.0F, 0.15F};
	const std::optional<glm::vec2> clearSnap = sys.nearestPathablePoint(nearWall, kAgentRadius);
	ASSERT_TRUE(clearSnap.has_value()) << "recovery must find a walkable point beside the wall";
	EXPECT_TRUE(sys.isOnMesh(*clearSnap)) << "the recovered point must be on a walkable face";
	EXPECT_GE(distToWall(*clearSnap), kClearance)
		<< "band-aware snap landed inside the wall collision band at " << clearSnap->x << "," << clearSnap->y;

	// The point fed in is itself inside the band: prove the call actually had to move it out,
	// i.e. the clearance is what the overload guarantees (not an accident of the input).
	EXPECT_LT(distToWall(nearWall), kClearance) << "test setup: the input point should start inside the band";
}

// Recovery GATE: a colonist that NO region covers is left exactly where it stands. The
// rework must not relocate an off-camera/unsimulated colonist merely because the sim area
// moved away (the camera-pan river-snap bug). Distinct from the in-region case above.
TEST_F(NavigationSystemTest, OffRegionColonistNotRelocated) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World world;
	NavigationSystem& nav = world.registerSystem<NavigationSystem>();
	wireArea(world, nav); // colonist region at origin, 60 m
	nav.setConstructionWorld(&cw);
	nav.finishPendingBuilds();
	ASSERT_TRUE(nav.hasMesh()) << "navmesh never built";

	AIDecisionSystem& ai = world.registerSystem<AIDecisionSystem>(
		assets::AssetRegistry::Get(), assets::RecipeRegistry::Get(), 1234u);
	ai.setNavigationSystem(&nav);
	ai.setColonyOrigin(glm::vec2{2.0F, 1.5F});

	// 500 m from origin: no region covers it (inSimArea false). The gate must leave it put.
	const glm::vec2 farAway{500.0F, 500.0F};
	ASSERT_FALSE(nav.inSimArea(farAway)) << "test point must be outside every region";

	EntityID colonist = spawnRecoverableColonist(world, farAway);
	ai.update(0.016F);

	const auto* pos = world.getComponent<Position>(colonist);
	ASSERT_NE(pos, nullptr);
	EXPECT_EQ(pos->value, farAway) << "a colonist no region covers must NOT be relocated";
}

// --- findValidPositionNear: place a thing a short distance off an origin --------
//
// The reusable "give me a walkable spot >= minDist from here" primitive behind the
// packaged-furniture placement (a crafted box must come out beside its station, not
// on top of it) and, later, the items-in-river drop fix. Deterministic: a fixed angle
// set with a fixed start and no RNG, so identical inputs yield identical points.

// Open ground: the result is on mesh, about minDist from the origin, and not the origin.
TEST_F(NavigationSystemTest, FindValidPositionNearOpenGroundReturnsPointAtDistance) {
	ConstructionWorld cw; // empty: open walkable terrain over the origin region
	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	const glm::vec2 origin{2.0F, 2.0F};
	ASSERT_TRUE(sys.isOnMesh(origin)) << "origin must be on open ground for the premise";

	const float					   minDist = 1.5F;
	const std::optional<glm::vec2> result = sys.findValidPositionNear(origin, minDist);
	ASSERT_TRUE(result.has_value()) << "open ground must yield a nearby valid position";
	EXPECT_TRUE(sys.isOnMesh(*result)) << "the result must sit on walkable mesh";
	EXPECT_NE(*result, origin) << "the result must be offset from the origin";
	const float dist = glm::distance(origin, *result);
	EXPECT_GE(dist, minDist - 0.01F) << "the result must be at least minDist away";
	EXPECT_NEAR(dist, minDist, 0.01F) << "an open-ground hit sits exactly on the minDist ring";
}

// Origin wedged inside a water hole whose radius exceeds minDist: every ring sample at
// minDist is off-mesh, so the Tier-3 fallback snaps to the nearest walkable face -- still
// on mesh and at least minDist away (the hole boundary is farther than minDist here).
TEST_F(NavigationSystemTest, FindValidPositionNearInPocketFallsBackToNearestWalkable) {
	ConstructionWorld cw;
	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	auto chunks = wireWaterPatch(world, sys, cw); // pond at world (40,40)
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	// Center of the pond: in-region but off-mesh, and a 1 m ring around it is wholly
	// inside the water, 5.5 m or more from any shore, so no ring sample is walkable -> Tier-3 fallback.
	const glm::vec2 origin{40.0F, 40.0F};
	ASSERT_TRUE(sys.inSimArea(origin)) << "origin must be inside the region";
	ASSERT_FALSE(sys.isOnMesh(origin)) << "origin must be off-mesh (inside the water hole)";

	const float					   minDist = 1.0F;
	const std::optional<glm::vec2> result = sys.findValidPositionNear(origin, minDist);
	ASSERT_TRUE(result.has_value()) << "the region has walkable land, so a result must exist";
	EXPECT_TRUE(sys.isOnMesh(*result)) << "the fallback result must be on walkable mesh";
	EXPECT_GE(glm::distance(origin, *result), minDist - 0.01F)
		<< "the fallback result must be at least minDist from the origin";
}

// Tier-3 PUSH branch (distinct from the early-return the pocket test above hits): the origin
// sits in the pond, ~5.5 m off its nearest shore, and minDist reaches past the region on every
// side, so no ring sample is on mesh. nearestPathableOnMesh(origin) lands on the shore (< minDist),
// so the fallback pushes that point radially out to minDist -- which overshoots the region's
// walkable floor. Pre-fix that off-mesh pushed point was returned verbatim (a target a colonist
// could never path to, stalling the place goal); the isOnMesh gate now falls back to the shore
// point. We assert the result is on mesh, which fails without the gate and passes with it.
TEST_F(NavigationSystemTest, FindValidPositionNearPushedFallbackStaysOnMesh) {
	ConstructionWorld cw;
	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	auto chunks = wireWaterPatch(world, sys, cw); // pond at world (40,40), region [-60, 60] on both axes
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	// The region's farthest corner, (-60, -60), is 141.4 m from the origin, so a 150 m ring clears
	// the region and every ring sample is off-mesh -> the Tier-3 push fires.
	const glm::vec2 origin{40.0F, 40.0F};
	const float		minDist = 150.0F;
	ASSERT_TRUE(sys.inSimArea(origin)) << "origin must be inside the region";
	ASSERT_FALSE(sys.isOnMesh(origin)) << "origin must be off-mesh (in the pond)";
	const std::optional<glm::vec2> nearest = sys.findValidPositionNear(origin, 0.0F);
	ASSERT_TRUE(nearest.has_value()) << "the shore gives a nearest walkable point";
	ASSERT_LT(glm::distance(origin, *nearest), minDist) << "nearest shore point must be inside minDist (push premise)";
	ASSERT_FALSE(sys.isOnMesh(origin + (*nearest - origin) / glm::distance(origin, *nearest) * minDist))
		<< "the push must land off the mesh (premise)";

	const std::optional<glm::vec2> result = sys.findValidPositionNear(origin, minDist);
	ASSERT_TRUE(result.has_value()) << "the region has walkable land, so a result must exist";
	EXPECT_TRUE(sys.isOnMesh(*result))
		<< "the pushed Tier-3 fallback must be re-validated on mesh, not returned off-mesh past the region";
}

// Determinism: two identical calls return the exact same point (no RNG, fixed angle set).
TEST_F(NavigationSystemTest, FindValidPositionNearIsDeterministic) {
	ConstructionWorld cw;
	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	const glm::vec2 origin{3.0F, 3.0F};
	const std::optional<glm::vec2> a = sys.findValidPositionNear(origin, 1.0F, {0.5F, 0.5F});
	const std::optional<glm::vec2> b = sys.findValidPositionNear(origin, 1.0F, {0.5F, 0.5F});
	ASSERT_TRUE(a.has_value());
	ASSERT_TRUE(b.has_value());
	EXPECT_EQ(*a, *b) << "identical inputs must yield an identical point (deterministic)";
}

// preferredDir={1,0} on open ground places the result exactly origin + {minDist, 0}: the
// preferred candidate is on mesh, so it is taken directly (no ring search).
TEST_F(NavigationSystemTest, FindValidPositionNearHonorsPreferredDirOnOpenGround) {
	ConstructionWorld cw;
	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys);
	sys.setConstructionWorld(&cw);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	const glm::vec2 origin{2.0F, 2.0F};
	const float		minDist = 2.0F;
	const std::optional<glm::vec2> result = sys.findValidPositionNear(origin, minDist, {1.0F, 0.0F});
	ASSERT_TRUE(result.has_value());
	EXPECT_NEAR(result->x, origin.x + minDist, 0.001F) << "preferred +X candidate taken directly";
	EXPECT_NEAR(result->y, origin.y, 0.001F) << "preferred +X candidate has no Y offset";
}

// Origin outside every region: nullopt (no mesh to place against, like nearestPathablePoint).
TEST_F(NavigationSystemTest, FindValidPositionNearOutsideRegionReturnsNullopt) {
	ConstructionWorld cw;
	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(world, sys); // 60 m region at origin
	sys.setConstructionWorld(&cw);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh()) << "navmesh never built";

	const glm::vec2 farAway{500.0F, 500.0F};
	ASSERT_FALSE(sys.inSimArea(farAway)) << "test point must be outside every region";
	EXPECT_FALSE(sys.findValidPositionNear(farAway, 1.0F).has_value())
		<< "an origin no region covers must return nullopt";
}

// --- Colonist clustering (overlap -> merge while it fits, else separate) ------
//
// clusterColonists builds the colonist regions from positions alone: overlapping squares merge
// while the cluster fits the max region size, so a region is never clamped below its colonists.

namespace {
	constexpr std::int64_t kR = NavigationSystem::kSimRadiusMm;
}

TEST(SimAreaClusteringTest, OverlappingSquaresMergeIntoBoundingBox) {
	const std::vector<DesiredRegion> c = NavigationSystem::clusterColonists({{0, 0}, {40000, 20000}});
	ASSERT_EQ(c.size(), 1U);
	EXPECT_EQ(c[0].rect.minX, -kR);
	EXPECT_EQ(c[0].rect.maxX, 40000 + kR);
	EXPECT_EQ(c[0].rect.maxY, 20000 + kR);
	EXPECT_EQ(c[0].drivers.size(), 2U);
	EXPECT_EQ(c[0].owner, RegionOwner::Colonists);
}

TEST(SimAreaClusteringTest, DisjointSquaresStaySeparate) {
	EXPECT_EQ(NavigationSystem::clusterColonists({{0, 0}, {100000, 0}}).size(), 2U);
}

// A chain whose bounding box would pass the max region size splits; every colonist keeps at least
// kSimRadiusMm between him and his cluster's edge, which is what keeps him out of its edge margin.
TEST(SimAreaClusteringTest, ChainPastTheMaxSizeSplitsAndEveryColonistSitsDeepInside) {
	const std::vector<geometry::Vec2i64> chain{{0, 0}, {55000, 0}, {110000, 0}, {165000, 0}};
	const std::vector<DesiredRegion>	 c = NavigationSystem::clusterColonists(chain);
	EXPECT_EQ(c.size(), 2U);
	for (const DesiredRegion& region : c) {
		EXPECT_LE(region.rect.maxX - region.rect.minX, 2 * NavigationSystem::kMaxSimHalfExtentMm);
		for (const geometry::Vec2i64& p : region.drivers) {
			EXPECT_GE(std::min(p.x - region.rect.minX, region.rect.maxX - p.x), kR);
		}
	}
}

TEST(SimAreaClusteringTest, OrderIndependent) {
	const std::vector<DesiredRegion> a = NavigationSystem::clusterColonists({{0, 0}, {55000, 0}, {110000, 0}});
	const std::vector<DesiredRegion> b = NavigationSystem::clusterColonists({{110000, 0}, {0, 0}, {55000, 0}});
	ASSERT_EQ(a.size(), b.size());
	for (std::size_t i = 0; i < a.size(); ++i) {
		EXPECT_EQ(a[i].rect.minX, b[i].rect.minX);
		EXPECT_EQ(a[i].rect.maxX, b[i].rect.maxX);
	}
}

TEST(SimAreaClusteringTest, NoColonistsNoRegions) {
	EXPECT_TRUE(NavigationSystem::clusterColonists({}).empty());
}

// With nothing pending finishPendingBuilds changes nothing; after an edit it returns with the
// rebuild landed. (Two regions both landing is QueryDispatchSelectsRegionContainingPoint.)
TEST_F(NavigationSystemTest, FinishPendingBuildsIsANoOpUntilAnInputChanges) {
	ConstructionWorld cw;

	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setConstructionWorld(&cw);
	wireArea(sys);
	sys.finishPendingBuilds();
	ASSERT_TRUE(sys.hasMesh());

	const std::uint64_t settled = sys.generation();
	sys.finishPendingBuilds();
	EXPECT_FALSE(sys.isBuilding());
	EXPECT_EQ(sys.generation(), settled) << "nothing pending: no build launched, no mesh swapped";

	buildWall(cw, {5000, 5000}, {8000, 5000});
	sys.finishPendingBuilds();
	EXPECT_FALSE(sys.isBuilding());
	EXPECT_GT(sys.generation(), settled) << "the wall edit's rebuild landed before returning";
}

// --- Query dispatch by position (point -> correct region) ---------------------
//
// Two colonists far enough apart drive two disjoint regions. A query dispatches to the
// region containing the point: a same-region path solves, a cross-region pair has no route
// (nullopt -- long-range routing is Phase 2; the AI reports it Blocked, since both ends are in
// the sim area), and inSimArea/isOnMesh resolve per region.

TEST_F(NavigationSystemTest, QueryDispatchSelectsRegionContainingPoint) {
	ConstructionWorld cw; // empty: open terrain, two regions of plain walkable ground

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(m_chunks.get());
	sys.setConstructionWorld(&cw);

	// Two colonists ~100 m apart: each drives its own 30 m square (kSimRadiusMm), and the
	// two squares are disjoint, so the clustering produces two separate regions.
	const glm::vec2 aPos{0.0F, 0.0F};
	const glm::vec2 bPos{100.0F, 0.0F};
	EntityID a = world.createEntity();
	world.addComponent<Position>(a, Position{aPos});
	world.addComponent<Colonist>(a, Colonist{"A"});
	EntityID b = world.createEntity();
	world.addComponent<Position>(b, Position{bPos});
	world.addComponent<Colonist>(b, Colonist{"B"});

	// No viewport pushed: regions come solely from the two colonists.
	sys.finishPendingBuilds();
	ASSERT_EQ(sys.builtRegions().size(), 2u) << "two disjoint colonists must produce two built regions";

	// Each colonist's position is inside SOME built region; a point between them (50 m) is
	// covered by neither (the squares are 30 m half-extent, so they reach to +/-30 m).
	EXPECT_TRUE(sys.inSimArea(aPos)) << "colonist A's position is inside its region";
	EXPECT_TRUE(sys.inSimArea(bPos)) << "colonist B's position is inside its region";
	EXPECT_FALSE(sys.inSimArea(glm::vec2{50.0F, 0.0F})) << "the gap between regions is unsimulated";

	// A same-region path solves (both endpoints in colonist A's region, open ground).
	const glm::vec2 nearA{5.0F, 5.0F};
	EXPECT_TRUE(sys.requestPath(aPos, nearA, kAgentRadius).has_value())
		<< "a same-region path over open ground should solve";

	// A cross-region pair has no route: start in A's region, goal in B's region. Phase 2 owns
	// long-range routing; Phase 1 returns nullopt.
	EXPECT_FALSE(sys.requestPath(aPos, bPos, kAgentRadius).has_value())
		<< "a cross-region pair has no route (nullopt) in Phase 1";

	// A goal only in another built region passes the pre-check: the AI walks toward it (edge legs)
	// rather than routing to it, since no region holding the start covers it.
	EXPECT_TRUE(isReachable(sys, aPos, bPos, kAgentRadius))
		<< "a goal beyond the start's regions is walked toward, not rejected";
}
