// The key guide's content against the design spec's tables (UI & Selection > Key guide), the keys that collapse it,
// and its legend glyphs against the preview's look.

#include "DrawingKeyGuide.h"

#include "scenes/game/world/construction/DrawingSystem.h"

#include <assets/ConstructionRegistry.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <numbers>
#include <set>
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

		// A tool's rows as the player reads them, "Keys: Action." each.
		std::string rowsRead(ToolKind tool) {
			std::string read;
			for (const KeyGuideKeyRow& row : keyGuideContent(tool, DrawingSettings{}).keyRows) {
				read += (read.empty() ? "" : " ") + phrase(row.keys) + ": " + row.action + ".";
			}
			return read;
		}

		constexpr std::array
			kEveryTool{ToolKind::FoundationRectangle, ToolKind::Foundation, ToolKind::Wall, ToolKind::Opening, ToolKind::FoundationEdit};

		// The words among the keys, which are dim text and never chips.
		const std::set<std::string> kDimWords{"or", "again", "point 1", "a wall", "a foundation edge", "hides this", "/"};

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
			rowsRead(ToolKind::FoundationRectangle),
			"Click: Place two opposite corners (or drag). Backspace / Esc / Right-click: Drop the first corner. Esc again: Leave "
			"the tool. Alt: Hold to place the raw cursor (no grid, no snapping)."
		);
		EXPECT_EQ(
			rowsRead(ToolKind::Foundation),
			"Click: Place a point. Click point 1: Close the shape. Backspace: Undo a point. Esc / Right-click: Cancel the shape. "
			"Esc again: Leave the tool. Alt: Hold to turn snapping off."
		);
		EXPECT_EQ(
			rowsRead(ToolKind::Wall),
			"Click: Place a point. Click point 1: Close the loop. Double-click / Enter / Right-click: End the wall. Backspace: "
			"Undo a point. Esc: End the wall. Esc again: Leave the tool. Ctrl+click a foundation edge: Wall the whole edge. Alt: "
			"Hold to turn snapping off."
		);
		EXPECT_EQ(rowsRead(ToolKind::Opening), "Click a wall: Place it. Esc / Right-click: Leave the tool.");
		EXPECT_EQ(
			rowsRead(ToolKind::FoundationEdit),
			"Click: Place a point. Click point 1: Close the shape and apply it (the tool stays on for the next). Backspace: "
			"Undo a point. Esc / Right-click: Cancel the shape. Esc again: Leave the tool. Alt: Hold to turn snapping off."
		);
	}

	// Only the keys themselves are chips, in every tool's rows; the words among them are not.
	TEST(KeyGuideContentTest, OnlyKeysAreChips) {
		for (const ToolKind tool : kEveryTool) {
			for (const KeyGuideKeyRow& row : keyGuideContent(tool, DrawingSettings{}).keyRows) {
				ASSERT_FALSE(row.keys.empty()) << row.action;
				EXPECT_TRUE(row.keys.front().chip) << row.action;
				for (const KeyGuidePart& part : row.keys) {
					EXPECT_EQ(part.chip, !kDimWords.contains(part.text)) << '"' << part.text << "\" in \"" << row.action << '"';
				}
			}
		}
	}

	TEST(KeyGuideContentTest, TheFootLineSaysHowToHideIt) {
		const KeyGuideContent content = keyGuideContent(ToolKind::Foundation, DrawingSettings{});
		ASSERT_EQ(content.foot.size(), 4U);
		EXPECT_EQ(
			content.foot,
			(std::vector<KeyGuidePart>{
				{.text = "?", .chip = true},
				{.text = "or", .chip = false},
				{.text = "H", .chip = true},
				{.text = "hides this", .chip = false},
			})
		);
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
		EXPECT_FALSE(togglesKeyGuide({.type = Type::Char, .codepoint = U'H'}));
		EXPECT_FALSE(togglesKeyGuide({.type = Type::Char, .codepoint = U'/'}));
		EXPECT_FALSE(togglesKeyGuide({.type = Type::KeyDown, .repeat = true, .key = foundation::Key::H}));
		EXPECT_FALSE(togglesKeyGuide({.type = Type::KeyUp, .key = foundation::Key::H}));
		EXPECT_FALSE(togglesKeyGuide({.type = Type::KeyDown, .key = foundation::Key::Slash}));
		EXPECT_FALSE(togglesKeyGuide({.type = Type::KeyDown, .key = foundation::Key::J}));
	}

	// The game takes the toggle only while a drawing tool is active, no modal dialog is open, and Show key guide is on.
	TEST(KeyGuideToggleTest, TheGameTakesItOnlyWhileAToolIsOnNoDialogIsOpenAndTheSettingIs) {
		using Type = foundation::InputEvent::Type;
		const foundation::InputEvent							  question{.type = Type::Char, .codepoint = U'?'};
		const foundation::InputEvent							  pressH{.type = Type::KeyDown, .key = foundation::Key::H};
		const KeyGuideToggle::State								  on{.toolActive = true, .modalDialogOpen = false, .showKeyGuide = true};
		const std::vector<std::pair<KeyGuideToggle::State, bool>> cases{
			{on, true},
			{{.toolActive = false, .modalDialogOpen = false, .showKeyGuide = true}, false},
			{{.toolActive = true, .modalDialogOpen = true, .showKeyGuide = true}, false},
			{{.toolActive = true, .modalDialogOpen = false, .showKeyGuide = false}, false},
		};
		for (std::size_t index = 0; index < cases.size(); ++index) {
			KeyGuideToggle toggle;
			EXPECT_EQ(toggle.takes(question, cases[index].first), cases[index].second) << "?, case " << index;
			EXPECT_EQ(toggle.takes(pressH, cases[index].first), cases[index].second) << "H, case " << index;
		}
		KeyGuideToggle toggle;
		EXPECT_FALSE(toggle.takes({.type = Type::Char, .codepoint = U'h'}, on));
	}

	// A held `?` is a key-down, its `?`, then repeated key-downs each with a `?`: it toggles once, and a fresh press after
	// the release toggles again.
	TEST(KeyGuideToggleTest, AHeldQuestionMarkTogglesOnce) {
		using Type = foundation::InputEvent::Type;
		const KeyGuideToggle::State	 on{.toolActive = true, .modalDialogOpen = false, .showKeyGuide = true};
		const foundation::InputEvent press{.type = Type::KeyDown, .mods = foundation::ModShift, .key = foundation::Key::Slash};
		const foundation::InputEvent repeat{
			.type = Type::KeyDown, .mods = foundation::ModShift, .repeat = true, .key = foundation::Key::Slash
		};
		const foundation::InputEvent release{.type = Type::KeyUp, .mods = foundation::ModShift, .key = foundation::Key::Slash};
		const foundation::InputEvent question{.type = Type::Char, .codepoint = U'?'};
		KeyGuideToggle				 toggle;
		int							 toggles = 0;
		const auto					 feed = [&](const foundation::InputEvent& event) {
			  if (toggle.takes(event, on)) {
				  ++toggles;
			  }
		};
		feed(press);
		feed(question);
		for (int held = 0; held < 5; ++held) {
			feed(repeat);
			feed(question);
		}
		feed(release);
		EXPECT_EQ(toggles, 1);
		feed(press);
		feed(question);
		EXPECT_EQ(toggles, 2);
	}

	// Every glyph's marks lie in its box (a stroke's points, a dot's disc, the band an arc draws over its sweep), at the
	// default look and with the marker and the crossing dot set far larger than the box.
	TEST(LegendGlyphTest, EveryGlyphStaysInItsBox) {
		assets::PreviewStyle large;
		large.rightAngleMarkerPx = 40.0F;
		large.guideCrossingDotPx = 30.0F;
		const auto inside = [](Foundation::Vec2 point, float reach) {
			return point.x - reach >= 0.0F && point.y - reach >= 0.0F && point.x + reach <= kLegendGlyphWidth &&
				   point.y + reach <= kLegendGlyphHeight;
		};
		constexpr int kArcSamples = 32;
		using enum LegendGlyph;
		for (const assets::PreviewStyle& style : {assets::PreviewStyle{}, large}) {
			for (const LegendGlyph glyph : {Guide, RightAngle, Crossing, CommonLength, AngleArc}) {
				const DrawingFeedbackPlan plan = planLegendGlyph(glyph, style, {1.0F, 1.0F, 1.0F, 1.0F});
				EXPECT_FALSE(plan.strokes.empty()) << static_cast<int>(glyph);
				for (const FeedbackStroke& stroke : plan.strokes) {
					for (std::size_t index = 0; index < stroke.pointCount; ++index) {
						EXPECT_TRUE(inside(stroke.points[index], 0.0F)) << static_cast<int>(glyph) << " point " << index;
					}
				}
				for (const FeedbackDot& dot : plan.dots) {
					EXPECT_TRUE(inside(dot.center, dot.radius)) << static_cast<int>(glyph) << " dot radius " << dot.radius;
				}
				for (const FeedbackArc& arc : plan.arcs) {
					for (int sample = 0; sample <= kArcSamples; ++sample) {
						const float angle = arc.startRadians + (arc.sweepRadians * static_cast<float>(sample) / kArcSamples);
						for (const float radius : {arc.radius - (arc.width * 0.5F), arc.radius + (arc.width * 0.5F)}) {
							const Foundation::Vec2 point{
								arc.center.x + (radius * std::cos(angle)), arc.center.y + (radius * std::sin(angle))
							};
							EXPECT_TRUE(inside(point, 0.0F))
								<< static_cast<int>(glyph) << " arc at " << angle << " radians, radius " << radius;
						}
					}
				}
			}
		}
	}

	// The right-angle marker's legs keep to the box: the look's length while it fits, the cap beyond it.
	TEST(LegendGlyphTest, TheRightAngleMarkersLegsAreHeldToTheBox) {
		const auto legOf = [](float markerPx) {
			assets::PreviewStyle style;
			style.rightAngleMarkerPx = markerPx;
			const DrawingFeedbackPlan plan = planLegendGlyph(LegendGlyph::RightAngle, style, {});
			const FeedbackStroke&	  marker = plan.strokes.back();
			EXPECT_EQ(marker.pointCount, 3U);
			// Its first point is the corner plus the upright leg, its last the corner plus the level one.
			return std::pair{marker.points[2].x - marker.points[0].x, marker.points[2].y - marker.points[0].y};
		};
		const auto [smallAcross, smallUp] = legOf(6.0F);
		EXPECT_FLOAT_EQ(smallAcross, 6.0F);
		EXPECT_FLOAT_EQ(smallUp, 6.0F);
		const auto [largeAcross, largeUp] = legOf(40.0F);
		EXPECT_LT(largeAcross, kLegendGlyphHeight);
		EXPECT_LT(largeUp, kLegendGlyphHeight);
		EXPECT_FLOAT_EQ(largeAcross, largeUp);
	}

	// The solid edges take the geometry colour the panel passes in, and the marks the marker look.
	TEST(LegendGlyphTest, EdgesTakeTheGeometryColourAndMarksTheMarkerLook) {
		assets::PreviewStyle style;
		style.markerOpacity = 0.7F;
		const Foundation::Color geometry{0.25F, 0.5F, 0.75F, 1.0F};
		const Foundation::Color marker{style.guideColor.r, style.guideColor.g, style.guideColor.b, 0.7F};
		const auto				same = [](Foundation::Color left, Foundation::Color right) {
			 return left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;
		};

		const DrawingFeedbackPlan square = planLegendGlyph(LegendGlyph::RightAngle, style, geometry);
		ASSERT_EQ(square.strokes.size(), 2U);
		EXPECT_TRUE(same(square.strokes[0].color, geometry));
		EXPECT_TRUE(same(square.strokes[1].color, marker));

		const DrawingFeedbackPlan angle = planLegendGlyph(LegendGlyph::AngleArc, style, geometry);
		ASSERT_EQ(angle.strokes.size(), 1U);
		ASSERT_EQ(angle.arcs.size(), 1U);
		EXPECT_TRUE(same(angle.strokes[0].color, geometry));
		EXPECT_TRUE(same(angle.arcs[0].color, marker));

		const DrawingFeedbackPlan length = planLegendGlyph(LegendGlyph::CommonLength, style, geometry);
		ASSERT_GE(length.strokes.size(), 2U);
		EXPECT_TRUE(same(length.strokes[1].color, geometry));

		const DrawingFeedbackPlan crossing = planLegendGlyph(LegendGlyph::Crossing, style, geometry);
		ASSERT_EQ(crossing.dots.size(), 1U);
		EXPECT_TRUE(same(crossing.dots[0].fill, marker));
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
