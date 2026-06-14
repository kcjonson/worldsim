#include "RoomDetection.h"

#include "ConstructionWorld.h"

#include <core/Vec2i64.h>

#include <algorithm>
#include <vector>

#include <gtest/gtest.h>

using namespace engine::construction;
using geometry::Vec2i64;

namespace {

	// Commit a built wall between integer-mm endpoints and return its SegmentId.
	SegmentId buildWall(ConstructionWorld& world, Vec2i64 a, Vec2i64 b) {
		SegmentCommitResult r = world.commitSegment(a, b, "wood", "standard", kInvalidFoundation);
		EXPECT_TRUE(r.ok());
		world.setSegmentState(r.id, FoundationState::Built);
		return r.id;
	}

	// Commit a wall left in Blueprint state.
	SegmentId blueprintWall(ConstructionWorld& world, Vec2i64 a, Vec2i64 b) {
		SegmentCommitResult r = world.commitSegment(a, b, "wood", "standard", kInvalidFoundation);
		EXPECT_TRUE(r.ok());
		return r.id;
	}

	bool boundsContain(const RoomFace& face, SegmentId id) {
		return std::find(face.boundingSegmentIds.begin(), face.boundingSegmentIds.end(), id) != face.boundingSegmentIds.end();
	}

} // namespace

TEST(RoomDetection, SingleRectangleIsOneRoom) {
	ConstructionWorld world;
	// 4m x 3m rectangle in mm.
	SegmentId s0 = buildWall(world, {0, 0}, {4000, 0});
	SegmentId s1 = buildWall(world, {4000, 0}, {4000, 3000});
	SegmentId s2 = buildWall(world, {4000, 3000}, {0, 3000});
	SegmentId s3 = buildWall(world, {0, 3000}, {0, 0});

	std::vector<RoomFace> rooms = detectRooms(world);
	ASSERT_EQ(rooms.size(), 1u);
	EXPECT_NEAR(rooms[0].area, 12.0f, 1e-3f); // 4 x 3 = 12 m^2

	EXPECT_EQ(rooms[0].boundingSegmentIds.size(), 4u);
	EXPECT_TRUE(boundsContain(rooms[0], s0));
	EXPECT_TRUE(boundsContain(rooms[0], s1));
	EXPECT_TRUE(boundsContain(rooms[0], s2));
	EXPECT_TRUE(boundsContain(rooms[0], s3));
}

TEST(RoomDetection, TwoAdjacentRectanglesShareWallTwoRooms) {
	ConstructionWorld world;
	// Left cell [0,1000]x[0,1000], right cell [1000,2000]x[0,1000], shared edge x=1000.
	buildWall(world, {0, 0}, {1000, 0});
	buildWall(world, {1000, 0}, {2000, 0});
	buildWall(world, {2000, 0}, {2000, 1000});
	buildWall(world, {2000, 1000}, {1000, 1000});
	buildWall(world, {1000, 1000}, {0, 1000});
	buildWall(world, {0, 1000}, {0, 0});
	buildWall(world, {1000, 0}, {1000, 1000}); // shared divider

	std::vector<RoomFace> rooms = detectRooms(world);
	EXPECT_EQ(rooms.size(), 2u);
}

TEST(RoomDetection, OpenChainIsNoRoom) {
	ConstructionWorld world;
	buildWall(world, {0, 0}, {1000, 0});
	buildWall(world, {1000, 0}, {1000, 1000});
	buildWall(world, {1000, 1000}, {0, 1000}); // three sides, not closed

	EXPECT_TRUE(detectRooms(world).empty());
}

TEST(RoomDetection, BlueprintWallsDoNotEncloseUntilBuilt) {
	ConstructionWorld world;
	// Commit a full loop but leave every segment in Blueprint state.
	blueprintWall(world, {0, 0}, {1000, 0});
	blueprintWall(world, {1000, 0}, {1000, 1000});
	blueprintWall(world, {1000, 1000}, {0, 1000});
	blueprintWall(world, {0, 1000}, {0, 0});

	EXPECT_TRUE(detectRooms(world).empty());

	// Flip them all to Built: now the loop encloses.
	for (const WallSegment& seg : world.segments()) {
		world.setSegmentState(seg.id, FoundationState::Built);
	}
	EXPECT_EQ(detectRooms(world).size(), 1u);
}

TEST(RoomDetection, OpeningOnWallStillEncloses) {
	ConstructionWorld world;
	SegmentId		  withDoor = buildWall(world, {0, 0}, {2000, 0});
	buildWall(world, {2000, 0}, {2000, 2000});
	buildWall(world, {2000, 2000}, {0, 2000});
	buildWall(world, {0, 2000}, {0, 0});

	// A door on a wall does not split its centerline; the loop still encloses.
	world.addOpening(withDoor, 0.5f, "door", "wood");

	std::vector<RoomFace> rooms = detectRooms(world);
	ASSERT_EQ(rooms.size(), 1u);
	EXPECT_TRUE(boundsContain(rooms[0], withDoor));
}

TEST(RoomDetection, NestedLoopsDetectedButEnclosingAreaIncludesHole_V1Limitation) {
	// KNOWN v1 LIMITATION (see the matching comment in RoomDetectionSystem): a loop
	// fully nested inside another with no connecting wall yields two faces, but
	// extractFaces represents the outer face by its outer cycle only -- the inner
	// loop is a separate component, so the enclosing face's area is the FULL outer
	// area, not the annulus. This pins the current behavior so a future hole-aware
	// fix is a visible, intentional change rather than a silent regression.
	ConstructionWorld world;
	// Inner 1m x 1m loop.
	buildWall(world, {2000, 2000}, {3000, 2000});
	buildWall(world, {3000, 2000}, {3000, 3000});
	buildWall(world, {3000, 3000}, {2000, 3000});
	buildWall(world, {2000, 3000}, {2000, 2000});
	// Outer 5m x 5m loop around it.
	buildWall(world, {0, 0}, {5000, 0});
	buildWall(world, {5000, 0}, {5000, 5000});
	buildWall(world, {5000, 5000}, {0, 5000});
	buildWall(world, {0, 5000}, {0, 0});

	std::vector<RoomFace> rooms = detectRooms(world);
	ASSERT_EQ(rooms.size(), 2u);

	float maxArea = 0.0f;
	for (const RoomFace& r : rooms) {
		maxArea = std::max(maxArea, r.area);
	}
	// Full outer 25 m^2, NOT the 24 m^2 annulus. When hole-aware extraction lands,
	// this expectation flips to 24.
	EXPECT_NEAR(maxArea, 25.0f, 1e-2f);
}
