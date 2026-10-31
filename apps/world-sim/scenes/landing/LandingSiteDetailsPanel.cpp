#include "scenes/landing/LandingSiteDetailsPanel.h"

#include "scenes/shared/SalalUi.h"

#include <theme/Panel.h>
#include <ui/components/Badge.h>
#include <ui/components/Divider.h>
#include <ui/components/Stat.h>
#include <ui/shapes/Icon.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>

namespace world_sim {

	namespace {
		namespace family = ui::tokens::font::family;
	}

	void LandingSiteDetailsPanel::mount(ui::Ref<ui::Component> host, const ui::Theme& themeRef) {
		theme = &themeRef;
		ui::Ref<::theme::Panel> frame = host.addChild<::theme::Panel>({
			.id = "landing_zone",
			.size = {ui::percent(100.0F), 0.0F},
			.layout = {.flexGrow = 1.0F},
			.title = "Landing Zone",
			.kicker = "Site Analysis",
		});
		if (const ::theme::Panel* shown = frame.get()) {
			body = shown->body();
		}
	}

	void LandingSiteDetailsPanel::show(const LandingSiteDetails& details) {
		ui::Component* shownBody = body.get();
		if (shownBody == nullptr) {
			return;
		}
		if (content.get() != nullptr) {
			shownBody->removeChild(content);
		}
		const float space2 = theme->get(tokens::spacing::space2);
		content = body.addChild<ui::Component>({
			.id = "landing_details",
			.size = {ui::percent(100.0F), 0.0F},
			.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = space2, .flexGrow = 1.0F},
			.overflow = ui::Overflow::Scroll,
		});
		const ui::Style label = world_sim::text(
			*theme,
			{.family = family::mono,
			 .size = tokens::fontSize::fs2xs,
			 .color = tokens::color::textDim,
			 .tracking = tokens::letterSpacing::lsWider,
			 .transform = Foundation::TextTransform::Uppercase}
		);

		// Coordinates: crosshair + teal mono readout.
		ui::Ref<ui::Component> coords =
			content.addChild<ui::Component>({.layout = {.display = ui::Display::Flex, .gap = 6.0F, .alignItems = ui::AlignItems::Center}});
		coords.addChild<ui::Icon>({.size = {14.0F, 14.0F}, .style = {.color = theme->get(tokens::color::dataBright)}, .name = "crosshair"});
		coords.addChild<ui::Text>({
			.style = world_sim::text(*theme, {.family = family::mono, .size = tokens::fontSize::fsSm, .color = tokens::color::dataBright}),
			.text = details.coords,
		});

		// Biome-name header.
		if (!details.biomeName.empty()) {
			content.addChild<ui::Text>({
				.style = world_sim::text(
					*theme, {.family = family::display, .size = tokens::fontSize::fsXl, .color = tokens::color::textBright}
				),
				.text = details.biomeName,
			});
		}

		if (details.recommended) {
			content.addChild<ui::Badge>({.label = "Recommended", .tone = ui::Tone::Positive, .dot = true});
		}

		// Stat row: temp range / rainfall.
		if (!details.tempRange.empty() || !details.rainfall.empty()) {
			ui::Ref<ui::Component> row =
				content.addChild<ui::Component>({.size = {ui::percent(100.0F), ui::kAuto}, .layout = {.display = ui::Display::Flex}});
			if (!details.tempRange.empty()) {
				row.addChild<ui::Stat>(
					{.size = {ui::percent(55.0F), ui::kAuto},
					 .label = "Temp Range",
					 .value = details.tempRange,
					 .controlSize = ui::ControlSize::Sm}
				);
			}
			if (!details.rainfall.empty()) {
				row.addChild<ui::Stat>(
					{.label = "Rainfall", .value = details.rainfall, .tone = ui::Tone::Info, .controlSize = ui::ControlSize::Sm}
				);
			}
		}

		// Difficulty inset with skull pips.
		if (details.difficulty > 0) {
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
			row.addChild<ui::Text>({.style = label, .text = "Difficulty"});
			ui::Ref<ui::Component> pips = row.addChild<ui::Component>({.layout = {.display = ui::Display::Flex, .gap = 3.0F}});
			for (int i = 0; i < 5; ++i) {
				pips.addChild<ui::Icon>({
					.size = {15.0F, 15.0F},
					.style = {.color = theme->get(i < details.difficulty ? tokens::color::statusWarn : tokens::color::textFaint)},
					.name = "skull",
				});
			}
		}

		// Field report: the water verdict headline plus the survey sections.
		content.addChild<ui::Divider>({.size = {ui::percent(100.0F), ui::kAuto}, .label = "Field Report"});
		ui::Style verdict = world_sim::text(*theme, {.size = tokens::fontSize::fsSm});
		verdict.color = details.verdictColor;
		content.addChild<ui::Text>({.size = {ui::percent(100.0F), ui::kAuto}, .style = verdict, .text = details.verdict, .wrap = true});

		const ui::Style rowLabel = world_sim::text(*theme, {.size = tokens::fontSize::fsSm, .color = tokens::color::textDim});
		for (const auto& section : details.sections) {
			content.addChild<ui::Text>({.margin = {space2, 0.0F, 0.0F, 0.0F}, .style = label, .text = section.header});
			for (const auto& row : section.rows) {
				ui::Ref<ui::Component> line = content.addChild<ui::Component>({
					.size = {ui::percent(100.0F), ui::kAuto},
					.layout = {.display = ui::Display::Flex, .justifyContent = ui::JustifyContent::SpaceBetween},
				});
				line.addChild<ui::Text>({.style = rowLabel, .text = row.label});
				ui::Style value = world_sim::text(*theme, {.family = family::mono, .size = tokens::fontSize::fsSm});
				value.color = row.accent;
				line.addChild<ui::Text>({.style = value, .text = row.value});
			}
		}
	}

} // namespace world_sim
