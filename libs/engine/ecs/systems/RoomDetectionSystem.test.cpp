#include "RoomDetectionSystem.h"

#include "../World.h"
#include "../components/Room.h"

#include <construction/ConstructionWorld.h>

#include <core/Vec2i64.h>
#include <predicates/Predicates.h>

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

using namespace ecs;
using engine::construction::ConstructionWorld;
using engine::construction::FoundationState;
using engine::construction::kInvalidFoundation;
using engine::construction::SegmentCommitResult;
using engine::construction::SegmentId;
using geometry::Vec2i64;

namespace {

	SegmentId buildWall(ConstructionWorld& cw, Vec2i64 a, Vec2i64 b) {
		SegmentCommitResult r = cw.commitSegment(a, b, "wood", "standard", kInvalidFoundation);
		EXPECT_TRUE(r.ok());
		// A T-junction commit can create several segments (the new span plus the
		// split halves of any host it lands on); build every one of them.
		for (SegmentId id : r.createdSegments) {
			cw.setSegmentState(id, FoundationState::Built);
		}
		return r.id;
	}

	std::size_t countRoomEntities(World& world) {
		std::size_t n = 0;
		for ([[maybe_unused]] auto&& _ : world.view<Room>()) {
			++n;
		}
		return n;
	}

	// Commit a closed 4m x 3m loop of built walls.
	void buildClosedLoop(ConstructionWorld& cw) {
		buildWall(cw, {0, 0}, {4000, 0});
		buildWall(cw, {4000, 0}, {4000, 3000});
		buildWall(cw, {4000, 3000}, {0, 3000});
		buildWall(cw, {0, 3000}, {0, 0});
	}

} // namespace

TEST(RoomDetectionSystem, ClosedLoopFormsOneRoomAndFiresCallbackOnce) {
	World			  world;
	ConstructionWorld cw;
	auto&			  sys = world.registerSystem<RoomDetectionSystem>();
	sys.setConstructionWorld(&cw);

	int fired = 0;
	sys.setRoomFormedCallback([&fired](EntityID) { ++fired; });

	buildClosedLoop(cw);
	sys.update(0.0f);

	EXPECT_EQ(countRoomEntities(world), 1u);
	EXPECT_EQ(fired, 1);
	ASSERT_EQ(sys.rooms().size(), 1u);

	const Room* room = world.getComponent<Room>(sys.rooms()[0].entity);
	ASSERT_NE(room, nullptr);
	EXPECT_NEAR(room->area, 12.0f, 1e-3f);
	EXPECT_EQ(room->name, "Room 1");
}

TEST(RoomDetectionSystem, SplittingARoomKeepsIdentityForTheRepContainingHalf) {
	World			  world;
	ConstructionWorld cw;
	auto&			  sys = world.registerSystem<RoomDetectionSystem>();
	sys.setConstructionWorld(&cw);

	int fired = 0;
	sys.setRoomFormedCallback([&fired](EntityID) { ++fired; });

	buildClosedLoop(cw);
	sys.update(0.0f);
	ASSERT_EQ(sys.rooms().size(), 1u);
	EXPECT_EQ(fired, 1);

	const uint64_t	  originalId = sys.rooms()[0].roomId;
	const std::string originalName = sys.rooms()[0].name;
	const Vec2i64	  originalRep = sys.rooms()[0].rep;

	// Add an interior divider, offset so the original rep is not on the cut line.
	// (If the rep landed on x=2000 the test would be ambiguous; the loop's rep is
	// well off-center, so any vertical line away from it is safe.)
	const std::int64_t cutX = (originalRep.x < 2000) ? 2800 : 1200;
	buildWall(cw, {cutX, 0}, {cutX, 3000});
	sys.update(0.0f);

	ASSERT_EQ(sys.rooms().size(), 2u);
	EXPECT_EQ(fired, 2); // exactly one NEW room beyond the inherited one

	// The sub-room whose ring contains the original rep must keep id + name.
	bool foundInheritor = false;
	bool foundFresh = false;
	for (const RoomDetectionSystem::RoomRecord& rec : sys.rooms()) {
		const bool containsOriginalRep = geometry::pointInPolygon(originalRep, rec.ring) == geometry::PointInPolygon::Inside;
		if (containsOriginalRep) {
			EXPECT_EQ(rec.roomId, originalId);
			EXPECT_EQ(rec.name, originalName);
			foundInheritor = true;
		} else {
			EXPECT_NE(rec.roomId, originalId);
			foundFresh = true;
		}
	}
	EXPECT_TRUE(foundInheritor);
	EXPECT_TRUE(foundFresh);
	EXPECT_EQ(countRoomEntities(world), 2u);
}

