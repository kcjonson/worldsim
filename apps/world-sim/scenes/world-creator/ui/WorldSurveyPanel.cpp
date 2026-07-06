#include "WorldSurveyPanel.h"

#include <worldgen/data/Biome.h>
#include <worldgen/data/WorldData.h>

#include <components/badge/Badge.h>
#include <components/icon/Icon.h>
#include <components/panel/Panel.h>
#include <components/progress/ProgressBar.h>
#include <components/stat/Stat.h>
#include <primitives/Primitives.h>
#include <shapes/Shapes.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace world_sim {

namespace {

	bool hasField(const worldgen::GeneratedWorld& w, worldgen::WorldField f) {
		return (w.validFields & static_cast<uint32_t>(f)) != 0;
	}

	// Climate band per biome, for the distribution meters. Water biomes are
	// excluded (the distribution is over land).
	enum class Band : int { Tropical, Temperate, Desert, Boreal, Polar, None };

	Band bandFor(worldgen::Biome b) {
		using worldgen::Biome;
		switch (b) {
			case Biome::TropicalRainforest:
			case Biome::TropicalSeasonalForest:
			case Biome::TropicalSavanna:
			case Biome::TropicalWetland: return Band::Tropical;
			case Biome::TemperateDeciduousForest:
			case Biome::TemperateRainforest:
			case Biome::TemperateGrassland:
			case Biome::TemperateWetland:
			case Biome::Beach: return Band::Temperate;
			case Biome::HotDesert:
			case Biome::ColdDesert:
			case Biome::SemiDesert:
			case Biome::XericShrubland: return Band::Desert;
			case Biome::BorealForest:
			case Biome::MontaneForest:
			case Biome::AlpineGrassland: return Band::Boreal;
			case Biome::ArcticTundra:
			case Biome::AlpineTundra:
			case Biome::PolarDesert: return Band::Polar;
			case Biome::Ocean:
			case Biome::Lake:
			case Biome::Count: break;
		}
		return Band::None;
	}

	UI::Tone badgeTone(int tone) {
		if (tone == 0) return UI::Tone::Ok;
		if (tone == 1) return UI::Tone::Warn;
		return UI::Tone::Crit;
	}

} // namespace

void WorldSurveyPanel::setWorld(std::shared_ptr<const worldgen::GeneratedWorld> newWorld) {
	world = std::move(newWorld);
	stats.clear();
	climate.clear();
	badges.clear();
	habitability = -1.0F;
	if (!world) {
		return;
	}

	const worldgen::WorldSummary& summary = world->summary;

	if (hasField(*world, worldgen::WorldField::Elevation)) {
		stats.push_back({"Land", std::format("{:.0f}", summary.landFraction * 100.0F), "%", false});
		stats.push_back({"Water", std::format("{:.0f}", (1.0F - summary.landFraction) * 100.0F), "%", true});

		const float waterFraction = 1.0F - summary.landFraction;
		if (waterFraction < 0.15F) {
			badges.push_back({"Water: Scarce", 1});
		} else if (waterFraction > 0.92F) {
			badges.push_back({"Water: Extreme", 1});
		} else {
			badges.push_back({"Water: Good", 0});
		}
	}
	if (hasField(*world, worldgen::WorldField::TemperatureMean)) {
		stats.push_back({"Mean Temp", std::format("{:.1f}", summary.meanTemperatureC), " C", false});
	}
	if (hasField(*world, worldgen::WorldField::FlowAccum)) {
		stats.push_back({"River Tiles", std::format("{}", summary.riverTileCount), "", summary.riverTileCount > 0});
	}

	if (hasField(*world, worldgen::WorldField::Biome)) {
		habitability = std::clamp(summary.habitability, 0.0F, 1.0F);
		if (habitability >= 0.55F) {
			badges.push_back({"Habitability: Good", 0});
		} else if (habitability >= 0.3F) {
			badges.push_back({"Habitability: Fair", 1});
		} else {
			badges.push_back({"Habitability: Poor", 2});
		}

		std::array<uint64_t, 5> counts{};
		uint64_t				landTotal = 0;
		for (size_t i = 0; i < summary.biomeHistogram.size(); ++i) {
			const Band band = bandFor(static_cast<worldgen::Biome>(i));
			if (band == Band::None) continue;
			counts[static_cast<size_t>(band)] += summary.biomeHistogram[i];
			landTotal += summary.biomeHistogram[i];
		}
		if (landTotal > 0) {
			constexpr std::array<const char*, 5> kBandNames{"Tropical", "Temperate", "Desert", "Boreal", "Polar"};
			for (size_t i = 0; i < counts.size(); ++i) {
				climate.push_back({kBandNames[i], static_cast<float>(counts[i]) / static_cast<float>(landTotal)});
			}
		}
	}
}

