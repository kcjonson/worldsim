#include "components/progress/ProgressBar.h"

#include "font/FontRenderer.h"
#include "graphics/Color.h"
#include "graphics/PrimitiveStyles.h"
#include "graphics/Rect.h"
#include "primitives/Primitives.h"
#include "theme/Tokens.h"

#include <algorithm>

namespace UI {

	namespace {

		// drawText scale is relative to a 16px base.
		constexpr float kTextBasePx = 16.0F;

		float textScale(float sizePx) { return sizePx / kTextBasePx; }

		// Stacked track height by size (md=8px, sm=5px).
		float trackHeight(Size size) { return size == Size::Sm ? 5.0F : 8.0F; }

		// Inline-variant track height (18px, 16px at sm).
		float inlineHeight(Size size) { return size == Size::Sm ? 16.0F : 18.0F; }

		// Segment rhythm: 7px segment, 2px gutter.
		constexpr float kSegmentWidth = 7.0F;
		constexpr float kSegmentGap = 2.0F;

		float measureWidth(const std::string& text, float scale) {
			if (const ui::FontRenderer* font = Renderer::Primitives::getFontRenderer(); font != nullptr) {
				return font->MeasureText(text, scale).x;
			}
			return 0.0F;
		}

		// Scale a color's RGB toward black, keeping alpha. Gives the fill a subtle
		// top-bright -> bottom-darker gradient without inventing new tokens.
		Foundation::Color darken(Foundation::Color color, float factor) {
			return {color.r * factor, color.g * factor, color.b * factor, color.a};
		}

		// Inline text with a dark text-shadow, so labels stay legible over both the
		// filled and empty track (the shadow is a style prop, not a double draw).
		void drawShadowedText(
			const std::string&		  text,
			Foundation::Vec2		  pos,
			float					  scale,
			Foundation::Color		  color,
			const char*				  id,
			float					  letterSpacing = 0.0F,
			Foundation::TextTransform transform = Foundation::TextTransform::None
		) {
			Renderer::Primitives::drawText({.text = text,
											.position = pos,
											.scale = scale,
											.color = color,
											.font = fontMono,
											.shadowColor = bg_void,
											.shadowOffset = {1.0F, 1.0F},
											.letterSpacing = letterSpacing,
											.transform = transform,
											.id = id});
		}

	} // namespace

	ProgressBar::ProgressBar(const Args& args)
		: value(std::clamp(args.value, 0.0F, 1.0F))
		, width(args.width)
		, tone(args.tone)
		, label(args.label)
		, valueText(args.valueText)
		, sizeVariant(args.size)
		, segmented(args.segmented)
		, inlineLabel(args.inlineLabel) {
		position = args.position;
		margin = args.margin;
		syncSize();
	}

	void ProgressBar::syncSize() {
		float height = 0.0F;
		if (inlineLabel) {
			height = inlineHeight(sizeVariant);
		} else {
			height = trackHeight(sizeVariant);
			if (!label.empty() || !valueText.empty()) {
				height += fs_xs + space_1; // header text + column gap
			}
		}
		size = {width, height};
	}

	void ProgressBar::setValue(float newValue) { value = std::clamp(newValue, 0.0F, 1.0F); }

	void ProgressBar::setPosition(Foundation::Vec2 newPos) { position = newPos; }

	void ProgressBar::setWidth(float newWidth) {
		width = newWidth;
		syncSize();
	}

