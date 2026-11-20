#include "RoomDetectionSystem.h"

#include "../World.h"
#include "../components/Room.h"

#include <construction/ConstructionWorld.h>

#include <core/Vec2i64.h>
#include <predicates/Predicates.h>

#include <polygon/Polygon.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <utility>
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
		SegmentCommitResult r = cw.commitSegment(a, b, "wood", "standard", kInvalidFoundation, cw.version());
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

TEST(RoomDetectionSystem, InnerRoomBuiltRoundTheOuterPointWhileTheOuterRingChangesTakesItsName) {
	// A known limit of point matching when several walls change in one reconcile (WOR-875).
	World			  world;
	ConstructionWorld cw;
	auto&			  sys = world.registerSystem<RoomDetectionSystem>();
	sys.setConstructionWorld(&cw);

	std::vector<EntityID> formed;
	sys.setRoomFormedCallback([&formed](EntityID room) { formed.push_back(room); });

	buildSquare(cw, {0, 0}, 5000);
	sys.update(0.0f);
	ASSERT_EQ(sys.rooms().size(), 1u);
	const RoomIdentity outer = identityOf(sys, 25.0);
	const Vec2i64	   storedPoint = sys.rooms()[0].rep;

	// In one reconcile: a free-standing 1 m room round the stored point, and a spur whose
	// T-junction splits a side wall, so the outer ring is no longer the stored one. The
	// spur comes off whichever side wall is farther from the inner room.
	buildSquare(cw, {storedPoint.x - 500, storedPoint.y - 500}, 1000);
	if (storedPoint.x > 2500) {
		buildWall(cw, {0, 2500}, {1000, 2500});
	} else {
		buildWall(cw, {5000, 2500}, {4000, 2500});
	}
	sys.update(0.0f);

	ASSERT_EQ(sys.rooms().size(), 2u);
	const RoomDetectionSystem::RoomRecord* outerRecord = recordWithRingArea(sys, 25.0);
	const RoomDetectionSystem::RoomRecord* innerRecord = recordWithRingArea(sys, 1.0);
	ASSERT_NE(outerRecord, nullptr);
	ASSERT_NE(innerRecord, nullptr);
	ASSERT_EQ(outerRecord->holes.size(), 1u);
	EXPECT_EQ(geometry::pointInPolygon(storedPoint, innerRecord->ring), geometry::PointInPolygon::Inside);
	EXPECT_EQ(innerRecord->roomId, outer.roomId);
	EXPECT_EQ(innerRecord->name, outer.name);
	EXPECT_NE(outerRecord->roomId, outer.roomId);
	EXPECT_EQ(outerRecord->name, "Room 2");
	ASSERT_EQ(formed.size(), 2u);
	EXPECT_EQ(formed[1], outerRecord->entity);
}

TEST(RoomDetectionSystem, RebuildingARoomSmallerOnEverySideInOneReconcileKeepsItsIdentity) {
	World			  world;
	ConstructionWorld cw;
	auto&			  sys = world.registerSystem<RoomDetectionSystem>();
	sys.setConstructionWorld(&cw);

	int fired = 0;
	sys.setRoomFormedCallback([&fired](EntityID) { ++fired; });

	const SegmentId outerWalls[] = {
		buildWall(cw, {0, 0}, {5000, 0}),
		buildWall(cw, {5000, 0}, {5000, 5000}),
		buildWall(cw, {5000, 5000}, {0, 5000}),
		buildWall(cw, {0, 5000}, {0, 0}),
	};
	sys.update(0.0f);
	ASSERT_EQ(sys.rooms().size(), 1u);
	const RoomIdentity room = identityOf(sys, 25.0);
	const EntityID	   roomEntity = sys.rooms()[0].entity;

	buildSquare(cw, {500, 500}, 4000);
	for (const SegmentId wall : outerWalls) {
		cw.removeSegment(wall);
	}
	sys.update(0.0f);

	ASSERT_EQ(sys.rooms().size(), 1u);
	EXPECT_EQ(identityOf(sys, 16.0).roomId, room.roomId);
	EXPECT_EQ(identityOf(sys, 16.0).name, room.name);
	EXPECT_EQ(sys.rooms()[0].entity, roomEntity);
	EXPECT_EQ(fired, 1);
}

