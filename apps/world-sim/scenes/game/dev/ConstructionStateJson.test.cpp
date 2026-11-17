#include "scenes/game/dev/ConstructionStateJson.h"

#include <core/Vec2i64.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

	using Foundation::Vec2;

	bool contains(const std::string& json, const std::string& piece) {
		return json.find(piece) != std::string::npos;
	}

	construction::SegmentId commitWall(construction::ConstructionWorld& world, Vec2 start, Vec2 end) {
		const construction::SegmentCommitResult wall = world.commitSegment(
			geometry::quantize(start), geometry::quantize(end), "Wood", "Standard", construction::kInvalidFoundation, world.version()
		);
		EXPECT_TRUE(wall.ok());
		return wall.id;
	}

	const world_sim::BlueprintFragment kNoBlueprint = [](ecs::EntityID) { return std::string(); };

} // namespace

TEST(ConstructionStateJson, AFoundationsCornersKeepTheRingsOrderWithoutRepeatingTheFirst) {
	construction::ConstructionWorld world;
	const construction::CommitResult floor = world.commitFoundation(std::vector<Vec2>{{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 3.0F}, {0.0F, 3.0F}}, "Wood");
	ASSERT_TRUE(floor.ok());
	const std::string json = world_sim::foundationCornersJson(*world.get(floor.id));
	EXPECT_EQ(json, "[{\"x\":0.000,\"y\":0.000},{\"x\":4.000,\"y\":0.000},{\"x\":4.000,\"y\":3.000},{\"x\":0.000,\"y\":3.000}]");
}

TEST(ConstructionStateJson, AnOpeningAtTheMiddleOfAWallIsOnTheWallsLine) {
	construction::ConstructionWorld world;
	const construction::SegmentId wall = commitWall(world, {13.0F, 0.5F}, {17.0F, 0.5F});
	const construction::OpeningId door = world.addOpening(wall, 0.5F, "Door", "Wood");
	ASSERT_NE(door, construction::kInvalidOpening);
	const std::string json = world_sim::openingsJson(world, kNoBlueprint);
	EXPECT_TRUE(contains(json, "\"wall\":" + std::to_string(wall) + ",\"fractionAlongWall\":0.500,")) << json;
	EXPECT_TRUE(contains(json, "\"x\":15.000,\"y\":0.500")) << json;
	EXPECT_TRUE(contains(json, "\"type\":\"Door\"")) << json;
}

TEST(ConstructionStateJson, ASplitWallKeepsItsOpeningOnAWallThatExists) {
	construction::ConstructionWorld world;
	const construction::SegmentId wall = commitWall(world, {0.0F, 0.0F}, {8.0F, 0.0F});
	ASSERT_NE(world.addOpening(wall, 0.75F, "Door", "Wood"), construction::kInvalidOpening);
	commitWall(world, {4.0F, -4.0F}, {4.0F, 0.0F});
	ASSERT_EQ(world.openings().size(), 1U);
	const construction::SegmentId onWall = world.openings().front().segment;
	ASSERT_NE(world.getSegment(onWall), nullptr);
	const std::string json = world_sim::openingsJson(world, kNoBlueprint);
	EXPECT_TRUE(contains(json, "\"wall\":" + std::to_string(onWall) + ",\"fractionAlongWall\":0.500,")) << json;
	EXPECT_TRUE(contains(json, "\"x\":6.000,\"y\":0.000")) << json;
}

TEST(ConstructionStateJson, AnOpeningWithABlueprintEntityTakesTheFragment) {
	construction::ConstructionWorld world;
	const construction::SegmentId wall = commitWall(world, {0.0F, 0.0F}, {4.0F, 0.0F});
	const construction::OpeningId door = world.addOpening(wall, 0.5F, "Door", "Wood");
	ASSERT_NE(door, construction::kInvalidOpening);
	const world_sim::BlueprintFragment fragment = [](ecs::EntityID) { return std::string(",\"phase\":\"Clearing\""); };
	EXPECT_FALSE(contains(world_sim::openingsJson(world, fragment), "\"phase\"")) << "no entity yet";
	ASSERT_TRUE(world.setOpeningEntity(door, 7));
	EXPECT_TRUE(contains(world_sim::openingsJson(world, fragment), ",\"entity\":7,\"x\":2.000,\"y\":0.000,\"phase\":\"Clearing\"}"));
}

TEST(ConstructionStateJson, NoOpeningsIsAnEmptyList) {
	const construction::ConstructionWorld world;
	EXPECT_EQ(world_sim::openingsJson(world, kNoBlueprint), "[]");
}
