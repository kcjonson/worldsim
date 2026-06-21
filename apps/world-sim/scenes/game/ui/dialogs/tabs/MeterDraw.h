#pragma once

// Shared manual-render helpers for the colonist dossier tabs.
//
// The dossier tabs draw themselves with Renderer::Primitives at absolute coords
// (see BioTabView for the canonical pattern). These helpers keep the Salvage look
// consistent across Health, Tasks, Memory, and the placeholder tabs: a Meter
// (track + tone fill + label/value), a kicker divider, plain text lines, and a
// dashed empty-state panel.

#include <font/FontRenderer.h>
#include <graphics/Color.h>
#include <graphics/PrimitiveStyles.h>
#include <graphics/Rect.h>
#include <primitives/Primitives.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>

#include <string>

namespace world_sim::tabs {

// Draw one line of UI-font text at an absolute point.
inline void drawText(const std::string& text, Foundation::Vec2 pos, float fontSize, const Foundation::Color& color, Renderer::FontFamily font = UI::fontUi) {
	Renderer::Primitives::drawText({.text = text, .position = pos, .scale = fontSize / 16.0F, .color = color, .font = font});
}

// Measure text width in pixels (0 if no font renderer is available).
inline float measureText(const std::string& text, float fontSize, Renderer::FontFamily font = UI::fontUi) {
	if (const ui::FontRenderer* fonts = Renderer::Primitives::getFontRenderer(); fonts != nullptr) {
		return fonts->MeasureText(text, fontSize / 16.0F, font).x;
	}
	return 0.0F;
}

// A Salvage Meter: a label (upper-left) + value text (upper-right) over a rounded
// track with a tone-colored fill. Mirrors Meter.tsx. Returns the y past the meter.
inline float drawMeter(
	float x, float y, float width, const std::string& label, float value01, const std::string& valueText, const Foundation::Color& tone, float labelAlpha = 1.0F
) {
	constexpr float kLabelPx = UI::fs_2xs;
	constexpr float kTrackH	 = 6.0F;
	constexpr float kGap	 = 4.0F; // gap between label row and track

	const bool hasLabelRow = !label.empty() || !valueText.empty();
	float	   cursorY	   = y;

	if (hasLabelRow) {
		if (!label.empty()) {
			Renderer::Primitives::drawText({
				.text		   = label,
				.position	   = {x, cursorY},
				.scale		   = kLabelPx / 16.0F,
				.color		   = UI::withAlpha(UI::text_dim, labelAlpha),
				.font		   = UI::fontMono,
				.letterSpacing = kLabelPx * UI::ls_wider,
				.transform	   = Foundation::TextTransform::Uppercase,
			});
		}
		if (!valueText.empty()) {
			Renderer::Primitives::drawText({
				.text	  = valueText,
				.position = {x, cursorY},
				.scale	  = kLabelPx / 16.0F,
				.color	  = UI::withAlpha(tone, labelAlpha),
				.font	  = UI::fontMono,
				.hAlign	  = Foundation::HorizontalAlign::Right,
				.boxWidth = width,
			});
		}
		cursorY += kLabelPx + kGap;
	}

	// Track.
	Renderer::Primitives::drawRect({
		.bounds = {x, cursorY, width, kTrackH},
		.style	= {.fill = UI::bg_inset, .border = Foundation::BorderStyle{.color = UI::line_hairline, .width = UI::bw, .cornerRadius = UI::r_pill, .position = Foundation::BorderPosition::Inside}},
	});

	// Fill.
	const float clamped	  = value01 < 0.0F ? 0.0F : (value01 > 1.0F ? 1.0F : value01);
	const float fillWidth = width * clamped;
	if (fillWidth > 0.5F) {
		Renderer::Primitives::drawRect({
			.bounds = {x, cursorY, fillWidth, kTrackH},
			.style	= {.fill = UI::withAlpha(tone, labelAlpha), .border = Foundation::BorderStyle{.color = UI::withAlpha(tone, labelAlpha), .width = 0.0F, .cornerRadius = UI::r_pill, .position = Foundation::BorderPosition::Inside}},
		});
	}

	return cursorY + kTrackH;
}

// A kicker divider: a small uppercase mono label with a hairline running to the
// right edge. Mirrors Divider.tsx. Returns the y past the divider.
inline float drawDivider(float x, float y, float width, const std::string& label) {
	constexpr float kLabelPx = UI::fs_2xs;
	const float		labelW	 = measureText(label, kLabelPx, UI::fontMono) + (label.empty() ? 0.0F : UI::space_2);

	Renderer::Primitives::drawText({
		.text		   = label,
		.position	   = {x, y},
		.scale		   = kLabelPx / 16.0F,
		.color		   = UI::text_faint,
		.font		   = UI::fontMono,
		.letterSpacing = kLabelPx * UI::ls_wider,
		.transform	   = Foundation::TextTransform::Uppercase,
	});

	const float lineY = y + (kLabelPx * 0.5F);
	if (labelW < width) {
		Renderer::Primitives::drawLine({.start = {x + labelW, lineY}, .end = {x + width, lineY}, .style = {.color = UI::line_hairline, .width = UI::bw}});
	}

	return y + kLabelPx + UI::space_2;
}

// A dashed empty-state panel: a bg_inset rounded rect with a hairline border, a
// title, and a subtitle. Mirrors the dossier empty states. (Borders are solid;
// the renderer has no dashed stroke, so a faint hairline stands in.)
inline void drawEmptyState(const Foundation::Rect& bounds, const std::string& title, const std::string& subtitle) {
	Renderer::Primitives::drawRect({
		.bounds = bounds,
		.style	= {.fill = UI::withAlpha(UI::bg_inset, 0.5F), .border = Foundation::BorderStyle{.color = UI::line_hairline, .width = UI::bw, .cornerRadius = UI::r_md, .position = Foundation::BorderPosition::Inside}},
	});

	const float pad = UI::space_3;
	float		ty	= bounds.y + pad;
	Renderer::Primitives::drawText({.text = title, .position = {bounds.x + pad, ty}, .scale = UI::fs_sm / 16.0F, .color = UI::text, .font = UI::fontUi});
	ty += UI::fs_sm + 4.0F;
	Renderer::Primitives::drawText({.text = subtitle, .position = {bounds.x + pad, ty}, .scale = UI::fs_xs / 16.0F, .color = UI::text_faint, .font = UI::fontUi});
}

} // namespace world_sim::tabs
