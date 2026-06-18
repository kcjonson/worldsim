#include "VisionSystem.h"

#include "../World.h"
#include "../components/Appearance.h"
#include "../components/Memory.h"
#include "../components/Transform.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/RecipeRegistry.h>
#include <construction/ConstructionWorld.h>

#include <glm/vec2.hpp>

#include <filesystem>
#include <string>

#include <gtest/gtest.h>

using namespace ecs;
using engine::assets::AssetRegistry;
using engine::assets::CapabilityType;
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

	// Build a built wall segment (every created sub-segment marked Built).
	SegmentId buildWall(ConstructionWorld& cw, Vec2i64 a, Vec2i64 b) {
		SegmentCommitResult r = cw.commitSegment(a, b, "Wood", "Standard", kInvalidFoundation);
		EXPECT_TRUE(r.ok());
		for (SegmentId id : r.createdSegments) {
			cw.setSegmentState(id, FoundationState::Built);
		}
		return r.id;
	}

	// Add a BUILT opening at parameter t.
	OpeningId addBuiltOpening(ConstructionWorld& cw, SegmentId seg, float t, const std::string& type) {
		OpeningId op = cw.addOpening(seg, t, type, "Wood");
		EXPECT_NE(op, kInvalidOpening);
		EXPECT_TRUE(cw.setOpeningState(op, FoundationState::Built));
		return op;
	}

	// A discoverable target def: synthetic, with a non-zero capability so
	// rememberWorldEntity actually stores it (mask 0 is dropped as decorative).
	constexpr const char* kTargetDefName = "Test_VisionTarget";

	// An observer colonist at `where` and a target entity at `target`, both ECS
	// entities so pass 2 (pure ECS Appearance scan) exercises them with no
	// PlacementExecutor or ChunkManager wired. Returns the observer's id.
	EntityID spawnObserver(World& world, glm::vec2 where) {
		EntityID e = world.createEntity();
		world.addComponent<Position>(e, Position{where});
		world.addComponent<Memory>(e, Memory{});
		world.getComponent<Memory>(e)->owner = e;
		return e;
	}

	EntityID spawnTarget(World& world, glm::vec2 at) {
		EntityID e = world.createEntity();
		world.addComponent<Position>(e, Position{at});
		Appearance ap;
		ap.defName = kTargetDefName;
		world.addComponent<Appearance>(e, ap);
		return e;
	}

	// Did the observer's Memory record the target's def at `at`?
	bool remembers(World& world, EntityID observer, glm::vec2 at) {
		const Memory* mem = world.getComponent<Memory>(observer);
		return mem != nullptr && mem->knowsWorldEntity(at, kTargetDefName);
	}

	class VisionSystemTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			ConstructionRegistry::Get().clear();
			ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder()));
			// ID 0 is the AssetRegistry's "invalid" sentinel. A fresh registry (these
			// tests don't load assets) has an empty index, so prime a throwaway def to
			// claim ID 0 before the target def, guaranteeing the target gets non-zero.
			auto& reg = AssetRegistry::Get();
			if (reg.getDefNameId("Test_Vision_IdZeroReservation") == 0) {
				reg.registerSyntheticDefinition("Test_Vision_IdZeroReservation", 0);
			}
			// A synthetic discoverable def with one capability so memory keeps it.
			reg.registerSyntheticDefinition(
				kTargetDefName, static_cast<uint8_t>(1u << static_cast<uint8_t>(CapabilityType::Edible)));
			ASSERT_NE(reg.getDefNameId(kTargetDefName), 0u) << "target def must get a non-zero id";
		}
		void TearDown() override { ConstructionRegistry::Get().clear(); }
	};

	// Drive a single immediate vision tick (interval 1 so update() never throttles).
	void tick(VisionSystem& sys) {
		sys.setUpdateInterval(1);
		sys.update(0.0F);
	}

} // namespace

// --- Outdoor fast path -------------------------------------------------------

// No construction world wired: a target inside the sight radius is remembered,
// exactly as before walls existed. The occlusion gate must add no regression on
// the common outdoor path.
TEST_F(VisionSystemTest, OutdoorTargetRememberedNoWorld) {
	World world;
	VisionSystem& sys = world.registerSystem<VisionSystem>();

	EntityID observer = spawnObserver(world, {0.0F, 0.0F});
	const glm::vec2 targetPos{3.0F, 0.0F};
	spawnTarget(world, targetPos);

	tick(sys);

	EXPECT_TRUE(remembers(world, observer, targetPos));
	// No occluders anywhere => no polygon ever built (pure fast path).
	EXPECT_EQ(sys.polygonBuildCount(), 0u);
}

// A construction world with no walls is still the fast path: empty occluder set.
TEST_F(VisionSystemTest, OutdoorTargetRememberedEmptyWorld) {
	ConstructionWorld cw; // no segments

	World world;
	VisionSystem& sys = world.registerSystem<VisionSystem>();
	sys.setConstructionWorld(&cw);

	EntityID observer = spawnObserver(world, {0.0F, 0.0F});
	const glm::vec2 targetPos{3.0F, 0.0F};
	spawnTarget(world, targetPos);

	tick(sys);

	EXPECT_TRUE(remembers(world, observer, targetPos));
	EXPECT_EQ(sys.polygonBuildCount(), 0u);
}

// --- Occlusion (headline behavior) -------------------------------------------