TEST(RoomDetectionSystem, RebuildingOneWallFurtherInKeepsTheShrunkRoom) {
	World			  world;
	ConstructionWorld cw;
	auto&			  sys = world.registerSystem<RoomDetectionSystem>();
	sys.setConstructionWorld(&cw);

	int fired = 0;
	sys.setRoomFormedCallback([&fired](EntityID) { ++fired; });

	buildWall(cw, {0, 0}, {5000, 0});
	const SegmentId right = buildWall(cw, {5000, 0}, {5000, 5000});
	buildWall(cw, {5000, 5000}, {0, 5000});
	const SegmentId left = buildWall(cw, {0, 5000}, {0, 0});
	sys.update(0.0f);
	const RoomIdentity room = identityOf(sys, 25.0);
	const Vec2i64	   storedPoint = sys.rooms()[0].rep;

	// Move the side wall farther from the stored point 1 m in, in one reconcile.
	if (storedPoint.x < 2500) {
		buildWall(cw, {4000, 0}, {4000, 5000});
		cw.removeSegment(right);
	} else {
		buildWall(cw, {1000, 0}, {1000, 5000});
		cw.removeSegment(left);
	}
	sys.update(0.0f);

	ASSERT_EQ(sys.rooms().size(), 1u);
	EXPECT_EQ(identityOf(sys, 20.0).roomId, room.roomId);
	EXPECT_EQ(identityOf(sys, 20.0).name, room.name);
	EXPECT_EQ(fired, 1);
}

TEST(RoomDetectionSystem, InnerRoomOutlivesTheOuterRoomWhenAnOuterWallGoes) {
	World			  world;
	ConstructionWorld cw;
	auto&			  sys = world.registerSystem<RoomDetectionSystem>();
	sys.setConstructionWorld(&cw);

	int fired = 0;
	sys.setRoomFormedCallback([&fired](EntityID) { ++fired; });

	const SegmentId outerWall = buildSquare(cw, {0, 0}, 5000);
	buildSquare(cw, {2000, 2000}, 1000);
	sys.update(0.0f);
	const RoomIdentity inner = identityOf(sys, 1.0);
	const EntityID	   innerEntity = recordWithRingArea(sys, 1.0)->entity;
	const EntityID	   outerEntity = recordWithRingArea(sys, 25.0)->entity;

	cw.removeSegment(outerWall);
	sys.update(0.0f);

	ASSERT_EQ(sys.rooms().size(), 1u);
	EXPECT_EQ(sys.rooms()[0].roomId, inner.roomId);
	EXPECT_EQ(sys.rooms()[0].name, inner.name);
	EXPECT_EQ(sys.rooms()[0].entity, innerEntity);
	EXPECT_FALSE(world.isAlive(outerEntity));
	EXPECT_EQ(countRoomEntities(world), 1u);
	EXPECT_EQ(fired, 2);
}

TEST(RoomDetectionSystem, SplittingARoomBesideItsInnerRoomKeepsEveryIdentityAndAnnouncesTheNewHalf) {
	World			  world;
	ConstructionWorld cw;
	auto&			  sys = world.registerSystem<RoomDetectionSystem>();
	sys.setConstructionWorld(&cw);

	std::vector<EntityID> formed;
	sys.setRoomFormedCallback([&formed](EntityID room) { formed.push_back(room); });

	buildSquare(cw, {0, 0}, 5000);
	buildSquare(cw, {2000, 2000}, 1000);
	sys.update(0.0f);
	const RoomIdentity outer = identityOf(sys, 25.0);
	const RoomIdentity inner = identityOf(sys, 1.0);
	const Vec2i64	   outerPoint = recordWithRingArea(sys, 25.0)->rep;
	ASSERT_EQ(formed.size(), 2u);
	ASSERT_NE(outerPoint.x, 1000);

	buildWall(cw, {1000, 0}, {1000, 5000});
	sys.update(0.0f);

	ASSERT_EQ(sys.rooms().size(), 3u);
	const RoomDetectionSystem::RoomRecord* strip = recordWithRingArea(sys, 5.0);
	const RoomDetectionSystem::RoomRecord* rest = recordWithRingArea(sys, 20.0);
	ASSERT_NE(strip, nullptr);
	ASSERT_NE(rest, nullptr);
	EXPECT_EQ(rest->holes.size(), 1u);
	EXPECT_EQ(identityOf(sys, 1.0).roomId, inner.roomId);
	const RoomDetectionSystem::RoomRecord* keeper = outerPoint.x < 1000 ? strip : rest;
	const RoomDetectionSystem::RoomRecord* fresh = outerPoint.x < 1000 ? rest : strip;
	EXPECT_EQ(keeper->roomId, outer.roomId);
	EXPECT_EQ(keeper->name, outer.name);
	EXPECT_EQ(fresh->name, "Room 3");
	ASSERT_EQ(formed.size(), 3u);
	EXPECT_EQ(formed[2], fresh->entity);
}

