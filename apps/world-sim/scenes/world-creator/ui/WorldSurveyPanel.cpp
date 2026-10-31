#include "WorldSurveyPanel.h"

#include "scenes/shared/SalvageUi.h"

#include <worldgen/data/Biome.h>
#include <worldgen/data/WorldData.h>

#include <theme/Panel.h>
#include <ui/components/Badge.h>
#include <ui/components/ProgressBar.h>
#include <ui/components/Stat.h>
#include <ui/shapes/Icon.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace world_sim {

namespace {

	namespace family = ui::tokens::font::family;

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

	ui::Tone badgeTone(int tone) {
		if (tone == 0)
			return ui::Tone::Positive;
		if (tone == 1)
			return ui::Tone::Warning;
		return ui::Tone::Danger;
	}

} // namespace

void WorldSurveyPanel::mount(ui::Ref<ui::Component> hostRef, const ui::Theme& themeRef) {
	host = hostRef;
	theme = &themeRef;
	rebuild();
}

void WorldSurveyPanel::setWorld(std::shared_ptr<const worldgen::GeneratedWorld> newWorld) {
	world = std::move(newWorld);
	stats.clear();
	climate.clear();
	badges.clear();
	habitability = -1.0F;
	if (world) {
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
				if (band == Band::None)
					continue;
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
	rebuild();
}

void WorldSurveyPanel::rebuild() {
	ui::Component* shownHost = host.get();
	if (shownHost == nullptr || theme == nullptr) {
		return;
	}
	if (panel.get() != nullptr) {
		shownHost->removeChild(panel);
	}
	// Pending, the panel fills the column; complete, it takes its content's height and the Landing
	// Zone pane takes the rest.
	ui::Layout hostLayout = shownHost->getLayout();
	hostLayout.flexGrow = world ? 0.0F : 1.0F;
	shownHost->setLayout(hostLayout);

	ui::Ref<::theme::Panel> frame = host.addChild<::theme::Panel>({
		.id = "world_survey",
		.size = {ui::percent(100.0F), ui::kAuto},
		.layout = {.flexGrow = 1.0F},
		.title = "World Survey",
		.kicker = world ? "Complete" : "Pending",
	});
	panel = frame;
	const ::theme::Panel* shownFrame = frame.get();
	if (shownFrame == nullptr) {
		return;
	}
	ui::Ref<ui::Component> body = shownFrame->body();
	const float			   space2 = theme->get(tokens::spacing::space2);
	const ui::Style		   label = world_sim::text(
		   *theme,
		   {.family = family::mono,
				.size = tokens::fontSize::fs2xs,
				.color = tokens::color::textDim,
				.tracking = tokens::letterSpacing::lsWider,
				.transform = Foundation::TextTransform::Uppercase}
	   );

	if (!world) {
		ui::Ref<ui::Component> empty = body.addChild<ui::Component>({
			.size = {ui::percent(100.0F), 0.0F},
			.layout = {
				.display = ui::Display::Flex,
				.flexDirection = ui::FlexDirection::Column,
				.gap = theme->get(tokens::spacing::space4),
				.alignItems = ui::AlignItems::Center,
				.justifyContent = ui::JustifyContent::Center,
				.flexGrow = 1.0F
			},
		});
		empty.addChild<ui::Icon>({.size = {32.0F, 32.0F}, .style = {.color = theme->get(tokens::color::textFaint)}, .name = "globe"});
		empty.addChild<ui::Text>({
			.size = {220.0F, ui::kAuto},
			.style = world_sim::text(
				*theme, {.size = tokens::fontSize::fsSm, .color = tokens::color::textFaint, .align = Foundation::HorizontalAlign::Center}
			),
			.text = "Survey data resolves as the world is generated.",
			.wrap = true,
		});
		return;
	}

	ui::Ref<ui::Component> content = body.addChild<ui::Component>({
		.size = {ui::percent(100.0F), ui::kAuto},
		.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = space2},
	});

	// Stat grid, two columns.
	ui::Ref<ui::Component> statRow;
	for (size_t i = 0; i < stats.size(); ++i) {
		if (i % 2 == 0) {
			statRow = content.addChild<ui::Component>({.size = {ui::percent(100.0F), ui::kAuto}, .layout = {.display = ui::Display::Flex}});
		}
		const StatEntry& s = stats[i];
		statRow.addChild<ui::Stat>({
			.size = {ui::percent(50.0F), ui::kAuto},
			.label = s.label,
			.value = s.value,
			.unit = s.unit,
			.tone = s.dataTone ? ui::Tone::Info : ui::Tone::Default,
			.controlSize = ui::ControlSize::Sm,
		});
	}

	// Habitability: inset row with five accent pips.
	if (habitability >= 0.0F) {
		ui::Ref<ui::Rectangle> row = content.addChild<ui::Rectangle>({
			.size = {ui::percent(100.0F), 36.0F},
			.layout =
				{.display = ui::Display::Flex,
				 .padding = {0.0F, theme->get(tokens::spacing::space3), 0.0F, theme->get(tokens::spacing::space3)},
				 .alignItems = ui::AlignItems::Center,
				 .justifyContent = ui::JustifyContent::SpaceBetween},
			.style = {
				.backgroundColor = theme->get(tokens::color::bgInset),
				.borderColor = theme->get(tokens::color::lineHairline),
				.borderWidth = theme->get(tokens::border::bw),
				.borderRadius = theme->get(tokens::radius::rSm)
			},
		});
		row.addChild<ui::Text>({.style = label, .text = "Habitability"});
		ui::Ref<ui::Component> pips = row.addChild<ui::Component>({.layout = {.display = ui::Display::Flex, .gap = 3.0F}});
		const int filled = static_cast<int>(std::lround(habitability * 5.0F));
		for (int i = 0; i < 5; ++i) {
			pips.addChild<ui::Icon>({
				.size = {16.0F, 16.0F},
				.style = {.color = theme->get(i < filled ? tokens::color::accent : tokens::color::textFaint)},
				.name = "star",
			});
		}
	}

	// Climate distribution meters.
	if (!climate.empty()) {
		content.addChild<ui::Text>({.margin = {space2, 0.0F, 0.0F, 0.0F}, .style = label, .text = "Climate Distribution"});
		for (const MeterEntry& m : climate) {
			content.addChild<ui::ProgressBar>({
				.size = {ui::percent(100.0F), ui::kAuto},
				.value = m.fraction,
				.tone = ui::Tone::Info,
				.label = m.label,
				.valueText = std::format("{:.0f}%", m.fraction * 100.0F),
				.controlSize = ui::ControlSize::Sm,
			});
		}
	}

	// Hazard/report badges.
	if (!badges.empty()) {
		ui::Ref<ui::Component> row = content.addChild<ui::Component>({
			.margin = {space2, 0.0F, 0.0F, 0.0F},
			.layout = {.display = ui::Display::Flex, .gap = space2},
		});
		for (const BadgeEntry& b : badges) {
			row.addChild<ui::Badge>({.label = b.label, .tone = badgeTone(b.tone)});
		}
	}
}

} // namespace world_sim
