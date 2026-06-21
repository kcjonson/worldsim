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
inline float measureText(const std::string& text, float fontSize, Renderer::FontFamily font = UI::fontUi, float letterSpacing = 0.0F) {
	if (const ui::FontRenderer* fonts = Renderer::Primitives::getFontRenderer(); fonts != nullptr) {
		return fonts->MeasureText(text, fontSize / 16.0F, font, letterSpacing).x;
	}
	return 0.0F;
}

// The C++ text renderer lays glyphs slightly wider than MeasureText reports, so wrap
// and fit decisions inflate the measured width to stay clear of the box edge.
inline constexpr float kRenderWidthFudge = 1.12F;
inline constexpr float kWrapLineGap		 = 4.0F; // added to fontSize for line height

// Number of lines `text` wraps into at maxWidth (always >= 1).
inline int wrappedLineCount(const std::string& text, float fontSize, float maxWidth, Renderer::FontFamily font = UI::fontUi) {
	const ui::FontRenderer* fonts = Renderer::Primitives::getFontRenderer();
	if (fonts == nullptr || text.empty()) return 1;
	int			lines = 0;
	std::string line;
	size_t		i = 0;
	while (i < text.size()) {
		const size_t start = i;
		while (i < text.size() && text[i] != ' ') ++i;
		std::string word = text.substr(start, i - start);
		while (i < text.size() && text[i] == ' ') ++i;
		std::string candidate = line.empty() ? word : line + " " + word;
		if (fonts->MeasureText(candidate, fontSize / 16.0F, font).x * kRenderWidthFudge > maxWidth && !line.empty()) {
			++lines;
			line = word;
		} else {
			line = candidate;
		}
	}
	if (!line.empty()) ++lines;
	return lines < 1 ? 1 : lines;
}

// Word-wrap text to maxWidth, drawing each line; returns the y past the last line.
inline float drawWrapped(const std::string& text, Foundation::Vec2 pos, float fontSize, float maxWidth, const Foundation::Color& color, Renderer::FontFamily font = UI::fontUi) {
	const float lineH = fontSize + kWrapLineGap;
	const ui::FontRenderer* fonts = Renderer::Primitives::getFontRenderer();
	if (fonts == nullptr || text.empty()) {
		drawText(text, pos, fontSize, color, font);
		return pos.y + lineH;
	}

	std::string line;
	float		y = pos.y;
	size_t		i = 0;
	while (i < text.size()) {
		const size_t start = i;
		while (i < text.size() && text[i] != ' ') ++i;
		std::string word = text.substr(start, i - start);
		while (i < text.size() && text[i] == ' ') ++i; // consume separators

		std::string candidate = line.empty() ? word : line + " " + word;
		if (fonts->MeasureText(candidate, fontSize / 16.0F, font).x * kRenderWidthFudge > maxWidth && !line.empty()) {
			drawText(line, {pos.x, y}, fontSize, color, font);
			y += lineH;
			line = word;
		} else {
			line = candidate;
		}
	}
	if (!line.empty()) {
		drawText(line, {pos.x, y}, fontSize, color, font);
		y += lineH;
	}
	return y;
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
	const float		labelW	 = measureText(label, kLabelPx, UI::fontMono, kLabelPx * UI::ls_wider) + (label.empty() ? 0.0F : UI::space_2);

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
// Returns the y past the panel. The box auto-grows to fit the wrapped subtitle, so
// callers only need to supply a width and top-left (bounds.height is a minimum).
inline float drawEmptyState(const Foundation::Rect& bounds, const std::string& title, const std::string& subtitle) {
	const float pad		= UI::space_3;
	const float innerW	= bounds.width - pad * 2.0F;
	const float subLineH = UI::fs_xs + kWrapLineGap;
	const int	subLines = wrappedLineCount(subtitle, UI::fs_xs, innerW);
	const float needed	= pad + UI::fs_sm + 4.0F + static_cast<float>(subLines) * subLineH + pad;
	const float boxH	= needed > bounds.height ? needed : bounds.height;

	Renderer::Primitives::drawRect({
		.bounds = {bounds.x, bounds.y, bounds.width, boxH},
		.style	= {.fill = UI::withAlpha(UI::bg_inset, 0.5F), .border = Foundation::BorderStyle{.color = UI::line_hairline, .width = UI::bw, .cornerRadius = UI::r_md, .position = Foundation::BorderPosition::Inside}},
	});

	float ty = bounds.y + pad;
	Renderer::Primitives::drawText({.text = title, .position = {bounds.x + pad, ty}, .scale = UI::fs_sm / 16.0F, .color = UI::text, .font = UI::fontUi});
	ty += UI::fs_sm + 4.0F;
	// Wrap the subtitle within the panel so it never runs past the edge.
	drawWrapped(subtitle, {bounds.x + pad, ty}, UI::fs_xs, innerW, UI::text_faint);
	return bounds.y + boxH;
}

} // namespace world_sim::tabs
