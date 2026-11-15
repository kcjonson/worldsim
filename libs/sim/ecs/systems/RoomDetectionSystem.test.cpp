#include "RoomDetectionSystem.h"

#include "../World.h"
#include "../components/Room.h"

#include <construction/ConstructionWorld.h>

#include <core/Vec2i64.h>
#include <predicates/Predicates.h>

#include <polygon/Polygon.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using namespace ecs;
using construction::ConstructionWorld;
using construction::FoundationState;
using construction::kInvalidFoundation;
using construction::SegmentCommitResult;
using construction::SegmentId;
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

	// Commit a closed square loop of built walls; returns its first wall.
	SegmentId buildSquare(ConstructionWorld& cw, Vec2i64 corner, std::int64_t side) {
		const SegmentId first = buildWall(cw, corner, {corner.x + side, corner.y});
		buildWall(cw, {corner.x + side, corner.y}, {corner.x + side, corner.y + side});
		buildWall(cw, {corner.x + side, corner.y + side}, {corner.x, corner.y + side});
		buildWall(cw, {corner.x, corner.y + side}, corner);
		return first;
	}

	// The record whose outer ring (holes ignored) has the given area in m^2.
	const RoomDetectionSystem::RoomRecord* recordWithRingArea(const RoomDetectionSystem& sys, double squareMeters) {
		for (const RoomDetectionSystem::RoomRecord& rec : sys.rooms()) {
			if (std::fabs(geometry::signedAreaSquareMeters(rec.ring) - squareMeters) < 1e-6) {
				return &rec;
			}
		}
		return nullptr;
	}

	struct RoomIdentity {
		uint64_t	roomId = 0;
		std::string name;
	};

	RoomIdentity identityOf(const RoomDetectionSystem& sys, double squareMeters) {
		const RoomDetectionSystem::RoomRecord* rec = recordWithRingArea(sys, squareMeters);
		EXPECT_NE(rec, nullptr);
		return rec == nullptr ? RoomIdentity{} : RoomIdentity{rec->roomId, rec->name};
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

TEST(RoomDetectionSystem, BuildingAnInnerRoomKeepsTheOuterRoomAndAnnouncesOnlyTheInner) {
	World			  world;
	ConstructionWorld cw;
	auto&			  sys = world.registerSystem<RoomDetectionSystem>();
	sys.setConstructionWorld(&cw);

	std::vector<EntityID> formed;
	sys.setRoomFormedCallback([&formed](EntityID room) { formed.push_back(room); });

	buildSquare(cw, {0, 0}, 5000);
	sys.update(0.0f);
	ASSERT_EQ(formed.size(), 1u);
	const RoomIdentity outer = identityOf(sys, 25.0);

	buildSquare(cw, {3500, 3500}, 1000);
	sys.update(0.0f);

	ASSERT_EQ(sys.rooms().size(), 2u);
	const RoomDetectionSystem::RoomRecord* outerRecord = recordWithRingArea(sys, 25.0);
	const RoomDetectionSystem::RoomRecord* innerRecord = recordWithRingArea(sys, 1.0);
	ASSERT_NE(outerRecord, nullptr);
	ASSERT_NE(innerRecord, nullptr);
	EXPECT_EQ(outerRecord->roomId, outer.roomId);
	EXPECT_EQ(outerRecord->name, outer.name);
	EXPECT_EQ(outerRecord->holes.size(), 1u);
	EXPECT_NE(innerRecord->roomId, outer.roomId);
	EXPECT_EQ(innerRecord->name, "Room 2");

	ASSERT_EQ(formed.size(), 2u);
	EXPECT_EQ(formed[1], innerRecord->entity);

	const Room* outerRoom = world.getComponent<Room>(outerRecord->entity);
	ASSERT_NE(outerRoom, nullptr);
	EXPECT_NEAR(outerRoom->area, 24.0f, 1e-3f);
}

TEST(RoomDetectionSystem, RemovingAnInnerWallRetiresTheInnerRoomAndKeepsTheOuter) {
	World			  world;
	ConstructionWorld cw;
	auto&			  sys = world.registerSystem<RoomDetectionSystem>();
	sys.setConstructionWorld(&cw);

	int fired = 0;
	sys.setRoomFormedCallback([&fired](EntityID) { ++fired; });

	buildSquare(cw, {0, 0}, 5000);
	const SegmentId innerWall = buildSquare(cw, {2000, 2000}, 1000);
	sys.update(0.0f);
	ASSERT_EQ(sys.rooms().size(), 2u);
	const RoomIdentity outer = identityOf(sys, 25.0);
	const EntityID	   innerEntity = recordWithRingArea(sys, 1.0)->entity;

	cw.removeSegment(innerWall);
	sys.update(0.0f);

	ASSERT_EQ(sys.rooms().size(), 1u);
	EXPECT_EQ(sys.rooms()[0].roomId, outer.roomId);
	EXPECT_EQ(sys.rooms()[0].name, outer.name);
	EXPECT_TRUE(sys.rooms()[0].holes.empty());
	EXPECT_FALSE(world.isAlive(innerEntity));
	EXPECT_EQ(countRoomEntities(world), 1u);
	EXPECT_EQ(fired, 2);

	const Room* room = world.getComponent<Room>(sys.rooms()[0].entity);
	ASSERT_NE(room, nullptr);
	EXPECT_NEAR(room->area, 25.0f, 1e-3f);
}

TEST(RoomDetectionSystem, InnerRoomBuiltAroundTheOuterRoomsPointDoesNotTakeItsIdentity) {
	World			  world;
	ConstructionWorld cw;
	auto&			  sys = world.registerSystem<RoomDetectionSystem>();
	sys.setConstructionWorld(&cw);

	buildSquare(cw, {0, 0}, 5000);
	sys.update(0.0f);
	ASSERT_EQ(sys.rooms().size(), 1u);
	const RoomIdentity outer = identityOf(sys, 25.0);
	const Vec2i64	   storedPoint = sys.rooms()[0].rep;

	buildSquare(cw, {storedPoint.x - 500, storedPoint.y - 500}, 1000);
	sys.update(0.0f);

	ASSERT_EQ(sys.rooms().size(), 2u);
	const RoomDetectionSystem::RoomRecord* outerRecord = recordWithRingArea(sys, 25.0);
	const RoomDetectionSystem::RoomRecord* innerRecord = recordWithRingArea(sys, 1.0);
	ASSERT_NE(outerRecord, nullptr);
	ASSERT_NE(innerRecord, nullptr);
	EXPECT_EQ(geometry::pointInPolygon(storedPoint, innerRecord->ring), geometry::PointInPolygon::Inside);
	EXPECT_EQ(outerRecord->roomId, outer.roomId);
	EXPECT_EQ(outerRecord->name, outer.name);
	EXPECT_NE(innerRecord->roomId, outer.roomId);
	EXPECT_EQ(construction::pointInRoom(outerRecord->rep, outerRecord->ring, outerRecord->holes), geometry::PointInPolygon::Inside);
}

TEST(RoomDetectionSystem, ThreeLevelNestBuiltOutsideInKeepsEveryIdentity) {
	World			  world;
	ConstructionWorld cw;
	auto&			  sys = world.registerSystem<RoomDetectionSystem>();
	sys.setConstructionWorld(&cw);

	int fired = 0;
	sys.setRoomFormedCallback([&fired](EntityID) { ++fired; });

	buildSquare(cw, {0, 0}, 9000);
	sys.update(0.0f);
	buildSquare(cw, {2000, 2000}, 5000);
	sys.update(0.0f);
	const RoomIdentity outer = identityOf(sys, 81.0);
	const RoomIdentity middle = identityOf(sys, 25.0);
	buildSquare(cw, {4000, 4000}, 1000);
	sys.update(0.0f);

	ASSERT_EQ(sys.rooms().size(), 3u);
	EXPECT_EQ(fired, 3);
	EXPECT_EQ(identityOf(sys, 81.0).roomId, outer.roomId);
	EXPECT_EQ(identityOf(sys, 81.0).name, "Room 1");
	EXPECT_EQ(identityOf(sys, 25.0).roomId, middle.roomId);
	EXPECT_EQ(identityOf(sys, 25.0).name, "Room 2");
	EXPECT_EQ(identityOf(sys, 1.0).name, "Room 3");
}

TEST(RoomDetectionSystem, ThreeLevelNestBuiltInsideOutKeepsEveryIdentity) {
	World			  world;
	ConstructionWorld cw;
	auto&			  sys = world.registerSystem<RoomDetectionSystem>();
	sys.setConstructionWorld(&cw);

	int fired = 0;
	sys.setRoomFormedCallback([&fired](EntityID) { ++fired; });

	buildSquare(cw, {4000, 4000}, 1000);
	sys.update(0.0f);
	buildSquare(cw, {2000, 2000}, 5000);
	sys.update(0.0f);
	const RoomIdentity inner = identityOf(sys, 1.0);
	const RoomIdentity middle = identityOf(sys, 25.0);
	buildSquare(cw, {0, 0}, 9000);
	sys.update(0.0f);

	ASSERT_EQ(sys.rooms().size(), 3u);
	EXPECT_EQ(fired, 3);
	EXPECT_EQ(identityOf(sys, 1.0).roomId, inner.roomId);
	EXPECT_EQ(identityOf(sys, 1.0).name, "Room 1");
	EXPECT_EQ(identityOf(sys, 25.0).roomId, middle.roomId);
	EXPECT_EQ(identityOf(sys, 25.0).name, "Room 2");
	EXPECT_EQ(identityOf(sys, 81.0).name, "Room 3");
}