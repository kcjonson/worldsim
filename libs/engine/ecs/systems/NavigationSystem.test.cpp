#include "NavigationSystem.h"

#include "AIDecisionSystem.h"
#include "../World.h"
#include "../components/Colonist.h"
#include "../components/DecisionTrace.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/Movement.h"
#include "../components/NavPath.h"
#include "../components/Needs.h"
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

#include <world/Biome.h>
#include <world/BiomeWeights.h>
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
using engine::assets::AssetDefinition;
using engine::assets::AssetRegistry;
using engine::assets::CollisionShapeType;
using engine::assets::ConstructionRegistry;
using engine::assets::PlacedEntity;
using engine::assets::PlacementExecutor;
using engine::assets::SpatialIndex;
using engine::world::ChunkCoordinate;
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

	// First built region's mesh. The viewport-tracking tests drive a single region, so the
	// first built region IS that region's mesh. Returns a shared empty mesh when none built.
	const geometry::nav::NavMesh& firstMesh(const NavigationSystem& sys) {
		static const geometry::nav::NavMesh kEmpty;
		const std::vector<NavigationSystem::RegionView> built = sys.builtRegions();
		return built.empty() ? kEmpty : *built.front().mesh;
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

	// Mostly-land sampler with a water patch carved into the origin chunk: sectors whose
	// local tile range covers ~[32 m, 48 m) on both axes read Ocean, the rest grassland.
	// Lets a test assert isValidPosition is false on a real water face inside the region
	// while a nearby land face is valid. (Sector = localTile / 16; chunk is 512 tiles.)
	class WaterPatchSampler : public engine::world::IWorldSampler {
	  public:
		[[nodiscard]] engine::world::ChunkSampleResult sampleChunk(engine::world::ChunkCoordinate coord) const override {
			engine::world::ChunkSampleResult r;
			for (auto& cb : r.cornerBiomes) {
				cb = engine::world::BiomeWeights::single(engine::world::Biome::TemperateGrassland);
			}
			r.cornerElevations = {1.0F, 1.0F, 1.0F, 1.0F};
			r.computeSectorGrid();
			// Only carve the patch in the origin chunk so points near (0,0) stay land.
			if (coord.x == 0 && coord.y == 0) {
				const engine::world::BiomeWeights ocean =
					engine::world::BiomeWeights::single(engine::world::Biome::Ocean);
				// Tiles [32,48) -> sectors [2,3); a 16 m square of open water at ~(40,40).
				for (int sy = 2; sy < 3; ++sy) {
					for (int sx = 2; sx < 3; ++sx) {
						r.sectorGrid[static_cast<size_t>(sy * engine::world::kSectorGridSize + sx)] = ocean;
					}
				}
			}
			return r;
		}
		[[nodiscard]] float	   sampleElevation(engine::world::WorldPosition) const override { return 1.0F; }
		[[nodiscard]] uint64_t getWorldSeed() const override { return 7u; }
	};

	// Inverse of WaterPatchSampler: open water EVERYWHERE except one 16 m land sector
	// (the "island") carved into the origin chunk at world [32,48) on both axes. The only
	// walkable face in the whole region is that island, which lets a test drop an origin in
	// the surrounding water close to the island and force findValidPositionNear's Tier-3
	// PUSH branch: nearest walkable (the island, ~4 m off) is inside minDist, yet a 24 m ring
	// clears the island entirely (its farthest corner is ~21.5 m from the origin) so no ring
	// sample is on mesh, and the radial push to minDist lands past the island back in water.
	class LandIslandSampler : public engine::world::IWorldSampler {
	  public:
		[[nodiscard]] engine::world::ChunkSampleResult sampleChunk(engine::world::ChunkCoordinate coord) const override {
			engine::world::ChunkSampleResult r;
			// Default the whole chunk to water by seeding every corner Ocean.
			for (auto& cb : r.cornerBiomes) {
				cb = engine::world::BiomeWeights::single(engine::world::Biome::Ocean);
			}
			r.cornerElevations = {1.0F, 1.0F, 1.0F, 1.0F};
			r.computeSectorGrid();
			// Carve the single land island only in the origin chunk so the rest stays water.
			if (coord.x == 0 && coord.y == 0) {
				const engine::world::BiomeWeights land =
					engine::world::BiomeWeights::single(engine::world::Biome::TemperateGrassland);
				// Tiles [32,48) -> sector (2,2): one 16 m square of walkable land at ~(40,40).
				r.sectorGrid[static_cast<size_t>(2 * engine::world::kSectorGridSize + 2)] = land;
			}
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

			// A ready chunk region (all land) and a 60 m simulation area at the origin, so
			// the NavigationSystem build runs through the SIMULATION-AREA path. The origin
			// room (0..4 m x 0..3 m) sits well inside the area.
			m_chunks = std::make_unique<engine::world::ChunkManager>(std::make_unique<LandSampler>());
			m_chunks->setLoadRadius(2);
			m_chunks->setUnloadRadius(4);
			m_chunks->update({0.0F, 0.0F});
			m_chunks->finishPendingGeneration();
		}
		void TearDown() override { ConstructionRegistry::Get().clear(); }

		// Wire the area build (chunks + a 60 m viewport region at origin) onto a system in
		// addition to its ConstructionWorld, so the test exercises the simulation-region
		// path. The viewport drives a single region centered at origin; the 3x5 m origin
		// room sits well inside it.
		void wireArea(NavigationSystem& sys) {
			sys.setChunkManager(m_chunks.get());
			pushArea(sys, {0, 0}, 60000); // 60 m -- covers the origin room comfortably
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

		std::unique_ptr<engine::world::ChunkManager> m_chunks;
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
			const std::int32_t t = geometry::nav::locateTriangle(firstMesh(sys), engine::nav::toMm(goal));
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
	ASSERT_TRUE(pumpUntilMesh(sys)) << "initial mesh never built";
	const std::uint64_t gen1 = sys.generation();
	const Aabb aabb1 = meshAabb(firstMesh(sys));

	// Zoom out: 100 m (> 20% bigger than 30 m after clamping stays 100 m).
	pushArea(sys, {0, 0}, 100000);
	bool grew = false;
	for (int i = 0; i < 500 && !grew; ++i) {
		sys.update(0.0F);
		if (sys.generation() != gen1) {
			const Aabb aabb2 = meshAabb(firstMesh(sys));
			grew = (aabb2.maxX - aabb2.minX) > (aabb1.maxX - aabb1.minX)
				|| (aabb2.maxY - aabb2.minY) > (aabb1.maxY - aabb1.minY);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	EXPECT_TRUE(grew) << "zoom-out rebuild should produce a larger mesh AABB";
	EXPECT_GT(sys.generation(), gen1) << "generation must bump on zoom-out rebuild";
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
	ASSERT_TRUE(pumpUntilMesh(sys)) << "initial mesh never built";
	const std::uint64_t gen1 = sys.generation();
	const Aabb aabb1 = meshAabb(firstMesh(sys));

	// Zoom in: 50 m (50% of 100 m -> delta 50% > 20% threshold).
	pushArea(sys, {0, 0}, 50000);
	bool shrank = false;
	for (int i = 0; i < 500 && !shrank; ++i) {
		sys.update(0.0F);
		if (sys.generation() != gen1) {
			const Aabb aabb2 = meshAabb(firstMesh(sys));
			shrank = (aabb2.maxX - aabb2.minX) < (aabb1.maxX - aabb1.minX)
				  || (aabb2.maxY - aabb2.minY) < (aabb1.maxY - aabb1.minY);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	EXPECT_TRUE(shrank) << "zoom-in rebuild should produce a smaller mesh AABB";
	EXPECT_GT(sys.generation(), gen1) << "generation must bump on zoom-in rebuild";
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
	ASSERT_TRUE(pumpUntilMesh(sys)) << "initial mesh never built";
	const std::uint64_t gen1 = sys.generation();

	// Pan the viewport center to 55 m: that is within kEdgeMarginMm (8 m) of the +60 m
	// edge, so the driver crosses the margin and the region must recenter/rebuild.
	pushArea(sys, {55000, 0}, 60000);
	bool recentered = false;
	for (int i = 0; i < 500 && !recentered; ++i) {
		sys.update(0.0F);
		recentered = sys.generation() != gen1;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	EXPECT_TRUE(recentered) << "pan past the edge margin must trigger a rebuild";
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
	ASSERT_TRUE(pumpUntilMesh(sys)) << "initial mesh never built";
	const std::uint64_t gen1 = sys.generation();

	// Pan to 5 m (driver stays ~55 m from the +60 m edge, well past the 8 m margin) and a
	// 5% size change (< 20% threshold).
	pushArea(sys, {5000, 0}, 63000); // 63/60 = 1.05 -> 5% delta, driver at 5 m
	for (int i = 0; i < 50; ++i) {
		sys.update(0.0F);
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	EXPECT_EQ(sys.generation(), gen1) << "a pan within the margin must NOT trigger a rebuild";
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
	ASSERT_TRUE(pumpUntilMesh(sys)) << "clamped-to-min mesh never built";
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
	ASSERT_TRUE(pumpUntilMesh(sys)) << "clamped-to-max mesh never built";
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
	wireArea(sys); // ChunkManager + 60 m area at origin (clamped to 64 m)
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);

	ASSERT_TRUE(pumpUntilMesh(sys)) << "initial navmesh never built";
	const std::uint64_t gen1	  = sys.generation();
	const std::size_t	triBefore = firstMesh(sys).triangles.size();

	// Now a spawn-ring chunk finishes placement: a blocking tree well inside the area
	// (chunk (0,0), tile (10,10)) lands in the index and the chunk joins the processed
	// set. The camera has NOT moved and the construction version is unchanged, so the
	// signature flip is the ONLY thing that can trigger the rebuild.
	const glm::vec2		  treeAt{10.0F, 10.0F};
	const ChunkCoordinate coord = engine::world::worldToChunk({treeAt.x, treeAt.y});
	SpatialIndex		  index;
	PlacedEntity		  pe;
	pe.defName	= "Test_NavLatePlaceTree";
	pe.position = treeAt;
	index.insert(pe);
	engine::assets::AsyncChunkPlacementResult result;
	result.coord		= coord;
	result.spatialIndex = std::move(index);
	executor.storeChunkResult(std::move(result));
	processed.insert(coord);

	// Pump until the signature-driven rebuild lands.
	bool rebuilt = false;
	for (int i = 0; i < 500 && !rebuilt; ++i) {
		sys.update(0.0F);
		rebuilt = sys.generation() != gen1;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	ASSERT_TRUE(rebuilt) << "in-area placement completion must trigger a rebuild";
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
	const ChunkCoordinate				coord = engine::world::worldToChunk({treeAt.x, treeAt.y});
	{
		SpatialIndex index;
		PlacedEntity pe;
		pe.defName	= "Test_NavBuildableTree";
		pe.position = treeAt;
		index.insert(pe);
		engine::assets::AsyncChunkPlacementResult result;
		result.coord		= coord;
		result.spatialIndex = std::move(index);
		executor.storeChunkResult(std::move(result));
		processed.insert(coord);
	}

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);

	ASSERT_TRUE(pumpUntilMesh(sys)) << "initial navmesh never built";

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
	const ChunkCoordinate				coord = engine::world::worldToChunk({treeAt.x, treeAt.y});
	{
		SpatialIndex index;
		PlacedEntity pe;
		pe.defName	= "Test_NavFellTree";
		pe.position = treeAt;
		index.insert(pe);
		engine::assets::AsyncChunkPlacementResult result;
		result.coord		= coord;
		result.spatialIndex = std::move(index);
		executor.storeChunkResult(std::move(result));
		processed.insert(coord);
	}

	ConstructionWorld cw;
	World			  world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setPlacementData(&executor, &processed);
	sys.setConstructionWorld(&cw);

	ASSERT_TRUE(pumpUntilMesh(sys)) << "initial navmesh never built";
	const std::uint64_t gen1		 = sys.generation();
	const std::size_t	triWithTree	 = firstMesh(sys).triangles.size();

	// Fell the tree: remove it from the index. Chunk stays processed, construction version is
	// unchanged -- the removal epoch is the sole trigger for the rebuild.
	ASSERT_TRUE(executor.removeEntity(coord, treeAt, "Test_NavFellTree")) << "tree must be removed from the index";

	bool rebuilt = false;
	for (int i = 0; i < 500 && !rebuilt; ++i) {
		sys.update(0.0F);
		rebuilt = sys.generation() != gen1;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	ASSERT_TRUE(rebuilt) << "an in-area entity removal must trigger a rebuild (reclaim the trunk hole)";
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
	// Push an area but don't pump -- no mesh built yet.
	pushArea(sys, {0, 0}, 60000);

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

	// 60 m area at origin: covers roughly (-60 m, -60 m) to (+60 m, +60 m).
	pushArea(sys, {0, 0}, 60000);
	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";

	// A point well inside the area.
	EXPECT_TRUE(sys.inSimArea({0.0F, 0.0F}));
	EXPECT_TRUE(sys.inSimArea({30.0F, 30.0F}));  // exactly on the edge is <=, so inside
	// A point clearly outside the area (100 m from center, area half-extent is 60 m
	// but the system clamps that to kMaxSimHalfExtentMm = 64 m, so 100 m is outside).
	EXPECT_FALSE(sys.inSimArea({100.0F, 0.0F}));
	EXPECT_FALSE(sys.inSimArea({0.0F, 100.0F}));
}

// --- isValidPosition: the canonical spawn/placement validity predicate ----------
//
// isValidPosition delegates to isOnMesh: a position is valid IFF it sits on a walkable
// nav face inside an active region. A walkable face is valid; a water face inside the
// region is invalid; a point no region covers is invalid (can't place off-mesh yet).

TEST_F(NavigationSystemTest, IsValidPositionTrueOnWalkableFaceFalseOnWaterAndOffMesh) {
	// Mostly-land world with a 16 m water patch at ~(40,40) inside the origin chunk.
	auto chunks = std::make_unique<engine::world::ChunkManager>(std::make_unique<WaterPatchSampler>());
	chunks->setLoadRadius(2);
	chunks->setUnloadRadius(4);
	chunks->update({0.0F, 0.0F});
	chunks->finishPendingGeneration();

	ConstructionWorld cw; // empty: open terrain plus the water patch obstacle

	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	sys.setChunkManager(chunks.get());
	pushArea(sys, {0, 0}, 60000); // 60 m region (clamped to 64) covers origin and the patch
	sys.setConstructionWorld(&cw);
	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";

	// A clearly-land point near origin sits on a walkable face: VALID.
	const glm::vec2 land{2.0F, 2.0F};
	ASSERT_TRUE(sys.inSimArea(land)) << "land point must be inside the region";
	EXPECT_TRUE(sys.isValidPosition(land)) << "a walkable land face must be a valid position";

	// A point inside the water patch is off-mesh (water face): INVALID, but still in-region.
	const glm::vec2 water{40.0F, 40.0F};
	ASSERT_TRUE(sys.inSimArea(water)) << "the water point must be inside the region";
	EXPECT_FALSE(sys.isOnMesh(water)) << "the water tile must not be a walkable face";
	EXPECT_FALSE(sys.isValidPosition(water)) << "a water face must be an invalid position";

	// A point no region covers is invalid: you cannot place outside an active mesh yet.
	const glm::vec2 offMesh{500.0F, 500.0F};
	ASSERT_FALSE(sys.inSimArea(offMesh)) << "the far point must be outside every region";
	EXPECT_FALSE(sys.isValidPosition(offMesh)) << "a point off every region must be invalid";
}

// --- Whole-footprint walkability: isAreaWalkable / isSegmentWalkable -------------
//
// Vertex-only validity is insufficient: a footprint can have every corner on land yet
// span water, or fully enclose a water pond. These predicates sample edges + interior
// (isAreaWalkable) or the centerline (isSegmentWalkable) so any off-mesh part rejects.
// All use the WaterPatchSampler's 16 m water square at world ~[32,48) on both axes.

namespace {
	// Set up a system over the water-patch world + a 60 m origin region with a built mesh.
	// Returns the ChunkManager the caller must keep alive for the system's lifetime.
	std::unique_ptr<engine::world::ChunkManager> wireWaterPatch(NavigationSystem& sys, ConstructionWorld& cw) {
		auto chunks = std::make_unique<engine::world::ChunkManager>(std::make_unique<WaterPatchSampler>());
		chunks->setLoadRadius(2);
		chunks->setUnloadRadius(4);
		chunks->update({0.0F, 0.0F});
		chunks->finishPendingGeneration();
		sys.setChunkManager(chunks.get());
		// 60 m region at origin (clamped to 64), exactly as the existing water-patch test
		// wires it: covers the origin land AND the [32,48] water square. (Viewport center/
		// half in meters; mirrors NavigationSystemTest::pushArea, inlined for a free helper.)
		sys.setViewportRect(glm::vec2{0.0F, 0.0F}, glm::vec2{60.0F, 60.0F});
		sys.setConstructionWorld(&cw);
		return chunks;
	}
} // namespace

TEST_F(NavigationSystemTest, IsAreaWalkableTrueWhenWhollyOnLand) {
	ConstructionWorld cw;
	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	auto chunks = wireWaterPatch(sys, cw);
	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";

	// A 4 m square wholly on land (well clear of the [32,48] water patch).
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
	auto chunks = wireWaterPatch(sys, cw);
	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";

	// Corners straddle the water square horizontally: x=28 and x=52 are LAND (outside
	// [32,48]), but the footprint's middle (and its top/bottom edges) crosses the water.
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
	auto chunks = wireWaterPatch(sys, cw);
	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";

	// A big square whose corners AND edges are all on land but that fully encloses the
	// [32,48] water square. Only the INTERIOR grid sampling can catch this -- the edge
	// pass alone would pass. Corners at 28 and 52 are land; edges run along y=28/y=52 and
	// x=28/x=52, all outside the water square.
	const std::vector<glm::vec2> enclosing{{28.0F, 28.0F}, {52.0F, 28.0F}, {52.0F, 52.0F}, {28.0F, 52.0F}};
	for (const auto& v : enclosing) {
		ASSERT_TRUE(sys.isValidPosition(v)) << "each corner must be on land (premise)";
	}
	ASSERT_FALSE(sys.isValidPosition({40.0F, 40.0F})) << "the enclosed center must be water (premise)";
	EXPECT_FALSE(sys.isAreaWalkable(enclosing)) << "a footprint enclosing a water pond must be rejected";
}

TEST_F(NavigationSystemTest, IsSegmentAndPolylineWalkableCatchWaterCrossing) {
	ConstructionWorld cw;
	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	auto chunks = wireWaterPatch(sys, cw);
	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";

	// A wall segment fully on land is walkable.
	EXPECT_TRUE(sys.isSegmentWalkable({4.0F, 4.0F}, {8.0F, 4.0F})) << "an all-land segment must be walkable";

	// A segment whose endpoints are land (x=28, x=52) but that crosses the water square
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
	wireArea(sys); // ChunkManager (all land) + 60 m region at origin
	sys.setConstructionWorld(&cw);
	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";

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
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";

	// Confirm in-area walled-off goal IS false (baseline: the closed room blocks it).
	EXPECT_FALSE(sys.isReachable(kOutside, kInside, kAgentRadius))
		<< "in-area closed-room goal should be false (baseline for this test)";

	// Now try a goal far outside the built area: must return true ("can't prove unreachable").
	const glm::vec2 offAreaGoal{200.0F, 200.0F}; // well outside 60 m area
	EXPECT_TRUE(sys.isReachable(kOutside, offAreaGoal, kAgentRadius))
		<< "off-area goal must return true (can't prove unreachable)";

	// Also true when START is off-area.
	const glm::vec2 offAreaStart{200.0F, 0.0F};
	EXPECT_TRUE(sys.isReachable(offAreaStart, kInside, kAgentRadius))
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
// its own region -- the viewport region under test is what covers it).
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
	const ChunkCoordinate				coord = engine::world::worldToChunk({treeAt.x, treeAt.y});
	SpatialIndex						index;
	PlacedEntity						pe;
	pe.defName	= "Test_NavRecoveryTree";
	pe.position = treeAt;
	index.insert(pe);
	engine::assets::AsyncChunkPlacementResult placed;
	placed.coord		= coord;
	placed.spatialIndex = std::move(index);
	executor.storeChunkResult(std::move(placed));
	processed.insert(coord);

	ConstructionWorld cw; // empty: open terrain + the one tree obstacle

	World world;
	NavigationSystem& nav = world.registerSystem<NavigationSystem>();
	wireArea(nav);
	nav.setPlacementData(&executor, &processed);
	nav.setConstructionWorld(&cw);
	ASSERT_TRUE(pumpUntilMesh(nav)) << "navmesh never built";

	AIDecisionSystem& ai = world.registerSystem<AIDecisionSystem>(
		engine::assets::AssetRegistry::Get(), engine::assets::RecipeRegistry::Get(), 1234u);
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
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";

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
	wireArea(nav); // viewport region at origin, 60 m
	nav.setConstructionWorld(&cw);
	ASSERT_TRUE(pumpUntilMesh(nav)) << "navmesh never built";

	AIDecisionSystem& ai = world.registerSystem<AIDecisionSystem>(
		engine::assets::AssetRegistry::Get(), engine::assets::RecipeRegistry::Get(), 1234u);
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
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";

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
	auto chunks = wireWaterPatch(sys, cw); // 16 m water square at world [32,48)
	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";

	// Center of the water square: in-region but off-mesh, and a 1 m ring around it is wholly
	// inside the ~16 m water hole, so no ring sample is walkable -> Tier-3 fallback.
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

namespace {
	// Wire the LandIslandSampler world (water everywhere except one 16 m land island at world
	// [32,48)) onto a system over a 60 m origin region with a built mesh. Returns the
	// ChunkManager the caller must keep alive for the system's lifetime.
	std::unique_ptr<engine::world::ChunkManager> wireLandIsland(NavigationSystem& sys, ConstructionWorld& cw) {
		auto chunks = std::make_unique<engine::world::ChunkManager>(std::make_unique<LandIslandSampler>());
		chunks->setLoadRadius(2);
		chunks->setUnloadRadius(4);
		chunks->update({0.0F, 0.0F});
		chunks->finishPendingGeneration();
		sys.setChunkManager(chunks.get());
		sys.setViewportRect(glm::vec2{0.0F, 0.0F}, glm::vec2{60.0F, 60.0F});
		sys.setConstructionWorld(&cw);
		return chunks;
	}
} // namespace

// Tier-3 PUSH branch (distinct from the early-return the pocket test above hits): the origin
// sits in open water 4 m off a lone 16 m land island, the ONLY walkable face in the region.
// nearestPathableOnMesh(origin) lands on the island ~4 m away (< minDist), so the fallback
// pushes that point radially out to minDist=24 m -- which overshoots the island back into
// water. Pre-fix that off-mesh pushed point was returned verbatim (a target a colonist could
// never path to, stalling the place goal); the isOnMesh gate now falls back to the island
// point. We assert the result is on mesh, which fails without the gate and passes with it.
TEST_F(NavigationSystemTest, FindValidPositionNearPushedFallbackStaysOnMesh) {
	ConstructionWorld cw;
	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	auto chunks = wireLandIsland(sys, cw); // 16 m land island at world [32,48), water elsewhere
	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";

	// 4 m west of the island's west face (x=32); the whole island lies within ~21.5 m, so a
	// 24 m ring clears it and every ring sample is off-mesh -> the Tier-3 push fires.
	const glm::vec2 origin{28.0F, 40.0F};
	ASSERT_TRUE(sys.inSimArea(origin)) << "origin must be inside the region";
	ASSERT_FALSE(sys.isOnMesh(origin)) << "origin must be off-mesh (in the surrounding water)";
	const std::optional<glm::vec2> nearest = sys.findValidPositionNear(origin, 0.0F);
	ASSERT_TRUE(nearest.has_value()) << "the island gives a nearest walkable point";
	ASSERT_LT(glm::distance(origin, *nearest), 24.0F) << "nearest island point must be inside minDist (push premise)";

	const float					   minDist = 24.0F;
	const std::optional<glm::vec2> result = sys.findValidPositionNear(origin, minDist);
	ASSERT_TRUE(result.has_value()) << "the region has a walkable island, so a result must exist";
	EXPECT_TRUE(sys.isOnMesh(*result))
		<< "the pushed Tier-3 fallback must be re-validated on mesh, not returned off-mesh in water";
}

// Determinism: two identical calls return the exact same point (no RNG, fixed angle set).
TEST_F(NavigationSystemTest, FindValidPositionNearIsDeterministic) {
	ConstructionWorld cw;
	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";

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
	wireArea(sys);
	sys.setConstructionWorld(&cw);
	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";

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
	wireArea(sys); // 60 m region at origin
	sys.setConstructionWorld(&cw);
	ASSERT_TRUE(pumpUntilMesh(sys)) << "navmesh never built";

	const glm::vec2 farAway{500.0F, 500.0F};
	ASSERT_FALSE(sys.inSimArea(farAway)) << "test point must be outside every region";
	EXPECT_FALSE(sys.findValidPositionNear(farAway, 1.0F).has_value())
		<< "an origin no region covers must return nullopt";
}

// --- AABB clustering (overlap -> merge, disjoint -> separate) -----------------
//
// clusterAabbs is the pure clustering primitive the sim-area computation builds on:
// any two squares that overlap (directly or transitively) collapse into their shared
// bounding box; disjoint squares stay separate. These exercise it without an ECS world.

TEST(SimAreaClusteringTest, OverlappingBoxesMergeIntoBoundingBox) {
	// Two squares that overlap in x and y -> one cluster spanning both.
	const SimAabb a{0, 0, 100, 100};
	const SimAabb b{50, 50, 150, 150};
	const std::vector<SimAabb> merged = clusterAabbs({a, b});
	ASSERT_EQ(merged.size(), 1u) << "overlapping squares must merge into one region";
	EXPECT_EQ(merged[0].minX, 0);
	EXPECT_EQ(merged[0].minY, 0);
	EXPECT_EQ(merged[0].maxX, 150);
	EXPECT_EQ(merged[0].maxY, 150) << "the merged rect is the bounding box of both inputs";
}

TEST(SimAreaClusteringTest, DisjointBoxesStaySeparate) {
	// Two squares with a clear gap on both axes -> two separate clusters.
	const SimAabb a{0, 0, 100, 100};
	const SimAabb b{1000, 1000, 1100, 1100};
	const std::vector<SimAabb> clusters = clusterAabbs({a, b});
	EXPECT_EQ(clusters.size(), 2u) << "disjoint squares must stay separate regions";
}

TEST(SimAreaClusteringTest, TransitiveChainMergesAllThree) {
	// A overlaps B, B overlaps C, A does NOT overlap C: all three still collapse to one,
	// because the union-find sweep absorbs C through B.
	const SimAabb a{0, 0, 100, 100};
	const SimAabb b{80, 0, 180, 100};
	const SimAabb c{160, 0, 260, 100};
	const std::vector<SimAabb> merged = clusterAabbs({a, b, c});
	ASSERT_EQ(merged.size(), 1u) << "a transitive overlap chain must merge to one region";
	EXPECT_EQ(merged[0].minX, 0);
	EXPECT_EQ(merged[0].maxX, 260);
}

TEST(SimAreaClusteringTest, OrderIndependent) {
	// The same inputs in a different order produce the same clustering.
	const SimAabb a{0, 0, 100, 100};
	const SimAabb b{80, 0, 180, 100};
	const SimAabb c{1000, 1000, 1100, 1100};
	const std::vector<SimAabb> m1 = clusterAabbs({a, b, c});
	const std::vector<SimAabb> m2 = clusterAabbs({c, b, a});
	EXPECT_EQ(m1.size(), m2.size());
	EXPECT_EQ(m1.size(), 2u) << "{a,b} merge, c separate -> two regions regardless of order";
}

TEST(SimAreaClusteringTest, EmptyInputYieldsNoRegions) {
	EXPECT_TRUE(clusterAabbs({}).empty());
}

// --- Query dispatch by position (point -> correct region) ---------------------
//
// Two colonists far enough apart drive two disjoint regions. A query dispatches to the
// region containing the point: a same-region path solves, a cross-region pair holds
// (nullopt -- long-range routing is Phase 2), and inSimArea/isOnMesh resolve per region.

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

	// No viewport pushed: regions come solely from the two colonists. Pump until both
	// regions have built their meshes.
	bool twoRegions = false;
	for (int i = 0; i < 500 && !twoRegions; ++i) {
		sys.update(0.0F);
		twoRegions = sys.builtRegions().size() == 2u;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	ASSERT_TRUE(twoRegions) << "two disjoint colonists must produce two built regions";

	// Each colonist's position is inside SOME built region; a point between them (50 m) is
	// covered by neither (the squares are 30 m half-extent, so they reach to +/-30 m).
	EXPECT_TRUE(sys.inSimArea(aPos)) << "colonist A's position is inside its region";
	EXPECT_TRUE(sys.inSimArea(bPos)) << "colonist B's position is inside its region";
	EXPECT_FALSE(sys.inSimArea(glm::vec2{50.0F, 0.0F})) << "the gap between regions is unsimulated";

	// A same-region path solves (both endpoints in colonist A's region, open ground).
	const glm::vec2 nearA{5.0F, 5.0F};
	EXPECT_TRUE(sys.requestPath(aPos, nearA, kAgentRadius).has_value())
		<< "a same-region path over open ground should solve";

	// A cross-region pair holds: start in A's region, goal in B's region. Phase 2 owns
	// long-range routing; Phase 1 returns nullopt so the task waits.
	EXPECT_FALSE(sys.requestPath(aPos, bPos, kAgentRadius).has_value())
		<< "a cross-region path must hold (nullopt) in Phase 1";

	// isReachable mirrors the hold: cross-region returns true ("can't prove unreachable").
	EXPECT_TRUE(sys.isReachable(aPos, bPos, kAgentRadius))
		<< "cross-region isReachable must not falsely reject";
}
