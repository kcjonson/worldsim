// The key guide's content against the design spec's tables (UI & Selection > Key guide), the keys that collapse it,
// and its legend glyphs against the preview's look.

#include "DrawingKeyGuide.h"

#include "scenes/game/world/construction/DrawingSystem.h"

#include <assets/ConstructionRegistry.h>

#include <gtest/gtest.h>

#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace world_sim {

	namespace {

		std::string phrase(const std::vector<KeyGuidePart>& parts) {
			std::string text;
			for (const KeyGuidePart& part : parts) {
				text += (text.empty() ? "" : " ") + part.text;
			}
			return text;
		}

		// A tool's rows read as the design spec's table cell: "Keys: action." each, the action's first letter in
		// lower case.
		std::string tableCell(ToolKind tool) {
			std::string cell;
			for (const KeyGuideKeyRow& row : keyGuideContent(tool, DrawingSettings{}).keyRows) {
				std::string action = row.action;
				if (!action.empty()) {
					action.front() = static_cast<char>(std::tolower(static_cast<unsigned char>(action.front())));
				}
				cell += (cell.empty() ? "" : " ") + phrase(row.keys) + ": " + action + ".";
			}
			return cell;
		}

		std::vector<LegendGlyph> glyphs(ToolKind tool, const DrawingSettings& settings) {
			std::vector<LegendGlyph> shown;
			for (const KeyGuideLegendRow& row : keyGuideContent(tool, settings).legendRows) {
				shown.push_back(row.glyph);
			}
			return shown;
		}

		DrawingSettings everyHelper() {
			return {
				.angleSteps = true,
				.guidesFromShape = true,
				.alignToExistingBuildings = true,
				.guidesFromWorldAxes = true,
				.guideCrossings = true,
				.commonLengths = true,
				.guideDistances = true,
				.angleReadout = true,
				.roundLengths = true,
				.showKeyGuide = true,
			};
		}

	} // namespace

	TEST(KeyGuideContentTest, EachToolListsTheDesignSpecsKeyRows) {
		EXPECT_EQ(
			tableCell(ToolKind::FoundationRectangle),
			"Click: place two opposite corners (or drag). Backspace / Esc / Right-click: drop the first corner. Esc again: leave "
			"the tool. Alt: hold to place the raw cursor (no grid, no snapping)."
		);
		EXPECT_EQ(
			tableCell(ToolKind::Foundation),
			"Click: place a point. Click point 1: close the shape. Backspace: undo a point. Esc / Right-click: cancel the shape. "
			"Esc again: leave the tool. Alt: hold to turn snapping off."
		);
		EXPECT_EQ(
			tableCell(ToolKind::Wall),
			"Click: place a point. Click point 1: close the loop. Double-click / Enter / Right-click: end the wall. Backspace: "
			"undo a point. Esc: end the wall. Esc again: leave the tool. Ctrl+click a foundation edge: wall the whole edge. Alt: "
			"hold to turn snapping off."
		);
		EXPECT_EQ(tableCell(ToolKind::Opening), "Click a wall: place it. Esc / Right-click: leave the tool.");
		EXPECT_EQ(
			tableCell(ToolKind::FoundationEdit),
			"Click: place a point. Click point 1: close the shape and apply it (the tool stays on for the next). Backspace: "
			"undo a point. Esc / Right-click: cancel the shape. Esc again: leave the tool. Alt: hold to turn snapping off."
		);
	}

	// Only the keys themselves are chips; "again", "point 1" and the separators are words.
	TEST(KeyGuideContentTest, OnlyKeysAreChips) {
		const std::vector<KeyGuideKeyRow> rows = keyGuideContent(ToolKind::Wall, DrawingSettings{}).keyRows;
		ASSERT_GE(rows.size(), 3U);
		EXPECT_EQ(rows[1].keys, (std::vector<KeyGuidePart>{{.text = "Click", .chip = true}, {.text = "point 1", .chip = false}}));
		EXPECT_EQ(
			rows[2].keys,
			(std::vector<KeyGuidePart>{
				{.text = "Double-click", .chip = true},
				{.text = "/", .chip = false},
				{.text = "Enter", .chip = true},
				{.text = "/", .chip = false},
				{.text = "Right-click", .chip = true},
			})
		);
	}

	TEST(KeyGuideContentTest, TheFootLineSaysHowToHideIt) {
		const KeyGuideContent content = keyGuideContent(ToolKind::Foundation, DrawingSettings{});
		EXPECT_EQ(phrase(content.foot), "? or H hides this");
		EXPECT_TRUE(content.foot[0].chip);
		EXPECT_TRUE(content.foot[2].chip);
	}

	TEST(KeyGuideContentTest, EachLegendRowIsSwitchedByItsHelpers) {
		using enum LegendGlyph;
		const std::vector<std::pair<DrawingSettings, std::vector<LegendGlyph>>> cases{
			{DrawingSettings{}, {}},
			{DrawingSettings{.angleSteps = true}, {Guide, RightAngle}},
			{DrawingSettings{.guidesFromShape = true}, {Guide, RightAngle}},
			{DrawingSettings{.alignToExistingBuildings = true}, {Guide, RightAngle}},
			{DrawingSettings{.guidesFromWorldAxes = true}, {Guide, RightAngle}},
			{DrawingSettings{.guideCrossings = true}, {}},
			{DrawingSettings{.guidesFromWorldAxes = true, .guideCrossings = true}, {Guide, RightAngle, Crossing}},
			{DrawingSettings{.commonLengths = true}, {CommonLength}},
			{DrawingSettings{.guideDistances = true}, {}},
			{DrawingSettings{.angleReadout = true}, {RightAngle, AngleArc}},
			{DrawingSettings{.roundLengths = true}, {}},
			{everyHelper(), {Guide, RightAngle, Crossing, CommonLength, AngleArc}},
		};
		for (const ToolKind tool : {ToolKind::Foundation, ToolKind::Wall, ToolKind::FoundationEdit}) {
			for (std::size_t index = 0; index < cases.size(); ++index) {
				EXPECT_EQ(glyphs(tool, cases[index].first), cases[index].second) << "case " << index << ", tool " << static_cast<int>(tool);
			}
		}
	}

	TEST(KeyGuideContentTest, LegendRowsReadAsTheDesignSpecsLines) {
		std::vector<std::string> lines;
		for (const KeyGuideLegendRow& row : keyGuideContent(ToolKind::Foundation, everyHelper()).legendRows) {
			lines.push_back(row.text);
		}
		EXPECT_EQ(
			lines,
			(std::vector<std::string>{
				"Lined up with something",
				"A square corner",
				"Two guides cross",
				"= and ticks: same length as an edge already built",
				"Arc and degrees: the corner's angle",
			})
		);
	}

	TEST(KeyGuideContentTest, TheRectangleAndOpeningToolsHaveNoLegend) {
		EXPECT_TRUE(keyGuideContent(ToolKind::FoundationRectangle, everyHelper()).legendRows.empty());
		EXPECT_TRUE(keyGuideContent(ToolKind::Opening, everyHelper()).legendRows.empty());
	}

	TEST(KeyGuideToggleTest, TheQuestionMarkCharacterAndAPressOfHToggleIt) {
		using Type = foundation::InputEvent::Type;
		EXPECT_TRUE(togglesKeyGuide({.type = Type::Char, .codepoint = U'?'}));
		EXPECT_TRUE(togglesKeyGuide({.type = Type::KeyDown, .key = foundation::Key::H}));
		EXPECT_TRUE(togglesKeyGuide({.type = Type::KeyDown, .mods = foundation::ModShift, .key = foundation::Key::H}));
	}

	TEST(KeyGuideToggleTest, NothingElseTogglesIt) {
		using Type = foundation::InputEvent::Type;
		EXPECT_FALSE(togglesKeyGuide({.type = Type::Char, .codepoint = U'h'}));
		EXPECT_FALSE(togglesKeyGuide({.type = Type::Char, .codepoint = U'/'}));
		EXPECT_FALSE(togglesKeyGuide({.type = Type::KeyDown, .repeat = true, .key = foundation::Key::H}));
		EXPECT_FALSE(togglesKeyGuide({.type = Type::KeyUp, .key = foundation::Key::H}));
		EXPECT_FALSE(togglesKeyGuide({.type = Type::KeyDown, .key = foundation::Key::Slash}));
	}

	// Every glyph's marks lie in its box (a stroke's points, a dot's disc, an arc's ring).
	TEST(LegendGlyphTest, EveryGlyphStaysInItsBox) {
		const assets::PreviewStyle style;
		const auto				   inside = [](Foundation::Vec2 point, float reach) {
			  return point.x - reach >= 0.0F && point.y - reach >= 0.0F && point.x + reach <= kLegendGlyphWidth && point.y + reach <= kLegendGlyphHeight;
		};
		using enum LegendGlyph;
		for (const LegendGlyph glyph : {Guide, RightAngle, Crossing, CommonLength, AngleArc}) {
			const DrawingFeedbackPlan plan = planLegendGlyph(glyph, style, {1.0F, 1.0F, 1.0F, 1.0F});
			EXPECT_FALSE(plan.strokes.empty()) << static_cast<int>(glyph);
			for (const FeedbackStroke& stroke : plan.strokes) {
				for (std::size_t index = 0; index < stroke.pointCount; ++index) {
					EXPECT_TRUE(inside(stroke.points[index], 0.0F)) << static_cast<int>(glyph) << " point " << index;
				}
			}
			for (const FeedbackDot& dot : plan.dots) {
				EXPECT_TRUE(inside(dot.center, dot.radius)) << static_cast<int>(glyph);
			}
			for (const FeedbackArc& arc : plan.arcs) {
				EXPECT_TRUE(inside(arc.center, 0.0F)) << static_cast<int>(glyph);
				EXPECT_LE(arc.radius, kLegendGlyphHeight) << static_cast<int>(glyph);
			}
		}
	}

	// The glyphs take the look keys the preview's marks take: the guide's dash, width and opacity, the marker opacity,
	// and the common-length color by source.
	TEST(LegendGlyphTest, GlyphsFollowThePreviewsLookKeys) {
		assets::PreviewStyle style;
		style.guideDashPx = 2.0F;
		style.guideGapPx = 3.0F;
		style.guideWidth = 1.25F;
		style.guideOpacity = 0.5F;
		style.markerOpacity = 0.8F;
		style.guideColorMode = assets::GuideColorMode::BySource;

		const DrawingFeedbackPlan guide = planLegendGlyph(LegendGlyph::Guide, style, {});
		ASSERT_EQ(guide.strokes.size(), 1U);
		ASSERT_TRUE(guide.strokes[0].dash.has_value());
		EXPECT_EQ(guide.strokes[0].dash->dash, 2.0F);
		EXPECT_EQ(guide.strokes[0].dash->gap, 3.0F);
		EXPECT_EQ(guide.strokes[0].width, 1.25F);
		EXPECT_EQ(guide.strokes[0].color.a, 0.5F);
		EXPECT_EQ(guide.strokes[0].color.r, style.guideColorShape.r);

		const DrawingFeedbackPlan crossing = planLegendGlyph(LegendGlyph::Crossing, style, {});
		ASSERT_EQ(crossing.dots.size(), 1U);
		EXPECT_EQ(crossing.dots[0].radius, style.guideCrossingDotPx);
		EXPECT_EQ(crossing.dots[0].fill.a, 0.8F);

		const DrawingFeedbackPlan length = planLegendGlyph(LegendGlyph::CommonLength, style, {});
		ASSERT_FALSE(length.strokes.empty());
		EXPECT_EQ(length.strokes.back().color.r, style.guideColorLength.r);
		EXPECT_EQ(length.strokes.back().color.a, 0.8F);
	}

} // namespace world_sim
