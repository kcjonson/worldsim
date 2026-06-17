#include "NavigationSystem.h"

#include "../World.h"

#include <assets/ConstructionRegistry.h>
#include <construction/ConstructionWorld.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
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

	class NavigationSystemTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			ConstructionRegistry::Get().clear();
			ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder()));
		}
		void TearDown() override { ConstructionRegistry::Get().clear(); }
	};

} // namespace

// A query before any mesh is built returns nullopt (no inputs wired / first frame).
TEST_F(NavigationSystemTest, NoMeshBeforeBuildReturnsNullopt) {
	World world;
	NavigationSystem& sys = world.registerSystem<NavigationSystem>();

	EXPECT_FALSE(sys.hasMesh());
	EXPECT_FALSE(sys.requestPath(kOutside, kInside, kAgentRadius).has_value());
	EXPECT_FALSE(sys.isReachable(kOutside, kInside, kAgentRadius));
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
