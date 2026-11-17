#include "scenes/game/dev/DrawingStateJson.h"

#include "scenes/game/world/construction/DrawingStatusHint.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace {

	using world_sim::DrawingSnapshot;
	using world_sim::DrawingStateContext;
	using world_sim::StatusHintTone;

	bool contains(const std::string& json, const std::string& piece) {
		return json.find(piece) != std::string::npos;
	}

	// The view's answer, which must always be JSON a script can parse.
	std::string render(const DrawingSnapshot& shot, const DrawingStateContext& context = {}) {
		const std::string json = world_sim::drawingStateJson(shot, context);
		EXPECT_TRUE(nlohmann::json::accept(json)) << json;
		return json;
	}

} // namespace

TEST(DrawingStateJson, NoToolIsNoneWithNothingProposed) {
	DrawingSnapshot shot;
	shot.verdict = {.valid = true, .proposed = false};
	const std::string json = render(shot);
	EXPECT_TRUE(contains(json, "\"tool\":\"none\"")) << json;
	EXPECT_TRUE(contains(json, "\"lastFoundationTool\":\"rectangle\"")) << json;
	EXPECT_TRUE(contains(json, "\"points\":[]")) << json;
	EXPECT_TRUE(contains(json, "\"pointer\":null")) << json;
	EXPECT_TRUE(contains(json, "\"snap\":null")) << json;
	EXPECT_TRUE(contains(json, "\"verdict\":{\"valid\":true,\"proposed\":false,\"reason\":\"\"}")) << json;
}

TEST(DrawingStateJson, AFreeformShapeMidDrawNamesItsSnapGuidesAndRefusal) {
	DrawingSnapshot shot;
	shot.tool = world_sim::ToolKind::Foundation;
	shot.lastFoundationTool = world_sim::FoundationTool::Freeform;
	shot.followingPointer = true;
	shot.pointer = Foundation::Vec2{4.2F, -2.6F};
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
		.segmentLengthMeters = 2.5F,
		.matchedEdgeCount = 2,
		.commonLengths = {2.5F, 4.0F},
	};
	shot.verdict = {.valid = false, .proposed = true, .reason = "the \"area\" is unexplored"};
	shot.status.active = true;
	shot.status.pointCount = 2;
	shot.status.material = "Wood";
	const std::string json = render(shot);
	EXPECT_TRUE(contains(json, "\"tool\":\"foundationFreeform\"")) << json;
	EXPECT_TRUE(contains(json, "\"pointer\":{\"x\":4.200,\"y\":-2.600},\"points\":[{\"x\":0.000,\"y\":0.000},{\"x\":4.000,\"y\":0.000}]")) << json;
	EXPECT_TRUE(contains(json, "\"point\":{\"x\":4.000,\"y\":-2.500},\"kind\":\"GuideLine\"")) << json;
	EXPECT_TRUE(contains(json, "\"source\":\"NearbyStructure\",\"relation\":\"Perpendicular\",\"from\":{\"x\":4.000,\"y\":0.000},\"distanceFromNodeMeters\":2.500")) << json;
	EXPECT_TRUE(contains(json, "\"lengthSnap\":\"CommonLength\",\"lengthMeters\":2.500,\"segmentLengthMeters\":2.500,\"matchedEdgeCount\":2,\"commonLengthsMeters\":[2.500,4.000]")) << json;
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
	const std::string json = render(shot);
	EXPECT_TRUE(contains(json, "\"tool\":\"foundationRectangle\"")) << json;
	EXPECT_TRUE(contains(json, "\"rectangle\":{\"pressPending\":true,\"firstCorner\":{\"x\":2.000,\"y\":1.000},\"corners\":[")) << json;
	EXPECT_TRUE(contains(json, "\"sizeLabel\":\"4 x 3 m\"")) << json;
	EXPECT_TRUE(contains(json, "\"kind\":\"Grid\"")) << json;
	EXPECT_TRUE(contains(json, "\"segmentLengthMeters\":null")) << json;
}

TEST(DrawingStateJson, TheOpeningToolNamesTheWallAndHowFarAlongItTheOpeningIs) {
	DrawingSnapshot shot;
	shot.tool = world_sim::ToolKind::Opening;
	shot.followingPointer = true;
	shot.openingType = "Door";
	shot.openingSnap = construction::OpeningSnap{.segment = 7, .t = 0.5F, .point = {4.0F, -26.0F}, .valid = true};
	const std::string json = render(shot);
	EXPECT_TRUE(contains(json, "\"opening\":{\"type\":\"Door\",\"snap\":{\"wall\":7,\"fractionAlongWall\":0.500,\"point\":{\"x\":4.000,\"y\":-26.000}}}")) << json;
}

