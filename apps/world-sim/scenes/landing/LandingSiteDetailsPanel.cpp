#include "scenes/landing/LandingSiteDetailsPanel.h"

#include <components/badge/Badge.h>
#include <components/divider/Divider.h>
#include <components/icon/Icon.h>
#include <components/panel/Panel.h>
#include <components/stat/Stat.h>
#include <primitives/Primitives.h>
#include <shapes/Shapes.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>

namespace world_sim {

void LandingSiteDetailsPanel::render(
		const LandingSiteDetails& details, const Foundation::Rect& bounds) const {
	using Renderer::Primitives::drawRect;
	using Renderer::Primitives::drawText;

	UI::Panel panel({.position = bounds.position(),
					 .size = {bounds.width, bounds.height},
					 .title = "Landing Zone",
					 .kicker = "Site Analysis",
					 .accent = UI::PanelAccent::Accent});
	panel.render();
	const Foundation::Rect body = panel.bodyBounds();
	float				   y = body.y;

	// Coordinates: crosshair + teal mono readout.
	UI::Icon({.position = {body.x, y}, .size = 14.0F, .glyph = "crosshair", .tint = UI::data_bright}).render();
	drawText({.text = details.coords,
			  .position = {body.x + 20.0F, y},
			  .scale = UI::fs_sm / 16.0F,
			  .color = UI::data_bright,
			  .font = UI::fontMono,
			  .vAlign = Foundation::VerticalAlign::Top});
	y += 24.0F;

	// Biome-name header.
	if (!details.biomeName.empty()) {
		drawText({.text = details.biomeName,
				  .position = {body.x, y},
				  .scale = UI::fs_xl / 16.0F,
				  .color = UI::text_bright,
				  .font = UI::fontDisplay,
				  .vAlign = Foundation::VerticalAlign::Top});
		y += 30.0F;
	}

	if (details.recommended) {
		UI::Badge({.position = {body.x, y}, .label = "Recommended", .tone = UI::Tone::Ok, .dot = true}).render();
		y += 26.0F;
	}
	y += UI::space_1;

	// Stat row: temp range / rainfall.
	if (!details.tempRange.empty() || !details.rainfall.empty()) {
		if (!details.tempRange.empty()) {
			UI::Stat({.position = {body.x, y}, .label = "Temp Range", .value = details.tempRange, .size = UI::Size::Sm})
				.render();
		}
		if (!details.rainfall.empty()) {
			UI::Stat({.position = {body.x + body.width * 0.55F, y},
					  .label = "Rainfall",
					  .value = details.rainfall,
					  .tone = UI::Tone::Data,
					  .size = UI::Size::Sm})
				.render();
		}
		y += 46.0F;
	}

	// Difficulty inset with skull pips.
	if (details.difficulty > 0) {
		const Foundation::Rect row{body.x, y, body.width, 36.0F};
		drawRect({.bounds = row,
				  .style = {.fill = UI::bg_inset,
							.border = Foundation::BorderStyle{
								.color = UI::line_hairline, .width = UI::bw, .cornerRadius = UI::r_sm,
								.position = Foundation::BorderPosition::Inside}}});
		drawText({.text = "DIFFICULTY",
				  .position = {row.x + UI::space_3, row.y},
				  .scale = UI::fs_2xs / 16.0F,
				  .color = UI::text_dim,
				  .font = UI::fontMono,
				  .vAlign = Foundation::VerticalAlign::Middle,
				  .boxHeight = row.height,
				  .letterSpacing = UI::fs_2xs * UI::ls_wider});
		for (int i = 0; i < 5; ++i) {
			UI::Icon skull({.position = {row.x + row.width - UI::space_3 - (5.0F - static_cast<float>(i)) * 18.0F,
										 row.y + (row.height - 15.0F) * 0.5F},
							.size = 15.0F,
							.glyph = "skull",
							.tint = i < details.difficulty ? UI::status_warn : UI::text_faint});
			skull.render();
		}
		y += 36.0F + UI::space_3;
	}

	// Field report: the water verdict headline plus the survey sections.
	UI::Divider({.position = {body.x, y + 6.0F}, .width = body.width, .label = "Field Report"}).render();
	y += 20.0F;
	{
		UI::Text verdict(UI::Text::Args{
			.position = {body.x, y},
			.width = body.width,
			.text = details.verdict,
			.style = {.color = details.verdictColor, .fontSize = UI::fs_sm, .wordWrap = true},
		});
		verdict.render();
		y += verdict.getHeight() + UI::space_2;
	}

	for (const auto& section : details.sections) {
		drawText({.text = section.header,
				  .position = {body.x, y},
				  .scale = UI::fs_2xs / 16.0F,
				  .color = UI::text_dim,
				  .font = UI::fontMono,
				  .vAlign = Foundation::VerticalAlign::Top,
				  .letterSpacing = UI::fs_2xs * UI::ls_wider,
				  .transform = Foundation::TextTransform::Uppercase});
		y += 18.0F;
		for (const auto& row : section.rows) {
			drawText({.text = row.label,
					  .position = {body.x, y},
					  .scale = UI::fs_sm / 16.0F,
					  .color = UI::text_dim,
					  .font = UI::fontUi,
					  .vAlign = Foundation::VerticalAlign::Top});
			drawText({.text = row.value,
					  .position = {body.x, y},
					  .scale = UI::fs_sm / 16.0F,
					  .color = row.accent,
					  .font = UI::fontMono,
					  .hAlign = Foundation::HorizontalAlign::Right,
					  .vAlign = Foundation::VerticalAlign::Top,
					  .boxWidth = body.width});
			y += 19.0F;
		}
		y += UI::space_2;
	}
}

} // namespace world_sim
