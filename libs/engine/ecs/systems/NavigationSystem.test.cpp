#include "NavigationSystem.h"

#include "../World.h"
#include "../components/Memory.h"
#include "../components/NavPath.h"

#include "nav/NavCoords.h"

#include <assets/ConstructionRegistry.h>
#include <construction/ConstructionWorld.h>

#include <nav/PathQuery.h>

#include <world/Biome.h>
#include <world/BiomeWeights.h>
#include <world/camera/WorldCamera.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/ChunkSampleResult.h>
#include <world/chunk/IWorldSampler.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

using namespace ecs;
using engine::construction::ConstructionWorld;
using engine::construction::FoundationState;
using engine::construction::kInvalidFoundation;
using engine::construction::kInvalidOpening;
using engine::construction::OpeningId;
using engine::construction::SegmentCommitResult;
using engine::construction::SegmentId;
using engine::assets::ConstructionRegistry;
using geometry::Vec2i64;

namespace {

	// Project root from __FILE__: this file lives at <root>/libs/engine/ecs/systems/.
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

	// Pump update() until a mesh is built or the bound is hit. The build is async,
	// so we poll with a short yield/sleep between frames (mirrors how the chunk
	// processor's futures are polled). Bounded so a hung build fails loudly.
	bool pumpUntilMesh(NavigationSystem& sys, int maxFrames = 500) {
		for (int i = 0; i < maxFrames; ++i) {
			sys.update(0.0F);
			if (sys.hasMesh()) {
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		return false;
	}

	// Standard query endpoints: just outside the south wall, and room center.
	const glm::vec2 kOutside{2.0F, -1.0F};
	const glm::vec2 kInside{2.0F, 1.5F};
	constexpr float kAgentRadius = 0.3F; // 0.3 m radius fits the 0.9 m door

	// All-land sampler: every tile is grassland, so the area build never invents water
	// obstacles around the origin room. (The area water predicate keys off Lake/Ocean.)
	class LandSampler : public engine::world::IWorldSampler {
	  public:
		[[nodiscard]] engine::world::ChunkSampleResult sampleChunk(engine::world::ChunkCoordinate) const override {
			engine::world::ChunkSampleResult r;
			for (auto& cb : r.cornerBiomes) {
				cb = engine::world::BiomeWeights::single(engine::world::Biome::TemperateGrassland);
			}
			r.cornerElevations = {1.0F, 1.0F, 1.0F, 1.0F};
			r.computeSectorGrid();
			return r;
		}
		[[nodiscard]] float	   sampleElevation(engine::world::WorldPosition) const override { return 1.0F; }
		[[nodiscard]] uint64_t getWorldSeed() const override { return 7u; }
	};

	class NavigationSystemTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			ConstructionRegistry::Get().clear();
			ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder()));

			// A ready chunk region (all land) and a camera at the origin, so the
			// NavigationSystem build runs through the SIMULATION-AREA path. The origin
			// room (0..4 m x 0..3 m) sits well inside the 120 m area.
			m_chunks = std::make_unique<engine::world::ChunkManager>(std::make_unique<LandSampler>());
			m_chunks->setLoadRadius(2);
			m_chunks->setUnloadRadius(4);
			m_chunks->update({0.0F, 0.0F});
			m_chunks->finishPendingGeneration();
			m_camera = std::make_unique<engine::world::WorldCamera>();
			m_camera->setPosition({0.0F, 0.0F});
		}
		void TearDown() override { ConstructionRegistry::Get().clear(); }

		// Wire the area build (chunks + camera) onto a system in addition to its
		// ConstructionWorld, so the test exercises the real simulation-area path.
		void wireArea(NavigationSystem& sys) {
			sys.setChunkManager(m_chunks.get());
			sys.setCamera(m_camera.get());
		}

		std::unique_ptr<engine::world::ChunkManager> m_chunks;
		std::unique_ptr<engine::world::WorldCamera>	 m_camera;
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
	EXPECT_TRUE(sys.isReachable(kOutside, kInside, kAgentRadius));
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
	wireArea(sys);
	sys.setConstructionWorld(&cw);

	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";

	std::optional<std::vector<glm::vec2>> path = sys.requestPath(kOutside, kInside, kAgentRadius);
	ASSERT_TRUE(path.has_value());
	EXPECT_GE(path->size(), 2u); // at least start..goal
	EXPECT_TRUE(sys.isReachable(kOutside, kInside, kAgentRadius));
}

// A window (non-pathable opening) leaves the wall solid: no path.
TEST_F(NavigationSystemTest, WindowBlocksPath) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/false);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);

	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
}

// No opening at all also blocks the path.
TEST_F(NavigationSystemTest, NoOpeningBlocksPath) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);

	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
}