TEST(RoomDetectionSystem, DividerThroughStoredRepKeepsIdentityOnExactlyOneSide) {
	World			  world;
	ConstructionWorld cw;
	auto&			  sys = world.registerSystem<RoomDetectionSystem>();
	sys.setConstructionWorld(&cw);

	int fired = 0;
	sys.setRoomFormedCallback([&fired](EntityID) { ++fired; });

	buildClosedLoop(cw); // 4m x 3m
	sys.update(0.0f);
	ASSERT_EQ(sys.rooms().size(), 1u);
	EXPECT_EQ(fired, 1);

	const uint64_t	  originalId = sys.rooms()[0].roomId;
	const std::string originalName = sys.rooms()[0].name;
	const Vec2i64	  rep = sys.rooms()[0].rep;

	// Divider drawn exactly through the stored rep (vertical line at x = rep.x): the
	// rep lands ON the divider, so it is OnBoundary -- not Inside -- of both halves.
	// Without the OnBoundary fallback both halves reset identity; with it, exactly
	// one side keeps id + name and exactly one new room forms.
	buildWall(cw, {rep.x, 0}, {rep.x, 3000});
	sys.update(0.0f);

	ASSERT_EQ(sys.rooms().size(), 2u);
	EXPECT_EQ(fired, 2); // exactly one NEW room beyond the inherited one

	std::size_t keptOriginal = 0;
	for (const RoomDetectionSystem::RoomRecord& rec : sys.rooms()) {
		if (rec.roomId == originalId) {
			++keptOriginal;
			EXPECT_EQ(rec.name, originalName);
		}
	}
	EXPECT_EQ(keptOriginal, 1u);
	EXPECT_EQ(countRoomEntities(world), 2u);
}

TEST(RoomDetectionSystem, DemolishingAWallDestroysTheRoom) {
	World			  world;
	ConstructionWorld cw;
	auto&			  sys = world.registerSystem<RoomDetectionSystem>();
	sys.setConstructionWorld(&cw);

	buildWall(cw, {0, 0}, {4000, 0});
	const SegmentId top = buildWall(cw, {4000, 0}, {4000, 3000});
	buildWall(cw, {4000, 3000}, {0, 3000});
	buildWall(cw, {0, 3000}, {0, 0});
	sys.update(0.0f);
	ASSERT_EQ(sys.rooms().size(), 1u);
	const EntityID roomEntity = sys.rooms()[0].entity;
	ASSERT_TRUE(world.isAlive(roomEntity));

	cw.removeSegment(top); // break the loop
	sys.update(0.0f);

	EXPECT_TRUE(sys.rooms().empty());
	EXPECT_EQ(countRoomEntities(world), 0u);
	EXPECT_FALSE(world.isAlive(roomEntity));
}

TEST(RoomDetectionSystem, NoVersionChangeIsANoOp) {
	World			  world;
	ConstructionWorld cw;
	auto&			  sys = world.registerSystem<RoomDetectionSystem>();
	sys.setConstructionWorld(&cw);

	int fired = 0;
	sys.setRoomFormedCallback([&fired](EntityID) { ++fired; });

	buildClosedLoop(cw);
	sys.update(0.0f);
	EXPECT_EQ(fired, 1);
	EXPECT_EQ(countRoomEntities(world), 1u);

	// Second update with no topology change: no new entities, no extra callbacks.
	sys.update(0.0f);
	EXPECT_EQ(fired, 1);
	EXPECT_EQ(countRoomEntities(world), 1u);
	EXPECT_EQ(sys.rooms().size(), 1u);
}