TEST(RoomDetectionSystem, MergingTwoRoomsThatEachHoldAnInnerRoomKeepsTheLargerNameAndBothInnerRooms) {
	World			  world;
	ConstructionWorld cw;
	auto&			  sys = world.registerSystem<RoomDetectionSystem>();
	sys.setConstructionWorld(&cw);

	int fired = 0;
	sys.setRoomFormedCallback([&fired](EntityID) { ++fired; });

	// A 5 x 5 m room and a 6 x 5 m room sharing the wall at x = 5000, a 1 m room in each.
	buildWall(cw, {0, 0}, {11000, 0});
	buildWall(cw, {11000, 0}, {11000, 5000});
	buildWall(cw, {11000, 5000}, {0, 5000});
	buildWall(cw, {0, 5000}, {0, 0});
	buildWall(cw, {5000, 0}, {5000, 5000});
	buildSquare(cw, {2000, 2000}, 1000);
	buildSquare(cw, {7500, 2000}, 1000);
	sys.update(0.0f);
	ASSERT_EQ(sys.rooms().size(), 4u);
	const RoomIdentity larger = identityOf(sys, 30.0);
	const EntityID	   smallerEntity = recordWithRingArea(sys, 25.0)->entity;

	std::vector<uint64_t> innerIds;
	for (const RoomDetectionSystem::RoomRecord& rec : sys.rooms()) {
		if (rec.holes.empty()) {
			innerIds.push_back(rec.roomId);
		}
	}
	ASSERT_EQ(innerIds.size(), 2u);

	cw.removeSegment(cw.segmentAt({5000, 2500}, 1));
	sys.update(0.0f);

	ASSERT_EQ(sys.rooms().size(), 3u);
	const RoomDetectionSystem::RoomRecord* merged = recordWithRingArea(sys, 55.0);
	ASSERT_NE(merged, nullptr);
	EXPECT_EQ(merged->holes.size(), 2u);
	EXPECT_EQ(merged->roomId, larger.roomId);
	EXPECT_EQ(merged->name, larger.name);
	EXPECT_FALSE(world.isAlive(smallerEntity));
	for (const RoomDetectionSystem::RoomRecord& rec : sys.rooms()) {
		if (&rec != merged) {
			EXPECT_NE(std::find(innerIds.begin(), innerIds.end(), rec.roomId), innerIds.end());
		}
	}
	EXPECT_EQ(fired, 4);
}