TEST(DrawingStateJson, SettingsKeyGuideAndDialogAreThereWithOrWithoutATool) {
	DrawingStateContext context;
	context.settings.angleSteps = true;
	context.settings.rectangleGridSpacingMeters = 0.5F;
	context.dialogOpen = true;
	DrawingSnapshot withTool;
	withTool.tool = world_sim::ToolKind::Wall;
	for (const DrawingSnapshot& shot : {DrawingSnapshot{}, withTool}) {
		const auto parsed = nlohmann::json::parse(render(shot, context));
		const auto& settings = parsed.at("settings");
		EXPECT_EQ(settings.size(), 11U);
		EXPECT_TRUE(settings.at("angleSteps").get<bool>());
		EXPECT_FALSE(settings.at("guidesFromShape").get<bool>());
		EXPECT_DOUBLE_EQ(settings.at("rectangleGridSpacingMeters").get<double>(), 0.5);
		EXPECT_TRUE(parsed.at("dialogOpen").get<bool>());
		EXPECT_TRUE(parsed.at("keyGuide").contains("shown"));
	}
	context.dialogOpen = false;
	context.settings.angleSteps = false;
	const auto parsed = nlohmann::json::parse(render(withTool, context));
	EXPECT_FALSE(parsed.at("dialogOpen").get<bool>());
	EXPECT_FALSE(parsed.at("settings").at("angleSteps").get<bool>());
}

TEST(DrawingStateJson, TheKeyGuideIsShownWhenAToolIsActiveAndTheSettingIsOn) {
	struct Case {
		bool tool;
		bool showKeyGuide;
		bool collapsed;
		bool shown;
	};
	const std::vector<Case> cases = {
		{false, true, false, false},
		{true, false, false, false},
		{true, true, false, true},
		{true, true, true, true},
	};
	for (const Case& entry : cases) {
		DrawingSnapshot shot;
		if (entry.tool) {
			shot.tool = world_sim::ToolKind::Foundation;
		}
		DrawingStateContext context;
		context.settings.showKeyGuide = entry.showKeyGuide;
		context.keyGuideCollapsed = entry.collapsed;
		const auto keyGuide = nlohmann::json::parse(render(shot, context)).at("keyGuide");
		EXPECT_EQ(keyGuide.at("shown").get<bool>(), entry.shown) << entry.tool << entry.showKeyGuide << entry.collapsed;
		EXPECT_EQ(keyGuide.at("collapsed").get<bool>(), entry.collapsed) << entry.tool << entry.showKeyGuide << entry.collapsed;
	}
}

TEST(DrawingStateJson, TheHintIsTheStripsOwnLineForEachStateItDistinguishes) {
	using world_sim::DrawingStatus;
	struct Case {
		const char*	   name;
		DrawingStatus  status;
		const char*	   text;
		StatusHintTone tone;
	};
	const auto rectangle = [](int points, bool valid, const char* message) {
		return DrawingStatus{.active = true, .foundationTool = world_sim::FoundationTool::Rectangle, .pointCount = points, .valid = valid, .message = message};
	};
	const auto freeform = [](int points, bool valid, const char* message) {
		return DrawingStatus{.active = true, .foundationTool = world_sim::FoundationTool::Freeform, .pointCount = points, .valid = valid, .message = message};
	};
	const auto wall = [](int points, bool valid, const char* message) {
		return DrawingStatus{.active = true, .wall = true, .pointCount = points, .valid = valid, .message = message};
	};
	const auto opening = [](bool valid, const char* message) {
		return DrawingStatus{.active = true, .opening = true, .valid = valid, .message = message};
	};
	const std::vector<Case> cases = {
		{"no tool", {}, "Ready", StatusHintTone::Ok},
		{"rectangle, no corner", rectangle(0, true, ""), "Click or drag from a corner", StatusHintTone::Ok},
		{"rectangle, one corner", rectangle(1, true, ""), "Click the opposite corner", StatusHintTone::Ok},
		{"rectangle, refused", rectangle(1, false, "not on buildable ground"), "not on buildable ground", StatusHintTone::Crit},
		{"freeform, no point", freeform(0, true, ""), "Ready", StatusHintTone::Ok},
		{"freeform, two points", freeform(2, true, ""), "Keep placing points", StatusHintTone::Warn},
		{"freeform, three points", freeform(3, true, ""), "Click origin to close", StatusHintTone::Ok},
		{"freeform, refused", freeform(3, false, "crosses itself"), "crosses itself", StatusHintTone::Crit},
		{"edit, no point",
		 DrawingStatus{.active = true, .foundationEdit = true, .pointCount = 0},
		 "Draw a region across the foundation edge",
		 StatusHintTone::Ok},
		{"wall, no foundation yet", wall(0, true, ""), "Click a foundation to start", StatusHintTone::Warn},
		{"wall, chain started", wall(2, true, ""), "Right-click / Enter to finish", StatusHintTone::Ok},
		{"wall, refused", wall(2, false, "off the foundation"), "off the foundation", StatusHintTone::Crit},
		{"opening, fits", opening(true, ""), "Click a wall to place", StatusHintTone::Ok},
		{"opening, no wall near", opening(false, ""), "Hover a built wall", StatusHintTone::Warn},
		{"opening, no wall in range", opening(false, "no wall in range"), "no wall in range", StatusHintTone::Warn},
		{"opening, blocked", opening(false, "too close to a corner"), "too close to a corner", StatusHintTone::Crit},
	};
	for (const Case& entry : cases) {
		const world_sim::StatusHint hint = world_sim::statusHint(entry.status);
		EXPECT_EQ(hint.text, entry.text) << entry.name;
		EXPECT_EQ(hint.tone, entry.tone) << entry.name;
		DrawingSnapshot shot;
		shot.status = entry.status;
		const auto parsed = nlohmann::json::parse(render(shot));
		EXPECT_EQ(parsed.at("status").at("hint").get<std::string>(), entry.text) << entry.name;
	}
}