// Rebuild-on-version-change: a closed room blocks the path; cutting a door bumps
// version(), and after the async rebuild lands the same query succeeds.
TEST_F(NavigationSystemTest, RebuildOnVersionChangeOpensPath) {
	ConstructionWorld cw;
	SegmentId south = buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);

	ASSERT_TRUE(pumpUntilMesh(sys)) << "initial navmesh never built";
	ASSERT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value())
		<< "closed room should block the path";

	// Cut a door in the south wall. This bumps ConstructionWorld::version(), so the
	// next update() detects the change and launches a rebuild.
	const std::uint64_t before = cw.version();
	OpeningId op = cw.addOpening(south, 0.5F, "Door", "Wood");
	ASSERT_NE(op, kInvalidOpening);
	ASSERT_TRUE(cw.setOpeningState(op, FoundationState::Built));
	ASSERT_NE(cw.version(), before);

	// Pump until the rebuilt mesh makes the path reachable (bounded).
	bool opened = false;
	for (int i = 0; i < 500 && !opened; ++i) {
		sys.update(0.0F);
		opened = sys.requestPath(kOutside, kInside, kAgentRadius).has_value();
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	EXPECT_TRUE(opened) << "door rebuild never opened the path";
}

// Rebuild the other direction: an open (door) room is pathable; setting the door
// opening back to Blueprint makes extractWalls treat the band as solid again, which
// bumps version() and should re-seal the room after the async rebuild lands.
TEST_F(NavigationSystemTest, RebuildSealsRoomWhenDoorRemoved) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/true, /*pathableOpening=*/true);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);

	ASSERT_TRUE(pumpUntilMesh(sys)) << "initial navmesh never built";
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

	bool sealed = false;
	for (int i = 0; i < 500 && !sealed; ++i) {
		sys.update(0.0F);
		sealed = !sys.requestPath(kOutside, kInside, kAgentRadius).has_value();
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	EXPECT_TRUE(sealed) << "blueprinting the door never re-sealed the room";
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
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built after deferred wiring";
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
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";

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
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";

	EXPECT_TRUE(sys.isReachable(kOutside, kInside, kAgentRadius));
	EXPECT_TRUE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
}

// Walled-off goal: isReachable returns false, requestPath returns nullopt.
TEST_F(NavigationSystemTest, IsReachableFalseForWalledOffGoal) {
	ConstructionWorld cw;
	buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";

	// reachable() should detect the disconnected component and return false.
	EXPECT_FALSE(sys.isReachable(kOutside, kInside, kAgentRadius));
	// requestPath must also return nullopt (short-circuit already there via pathThrough).
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
}

// No-mesh: isReachable returns true regardless of endpoints (can't prove unreachable).
TEST_F(NavigationSystemTest, IsReachableTrueWhenNoMesh) {
	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();

	ASSERT_FALSE(sys.hasMesh());
	EXPECT_TRUE(sys.isReachable(kOutside, kInside, kAgentRadius));
	// requestPath is still nullopt with no mesh.
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
}

// generation() bumps each time a freshly built mesh is swapped in.
TEST_F(NavigationSystemTest, GenerationBumpsOnMeshSwap) {
	ConstructionWorld cw;
	SegmentId south = buildRoom(cw, /*withOpening=*/false, /*pathableOpening=*/false);

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);

	EXPECT_EQ(sys.generation(), 0u) << "no mesh yet => generation 0";
	ASSERT_TRUE(pumpUntilMesh(sys)) << "initial navmesh never built";
	const std::uint64_t afterFirst = sys.generation();
	EXPECT_GT(afterFirst, 0u) << "first mesh swap should bump generation";

	// A geometry change forces a rebuild; the swap-in bumps generation again.
	OpeningId op = cw.addOpening(south, 0.5F, "Door", "Wood");
	ASSERT_NE(op, kInvalidOpening);
	ASSERT_TRUE(cw.setOpeningState(op, FoundationState::Built));
	for (int i = 0; i < 500 && sys.generation() == afterFirst; ++i) {
		sys.update(0.0F);
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	EXPECT_GT(sys.generation(), afterFirst) << "rebuild swap should bump generation again";
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
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";

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
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	ASSERT_TRUE(pumpUntilMesh(sys)) << "initial navmesh never built";

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

	bool opened = false;
	for (int i = 0; i < 500 && !opened; ++i) {
		sys.update(0.0F);
		opened = sys.generation() != genBefore && sys.requestPath(kOutside, kInside, kAgentRadius).has_value();
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	ASSERT_TRUE(opened) << "door rebuild never opened the path";
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
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";

	// Collect the distinct goal triangles a fine grid of goal points maps to; this is the
	// number of cache entries an UNBOUNDED map would create.
	const glm::vec2					   start{1.0F, 1.0F};
	std::unordered_set<std::int32_t>   distinctGoalTris;
	for (int gx = 0; gx < 13; ++gx) {
		for (int gy = 0; gy < 13; ++gy) {
			const glm::vec2 goal{0.7F + static_cast<float>(gx), 0.7F + static_cast<float>(gy)};
			const std::int32_t t = geometry::nav::locateTriangle(sys.mesh(), engine::nav::toMm(goal));
			if (t >= 0) {
				distinctGoalTris.insert(t);
			}
			sys.requestPath(start, goal, kAgentRadius);
		}
	}

	// The bound must hold regardless.
	EXPECT_LE(sys.rraCacheCount(), 64u) << "the cache map must stay within its cap under goal churn";
	// And the scenario must actually push past the cap (otherwise the bound is untested).
	EXPECT_GT(distinctGoalTris.size(), 64u)
		<< "test setup must produce > 64 distinct goal triangles to exercise the cap";
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