void WorldSurveyPanel::render(const Foundation::Rect& bounds) const {
	using Renderer::Primitives::drawRect;
	using Renderer::Primitives::drawText;

	UI::Panel panel({.position = bounds.position(),
					 .size = {bounds.width, bounds.height},
					 .title = "World Survey",
					 .kicker = world ? "Complete" : "Pending",
					 .accent = UI::PanelAccent::Accent});
	panel.render();
	const Foundation::Rect body = panel.bodyBounds();

	if (!world) {
		UI::Icon globe({.position = {body.x + (body.width - 32.0F) * 0.5F, body.y + body.height * 0.4F - 48.0F},
						.size = 32.0F,
						.glyph = "globe",
						.tint = UI::text_faint});
		globe.render();
		UI::Text hint(UI::Text::Args{
			.position = {body.x + (body.width - 220.0F) * 0.5F, body.y + body.height * 0.4F},
			.width = 220.0F,
			.text = "Survey data resolves as the world is generated.",
			.style = {.color = UI::text_faint,
					  .fontSize = UI::fs_sm,
					  .hAlign = Foundation::HorizontalAlign::Center,
					  .wordWrap = true},
		});
		hint.render();
		return;
	}

	float y = body.y + UI::space_2;

	// Stat grid, two columns.
	const float colW = body.width * 0.5F;
	for (size_t i = 0; i < stats.size(); ++i) {
		const StatEntry& s = stats[i];
		const float		 x = body.x + (i % 2 == 0 ? 0.0F : colW);
		UI::Stat({.position = {x, y},
				  .label = s.label,
				  .value = s.value,
				  .unit = s.unit,
				  .tone = s.dataTone ? UI::Tone::Data : UI::Tone::Default,
				  .size = UI::Size::Sm})
			.render();
		if (i % 2 == 1) y += 46.0F;
	}
	if (stats.size() % 2 == 1) y += 46.0F;
	y += UI::space_2;

	// Habitability: inset row with five accent pips.
	if (habitability >= 0.0F) {
		const Foundation::Rect row{body.x, y, body.width, 36.0F};
		drawRect({.bounds = row,
				  .style = {.fill = UI::bg_inset,
							.border = Foundation::BorderStyle{
								.color = UI::line_hairline, .width = UI::bw, .cornerRadius = UI::r_sm,
								.position = Foundation::BorderPosition::Inside}}});
		drawText({.text = "HABITABILITY",
				  .position = {row.x + UI::space_3, row.y},
				  .scale = UI::fs_2xs / 16.0F,
				  .color = UI::text_dim,
				  .font = UI::fontMono,
				  .vAlign = Foundation::VerticalAlign::Middle,
				  .boxHeight = row.height,
				  .letterSpacing = UI::fs_2xs * UI::ls_wider});
		const int filled = static_cast<int>(std::lround(habitability * 5.0F));
		for (int i = 0; i < 5; ++i) {
			UI::Icon star({.position = {row.x + row.width - UI::space_3 - (5.0F - static_cast<float>(i)) * 19.0F,
										row.y + (row.height - 16.0F) * 0.5F},
						   .size = 16.0F,
						   .glyph = "star",
						   .tint = i < filled ? UI::accent : UI::text_faint});
			star.render();
		}
		y += 36.0F + UI::space_4;
	}

	// Climate distribution meters.
	if (!climate.empty()) {
		drawText({.text = "CLIMATE DISTRIBUTION",
				  .position = {body.x, y},
				  .scale = UI::fs_2xs / 16.0F,
				  .color = UI::text_dim,
				  .font = UI::fontMono,
				  .vAlign = Foundation::VerticalAlign::Top,
				  .letterSpacing = UI::fs_2xs * UI::ls_wider});
		y += 18.0F;
		for (const MeterEntry& m : climate) {
			UI::ProgressBar bar({.position = {body.x, y},
								 .width = body.width,
								 .value = m.fraction,
								 .tone = UI::Tone::Data,
								 .label = m.label,
								 .valueText = std::format("{:.0f}%", m.fraction * 100.0F),
								 .size = UI::Size::Sm});
			bar.render();
			y += 30.0F;
		}
		y += UI::space_2;
	}

	// Hazard/report badges.
	float badgeX = body.x;
	for (const BadgeEntry& b : badges) {
		const float w = UI::Badge::MeasureWidth(b.label);
		if (badgeX + w > body.x + body.width && badgeX > body.x) {
			badgeX = body.x;
			y += 26.0F;
		}
		UI::Badge({.position = {badgeX, y}, .label = b.label, .tone = badgeTone(b.tone)}).render();
		badgeX += w + UI::space_2;
	}
}

float WorldSurveyPanel::contentHeight(float width) const {
	// Probe the panel chrome (header + body padding are fixed, independent of
	// height) so the frame can be sized to the content it will draw.
	UI::Panel probe({.position = {0.0F, 0.0F},
					 .size = {width, 1000.0F},
					 .title = "World Survey",
					 .kicker = world ? "Complete" : "Pending",
					 .accent = UI::PanelAccent::Accent});
	const Foundation::Rect body = probe.bodyBounds();
	const float			   topOffset = body.y;
	const float			   botPad = 1000.0F - (body.y + body.height);

	if (!world) {
		return topOffset + 120.0F + botPad; // compact empty/pending frame
	}

	// Mirror render()'s vertical accumulation from body.y.
	float extent = UI::space_2;
	const int rows = (static_cast<int>(stats.size()) + 1) / 2;
	extent += static_cast<float>(rows) * 46.0F + UI::space_2;
	if (habitability >= 0.0F) extent += 36.0F + UI::space_4;
	if (!climate.empty()) extent += 18.0F + static_cast<float>(climate.size()) * 30.0F + UI::space_2;
	if (!badges.empty()) extent += 26.0F;
	return topOffset + extent + botPad;
}

} // namespace world_sim
