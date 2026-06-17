#include "WallCollisionSystem.h"

#include "../World.h"
#include "../components/AgentRadius.h"
#include "../components/Transform.h"

#include <assets/ConstructionRegistry.h>
#include <construction/ConstructionWorld.h>

#include <cmath>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

using namespace ecs;
using engine::assets::ConstructionRegistry;
using engine::construction::ConstructionWorld;
using engine::construction::FoundationState;
using engine::construction::kInvalidFoundation;
using engine::construction::kInvalidOpening;
using engine::construction::OpeningId;
using engine::construction::SegmentCommitResult;
using engine::construction::SegmentId;
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

	// Build a built wall segment (every created sub-segment marked Built). Mirrors
	// NavigationSystem.test.cpp's helper.
	SegmentId buildWall(ConstructionWorld& cw, Vec2i64 a, Vec2i64 b) {
		SegmentCommitResult r = cw.commitSegment(a, b, "Wood", "Standard", kInvalidFoundation);
		EXPECT_TRUE(r.ok());
		for (SegmentId id : r.createdSegments) {
			cw.setSegmentState(id, FoundationState::Built);
		}
		return r.id;
	}

	// Standard "Standard" Wood preset half-thickness in meters, read from config so
	// the asserts track the real band, not a hard-coded guess.
	float standardHalfThicknessMeters() {
		const auto* preset = ConstructionRegistry::Get().getThicknessPreset("Wood", "Standard");
		EXPECT_NE(preset, nullptr);
		return static_cast<float>(preset->halfThicknessMm) / 1000.0F;
	}

	// Distance from point p to the infinite line through a..b (the wall centerline).
	float distToCenterline(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
		const glm::vec2 d	= b - a;
		const float		len = std::sqrt(glm::dot(d, d));
		const glm::vec2 n	= {-d.y / len, d.x / len};
		return std::abs(glm::dot(p - a, n));
	}

	// Fixture: load construction config once per test (singleton), spawn one agent.
	class WallCollisionSystemTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			ConstructionRegistry::Get().clear();
			ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder()));
		}
		void TearDown() override { ConstructionRegistry::Get().clear(); }

		EntityID spawnAgent(World& world, glm::vec2 pos, float radius = 0.3F) {
			EntityID e = world.createEntity();
			world.addComponent<Position>(e, Position{pos});
			world.addComponent<AgentRadius>(e, AgentRadius{radius, 1.0F});
			return e;
		}
	};

	// Endpoints of a horizontal wall along y=0 from x=0 to x=4 (meters).
	const glm::vec2 kWallA{0.0F, 0.0F};
	const glm::vec2 kWallB{4.0F, 0.0F};

} // namespace

// An agent overlapping a solid built wall band is pushed clear: after update() its
// distance to the centerline is >= halfThickness + radius, on the side it started.
TEST_F(WallCollisionSystemTest, AgentInSolidWallIsPushedOut) {
	ConstructionWorld cw;
	buildWall(cw, {0, 0}, {4000, 0});

	World world;
	const float radius	= 0.3F;
	const glm::vec2 start{2.0F, 0.05F}; // just above the centerline, inside the band
	EntityID agent = spawnAgent(world, start, radius);

	auto& sys = world.registerSystem<WallCollisionSystem>();
	sys.setConstructionWorld(&cw);
	sys.update(0.0F);

	auto* pos = world.getComponent<Position>(agent);
	ASSERT_NE(pos, nullptr);

	const float clearance = standardHalfThicknessMeters() + radius;
	const float d		  = distToCenterline(pos->value, kWallA, kWallB);
	EXPECT_GE(d, clearance - 1e-3F) << "agent not cleared of the band";
	EXPECT_GT(pos->value.y, 0.0F) << "agent pushed to the wrong side"; // stayed above
}

// An agent centered in a DOOR gap (built pathable opening) is NOT pushed: the wall
// has a real gap there, so it stays put inside the gap. Proves doors stay passable.
TEST_F(WallCollisionSystemTest, AgentInDoorGapIsNotPushed) {
	ConstructionWorld cw;
	SegmentId south = buildWall(cw, {0, 0}, {4000, 0});
	OpeningId door	= cw.addOpening(south, 0.5F, "Door", "Wood"); // t=0.5 -> x=2.0
	ASSERT_NE(door, kInvalidOpening);
	ASSERT_TRUE(cw.setOpeningState(door, FoundationState::Built));

	World world;
	const glm::vec2 start{2.0F, 0.0F}; // dead center of the door, on the centerline
	EntityID agent = spawnAgent(world, start, 0.3F);

	auto& sys = world.registerSystem<WallCollisionSystem>();
	sys.setConstructionWorld(&cw);
	sys.update(0.0F);

	auto* pos = world.getComponent<Position>(agent);
	ASSERT_NE(pos, nullptr);
	EXPECT_FLOAT_EQ(pos->value.x, start.x);
	EXPECT_FLOAT_EQ(pos->value.y, start.y);
}