TEST(RoomDetectionSystem, IdentityWithoutNestingIsWhatTheStoredPointsAlonePick) {
	// Random wall additions and removals on two separate 5 x 5 grids of 1 m edges, removals
	// rarer so the grids fill up, and every tenth step one wall toggled twice (a rebuild
	// that changes no room). After every reconcile each room must hold the identity the
	// point passes alone pick: the dominant record whose stored point is strictly inside
	// it, then, for rooms still without one, the dominant record whose point is on its
	// boundary, first room first; anything else is new. The same-ring pass is absent from
	// that model, so a match shows that without nesting it never changes what the point
	// passes pick. Edits that would leave a room with a hole are undone before the
	// reconcile, because nesting is where the two part: an inner room built round the
	// outer room's stored point takes it in the model, while the same-ring pass keeps it
	// with the unchanged outer room.
	constexpr std::int64_t kGrid = 5;
	constexpr std::int64_t kCell = 1000;
	constexpr std::int64_t kStride = kGrid * kCell + 1500;

	std::vector<std::pair<Vec2i64, Vec2i64>> edges;
	for (std::int64_t grid = 0; grid < 2; ++grid) {
		for (std::int64_t i = 0; i < kGrid; ++i) {
			for (std::int64_t j = 0; j < kGrid; ++j) {
				const Vec2i64 point{grid * kStride + i * kCell, j * kCell};
				if (i + 1 < kGrid) {
					edges.push_back({point, {point.x + kCell, point.y}});
				}
				if (j + 1 < kGrid) {
					edges.push_back({point, {point.x, point.y + kCell}});
				}
			}
		}
	}

	World			  world;
	ConstructionWorld cw;
	auto&			  sys = world.registerSystem<RoomDetectionSystem>();
	sys.setConstructionWorld(&cw);
	std::size_t formedCount = 0;
	sys.setRoomFormedCallback([&formedCount](EntityID) { ++formedCount; });

	std::vector<SegmentId> built(edges.size(), construction::kInvalidSegment);

	const auto toggle = [&](std::size_t e) {
		if (built[e] == construction::kInvalidSegment) {
			built[e] = buildWall(cw, edges[e].first, edges[e].second);
		} else {
			cw.removeSegment(built[e]);
			built[e] = construction::kInvalidSegment;
		}
	};

	std::mt19937 rng(4096);
	std::size_t	 noOpSteps = 0;
	std::size_t	 merges = 0;
	std::size_t	 inheritedWithNewRing = 0;
	std::size_t	 retiredRooms = 0;
	for (int step = 0; step < 400; ++step) {
		const std::vector<RoomDetectionSystem::RoomRecord> before = sys.rooms();
		uint64_t										   highestId = 0;
		for (const RoomDetectionSystem::RoomRecord& rec : before) {
			highestId = std::max(highestId, rec.roomId);
		}

		const bool noOp = step % 10 == 9;
		if (noOp) {
			const std::size_t e = rng() % edges.size();
			toggle(e);
			toggle(e);
			++noOpSteps;
		} else {
			std::vector<std::size_t> changed;
			const std::size_t		 changeCount = 1 + rng() % 3;
			for (std::size_t c = 0; c < changeCount; ++c) {
				const std::size_t e = rng() % edges.size();
				if (std::find(changed.begin(), changed.end(), e) != changed.end() ||
					(built[e] != construction::kInvalidSegment && rng() % 3 != 0)) {
					continue;
				}
				changed.push_back(e);
				toggle(e);
			}
			const std::vector<construction::RoomFace> probe = construction::detectRooms(cw);
			if (std::any_of(probe.begin(), probe.end(), [](const construction::RoomFace& face) { return !face.holes.empty(); })) {
				for (auto it = changed.rbegin(); it != changed.rend(); ++it) {
					toggle(*it);
				}
			}
		}

		const std::size_t formedBefore = formedCount;
		sys.update(0.0f);
		const std::vector<construction::RoomFace> faces = construction::detectRooms(cw);
		ASSERT_EQ(sys.rooms().size(), faces.size());

		constexpr std::size_t	 kNone = static_cast<std::size_t>(-1);
		std::vector<std::size_t> expected(faces.size(), kNone);
		std::vector<bool>		 claimed(before.size(), false);
		for (const geometry::PointInPolygon want : {geometry::PointInPolygon::Inside, geometry::PointInPolygon::OnBoundary}) {
			for (std::size_t fi = 0; fi < faces.size(); ++fi) {
				if (expected[fi] != kNone) {
					continue;
				}
				std::size_t candidates = 0;
				for (std::size_t ri = 0; ri < before.size(); ++ri) {
					if (claimed[ri] || construction::pointInRoom(before[ri].rep, faces[fi].ring, faces[fi].holes) != want) {
						continue;
					}
					++candidates;
					const std::size_t best = expected[fi];
					if (best == kNone || before[ri].areaDoubled > before[best].areaDoubled ||
						(before[ri].areaDoubled == before[best].areaDoubled && before[ri].roomId < before[best].roomId)) {
						expected[fi] = ri;
					}
				}
				if (expected[fi] != kNone) {
					claimed[expected[fi]] = true;
				}
				if (want == geometry::PointInPolygon::Inside && candidates > 1) {
					++merges;
				}
			}
		}

		std::size_t freshCount = 0;
		for (std::size_t fi = 0; fi < faces.size(); ++fi) {
			const RoomDetectionSystem::RoomRecord& rec = sys.rooms()[fi];
			EXPECT_EQ(rec.ring, faces[fi].ring);
			if (expected[fi] == kNone) {
				++freshCount;
				EXPECT_GT(rec.roomId, highestId) << "step " << step << " room " << fi;
				continue;
			}
			EXPECT_EQ(rec.roomId, before[expected[fi]].roomId) << "step " << step << " room " << fi;
			EXPECT_EQ(rec.name, before[expected[fi]].name);
			if (rec.ring != before[expected[fi]].ring) {
				++inheritedWithNewRing;
			}
		}
		EXPECT_EQ(formedCount - formedBefore, freshCount) << "step " << step;
		if (noOp) {
			ASSERT_EQ(sys.rooms().size(), before.size());
			for (std::size_t fi = 0; fi < before.size(); ++fi) {
				EXPECT_EQ(sys.rooms()[fi].roomId, before[fi].roomId) << "step " << step;
			}
		}
		retiredRooms += static_cast<std::size_t>(std::count(claimed.begin(), claimed.end(), false));
		EXPECT_EQ(countRoomEntities(world), faces.size());
	}
	EXPECT_EQ(noOpSteps, 40u);
	EXPECT_GT(merges, 15u);
	EXPECT_GT(inheritedWithNewRing, 35u);
	EXPECT_GT(retiredRooms, 50u);
	EXPECT_GT(formedCount, 60u);
}