	void ProgressBar::render() {
		using Renderer::Primitives::drawRect;
		using Renderer::Primitives::drawText;

		const Foundation::Color toneCol = toneColor(tone, value);

		if (inlineLabel) {
			const float			   height = inlineHeight(sizeVariant);
			const Foundation::Rect track{position.x, position.y, width, height};

			drawRect({.bounds = track,
					  .style = {.fill = bg_inset,
								.border = Foundation::BorderStyle{.color = line_hairline,
																  .width = bw,
																  .cornerRadius = r_sm,
																  .position = Foundation::BorderPosition::Inside}},
					  .id = "pb_track"});

			const float fillWidth = value * width;

			// Dim wash fill with a bright leading edge. Subtle vertical gradient
			// (brighter at the top, settling to the dim tone wash below) plus a soft
			// tone glow behind it as an inline box-shadow.
			drawRect({.bounds = {track.x, track.y, fillWidth, height},
					  .style = {.fill = withAlpha(toneCol, 0.22F),
								.gradient = Foundation::LinearGradient{
									.from = withAlpha(toneCol, 0.30F), .to = withAlpha(darken(toneCol, 0.85F), 0.18F), .horizontal = false},
								.boxShadow = Foundation::BoxShadow{.color = withAlpha(toneCol, 0.35F), .blur = 8.0F, .spread = 0.0F, .offset = {0.0F, 0.0F}}},
					  .id = "pb_fill"});
			if (fillWidth >= bw_thick) {
				drawRect({.bounds = {track.x + fillWidth - bw_thick, track.y, bw_thick, height},
						  .style = {.fill = toneCol},
						  .id = "pb_edge"});
			}

			// Label left, value right, both inside the bar, with a dark text-shadow.
			const float scale = textScale(fs_2xs);
			const float pad = space_2;
			const float textY = track.y + ((height - fs_2xs) * 0.5F);

			if (!label.empty()) {
				drawShadowedText(label, {track.x + pad, textY}, scale, text_bright, "pb_label", fs_2xs * ls_wider, Foundation::TextTransform::Uppercase);
			}
			if (!valueText.empty()) {
				const float valueWidth = measureWidth(valueText, scale);
				drawShadowedText(valueText, {track.right() - pad - valueWidth, textY}, scale, toneCol, "pb_value");
			}
			return;
		}

		// Stacked layout: optional header row, then the track below.
		float trackTop = position.y;

		const bool hasHeader = !label.empty() || !valueText.empty();
		if (hasHeader) {
			const float scale = textScale(fs_xs);
			if (!label.empty()) {
				drawText({.text = label,
						  .position = {position.x, position.y},
						  .scale = scale,
						  .color = text_dim,
						  .font = fontMono,
						  .letterSpacing = fs_xs * ls_wider,
						  .transform = Foundation::TextTransform::Uppercase,
						  .id = "pb_label"});
			}
			if (!valueText.empty()) {
				const float valueWidth = measureWidth(valueText, scale);
				drawText({.text = valueText,
						  .position = {position.x + width - valueWidth, position.y},
						  .scale = scale,
						  .color = toneCol,
						  .font = fontMono,
						  .id = "pb_value"});
			}
			trackTop += fs_xs + space_1; // header text + column gap
		}

		const float			   height = trackHeight(sizeVariant);
		const float			   radius = height * 0.5F; // full pill
		const Foundation::Rect track{position.x, trackTop, width, height};

		drawRect({.bounds = track,
				  .style = {.fill = bg_inset,
							.border = Foundation::BorderStyle{
								.color = line_hairline, .width = bw, .cornerRadius = radius, .position = Foundation::BorderPosition::Inside}},
				  .id = "pb_track"});

		const float fillWidth = value * width;
		if (fillWidth > 0.0F) {
			drawRect({.bounds = {track.x, trackTop, fillWidth, height},
					  .style = {.fill = toneCol,
								.border = Foundation::BorderStyle{
									.color = toneCol, .width = 0.0F, .cornerRadius = radius, .position = Foundation::BorderPosition::Inside},
								.gradient = Foundation::LinearGradient{.from = toneCol, .to = darken(toneCol, 0.78F), .horizontal = false},
								.boxShadow = Foundation::BoxShadow{.color = withAlpha(toneCol, 0.35F), .blur = 8.0F, .spread = 0.0F, .offset = {0.0F, 0.0F}}},
					  .id = "pb_fill"});
		}

		// Segmented notches: bg_panel gutters laid over the track on the 7+2 rhythm.
		if (segmented) {
			float x = track.x + kSegmentWidth;
			while (x < track.right()) {
				const float gapWidth = std::min(kSegmentGap, track.right() - x);
				drawRect({.bounds = {x, trackTop, gapWidth, height}, .style = {.fill = bg_panel}, .id = "pb_notch"});
				x += kSegmentWidth + kSegmentGap;
			}
		}
	}

} // namespace UI
