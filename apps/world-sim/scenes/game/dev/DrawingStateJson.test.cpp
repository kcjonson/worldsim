#include "scenes/game/dev/DrawingStateJson.h"

#include <gtest/gtest.h>

#include <string>

namespace {

	using world_sim::DrawingSnapshot;

	bool contains(const std::string& json, const std::string& piece) {
		return json.find(piece) != std::string::npos;
	}

} // namespace

TEST(DrawingStateJson, NoToolIsNoneWithNothingProposed) {
	DrawingSnapshot shot;
	shot.verdict = {.valid = true, .proposed = false};
	const std::string json = world_sim::drawingStateJson(shot);
	EXPECT_TRUE(contains(json, "\"tool\":\"none\"")) << json;
	EXPECT_TRUE(contains(json, "\"lastFoundationTool\":\"rectangle\"")) << json;
	EXPECT_TRUE(contains(json, "\"points\":[]")) << json;
	EXPECT_TRUE(contains(json, "\"snap\":null")) << json;
	EXPECT_TRUE(contains(json, "\"verdict\":{\"valid\":true,\"proposed\":false,\"reason\":\"\"}")) << json;
}

TEST(DrawingStateJson, AFreeformShapeMidDrawNamesItsSnapGuidesAndRefusal) {
	DrawingSnapshot shot;
	shot.tool = world_sim::ToolKind::Foundation;
	shot.lastFoundationTool = world_sim::FoundationTool::Freeform;
	shot.followingPointer = true;
	shot.points = {{0.0F, 0.0F}, {4.0F, 0.0F}};
	shot.snap = DrawingSnapshot::Snap{
		.point = {4.0F, -2.5F},
		.kind = construction::SnapKind::GuideLine,
		.guides = {construction::SnapGuide{
			.from = {4.0F, 0.0F},
			.source = construction::GuideSource::NearbyStructure,
			.relation = construction::GuideRelation::Perpendicular,
			.distanceFromNodeMeters = 2.5F,
		}},
		.lengthSnap = construction::LengthSnapKind::CommonLength,
		.lengthMeters = 2.5F,
		.matchedEdgeCount = 2,
		.commonLengths = {2.5F, 4.0F},
	};
	shot.verdict = {.valid = false, .proposed = true, .reason = "the \"area\" is unexplored"};
	shot.status.active = true;
	shot.status.pointCount = 2;
	shot.status.material = "Wood";
	const std::string json = world_sim::drawingStateJson(shot);
	EXPECT_TRUE(contains(json, "\"tool\":\"foundationFreeform\"")) << json;
	EXPECT_TRUE(contains(json, "\"points\":[{\"x\":0.000,\"y\":0.000},{\"x\":4.000,\"y\":0.000}]")) << json;
	EXPECT_TRUE(contains(json, "\"point\":{\"x\":4.000,\"y\":-2.500},\"kind\":\"GuideLine\"")) << json;
	EXPECT_TRUE(contains(json, "\"source\":\"NearbyStructure\",\"relation\":\"Perpendicular\",\"from\":{\"x\":4.000,\"y\":0.000},\"distanceFromNodeMeters\":2.500")) << json;
	EXPECT_TRUE(contains(json, "\"lengthSnap\":\"CommonLength\",\"lengthMeters\":2.500,\"matchedEdgeCount\":2,\"commonLengthsMeters\":[2.500,4.000]")) << json;
	EXPECT_TRUE(contains(json, "\"closesShape\":false")) << json;
	EXPECT_TRUE(contains(json, "\"reason\":\"the \\\"area\\\" is unexplored\"")) << json;
	EXPECT_FALSE(contains(json, "\"rectangle\"")) << json;
}

TEST(DrawingStateJson, TheRectangleToolReportsItsCornersUnderTheRectangleKey) {
	DrawingSnapshot shot;
	shot.tool = world_sim::ToolKind::FoundationRectangle;
	shot.followingPointer = true;
	shot.pressPending = true;
	shot.points = {{2.0F, 1.0F}};
	shot.rectangleCorners = {{2.0F, 1.0F}, {6.0F, 1.0F}, {6.0F, 4.0F}, {2.0F, 4.0F}};
	shot.rectangleLabel = "4 x 3 m";
	shot.snap = DrawingSnapshot::Snap{.point = {6.0F, 4.0F}, .rectangleKind = construction::RectangleSnapKind::Grid, .closesShape = true};
	const std::string json = world_sim::drawingStateJson(shot);
	EXPECT_TRUE(contains(json, "\"tool\":\"foundationRectangle\"")) << json;
	EXPECT_TRUE(contains(json, "\"rectangle\":{\"pressPending\":true,\"firstCorner\":{\"x\":2.000,\"y\":1.000},\"corners\":[")) << json;
	EXPECT_TRUE(contains(json, "\"sizeLabel\":\"4 x 3 m\"")) << json;
	EXPECT_TRUE(contains(json, "\"kind\":\"Grid\"")) << json;
}