TEST(RoomDetectionSystem, FreeUClosedWithASpurToTheOuterWallFollowsTheStoredPoint) {
	// Two edges in one step and no hole at any step, so the random history's filter keeps
	// this one: the closed U joins the outer room's wall group, and the room it makes round
	// the stored point takes the outer record, as the point passes alone pick.
	World			  world;
	ConstructionWorld cw;
	auto&			  sys = world.registerSystem<RoomDetectionSystem>();
	sys.setConstructionWorld(&cw);

	std::vector<EntityID> formed;
	sys.setRoomFormedCallback([&formed](EntityID room) { formed.push_back(room); });

	buildSquare(cw, {0, 0}, 5000);
	sys.update(0.0f);
	ASSERT_EQ(sys.rooms().size(), 1u);
	const Vec2i64 storedPoint = sys.rooms()[0].rep;
	const Vec2i64 low{storedPoint.x - 500, storedPoint.y - 500};
	const Vec2i64 high{storedPoint.x + 500, storedPoint.y + 500};

	buildWall(cw, low, {high.x, low.y});
	buildWall(cw, {high.x, low.y}, high);
	buildWall(cw, high, {low.x, high.y});
	sys.update(0.0f);
	ASSERT_EQ(sys.rooms().size(), 1u);
	ASSERT_EQ(sys.rooms()[0].rep, storedPoint);
	ASSERT_TRUE(sys.rooms()[0].holes.empty());

	buildWall(cw, {low.x, high.y}, low);
	buildWall(cw, {0, storedPoint.y}, {low.x, storedPoint.y});
	sys.update(0.0f);

	ASSERT_EQ(sys.rooms().size(), 2u);
	const RoomDetectionSystem::RoomRecord* outerRecord = recordWithRingArea(sys, 24.0);
	const RoomDetectionSystem::RoomRecord* innerRecord = recordWithRingArea(sys, 1.0);
	ASSERT_NE(outerRecord, nullptr);
	ASSERT_NE(innerRecord, nullptr);
	EXPECT_TRUE(outerRecord->holes.empty());
	EXPECT_EQ(innerRecord->roomId, 1u);
	EXPECT_EQ(innerRecord->name, "Room 1");
	EXPECT_EQ(outerRecord->roomId, 2u);
	EXPECT_EQ(outerRecord->name, "Room 2");
	ASSERT_EQ(formed.size(), 2u);
	EXPECT_EQ(formed[1], outerRecord->entity);
}