// Observer and target on opposite sides of a solid built wall: the target is
// hidden and NOT remembered.
TEST_F(VisionSystemTest, SolidWallHidesTarget) {
	ConstructionWorld cw;
	buildWall(cw, {1500, 0}, {1500, 3000}); // vertical wall at x=1.5m, y in [0,3]

	World world;
	VisionSystem& sys = world.registerSystem<VisionSystem>();
	sys.setConstructionWorld(&cw);

	EntityID observer = spawnObserver(world, {0.5F, 1.5F}); // west of wall
	const glm::vec2 targetPos{2.5F, 1.5F};					// east of wall, directly behind
	spawnTarget(world, targetPos);

	tick(sys);

	EXPECT_FALSE(remembers(world, observer, targetPos)) << "wall must block sight to the target";
	EXPECT_GT(sys.polygonBuildCount(), 0u) << "an occluder in range should force a polygon build";
}

// Same wall, but the target is on the SAME side as the observer (not behind it):
// it is visible and remembered.
TEST_F(VisionSystemTest, TargetOnSameSideIsVisible) {
	ConstructionWorld cw;
	buildWall(cw, {1500, 0}, {1500, 3000});

	World world;
	VisionSystem& sys = world.registerSystem<VisionSystem>();
	sys.setConstructionWorld(&cw);

	EntityID observer = spawnObserver(world, {0.5F, 1.5F});
	const glm::vec2 targetPos{0.8F, 1.5F}; // west of wall, same side as observer
	spawnTarget(world, targetPos);

	tick(sys);

	EXPECT_TRUE(remembers(world, observer, targetPos));
}

// Demolishing the wall (demote to Blueprint) bumps the version: the next tick
// rebuilds against an empty occluder set, the observer drops to the fast path,
// and the previously hidden target becomes remembered.
TEST_F(VisionSystemTest, RemovingWallRevealsTarget) {
	ConstructionWorld cw;
	SegmentId wall = buildWall(cw, {1500, 0}, {1500, 3000});

	World world;
	VisionSystem& sys = world.registerSystem<VisionSystem>();
	sys.setConstructionWorld(&cw);

	EntityID observer = spawnObserver(world, {0.5F, 1.5F});
	const glm::vec2 targetPos{2.5F, 1.5F};
	spawnTarget(world, targetPos);

	tick(sys);
	ASSERT_FALSE(remembers(world, observer, targetPos)) << "precondition: wall hides target";

	// Demote the wall to Blueprint: a non-Built segment contributes no occluder.
	const std::uint64_t before = cw.version();
	ASSERT_TRUE(cw.setSegmentState(wall, FoundationState::Blueprint));
	ASSERT_NE(cw.version(), before);

	tick(sys);
	EXPECT_TRUE(remembers(world, observer, targetPos)) << "removing the wall must reveal the target";
}

// --- Doorway -----------------------------------------------------------------

// A built wall with a Door gap between observer and target, the target aligned
// with the gap: the target is visible through the doorway and remembered.
TEST_F(VisionSystemTest, TargetVisibleThroughDoorGap) {
	ConstructionWorld cw;
	SegmentId wall = buildWall(cw, {1500, 0}, {1500, 3000}); // vertical wall, y in [0,3]
	addBuiltOpening(cw, wall, 0.5F, "Door");				 // gap centered at y=1.5m

	World world;
	VisionSystem& sys = world.registerSystem<VisionSystem>();
	sys.setConstructionWorld(&cw);

	EntityID observer = spawnObserver(world, {0.5F, 1.5F}); // aligned with the door center
	const glm::vec2 targetPos{2.5F, 1.5F};					// straight through the gap
	spawnTarget(world, targetPos);

	tick(sys);

	EXPECT_TRUE(remembers(world, observer, targetPos)) << "target should be visible through the door gap";
}

// Same doorway wall, but the target sits behind the SOLID flank (not aligned with
// the gap): it stays hidden.
TEST_F(VisionSystemTest, TargetBehindSolidFlankHidden) {
	ConstructionWorld cw;
	SegmentId wall = buildWall(cw, {1500, 0}, {1500, 3000});
	addBuiltOpening(cw, wall, 0.5F, "Door"); // gap centered at y=1.5m

	World world;
	VisionSystem& sys = world.registerSystem<VisionSystem>();
	sys.setConstructionWorld(&cw);

	EntityID observer = spawnObserver(world, {0.5F, 1.5F});
	const glm::vec2 targetPos{2.5F, 0.3F}; // behind the lower solid flank, off the gap
	spawnTarget(world, targetPos);

	tick(sys);

	EXPECT_FALSE(remembers(world, observer, targetPos)) << "solid flank must still block sight";
}

// --- Caching -----------------------------------------------------------------

// A stationary indoor observer builds its polygon once, then reuses it across
// several ticks (build count stays at 1). Building a new wall nearby bumps the
// GeometryIndex generation, which invalidates the cache and rebuilds.
TEST_F(VisionSystemTest, StationaryObserverReusesPolygon) {
	ConstructionWorld cw;
	buildWall(cw, {1500, 0}, {1500, 3000});

	World world;
	VisionSystem& sys = world.registerSystem<VisionSystem>();
	sys.setConstructionWorld(&cw);

	spawnObserver(world, {0.5F, 1.5F});

	tick(sys);
	const uint64_t afterFirst = sys.polygonBuildCount();
	EXPECT_EQ(afterFirst, 1u) << "first tick with an occluder in range builds once";

	tick(sys);
	tick(sys);
	EXPECT_EQ(sys.polygonBuildCount(), afterFirst) << "stationary observer must reuse the cached polygon";

	// Build another wall: version bumps -> generation moves -> cache invalidates.
	const std::uint64_t before = cw.version();
	buildWall(cw, {500, 2000}, {1000, 2000});
	ASSERT_NE(cw.version(), before);

	tick(sys);
	EXPECT_GT(sys.polygonBuildCount(), afterFirst) << "a new wall must invalidate the cached polygon";
}