// An agent at a WINDOW (built non-pathable opening) IS pushed out: windows leave
// the band solid. Proves the door exemption is pathable-only.
TEST_F(WallCollisionSystemTest, AgentAtWindowIsPushedOut) {
	ConstructionWorld cw;
	SegmentId south = buildWall(cw, {0, 0}, {4000, 0});
	OpeningId win	= cw.addOpening(south, 0.5F, "Window", "Wood");
	ASSERT_NE(win, kInvalidOpening);
	ASSERT_TRUE(cw.setOpeningState(win, FoundationState::Built));

	World world;
	const float radius = 0.3F;
	EntityID	agent  = spawnAgent(world, {2.0F, 0.0F}, radius);

	auto& sys = world.registerSystem<WallCollisionSystem>();
	sys.setConstructionWorld(&cw);
	sys.update(0.0F);

	auto* pos = world.getComponent<Position>(agent);
	ASSERT_NE(pos, nullptr);
	const float clearance = standardHalfThicknessMeters() + radius;
	EXPECT_GE(distToCenterline(pos->value, kWallA, kWallB), clearance - 1e-3F);
}

// An agent against a BLUEPRINT (not built) wall is NOT pushed: only built walls are
// physical.
TEST_F(WallCollisionSystemTest, BlueprintWallDoesNotCollide) {
	ConstructionWorld cw;
	// Commit but leave Blueprint (do NOT setSegmentState to Built).
	SegmentCommitResult r = cw.commitSegment({0, 0}, {4000, 0}, "Wood", "Standard", kInvalidFoundation);
	ASSERT_TRUE(r.ok());

	World world;
	const glm::vec2 start{2.0F, 0.0F};
	EntityID agent = spawnAgent(world, start, 0.3F);

	auto& sys = world.registerSystem<WallCollisionSystem>();
	sys.setConstructionWorld(&cw);
	sys.update(0.0F);

	auto* pos = world.getComponent<Position>(agent);
	ASSERT_NE(pos, nullptr);
	EXPECT_FLOAT_EQ(pos->value.x, start.x);
	EXPECT_FLOAT_EQ(pos->value.y, start.y);
}

// An agent far from any wall is unchanged.
TEST_F(WallCollisionSystemTest, AgentFarFromWallIsUnchanged) {
	ConstructionWorld cw;
	buildWall(cw, {0, 0}, {4000, 0});

	World world;
	const glm::vec2 start{2.0F, 5.0F}; // 5 m above the wall, well clear
	EntityID agent = spawnAgent(world, start, 0.3F);

	auto& sys = world.registerSystem<WallCollisionSystem>();
	sys.setConstructionWorld(&cw);
	sys.update(0.0F);

	auto* pos = world.getComponent<Position>(agent);
	ASSERT_NE(pos, nullptr);
	EXPECT_FLOAT_EQ(pos->value.x, start.x);
	EXPECT_FLOAT_EQ(pos->value.y, start.y);
}

// An agent wedged into the inside corner of two perpendicular built walls ends up
// clear of BOTH after the relaxation iterations. Walls: south along y=0 and west
// along x=0; the inside corner is at the origin, interior is +x,+y. The agent
// starts just inside that corner.
TEST_F(WallCollisionSystemTest, CornerWedgeClearsBothWalls) {
	ConstructionWorld cw;
	buildWall(cw, {0, 0}, {4000, 0}); // south, along y=0
	buildWall(cw, {0, 0}, {0, 4000}); // west,  along x=0

	World world;
	const float		radius = 0.3F;
	const glm::vec2 start{0.05F, 0.05F}; // deep in the corner, inside both bands
	EntityID		agent = spawnAgent(world, start, radius);

	auto& sys = world.registerSystem<WallCollisionSystem>();
	sys.setConstructionWorld(&cw);
	sys.update(0.0F);

	auto* pos = world.getComponent<Position>(agent);
	ASSERT_NE(pos, nullptr);

	const float clearance = standardHalfThicknessMeters() + radius;
	// Clear of south wall (centerline y=0) => |y| >= clearance.
	EXPECT_GE(std::abs(pos->value.y), clearance - 1e-3F) << "not clear of south wall";
	// Clear of west wall (centerline x=0) => |x| >= clearance.
	EXPECT_GE(std::abs(pos->value.x), clearance - 1e-3F) << "not clear of west wall";
	// Pushed into the interior, not out the back.
	EXPECT_GT(pos->value.x, 0.0F);
	EXPECT_GT(pos->value.y, 0.0F);
}

// update() with no construction world wired is a safe no-op.
TEST_F(WallCollisionSystemTest, UnwiredUpdateIsNoOp) {
	World world;
	const glm::vec2 start{1.0F, 1.0F};
	EntityID agent = spawnAgent(world, start, 0.3F);

	auto& sys = world.registerSystem<WallCollisionSystem>();
	// Deliberately do NOT setConstructionWorld.
	sys.update(0.0F);

	auto* pos = world.getComponent<Position>(agent);
	ASSERT_NE(pos, nullptr);
	EXPECT_FLOAT_EQ(pos->value.x, start.x);
	EXPECT_FLOAT_EQ(pos->value.y, start.y);
}
